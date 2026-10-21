#include "Core.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <cctype>

namespace lab {
namespace {
constexpr uint32_t OGF_HEADER=1,OGF_TEXTURE=2,OGF_VERTICES=3,OGF_INDICES=4,OGF_CHILDREN=9,OGF_CHILDREN_L=10;
constexpr uint32_t OGF_S_BONE_NAMES=13,OGF_S_IKDATA=16;
constexpr unsigned MT_NORMAL=0,MT_HIERRARHY=1,MT_PROGRESSIVE=2,MT_SKELETON_ANIM=3,MT_SKELETON_GEOMDEF_PM=4,MT_SKELETON_GEOMDEF_ST=5,MT_SKELETON_RIGID=10;
constexpr uint32_t OGF_VERTEXFORMAT_FVF_1L=1u*0x12071980u,OGF_VERTEXFORMAT_FVF_2L=2u*0x12071980u;
constexpr size_t FOBB_BYTES=60,SBONE_SHAPE_BYTES=112,SJOINT_IK_V0_BYTES=72,SJOINT_IK_V1_BYTES=76;
uint32_t u32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
uint16_t u16(const uint8_t* p){return uint16_t(p[0]|(uint16_t(p[1])<<8));}
float f32(const uint8_t* p){float f;std::memcpy(&f,p,4);return f;}
struct Block{const uint8_t* ptr=nullptr;size_t size=0;};
bool chunk(Block src,uint32_t id,Block& found){size_t off=0;while(off+8<=src.size){const uint32_t kind=u32(src.ptr+off),length=u32(src.ptr+off+4);off+=8;if(length>src.size-off)return false;if((kind&0x7fffffffu)==id){found={src.ptr+off,length};return true;}off+=length;}return false;}
bool sane(float v){return std::isfinite(v)&&std::fabs(v)<1000000.f;}
bool saneVertex(const Vertex& v){return sane(v.p.x)&&sane(v.p.y)&&sane(v.p.z)&&sane(v.n.x)&&sane(v.n.y)&&sane(v.n.z)&&sane(v.u)&&sane(v.v);}
bool zstr(Block b,size_t& at,std::string& s){if(at>=b.size)return false;size_t e=at;while(e<b.size&&b.ptr[e])++e;if(e>=b.size)return false;s.assign(reinterpret_cast<const char*>(b.ptr+at),e-at);at=e+1;return true;}
void readMaterial(Block src,MeshPart& part){Block t{};if(!chunk(src,OGF_TEXTURE,t)||!t.size)return;size_t at=0;zstr(t,at,part.texture);zstr(t,at,part.shader);}
bool appendIndices(Block ib,uint32_t nv,MeshPart& part,std::string& error){if(ib.size<4){error="OGF index chunk truncated";return false;}const uint32_t ni=u32(ib.ptr);if(!ni||ni>3000000||(ni%3)!=0||size_t(ni)*2>ib.size-4){error="OGF index bounds invalid";return false;}part.indices.reserve(ni);for(uint32_t i=0;i<ni;++i){uint16_t ix=u16(ib.ptr+4+size_t(i)*2);if(ix>=nv){error="OGF index out of bounds";return false;}part.indices.push_back(ix);}return true;}
bool appendStatic(Block vb,Block ib,MeshPart& part,std::string& error){if(vb.size<8){error="OGF vertex chunk truncated";return false;}const uint32_t fvf=u32(vb.ptr),nv=u32(vb.ptr+4);if(fvf!=0x112u){error="Unsupported OGF static FVF (expected 0x112)";return false;}constexpr size_t stride=32;if(!nv||nv>1000000||size_t(nv)*stride>vb.size-8){error="OGF static vertex bounds invalid";return false;}part.vertices.reserve(nv);for(uint32_t i=0;i<nv;++i){const uint8_t* p=vb.ptr+8+size_t(i)*stride;Vertex v;v.p={f32(p),f32(p+4),f32(p+8)};v.n={f32(p+12),f32(p+16),f32(p+20)};v.u=f32(p+24);v.v=f32(p+28);if(!saneVertex(v)){error="OGF static vertex contains invalid floats";return false;}part.vertices.push_back(v);}return appendIndices(ib,nv,part,error);}
bool appendSkeletal(Block vb,Block ib,MeshPart& part,std::string& error){if(vb.size<8){error="OGF skeletal vertex chunk truncated";return false;}const uint32_t fmt=u32(vb.ptr),nv=u32(vb.ptr+4);if(!nv||nv>1000000){error="OGF skeletal vertex count invalid";return false;}size_t stride=0,pos=0,norm=0,uv=0;if(fmt==OGF_VERTEXFORMAT_FVF_1L){stride=60;pos=0;norm=12;uv=48;}else if(fmt==OGF_VERTEXFORMAT_FVF_2L){stride=64;pos=4;norm=16;uv=56;}else{error="Unsupported SoC skeletal vertex format (1-link/2-link only)";return false;}if(size_t(nv)*stride>vb.size-8){error="OGF skeletal vertex data truncated";return false;}part.vertices.reserve(nv);for(uint32_t i=0;i<nv;++i){const uint8_t* p=vb.ptr+8+size_t(i)*stride;Vertex v;v.p={f32(p+pos),f32(p+pos+4),f32(p+pos+8)};v.n={f32(p+norm),f32(p+norm+4),f32(p+norm+8)};v.u=f32(p+uv);v.v=f32(p+uv+4);if(!saneVertex(v)){error="OGF skeletal vertex contains invalid floats";return false;}part.vertices.push_back(v);}part.skeletal=true;return appendIndices(ib,nv,part,error);}

struct Mat{float m[4][4];};
Mat identity(){Mat r={};for(int i=0;i<4;++i)r.m[i][i]=1;return r;}
Mat setHPB(float h,float p,float b){
 const float sh=std::sin(h),ch=std::cos(h),sp=std::sin(p),cp=std::cos(p),sb=std::sin(b),cb=std::cos(b);
 const float cc=ch*cb,cs=ch*sb,sc=sh*cb,ss=sh*sb;Mat r=identity();
 r.m[0][0]=cc-sp*ss; r.m[0][1]=-cp*sb; r.m[0][2]=sp*cs+sc;
 r.m[1][0]=sp*sc+cs; r.m[1][1]=cp*cb; r.m[1][2]=ss-sp*cc;
 r.m[2][0]=-cp*sh; r.m[2][1]=sp; r.m[2][2]=cp*ch;return r;}
Mat localBind(V3 rot,V3 tr){
 // Exact equivalent of X-Ray Fmatrix::setXYZi(vXYZ), followed by translate_over(vT).
 Mat r=setHPB(-rot.y,-rot.x,-rot.z);r.m[3][0]=tr.x;r.m[3][1]=tr.y;r.m[3][2]=tr.z;return r;}
Mat mul43(const Mat& A,const Mat& B){Mat r=identity();for(int row=0;row<3;++row)for(int col=0;col<3;++col)r.m[row][col]=A.m[0][col]*B.m[row][0]+A.m[1][col]*B.m[row][1]+A.m[2][col]*B.m[row][2];for(int col=0;col<3;++col)r.m[3][col]=A.m[0][col]*B.m[3][0]+A.m[1][col]*B.m[3][1]+A.m[2][col]*B.m[3][2]+A.m[3][col];return r;}

bool parseBones(Block src,Mesh& mesh,std::string& error){Block names{};if(!chunk(src,OGF_S_BONE_NAMES,names))return true;if(names.size<4){error="OGF bone names chunk truncated";return false;}size_t at=0;const uint32_t count=u32(names.ptr);at=4;if(!count||count>256){error="OGF bone count invalid";return false;}mesh.bones.clear();mesh.bones.resize(count);for(uint32_t i=0;i<count;++i){if(!zstr(names,at,mesh.bones[i].name)||!zstr(names,at,mesh.bones[i].parentName)){error="OGF bone name/parent truncated";return false;}if(at+FOBB_BYTES>names.size){error="OGF bone OBB truncated";return false;}at+=FOBB_BYTES;}
 std::unordered_map<std::string,int32_t> ids;for(uint32_t i=0;i<count;++i){std::string n=mesh.bones[i].name;std::transform(n.begin(),n.end(),n.begin(),[](unsigned char c){return char(std::tolower(c));});ids[n]=int32_t(i);}for(uint32_t i=0;i<count;++i){std::string p=mesh.bones[i].parentName;std::transform(p.begin(),p.end(),p.begin(),[](unsigned char c){return char(std::tolower(c));});auto it=ids.find(p);mesh.bones[i].parent=(p.empty()||it==ids.end())?-1:it->second;}
 Block ik{};if(!chunk(src,OGF_S_IKDATA,ik))return true;at=0;for(uint32_t i=0;i<count;++i){if(at+4>ik.size){error="OGF IK data truncated before bone record";return false;}uint32_t vers=u32(ik.ptr+at);at+=4;std::string material;if(!zstr(ik,at,material)){error="OGF IK material string truncated";return false;}const size_t ikBytes=vers>0?SJOINT_IK_V1_BYTES:SJOINT_IK_V0_BYTES;const size_t need=SBONE_SHAPE_BYTES+ikBytes+12+12+4+12;if(at+need>ik.size){error="OGF IK bone record truncated";return false;}at+=SBONE_SHAPE_BYTES+ikBytes;Bone& b=mesh.bones[i];b.localRotate={f32(ik.ptr+at),f32(ik.ptr+at+4),f32(ik.ptr+at+8)};at+=12;b.localTranslate={f32(ik.ptr+at),f32(ik.ptr+at+4),f32(ik.ptr+at+8)};at+=12;at+=4+12;if(!sane(b.localRotate.x)||!sane(b.localRotate.y)||!sane(b.localRotate.z)||!sane(b.localTranslate.x)||!sane(b.localTranslate.y)||!sane(b.localTranslate.z)){error="OGF bind transform contains invalid floats";return false;}b.hasBind=true;}
 std::vector<Mat> global(count,identity());std::vector<uint8_t> done(count,0),visiting(count,0);auto solve=[&](auto&& self,int i)->bool{if(done[i])return true;if(visiting[i])return false;visiting[i]=1;Mat local=mesh.bones[i].hasBind?localBind(mesh.bones[i].localRotate,mesh.bones[i].localTranslate):identity();int p=mesh.bones[i].parent;if(p>=0){if(p>=int(count)||!self(self,p))return false;global[i]=mul43(global[p],local);}else global[i]=local;mesh.bones[i].position={global[i].m[3][0],global[i].m[3][1],global[i].m[3][2]};visiting[i]=0;done[i]=1;return true;};for(uint32_t i=0;i<count;++i)if(!solve(solve,int(i))){error="OGF bone hierarchy contains a cycle";return false;}return true;}

bool parse(Block src,Mesh& mesh,std::string& error,int level){const size_t partsBefore=mesh.parts.size();if(level>16){error="OGF nesting too deep";return false;}Block hdr{};if(!chunk(src,OGF_HEADER,hdr)||hdr.size<2||hdr.ptr[0]!=4){error="Expected OGF version 4 header";return false;}const unsigned typ=hdr.ptr[1];const bool known=typ==MT_NORMAL||typ==MT_HIERRARHY||typ==MT_PROGRESSIVE||typ==MT_SKELETON_ANIM||typ==MT_SKELETON_GEOMDEF_PM||typ==MT_SKELETON_GEOMDEF_ST||typ==MT_SKELETON_RIGID;if(!known){error="Unsupported OGF v4 visual type: "+std::to_string(typ);return false;}if(typ==MT_SKELETON_ANIM||typ==MT_SKELETON_GEOMDEF_PM||typ==MT_SKELETON_GEOMDEF_ST||typ==MT_SKELETON_RIGID)mesh.skeletalSource=true;if(level==0&&mesh.skeletalSource){if(!parseBones(src,mesh,error))return false;}
 Block vb{},ib{};const bool hasV=chunk(src,OGF_VERTICES,vb),hasI=chunk(src,OGF_INDICES,ib);if(hasV!=hasI){error="OGF has vertices without indices (or indices without vertices)";return false;}if(hasV&&hasI){MeshPart part;readMaterial(src,part);const bool skeletal=typ==MT_SKELETON_GEOMDEF_PM||typ==MT_SKELETON_GEOMDEF_ST||typ==MT_SKELETON_RIGID;if(skeletal){if(!appendSkeletal(vb,ib,part,error))return false;}else{if(!appendStatic(vb,ib,part,error))return false;}mesh.parts.push_back(std::move(part));++mesh.sourceParts;}
 Block children{};if(chunk(src,OGF_CHILDREN,children)){size_t off=0;while(off+8<=children.size){const uint32_t length=u32(children.ptr+off+4);off+=8;if(length>children.size-off){error="OGF child size out of range";return false;}if(!parse({children.ptr+off,length},mesh,error,level+1))return false;off+=length;}if(off!=children.size){error="OGF children chunk has trailing bytes";return false;}}
 if(mesh.parts.size()>partsBefore)return true;
 Block links{};
 if(chunk(src,OGF_CHILDREN_L,links)){error="OGF uses external child references (OGF_CHILDREN_L); embedded mesh data is absent";return false;}
 if(level==0){error="OGF has no embedded previewable vertices/indices";return false;}
 return true;}
}
bool LoadOGFMesh(const std::vector<uint8_t>& data,Mesh& mesh,std::string& error){Mesh m;error.clear();if(data.size()>128*1024*1024||data.size()<16){error="OGF size invalid";return false;}if(!parse({data.data(),data.size()},m,error,0))return false;if(!m.valid()){error="OGF contains no triangles";return false;}mesh=std::move(m);return true;}
}
