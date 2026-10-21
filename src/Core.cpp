#include "Core.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <stdexcept>
namespace lab {
namespace {
float sat(float x){return std::max(0.f,std::min(x,1.f));}
uint8_t b8(float x){return uint8_t(sat(x)*255.f+0.5f);}
float lum(Pixel p){return (0.2126f*p.r+0.7152f*p.g+0.0722f*p.b)/255.f;}
Pixel sample(const Image& im,float u,float v){if(!im.valid())return {};u=sat(u)*im.w-.5f;v=sat(v)*im.h-.5f;int x=int(floor(u)),y=int(floor(v));float a=u-x,b=v-y;auto at=[&](int i,int j)->Pixel{return im.pixels[size_t(std::min(int(im.h)-1,std::max(0,j)))*im.w+std::min(int(im.w)-1,std::max(0,i))];};Pixel a0=at(x,y),b0=at(x+1,y),a1=at(x,y+1),b1=at(x+1,y+1),o;uint8_t* d=&o.r;for(int k=0;k<4;++k){const float v0=(&a0.r)[k]*(1-a)+(&b0.r)[k]*a,v1=(&a1.r)[k]*(1-a)+(&b1.r)[k]*a;d[k]=b8((v0*(1-b)+v1*b)/255.f);}return o;}
V3 norm(V3 n){float l=sqrt(n.x*n.x+n.y*n.y+n.z*n.z);if(l<1e-5f)return {0,0,1};return{n.x/l,n.y/l,n.z/l};}
Image makeBump(const Sources& s,const Settings& cfg,uint32_t w,uint32_t h){Image out;out.w=w;out.h=h;out.pixels.resize(size_t(w)*h);for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){float u=(x+.5f)/w,v=(y+.5f)/h;V3 n{0,0,1};if(s.normal.valid()){Pixel p=sample(s.normal,u,v);n=norm({p.r/127.5f-1,p.g/127.5f-1,p.b/127.5f-1});}else{
     const Image& hmap=s.height.valid()?s.height:s.diffuse;
     // Symmetric central gradient in texture space, with edge clamp.
     float a=lum(sample(hmap,u-1.f/w,v)),b=lum(sample(hmap,u+1.f/w,v));
     float c=lum(sample(hmap,u,v-1.f/h)),d=lum(sample(hmap,u,v+1.f/h));
     n=norm({(a-b)*2.f,(c-d)*2.f,1.f});
   }
   n=norm({n.x*cfg.normalStrength,n.y*cfg.normalStrength*(cfg.invertY?-1.f:1.f),n.z});
   float g=s.gloss.valid()?lum(sample(s.gloss,u,v)):lum(sample(s.diffuse,u,v));
   g=sat(g*cfg.gloss*2.f);Pixel p;
   p.r=b8(sqrt(g));p.g=b8(n.z*.5f+.5f);p.b=b8(n.y*.5f+.5f);p.a=b8(n.x*.5f+.5f);
   out.pixels[size_t(y)*w+x]=p;
 }return out;}
Image correction(const Image& original,const Image& decoded,const Sources& src,const Settings& cfg){Image out;out.w=original.w;out.h=original.h;out.pixels.resize(original.pixels.size());for(uint32_t y=0;y<out.h;++y)for(uint32_t x=0;x<out.w;++x){size_t i=size_t(y)*out.w+x;Pixel p=original.pixels[i],c=decoded.pixels[i];Pixel e;e.r=b8(.5f+(int(p.a)-int(c.a))/255.f);e.g=b8(.5f+(int(p.b)-int(c.b))/255.f);e.b=b8(.5f+(int(p.g)-int(c.g))/255.f);const Image& map=src.height.valid()?src.height:src.diffuse;float hh=lum(sample(map,(x+.5f)/out.w,(y+.5f)/out.h));e.a=b8(.5f+(hh-.5f)*cfg.height);out.pixels[i]=e;}return out;}
BumpPair oneMip(const Sources& src,const Settings& cfg,uint32_t w,uint32_t h){BumpPair pair;pair.bump=makeBump(src,cfg,w,h);auto compressed=EncodeBC3DDS(pair.bump,{pair.bump});std::string err;Image decoded=DecodeDDS(compressed,err);if(!decoded.valid())return {};pair.bumpX=correction(pair.bump,decoded,src,cfg);return pair;}
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
