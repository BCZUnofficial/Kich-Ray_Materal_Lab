#include "Core.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstring>
#include <chrono>
#include <atomic>
#include <system_error>
namespace lab {
namespace {
float sat(float x){return std::max(0.f,std::min(x,1.f));}
float brushMask(float distance,float alpha,float hardness){hardness=sat(hardness);const float feather=std::max(0.02f,1.f-hardness*0.98f);const float edge=sat((1.f-distance)/feather);return sat(alpha)*edge;}
uint8_t b8(float x){return uint8_t(sat(x)*255.f+0.5f);}
float lum(Pixel p){return (0.2126f*p.r+0.7152f*p.g+0.0722f*p.b)/255.f;}
Pixel sample(const Image& im,float u,float v){
 if(!im.valid())return {};u=sat(u)*im.w-.5f;v=sat(v)*im.h-.5f;const int x=int(std::floor(u)),y=int(std::floor(v));const float a=u-x,b=v-y;
 auto at=[&](int i,int j)->Pixel{return im.pixels[size_t(std::clamp(j,0,int(im.h)-1))*im.w+uint32_t(std::clamp(i,0,int(im.w)-1))];};
 const Pixel p00=at(x,y),p10=at(x+1,y),p01=at(x,y+1),p11=at(x+1,y+1);
 auto mix=[&](uint8_t q00,uint8_t q10,uint8_t q01,uint8_t q11){return b8(((q00*(1-a)+q10*a)*(1-b)+(q01*(1-a)+q11*a)*b)/255.f);};
 return {mix(p00.r,p10.r,p01.r,p11.r),mix(p00.g,p10.g,p01.g,p11.g),mix(p00.b,p10.b,p01.b,p11.b),mix(p00.a,p10.a,p01.a,p11.a)};
}
V3 norm(V3 n){float l=sqrt(n.x*n.x+n.y*n.y+n.z*n.z);if(l<1e-5f)return {0,0,1};return{n.x/l,n.y/l,n.z/l};}
Image makeBump(const Sources& s,const Settings& cfg,uint32_t w,uint32_t h){
 if(!s.diffuse.valid()||!w||!h)return {};
 Image out;out.w=w;out.h=h;out.pixels.resize(size_t(w)*h);
 const Image& heightMap=s.height.valid()?s.height:s.diffuse;
 const float relief=std::max(0.f,cfg.height)*8.f;
 const float normalGain=std::max(0.f,cfg.normalStrength);
 const float blurAmount=std::clamp(cfg.blur,0.f,1.f);
 const float sharpAmount=std::clamp(cfg.sharpness,0.f,2.f);
 auto wrapI=[](int v,int n){v%=n;return v<0?v+n:v;};
 std::vector<float> heightField(size_t(w)*h),filteredHeight;
 for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)
  heightField[size_t(y)*w+x]=lum(sample(heightMap,(x+.5f)/float(w),(y+.5f)/float(h)));
 if(blurAmount>0.0001f||sharpAmount>0.0001f){
  filteredHeight.resize(heightField.size());
  for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
   auto at=[&](int ox,int oy){return heightField[size_t(wrapI(int(y)+oy,int(h)))*w+uint32_t(wrapI(int(x)+ox,int(w)))];};
   const float c=at(0,0);
   const float avg=(c*4.f+(at(0,-1)+at(0,1)+at(-1,0)+at(1,0))*2.f+at(-1,-1)+at(1,-1)+at(-1,1)+at(1,1))/16.f;
   const float softened=c+(avg-c)*blurAmount;
   filteredHeight[size_t(y)*w+x]=std::clamp(softened+(c-avg)*sharpAmount,0.f,1.f);
  }
  heightField.swap(filteredHeight);
 }
 auto H=[&](int x,int y){return heightField[size_t(wrapI(y,int(h)))*w+uint32_t(wrapI(x,int(w)))];};
 auto decodeNormal=[&](Pixel p){return norm({p.r/127.5f-1.f,p.g/127.5f-1.f,p.b/127.5f-1.f});};
 std::vector<V3> normalField(size_t(w)*h),normalFiltered;
 for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
  const int ix=int(x),iy=int(y);
  const float tl=H(ix-1,iy-1),t=H(ix,iy-1),tr=H(ix+1,iy-1);
  const float l=H(ix-1,iy),r=H(ix+1,iy);
  const float bl=H(ix-1,iy+1),b=H(ix,iy+1),br=H(ix+1,iy+1);
  const float gx=tl+2.f*l+bl-tr-2.f*r-br;
  const float gy=tl+2.f*t+tr-bl-2.f*b-br;
  V3 reliefN=norm({gx*relief,gy*relief*(cfg.invertY?-1.f:1.f),1.f});
  const float u=(x+.5f)/float(w),v=(y+.5f)/float(h);
  V3 n=reliefN;
  if(s.normal.valid()){
   V3 base=decodeNormal(sample(s.normal,u,v));
   if(cfg.invertY)base.y=-base.y;
   const float blend=std::clamp(cfg.height*1.35f,0.f,1.35f);
   n=norm({base.x+reliefN.x*blend,base.y+reliefN.y*blend,std::max(0.15f,base.z*reliefN.z)});
  }
  // Apply strength once to the complete normal, including generated height
  // detail. Scaling only the authored layer left relief active at zero.
  normalField[size_t(y)*w+x]=norm({n.x*normalGain,n.y*normalGain,n.z});
 }
 // Blur/sharp must also affect an authored or painted normal layer.  Applying
 // it only to the scalar height source made the control almost invisible once
 // a normal map existed.
 if(blurAmount>0.0001f||sharpAmount>0.0001f){
  normalFiltered.resize(normalField.size());
  for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
   auto at=[&](int ox,int oy){return normalField[size_t(wrapI(int(y)+oy,int(h)))*w+uint32_t(wrapI(int(x)+ox,int(w)))];};
   V3 c=at(0,0),avg{};
   for(int oy=-1;oy<=1;++oy)for(int ox=-1;ox<=1;++ox){V3 q=at(ox,oy);const float k=(ox==0&&oy==0)?4.f:((ox==0||oy==0)?2.f:1.f);avg.x+=q.x*k;avg.y+=q.y*k;avg.z+=q.z*k;}
   avg=norm({avg.x/16.f,avg.y/16.f,avg.z/16.f});
   V3 softened=norm({c.x+(avg.x-c.x)*blurAmount,c.y+(avg.y-c.y)*blurAmount,c.z+(avg.z-c.z)*blurAmount});
   V3 n=norm({softened.x+(c.x-avg.x)*sharpAmount,softened.y+(c.y-avg.y)*sharpAmount,softened.z+(c.z-avg.z)*sharpAmount});
   normalFiltered[size_t(y)*w+x]=n;
  }
  normalField.swap(normalFiltered);
 }
 for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
  const float u=(x+.5f)/float(w),v=(y+.5f)/float(h);const V3 n=normalField[size_t(y)*w+x];
  const Pixel gp=s.gloss.valid()?sample(s.gloss,u,v):sample(s.diffuse,u,v);
  const float sourceGloss=s.gloss.valid()?lum(gp):(std::max(gp.r,std::max(gp.g,gp.b))/255.f);
  const float glossGain=std::clamp(cfg.gloss*2.f,0.f,2.f);
  const float g=sat(sourceGloss*glossGain);
  Pixel p;p.r=b8(std::sqrt(g));p.g=b8(n.z*.5f+.5f);p.b=b8(n.y*.5f+.5f);p.a=b8(n.x*.5f+.5f);
  out.pixels[size_t(y)*w+x]=p;
 }
 return out;
}
Image correction(const Image& original,const Image& decoded,const Sources& src,const Settings& cfg){
 Image out;out.w=original.w;out.h=original.h;out.pixels.resize(original.pixels.size());
 const Image& map=src.height.valid()?src.height:src.diffuse;
 for(uint32_t y=0;y<out.h;++y)for(uint32_t x=0;x<out.w;++x){
  const size_t i=size_t(y)*out.w+x;const Pixel p=original.pixels[i],c=decoded.pixels[i];
  // The shader adds bump.agb + bumpX.rgb - 1. Store the BC3 error
  // once around neutral 128; doubling it overcorrects every normal.
  // Alpha remains the independent height channel.
  auto err8=[](int d)->uint8_t{return uint8_t(std::clamp(d+128,0,255));};
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
struct SampleWeight {uint32_t index;float weight;};
using AxisWeights=std::vector<std::vector<SampleWeight>>;
AxisWeights resampleWeights(uint32_t source,uint32_t target){
 AxisWeights weights(target);
 for(uint32_t i=0;i<target;++i){
  if(target<source){
   const double begin=double(i)*source/target,end=double(i+1)*source/target;
   for(uint32_t j=uint32_t(std::floor(begin));j<std::min(source,uint32_t(std::ceil(end)));++j){const double overlap=std::min(end,double(j+1))-std::max(begin,double(j));if(overlap>0)weights[i].push_back({j,float(overlap/(end-begin))});}
  }else{
   const double center=(double(i)+.5)*source/target-.5;const int left=int(std::floor(center));const float t=float(center-left);
   const uint32_t a=uint32_t(std::clamp(left,0,int(source)-1)),b=uint32_t(std::clamp(left+1,0,int(source)-1));
   if(a==b)weights[i].push_back({a,1});else{weights[i].push_back({a,1-t});weights[i].push_back({b,t});}
  }
 }
 return weights;
}
Image resizeImageExact(const Image& src,uint32_t w,uint32_t h){
 if(!src.valid()||!w||!h)return {};
 if(src.w==w&&src.h==h)return src;
 Image out;out.w=w;out.h=h;out.pixels.resize(size_t(w)*h);
 const auto xs=resampleWeights(src.w,w),ys=resampleWeights(src.h,h);
 for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){double sum[4]={};for(const auto yy:ys[y])for(const auto xx:xs[x]){const Pixel p=src.pixels[size_t(yy.index)*src.w+xx.index];const double weight=double(xx.weight)*yy.weight;sum[0]+=p.r*weight;sum[1]+=p.g*weight;sum[2]+=p.b*weight;sum[3]+=p.a*weight;}auto byte=[](double v){return uint8_t(std::clamp(v,0.,255.)+.5);};out.pixels[size_t(y)*w+x]={byte(sum[0]),byte(sum[1]),byte(sum[2]),byte(sum[3])};}
 return out;
}
GrayImage resizeGrayExact(const GrayImage& src,uint32_t w,uint32_t h){
 if(!src.valid()||!w||!h)return {};
 if(src.w==w&&src.h==h)return src;
 GrayImage out;out.w=w;out.h=h;out.pixels.resize(size_t(w)*h);
 const auto xs=resampleWeights(src.w,w),ys=resampleWeights(src.h,h);
 for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){double sum=0;for(const auto yy:ys[y])for(const auto xx:xs[x])sum+=src.pixels[size_t(yy.index)*src.w+xx.index]*double(xx.weight)*yy.weight;out.pixels[size_t(y)*w+x]=uint8_t(std::clamp(sum,0.,255.)+.5);}
 return out;
}
BumpPair packLayersOneMip(const Image& normal,const GrayImage& gloss,const GrayImage& height,uint32_t w,uint32_t h,bool bc3Correction,const GrayImage* baseHeight){
 if(!normal.valid()||!gloss.valid()||!height.valid()||!w||!h)return {};
 // Derive painted height relief at the authored resolution, then resample the
 // result. Recomputing Sobel at preview/mip resolution changes its strength.
 Image composed=normal;
 auto decode=[](Pixel p){return norm({p.r/127.5f-1.f,p.g/127.5f-1.f,p.b/127.5f-1.f});};
 if(baseHeight&&baseHeight->valid()){
  const GrayImage edited=resizeGrayExact(height,normal.w,normal.h),base=resizeGrayExact(*baseHeight,normal.w,normal.h);
  auto wrap=[](int v,int m){v%=m;return v<0?v+m:v;};
  for(uint32_t y=0;y<normal.h;++y)for(uint32_t x=0;x<normal.w;++x){
   auto D=[&](int ox,int oy){const int xx=wrap(int(x)+ox,int(normal.w)),yy=wrap(int(y)+oy,int(normal.h));const size_t at=size_t(yy)*normal.w+uint32_t(xx);return (int(edited.pixels[at])-int(base.pixels[at]))/255.f;};
   const float gx=D(-1,-1)+2.f*D(-1,0)+D(-1,1)-D(1,-1)-2.f*D(1,0)-D(1,1);
   const float gy=D(-1,-1)+2.f*D(0,-1)+D(1,-1)-D(-1,1)-2.f*D(0,1)-D(1,1);
   // A zero height difference is exactly neutral, including steep normals.
   if(gx==0.f&&gy==0.f)continue;
   const size_t i=size_t(y)*normal.w+x;const V3 q=decode(normal.pixels[i]);const float z=std::max(.001f,q.z);
   const V3 n=norm({q.x+gx*8.f*z,q.y+gy*8.f*z,q.z});
   composed.pixels[i]={b8(n.x*.5f+.5f),b8(n.y*.5f+.5f),b8(n.z*.5f+.5f),255};
  }
 }
 Image n=resizeImageExact(composed,w,h);GrayImage g=resizeGrayExact(gloss,w,h),hh=resizeGrayExact(height,w,h);
 if(!n.valid()||!g.valid()||!hh.valid())return {};
 BumpPair pair;pair.bump.w=w;pair.bump.h=h;pair.bump.pixels.resize(size_t(w)*h);pair.bumpX.w=w;pair.bumpX.h=h;pair.bumpX.pixels.resize(size_t(w)*h);
 for(size_t i=0;i<pair.bump.pixels.size();++i){const V3 q=decode(n.pixels[i]);const float gf=g.pixels[i]/255.f;pair.bump.pixels[i]={b8(std::sqrt(sat(gf))),b8(q.z*.5f+.5f),b8(q.y*.5f+.5f),b8(q.x*.5f+.5f)};pair.bumpX.pixels[i]={128,128,128,hh.pixels[i]};}
 if(bc3Correction){auto encoded=EncodeBC3DDS(pair.bump,{pair.bump});std::string err;Image decoded=DecodeDDS(encoded,err);if(!decoded.valid())return {};auto err8=[](int d)->uint8_t{return uint8_t(std::clamp(d+128,0,255));};for(size_t i=0;i<pair.bump.pixels.size();++i){const Pixel p=pair.bump.pixels[i],c=decoded.pixels[i];pair.bumpX.pixels[i].r=err8(int(p.a)-int(c.a));pair.bumpX.pixels[i].g=err8(int(p.b)-int(c.b));pair.bumpX.pixels[i].b=err8(int(p.g)-int(c.g));}}
 return pair;
}

}

Image ResizeImageMax(const Image& src,uint32_t maxDimension){
 if(!src.valid()||!maxDimension)return {};
 const uint32_t biggest=std::max(src.w,src.h);if(biggest<=maxDimension)return src;
 const float k=float(maxDimension)/float(biggest);const uint32_t w=std::max(1u,uint32_t(std::lround(src.w*k))),h=std::max(1u,uint32_t(std::lround(src.h*k)));
 return resizeImageExact(src,w,h);
}
V3 DecodeBumpNormal(Pixel packed,Pixel correction,bool rendered){
 V3 n{float(int(packed.a)+int(correction.r)-255),float(int(packed.b)+int(correction.g)-255),float(int(packed.g)+int(correction.b)-255)};
 if(rendered)n.z*=.5f;return norm(n);
}
Image UnpackNormalMap(const Image& bump,const Image& bumpX){
 if(!bump.valid()||!bumpX.valid()||bump.w!=bumpX.w||bump.h!=bumpX.h)return {};
 const uint32_t w=bump.w,h=bump.h;
 Image out;out.w=w;out.h=h;out.pixels.resize(size_t(w)*h);
 for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
  const Pixel n=sample(bump,(x+.5f)/float(w),(y+.5f)/float(h));
  const Pixel e=sample(bumpX,(x+.5f)/float(w),(y+.5f)/float(h));
  V3 v=DecodeBumpNormal(n,e);out.pixels[size_t(y)*w+x]={b8(v.x*.5f+.5f),b8(v.y*.5f+.5f),b8(v.z*.5f+.5f),255};
 }
 return out;
}
Image UnpackGlossMap(const Image& bump){
 if(!bump.valid())return {};
 Image out;out.w=bump.w;out.h=bump.h;out.pixels.resize(bump.pixels.size());
 for(size_t i=0;i<bump.pixels.size();++i){const float p=bump.pixels[i].r/255.f;const uint8_t g=b8(p*p);out.pixels[i]={g,g,g,255};}
 return out;
}
Image UnpackHeightMap(const Image& bumpX){
 if(!bumpX.valid())return {};
 Image out;out.w=bumpX.w;out.h=bumpX.h;out.pixels.resize(bumpX.pixels.size());
 for(size_t i=0;i<bumpX.pixels.size();++i){const uint8_t h=bumpX.pixels[i].a;out.pixels[i]={h,h,h,255};}
 return out;
}
GrayImage UnpackGlossGray(const Image& bump){
 if(!bump.valid())return {};
 GrayImage out;out.w=bump.w;out.h=bump.h;out.pixels.resize(bump.pixels.size());
 for(size_t i=0;i<bump.pixels.size();++i){const float p=bump.pixels[i].r/255.f;out.pixels[i]=b8(p*p);}
 return out;
}
GrayImage UnpackHeightGray(const Image& bumpX){
 if(!bumpX.valid())return {};
 GrayImage out;out.w=bumpX.w;out.h=bumpX.h;out.pixels.resize(bumpX.pixels.size());
 for(size_t i=0;i<bumpX.pixels.size();++i)out.pixels[i]=bumpX.pixels[i].a;
 return out;
}
GrayImage ToGrayImage(const Image& src,uint32_t maxDimension){
 if(!src.valid())return {};
 Image resized=maxDimension?ResizeImageMax(src,maxDimension):src;
 GrayImage out;out.w=resized.w;out.h=resized.h;out.pixels.resize(resized.pixels.size());
 for(size_t i=0;i<resized.pixels.size();++i)out.pixels[i]=b8(lum(resized.pixels[i]));
 return out;
}
Image GrayToImage(const GrayImage& src,uint32_t maxDimension){
 if(!src.valid())return {};
 uint32_t w=src.w,h=src.h;
 if(maxDimension&&std::max(w,h)>maxDimension){const float k=float(maxDimension)/float(std::max(w,h));w=std::max(1u,uint32_t(std::lround(w*k)));h=std::max(1u,uint32_t(std::lround(h*k)));}
 Image out;out.w=w;out.h=h;out.pixels.resize(size_t(w)*h);
 auto at=[&](uint32_t x,uint32_t y){return src.pixels[size_t(y)*src.w+x];};
 for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){const uint32_t sx=std::min(src.w-1,uint32_t((uint64_t(x)*src.w)/w)),sy=std::min(src.h-1,uint32_t((uint64_t(y)*src.h)/h));const uint8_t q=at(sx,sy);out.pixels[size_t(y)*w+x]={q,q,q,255};}
 return out;
}
Image MakeDefaultNormalPreset(uint32_t size){
 size=std::max(8u,size);Image out;out.w=out.h=size;out.pixels.resize(size_t(size)*size);
 for(uint32_t y=0;y<size;++y)for(uint32_t x=0;x<size;++x){
  const float fx=((x+.5f)/float(size))*2.f-1.f,fy=((y+.5f)/float(size))*2.f-1.f,r2=fx*fx+fy*fy;Pixel p{128,128,255,0};
  if(r2<=1.f){const float z=std::sqrt(std::max(0.f,1.f-r2));const V3 n=norm({fx,-fy,z});const float edge=sat((1.f-std::sqrt(r2))*4.f);p={b8(n.x*.5f+.5f),b8(n.y*.5f+.5f),b8(n.z*.5f+.5f),b8(edge)};}
  out.pixels[size_t(y)*size+x]=p;
 }
 return out;
}
void StampNormalPreset(Image& target,const Image& preset,float u,float v,float radiusPixels,float opacity){
 if(!target.valid()||!preset.valid()||radiusPixels<.5f||opacity<=0.f)return;
 opacity=sat(opacity);u-=std::floor(u);v-=std::floor(v);
 const int cx=int(std::floor(u*target.w)),cy=int(std::floor(v*target.h)),r=int(std::ceil(radiusPixels));
 auto decode=[](Pixel p){return norm({p.r/127.5f-1.f,p.g/127.5f-1.f,p.b/127.5f-1.f});};
 for(int oy=-r;oy<=r;++oy)for(int ox=-r;ox<=r;++ox){const float nx=ox/radiusPixels,ny=oy/radiusPixels,d2=nx*nx+ny*ny;if(d2>1.f)continue;const float pu=nx*.5f+.5f,pv=ny*.5f+.5f;const Pixel sp=sample(preset,pu,pv);const float radial=sat(1.f-std::sqrt(d2));const float a=opacity*(sp.a/255.f)*std::min(1.f,radial*4.f);if(a<.08f)continue;int tx=(cx+ox)%int(target.w),ty=(cy+oy)%int(target.h);if(tx<0)tx+=int(target.w);if(ty<0)ty+=int(target.h);const V3 sn=decode(sp);target.pixels[size_t(ty)*target.w+uint32_t(tx)]={b8(sn.x*.5f+.5f),b8(sn.y*.5f+.5f),b8(sn.z*.5f+.5f),255};}
}
void StampScalarPreset(Image& target,const Image& preset,float u,float v,float radiusPixels,float value){
 if(!target.valid()||!preset.valid()||radiusPixels<.5f)return;
 value=sat(value);u-=std::floor(u);v-=std::floor(v);const uint8_t q=b8(value);
 const int cx=int(std::floor(u*target.w)),cy=int(std::floor(v*target.h)),r=int(std::ceil(radiusPixels));
 for(int oy=-r;oy<=r;++oy)for(int ox=-r;ox<=r;++ox){const float nx=ox/radiusPixels,ny=oy/radiusPixels,d2=nx*nx+ny*ny;if(d2>1.f)continue;const float pu=nx*.5f+.5f,pv=ny*.5f+.5f;const Pixel sp=sample(preset,pu,pv);const float radial=sat(1.f-std::sqrt(d2));const float a=(sp.a/255.f)*std::min(1.f,radial*4.f);if(a<.08f)continue;int tx=(cx+ox)%int(target.w),ty=(cy+oy)%int(target.h);if(tx<0)tx+=int(target.w);if(ty<0)ty+=int(target.h);target.pixels[size_t(ty)*target.w+uint32_t(tx)]={q,q,q,255};}
}
void StampScalarPreset(GrayImage& target,const Image& preset,float u,float v,float radiusPixels,float value){
 if(!target.valid()||!preset.valid()||radiusPixels<.5f)return;
 value=sat(value);u-=std::floor(u);v-=std::floor(v);const uint8_t q=b8(value);
 const int cx=int(std::floor(u*target.w)),cy=int(std::floor(v*target.h)),r=int(std::ceil(radiusPixels));
 for(int oy=-r;oy<=r;++oy)for(int ox=-r;ox<=r;++ox){const float nx=ox/radiusPixels,ny=oy/radiusPixels,d2=nx*nx+ny*ny;if(d2>1.f)continue;const float pu=nx*.5f+.5f,pv=ny*.5f+.5f;const Pixel sp=sample(preset,pu,pv);const float radial=sat(1.f-std::sqrt(d2));const float a=(sp.a/255.f)*std::min(1.f,radial*4.f);if(a<.08f)continue;int tx=(cx+ox)%int(target.w),ty=(cy+oy)%int(target.h);if(tx<0)tx+=int(target.w);if(ty<0)ty+=int(target.h);target.pixels[size_t(ty)*target.w+uint32_t(tx)]=q;}
}
void StampNormalPresetLayer(Image& target,GrayImage& coverage,const Image& preset,float u,float v,float radiusPixels){
 if(!target.valid()||!preset.valid()||radiusPixels<.5f)return;
 if(!coverage.valid()||coverage.w!=target.w||coverage.h!=target.h){coverage.w=target.w;coverage.h=target.h;coverage.pixels.assign(size_t(target.w)*target.h,0);}
 u-=std::floor(u);v-=std::floor(v);const int cx=int(std::floor(u*target.w)),cy=int(std::floor(v*target.h)),r=int(std::ceil(radiusPixels));
 auto decode=[](Pixel p){return norm({p.r/127.5f-1.f,p.g/127.5f-1.f,p.b/127.5f-1.f});};
 for(int oy=-r;oy<=r;++oy)for(int ox=-r;ox<=r;++ox){const float nx=ox/radiusPixels,ny=oy/radiusPixels,d2=nx*nx+ny*ny;if(d2>1.f)continue;const float pu=nx*.5f+.5f,pv=ny*.5f+.5f;const Pixel sp=sample(preset,pu,pv);const float radial=sat(1.f-std::sqrt(d2));const uint8_t cov=b8((sp.a/255.f)*std::min(1.f,radial*4.f));if(cov<20)continue;int tx=(cx+ox)%int(target.w),ty=(cy+oy)%int(target.h);if(tx<0)tx+=int(target.w);if(ty<0)ty+=int(target.h);const size_t at=size_t(ty)*target.w+uint32_t(tx);if(cov<=coverage.pixels[at])continue;coverage.pixels[at]=cov;const V3 sn=decode(sp);target.pixels[at]={b8(sn.x*.5f+.5f),b8(sn.y*.5f+.5f),b8(sn.z*.5f+.5f),255};}
}
void StampScalarPresetLayer(GrayImage& target,GrayImage& coverage,const Image& preset,float u,float v,float radiusPixels,float value){
 if(!target.valid()||!preset.valid()||radiusPixels<.5f)return;
 if(!coverage.valid()||coverage.w!=target.w||coverage.h!=target.h){coverage.w=target.w;coverage.h=target.h;coverage.pixels.assign(size_t(target.w)*target.h,0);}
 value=sat(value);const uint8_t q=b8(value);u-=std::floor(u);v-=std::floor(v);const int cx=int(std::floor(u*target.w)),cy=int(std::floor(v*target.h)),r=int(std::ceil(radiusPixels));
 for(int oy=-r;oy<=r;++oy)for(int ox=-r;ox<=r;++ox){const float nx=ox/radiusPixels,ny=oy/radiusPixels,d2=nx*nx+ny*ny;if(d2>1.f)continue;const float pu=nx*.5f+.5f,pv=ny*.5f+.5f;const Pixel sp=sample(preset,pu,pv);const float radial=sat(1.f-std::sqrt(d2));const uint8_t cov=b8((sp.a/255.f)*std::min(1.f,radial*4.f));if(cov<20)continue;int tx=(cx+ox)%int(target.w),ty=(cy+oy)%int(target.h);if(tx<0)tx+=int(target.w);if(ty<0)ty+=int(target.h);const size_t at=size_t(ty)*target.w+uint32_t(tx);if(cov<=coverage.pixels[at])continue;coverage.pixels[at]=cov;target.pixels[at]=q;}
}
namespace {
bool samePixel(Pixel a,Pixel b){return a.r==b.r&&a.g==b.g&&a.b==b.b&&a.a==b.a;}
// Coverage is per stroke, not per event: overlapping dabs cannot repeatedly
// amplify a pixel merely because the mouse delivered more move messages.
template<class Paint>
bool stampLayer(uint32_t w,uint32_t h,GrayImage& coverage,const Image& preset,float u,float v,float radiusPixels,float hardness,Paint paint){
 if(!w||!h||!preset.valid()||!std::isfinite(radiusPixels)||radiusPixels<.5f||!std::isfinite(u)||!std::isfinite(v))return false;
 if(!coverage.valid()||coverage.w!=w||coverage.h!=h){coverage.w=w;coverage.h=h;coverage.pixels.assign(size_t(w)*h,0);}
 u-=std::floor(u);v-=std::floor(v);const int cx=int(std::floor(u*w)),cy=int(std::floor(v*h)),r=int(std::ceil(std::min(radiusPixels,8192.f)));
 bool changed=false;
 for(int oy=-r;oy<=r;++oy)for(int ox=-r;ox<=r;++ox){
  const float nx=ox/radiusPixels,ny=oy/radiusPixels,d2=nx*nx+ny*ny;if(d2>1.f)continue;
  const Pixel sp=sample(preset,nx*.5f+.5f,ny*.5f+.5f);const uint8_t cov=b8(brushMask(std::sqrt(d2),sp.a/255.f,hardness));if(!cov)continue;
  int tx=(cx+ox)%int(w),ty=(cy+oy)%int(h);if(tx<0)tx+=int(w);if(ty<0)ty+=int(h);
  const size_t at=size_t(ty)*w+uint32_t(tx);if(cov<=coverage.pixels[at])continue;coverage.pixels[at]=cov;
  changed=paint(at,tx,ty,sp,cov/255.f)||changed;
 }
 return changed;
}
V3 decodeLayerNormal(Pixel p){return norm({p.r/127.5f-1.f,p.g/127.5f-1.f,p.b/127.5f-1.f});}
Pixel encodeLayerNormal(V3 n){return {b8(n.x*.5f+.5f),b8(n.y*.5f+.5f),b8(n.z*.5f+.5f),255};}
}
bool StampNormalReliefLayer(Image& target,const Image& base,GrayImage& coverage,const Image& preset,float u,float v,float radiusPixels,float strength,bool remove,float hardness){
 if(!target.valid()||!base.valid()||target.w!=base.w||target.h!=base.h||!std::isfinite(strength)||strength<=0.f)return false;
 strength=sat(strength);
 return stampLayer(target.w,target.h,coverage,preset,u,v,radiusPixels,hardness,[&](size_t at,int,int,Pixel sp,float mask){
  const V3 bn=decodeLayerNormal(base.pixels[at]);const float a=strength*mask;V3 n;
  if(remove){
   // Remove detail by approaching the flat surface, never by reversing XY.
   n=norm({bn.x*(1.f-a),bn.y*(1.f-a),bn.z+(1.f-bn.z)*a});
  }else{
   // A preset adds its tangent-plane slopes to the existing detail. Canonical
   // (128,128,255) is neutral and must not flatten an authored normal map.
   const float dx=(int(sp.r)-128)/127.f,dy=(int(sp.g)-128)/127.f;
   if(dx==0.f&&dy==0.f)return false;
   const float dz=std::max(.2f,sp.b/127.5f-1.f),bz=std::max(.001f,bn.z);
   n=norm({bn.x+a*dx/dz*bz,bn.y+a*dy/dz*bz,bn.z});
  }
  const Pixel out=encodeLayerNormal(n);if(samePixel(out,target.pixels[at]))return false;target.pixels[at]=out;return true;
 });
}
bool StampScalarDeltaLayer(GrayImage& target,const GrayImage& base,GrayImage& coverage,const Image& preset,float u,float v,float radiusPixels,float delta,float hardness){
 if(!target.valid()||!base.valid()||target.w!=base.w||target.h!=base.h||!std::isfinite(delta)||delta==0.f)return false;
 delta=std::clamp(delta,-1.f,1.f);
 return stampLayer(target.w,target.h,coverage,preset,u,v,radiusPixels,hardness,[&](size_t at,int,int,Pixel,float mask){
  const uint8_t out=b8(base.pixels[at]/255.f+delta*mask);if(out==target.pixels[at])return false;target.pixels[at]=out;return true;
 });
}
bool StampNormalFilterLayer(Image& target,const Image& base,GrayImage& coverage,const Image& preset,float u,float v,float radiusPixels,float strength,bool sharpen,float hardness){
 if(!target.valid()||!base.valid()||target.w!=base.w||target.h!=base.h||!std::isfinite(strength)||strength<=0.f)return false;
 strength=sat(strength);auto wrap=[](int a,int n){a%=n;return a<0?a+n:a;};
 return stampLayer(target.w,target.h,coverage,preset,u,v,radiusPixels,hardness,[&](size_t at,int tx,int ty,Pixel,float mask){
  const V3 c=decodeLayerNormal(base.pixels[at]);V3 avg{};
  for(int fy=-1;fy<=1;++fy)for(int fx=-1;fx<=1;++fx){const V3 q=decodeLayerNormal(base.pixels[size_t(wrap(ty+fy,int(base.h)))*base.w+uint32_t(wrap(tx+fx,int(base.w)))]);const float k=(fx==0&&fy==0)?4.f:((fx==0||fy==0)?2.f:1.f);avg.x+=q.x*k;avg.y+=q.y*k;avg.z+=q.z*k;}
  avg=norm({avg.x/16.f,avg.y/16.f,avg.z/16.f});const float a=strength*mask;
  const V3 filtered=sharpen?norm({c.x+(c.x-avg.x)*2.f,c.y+(c.y-avg.y)*2.f,c.z+(c.z-avg.z)*2.f}):avg;
  const Pixel out=encodeLayerNormal(norm({c.x+(filtered.x-c.x)*a,c.y+(filtered.y-c.y)*a,c.z+(filtered.z-c.z)*a}));
  if(samePixel(out,target.pixels[at]))return false;target.pixels[at]=out;return true;
 });
}
void StampMaterialPreset(Image& normalTarget,Image& glossTarget,const Image& preset,float u,float v,float radiusPixels,float glossValue){
 if(!normalTarget.valid()||!preset.valid()||radiusPixels<.5f)return;
 if(!glossTarget.valid()||glossTarget.w!=normalTarget.w||glossTarget.h!=normalTarget.h){glossTarget.w=normalTarget.w;glossTarget.h=normalTarget.h;glossTarget.pixels.assign(normalTarget.pixels.size(),Pixel{128,128,128,255});}
 glossValue=sat(glossValue);u-=std::floor(u);v-=std::floor(v);
 const int cx=int(std::floor(u*normalTarget.w)),cy=int(std::floor(v*normalTarget.h)),r=int(std::ceil(radiusPixels));
 auto decode=[](Pixel p){return norm({p.r/127.5f-1.f,p.g/127.5f-1.f,p.b/127.5f-1.f});};
 for(int oy=-r;oy<=r;++oy)for(int ox=-r;ox<=r;++ox){const float nx=ox/radiusPixels,ny=oy/radiusPixels,d2=nx*nx+ny*ny;if(d2>1.f)continue;const float pu=nx*.5f+.5f,pv=ny*.5f+.5f;const Pixel sp=sample(preset,pu,pv);const float radial=sat(1.f-std::sqrt(d2));const float a=(sp.a/255.f)*std::min(1.f,radial*4.f);if(a<=.001f)continue;int tx=(cx+ox)%int(normalTarget.w),ty=(cy+oy)%int(normalTarget.h);if(tx<0)tx+=int(normalTarget.w);if(ty<0)ty+=int(normalTarget.h);const size_t at=size_t(ty)*normalTarget.w+uint32_t(tx);Pixel& np=normalTarget.pixels[at];V3 dn=decode(np),sn=decode(sp);V3 n=norm({dn.x+(sn.x-dn.x)*a,dn.y+(sn.y-dn.y)*a,dn.z+(sn.z-dn.z)*a});np={b8(n.x*.5f+.5f),b8(n.y*.5f+.5f),b8(n.z*.5f+.5f),255};Pixel& gp=glossTarget.pixels[at];const float old=lum(gp),g=old+(glossValue-old)*a;const uint8_t q=b8(g);gp={q,q,q,255};}
}
bool SaveNormalPreset(const Image& preset,float gloss,const std::wstring& path,std::string& error){
 error.clear();if(!preset.valid()){error="Normal preset is empty";return false;}try{std::ofstream out(std::filesystem::path(path),std::ios::binary|std::ios::trunc);if(!out){error="Cannot create preset";return false;}const char magic[4]={'K','R','N','P'};const uint32_t ver=2;gloss=sat(gloss);out.write(magic,4);out.write(reinterpret_cast<const char*>(&ver),4);out.write(reinterpret_cast<const char*>(&preset.w),4);out.write(reinterpret_cast<const char*>(&preset.h),4);out.write(reinterpret_cast<const char*>(&gloss),4);out.write(reinterpret_cast<const char*>(preset.pixels.data()),std::streamsize(preset.pixels.size()*sizeof(Pixel)));if(!out){error="Preset write failed";return false;}return true;}catch(const std::exception& e){error=e.what();return false;}
}
bool LoadNormalPreset(const std::wstring& path,Image& preset,float& gloss,std::string& error){
 error.clear();preset={};gloss=.45f;try{std::ifstream in(std::filesystem::path(path),std::ios::binary);if(!in){error="Cannot open preset";return false;}char magic[4]{};uint32_t ver=0,w=0,h=0;in.read(magic,4);in.read(reinterpret_cast<char*>(&ver),4);in.read(reinterpret_cast<char*>(&w),4);in.read(reinterpret_cast<char*>(&h),4);if(std::memcmp(magic,"KRNP",4)!=0||(ver!=1&&ver!=2)||!w||!h||w>8192||h>8192||size_t(w)*h>16000000){error="Invalid KRNP preset";return false;}if(ver>=2){in.read(reinterpret_cast<char*>(&gloss),4);if(!in||!std::isfinite(gloss)){error="Preset gloss is invalid";return false;}gloss=sat(gloss);}Image out;out.w=w;out.h=h;out.pixels.resize(size_t(w)*h);in.read(reinterpret_cast<char*>(out.pixels.data()),std::streamsize(out.pixels.size()*sizeof(Pixel)));if(!in){error="Preset data truncated";return false;}preset=std::move(out);return true;}catch(const std::exception& e){error=e.what();return false;}
}
bool SaveNormalPreset(const Image& preset,const std::wstring& path,std::string& error){return SaveNormalPreset(preset,.45f,path,error);}
bool LoadNormalPreset(const std::wstring& path,Image& preset,std::string& error){float gloss=.45f;return LoadNormalPreset(path,preset,gloss,error);}


Settings MinimalBaseSettings(){Settings s;s.normalStrength=.25f;s.height=.10f;s.gloss=.06f;s.blur=.65f;s.sharpness=0;return s;}

Image MakeSurfaceNormalPreset(SurfacePreset kind,uint32_t size){
 if(size<8||size>1024||int(kind)<0||int(kind)>4)return {};
 constexpr float tau=6.28318530718f;
 auto hash=[](int x,int y){uint32_t v=uint32_t(x)*0x9e3779b9u^uint32_t(y)*0x85ebca6bu^0x31415926u;v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;return (v&0xffff)/65535.f;};
 auto noise=[&](float u,float v,int cells){float x=u*cells,y=v*cells;int ix=int(std::floor(x)),iy=int(std::floor(y));x-=ix;y-=iy;x=x*x*(3-2*x);y=y*y*(3-2*y);auto at=[&](int a,int b){return hash((a%cells+cells)%cells,(b%cells+cells)%cells);};return (at(ix,iy)*(1-x)+at(ix+1,iy)*x)*(1-y)+(at(ix,iy+1)*(1-x)+at(ix+1,iy+1)*x)*y;};
 auto height=[&](float u,float v){
  const float a=noise(u,v,8),b=noise(u,v,23),c=noise(u,v,47);
  switch(kind){
   case SurfacePreset::Glass:return .08f*noise(u,v,3); // slight polishing waviness
   case SurfacePreset::Fabric:{float x=std::cos(tau*16*u),y=std::cos(tau*16*v);return .055f*(x+y)+.025f*x*y;}
   case SurfacePreset::Metal:return .015f*std::sin(tau*32*v)+.008f*c; // brushed grooves
   case SurfacePreset::Concrete:return .09f*a+.045f*b+.025f*c; // bounded multiscale grains
   case SurfacePreset::Leather:{float pore=std::pow(std::max(0.f,2*b-1),4.f);return .04f*a-.065f*pore+.006f*c;}
  }return 0.f;
 };
 Image result;result.w=result.h=size;result.pixels.resize(size_t(size)*size);
 const float step=1.f/size;
 for(uint32_t y=0;y<size;++y)for(uint32_t x=0;x<size;++x){const float u=(x+.5f)*step,v=(y+.5f)*step;
  // Central differences at a fixed UV scale keep the material independent of preset resolution.
  const float du=(height(u+step,v)-height(u-step,v))/(2*step*16),dv=(height(u,v+step)-height(u,v-step))/(2*step*16);
  const auto n=norm({-du,-dv,1});result.pixels[size_t(y)*size+x]={b8(n.x*.5f+.5f),b8(n.y*.5f+.5f),b8(n.z*.5f+.5f),255};}
 return result;
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
BumpPair GenerateBumpPreview(const Sources& src,const Settings& cfg,uint32_t maxDimension){
 const Image& ref=src.diffuse;if(!ref.valid())return {};
 if(!maxDimension&&(ref.w>4096||ref.h>4096))return {};
 const float scale=maxDimension?std::min(1.f,float(maxDimension)/float(std::max(ref.w,ref.h))):1.f;const uint32_t w=std::max(1u,uint32_t(ref.w*scale)),h=std::max(1u,uint32_t(ref.h*scale));
 BumpPair p;p.bump=makeBump(src,cfg,w,h);if(!p.bump.valid())return {};p.bumpX=correction(p.bump,p.bump,src,cfg);return p;
}
BumpPair PackBumpLayersPreview(const Image& normal,const GrayImage& gloss,const GrayImage& height,uint32_t maxDimension,const GrayImage* baseHeight){
 if(!normal.valid()||!gloss.valid()||!height.valid())return {};
 uint32_t w=normal.w,h=normal.h;
 if(maxDimension&&std::max(w,h)>maxDimension){const float k=float(maxDimension)/float(std::max(w,h));w=std::max(1u,uint32_t(std::lround(w*k)));h=std::max(1u,uint32_t(std::lround(h*k)));}
 return packLayersOneMip(normal,gloss,height,w,h,false,baseHeight);
}
BumpPair CompositeBumpLayersPreview(const Image& normal,const GrayImage& gloss,const GrayImage& height,const Image& baseNormal,const GrayImage& baseGloss,const GrayImage& baseHeight,const BumpPair& originalPair,uint32_t maxDimension){
 BumpPair edited=PackBumpLayersPreview(normal,gloss,height,maxDimension,&baseHeight);
 if(!edited.bump.valid()||!originalPair.bump.valid()||!originalPair.bumpX.valid())return edited;
 BumpPair baseline=packLayersOneMip(baseNormal,baseGloss,baseHeight,edited.bump.w,edited.bump.h,false,&baseHeight);
 if(!baseline.bump.valid())return edited;
 const Image original=resizeImageExact(originalPair.bump,edited.bump.w,edited.bump.h),correction=resizeImageExact(originalPair.bumpX,edited.bump.w,edited.bump.h);
 for(size_t i=0;i<edited.bump.pixels.size();++i){
  Pixel& p=edited.bump.pixels[i];Pixel& e=edited.bumpX.pixels[i];const Pixel b=baseline.bump.pixels[i];
  if(p.a==b.a&&p.b==b.b&&p.g==b.g){p.a=original.pixels[i].a;p.b=original.pixels[i].b;p.g=original.pixels[i].g;e.r=correction.pixels[i].r;e.g=correction.pixels[i].g;e.b=correction.pixels[i].b;}
  if(p.r==b.r)p.r=original.pixels[i].r;
  if(e.a==baseline.bumpX.pixels[i].a)e.a=correction.pixels[i].a;
 }
 return edited;
}
namespace {
// Preserve pre-existing correction (imported pairs) and compensate only the
// quantization introduced by this encoding. The full residual is deliberate:
// it measures better with our encoder than the old SDK's 2/3 heuristic.
bool correctCompressedPair(BumpPair& pair){
 std::string error;const Image decoded=DecodeDDS(EncodeBC3DDS(pair.bump,{pair.bump}),error);if(!decoded.valid())return false;
 for(size_t i=0;i<pair.bump.pixels.size();++i){const Pixel p=pair.bump.pixels[i],d=decoded.pixels[i];Pixel& e=pair.bumpX.pixels[i];e.r=uint8_t(std::clamp(int(e.r)+int(p.a)-int(d.a),0,255));e.g=uint8_t(std::clamp(int(e.g)+int(p.b)-int(d.b),0,255));e.b=uint8_t(std::clamp(int(e.b)+int(p.g)-int(d.g),0,255));}
 return true;
}
bool encodePreparedPair(const BumpPair& top,EncodedBumpPair& output){
 if(!top.bump.valid()||!top.bumpX.valid()||top.bump.w!=top.bumpX.w||top.bump.h!=top.bumpX.h||top.bump.w>4096||top.bump.h>4096)throw std::runtime_error("A matching bump pair up to 4096 x 4096 is required");
 // Derive mipmaps from the authored top level. Re-running Sobel at each mip
 // produced different relief at every distance; point sampling also aliased.
 const Image normals=UnpackNormalMap(top.bump,top.bumpX);const GrayImage gloss=UnpackGlossGray(top.bump),height=UnpackHeightGray(top.bumpX);
 std::vector<Image> mips,corrections;uint32_t w=top.bump.w,h=top.bump.h;
 for(;;){
  BumpPair p=(w==top.bump.w&&h==top.bump.h)?top:packLayersOneMip(normals,gloss,height,w,h,false,nullptr);
  if(!correctCompressedPair(p))throw std::runtime_error("BC3 correction generation failed");
  mips.push_back(std::move(p.bump));corrections.push_back(std::move(p.bumpX));if(w==1&&h==1)break;w=std::max(1u,w/2);h=std::max(1u,h/2);
 }
 const auto a=EncodeBC3DDS(mips.front(),mips),b=EncodeBC3DDS(corrections.front(),corrections);
 if(a.empty()||b.empty())throw std::runtime_error("BC3 encode failed");
 output={a,b};return true;
}
}
bool EncodePackedBumpPair(const BumpPair& pair,EncodedBumpPair& output,std::string& error){
 error.clear();try{EncodedBumpPair next;if(!encodePreparedPair(pair,next))return false;output=std::move(next);return true;}catch(const std::exception& e){error=e.what();return false;}
}
namespace {
bool exportPreparedPair(const BumpPair& top,const std::wstring& name,std::string& error){
 EncodedBumpPair encoded;if(!EncodePackedBumpPair(top,encoded,error))return false;
 return WriteBumpPairDDSBytesAtomic(name,encoded.bump,encoded.bumpX,error);
}
}
bool WriteBumpPairDDSBytesAtomic(const std::wstring& name,const std::vector<uint8_t>& bumpBytes,const std::vector<uint8_t>& bumpXBytes,std::string& error){
 error.clear();if(bumpBytes.size()<128||bumpXBytes.size()<128){error="DDS pair is incomplete";return false;}
 const std::filesystem::path base(name);const std::filesystem::path targets[]={std::filesystem::path(base.wstring()+L"_bump.dds"),std::filesystem::path(base.wstring()+L"_bump#.dds")};
 static std::atomic<unsigned long long> sequence{0};const auto token=std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count())+L"_"+std::to_wstring(sequence.fetch_add(1));
 std::filesystem::path staged[2],backups[2];bool backed[2]={},installed[2]={};
 try{
  if(!base.parent_path().empty())std::filesystem::create_directories(base.parent_path());
  for(int i=0;i<2;++i){
   if(std::filesystem::exists(targets[i])&&!std::filesystem::is_regular_file(targets[i]))throw std::runtime_error("Export destination is not a regular file");
   staged[i]=targets[i];staged[i]+=L".writing_"+token;backups[i]=targets[i];backups[i]+=L".backup_"+token;
   const auto& bytes=i==0?bumpBytes:bumpXBytes;std::ofstream out(staged[i],std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot stage output DDS file");
   out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));out.flush();if(!out)throw std::runtime_error("DDS staging write failed");out.close();if(!out)throw std::runtime_error("DDS staging close failed");
  }
  // Both new files exist completely before either old file is moved.
  for(int i=0;i<2;++i)if(std::filesystem::exists(targets[i])){std::filesystem::rename(targets[i],backups[i]);backed[i]=true;}
  for(int i=0;i<2;++i){std::filesystem::rename(staged[i],targets[i]);installed[i]=true;}
  for(int i=0;i<2;++i)if(backed[i]){std::error_code ec;std::filesystem::remove(backups[i],ec);}
  return true;
 }catch(const std::exception& ex){
  error=ex.what();bool rollbackFailed=false;
  for(int i=1;i>=0;--i){std::error_code ec;if(installed[i]){std::filesystem::remove(targets[i],ec);if(ec)rollbackFailed=true;}if(backed[i]){ec.clear();std::filesystem::rename(backups[i],targets[i],ec);if(ec)rollbackFailed=true;}if(!staged[i].empty()){ec.clear();std::filesystem::remove(staged[i],ec);}}
  if(rollbackFailed)error+="; rollback incomplete: recover the .backup files beside the export";
  return false;
 }
}
bool ExportBumpLayers(const Image& normal,const GrayImage& gloss,const GrayImage& height,uint32_t width,uint32_t heightPixels,const std::wstring& name,std::string& error,const GrayImage* baseHeight){
 error.clear();if(!normal.valid()||!gloss.valid()||!height.valid()){error="Material layers are incomplete";return false;}if(!width||!heightPixels||width>4096||heightPixels>4096){error="Export layer size is invalid";return false;}
 try{return exportPreparedPair(packLayersOneMip(normal,gloss,height,width,heightPixels,false,baseHeight),name,error);}catch(const std::exception& ex){error=ex.what();return false;}
}
bool ExportPackedBumpPair(const BumpPair& pair,const std::wstring& name,std::string& error){
 error.clear();if(!pair.bump.valid()||!pair.bumpX.valid()||pair.bump.w!=pair.bumpX.w||pair.bump.h!=pair.bumpX.h){error="Bump pair is incomplete or dimensions differ";return false;}
 if(pair.bump.w>4096||pair.bump.h>4096){error="Export limited to 4096x4096";return false;}
 try{return exportPreparedPair(pair,name,error);}catch(const std::exception& ex){error=ex.what();return false;}
}
bool ExportBumpPair(const Sources& src,const Settings& cfg,const std::wstring& name,std::string& error){
 error.clear();const Image& ref=src.diffuse;if(!ref.valid()){error="Diffuse texture is required";return false;}if(ref.w>4096||ref.h>4096){error="Export limited to 4096x4096";return false;}
 try{BumpPair top;top.bump=makeBump(src,cfg,ref.w,ref.h);top.bumpX=correction(top.bump,top.bump,src,cfg);return exportPreparedPair(top,name,error);}catch(const std::exception& ex){error=ex.what();return false;}
}
Mesh MakeCube(){Mesh m;MeshPart part;struct Face{V3 n,p[4];};const Face faces[]={{{0,0,1},{{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}}},{{0,0,-1},{{1,-1,-1},{-1,-1,-1},{-1,1,-1},{1,1,-1}}},{{1,0,0},{{1,-1,1},{1,-1,-1},{1,1,-1},{1,1,1}}},{{-1,0,0},{{-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1}}},{{0,1,0},{{-1,1,1},{1,1,1},{1,1,-1},{-1,1,-1}}},{{0,-1,0},{{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1}}}};
 const float uv[4][2]={{0,1},{1,1},{1,0},{0,0}};for(auto& f:faces){uint32_t o=uint32_t(part.vertices.size());for(int k=0;k<4;++k)part.vertices.push_back({f.p[k],f.n,uv[k][0],uv[k][1]});for(int k:{0,1,2,0,2,3})part.indices.push_back(o+k);}m.parts.push_back(std::move(part));m.sourceParts=1;return m;}

}
