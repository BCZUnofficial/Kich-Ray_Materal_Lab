// R2-inspired preview on D3D11. Bump channel unpack matches SoC r2/sload.h.
cbuffer Preview : register(b0) {
    row_major float4x4 worldViewProjection;
    float4 eyePosition;
    float4 lightDirection;
    float4 lightColor;
    float4 ambientColor;
    float4 materialTint;
    float4 parameters; // x bump, y normal strength, z gloss preview, w flip Y
    float4 outline;    // rgb outline color, w extrusion
};
Texture2D DiffuseMap : register(t0);
Texture2D BumpMap : register(t1);
Texture2D BumpErrorMap : register(t2);
SamplerState LinearWrap : register(s0);
struct In {float3 pos:POSITION;float3 norm:NORMAL;float2 uv:TEXCOORD;float3 tangent:TANGENT;float3 bitangent:BINORMAL;};
struct Out {float4 pos:SV_Position;float3 wp:TEXCOORD0;float3 n:TEXCOORD1;float2 uv:TEXCOORD2;float3 t:TEXCOORD3;float3 b:TEXCOORD4;};
Out VSMain(In i){Out o;o.pos=mul(float4(i.pos,1),worldViewProjection);o.wp=i.pos;o.n=i.norm;o.uv=i.uv;o.t=i.tangent;o.b=i.bitangent;return o;}
float4 PSMain(Out i):SV_Target {
    float4 albedo=DiffuseMap.Sample(LinearWrap,i.uv);float gloss=0.25;float3 N=normalize(i.n);
    if(parameters.x>0.5){
        float4 packed=BumpMap.Sample(LinearWrap,i.uv);float4 corr=BumpErrorMap.Sample(LinearWrap,i.uv);
        float3 tn=float3(packed.a,packed.b,packed.g)+corr.rgb-1.0;
        tn.xy*=max(parameters.y,0.0);if(parameters.w>0.5)tn.y=-tn.y;tn.z*=0.5;tn=normalize(tn);
        float3 T=normalize(i.t-dot(i.t,N)*N);float3 B=normalize(i.b-dot(i.b,N)*N-dot(i.b,T)*T);if(dot(B,B)<0.1)B=normalize(cross(N,T));
        N=normalize(T*tn.x+B*tn.y+N*tn.z);gloss=saturate(packed.r*packed.r*max(parameters.z*2.0,0.0));
    }
    float3 L=normalize(lightDirection.xyz);float3 V=normalize(eyePosition.xyz-i.wp);float nl=saturate(dot(N,L));float hemi=saturate(N.y*0.5+0.5);
    float3 diffuse=ambientColor.rgb*(0.65+0.35*hemi)+lightColor.rgb*nl;float3 H=normalize(L+V);
    float spec=pow(saturate(dot(N,H)),8+gloss*72)*(0.15+0.85*gloss)*nl;
    float3 result=albedo.rgb*materialTint.rgb*diffuse+lightColor.rgb*spec*0.85;
    return float4(saturate(result),1);
}
struct OutlineOut {float4 pos:SV_Position;};
OutlineOut VSOutline(In i){OutlineOut o;float3 p=i.pos+normalize(i.norm)*outline.w;o.pos=mul(float4(p,1),worldViewProjection);return o;}
float4 PSOutline(OutlineOut i):SV_Target{return float4(outline.rgb,1);}
struct BoneIn {float3 pos:POSITION;float4 color:COLOR;};
struct BoneOut {float4 pos:SV_Position;float4 color:COLOR;};
BoneOut VSSkeleton(BoneIn i){BoneOut o;o.pos=mul(float4(i.pos,1),worldViewProjection);o.color=i.color;return o;}
float4 PSSkeleton(BoneOut i):SV_Target{return i.color;}
