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
float brushMask(float distance,float alpha,float hardness){hardness=sat(hardness);const float feather=std::max(0.02f,1.f-hardness*0.98f);const float edge=sat((1.f-distance)/feather);return sat(alpha)*edge;}
uint8_t b8(float x){return uint8_t(sat(x)*255.f+0.5f);}
float lum(Pixel p){return (0.2126f*p.r+0.7152f*p.g+0.0722f*p.b)/255.f;}
Pixel sample(const Image& im,float u,float v){if(!im.valid())return {};u=sat(u)*im.w-.5f;v=sat(v)*im.h-.5f;int x=int(floor(u)),y=int(floor(v));float a=u-x,b=v-y;auto at=[&](int i,int j)->Pixel{return im.pixels[size_t(std::min(int(im.h)-1,std::max(0,j)))*im.w+std::min(int(im.w)-1,std::max(0,i))];};Pixel a0=at(x,y),b0=at(x+1,y),a1=at(x,y+1),b1=at(x+1,y+1),o;uint8_t* d=&o.r;for(int k=0;k<4;++k){const float v0=(&a0.r)[k]*(1-a)+(&b0.r)[k]*a,v1=(&a1.r)[k]*(1-a)+(&b1.r)[k]*a;d[k]=b8((v0*(1-b)+v1*b)/255.f);}return o;}
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
   base=norm({base.x*std::max(0.25f,normalGain),base.y*std::max(0.25f,normalGain)*(cfg.invertY?-1.f:1.f),base.z});
   const float blend=std::clamp(cfg.height*1.35f,0.f,1.35f);
   n=norm({base.x+reliefN.x*blend,base.y+reliefN.y*blend,std::max(0.15f,base.z*reliefN.z)});
  }else n=norm({reliefN.x*std::max(0.25f,normalGain),reliefN.y*std::max(0.25f,normalGain),reliefN.z});
  normalField[size_t(y)*w+x]=n;
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
  const float glossBoost=std::clamp(0.55f+cfg.gloss*1.45f,0.f,2.25f);
  const float g=s.gloss.valid()?sat(sourceGloss):sat(std::pow(std::max(0.f,sourceGloss),0.82f)*glossBoost);
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
Image resizeImageExact(const Image& src,uint32_t w,uint32_t h){
 if(!src.valid()||!w||!h)return {};
 if(src.w==w&&src.h==h)return src;
 Image out;out.w=w;out.h=h;out.pixels.resize(size_t(w)*h);
 for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)out.pixels[size_t(y)*w+x]=sample(src,(x+.5f)/float(w),(y+.5f)/float(h));
 return out;
}
GrayImage resizeGrayExact(const GrayImage& src,uint32_t w,uint32_t h){
 if(!src.valid()||!w||!h)return {};
 if(src.w==w&&src.h==h)return src;
 GrayImage out;out.w=w;out.h=h;out.pixels.resize(size_t(w)*h);
 auto at=[&](int x,int y){x=std::clamp(x,0,int(src.w)-1);y=std::clamp(y,0,int(src.h)-1);return src.pixels[size_t(y)*src.w+uint32_t(x)];};
 for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
  const float sx=(x+.5f)*src.w/float(w)-.5f,sy=(y+.5f)*src.h/float(h)-.5f;const int x0=int(std::floor(sx)),y0=int(std::floor(sy));const float fx=sx-x0,fy=sy-y0;
  const float a=at(x0,y0)*(1.f-fx)+at(x0+1,y0)*fx,b=at(x0,y0+1)*(1.f-fx)+at(x0+1,y0+1)*fx;
  out.pixels[size_t(y)*w+x]=uint8_t(std::clamp(a*(1.f-fy)+b*fy,0.f,255.f)+.5f);
 }
 return out;
}
BumpPair packLayersOneMip(const Image& normal,const GrayImage& gloss,const GrayImage& height,uint32_t w,uint32_t h,bool bc3Correction,const GrayImage* baseHeight){
 if(!normal.valid()||!gloss.valid()||!height.valid()||!w||!h)return {};
 Image n=resizeImageExact(normal,w,h);GrayImage g=resizeGrayExact(gloss,w,h),hh=resizeGrayExact(height,w,h),bh;if(baseHeight&&baseHeight->valid())bh=resizeGrayExact(*baseHeight,w,h);if(!n.valid()||!g.valid()||!hh.valid())return {};
 BumpPair pair;pair.bump.w=w;pair.bump.h=h;pair.bump.pixels.resize(size_t(w)*h);pair.bumpX.w=w;pair.bumpX.h=h;pair.bumpX.pixels.resize(size_t(w)*h);
 auto decode=[](Pixel p){return norm({p.r/127.5f-1.f,p.g/127.5f-1.f,p.b/127.5f-1.f});};
 auto wrap=[](int v,int m){v%=m;return v<0?v+m:v;};
 for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){const size_t i=size_t(y)*w+x;V3 q=decode(n.pixels[i]);if(bh.valid()){auto D=[&](int ox,int oy){const int xx=wrap(int(x)+ox,int(w)),yy=wrap(int(y)+oy,int(h));const size_t at=size_t(yy)*w+uint32_t(xx);return (int(hh.pixels[at])-int(bh.pixels[at]))/255.f;};const float tl=D(-1,-1),t=D(0,-1),tr=D(1,-1),l=D(-1,0),r=D(1,0),bl=D(-1,1),b=D(0,1),br=D(1,1);const float gx=tl+2.f*l+bl-tr-2.f*r-br,gy=tl+2.f*t+tr-bl-2.f*b-br;const V3 dn=norm({gx*8.f,gy*8.f,1.f});q=norm({q.x+dn.x,q.y+dn.y,std::max(.15f,q.z*dn.z)});}const float gf=g.pixels[i]/255.f;pair.bump.pixels[i]={b8(std::sqrt(sat(gf))),b8(q.z*.5f+.5f),b8(q.y*.5f+.5f),b8(q.x*.5f+.5f)};pair.bumpX.pixels[i]={128,128,128,hh.pixels[i]};}
 if(bc3Correction){auto encoded=EncodeBC3DDS(pair.bump,{pair.bump});std::string err;Image decoded=DecodeDDS(encoded,err);if(!decoded.valid())return {};auto err8=[](int d)->uint8_t{return uint8_t(std::clamp(d*2+128,0,255));};for(size_t i=0;i<pair.bump.pixels.size();++i){const Pixel p=pair.bump.pixels[i],c=decoded.pixels[i];pair.bumpX.pixels[i].r=err8(int(p.a)-int(c.a));pair.bumpX.pixels[i].g=err8(int(p.b)-int(c.b));pair.bumpX.pixels[i].b=err8(int(p.g)-int(c.g));}}
 return pair;
}

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
void StampNormalStrengthLayer(Image& target,const Image& base,GrayImage& coverage,const Image& preset,float u,float v,float radiusPixels,float strength,bool invert,float hardness){
 if(!target.valid()||!base.valid()||target.w!=base.w||target.h!=base.h||!preset.valid()||radiusPixels<.5f)return;
 if(!coverage.valid()||coverage.w!=target.w||coverage.h!=target.h){coverage.w=target.w;coverage.h=target.h;coverage.pixels.assign(size_t(target.w)*target.h,0);}
 strength=sat(strength);u-=std::floor(u);v-=std::floor(v);const int cx=int(std::floor(u*target.w)),cy=int(std::floor(v*target.h)),r=int(std::ceil(radiusPixels));
 auto decode=[](Pixel p){return norm({p.r/127.5f-1.f,p.g/127.5f-1.f,p.b/127.5f-1.f});};
 for(int oy=-r;oy<=r;++oy)for(int ox=-r;ox<=r;++ox){const float nx=ox/radiusPixels,ny=oy/radiusPixels,d2=nx*nx+ny*ny;if(d2>1.f)continue;const float pu=nx*.5f+.5f,pv=ny*.5f+.5f;const Pixel sp=sample(preset,pu,pv);const float dist=std::sqrt(d2);const uint8_t cov=b8(brushMask(dist,sp.a/255.f,hardness));if(cov<8)continue;int tx=(cx+ox)%int(target.w),ty=(cy+oy)%int(target.h);if(tx<0)tx+=int(target.w);if(ty<0)ty+=int(target.h);const size_t at=size_t(ty)*target.w+uint32_t(tx);if(cov<=coverage.pixels[at])continue;coverage.pixels[at]=cov;V3 bn=decode(base.pixels[at]),sn=decode(sp);if(invert){sn.x=-sn.x;sn.y=-sn.y;}const float a=strength*(cov/255.f);V3 n=norm({bn.x+(sn.x-bn.x)*a,bn.y+(sn.y-bn.y)*a,bn.z+(sn.z-bn.z)*a});target.pixels[at]={b8(n.x*.5f+.5f),b8(n.y*.5f+.5f),b8(n.z*.5f+.5f),255};}
}
void StampScalarDeltaLayer(GrayImage& target,const GrayImage& base,GrayImage& coverage,const Image& preset,float u,float v,float radiusPixels,float delta,float hardness){
 if(!target.valid()||!base.valid()||target.w!=base.w||target.h!=base.h||!preset.valid()||radiusPixels<.5f)return;
 if(!coverage.valid()||coverage.w!=target.w||coverage.h!=target.h){coverage.w=target.w;coverage.h=target.h;coverage.pixels.assign(size_t(target.w)*target.h,0);}
 delta=std::clamp(delta,-1.f,1.f);u-=std::floor(u);v-=std::floor(v);const int cx=int(std::floor(u*target.w)),cy=int(std::floor(v*target.h)),r=int(std::ceil(radiusPixels));
 for(int oy=-r;oy<=r;++oy)for(int ox=-r;ox<=r;++ox){const float nx=ox/radiusPixels,ny=oy/radiusPixels,d2=nx*nx+ny*ny;if(d2>1.f)continue;const float pu=nx*.5f+.5f,pv=ny*.5f+.5f;const Pixel sp=sample(preset,pu,pv);const float dist=std::sqrt(d2);const uint8_t cov=b8(brushMask(dist,sp.a/255.f,hardness));if(cov<8)continue;int tx=(cx+ox)%int(target.w),ty=(cy+oy)%int(target.h);if(tx<0)tx+=int(target.w);if(ty<0)ty+=int(target.h);const size_t at=size_t(ty)*target.w+uint32_t(tx);if(cov<=coverage.pixels[at])continue;coverage.pixels[at]=cov;const float b=base.pixels[at]/255.f;target.pixels[at]=b8(b+delta*(cov/255.f));}
}
void StampNormalFilterLayer(Image& target,const Image& base,GrayImage& coverage,const Image& preset,float u,float v,float radiusPixels,float strength,bool sharpen,float hardness){
 if(!target.valid()||!base.valid()||target.w!=base.w||target.h!=base.h||!preset.valid()||radiusPixels<.5f)return;
 if(!coverage.valid()||coverage.w!=target.w||coverage.h!=target.h){coverage.w=target.w;coverage.h=target.h;coverage.pixels.assign(size_t(target.w)*target.h,0);}
 strength=sat(strength);u-=std::floor(u);v-=std::floor(v);const int cx=int(std::floor(u*target.w)),cy=int(std::floor(v*target.h)),r=int(std::ceil(radiusPixels));
 auto decode=[](Pixel p){return norm({p.r/127.5f-1.f,p.g/127.5f-1.f,p.b/127.5f-1.f});};auto wrap=[](int a,int n){a%=n;return a<0?a+n:a;};
 for(int oy=-r;oy<=r;++oy)for(int ox=-r;ox<=r;++ox){const float nx=ox/radiusPixels,ny=oy/radiusPixels,d2=nx*nx+ny*ny;if(d2>1.f)continue;const float pu=nx*.5f+.5f,pv=ny*.5f+.5f;const Pixel sp=sample(preset,pu,pv);const float dist=std::sqrt(d2);const uint8_t cov=b8(brushMask(dist,sp.a/255.f,hardness));if(cov<8)continue;int tx=wrap(cx+ox,int(target.w)),ty=wrap(cy+oy,int(target.h));const size_t at=size_t(ty)*target.w+uint32_t(tx);if(cov<=coverage.pixels[at])continue;coverage.pixels[at]=cov;V3 c=decode(base.pixels[at]),avg{};for(int fy=-1;fy<=1;++fy)for(int fx=-1;fx<=1;++fx){const V3 q=decode(base.pixels[size_t(wrap(ty+fy,int(base.h)))*base.w+uint32_t(wrap(tx+fx,int(base.w)))]);const float k=(fx==0&&fy==0)?4.f:((fx==0||fy==0)?2.f:1.f);avg.x+=q.x*k;avg.y+=q.y*k;avg.z+=q.z*k;}avg=norm({avg.x/16.f,avg.y/16.f,avg.z/16.f});const float a=strength*(cov/255.f);V3 filtered=sharpen?norm({c.x+(c.x-avg.x)*2.f,c.y+(c.y-avg.y)*2.f,c.z+(c.z-avg.z)*2.f}):avg;V3 n=norm({c.x+(filtered.x-c.x)*a,c.y+(filtered.y-c.y)*a,c.z+(filtered.z-c.z)*a});target.pixels[at]={b8(n.x*.5f+.5f),b8(n.y*.5f+.5f),b8(n.z*.5f+.5f),255};}
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
BumpPair GenerateBumpPreview(const Sources& src,const Settings& cfg){
 const Image& ref=src.diffuse;if(!ref.valid())return {};
 const float scale=std::min(1.f,1024.f/float(std::max(ref.w,ref.h)));const uint32_t w=std::max(1u,uint32_t(ref.w*scale)),h=std::max(1u,uint32_t(ref.h*scale));
 BumpPair p;p.bump=makeBump(src,cfg,w,h);if(!p.bump.valid())return {};p.bumpX=correction(p.bump,p.bump,src,cfg);return p;
}
BumpPair PackBumpLayersPreview(const Image& normal,const GrayImage& gloss,const GrayImage& height,uint32_t maxDimension,const GrayImage* baseHeight){
 if(!normal.valid()||!gloss.valid()||!height.valid())return {};
 uint32_t w=normal.w,h=normal.h;
 if(maxDimension&&std::max(w,h)>maxDimension){const float k=float(maxDimension)/float(std::max(w,h));w=std::max(1u,uint32_t(std::lround(w*k)));h=std::max(1u,uint32_t(std::lround(h*k)));}
 return packLayersOneMip(normal,gloss,height,w,h,false,baseHeight);
}
bool ExportBumpLayers(const Image& normal,const GrayImage& gloss,const GrayImage& height,uint32_t width,uint32_t heightPixels,const std::wstring& name,std::string& error,const GrayImage* baseHeight){
 error.clear();if(!normal.valid()||!gloss.valid()||!height.valid()){error="Material layers are incomplete";return false;}if(!width||!heightPixels||width>4096||heightPixels>4096){error="Export layer size is invalid";return false;}
 try{std::vector<Image> mips,corrections;uint32_t w=width,h=heightPixels;for(;;){BumpPair p=packLayersOneMip(normal,gloss,height,w,h,true,baseHeight);if(!p.bump.valid()||!p.bumpX.valid()){error="Failed to pack material layers";return false;}mips.push_back(std::move(p.bump));corrections.push_back(std::move(p.bumpX));if(w==1&&h==1)break;w=std::max(1u,w/2);h=std::max(1u,h/2);}auto a=EncodeBC3DDS(mips[0],mips),b=EncodeBC3DDS(corrections[0],corrections);if(a.empty()||b.empty())throw std::runtime_error("BC3 encode failed");std::filesystem::path base(name);std::filesystem::path path1(base.wstring()+L"_bump.dds"),path2(base.wstring()+L"_bump#.dds");auto write=[&](const std::filesystem::path& path,const std::vector<uint8_t>& bytes){std::ofstream out(path,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot open output DDS file");out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));if(!out)throw std::runtime_error("DDS file write failed");};write(path1,a);write(path2,b);return true;}catch(const std::exception& ex){error=ex.what();return false;}
}
bool ExportBumpPair(const Sources& src,const Settings& cfg,const std::wstring& name,std::string& error){error.clear();const Image& ref=src.diffuse;if(!ref.valid()){error="Diffuse texture is required";return false;}if(ref.w>4096||ref.h>4096){error="Export limited to 4096x4096 in prototype";return false;}
 std::vector<Image> mips,corrections;uint32_t w=ref.w,h=ref.h;try{for(;;){BumpPair p=oneMip(src,cfg,w,h);if(!p.bump.valid()){error="Failed to generate bump";return false;}mips.push_back(std::move(p.bump));corrections.push_back(std::move(p.bumpX));if(w==1&&h==1)break;w=std::max(1u,w/2);h=std::max(1u,h/2);}auto a=EncodeBC3DDS(mips[0],mips),b=EncodeBC3DDS(corrections[0],corrections);if(a.empty()||b.empty())throw std::runtime_error("BC3 encode failed");std::filesystem::path base(name);std::filesystem::path path1(base.wstring()+L"_bump.dds"),path2(base.wstring()+L"_bump#.dds");auto write=[&](const std::filesystem::path& path,const std::vector<uint8_t>& bytes){std::ofstream out(path,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot open output DDS file");out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));if(!out)throw std::runtime_error("DDS file write failed");};write(path1,a);write(path2,b);return true;
 }catch(const std::exception& ex){error=ex.what();return false;}}
Mesh MakeCube(){Mesh m;MeshPart part;struct Face{V3 n,p[4];};const Face faces[]={{{0,0,1},{{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}}},{{0,0,-1},{{1,-1,-1},{-1,-1,-1},{-1,1,-1},{1,1,-1}}},{{1,0,0},{{1,-1,1},{1,-1,-1},{1,1,-1},{1,1,1}}},{{-1,0,0},{{-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1}}},{{0,1,0},{{-1,1,1},{1,1,1},{1,1,-1},{-1,1,-1}}},{{0,-1,0},{{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1}}}};
 const float uv[4][2]={{0,1},{1,1},{1,0},{0,0}};for(auto& f:faces){uint32_t o=uint32_t(part.vertices.size());for(int k=0;k<4;++k)part.vertices.push_back({f.p[k],f.n,uv[k][0],uv[k][1]});for(int k:{0,1,2,0,2,3})part.indices.push_back(o+k);}m.parts.push_back(std::move(part));m.sourceParts=1;return m;}

}
