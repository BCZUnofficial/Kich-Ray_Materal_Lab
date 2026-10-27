// KichRay DX11/R4 material inspection, reference 8f34fb7a0fbf8c5633494545130153031a78f953.
// Native UNORM maps, authored TBN, GBuffer material precision and hmodel lighting.
// Environment is a uniform cubemap sample; level cubemaps, shadows and bloom are not loaded.
cbuffer Preview : register(b0) {
    row_major float4x4 worldViewProjection;
    row_major float4x4 worldMatrix;
    float4 eyePosition;
    float4 lightDirection;
    float4 lightColor;
    float4 ambientColor;
    float4 materialTint;
    float4 parameters; // x bump, y normal strength, z gloss preview, w flip Y
    float4 outline;    // rgb outline color, w width in pixels
    float4 viewport;   // xy inverse viewport size
    float4 alphaSettings; // x: opaque/mask/blend, y: alpha-test cutoff
    float4 lighting; // x: material class, y: gloss enabled, zw: authored emission
    float4 environment; // rgb: uniform environment sample, w: ROS/hemi visibility
    float4 lightingOptions; // x: fixed exposure, y: r2_gloss_factor, z: gloss map present
};
Texture2D DiffuseMap : register(t0);
Texture2D BumpMap : register(t1);
Texture2D BumpErrorMap : register(t2);
Texture2D MaterialLut : register(t3);
SamplerState LinearWrap : register(s0);
SamplerState MaterialClamp : register(s1);
struct In {float3 pos:POSITION;float3 norm:NORMAL;float2 uv:TEXCOORD;float3 tangent:TANGENT;float3 bitangent:BINORMAL;};
struct Out {float4 pos:SV_Position;float3 wp:TEXCOORD0;float3 n:TEXCOORD1;float2 uv:TEXCOORD2;float3 t:TEXCOORD3;float3 b:TEXCOORD4;};
float3 safeNormal(float3 v,float3 fallback){
    float lengthSquared=dot(v,v);
    return lengthSquared>1e-10?v*rsqrt(max(lengthSquared,1e-10)):fallback;
}
Out VSMain(In i){
    Out o;
    float4 wp=mul(float4(i.pos,1),worldMatrix);
    o.pos=mul(float4(i.pos,1),worldViewProjection);
    o.wp=wp.xyz;
    o.n=safeNormal(mul(float4(i.norm,0),worldMatrix).xyz,float3(0,0,1));
    o.uv=i.uv;
    o.t=mul(float4(i.tangent,0),worldMatrix).xyz;
    o.b=mul(float4(i.bitangent,0),worldMatrix).xyz;
    return o;
}
float2 materialSlice(float2 texel,float slice){
    float4 response=MaterialLut.SampleLevel(MaterialClamp,(texel+float2(.5,.5+slice*256))/float2(128,1024),0);
    return float2(response.r,response.a);
}
float2 materialResponse(float ld,float lh,float material){
    float2 texel=clamp(float2(ld*128,lh*256)-.5,float2(0,0),float2(127,255));
    float slice=floor(material),blend=material-slice;
    slice-=4*floor(slice*.25);
    float2 response=materialSlice(texel,slice);
    if(blend>0)response=lerp(response,materialSlice(texel,slice<3?slice+1:0),blend);
    return response;
}
float4 PSMain(Out i,bool front:SV_IsFrontFace):SV_Target {
    float4 albedo=DiffuseMap.Sample(LinearWrap,i.uv);
    float sourceGloss=2.0/255.0;float3 N=safeNormal(i.n,float3(0,0,1));
    float opacity=1;
    if(alphaSettings.x>0.5){
        opacity=albedo.a;
        clip(opacity-(alphaSettings.x<1.5?max(alphaSettings.y,1.0/255.0):1.0/255.0));
        if(alphaSettings.x<1.5)opacity=1;
    }
    // R4 permits gloss-only materials even when their normal basis is absent
    // or normal preview is disabled. Gloss is not an emission channel.
    float4 packed=0;
    if(lightingOptions.z>0.5){
        packed=BumpMap.Sample(LinearWrap,i.uv);
        sourceGloss=saturate(packed.r*packed.r*max(parameters.z*2.0,0.0));
    }
    if(parameters.x>0.5){
        float4 corr=BumpErrorMap.Sample(LinearWrap,i.uv);
        float3 mapped=packed.wzy+corr.rgb-1.0;
        mapped.xy*=max(parameters.y,0.0);if(parameters.w>0.5)mapped.y=-mapped.y;mapped.z*=0.5;
        float3 fallbackT=safeNormal(cross(abs(N.y)<0.9?float3(0,1,0):float3(1,0,0),N),float3(1,0,0));
        float3 T=safeNormal(i.t-dot(i.t,N)*N,fallbackT);
        float handed=dot(cross(N,T),i.b);
        float3 B=safeNormal(cross(N,T),float3(0,1,0))*(handed<0?-1:1);
        N=safeNormal(T*mapped.x+B*mapped.y+N*mapped.z,N);
    }
    if(!front)N=-N;
    sourceGloss*=lighting.y;
    float material=clamp(lighting.x,0,3),gloss=sourceGloss;
    bool forward=alphaSettings.x>1.5;
    if(!forward){
        // R4 PackMaterial / MaterialGloss / MaterialClass. Opaque and cutout
        // surfaces travel through this GBuffer; forward alpha does not.
        gloss=floor(saturate(gloss)*127+.5)/127;
        material=floor(material*5+.5)*.2;
    }
    float3 L=safeNormal(lightDirection.xyz,float3(0,1,0));
    // R4 divides by max(length, epsilon): a cancelled half-vector stays zero.
    // Substituting N here invents a full-strength highlight for opposing L/V.
    float3 V=safeNormal(eyePosition.xyz-i.wp,float3(0,0,0)),H=safeNormal(L+V,float3(0,0,0));
    float2 response=materialResponse(dot(N,L),dot(N,H),material);
    float energy=max(0,dot(lightColor.rgb,float3(1.0/3,1.0/3,1.0/3)));
    float specularEnergy=lightingOptions.y*(energy<1?pow(energy,2.0/3):energy);
    float3 diffuse=lightColor.rgb*response.x+lighting.zzz;
    float3 specular=(response.y*specularEnergy*gloss+lighting.w*sourceGloss).xxx;
    float h=saturate(environment.w);
    if(forward){
        // PSForwardSun / ForwardIndirect: source hemi already encodes
        // visibility. No extra world-up factor and no cubemap square here.
        diffuse+=ambientColor.rgb+environment.rgb*materialResponse(h,0,material).x;
    }else{
        // R4 Hemisphere / SoC hmodel. A uniform cube still retains the
        // material/angle-dependent reflected specular term and squared env.
        float3 v2point=-V,reflected=reflect(v2point,N);
        float2 hemi=materialResponse(h,.5+.5*dot(reflected,v2point),material);
        float3 envSquared=environment.rgb*environment.rgb;
        diffuse+=envSquared*hemi.x+ambientColor.rgb;
        specular+=envSquared*hemi.y*gloss;
    }
    float3 result=albedo.rgb*materialTint.rgb*diffuse+specular;
    return float4(saturate(result*lightingOptions.x),opacity);
}
struct OutlineOut {float4 pos:SV_Position;float2 uv:TEXCOORD0;};
OutlineOut VSOutline(In i){
    OutlineOut o;
    o.uv=i.uv;
    o.pos=mul(float4(i.pos,1),worldViewProjection);
    // Project a direction, then differentiate the perspective divide. This
    // keeps the silhouette about two pixels wide at every zoom level.
    float4 n=mul(float4(normalize(i.norm),0),worldViewProjection);
    float2 direction=n.xy*o.pos.w-o.pos.xy*n.w;
    float2 pixelDirection=direction/max(viewport.xy,float2(1e-6,1e-6));
    float lengthSquared=dot(pixelDirection,pixelDirection);
    if(lengthSquared>1e-10)
        o.pos.xy+=pixelDirection*rsqrt(lengthSquared)*viewport.xy*(2*outline.w)*o.pos.w;
    return o;
}
float4 PSOutline(OutlineOut i):SV_Target{
    if(alphaSettings.x>0.5)
        clip(DiffuseMap.Sample(LinearWrap,i.uv).a-(alphaSettings.x<1.5?max(alphaSettings.y,1.0/255.0):0.05));
    return float4(outline.rgb,1);
}
struct BoneIn {float3 pos:POSITION;float4 color:COLOR;};
struct BoneOut {float4 pos:SV_Position;float4 color:COLOR;};
BoneOut VSSkeleton(BoneIn i){BoneOut o;o.pos=mul(float4(i.pos,1),worldViewProjection);o.color=i.color;return o;}
float4 PSSkeleton(BoneOut i):SV_Target{return i.color;}
