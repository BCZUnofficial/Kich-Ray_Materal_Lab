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
void palette(uint16_t c0,uint16_t c1,Pixel p[4],bool four){color565(c0,p[0]);color565(c1,p[1]);for(int c=0;c<3;++c){uint8_t* a=&p[2].r+c;uint8_t* b=&p[3].r+c;const uint8_t v0=(&p[0].r)[c],v1=(&p[1].r)[c];*a=uint8_t(four?(2*v0+v1)/3:(v0+v1)/2);*b=uint8_t(four?(v0+2*v1)/3:0);}if(!four)p[3].a=0;}
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
Image DecompressBC3Image(const std::vector<uint8_t>& b,uint32_t w,uint32_t h){Image o;if(!w||!h||w>8192||h>8192||b.size()<size_t((w+3)/4)*((h+3)/4)*16)return o;o.w=w;o.h=h;o.pixels.resize(size_t(w)*h);size_t offset=0;for(uint32_t y=0;y<h;y+=4)for(uint32_t x=0;x<w;x+=4){Pixel block[16];bc3(b.data()+offset,block);offset+=16;for(uint32_t k=0;k<4 && y+k<h;++k)for(uint32_t j=0;j<4&&x+j<w;++j)o.pixels[size_t(y+k)*w+x+j]=block[k*4+j];}return o;}
std::vector<uint8_t> EncodeBC3DDS(const Image& top,const std::vector<Image>& mips){
 if(!top.valid())return {};
 std::vector<uint8_t> o;o.reserve(128+size_t(top.w)*top.h);
 wr(o,0x20534444);wr(o,124);wr(o,0x000A1007|(mips.size()>1?0x20000:0));wr(o,top.h);wr(o,top.w);wr(o,((top.w+3)/4)*16);wr(o,0);wr(o,uint32_t(mips.size()));for(int i=0;i<11;++i)wr(o,0);
 wr(o,32);wr(o,0x4);wr(o,0x35545844);wr(o,0);wr(o,0);wr(o,0);wr(o,0);wr(o,0);
 wr(o,0x1000u|(mips.size()>1?0x400008u:0u));wr(o,0);wr(o,0);wr(o,0);wr(o,0);
 for(auto& im:mips){auto b=EncodeBC3Blocks(im);o.insert(o.end(),b.begin(),b.end());}return o;
}
Image DecodeDDS(const std::vector<uint8_t>& buf,std::string& err){Image im;err.clear();if(buf.size()<128 || rd(buf.data())!=0x20534444 || rd(buf.data()+4)!=124 || rd(buf.data()+76)!=32){err="Invalid DDS header";return im;}
 uint32_t h=rd(buf.data()+12),w=rd(buf.data()+16),flags=rd(buf.data()+80),code=rd(buf.data()+84),depth=rd(buf.data()+24);if(!w||!h||w>8192||h>8192||depth>1||size_t(w)*h>16000000){err="DDS dimensions unsupported";return im;}size_t offset=128;int kind=0;
 if(flags&4){if(code==0x31545844)kind=1;else if(code==0x33545844)kind=2;else if(code==0x35545844)kind=3;else if(code==0x30315844&&buf.size()>=148){uint32_t dx=rd(buf.data()+128);offset=148;if(dx==71||dx==72)kind=1;else if(dx==74||dx==75)kind=2;else if(dx==77||dx==78)kind=3;else if(dx==28||dx==29)kind=4;else if(dx==87||dx==91)kind=5;}}
 else if(rd(buf.data()+88)==32)kind=6;
 im.w=w;im.h=h;im.pixels.resize(size_t(w)*h);
 if(kind==1||kind==2||kind==3){size_t size=size_t((w+3)/4)*((h+3)/4)*(kind==1?8:16);if(offset+size>buf.size()){err="DDS truncated";return {};}size_t pos=offset;for(uint32_t y=0;y<h;y+=4)for(uint32_t x=0;x<w;x+=4){Pixel p[16];if(kind==1)bc1(buf.data()+pos,p,false);else if(kind==2)bc2(buf.data()+pos,p);else bc3(buf.data()+pos,p);pos+=(kind==1?8:16);for(uint32_t a=0;a<4 && y+a<h;++a)for(uint32_t b=0;b<4&&x+b<w;++b)im.pixels[size_t(y+a)*w+x+b]=p[a*4+b];}return im;}
 if(kind==4||kind==5||kind==6){if(offset+size_t(w)*h*4>buf.size()){err="DDS truncated";return {};}const uint8_t* p=buf.data()+offset;uint32_t masks[4]={rd(buf.data()+92),rd(buf.data()+96),rd(buf.data()+100),rd(buf.data()+104)};if(kind==4)masks[0]=0xff,masks[1]=0xff00,masks[2]=0xff0000,masks[3]=0xff000000;else if(kind==5)masks[0]=0xff0000,masks[1]=0xff00,masks[2]=0xff,masks[3]=0xff000000;
 for(size_t i=0;i<im.pixels.size();++i){uint32_t v=rd(p+i*4);uint8_t* c=&im.pixels[i].r;for(int k=0;k<4;++k){uint32_t m=masks[k];if(!m){c[k]=k==3?255:0;continue;}int sh=0;while(!(m&1)){m>>=1;sh++;}uint32_t bits=0,tmp=m;while(tmp){bits++;tmp>>=1;}uint32_t raw=(v>>sh)&m;uint32_t mx=(uint32_t(1)<<bits)-1;c[k]=uint8_t((raw*255u+mx/2)/mx);}}return im;}
 err="Unsupported DDS pixel format (supports BC1/BC2/BC3/RGBA8)";return {};
}
}
