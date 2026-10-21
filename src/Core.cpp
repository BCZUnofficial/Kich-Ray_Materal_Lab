#include "Core.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstring>
namespace lab {
namespace {
float sat(float x){return std::max(0.f,std::min(x,1.f));}
uint8_t b8(float x){return uint8_t(sat(x)*255.f+0.5f);}
float lum(Pixel p){return (0.2126f*p.r+0.7152f*p.g+0.0722f*p.b)/255.f;}
Pixel sample(const Image& im,float u,float v){if(!im.valid())return {};u=sat(u)*im.w-.5f;v=sat(v)*im.h-.5f;int x=int(floor(u)),y=int(floor(v));float a=u-x,b=v-y;auto at=[&](int i,int j)->Pixel{return im.pixels[size_t(std::min(int(im.h)-1,std::max(0,j)))*im.w+std::min(int(im.w)-1,std::max(0,i))];};Pixel a0=at(x,y),b0=at(x+1,y),a1=at(x,y+1),b1=at(x+1,y+1),o;uint8_t* d=&o.r;for(int k=0;k<4;++k){const float v0=(&a0.r)[k]*(1-a)+(&b0.r)[k]*a,v1=(&a1.r)[k]*(1-a)+(&b1.r)[k]*a;d[k]=b8((v0*(1-b)+v1*b)/255.f);}return o;}
V3 norm(V3 n){float l=sqrt(n.x*n.x+n.y*n.y+n.z*n.z);if(l<1e-5f)return {0,0,1};return{n.x/l,n.y/l,n.z/l};}
Image makeBump(const Sources& s,const Settings& cfg,uint32_t w,uint32_t h){
 if(!s.diffuse.valid()||!w||!h)return {};
 Image out;out.w=w;out.h=h;out.pixels.resize(size_t(w)*h);
 const Image& heightMap=s.height.valid()?s.height:s.diffuse;
 // The 3x3 Sobel kernel follows the standard normal-map generators used as
 // references (NormalMap-Online / NormalmapGenerator).  Height is the actual
 // relief gain now, rather than being stored only in bump# alpha.
 const float relief=std::max(0.f,cfg.height)*4.f;
 const float normalGain=std::max(0.f,cfg.normalStrength);
 auto wrap01=[](float a){a=std::fmod(a,1.f);return a<0.f?a+1.f:a;};
 for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
  const float u=(x+.5f)/float(w),v=(y+.5f)/float(h),du=1.f/float(w),dv=1.f/float(h);
  V3 n{0,0,1};
  if(s.normal.valid()){
   Pixel p=sample(s.normal,u,v);
   n=norm({p.r/127.5f-1.f,p.g/127.5f-1.f,p.b/127.5f-1.f});
   const float gain=std::max(.0001f,relief)*normalGain;
   n=norm({n.x*gain,n.y*gain*(cfg.invertY?-1.f:1.f),n.z});
  }else{
   auto H=[&](float ox,float oy){return lum(sample(heightMap,wrap01(u+ox),wrap01(v+oy)));};
   const float tl=H(-du,-dv),t=H(0,-dv),tr=H(du,-dv);
   const float l=H(-du,0),r=H(du,0);
   const float bl=H(-du,dv),b=H(0,dv),br=H(du,dv);
   const float gx=tl+2.f*l+bl-tr-2.f*r-br;
   const float gy=tl+2.f*t+tr-bl-2.f*b-br;
   const float gain=relief*normalGain;
   n=norm({gx*gain,gy*gain*(cfg.invertY?-1.f:1.f),1.f});
  }
  const Pixel gp=s.gloss.valid()?sample(s.gloss,u,v):sample(s.diffuse,u,v);
  const float sourceGloss=s.gloss.valid()?lum(gp):(std::max(gp.r,std::max(gp.g,gp.b))/255.f);
  const float glossGain=std::clamp(cfg.gloss*2.2f,0.f,2.2f);
  const float g=sat(std::pow(std::max(0.f,sourceGloss),0.85f)*glossGain);
  Pixel p;
  p.r=b8(std::sqrt(g)); // SoC R2: S.gloss = Nu.x * Nu.x
  p.g=b8(n.z*.5f+.5f);
  p.b=b8(n.y*.5f+.5f);
  p.a=b8(n.x*.5f+.5f);
  out.pixels[size_t(y)*w+x]=p;
 }
 return out;
}
Image correction(const Image& original,const Image& decoded,const Sources& src,const Settings& cfg){
 Image out;out.w=original.w;out.h=original.h;out.pixels.resize(original.pixels.size());
 const Image& map=src.height.valid()?src.height:src.diffuse;
 for(uint32_t y=0;y<out.h;++y)for(uint32_t x=0;x<out.w;++x){
  const size_t i=size_t(y)*out.w+x;const Pixel p=original.pixels[i],c=decoded.pixels[i];
  // BumpX / SDK-style compensation: RGB stores twice the BC3 normal error
  // around neutral 128, alpha stores height.  This is the pair expected by
  // the classic X-Ray bump workflow.
  auto err8=[](int d)->uint8_t{return uint8_t(std::clamp(d*2+128,0,255));};
  Pixel e;
  e.r=err8(int(p.a)-int(c.a));
  e.g=err8(int(p.b)-int(c.b));
  e.b=err8(int(p.g)-int(c.g));
  const float hh=lum(sample(map,(x+.5f)/float(out.w),(y+.5f)/float(out.h)));
  const float hscale=std::clamp(cfg.height,0.f,1.f);
  e.a=b8(.5f+(hh-.5f)*hscale);
  out.pixels[i]=e;
 }
 return out;
}
BumpPair oneMip(const Sources& src,const Settings& cfg,uint32_t w,uint32_t h){BumpPair pair;pair.bump=makeBump(src,cfg,w,h);auto compressed=EncodeBC3DDS(pair.bump,{pair.bump});std::string err;Image decoded=DecodeDDS(compressed,err);if(!decoded.valid())return {};pair.bumpX=correction(pair.bump,decoded,src,cfg);return pair;}
}

Image ResizeImageMax(const Image& src,uint32_t maxDimension){
 if(!src.valid()||!maxDimension)return {};
 const uint32_t biggest=std::max(src.w,src.h);if(biggest<=maxDimension)return src;
 const float k=float(maxDimension)/float(biggest);const uint32_t w=std::max(1u,uint32_t(std::lround(src.w*k))),h=std::max(1u,uint32_t(std::lround(src.h*k)));
 Image out;out.w=w;out.h=h;out.pixels.resize(size_t(w)*h);
 for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)out.pixels[size_t(y)*w+x]=sample(src,(x+.5f)/float(w),(y+.5f)/float(h));
 return out;
}
Image UnpackNormalMap(const Image& bump,const Image& bumpX){
 if(!bump.valid()||!bumpX.valid())return {};
 const uint32_t w=std::min(bump.w,bumpX.w),h=std::min(bump.h,bumpX.h);if(!w||!h)return {};
 Image out;out.w=w;out.h=h;out.pixels.resize(size_t(w)*h);
 for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
  const Pixel n=sample(bump,(x+.5f)/float(w),(y+.5f)/float(h));
  const Pixel e=sample(bumpX,(x+.5f)/float(w),(y+.5f)/float(h));
  V3 v{n.a/255.f+(e.r/255.f-1.f),n.b/255.f+(e.g/255.f-1.f),n.g/255.f+(e.b/255.f-1.f)};
  v=norm(v);out.pixels[size_t(y)*w+x]={b8(v.x*.5f+.5f),b8(v.y*.5f+.5f),b8(v.z*.5f+.5f),255};
 }
 return out;
}
Image MakeDefaultNormalPreset(uint32_t size){
 size=std::max(8u,size);Image out;out.w=out.h=size;out.pixels.resize(size_t(size)*size);
 for(uint32_t y=0;y<size;++y)for(uint32_t x=0;x<size;++x){
  const float fx=((x+.5f)/float(size))*2.f-1.f,fy=((y+.5f)/float(size))*2.f-1.f,r2=fx*fx+fy*fy;Pixel p{128,128,255,0};
  if(r2<=1.f){const float z=std::sqrt(std::max(0.f,1.f-r2));const V3 n=norm({fx,-fy,z});const float edge=sat((1.f-std::sqrt(r2))*4.f);p={b8(n.x*.5f+.5f),b8(n.y*.5f+.5f),b8(n.z*.5f+.5f),b8(edge)};}
  out.pixels[size_t(y)*size+x]=p;
 }return out;
}
void StampNormalPreset(Image& target,const Image& preset,float u,float v,float radiusPixels,float opacity){
 if(!target.valid()||!preset.valid()||radiusPixels<.5f||opacity<=0.f)return;
 opacity=sat(opacity);
 u-=std::floor(u);v-=std::floor(v);
 const int cx=int(std::floor(u*target.w)),cy=int(std::floor(v*target.h)),r=int(std::ceil(radiusPixels));
 auto decode=[](Pixel p){return norm({p.r/127.5f-1.f,p.g/127.5f-1.f,p.b/127.5f-1.f});};
 for(int oy=-r;oy<=r;++oy)for(int ox=-r;ox<=r;++ox){const float nx=ox/radiusPixels,ny=oy/radiusPixels,d2=nx*nx+ny*ny;if(d2>1.f)continue;const float pu=nx*.5f+.5f,pv=ny*.5f+.5f;Pixel sp=sample(preset,pu,pv);const float radial=sat(1.f-std::sqrt(d2));const float a=opacity*(sp.a/255.f)*std::min(1.f,radial*4.f);if(a<=.001f)continue;int tx=(cx+ox)%int(target.w),ty=(cy+oy)%int(target.h);if(tx<0)tx+=int(target.w);if(ty<0)ty+=int(target.h);Pixel& dp=target.pixels[size_t(ty)*target.w+uint32_t(tx)];V3 dn=decode(dp),sn=decode(sp);V3 n=norm({dn.x+(sn.x-dn.x)*a,dn.y+(sn.y-dn.y)*a,dn.z+(sn.z-dn.z)*a});dp.r=b8(n.x*.5f+.5f);dp.g=b8(n.y*.5f+.5f);dp.b=b8(n.z*.5f+.5f);dp.a=255;}
}
bool SaveNormalPreset(const Image& preset,const std::wstring& path,std::string& error){
 error.clear();if(!preset.valid()){error="Normal preset is empty";return false;}try{std::ofstream out(std::filesystem::path(path),std::ios::binary|std::ios::trunc);if(!out){error="Cannot create preset";return false;}const char magic[4]={'K','R','N','P'};const uint32_t ver=1;out.write(magic,4);out.write(reinterpret_cast<const char*>(&ver),4);out.write(reinterpret_cast<const char*>(&preset.w),4);out.write(reinterpret_cast<const char*>(&preset.h),4);out.write(reinterpret_cast<const char*>(preset.pixels.data()),std::streamsize(preset.pixels.size()*sizeof(Pixel)));if(!out){error="Preset write failed";return false;}return true;}catch(const std::exception& e){error=e.what();return false;}
}
bool LoadNormalPreset(const std::wstring& path,Image& preset,std::string& error){
 error.clear();preset={};try{std::ifstream in(std::filesystem::path(path),std::ios::binary);if(!in){error="Cannot open preset";return false;}char magic[4]{};uint32_t ver=0,w=0,h=0;in.read(magic,4);in.read(reinterpret_cast<char*>(&ver),4);in.read(reinterpret_cast<char*>(&w),4);in.read(reinterpret_cast<char*>(&h),4);if(std::memcmp(magic,"KRNP",4)!=0||ver!=1||!w||!h||w>8192||h>8192||size_t(w)*h>16000000){error="Invalid KRNP preset";return false;}Image out;out.w=w;out.h=h;out.pixels.resize(size_t(w)*h);in.read(reinterpret_cast<char*>(out.pixels.data()),std::streamsize(out.pixels.size()*sizeof(Pixel)));if(!in){error="Preset data truncated";return false;}preset=std::move(out);return true;}catch(const std::exception& e){error=e.what();return false;}
}

Image DefaultChecker(uint32_t w){Image im;im.w=im.h=w;im.pixels.resize(size_t(w)*w);for(uint32_t y=0;y<w;++y)for(uint32_t x=0;x<w;++x){int check=((x/32)^(y/32))&1;uint8_t k=uint8_t(check?166:104);im.pixels[size_t(y)*w+x]={uint8_t(k+18),k,uint8_t(k-7),255};}return im;}
BumpPair GenerateBump(const Sources& src,const Settings& cfg){
 const Image& ref=src.diffuse;
 if(!ref.valid())return {};
 // Keep the interactive preview inexpensive; export still uses original resolution.
 const float scale=std::min(1.f,1024.f/float(std::max(ref.w,ref.h)));
 const uint32_t w=std::max(1u,uint32_t(ref.w*scale));
 const uint32_t h=std::max(1u,uint32_t(ref.h*scale));
 BumpPair raw=oneMip(src,cfg,w,h);if(!raw.bump.valid())return {};
 // Preview actual post-BC3 data, not pristine source pixels. R2 compensation
 // is only meaningful when the first map has already undergone BC3 quantization.
 std::string err;BumpPair preview;
 preview.bump=DecodeDDS(EncodeBC3DDS(raw.bump,{raw.bump}),err);
 preview.bumpX=DecodeDDS(EncodeBC3DDS(raw.bumpX,{raw.bumpX}),err);
 return preview;
}
bool ExportBumpPair(const Sources& src,const Settings& cfg,const std::wstring& name,std::string& error){error.clear();const Image& ref=src.diffuse;if(!ref.valid()){error="Diffuse texture is required";return false;}if(ref.w>4096||ref.h>4096){error="Export limited to 4096x4096 in prototype";return false;}
 std::vector<Image> mips,corrections;uint32_t w=ref.w,h=ref.h;try{for(;;){BumpPair p=oneMip(src,cfg,w,h);if(!p.bump.valid()){error="Failed to generate bump";return false;}mips.push_back(std::move(p.bump));corrections.push_back(std::move(p.bumpX));if(w==1&&h==1)break;w=std::max(1u,w/2);h=std::max(1u,h/2);}auto a=EncodeBC3DDS(mips[0],mips),b=EncodeBC3DDS(corrections[0],corrections);if(a.empty()||b.empty())throw std::runtime_error("BC3 encode failed");std::filesystem::path base(name);std::filesystem::path path1(base.wstring()+L"_bump.dds"),path2(base.wstring()+L"_bump#.dds");auto write=[&](const std::filesystem::path& path,const std::vector<uint8_t>& bytes){std::ofstream out(path,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot open output DDS file");out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));if(!out)throw std::runtime_error("DDS file write failed");};write(path1,a);write(path2,b);return true;
 }catch(const std::exception& ex){error=ex.what();return false;}}
Mesh MakeCube(){Mesh m;MeshPart part;struct Face{V3 n,p[4];};const Face faces[]={{{0,0,1},{{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}}},{{0,0,-1},{{1,-1,-1},{-1,-1,-1},{-1,1,-1},{1,1,-1}}},{{1,0,0},{{1,-1,1},{1,-1,-1},{1,1,-1},{1,1,1}}},{{-1,0,0},{{-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1}}},{{0,1,0},{{-1,1,1},{1,1,1},{1,1,-1},{-1,1,-1}}},{{0,-1,0},{{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1}}}};
 const float uv[4][2]={{0,1},{1,1},{1,0},{0,0}};for(auto& f:faces){uint32_t o=uint32_t(part.vertices.size());for(int k=0;k<4;++k)part.vertices.push_back({f.p[k],f.n,uv[k][0],uv[k][1]});for(int k:{0,1,2,0,2,3})part.indices.push_back(o+k);}m.parts.push_back(std::move(part));m.sourceParts=1;return m;}

}
