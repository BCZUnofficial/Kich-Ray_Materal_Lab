#include "MaterialDocument.h"
#include "EditorWorkflow.h"
#include "EngineLighting.h"
#include "EngineTexture.h"
#include "ViewportTools.h"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
void check(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
bool same(const lab::Image& a,const lab::Image& b){return a.w==b.w&&a.h==b.h&&a.pixels.size()==b.pixels.size()&&(a.pixels.empty()||!std::memcmp(a.pixels.data(),b.pixels.data(),a.pixels.size()*4));}
bool same(const lab::BumpPair& a,const lab::BumpPair& b){return same(a.bump,b.bump)&&same(a.bumpX,b.bumpX);}
lab::BumpPair fixture(){lab::Sources source;source.diffuse=lab::DefaultChecker(96);return lab::GenerateBumpPreview(source,lab::MinimalBaseSettings(),0);}
double relief(const lab::BumpPair& p){double result=0;for(size_t i=0;i<p.bump.pixels.size();++i){const auto n=lab::DecodeBumpNormal(p.bump.pixels[i],p.bumpX.pixels[i],true);result+=n.x*n.x+n.y*n.y;}return result/p.bump.pixels.size();}
void baselineAndDraft(const std::filesystem::path& directory){
    const auto base=fixture();lab::Sources source;source.diffuse=lab::DefaultChecker(96);
    check(base.bump.w==96&&relief(base)>.0001,"minimal base must contain visible finite relief at source resolution");
    check(relief(base)<relief(lab::GenerateBumpPreview(source,lab::Settings{},0)),"minimal base must be gentler than legacy default");
    lab::MaterialDocument doc;std::string error;check(doc.Load(base,error),"load baseline");
    check(doc.Adjust({})&&!doc.HasPendingAdjustments()&&!doc.IsDirty()&&same(doc.Preview(0),base),"identity must be byte-exact and consume no history");
    check(doc.BeginStroke(lab::BrushOperation::GlossAdd,{10,.5f,.7f}),"start prior brush");
    doc.Stamp(.5f,.5f,lab::MakeDefaultNormalPreset());check(doc.EndStroke(),"prior brush changes material");
    doc.MarkSaved();const auto painted=doc.Preview(0);const size_t oldHistory=doc.UndoCount();
    lab::MaterialAdjustments adjustments;adjustments.normalStrength=2;adjustments.gloss=.5f;adjustments.height=.7f;adjustments.blur=.2f;
    check(doc.Adjust(adjustments)&&doc.HasPendingAdjustments()&&doc.IsDirty()&&!same(doc.Preview(0),painted),"sliders must immediately change pixels and mark draft dirty");
    const auto draft=doc.Preview(0);
    check(doc.Adjust(adjustments)&&same(draft,doc.Preview(0)),"repeated slider setting must not accumulate");
    check(!doc.BeginStroke(lab::BrushOperation::GlossAdd,{10,.5f,.7f})&&!doc.Undo()&&!doc.Redo()&&!doc.Clear(),"unresolved draft must block destructive tool transitions");
    auto invalid=adjustments;invalid.gloss=std::numeric_limits<float>::quiet_NaN();check(!doc.Adjust(invalid)&&same(draft,doc.Preview(0)),"invalid setting cannot corrupt current draft");
    doc.CancelAdjustments();check(!doc.HasPendingAdjustments()&&!doc.IsDirty()&&doc.UndoCount()==oldHistory&&same(painted,doc.Preview(0)),"cancel must restore all prior brush work, saved state and history");
    check(doc.Adjust(adjustments)&&doc.CommitAdjustments()&&!doc.HasPendingAdjustments()&&doc.UndoCount()==oldHistory+1,"apply must create exactly one undo step");
    check(doc.Undo()&&same(painted,doc.Preview(0))&&!doc.IsDirty(),"undo slider commit must restore saved brushed state");
    check(doc.Redo()&&same(draft,doc.Preview(0)),"redo restores all adjusted channels");
    check(doc.Adjust(adjustments)&&doc.Adjust({})&&!doc.HasPendingAdjustments()&&same(draft,doc.Preview(0)),"returning sliders to identity restores the committed baseline");
    check(doc.Adjust(adjustments)&&doc.Save(directory/"draft.krlab",error)&&!doc.HasPendingAdjustments()&&!doc.IsDirty(),"session save resolves and persists current draft");
    lab::MaterialDocument reopened;check(reopened.Open(directory/"draft.krlab",error)&&same(reopened.Preview(0),doc.Preview(0)),"saved adjusted session round trip");
}
std::vector<uint8_t> read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void encodedPreview(const std::filesystem::path& directory){
    const auto base=fixture();lab::EncodedBumpPair bytes;std::string error;
    check(lab::EncodePackedBumpPair(base,bytes,error),"encode production DDS pair");
    auto a=lab::DecodeDDSMipChain(bytes.bump,error),b=lab::DecodeDDSMipChain(bytes.bumpX,error);
    check(a.size()==7&&b.size()==a.size()&&a.back().w==1&&a.back().h==1,"viewport must consume the full exported mip chain");
    for(size_t i=1;i<a.size();++i)check(a[i].w==std::max(1u,a[i-1].w/2)&&a[i].h==b[i].h,"NPOT mip dimensions must halve correctly");
    check(same(a[0],lab::DecodeDDS(bytes.bump,error)),"viewport top mip must equal decoded disk bytes");
    const auto name=directory/"material";
    check(lab::WriteBumpPairDDSBytesAtomic(name.wstring(),bytes.bump,bytes.bumpX,error)&&read(directory/"material_bump.dds")==bytes.bump&&read(directory/"material_bump#.dds")==bytes.bumpX,"disk output must be byte-identical to encoded preview cache");
    auto truncated=bytes.bump;truncated.pop_back();check(lab::DecodeDDSMipChain(truncated,error).empty(),"truncated last mip must reject the entire chain");
    auto invalid=base;invalid.bumpX.h--;const auto saved=bytes;
    check(!lab::EncodePackedBumpPair(invalid,bytes,error)&&bytes.bump==saved.bump&&bytes.bumpX==saved.bumpX,"invalid pair must fail before touching prior encoded data");
    auto muted=base;for(auto& p:muted.bump.pixels)p.r=0;
    check(lab::EncodePackedBumpPair(muted,bytes,error),"gloss-disabled export encodes");
    for(const auto& mip:lab::DecodeDDSMipChain(bytes.bump,error))for(auto p:mip.pixels)check(p.r==0,"gloss-disabled DDS must have exactly zero specular in every mip");
    check(lab::EncodePackedBumpPair(base,bytes,error)&&bytes.bump==saved.bump&&bytes.bumpX==saved.bumpX,"re-enabling gloss must reproduce original data");
}
void engineLighting(){
    const auto lut=lab::MakeR2MaterialLut();check(lut.w==128&&lut.h==1024,"engine LUT dimensions");
    // Captured from upstream kichray 01b239e, KichR4MaterialLut.h::Texel.
    const unsigned golden[][5]={{0,1,1,6,0},{0,63,127,151,0},{0,110,245,229,62},{0,127,255,255,255},{1,1,1,3,0},{1,63,127,135,0},{1,110,245,224,87},{1,127,255,255,255},{2,1,1,2,0},{2,63,127,126,0},{2,110,245,221,3},{2,127,255,255,255},{3,1,1,2,114},{3,63,127,126,216},{3,110,245,221,25},{3,127,255,255,255}};
    for(const auto& g:golden){const auto p=lut.pixels[size_t(g[0]*256+g[2])*128+g[1]];check(p.r==g[3]&&p.a==g[4],"material LUT differs from pinned engine reference");}
    check(lab::R2SpecularIntensity({0,0,0})==0&&lab::R2SpecularIntensity({3,0,0})==1&&lab::R2SpecularIntensity({2,2,2})==2,"engine light intensity endpoints");
    check(std::fabs(lab::R2SpecularIntensity({.125f,.125f,.125f})-.25f)<1e-6f,"engine sub-unit specular power");
}
void metadata(){
    auto u32=[](std::vector<uint8_t>& b,uint32_t v){for(int k=0;k<4;++k)b.push_back(uint8_t(v>>(k*8)));};
    std::vector<uint8_t> b;u32(b,0x0816);u32(b,8);u32(b,2);u32(b,0x3f000000); // material 2 + .5
    u32(b,0x0817);const std::string name="actors\\custom_bump";u32(b,uint32_t(9+name.size()));u32(b,0x3d4ccccd);u32(b,2);b.insert(b.end(),name.begin(),name.end());b.push_back(0);
    lab::EngineTextureMetadata m;check(lab::ReadEngineTextureMetadata(b,m)&&m.hasMaterial&&m.material==2.5f&&m.bumpEnabled&&m.bumpName==name,"THM class weight and custom bump reference must be read");
    auto truncated=b;truncated.pop_back();check(!lab::ReadEngineTextureMetadata(truncated,m)&&m.material==2.5f,"bad THM must not partially overwrite metadata");
    b[14]=0xc0;b[15]=0x7f;check(!lab::ReadEngineTextureMetadata(b,m),"THM NaN rejected");
    check(!lab::ReadEngineTextureMetadata({},m),"absent THM uses caller default");
}
void navigation(){
    namespace fs=std::filesystem;
    check(lab::PreferredExportDirectory("game/textures/actors/a.dds","last","models/a.ogf")==fs::path("game/textures/actors"),"model texture folder has priority");
    check(lab::PreferredExportDirectory({},"last","models/a.ogf")==fs::path("last"),"last export is second priority");
    check(lab::PreferredExportDirectory({},{},"models/a.ogf")==fs::path("models"),"OGF folder is final fallback");
    const std::vector<std::string> names={"walk","Run","idle","run","Ящик","бег","Ёж","ель"};
    check(lab::SortedAnimationIndices(names)==std::vector<size_t>({2,1,3,0,5,6,7,4}),"animations sort alphabetically while retaining original motion indices and stable duplicates");
    lab::ViewportCamera c;for(int axis=0;axis<3;++axis){c.alignAxis(axis);const auto o=c.offset(),u=c.up();
        check(std::fabs(o.x*u.x+o.y*u.y+o.z*u.z)<1e-5f,"axis camera must have a non-degenerate up vector");
        const float values[]={o.x,o.y,o.z};check(std::fabs(values[axis]-c.distance)<1e-5f,"axis view must face the chosen axis");}
    c.alignAxis(1);const auto eye=c.eye();c.look(0,0);check(std::fabs(c.eye().x-eye.x)+std::fabs(c.eye().y-eye.y)+std::fabs(c.eye().z-eye.z)<1e-6f,"top view must not jump at zero input");
}
}
int main(){const auto dir=std::filesystem::temp_directory_path()/("material-workflow-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{std::filesystem::create_directories(dir);baselineAndDraft(dir);encodedPreview(dir);engineLighting();metadata();navigation();std::filesystem::remove_all(dir);std::cout<<"PASS: minimal baseline, live draft/apply/discard, exact DDS mip preview, gloss toggle, engine LUT/THM, paths, animations and axis views\n";return 0;}
    catch(const std::exception& e){std::filesystem::remove_all(dir);std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
