#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>
namespace lab {
struct Pixel {uint8_t r=255,g=255,b=255,a=255;};
struct Image {uint32_t w=0,h=0;std::vector<Pixel> pixels; bool valid()const{return w>0&&h>0&&pixels.size()==size_t(w)*h;} };
struct V3 {float x=0,y=0,z=0;};
struct Vertex {V3 p,n;float u=0,v=0;};
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
    bool hasBind=false;
};
struct Mesh {
    std::vector<MeshPart> parts;
    std::vector<Bone> bones;
    bool skeletalSource=false;
    uint32_t sourceParts=0;
    bool valid()const{for(const auto& p:parts)if(p.valid())return true;return false;}
    size_t vertexCount()const{size_t n=0;for(const auto& p:parts)n+=p.vertices.size();return n;}
    size_t indexCount()const{size_t n=0;for(const auto& p:parts)n+=p.indices.size();return n;}
};
struct Settings {float normalStrength=1.f,gloss=0.45f,height=0.5f;bool invertY=false;};
struct Sources {Image diffuse,normal,gloss,height;};
struct BumpPair {Image bump,bumpX;};
struct PartImages {Image diffuse,bump,bumpX;bool hasBump=false;};
Image DefaultChecker(uint32_t size=256);
Image DecodeDDS(const std::vector<uint8_t>& bytes,std::string& error);
std::vector<uint8_t> EncodeBC3DDS(const Image& rgba,const std::vector<Image>& mipImages);
Image DecompressBC3Image(const std::vector<uint8_t>& blocks,uint32_t width,uint32_t height);
BumpPair GenerateBump(const Sources& src,const Settings& cfg);
bool ExportBumpPair(const Sources& src,const Settings& cfg,const std::wstring& baseWithoutSuffix,std::string& error);
Mesh MakeCube();
bool LoadOGFMesh(const std::vector<uint8_t>& bytes,Mesh& mesh,std::string& error);
}
