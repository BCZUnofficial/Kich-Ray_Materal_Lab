#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>
namespace lab {
struct Pixel {uint8_t r=255,g=255,b=255,a=255;};
struct Image {uint32_t w=0,h=0;std::vector<Pixel> pixels; bool valid()const{return w>0&&h>0&&pixels.size()==size_t(w)*h;} };
struct GrayImage {uint32_t w=0,h=0;std::vector<uint8_t> pixels; bool valid()const{return w>0&&h>0&&pixels.size()==size_t(w)*h;} };
struct V3 {float x=0,y=0,z=0;};
struct Quat {float x=0,y=0,z=0,w=1;};
struct Vertex {
    V3 p,n;float u=0,v=0;
    uint16_t bone0=0,bone1=0;
    float weight=0.f; // 2-link: lerp(bone0, bone1, weight)
    uint8_t links=0; // 0 static, 1 or 2 skeletal
    // Skeletal OGF exports its tangent basis; keep it for engine-matching normals.
    V3 tangent{},binormal{};
    bool hasTangentBasis=false;
};
struct MeshPart {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::string texture;
    std::string shader;
    bool skeletal=false;
    bool valid()const{return !vertices.empty()&&!indices.empty();}
};
struct Bone {
    std::string name;
    std::string parentName;
    int32_t parent=-1;
    V3 position{};       // model-space bind/rest origin
    V3 localRotate{};
    V3 localTranslate{};
    std::array<float,16> inverseBind{}; // row-major X-Ray affine matrix
    bool hasBind=false;
};
struct MotionTrack {
    std::vector<Quat> rotations;     // one key when rotation is constant
    std::vector<V3> translations;    // one key when translation is constant
};
struct Motion {
    std::string name;
    uint32_t frames=0;
    float fps=30.f;
    float speed=1.f;
    bool stopAtEnd=false;
    std::vector<MotionTrack> tracks; // indexed by model bone id
    float duration()const{return fps>0.f?float(frames)/fps:0.f;}
};
struct Mesh {
    std::vector<MeshPart> parts;
    std::vector<Bone> bones;
    std::vector<Motion> motions;
    std::vector<std::string> motionRefs;
    bool skeletalSource=false;
    uint32_t sourceParts=0;
    bool valid()const{for(const auto& p:parts)if(p.valid())return true;return false;}
    size_t vertexCount()const{size_t n=0;for(const auto& p:parts)n+=p.vertices.size();return n;}
    size_t indexCount()const{size_t n=0;for(const auto& p:parts)n+=p.indices.size();return n;}
};
struct Settings {float normalStrength=1.f,gloss=0.45f,height=0.5f,blur=0.12f,sharpness=0.35f;bool invertY=false;};
// Relative to an immutable, committed layer snapshot; identity changes no bytes.
struct MaterialAdjustments {
    float normalStrength=1.f,gloss=1.f,height=1.f,blur=0.f,sharpness=0.f;
    bool invertY=false,heightFromDiffuse=false;
    float diffuseHeightStrength=.25f;
};
GrayImage MakeDiffuseHeightLift(const Image& diffuse,uint32_t width,uint32_t height);
Settings MinimalBaseSettings();
enum class SurfacePreset { Glass, Fabric, Metal, Concrete, Leather };
// A texel-aligned stamp derived from the diffuse around the hit UV. Its detail
// scale stays fixed in texture space when the brush radius changes.
Image MakeSurfaceReliefPatch(const Image& diffuse,SurfacePreset kind,uint32_t width,uint32_t height,float u,float v,float radiusPixels);
struct Sources {Image diffuse,normal,gloss,height;};
struct BumpPair {Image bump,bumpX;};
struct EncodedBumpPair {std::vector<uint8_t> bump,bumpX;};
// The viewport and disk export consume the same compressed bytes and mip chain.
bool EncodePackedBumpPair(const BumpPair& pair,EncodedBumpPair& output,std::string& error);
std::vector<Image> DecodeDDSMipChain(const std::vector<uint8_t>& bytes,std::string& error);
struct PartImages {Image diffuse,bump,bumpX;bool hasBump=false;};
Image DefaultChecker(uint32_t size=256);
Image DecodeDDS(const std::vector<uint8_t>& bytes,std::string& error);
std::vector<uint8_t> EncodeBC3DDS(const Image& rgba,const std::vector<Image>& mipImages);
Image DecompressBC3Image(const std::vector<uint8_t>& blocks,uint32_t width,uint32_t height);
Image ResizeImageMax(const Image& src,uint32_t maxDimension);
Image MakeDefaultNormalPreset(uint32_t size=128);
// Layer normals use the pre-SoC-boost basis; rendered=true applies the engine z*=0.5.
V3 DecodeBumpNormal(Pixel packed,Pixel correction,bool rendered=false);
Image UnpackNormalMap(const Image& bump,const Image& bumpX);
Image UnpackGlossMap(const Image& bump);
Image UnpackHeightMap(const Image& bumpX);
GrayImage UnpackGlossGray(const Image& bump);
GrayImage UnpackHeightGray(const Image& bumpX);
GrayImage ToGrayImage(const Image& src,uint32_t maxDimension=0);
Image GrayToImage(const GrayImage& src,uint32_t maxDimension=0);
void StampNormalPreset(Image& target,const Image& preset,float u,float v,float radiusPixels,float opacity);
void StampNormalPresetLayer(Image& target,GrayImage& coverage,const Image& preset,float u,float v,float radiusPixels);
void StampScalarPreset(Image& target,const Image& preset,float u,float v,float radiusPixels,float value);
void StampScalarPreset(GrayImage& target,const Image& preset,float u,float v,float radiusPixels,float value);
void StampScalarPresetLayer(GrayImage& target,GrayImage& coverage,const Image& preset,float u,float v,float radiusPixels,float value);
bool StampNormalReliefLayer(Image& target,const Image& base,GrayImage& coverage,const Image& preset,float u,float v,float radiusPixels,float strength,bool remove,float hardness,bool texelAligned=false);
bool StampScalarDeltaLayer(GrayImage& target,const GrayImage& base,GrayImage& coverage,const Image& preset,float u,float v,float radiusPixels,float delta,float hardness);
bool StampNormalFilterLayer(Image& target,const Image& base,GrayImage& coverage,const Image& preset,float u,float v,float radiusPixels,float strength,bool sharpen,float hardness);
void StampMaterialPreset(Image& normalTarget,Image& glossTarget,const Image& preset,float u,float v,float radiusPixels,float glossValue);
bool SaveNormalPreset(const Image& preset,float gloss,const std::wstring& path,std::string& error);
bool LoadNormalPreset(const std::wstring& path,Image& preset,float& gloss,std::string& error);
bool SaveNormalPreset(const Image& preset,const std::wstring& path,std::string& error);
bool LoadNormalPreset(const std::wstring& path,Image& preset,std::string& error);
BumpPair GenerateBump(const Sources& src,const Settings& cfg);
BumpPair GenerateBumpPreview(const Sources& src,const Settings& cfg,uint32_t maxDimension=1024);
BumpPair PackBumpLayersPreview(const Image& normal,const GrayImage& gloss,const GrayImage& height,uint32_t maxDimension=0,const GrayImage* baseHeight=nullptr);
// Preserve original packed channels wherever the editable layers did not change.
BumpPair CompositeBumpLayersPreview(const Image& normal,const GrayImage& gloss,const GrayImage& height,const Image& baseNormal,const GrayImage& baseGloss,const GrayImage& baseHeight,const BumpPair& originalPair,uint32_t maxDimension=0);
// Stage both files before replacement and roll back on ordinary I/O failures.
// Two separate paths cannot form a power-loss-atomic filesystem transaction.
bool WriteBumpPairDDSBytesAtomic(const std::wstring& baseWithoutSuffix,const std::vector<uint8_t>& bumpBytes,const std::vector<uint8_t>& bumpXBytes,std::string& error);
bool ExportPackedBumpPair(const BumpPair& pair,const std::wstring& baseWithoutSuffix,std::string& error);
bool ExportBumpLayers(const Image& normal,const GrayImage& gloss,const GrayImage& height,uint32_t width,uint32_t heightPixels,const std::wstring& baseWithoutSuffix,std::string& error,const GrayImage* baseHeight=nullptr);
bool ExportBumpPair(const Sources& src,const Settings& cfg,const std::wstring& baseWithoutSuffix,std::string& error);
Mesh MakeCube();
bool LoadOGFMesh(const std::vector<uint8_t>& bytes,Mesh& mesh,std::string& error);
bool LoadOGFMotions(const std::vector<uint8_t>& bytes,Mesh& mesh,std::string& error);
}
