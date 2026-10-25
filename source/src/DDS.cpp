#include "Core.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
namespace lab {
namespace {
uint32_t rd(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
uint16_t rd16(const uint8_t* p){return uint16_t(p[0]|(p[1]<<8));}
void wr(std::vector<uint8_t>& o,uint32_t x){for(int i=0;i<4;++i)o.push_back(uint8_t(x>>(8*i)));}
void color565(uint16_t c,Pixel& p){p.r=uint8_t((((c>>11)&31)*255+15)/31);p.g=uint8_t((((c>>5)&63)*255+31)/63);p.b=uint8_t(((c&31)*255+15)/31);p.a=255;}
uint16_t to565(Pixel p){return uint16_t(((uint32_t(p.r)*31+127)/255<<11)|((uint32_t(p.g)*63+127)/255<<5)|((uint32_t(p.b)*31+127)/255));}
void palette(uint16_t c0,uint16_t c1,Pixel p[4],bool four){
 color565(c0,p[0]);color565(c1,p[1]);
 for(auto channel:{&Pixel::r,&Pixel::g,&Pixel::b}){
  const uint8_t v0=p[0].*channel,v1=p[1].*channel;
  p[2].*channel=uint8_t(four?(2*v0+v1)/3:(v0+v1)/2);
  p[3].*channel=uint8_t(four?(v0+2*v1)/3:0);
 }
 p[2].a=255;p[3].a=four?255:0;
}
void bc1(const uint8_t* in,Pixel out[16],bool opaque){uint16_t a=rd16(in),b=rd16(in+2);Pixel pal[4];palette(a,b,pal,opaque||a>b);uint32_t idx=rd(in+4);for(int i=0;i<16;++i)out[i]=pal[(idx>>(i*2))&3];}
void bc3(const uint8_t* p,Pixel out[16]){bc1(p+8,out,true);uint8_t pal[8]={p[0],p[1]};for(int i=2;i<8;++i){if(p[0]>p[1])pal[i]=uint8_t(((8-i)*p[0]+(i-1)*p[1]+3)/7);else if(i<6)pal[i]=uint8_t(((6-i)*p[0]+(i-1)*p[1]+2)/5);else pal[i]=uint8_t(i==6?0:255);}uint64_t bits=0;for(int i=0;i<6;++i)bits|=uint64_t(p[2+i])<<(i*8);for(int i=0;i<16;++i)out[i].a=pal[(bits>>(3*i))&7];}
void bc2(const uint8_t* p,Pixel out[16]){bc1(p+8,out,true);for(int i=0;i<16;++i){int v=(p[i/2]>>(4*(i&1)))&15;out[i].a=uint8_t(v*17);}}
void encodeBlock(const Pixel in[16],std::vector<uint8_t>& result){
  uint8_t alo=255,ahi=0;for(int i=0;i<16;++i){alo=std::min(alo,in[i].a);ahi=std::max(ahi,in[i].a);}if(alo==ahi && ahi<255){if(ahi<255)ahi++;else alo--;}
  result.push_back(ahi);result.push_back(alo);uint8_t ap[8]={ahi,alo};for(int i=2;i<8;++i)ap[i]=uint8_t(((8-i)*ahi+(i-1)*alo+3)/7);
  uint64_t abit=0;for(int i=0;i<16;++i){int best=0,bestD=1000000;for(int k=0;k<8;++k){int d=int(in[i].a)-int(ap[k]);d*=d;if(d<bestD){bestD=d;best=k;}}abit|=uint64_t(best)<<(3*i);}for(int i=0;i<6;++i)result.push_back(uint8_t(abit>>(8*i)));
  Pixel minC={255,255,255,255},maxC={0,0,0,255};for(int i=0;i<16;++i){minC.r=std::min(minC.r,in[i].r);minC.g=std::min(minC.g,in[i].g);minC.b=std::min(minC.b,in[i].b);maxC.r=std::max(maxC.r,in[i].r);maxC.g=std::max(maxC.g,in[i].g);maxC.b=std::max(maxC.b,in[i].b);}
  uint16_t c0=to565(maxC),c1=to565(minC);if(c0<=c1){if(c0==c1){if(c0<65535)c0++;else c1--;}else std::swap(c0,c1);}
  Pixel pal[4];palette(c0,c1,pal,true);uint32_t bits=0;
  for(int i=0;i<16;++i){int best=0,bestD=INT32_MAX;for(int k=0;k<4;++k){int r=int(in[i].r)-int(pal[k].r),g=int(in[i].g)-int(pal[k].g),b=int(in[i].b)-int(pal[k].b);int d=r*r+g*g+b*b;if(d<bestD){bestD=d;best=k;}}bits|=uint32_t(best)<<(2*i);}
  result.push_back(uint8_t(c0));result.push_back(uint8_t(c0>>8));result.push_back(uint8_t(c1));result.push_back(uint8_t(c1>>8));wr(result,bits);
}
std::vector<uint8_t> EncodeBC3Blocks(const Image& im){std::vector<uint8_t> out;if(!im.valid())return out;out.reserve(size_t((im.w+3)/4)*((im.h+3)/4)*16);for(uint32_t y=0;y<im.h;y+=4)for(uint32_t x=0;x<im.w;x+=4){Pixel block[16];for(uint32_t k=0;k<4;++k)for(uint32_t j=0;j<4;++j)block[k*4+j]=im.pixels[size_t(std::min(y+k,im.h-1))*im.w+std::min(x+j,im.w-1)];encodeBlock(block,out);}return out;}
}
Image DecompressBC3Image(const std::vector<uint8_t>& b,uint32_t w,uint32_t h){Image o;if(!w||!h||w>8192||h>8192||uint64_t(w)*h>(16u*1024u*1024u)||b.size()<size_t((w+3)/4)*((h+3)/4)*16)return o;o.w=w;o.h=h;o.pixels.resize(size_t(w)*h);size_t offset=0;for(uint32_t y=0;y<h;y+=4)for(uint32_t x=0;x<w;x+=4){Pixel block[16];bc3(b.data()+offset,block);offset+=16;for(uint32_t k=0;k<4 && y+k<h;++k)for(uint32_t j=0;j<4&&x+j<w;++j)o.pixels[size_t(y+k)*w+x+j]=block[k*4+j];}return o;}
std::vector<uint8_t> EncodeBC3DDS(const Image& top,const std::vector<Image>& mips){
 // A compressed DDS declares the size of the whole top level, not one row.
 if(!top.valid()||top.w>8192||top.h>8192||uint64_t(top.w)*top.h>(16u*1024u*1024u))return {};
 const size_t count=mips.empty()?1:mips.size();
 uint32_t maxLevels=1;for(uint32_t w=top.w,h=top.h;w>1||h>1;++maxLevels){w=std::max(1u,w/2);h=std::max(1u,h/2);}
 if(count>maxLevels)return {};
 uint32_t w=top.w,h=top.h;size_t payload=0;
 auto level=[&](size_t i)->const Image&{return mips.empty()?top:mips[i];};
 for(size_t i=0;i<count;++i){const auto& im=level(i);if(!im.valid()||im.w!=w||im.h!=h)return {};payload+=size_t((w+3)/4)*((h+3)/4)*16;w=std::max(1u,w/2);h=std::max(1u,h/2);}
 std::vector<uint8_t> o;o.reserve(128+payload);
 wr(o,0x20534444);wr(o,124);wr(o,0x00081007u|(count>1?0x20000u:0));wr(o,top.h);wr(o,top.w);wr(o,((top.w+3)/4)*((top.h+3)/4)*16);wr(o,0);wr(o,uint32_t(count));for(int i=0;i<11;++i)wr(o,0);
 wr(o,32);wr(o,0x4);wr(o,0x35545844);wr(o,0);wr(o,0);wr(o,0);wr(o,0);wr(o,0);
 wr(o,0x1000u|(count>1?0x400008u:0u));wr(o,0);wr(o,0);wr(o,0);wr(o,0);
 for(size_t i=0;i<count;++i){auto b=EncodeBC3Blocks(level(i));o.insert(o.end(),b.begin(),b.end());}return o;
}
std::vector<Image> DecodeDDSMipChain(const std::vector<uint8_t>& bytes,std::string& error){
 Image top=DecodeDDS(bytes,error);if(!top.valid())return {};
 const uint32_t code=rd(bytes.data()+84),flags=rd(bytes.data()+80);
 const bool dx10=(flags&4)&&code==0x30315844;
 const uint32_t format=dx10?rd(bytes.data()+128):0;
 const bool bc1=(flags&4)&&(code==0x31545844||(dx10&&(format==71||format==72)));
 const bool compressed=bc1||((flags&4)&&(code==0x33545844||code==0x35545844||(dx10&&(format==74||format==75||format==77||format==78))));
 const size_t header=dx10?148:128;
 uint32_t w=top.w,h=top.h;size_t offset=header;
 std::vector<Image> images;const uint32_t levels=std::max(1u,rd(bytes.data()+28));images.reserve(levels);
 auto put=[](std::vector<uint8_t>& b,size_t at,uint32_t value){for(unsigned k=0;k<4;++k)b[at+k]=uint8_t(value>>(k*8));};
 for(uint32_t level=0;level<levels;++level){
  const size_t pitch=compressed?size_t((w+3)/4)*(bc1?8:16):(level==0&&(rd(bytes.data()+8)&8)?rd(bytes.data()+20):size_t(w)*4);
  const size_t count=pitch*(compressed?(h+3)/4:h);
  if(offset>bytes.size()||count>bytes.size()-offset){error="DDS mip chain truncated";return {};}
  if(level==0)images.push_back(std::move(top));
  else{std::vector<uint8_t> data(bytes.begin(),bytes.begin()+header);put(data,12,h);put(data,16,w);put(data,20,uint32_t(pitch));put(data,28,1);data.insert(data.end(),bytes.begin()+offset,bytes.begin()+offset+count);auto image=DecodeDDS(data,error);if(!image.valid())return {};images.push_back(std::move(image));}
  offset+=count;w=std::max(1u,w/2);h=std::max(1u,h/2);
 }
 return images;
}

Image DecodeDDS(const std::vector<uint8_t>& buf,std::string& err){
 err.clear();
 auto fail=[&](const char* reason)->Image{err=reason;return {};};
 if(buf.size()<128||rd(buf.data())!=0x20534444||rd(buf.data()+4)!=124||rd(buf.data()+76)!=32)return fail("Invalid DDS header");
 const uint32_t h=rd(buf.data()+12),w=rd(buf.data()+16),flags=rd(buf.data()+80),code=rd(buf.data()+84),depth=rd(buf.data()+24),headerFlags=rd(buf.data()+8),caps2=rd(buf.data()+112);
 if(!w||!h||w>8192||h>8192||depth>1||uint64_t(w)*h>(16u*1024u*1024u))return fail("DDS dimensions unsupported");
 if(caps2&(0x200u|0xfc00u|0x200000u))return fail("DDS cube and volume textures are unsupported");
 size_t offset=128;int kind=0;
 if(flags&4){
  if(code==0x31545844)kind=1;else if(code==0x33545844)kind=2;else if(code==0x35545844)kind=3;
  else if(code==0x30315844){
   if(buf.size()<148)return fail("DDS DX10 header truncated");
   const uint32_t dx=rd(buf.data()+128),dimension=rd(buf.data()+132),misc=rd(buf.data()+136),arraySize=rd(buf.data()+140);offset=148;
   if(dimension!=3||arraySize!=1||(misc&4))return fail("DDS DX10 requires a single 2D texture");
   if(dx==71||dx==72)kind=1;else if(dx==74||dx==75)kind=2;else if(dx==77||dx==78)kind=3;else if(dx==28||dx==29)kind=4;else if(dx==87||dx==91)kind=5;
  }
 }else if((flags&0x40u)&&rd(buf.data()+88)==32)kind=6;
 if(!kind)return fail("Unsupported DDS pixel format (supports BC1/BC2/BC3/RGBA8)");
 uint32_t masks[4]={rd(buf.data()+92),rd(buf.data()+96),rd(buf.data()+100),rd(buf.data()+104)};
 uint32_t shifts[4]={},ranges[4]={};
 size_t rowPitch=size_t(w)*4;
 if(kind>=4){
  if(kind==4){masks[0]=0xff;masks[1]=0xff00;masks[2]=0xff0000;masks[3]=0xff000000;}
  else if(kind==5){masks[0]=0xff0000;masks[1]=0xff00;masks[2]=0xff;masks[3]=0xff000000;}
  uint32_t used=0;
  for(int k=0;k<4;++k){
   uint32_t m=masks[k];if(!m){if(k!=3)return fail("DDS RGB channel mask is missing");continue;}
   if(used&m)return fail("DDS channel masks overlap");used|=m;
   while(!(m&1u)){m>>=1;++shifts[k];}
   if((m&(m+1u))!=0)return fail("DDS channel mask is not contiguous");ranges[k]=m;
  }
  if(headerFlags&8u){const uint32_t declared=rd(buf.data()+20);if(declared<rowPitch)return fail("DDS row pitch is smaller than the image width");rowPitch=declared;}
 }
 const uint64_t topBytes=kind<=3?uint64_t((w+3)/4)*((h+3)/4)*(kind==1?8:16):uint64_t(rowPitch)*h;
 if(topBytes>buf.size()-offset)return fail("DDS truncated");
 // The decoder returns the top image; impossible declared mip counts still
 // indicate a corrupt header and must not be accepted as a material.
 uint32_t maxLevels=1;for(uint32_t mw=w,mh=h;mw>1||mh>1;++maxLevels){mw=std::max(1u,mw/2);mh=std::max(1u,mh/2);}
 if(rd(buf.data()+28)>maxLevels)return fail("DDS mip count is invalid");
 Image im;im.w=w;im.h=h;im.pixels.resize(size_t(w)*h);
 if(kind<=3){
  size_t pos=offset;
  for(uint32_t y=0;y<h;y+=4)for(uint32_t x=0;x<w;x+=4){Pixel p[16];if(kind==1)bc1(buf.data()+pos,p,false);else if(kind==2)bc2(buf.data()+pos,p);else bc3(buf.data()+pos,p);pos+=(kind==1?8:16);for(uint32_t a=0;a<4&&y+a<h;++a)for(uint32_t b=0;b<4&&x+b<w;++b)im.pixels[size_t(y+a)*w+x+b]=p[a*4+b];}
 }else{
  for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){const uint32_t v=rd(buf.data()+offset+size_t(y)*rowPitch+size_t(x)*4);uint8_t* channels=&im.pixels[size_t(y)*w+x].r;
   for(int k=0;k<4;++k){const uint32_t range=ranges[k];if(!range){channels[k]=k==3?255:0;continue;}const uint32_t raw=(v>>shifts[k])&range;channels[k]=uint8_t((uint64_t(raw)*255u+range/2u)/range);}
  }
 }
 return im;
}
}
