#include "Core.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
bool same(const lab::Image& a,const lab::Image& b){return a.w==b.w&&a.h==b.h&&a.pixels.size()==b.pixels.size()&&std::memcmp(a.pixels.data(),b.pixels.data(),a.pixels.size()*sizeof(lab::Pixel))==0;}
bool same(const lab::BumpPair& a,const lab::BumpPair& b){return same(a.bump,b.bump)&&same(a.bumpX,b.bumpX);}
lab::Image image(lab::Pixel p,uint32_t size=32){lab::Image n;n.w=n.h=size;n.pixels.assign(size_t(size)*size,p);return n;}
lab::GrayImage gray(uint8_t p,uint32_t size=32){lab::GrayImage n;n.w=n.h=size;n.pixels.assign(size_t(size)*size,p);return n;}
float slopeX(lab::Pixel p){return (p.r-127.5f)/std::max(.5f,p.b-127.5f);}
void noOpStamps(){
 const auto base=image({166,105,246,17});auto painted=base;const auto preset=image({180,128,242,255},8);lab::GrayImage coverage;
 check(!lab::StampNormalReliefLayer(painted,base,coverage,preset,.5f,.5f,6,0,false,.5f)&&same(base,painted)&&!coverage.valid(),"zero relief must not quantize pixels or consume coverage");
 check(!lab::StampNormalFilterLayer(painted,base,coverage,preset,.5f,.5f,6,0,true,.5f)&&same(base,painted)&&!coverage.valid(),"zero filter must be byte-exact");
 auto scalar=gray(110),before=scalar;
 check(!lab::StampScalarDeltaLayer(scalar,before,coverage,preset,.5f,.5f,6,0,.5f)&&scalar.pixels==before.pixels&&!coverage.valid(),"zero scalar must be byte-exact");
 check(!lab::StampNormalReliefLayer(painted,base,coverage,image({128,128,255,255},8),.5f,.5f,6,1,false,.5f)&&same(base,painted),"a flat preset must not overwrite existing detail");
}
void reliefDirection(){
 auto base=image({166,105,246,255});auto added=base,removed=base;lab::GrayImage covA,covR;const auto preset=image({180,128,242,255},8);const size_t center=16*32+16;
 check(lab::StampNormalReliefLayer(added,base,covA,preset,.5f,.5f,6,.4f,false,.5f),"add relief must change the map");
 check(slopeX(added.pixels[center])>slopeX(base.pixels[center]),"add relief must accumulate the preset slope");
 check(lab::StampNormalReliefLayer(removed,base,covR,preset,.5f,.5f,6,.4f,true,.5f),"remove relief must change the map");
 check(slopeX(removed.pixels[center])>=0&&slopeX(removed.pixels[center])<slopeX(base.pixels[center]),"remove relief must flatten rather than invert a bump");
 check(removed.pixels[center].g<=128&&removed.pixels[center].g>=base.pixels[center].g,"remove relief must retain the sign of negative Y detail");
 check(std::memcmp(&added.pixels[0],&base.pixels[0],sizeof(lab::Pixel))==0,"a stamp must not alter distant texels");
 const auto after=added;
 check(!lab::StampNormalReliefLayer(added,base,covA,preset,.5f,.5f,6,.4f,false,.5f)&&same(added,after),"one stroke must not build up repeatedly at equal coverage");
 covA={};
 check(lab::StampNormalReliefLayer(added,after,covA,preset,.5f,.5f,6,.4f,false,.5f)&&slopeX(added.pixels[center])>slopeX(after.pixels[center]),"a new stroke must start from the previous result");
}
void scalarDirectionAndBounds(){
 const auto preset=image({128,128,255,255},8);const size_t center=16*32+16;auto base=gray(128),up=base,down=base;lab::GrayImage a,b;
 check(lab::StampScalarDeltaLayer(up,base,a,preset,.5f,.5f,5,.2f,.5f)&&up.pixels[center]>128,"add height/gloss must increase the channel");
 check(lab::StampScalarDeltaLayer(down,base,b,preset,.5f,.5f,5,-.2f,.5f)&&down.pixels[center]<128,"remove height/gloss must decrease the channel");
 a={};b={};up=base;down=base;
 lab::StampScalarDeltaLayer(up,base,a,preset,.5f,.5f,5,100,.5f);lab::StampScalarDeltaLayer(down,base,b,preset,.5f,.5f,5,-100,.5f);
 check(up.pixels[center]==255&&down.pixels[center]==0,"scalar paint must saturate at channel bounds");
 const auto full=gray(255);up=full;a={};
 check(!lab::StampScalarDeltaLayer(up,full,a,preset,.5f,.5f,5,1,.5f),"a saturated stroke must report no change");
}
void filterDirections(){
 auto base=image({128,128,255,255});const size_t center=16*32+16;base.pixels[center]={173,128,247,255};auto sharp=base,blur=base;lab::GrayImage a,b;const auto preset=image({128,128,255,255},8);
 check(lab::StampNormalFilterLayer(sharp,base,a,preset,.5f,.5f,5,.5f,true,.5f),"sharpen must modify detailed normals");
 check(lab::StampNormalFilterLayer(blur,base,b,preset,.5f,.5f,5,.5f,false,.5f),"blur must modify detailed normals");
 check(slopeX(sharp.pixels[center])>slopeX(base.pixels[center])&&slopeX(blur.pixels[center])<slopeX(base.pixels[center]),"add/remove sharpness must act in opposite directions");
}
void heightIdentityAndResolution(){
 auto normal=image({254,128,140,255});auto gloss=gray(100),height=gray(128);
 check(same(lab::PackBumpLayersPreview(normal,gloss,height),lab::PackBumpLayersPreview(normal,gloss,height,0,&height)),"unchanged height must not clamp steep normals");
 auto raised=gray(180);
 const auto plateau=lab::PackBumpLayersPreview(normal,gloss,raised,0,&height),baseline=lab::PackBumpLayersPreview(normal,gloss,height);
 check(same(plateau.bump,baseline.bump),"a constant height offset must not tilt normals");
 normal=image({128,128,255,255});auto edited=height;
 for(uint32_t y=0;y<32;++y)for(uint32_t x=0;x<32;++x)edited.pixels[y*32+x]=uint8_t(80+x*3);
 const auto full=lab::PackBumpLayersPreview(normal,gloss,edited,0,&height),half=lab::PackBumpLayersPreview(normal,gloss,edited,16,&height);
 const auto& a=full.bump.pixels[16*32+16];const auto& b=half.bump.pixels[8*16+8];
 check(std::abs(int(a.a)-int(b.a))<=1&&std::abs(int(a.g)-int(b.g))<=1,"preview size must not change authored height-gradient strength");
}
void preserveOriginalChannels(){
 auto normal=image({150,118,252,255});auto gloss=gray(90),height=gray(128);
 auto original=lab::PackBumpLayersPreview(normal,gloss,height);
 // Original imported correction is allowed to be non-neutral (BC3 residual).
 for(size_t i=0;i<original.bump.pixels.size();++i){original.bumpX.pixels[i].r=uint8_t(120+i%13);original.bump.pixels[i].r=uint8_t(130+i%17);}
 check(same(original,lab::CompositeBumpLayersPreview(normal,gloss,height,normal,gloss,height,original)),"an untouched material must retain its original packed bytes");
 auto editedGloss=gloss;editedGloss.pixels[16*32+16]=200;
 const auto changed=lab::CompositeBumpLayersPreview(normal,editedGloss,height,normal,gloss,height,original);
 for(size_t i=0;i<normal.pixels.size();++i){const auto a=original.bump.pixels[i],b=changed.bump.pixels[i];check(a.a==b.a&&a.b==b.b&&a.g==b.g,"gloss paint must preserve every packed normal channel");check(std::memcmp(&original.bumpX.pixels[i],&changed.bumpX.pixels[i],sizeof(lab::Pixel))==0,"gloss paint must preserve correction and height");}
 check(changed.bump.pixels[16*32+16].r!=original.bump.pixels[16*32+16].r,"gloss composite must retain its edit");
 check(same(original,lab::CompositeBumpLayersPreview(normal,gloss,height,normal,gloss,height,original)),"undo to the base must restore all original bytes");
}
void generatedPreviewAndCorrection(){
 lab::Sources src;src.diffuse=image({140,140,140,255});src.normal=image({128,128,255,255});
 for(size_t i=0;i<src.normal.pixels.size();++i){const float x=std::sin(float(i)*1.37f)*.65f,y=std::cos(float(i)*.79f)*.45f,z=std::sqrt(std::max(.01f,1-x*x-y*y));src.normal.pixels[i]={uint8_t((x*.5f+.5f)*255+.5f),uint8_t((y*.5f+.5f)*255+.5f),uint8_t((z*.5f+.5f)*255+.5f),255};}
 lab::Settings cfg;cfg.height=cfg.blur=cfg.sharpness=0;cfg.normalStrength=0;const auto flat=lab::GenerateBumpPreview(src,cfg);for(const auto& p:flat.bump.pixels)check(p.a==128&&p.b==128&&p.g==255,"zero normal strength must generate a flat normal");cfg.normalStrength=1;const auto preview=lab::GenerateBumpPreview(src,cfg),compressed=lab::GenerateBump(src,cfg);
 lab::Sources zeroSource=src;lab::Settings zeroConfig;zeroConfig.normalStrength=0;zeroConfig.height=.5f;
 for(size_t i=0;i<zeroSource.diffuse.pixels.size();++i){const uint8_t v=uint8_t((i*37)%256);zeroSource.diffuse.pixels[i]={v,v,v,255};}
 for(int explicitHeight=0;explicitHeight<2;++explicitHeight){
  if(explicitHeight)zeroSource.height=zeroSource.diffuse;
  for(int authored=0;authored<2;++authored){zeroSource.normal=authored?src.normal:lab::Image{};const auto zero=lab::GenerateBumpPreview(zeroSource,zeroConfig);check(zero.bump.valid(),"zero-strength height fixture must generate a material");for(const auto& p:zero.bump.pixels)check(p.a==128&&p.b==128&&p.g==255,"zero normal strength must suppress height relief with or without authored normals");}
 }
 check(preview.bump.valid()&&compressed.bump.valid(),"both generated preview paths must produce a pair");
 double corrected=0,uncorrected=0;
 for(size_t i=0;i<preview.bump.pixels.size();++i){const auto a=preview.bump.pixels[i],b=compressed.bump.pixels[i],c=compressed.bumpX.pixels[i];check(preview.bumpX.pixels[i].r==128&&preview.bumpX.pixels[i].g==128&&preview.bumpX.pixels[i].b==128,"uncompressed preview must use neutral correction");const int da=int(a.a)-int(b.a),db=int(a.b)-int(b.b),dg=int(a.g)-int(b.g);uncorrected+=da*da+db*db+dg*dg;const int ea=da-(int(c.r)-128),eb=db-(int(c.g)-128),eg=dg-(int(c.b)-128);corrected+=ea*ea+eb*eb+eg*eg;}
 check(corrected<uncorrected*.95,"BC3 correction must reduce normal error rather than overshoot it");
}
std::vector<uint8_t> readBytes(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
uint32_t read32(const std::vector<uint8_t>& b,size_t p){return uint32_t(b[p])|(uint32_t(b[p+1])<<8)|(uint32_t(b[p+2])<<16)|(uint32_t(b[p+3])<<24);}
void write32(std::vector<uint8_t>& b,size_t p,uint32_t n){for(int i=0;i<4;++i)b[p+size_t(i)]=uint8_t(n>>(i*8));}
lab::Image decodeMip(const std::vector<uint8_t>& bytes,uint32_t level){
 uint32_t w=read32(bytes,16),h=read32(bytes,12);size_t start=128;
 for(uint32_t i=0;i<level;++i){start+=size_t((w+3)/4)*((h+3)/4)*16;w=std::max(1u,w/2);h=std::max(1u,h/2);}
 const size_t length=size_t((w+3)/4)*((h+3)/4)*16;check(start+length<=bytes.size(),"complete mip bytes must be present");
 std::vector<uint8_t> mip(bytes.begin(),bytes.begin()+128);mip.insert(mip.end(),bytes.begin()+std::ptrdiff_t(start),bytes.begin()+std::ptrdiff_t(start+length));write32(mip,12,h);write32(mip,16,w);write32(mip,20,uint32_t(length));write32(mip,28,1);std::string error;auto out=lab::DecodeDDS(mip,error);check(out.valid(),"every exported mip must decode");return out;
}
void packingConventionAndMipmaps(){
 const lab::Pixel p{128,255,128,160},e{128,128,128,99};const auto edit=lab::DecodeBumpNormal(p,e),rendered=lab::DecodeBumpNormal(p,e,true);
 check(std::abs(rendered.x/rendered.z-2*edit.x/edit.z)<1e-5f,"SoC's final z half-scale must occur exactly once");
 auto a=image({0,0,0,255},16);for(int y=7;y<=8;++y)for(int x=7;x<=8;++x)a.pixels[size_t(y)*16+x]={255,255,255,255};
 const auto small=lab::ResizeImageMax(a,1);check(small.pixels[0].r==4,"minification must average the entire footprint, not sample its centre");
 lab::Sources wide;wide.diffuse.w=1030;wide.diffuse.h=1;wide.diffuse.pixels.assign(1030,{128,128,128,255});lab::Settings flatSettings;flatSettings.blur=flatSettings.sharpness=flatSettings.height=0;
 check(lab::GenerateBumpPreview(wide,flatSettings,0).bump.w==1030,"native generation must preserve dimensions above the preview limit");
 check(lab::GenerateBumpPreview(wide,flatSettings).bump.w<=1024,"explicit preview compatibility must remain bounded");
 lab::Sources src;src.diffuse=image({128,128,128,255},32);for(uint32_t y=0;y<32;++y)for(uint32_t x=0;x<32;++x){const uint8_t c=uint8_t(50+x*3);src.diffuse.pixels[y*32+x]={c,c,c,255};}
 lab::Settings cfg;cfg.blur=cfg.sharpness=0;cfg.height=.5f;cfg.gloss=0;
 const auto preview=lab::GenerateBumpPreview(src,cfg);for(const auto q:preview.bump.pixels)check(q.r==0,"zero gloss must remove derived gloss");src.gloss=image({160,160,160,255},32);const auto authored=lab::GenerateBumpPreview(src,cfg);for(const auto q:authored.bump.pixels)check(q.r==0,"zero gloss must remove authored gloss");
 const auto dir=std::filesystem::temp_directory_path()/("material-lab-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(dir);const auto base=dir/"ramp";std::string error;
 check(lab::ExportBumpPair(src,cfg,base.wstring(),error),"generated pair must export");const auto bytes=readBytes(dir/"ramp_bump.dds"),errors=readBytes(dir/"ramp_bump#.dds");check(read32(bytes,28)==6&&read32(errors,28)==6,"both DDS files must contain the complete matching mip chain");
 const auto top=decodeMip(bytes,0),topE=decodeMip(errors,0),mip=decodeMip(bytes,1),mipE=decodeMip(errors,1);
 const auto n0=lab::DecodeBumpNormal(top.pixels[16*32+16],topE.pixels[16*32+16],true),n1=lab::DecodeBumpNormal(mip.pixels[8*16+8],mipE.pixels[8*16+8],true);
 check(std::abs(n0.x/n0.z-n1.x/n1.z)<.12f,"mips must filter the authored relief instead of regenerating it at a different strength");
 for(uint32_t i=0;i<6;++i){decodeMip(bytes,i);decodeMip(errors,i);}
 // An unwritable second destination must not truncate the first old DDS.
 const auto failed=dir/"blocked";const std::vector<uint8_t> sentinel{1,7,3,9};{std::ofstream out(dir/"blocked_bump.dds",std::ios::binary);out.write(reinterpret_cast<const char*>(sentinel.data()),std::streamsize(sentinel.size()));}std::filesystem::create_directory(dir/"blocked_bump#.dds");
 check(!lab::WriteBumpPairDDSBytesAtomic(failed.wstring(),bytes,errors,error),"pair export must fail when either destination cannot be replaced");check(readBytes(dir/"blocked_bump.dds")==sentinel,"failed pair export must preserve the old first file");
 const auto copy=dir/"exact";check(lab::WriteBumpPairDDSBytesAtomic(copy.wstring(),bytes,errors,error),"raw DDS pair export must succeed");check(readBytes(dir/"exact_bump.dds")==bytes&&readBytes(dir/"exact_bump#.dds")==errors,"unchanged raw DDS export must be byte exact");
 std::filesystem::remove_all(dir);
}
}
int main(){try{noOpStamps();reliefDirection();scalarDirectionAndBounds();filterDirections();heightIdentityAndResolution();preserveOriginalChannels();generatedPreviewAndCorrection();packingConventionAndMipmaps();std::cout<<"PASS: brush direction, stroke coverage, zero-strength, bounds, height identity/resolution, packed-channel preservation, BC3 correction, SoC convention, area mipmaps, paired export\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
