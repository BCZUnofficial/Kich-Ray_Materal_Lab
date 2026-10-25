#include "Core.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes=std::vector<uint8_t>;
void check(bool good,const char* reason){if(!good)throw std::runtime_error(reason);}
void u16(Bytes& b,uint16_t v){b.push_back(uint8_t(v));b.push_back(uint8_t(v>>8));}
void u32(Bytes& b,uint32_t v){for(int i=0;i<4;++i)b.push_back(uint8_t(v>>(8*i)));}
void set32(Bytes& b,size_t at,uint32_t v){for(int i=0;i<4;++i)b.at(at+i)=uint8_t(v>>(8*i));}
uint32_t get32(const Bytes& b,size_t at){return uint32_t(b.at(at))|(uint32_t(b.at(at+1))<<8)|(uint32_t(b.at(at+2))<<16)|(uint32_t(b.at(at+3))<<24);}
void number(Bytes& b,float v){uint32_t bits=0;std::memcpy(&bits,&v,4);u32(b,bits);}
void stringZ(Bytes& b,const std::string& s){b.insert(b.end(),s.begin(),s.end());b.push_back(0);}
void chunk(Bytes& into,uint32_t id,const Bytes& bytes){u32(into,id);u32(into,uint32_t(bytes.size()));into.insert(into.end(),bytes.begin(),bytes.end());}
lab::Image image(uint32_t w,uint32_t h){lab::Image i;i.w=w;i.h=h;i.pixels.assign(size_t(w)*h,{97,129,204,177});return i;}
Bytes header(uint8_t type){Bytes b(44);b[0]=4;b[1]=type;return b;}
Bytes triangle(bool skeletal=false){
 Bytes out;chunk(out,1,header(skeletal?5:0));Bytes material;stringZ(material,"act/test");stringZ(material,"models/model");chunk(out,2,material);
 Bytes vertices;u32(vertices,skeletal?0x12071980u:0x112u);u32(vertices,3);
 const float positions[3][3]={{0,0,0},{1,0,0},{0,1,0}};
 for(int i=0;i<3;++i){for(float x:positions[i])number(vertices,x);number(vertices,0);number(vertices,0);number(vertices,1);
  if(skeletal)for(int n=0;n<6;++n)number(vertices,0);number(vertices,float(i==1));number(vertices,float(i==2));if(skeletal)u32(vertices,0);
 }
 chunk(out,3,vertices);Bytes indices;u32(indices,3);u16(indices,0);u16(indices,1);u16(indices,2);chunk(out,4,indices);return out;
}
Bytes skeleton(const std::vector<std::pair<std::string,std::string>>& names){
 Bytes out;chunk(out,1,header(3));Bytes bones;u32(bones,uint32_t(names.size()));for(const auto& name:names){stringZ(bones,name.first);stringZ(bones,name.second);bones.resize(bones.size()+60);}chunk(out,13,bones);
 Bytes children;chunk(children,0,triangle(true));chunk(out,9,children);return out;
}
Bytes motions(bool completeMap=true){
 Bytes params;u16(params,3);u16(params,1);stringZ(params,"default");u16(params,completeMap?1:0);
 if(completeMap){stringZ(params,"root");u32(params,0);}
 u16(params,1);stringZ(params,"idle");u32(params,0);u16(params,0);u16(params,0);number(params,1);number(params,1);number(params,.2f);number(params,.1f);
 Bytes frame;stringZ(frame,"idle");u32(frame,2);frame.push_back(2);u16(frame,0);u16(frame,0);u16(frame,0);u16(frame,32767);number(frame,1);number(frame,2);number(frame,3);
 Bytes data,count;u32(count,1);chunk(data,0,count);chunk(data,1,frame);
 Bytes out;chunk(out,15,params);chunk(out,14,data);return out;
}
void ddsHeadersAndPitch(){
 auto top=image(8,8);const auto encoded=lab::EncodeBC3DDS(top,{top});check(encoded.size()==192&&get32(encoded,20)==64,"BC3 header must declare full top-level linear size");
 std::string error;check(lab::DecodeDDS(encoded,error).valid(),"encoded BC3 must decode");
 check(lab::DecodeDDS(lab::EncodeBC3DDS(top,{}),error).valid(),"omitted mip chain must emit the top image");
 check(lab::EncodeBC3DDS(top,{image(4,4)}).empty(),"inconsistent top mip must be rejected");
 check(lab::EncodeBC3DDS(top,{top,image(3,4)}).empty(),"inconsistent mip dimensions must be rejected");
 auto bad=encoded;bad.pop_back();check(!lab::DecodeDDS(bad,error).valid()&&!error.empty(),"truncated BC3 must fail");
 bad=encoded;set32(bad,28,100);check(!lab::DecodeDDS(bad,error).valid(),"impossible mip count must fail");
 bad=encoded;set32(bad,112,0x200);check(!lab::DecodeDDS(bad,error).valid(),"cube DDS must not silently become a 2D image");
 // Padded legacy RGB rows: two pixels plus four padding bytes per row.
 Bytes rgb(encoded.begin(),encoded.begin()+128);set32(rgb,8,0x100fu);set32(rgb,12,2);set32(rgb,16,2);set32(rgb,20,12);set32(rgb,28,1);set32(rgb,80,0x41);set32(rgb,84,0);set32(rgb,88,32);
 set32(rgb,92,0xff);set32(rgb,96,0xff00);set32(rgb,100,0xff0000);set32(rgb,104,0xff000000);
 for(uint32_t pixel:{0xff000001u,0xff000002u,0xdeadbeefu,0xff000003u,0xff000004u,0xdeadbeefu})u32(rgb,pixel);
 auto decoded=lab::DecodeDDS(rgb,error);check(decoded.valid()&&decoded.pixels[0].r==1&&decoded.pixels[2].r==3&&decoded.pixels[3].r==4,"RGBA decoder must honor row pitch");
 bad=rgb;set32(bad,92,0xffffffffu);check(!lab::DecodeDDS(bad,error).valid(),"overlapping full-width mask must be rejected without undefined shifts");
 bad=rgb;set32(bad,92,0x5);check(!lab::DecodeDDS(bad,error).valid(),"noncontiguous channel mask must fail");
 bad=rgb;set32(bad,20,4);check(!lab::DecodeDDS(bad,error).valid(),"undersized row pitch must fail");
 bad=rgb;set32(bad,20,0xffffffffu);check(!lab::DecodeDDS(bad,error).valid(),"oversized row pitch must fail before allocation or arithmetic overflow");
 Bytes dx(rgb.begin(),rgb.begin()+128);set32(dx,80,4);set32(dx,84,0x30315844);u32(dx,28);u32(dx,3);u32(dx,0);u32(dx,1);u32(dx,0);dx.insert(dx.end(),rgb.begin()+128,rgb.end());
 check(lab::DecodeDDS(dx,error).valid(),"DX10 single 2D RGBA texture must decode");
 set32(dx,140,2);check(!lab::DecodeDDS(dx,error).valid(),"DX10 array must be rejected explicitly");
 set32(dx,140,1);set32(dx,132,4);check(!lab::DecodeDDS(dx,error).valid(),"DX10 volume must be rejected explicitly");
 // 4096² was previously rejected by a decimal16,000,000 pixel limit even
 // though Material Lab exports this supported size.
 bad.assign(encoded.begin(),encoded.begin()+128);set32(bad,12,4096);set32(bad,16,4096);set32(bad,20,4096u*4096u);set32(bad,28,1);bad.resize(128+4096u*4096u);
 decoded=lab::DecodeDDS(bad,error);check(decoded.valid()&&decoded.w==4096,"legal4K BC3 texture must decode");
}
void geometryValidation(){
 std::string error;lab::Mesh mesh;const auto valid=triangle();check(lab::LoadOGFMesh(valid,mesh,error)&&mesh.parts.size()==1&&mesh.parts[0].texture=="act/test","valid static OGF triangle must load");
 auto bad=valid;bad.pop_back();check(!lab::LoadOGFMesh(bad,mesh,error)&&mesh.parts.size()==1,"truncated OGF must fail without replacing existing scene");
 bad=valid;bad.push_back(1);check(!lab::LoadOGFMesh(bad,mesh,error),"trailing chunk-table byte must fail");
 bad=valid;chunk(bad,1,header(0));check(!lab::LoadOGFMesh(bad,mesh,error),"duplicate OGF header must fail");
 bad=valid;set32(bad,0,0x80000001u);check(!lab::LoadOGFMesh(bad,mesh,error),"compressed OGF must not be interpreted as raw bytes");
 bad=valid;bad[bad.size()-2]=3;check(!lab::LoadOGFMesh(bad,mesh,error),"out-of-range triangle index must fail");
 Bytes nested;chunk(nested,1,header(1));Bytes children;chunk(children,0,valid);chunk(children,1,valid);chunk(nested,9,children);
 check(lab::LoadOGFMesh(nested,mesh,error)&&mesh.parts.size()==2,"embedded child geometry must load");
 auto deep=valid;for(int i=0;i<18;++i){Bytes parent;chunk(parent,1,header(1));Bytes child;chunk(child,0,deep);chunk(parent,9,child);deep=std::move(parent);}check(!lab::LoadOGFMesh(deep,mesh,error),"excessively deep hierarchy must fail");
 check(lab::LoadOGFMesh(skeleton({{"root",""}}),mesh,error)&&mesh.bones[0].inverseBind[0]==1&&mesh.bones[0].inverseBind[15]==1,"missing optional IK must use an identity inverse bind");
 check(!lab::LoadOGFMesh(skeleton({{"a","b"},{"b","a"}}),mesh,error),"bone cycles must be rejected even without IK records");
 check(!lab::LoadOGFMesh(skeleton({{"root","missing"}}),mesh,error),"missing bone parent must not silently become a root");
 check(!lab::LoadOGFMesh(skeleton({{"root",""},{"ROOT",""}}),mesh,error),"case-insensitive duplicate bone names must fail");
}
void motionValidation(){
 std::string error;lab::Mesh mesh;check(lab::LoadOGFMesh(skeleton({{"root",""}}),mesh,error),"motion fixture skeleton must load");const auto valid=motions();
 check(lab::LoadOGFMotions(valid,mesh,error)&&mesh.motions.size()==1,"valid SoC OMF motion must load");
 check(mesh.motions[0].tracks[0].rotations.size()==1&&mesh.motions[0].tracks[0].translations[0].z==3,"constant rotation/translation must decode");
 check(lab::LoadOGFMotions(valid,mesh,error)&&mesh.motions.size()==1,"duplicate motion import must remain stable");
 auto bad=valid;bad.pop_back();check(!lab::LoadOGFMotions(bad,mesh,error)&&mesh.motions.size()==1,"bad OMF import must retain existing animations");
 check(!lab::LoadOGFMotions(motions(false),mesh,error),"incomplete bone remap must not guess missing source bones");
 bad=valid;bad.push_back(0);check(!lab::LoadOGFMotions(bad,mesh,error),"OMF trailing data must fail");
 // A small deterministic mutation corpus exercises all chunk/length fields.
 uint32_t seed=0x12345678;
 for(int i=0;i<600;++i){seed=seed*1664525u+1013904223u;bad=valid;bad[seed%bad.size()]^=uint8_t(1u<<(seed%8));lab::Mesh candidate=mesh;lab::LoadOGFMotions(bad,candidate,error);}
}
void references(int argc,char** argv){
 for(int i=1;i<argc;++i){std::ifstream in(argv[i],std::ios::binary);check(bool(in),"reference DDS cannot be opened");Bytes data((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());std::string error;auto im=lab::DecodeDDS(data,error);check(im.valid(),"release reference DDS must decode");std::cout<<"REFERENCE: "<<argv[i]<<" "<<im.w<<"x"<<im.h<<" mips="<<get32(data,28)<<"\n";}
}
}
int main(int argc,char** argv){try{ddsHeadersAndPitch();geometryValidation();motionValidation();references(argc,argv);std::cout<<"PASS: DDS metadata/pitch/masks/4K, OGF chunk hierarchy/bones, OMF mapping/transactionality and mutation corpus\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
