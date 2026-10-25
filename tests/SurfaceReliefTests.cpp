#include "Core.h"
#include "MaterialDocument.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
bool same(const lab::Image& a,const lab::Image& b) {
    return a.w==b.w&&a.h==b.h&&a.pixels.size()==b.pixels.size()&&
        (a.pixels.empty()||!std::memcmp(a.pixels.data(),b.pixels.data(),a.pixels.size()*sizeof(lab::Pixel)));
}
lab::Image texture() {
    lab::Image result;result.w=128;result.h=96;result.pixels.resize(size_t(result.w)*result.h);
    for(uint32_t y=0;y<result.h;++y)for(uint32_t x=0;x<result.w;++x){
        const auto a=uint8_t(120+35*std::sin(x*.48f)+20*std::cos(y*.21f));
        const auto b=uint8_t(110+25*std::cos(x*.19f+y*.33f));
        result.pixels[size_t(y)*result.w+x]={a,b,uint8_t(70+(x/13+y/9)%2*80),255};
    }return result;
}
lab::BumpPair flat(uint32_t width=128,uint32_t height=96) {
    lab::Image normal;normal.w=width;normal.h=height;normal.pixels.assign(size_t(width)*height,{128,128,255,255});
    lab::GrayImage scalar;scalar.w=width;scalar.h=height;scalar.pixels.assign(size_t(width)*height,128);
    return lab::PackBumpLayersPreview(normal,scalar,scalar);
}
void recipesAndExport() {
    const auto source=texture();const auto base=flat();std::set<std::vector<uint8_t>> recipes;
    std::string error;lab::EncodedBumpPair flatBytes;
    check(lab::EncodePackedBumpPair(base,flatBytes,error),"encode baseline");
    const auto flatNormal=lab::UnpackNormalMap(lab::DecodeDDS(flatBytes.bump,error),lab::DecodeDDS(flatBytes.bumpX,error));
    for(int i=0;i<5;++i){
        const auto kind=lab::SurfacePreset(i);
        const auto patch=lab::MakeSurfaceReliefPatch(source,kind,128,96,.5f,.5f,18.5f);
        check(patch.valid()&&patch.w==39&&same(patch,lab::MakeSurfaceReliefPatch(source,kind,128,96,.5f,.5f,18.5f)),"deterministic patch at fractional radius");
        std::vector<uint8_t> key;
        for(const auto p:patch.pixels){
            const float x=(int(p.r)-128)/127.f,y=(int(p.g)-128)/127.f,z=p.b/127.5f-1.f;
            check(z>0&&std::fabs(std::sqrt(x*x+y*y+z*z)-1)<.015f,"finite forward normalized surface normals");
            key.push_back(p.r);key.push_back(p.g);
        }
        recipes.insert(key);
        lab::MaterialDocument doc;check(doc.Load(base,error),"load original");
        const auto gloss=doc.Gloss().pixels,height=doc.Height().pixels;
        check(doc.ApplySurfaceRelief(source,kind,.5f,.5f,{18.5f,.35f,.65f}),"all presets must change normals at default strength");
        check(!doc.InStroke()&&doc.UndoCount()==1&&doc.Gloss().pixels==gloss&&doc.Height().pixels==height,"one click is one closed normal-only transaction");
        const auto painted=doc.Normal();
        lab::EncodedBumpPair encoded;check(lab::EncodePackedBumpPair(doc.Preview(0),encoded,error),"encode surface result");
        const auto exported=lab::UnpackNormalMap(lab::DecodeDDS(encoded.bump,error),lab::DecodeDDS(encoded.bumpX,error));
        check(!same(exported,flatNormal),"default surface relief must survive BC3 export");
        check(doc.Undo()&&!doc.IsDirty()&&same(doc.Preview(0).bump,base.bump),"undo returns original material byte for byte");
        check(doc.Redo()&&same(doc.Normal(),painted),"redo returns stamped normal exactly");
        check(doc.ApplySurfaceRelief(source,kind,.5f,.5f,{18.5f,.35f,.65f})&&doc.UndoCount()==2,"a second intentional click adds exactly one step");
        check(doc.Undo()&&same(doc.Normal(),painted),"second click is independently undoable");
    }
    check(recipes.size()==5,"profiles produce five different texture responses");
}
void textureCoordinates() {
    const auto source=texture();
    for(int i=0;i<5;++i){
        const auto kind=lab::SurfacePreset(i);
        const auto smallPatch=lab::MakeSurfaceReliefPatch(source,kind,128,96,.5f,.5f,10.f);
        const auto largePatch=lab::MakeSurfaceReliefPatch(source,kind,128,96,.5f,.5f,20.f);
        for(uint32_t y=0;y<smallPatch.h;++y)for(uint32_t x=0;x<smallPatch.w;++x){
            const auto a=smallPatch.pixels[size_t(y)*smallPatch.w+x],b=largePatch.pixels[size_t(y+10)*largePatch.w+x+10];
            check(!std::memcmp(&a,&b,sizeof(a)),"increasing radius cannot stretch or change texture detail");
        }
        const auto shifted=lab::MakeSurfaceReliefPatch(source,kind,128,96,.5f+4.f/128,.5f,10.f);
        for(uint32_t y=0;y<smallPatch.h;++y)for(uint32_t x=0;x+4<smallPatch.w;++x){
            const auto a=shifted.pixels[size_t(y)*shifted.w+x],b=smallPatch.pixels[size_t(y)*smallPatch.w+x+4];
            check(!std::memcmp(&a,&b,sizeof(a)),"overlapping clicks must sample the same base-texture texels");
        }
        check(same(smallPatch,lab::MakeSurfaceReliefPatch(source,kind,128,96,2.5f,-1.5f,10.f)),"tiled UVs repeat the same patch");
        auto changed=source;for(auto& p:changed.pixels){p.r=uint8_t(255-p.r);p.g=uint8_t(255-p.g);p.b=uint8_t(255-p.b);}
        check(!same(smallPatch,lab::MakeSurfaceReliefPatch(changed,kind,128,96,.5f,.5f,10.f)),"each profile must respond to diffuse, not a fixed procedural field");
        check(lab::MakeSurfaceReliefPatch(source,kind,64,192,.99f,.01f,12.f).valid(),"rectangular normals and differently sized diffuse are supported");
    }
}
void noOpAndGuards() {
    const auto source=texture();auto uniform=source;for(auto& p:uniform.pixels)p={100,120,130,255};
    std::string error;lab::MaterialDocument doc;check(doc.Load(flat(),error),"load guard fixture");
    for(int i=0;i<5;++i)
        check(!doc.ApplySurfaceRelief(uniform,lab::SurfacePreset(i),.42f,.37f,{12,.8f,.65f})&&!doc.IsDirty()&&doc.UndoCount()==0,"uniform diffuse must not invent fibers or pores");
    auto invisible=source;for(auto& p:invisible.pixels)p.a=0;
    check(!doc.ApplySurfaceRelief(invisible,lab::SurfacePreset::Fabric,.5f,.5f,{12,.8f,.65f})&&doc.UndoCount()==0,"transparent diffuse must not edit hidden pixels");
    check(!doc.ApplySurfaceRelief({},lab::SurfacePreset::Fabric,.5f,.5f,{12,.8f,.65f}),"missing diffuse cannot silently use a procedural fallback");
    check(!doc.ApplySurfaceRelief(source,lab::SurfacePreset(99),.5f,.5f,{12,.8f,.65f}),"invalid material rejected");
    check(!doc.ApplySurfaceRelief(source,lab::SurfacePreset::Fabric,.5f,.5f,{12,0,.65f}),"zero strength is a no-op");
    check(!doc.ApplySurfaceRelief(source,lab::SurfacePreset::Fabric,std::numeric_limits<float>::quiet_NaN(),.5f,{12,.8f,.65f}),"non-finite UV rejected");
    check(!doc.ApplySurfaceRelief(source,lab::SurfacePreset::Fabric,.5f,.5f,{3000,.8f,.65f}),"oversized allocation rejected");
    lab::MaterialAdjustments adjustment;adjustment.gloss=.5f;check(doc.Adjust(adjustment),"draft exists");
    check(!doc.ApplySurfaceRelief(source,lab::SurfacePreset::Fabric,.5f,.5f,{12,.8f,.65f})&&doc.HasPendingAdjustments(),"draft must be resolved before stamping");doc.CancelAdjustments();
    check(doc.BeginStroke(lab::BrushOperation::GlossRemove,{12,.3f,.65f}),"unrelated freehand stroke starts");
    check(!doc.ApplySurfaceRelief(source,lab::SurfacePreset::Fabric,.5f,.5f,{12,.8f,.65f})&&doc.InStroke(),"surface tool cannot hijack a live freehand stroke");doc.CancelStroke();
    auto sourceAlpha=source;for(uint32_t y=0;y<sourceAlpha.h;++y)for(uint32_t x=0;x<sourceAlpha.w/2;++x)sourceAlpha.pixels[size_t(y)*sourceAlpha.w+x].a=0;
    const auto original=doc.Normal();check(doc.ApplySurfaceRelief(sourceAlpha,lab::SurfacePreset::Concrete,.5f,.5f,{18,.8f,.65f}),"opaque half changes");
    for(uint32_t y=0;y<sourceAlpha.h;++y)for(uint32_t x=0;x<sourceAlpha.w/2;++x){const size_t at=size_t(y)*sourceAlpha.w+x;check(!std::memcmp(&original.pixels[at],&doc.Normal().pixels[at],sizeof(lab::Pixel)),"fully transparent source pixels remain untouched");}
}
}
int main() {
    try {recipesAndExport();textureCoordinates();noOpAndGuards();std::cout<<"PASS: texture-derived relief, texel alignment, one-click undo, alpha, guard cases and BC3 export\n";return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
