#include "Core.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <cctype>
#include <sstream>

namespace lab {
namespace {
constexpr uint32_t OGF_HEADER=1,OGF_TEXTURE=2,OGF_VERTICES=3,OGF_INDICES=4,OGF_CHILDREN=9,OGF_CHILDREN_L=10;
constexpr uint32_t OGF_S_BONE_NAMES=13,OGF_S_MOTIONS=14,OGF_S_SMPARAMS=15,OGF_S_IKDATA=16,OGF_S_MOTION_REFS=19;
constexpr unsigned MT_NORMAL=0,MT_HIERRARHY=1,MT_PROGRESSIVE=2,MT_SKELETON_ANIM=3,MT_SKELETON_GEOMDEF_PM=4,MT_SKELETON_GEOMDEF_ST=5,MT_SKELETON_RIGID=10;
constexpr uint32_t OGF_VERTEXFORMAT_FVF_1L=0x12071980u,OGF_VERTEXFORMAT_FVF_2L=0x240e3300u;
constexpr size_t FOBB_BYTES=60,SBONE_SHAPE_BYTES=112,SJOINT_IK_V0_BYTES=72,SJOINT_IK_V1_BYTES=76;
constexpr uint8_t flTKeyPresent=1u<<0,flRKeyAbsent=1u<<1;

uint32_t u32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
uint16_t u16(const uint8_t* p){return uint16_t(p[0]|(uint16_t(p[1])<<8));}
int16_t s16(const uint8_t* p){return int16_t(u16(p));}
float f32(const uint8_t* p){float f;std::memcpy(&f,p,4);return f;}
struct Block{const uint8_t* ptr=nullptr;size_t size=0;};
bool chunk(Block src,uint32_t id,Block& found){size_t off=0;while(off+8<=src.size){const uint32_t kind=u32(src.ptr+off),length=u32(src.ptr+off+4);off+=8;if(length>src.size-off)return false;if((kind&0x7fffffffu)==id){found={src.ptr+off,length};return true;}off+=length;}return false;}
bool sane(float v){return std::isfinite(v)&&std::fabs(v)<1000000.f;}
bool saneVertex(const Vertex& v){return sane(v.p.x)&&sane(v.p.y)&&sane(v.p.z)&&sane(v.n.x)&&sane(v.n.y)&&sane(v.n.z)&&sane(v.u)&&sane(v.v);}
bool zstr(Block b,size_t& at,std::string& s){if(at>=b.size)return false;size_t e=at;while(e<b.size&&b.ptr[e])++e;if(e>=b.size)return false;s.assign(reinterpret_cast<const char*>(b.ptr+at),e-at);at=e+1;return true;}
std::string lowerAscii(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});return s;}

struct Reader{
    Block b{};size_t at=0;
    bool need(size_t n)const{return at+n<=b.size;}
    bool ru8(uint8_t& v){if(!need(1))return false;v=b.ptr[at++];return true;}
    bool ru16(uint16_t& v){if(!need(2))return false;v=u16(b.ptr+at);at+=2;return true;}
    bool ru32(uint32_t& v){if(!need(4))return false;v=u32(b.ptr+at);at+=4;return true;}
    bool rf(float& v){if(!need(4))return false;v=f32(b.ptr+at);at+=4;return sane(v);}
    bool rv3(V3& v){return rf(v.x)&&rf(v.y)&&rf(v.z);}
    bool rz(std::string& s){return zstr(b,at,s);}
    bool skip(size_t n){if(!need(n))return false;at+=n;return true;}
};

void readMaterial(Block src,MeshPart& part){Block t{};if(!chunk(src,OGF_TEXTURE,t)||!t.size)return;size_t at=0;zstr(t,at,part.texture);zstr(t,at,part.shader);}
bool appendIndices(Block ib,uint32_t nv,MeshPart& part,std::string& error){if(ib.size<4){error="OGF index chunk truncated";return false;}const uint32_t ni=u32(ib.ptr);if(!ni||ni>3000000||(ni%3)!=0||size_t(ni)*2>ib.size-4){error="OGF index bounds invalid";return false;}part.indices.reserve(ni);for(uint32_t i=0;i<ni;++i){uint16_t ix=u16(ib.ptr+4+size_t(i)*2);if(ix>=nv){error="OGF index out of bounds";return false;}part.indices.push_back(ix);}return true;}
bool appendStatic(Block vb,Block ib,MeshPart& part,std::string& error){if(vb.size<8){error="OGF vertex chunk truncated";return false;}const uint32_t fvf=u32(vb.ptr),nv=u32(vb.ptr+4);if(fvf!=0x112u){error="Unsupported OGF static FVF (expected 0x112)";return false;}constexpr size_t stride=32;if(!nv||nv>1000000||size_t(nv)*stride>vb.size-8){error="OGF static vertex bounds invalid";return false;}part.vertices.reserve(nv);for(uint32_t i=0;i<nv;++i){const uint8_t* p=vb.ptr+8+size_t(i)*stride;Vertex v;v.p={f32(p),f32(p+4),f32(p+8)};v.n={f32(p+12),f32(p+16),f32(p+20)};v.u=f32(p+24);v.v=f32(p+28);if(!saneVertex(v)){error="OGF static vertex contains invalid floats";return false;}part.vertices.push_back(v);}return appendIndices(ib,nv,part,error);}
bool appendSkeletal(Block vb,Block ib,MeshPart& part,std::string& error){if(vb.size<8){error="OGF skeletal vertex chunk truncated";return false;}const uint32_t fmt=u32(vb.ptr),nv=u32(vb.ptr+4);if(!nv||nv>1000000){error="OGF skeletal vertex count invalid";return false;}size_t stride=0,pos=0,norm=0,uv=0;if(fmt==OGF_VERTEXFORMAT_FVF_1L){stride=60;pos=0;norm=12;uv=48;}else if(fmt==OGF_VERTEXFORMAT_FVF_2L){stride=64;pos=4;norm=16;uv=56;}else{error="Unsupported SoC skeletal vertex format (1-link/2-link only)";return false;}if(size_t(nv)*stride>vb.size-8){error="OGF skeletal vertex data truncated";return false;}part.vertices.reserve(nv);for(uint32_t i=0;i<nv;++i){const uint8_t* p=vb.ptr+8+size_t(i)*stride;Vertex v;v.p={f32(p+pos),f32(p+pos+4),f32(p+pos+8)};v.n={f32(p+norm),f32(p+norm+4),f32(p+norm+8)};v.u=f32(p+uv);v.v=f32(p+uv+4);if(fmt==OGF_VERTEXFORMAT_FVF_1L){uint32_t bone=u32(p+56);if(bone>65535u){error="OGF 1-link bone id out of range";return false;}v.bone0=uint16_t(bone);v.bone1=v.bone0;v.weight=0.f;v.links=1;}else{v.bone0=u16(p);v.bone1=u16(p+2);v.weight=f32(p+52);v.links=2;if(!sane(v.weight)||v.weight<-.001f||v.weight>1.001f){error="OGF 2-link weight out of range";return false;}v.weight=std::max(0.f,std::min(1.f,v.weight));}if(!saneVertex(v)){error="OGF skeletal vertex contains invalid floats";return false;}part.vertices.push_back(v);}part.skeletal=true;return appendIndices(ib,nv,part,error);}

struct Mat{float m[4][4];};
Mat identity(){Mat r={};for(int i=0;i<4;++i)r.m[i][i]=1;return r;}
Mat setHPB(float h,float p,float b){const float sh=std::sin(h),ch=std::cos(h),sp=std::sin(p),cp=std::cos(p),sb=std::sin(b),cb=std::cos(b);const float cc=ch*cb,cs=ch*sb,sc=sh*cb,ss=sh*sb;Mat r=identity();r.m[0][0]=cc-sp*ss;r.m[0][1]=-cp*sb;r.m[0][2]=sp*cs+sc;r.m[1][0]=sp*sc+cs;r.m[1][1]=cp*cb;r.m[1][2]=ss-sp*cc;r.m[2][0]=-cp*sh;r.m[2][1]=sp;r.m[2][2]=cp*ch;return r;}
Mat localBind(V3 rot,V3 tr){Mat r=setHPB(-rot.y,-rot.x,-rot.z);r.m[3][0]=tr.x;r.m[3][1]=tr.y;r.m[3][2]=tr.z;return r;}
// X-Ray mul_43(A,B) corresponds to conventional row-vector B*A.
Mat mul43(const Mat& A,const Mat& B){Mat r=identity();for(int row=0;row<3;++row)for(int col=0;col<3;++col)r.m[row][col]=A.m[0][col]*B.m[row][0]+A.m[1][col]*B.m[row][1]+A.m[2][col]*B.m[row][2];for(int col=0;col<3;++col)r.m[3][col]=A.m[0][col]*B.m[3][0]+A.m[1][col]*B.m[3][1]+A.m[2][col]*B.m[3][2]+A.m[3][col];return r;}
Mat rigidInverse(const Mat& a){Mat r=identity();for(int i=0;i<3;++i)for(int j=0;j<3;++j)r.m[i][j]=a.m[j][i];const float tx=a.m[3][0],ty=a.m[3][1],tz=a.m[3][2];r.m[3][0]=-(tx*r.m[0][0]+ty*r.m[1][0]+tz*r.m[2][0]);r.m[3][1]=-(tx*r.m[0][1]+ty*r.m[1][1]+tz*r.m[2][1]);r.m[3][2]=-(tx*r.m[0][2]+ty*r.m[1][2]+tz*r.m[2][2]);return r;}

bool parseBones(Block src,Mesh& mesh,std::string& error){Block names{};if(!chunk(src,OGF_S_BONE_NAMES,names))return true;if(names.size<4){error="OGF bone names chunk truncated";return false;}size_t at=0;const uint32_t count=u32(names.ptr);at=4;if(!count||count>256){error="OGF bone count invalid";return false;}mesh.bones.clear();mesh.bones.resize(count);for(uint32_t i=0;i<count;++i){if(!zstr(names,at,mesh.bones[i].name)||!zstr(names,at,mesh.bones[i].parentName)){error="OGF bone name/parent truncated";return false;}if(at+FOBB_BYTES>names.size){error="OGF bone OBB truncated";return false;}at+=FOBB_BYTES;}
 std::unordered_map<std::string,int32_t> ids;for(uint32_t i=0;i<count;++i)ids[lowerAscii(mesh.bones[i].name)]=int32_t(i);for(uint32_t i=0;i<count;++i){auto it=ids.find(lowerAscii(mesh.bones[i].parentName));mesh.bones[i].parent=(mesh.bones[i].parentName.empty()||it==ids.end())?-1:it->second;}
 Block ik{};if(!chunk(src,OGF_S_IKDATA,ik))return true;at=0;for(uint32_t i=0;i<count;++i){if(at+4>ik.size){error="OGF IK data truncated before bone record";return false;}uint32_t vers=u32(ik.ptr+at);at+=4;std::string material;if(!zstr(ik,at,material)){error="OGF IK material string truncated";return false;}const size_t ikBytes=vers>0?SJOINT_IK_V1_BYTES:SJOINT_IK_V0_BYTES;const size_t need=SBONE_SHAPE_BYTES+ikBytes+12+12+4+12;if(at+need>ik.size){error="OGF IK bone record truncated";return false;}at+=SBONE_SHAPE_BYTES+ikBytes;Bone& b=mesh.bones[i];b.localRotate={f32(ik.ptr+at),f32(ik.ptr+at+4),f32(ik.ptr+at+8)};at+=12;b.localTranslate={f32(ik.ptr+at),f32(ik.ptr+at+4),f32(ik.ptr+at+8)};at+=12;at+=4+12;if(!sane(b.localRotate.x)||!sane(b.localRotate.y)||!sane(b.localRotate.z)||!sane(b.localTranslate.x)||!sane(b.localTranslate.y)||!sane(b.localTranslate.z)){error="OGF bind transform contains invalid floats";return false;}b.hasBind=true;}
 std::vector<Mat> global(count,identity());std::vector<uint8_t> done(count,0),visiting(count,0);auto solve=[&](auto&& self,int i)->bool{if(done[i])return true;if(visiting[i])return false;visiting[i]=1;Mat local=mesh.bones[i].hasBind?localBind(mesh.bones[i].localRotate,mesh.bones[i].localTranslate):identity();int p=mesh.bones[i].parent;if(p>=0){if(p>=int(count)||!self(self,p))return false;global[i]=mul43(global[p],local);}else global[i]=local;mesh.bones[i].position={global[i].m[3][0],global[i].m[3][1],global[i].m[3][2]};Mat inv=rigidInverse(global[i]);for(int r=0;r<4;++r)for(int c=0;c<4;++c)mesh.bones[i].inverseBind[size_t(r)*4+c]=inv.m[r][c];visiting[i]=0;done[i]=1;return true;};for(uint32_t i=0;i<count;++i)if(!solve(solve,int(i))){error="OGF bone hierarchy contains a cycle";return false;}return true;}

void parseMotionRefs(Block src,Mesh& mesh){Block refs{};if(!chunk(src,OGF_S_MOTION_REFS,refs)||!refs.size)return;size_t at=0;std::string all;if(!zstr(refs,at,all))return;std::stringstream ss(all);std::string item;while(std::getline(ss,item,',')){while(!item.empty()&&std::isspace(static_cast<unsigned char>(item.front())))item.erase(item.begin());while(!item.empty()&&std::isspace(static_cast<unsigned char>(item.back())))item.pop_back();if(!item.empty())mesh.motionRefs.push_back(item);}}

struct MotionDef{std::string name;float speed=1.f;bool stopAtEnd=false;};
Quat readQuat(const uint8_t* p){return {float(s16(p))/32767.f,float(s16(p+2))/32767.f,float(s16(p+4))/32767.f,float(s16(p+6))/32767.f};}

bool parseMotionContainer(Block src,Mesh& mesh,std::string& error,bool append){Block params{},motions{};if(!chunk(src,OGF_S_SMPARAMS,params)||!chunk(src,OGF_S_MOTIONS,motions))return false;if(mesh.bones.empty()){error="Motion data requires skeleton bones";return false;}
 Reader p{params};uint16_t version=0,partCount=0;if(!p.ru16(version)||!p.ru16(partCount)){error="SMPARAMS truncated";return false;}if(version<1||version>3){error="Unsupported SoC SMPARAMS version: "+std::to_string(version);return false;}if(partCount>16){error="SMPARAMS partition count invalid";return false;}
 std::unordered_map<std::string,uint16_t> boneByName;for(uint16_t i=0;i<mesh.bones.size();++i)boneByName[lowerAscii(mesh.bones[i].name)]=i;std::vector<int32_t> remap(mesh.bones.size(),-1);
 for(uint16_t part=0;part<partCount;++part){std::string partName;uint16_t bc=0;if(!p.rz(partName)||!p.ru16(bc)){error="SMPARAMS partition truncated";return false;}if(bc>mesh.bones.size()){error="SMPARAMS partition bone count invalid";return false;}for(uint16_t j=0;j<bc;++j){std::string bn;uint32_t motionIndex=0;if(!p.rz(bn)||!p.ru32(motionIndex)){error="SMPARAMS bone map truncated";return false;}auto it=boneByName.find(lowerAscii(bn));if(it==boneByName.end()||motionIndex>=remap.size()){error="SMPARAMS bone map does not match OGF skeleton";return false;}remap[motionIndex]=it->second;}}
 for(size_t i=0;i<remap.size();++i)if(remap[i]<0)remap[i]=int32_t(i);
 uint16_t defCount=0;if(!p.ru16(defCount)){error="SMPARAMS motion count missing";return false;}std::vector<MotionDef> defs(defCount);for(uint16_t i=0;i<defCount;++i){std::string name;uint32_t flags=0;uint16_t boneOrPart=0,motionId=0;float speed=1,power=1,accrue=0,falloff=0;if(!p.rz(name)||!p.ru32(flags)||!p.ru16(boneOrPart)||!p.ru16(motionId)||!p.rf(speed)||!p.rf(power)||!p.rf(accrue)||!p.rf(falloff)){error="SMPARAMS motion definition truncated";return false;}(void)motionId;defs[i]={name,speed,(flags&(1u<<1))!=0};}
 Block countBlock{};if(!chunk(motions,0,countBlock)||countBlock.size<4){error="MOTIONS count chunk missing";return false;}const uint32_t motionCount=u32(countBlock.ptr);if(!motionCount||motionCount>16383){error="MOTIONS count invalid";return false;}
 std::vector<Motion> loaded;loaded.reserve(motionCount);
 for(uint32_t mi=0;mi<motionCount;++mi){Block mb{};if(!chunk(motions,mi+1,mb)){error="MOTIONS entry missing: "+std::to_string(mi);return false;}Reader r{mb};Motion motion;if(!r.rz(motion.name)||!r.ru32(motion.frames)){error="Motion header truncated";return false;}if(!motion.frames||motion.frames>100000){error="Motion frame count invalid";return false;}motion.fps=30.f;if(mi<defs.size()){if(motion.name.empty())motion.name=defs[mi].name;motion.speed=defs[mi].speed;motion.stopAtEnd=defs[mi].stopAtEnd;}motion.tracks.resize(mesh.bones.size());
  for(size_t srcBone=0;srcBone<mesh.bones.size();++srcBone){uint8_t flags=0;if(!r.ru8(flags)){error="Motion bone flags truncated";return false;}const int32_t mapped=remap[srcBone];if(mapped<0||size_t(mapped)>=motion.tracks.size()){error="Motion bone remap invalid";return false;}MotionTrack track;
   if(flags&flRKeyAbsent){if(!r.need(8)){error="Motion constant rotation truncated";return false;}track.rotations.push_back(readQuat(r.b.ptr+r.at));r.at+=8;}else{uint32_t crc=0;if(!r.ru32(crc)||!r.need(size_t(motion.frames)*8)){error="Motion rotation keys truncated";return false;}track.rotations.resize(motion.frames);for(uint32_t k=0;k<motion.frames;++k)track.rotations[k]=readQuat(r.b.ptr+r.at+size_t(k)*8);r.at+=size_t(motion.frames)*8;}
   if(flags&flTKeyPresent){uint32_t crc=0;if(!r.ru32(crc)||!r.need(size_t(motion.frames)*3+24)){error="Motion translation keys truncated";return false;}const uint8_t* keys=r.b.ptr+r.at;r.at+=size_t(motion.frames)*3;V3 scale{},init{};if(!r.rv3(scale)||!r.rv3(init)){error="Motion translation quantizer truncated";return false;}track.translations.resize(motion.frames);for(uint32_t k=0;k<motion.frames;++k){const int8_t x=int8_t(keys[size_t(k)*3]),y=int8_t(keys[size_t(k)*3+1]),z=int8_t(keys[size_t(k)*3+2]);track.translations[k]={float(x)*scale.x+init.x,float(y)*scale.y+init.y,float(z)*scale.z+init.z};}}else{V3 init{};if(!r.rv3(init)){error="Motion constant translation truncated";return false;}track.translations.push_back(init);}
   motion.tracks[size_t(mapped)]=std::move(track);
  }
  loaded.push_back(std::move(motion));
 }
 if(!append) mesh.motions.clear();
 for(auto& m:loaded){
  bool duplicate=false;
  for(const auto& existing:mesh.motions){
   if(lowerAscii(existing.name)==lowerAscii(m.name)){duplicate=true;break;}
  }
  if(!duplicate) mesh.motions.push_back(std::move(m));
 }
 return !loaded.empty();
}

bool parse(Block src,Mesh& mesh,std::string& error,int level){const size_t partsBefore=mesh.parts.size();if(level>16){error="OGF nesting too deep";return false;}Block hdr{};if(!chunk(src,OGF_HEADER,hdr)||hdr.size<2||hdr.ptr[0]!=4){error="Expected OGF version 4 header";return false;}const unsigned typ=hdr.ptr[1];const bool known=typ==MT_NORMAL||typ==MT_HIERRARHY||typ==MT_PROGRESSIVE||typ==MT_SKELETON_ANIM||typ==MT_SKELETON_GEOMDEF_PM||typ==MT_SKELETON_GEOMDEF_ST||typ==MT_SKELETON_RIGID;if(!known){error="Unsupported OGF v4 visual type: "+std::to_string(typ);return false;}if(typ==MT_SKELETON_ANIM||typ==MT_SKELETON_GEOMDEF_PM||typ==MT_SKELETON_GEOMDEF_ST||typ==MT_SKELETON_RIGID)mesh.skeletalSource=true;if(level==0&&mesh.skeletalSource){if(!parseBones(src,mesh,error))return false;parseMotionRefs(src,mesh);std::string motionError;parseMotionContainer(src,mesh,motionError,false);}
 Block vb{},ib{};const bool hasV=chunk(src,OGF_VERTICES,vb),hasI=chunk(src,OGF_INDICES,ib);if(hasV!=hasI){error="OGF has vertices without indices (or indices without vertices)";return false;}if(hasV&&hasI){MeshPart part;readMaterial(src,part);const bool skeletal=typ==MT_SKELETON_GEOMDEF_PM||typ==MT_SKELETON_GEOMDEF_ST||typ==MT_SKELETON_RIGID;if(skeletal){if(!appendSkeletal(vb,ib,part,error))return false;}else{if(!appendStatic(vb,ib,part,error))return false;}mesh.parts.push_back(std::move(part));++mesh.sourceParts;}
 Block children{};if(chunk(src,OGF_CHILDREN,children)){size_t off=0;while(off+8<=children.size){const uint32_t length=u32(children.ptr+off+4);off+=8;if(length>children.size-off){error="OGF child size out of range";return false;}if(!parse({children.ptr+off,length},mesh,error,level+1))return false;off+=length;}if(off!=children.size){error="OGF children chunk has trailing bytes";return false;}}
 if(mesh.parts.size()>partsBefore) return true;
 Block links{};
 if(chunk(src,OGF_CHILDREN_L,links)){error="OGF uses external child references (OGF_CHILDREN_L); embedded mesh data is absent";return false;}
 if(level==0){error="OGF has no embedded previewable vertices/indices";return false;}
 return true;
}
}

bool LoadOGFMesh(const std::vector<uint8_t>& data,Mesh& mesh,std::string& error){Mesh m;error.clear();if(data.size()>128*1024*1024||data.size()<16){error="OGF size invalid";return false;}if(!parse({data.data(),data.size()},m,error,0))return false;if(!m.valid()){error="OGF contains no triangles";return false;}for(const auto& part:m.parts)if(part.skeletal)for(const auto& v:part.vertices)if(v.links&&(v.bone0>=m.bones.size()||v.bone1>=m.bones.size())){error="OGF skeletal vertex references missing bone";return false;}mesh=std::move(m);return true;}
bool LoadOGFMotions(const std::vector<uint8_t>& data,Mesh& mesh,std::string& error){error.clear();if(data.empty()||data.size()>128*1024*1024){error="OMF size invalid";return false;}Block src{data.data(),data.size()};if(!parseMotionContainer(src,mesh,error,true)){if(error.empty())error="OMF has no SoC motion chunks";return false;}return true;}
}
