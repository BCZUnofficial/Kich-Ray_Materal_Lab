// SoC/Kich-Ray material inspection: R2 normal unpack, material LUT and direct light.
// Neutral ambient, no level cubemaps, shadows, exposure adaptation or bloom.
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
    float4 lighting; // x: R2 material slice, y: gloss enabled, zw: authored emission
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
float2 materialResponse(float ld,float lh){
    float2 texel=clamp(float2(ld*128,lh*256)-.5,float2(0,0),float2(127,255));
    float material=frac(lighting.x*.25)*4;
    float slice=floor(material),blend=frac(material);
    return lerp(materialSlice(texel,slice),materialSlice(texel,slice<3?slice+1:0),blend);
}
float4 PSMain(Out i,bool front:SV_IsFrontFace):SV_Target {
    float4 albedo=DiffuseMap.Sample(LinearWrap,i.uv);float gloss=2.0/255.0;float3 N=safeNormal(i.n,float3(0,0,1));
    float opacity=1;
    if(alphaSettings.x>0.5){
        opacity=albedo.a;
        clip(opacity-(alphaSettings.x<1.5?max(alphaSettings.y,1.0/255.0):1.0/255.0));
        if(alphaSettings.x<1.5)opacity=1;
    }
    if(parameters.x>0.5){
        float4 packed=BumpMap.Sample(LinearWrap,i.uv);float4 corr=BumpErrorMap.Sample(LinearWrap,i.uv);
        float3 tn=float3(packed.a,packed.b,packed.g)+corr.rgb-1.0;
        tn.xy*=max(parameters.y,0.0);if(parameters.w>0.5)tn.y=-tn.y;tn.z*=0.5;tn=safeNormal(tn,float3(0,0,1));
        float3 fallbackT=safeNormal(cross(abs(N.y)<0.9?float3(0,1,0):float3(1,0,0),N),float3(1,0,0));
        float3 T=safeNormal(i.t-dot(i.t,N)*N,fallbackT);
        float3 B=safeNormal(i.b-dot(i.b,N)*N-dot(i.b,T)*T,cross(N,T));
        N=safeNormal(T*tn.x+B*tn.y+N*tn.z,N);gloss=saturate(packed.r*packed.r*max(parameters.z*2.0,0.0));
    }
    if(!front)N=-N;
    gloss*=lighting.y;
    float3 L=safeNormal(lightDirection.xyz,float3(0,1,0));
    float3 V=safeNormal(eyePosition.xyz-i.wp,N),H=safeNormal(L+V,N);
    float2 response=materialResponse(dot(N,L),dot(N,H));
    // R2 u_diffuse2s(): mean radiance ^ (2/3) below one. r2_gloss_factor=1.
    float energy=max(0,dot(lightColor.rgb,float3(1.0/3,1.0/3,1.0/3)));
    float specularEnergy=energy<1?pow(energy,2.0/3):energy;
    float3 diffuse=ambientColor.rgb+lightColor.rgb*response.x+lighting.zzz;
    float specular=(response.y*specularEnergy+lighting.w)*gloss;
    // combine_1.ps: albedo * diffuse + replicated scalar specular.
    // A black gloss map contributes exactly zero specular; there is no fake glow.
    float3 result=albedo.rgb*materialTint.rgb*diffuse+specular.xxx;
    return float4(saturate(result),opacity);
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
