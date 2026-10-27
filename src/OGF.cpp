#include "Core.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <cctype>
#include <sstream>
#include <unordered_set>
#include <new>
#include <stdexcept>

namespace lab {
namespace {
constexpr uint32_t OGF_HEADER=1,OGF_TEXTURE=2,OGF_VERTICES=3,OGF_INDICES=4,OGF_SWIDATA=6,OGF_CHILDREN=9,OGF_CHILDREN_L=10;
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
bool validChunks(Block src,std::string& error){
 size_t off=0,count=0;std::unordered_set<uint32_t> ids;
 while(off<src.size){
  if(src.size-off<8){error="Chunk table has trailing bytes";return false;}
  const uint32_t kind=u32(src.ptr+off),length=u32(src.ptr+off+4);off+=8;
  if(length>src.size-off){error="Chunk payload is truncated";return false;}
  if(++count>65536){error="Chunk count exceeds the supported limit";return false;}
  if(kind&0x80000000u){error="Compressed OGF/OMF chunks are unsupported";return false;}
  if(!ids.insert(kind).second){error="Duplicate chunk id";return false;}
  off+=length;
 }
 return true;
}
bool sane(float v){return std::isfinite(v)&&std::fabs(v)<1000000.f;}
bool saneVertex(const Vertex& v){return sane(v.p.x)&&sane(v.p.y)&&sane(v.p.z)&&sane(v.n.x)&&sane(v.n.y)&&sane(v.n.z)&&sane(v.u)&&sane(v.v);}
bool zstr(Block b,size_t& at,std::string& s){if(at>=b.size)return false;size_t e=at;while(e<b.size&&b.ptr[e])++e;if(e>=b.size||e-at>4096)return false;s.assign(reinterpret_cast<const char*>(b.ptr+at),e-at);at=e+1;return true;}
std::string lowerAscii(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});return s;}

struct Reader{
    Block b{};size_t at=0;
    bool need(size_t n)const{return at<=b.size&&n<=b.size-at;}
    bool ru8(uint8_t& v){if(!need(1))return false;v=b.ptr[at++];return true;}
    bool ru16(uint16_t& v){if(!need(2))return false;v=u16(b.ptr+at);at+=2;return true;}
    bool ru32(uint32_t& v){if(!need(4))return false;v=u32(b.ptr+at);at+=4;return true;}
    bool rf(float& v){if(!need(4))return false;v=f32(b.ptr+at);at+=4;return sane(v);}
    bool rv3(V3& v){return rf(v.x)&&rf(v.y)&&rf(v.z);}
    bool rz(std::string& s){return zstr(b,at,s);}
    bool rline(){
        // motion_marks::Save uses IWriter::w_string (CR/LF), not stringZ.
        const size_t start=at;
        while(need(1)&&b.ptr[at]!='\r'&&b.ptr[at]!='\n'){
            if(!b.ptr[at]||at-start>=4096)return false;
            ++at;
        }
        if(!need(1))return false;
        const uint8_t terminator=b.ptr[at++];
        if(need(1)&&((terminator=='\r'&&b.ptr[at]=='\n')||(terminator=='\n'&&b.ptr[at]=='\r')))++at;
        return true;
    }
    bool skip(size_t n){if(!need(n))return false;at+=n;return true;}
};

bool readMaterial(Block src,MeshPart& part,std::string& error){Block t{};if(!chunk(src,OGF_TEXTURE,t))return true;size_t at=0;if(!zstr(t,at,part.texture)||!zstr(t,at,part.shader)){error="OGF material strings are truncated";return false;}return true;}
bool appendIndices(Block ib,uint32_t nv,MeshPart& part,std::string& error){if(ib.size<4){error="OGF index chunk truncated";return false;}const uint32_t ni=u32(ib.ptr);if(!ni||ni>3000000||(ni%3)!=0||size_t(ni)*2>ib.size-4){error="OGF index bounds invalid";return false;}part.indices.reserve(ni);for(uint32_t i=0;i<ni;++i){uint16_t ix=u16(ib.ptr+4+size_t(i)*2);if(ix>=nv){error="OGF index out of bounds";return false;}part.indices.push_back(ix);}return true;}
bool appendStatic(Block vb,Block ib,MeshPart& part,std::string& error){if(vb.size<8){error="OGF vertex chunk truncated";return false;}const uint32_t fvf=u32(vb.ptr),nv=u32(vb.ptr+4);if(fvf!=0x112u){error="Unsupported OGF static FVF (expected 0x112)";return false;}constexpr size_t stride=32;if(!nv||nv>1000000||size_t(nv)*stride>vb.size-8){error="OGF static vertex bounds invalid";return false;}part.vertices.reserve(nv);for(uint32_t i=0;i<nv;++i){const uint8_t* p=vb.ptr+8+size_t(i)*stride;Vertex v;v.p={f32(p),f32(p+4),f32(p+8)};v.n={f32(p+12),f32(p+16),f32(p+20)};v.u=f32(p+24);v.v=f32(p+28);if(!saneVertex(v)){error="OGF static vertex contains invalid floats";return false;}part.vertices.push_back(v);}return appendIndices(ib,nv,part,error);}
bool appendSkeletal(Block vb,Block ib,MeshPart& part,std::string& error){
 if(vb.size<8){error="OGF skeletal vertex chunk truncated";return false;}
 const uint32_t fmt=u32(vb.ptr),nv=u32(vb.ptr+4);
 if(!nv||nv>1000000){error="OGF skeletal vertex count invalid";return false;}
 size_t stride=0,pos=0,norm=0,uv=0;
 if(fmt==OGF_VERTEXFORMAT_FVF_1L){stride=60;pos=0;norm=12;uv=48;}
 else if(fmt==OGF_VERTEXFORMAT_FVF_2L){stride=64;pos=4;norm=16;uv=56;}
 else{error="Unsupported SoC skeletal vertex format (1-link/2-link only)";return false;}
 if(size_t(nv)*stride>vb.size-8){error="OGF skeletal vertex data truncated";return false;}
 part.vertices.reserve(nv);
 for(uint32_t i=0;i<nv;++i){
  const uint8_t* p=vb.ptr+8+size_t(i)*stride;Vertex v;
  v.p={f32(p+pos),f32(p+pos+4),f32(p+pos+8)};
  v.n={f32(p+norm),f32(p+norm+4),f32(p+norm+8)};
  v.tangent={f32(p+pos+24),f32(p+pos+28),f32(p+pos+32)};
  v.binormal={f32(p+pos+36),f32(p+pos+40),f32(p+pos+44)};
  v.u=f32(p+uv);v.v=f32(p+uv+4);
  const auto validDirection=[](V3 d){return sane(d.x)&&sane(d.y)&&sane(d.z);};
  if(!validDirection(v.tangent)||!validDirection(v.binormal)){error="OGF skeletal tangent basis contains invalid floats";return false;}
  // SoC's vertBoned1W/2W stores the exporter's tangent basis. Rebuilding it
  // from triangles can change mirrored UV seams and the normal-map response.
  // Some exporters omit T/B (all zero); those vertices use the renderer's
  // derived basis instead, without claiming the absent data is authoritative.
  const V3 cross{v.n.y*v.tangent.z-v.n.z*v.tangent.y,
                 v.n.z*v.tangent.x-v.n.x*v.tangent.z,
                 v.n.x*v.tangent.y-v.n.y*v.tangent.x};
  v.hasTangentBasis=std::fabs(cross.x*v.binormal.x+cross.y*v.binormal.y+cross.z*v.binormal.z)>1e-8f;
  if(fmt==OGF_VERTEXFORMAT_FVF_1L){
   uint32_t bone=u32(p+56);
   if(bone>65535u){error="OGF 1-link bone id out of range";return false;}
   v.bone0=uint16_t(bone);v.bone1=v.bone0;v.weight=0.f;v.links=1;
  }else{
   v.bone0=u16(p);v.bone1=u16(p+2);v.weight=f32(p+52);v.links=2;
   // xrSkin2W / KichR4 VSModelGPU ignores the unused weight when both
   // links address the same bone; an uninitialized value must not deform it.
   if(v.bone0==v.bone1)v.weight=0.f;
   if(!sane(v.weight)){error="OGF 2-link weight is not finite";return false;}
   // Preserve the previous preview policy for exporters with out-of-range weights.
   v.weight=std::clamp(v.weight,0.f,1.f);
  }
  if(!saneVertex(v)){error="OGF skeletal vertex contains invalid floats";return false;}
  part.vertices.push_back(v);
 }
 part.skeletal=true;return appendIndices(ib,nv,part,error);
}

bool selectProgressiveWindow(Block src,MeshPart& part,std::string& error){
 Block swi{};
 if(!chunk(src,OGF_SWIDATA,swi)){error="Progressive OGF is missing OGF_SWIDATA";return false;}
 // Fmesh.h: reserved[4], u32 count, then {u32 offset,u16 num_tris,u16 num_verts}.
 // FProgressive and CSkeletonX_PM render sw[0] for LOD=1 (maximum detail).
 // OGF_INDICES includes several overlapping LOD windows, not one triangle list.
 if(swi.size<20){error="OGF sliding-window header truncated";return false;}
 const uint32_t count=u32(swi.ptr+16);
 if(!count||count>1000000||size_t(count)>(swi.size-20)/8||swi.size-20!=size_t(count)*8){
  error="OGF sliding-window record count invalid";return false;
 }
 uint32_t selectedOffset=0,selectedIndices=0,selectedVertices=0;
 for(uint32_t i=0;i<count;++i){
  const uint8_t* p=swi.ptr+20+size_t(i)*8;
  const uint32_t offset=u32(p),triangles=u16(p+4),vertices=u16(p+6);
  const size_t indices=size_t(triangles)*3;
  if(offset>part.indices.size()||indices>part.indices.size()-offset||vertices>part.vertices.size()||(triangles&&!vertices)){
   error="OGF sliding-window index or vertex range invalid";return false;
  }
  if(i==0){selectedOffset=offset;selectedIndices=uint32_t(indices);selectedVertices=vertices;}
 }
 if(!selectedIndices||!selectedVertices){error="OGF maximum-detail window has no triangles";return false;}
 for(size_t i=selectedOffset;i<size_t(selectedOffset)+selectedIndices;++i){
  if(part.indices[i]>=selectedVertices){error="OGF maximum-detail index exceeds its vertex window";return false;}
 }
 std::vector<uint32_t> selected(part.indices.begin()+selectedOffset,part.indices.begin()+selectedOffset+selectedIndices);
 part.indices=std::move(selected);
 part.vertices.resize(selectedVertices);
 return true;
}

struct Mat{float m[4][4];};
Mat identity(){Mat r={};for(int i=0;i<4;++i)r.m[i][i]=1;return r;}
Mat setHPB(float h,float p,float b){const float sh=std::sin(h),ch=std::cos(h),sp=std::sin(p),cp=std::cos(p),sb=std::sin(b),cb=std::cos(b);const float cc=ch*cb,cs=ch*sb,sc=sh*cb,ss=sh*sb;Mat r=identity();r.m[0][0]=cc-sp*ss;r.m[0][1]=-cp*sb;r.m[0][2]=sp*cs+sc;r.m[1][0]=sp*sc+cs;r.m[1][1]=cp*cb;r.m[1][2]=ss-sp*cc;r.m[2][0]=-cp*sh;r.m[2][1]=sp;r.m[2][2]=cp*ch;return r;}
Mat localBind(V3 rot,V3 tr){Mat r=setHPB(-rot.y,-rot.x,-rot.z);r.m[3][0]=tr.x;r.m[3][1]=tr.y;r.m[3][2]=tr.z;return r;}
// X-Ray mul_43(A,B) corresponds to conventional row-vector B*A.
Mat mul43(const Mat& A,const Mat& B){Mat r=identity();for(int row=0;row<3;++row)for(int col=0;col<3;++col)r.m[row][col]=A.m[0][col]*B.m[row][0]+A.m[1][col]*B.m[row][1]+A.m[2][col]*B.m[row][2];for(int col=0;col<3;++col)r.m[3][col]=A.m[0][col]*B.m[3][0]+A.m[1][col]*B.m[3][1]+A.m[2][col]*B.m[3][2]+A.m[3][col];return r;}
Mat rigidInverse(const Mat& a){Mat r=identity();for(int i=0;i<3;++i)for(int j=0;j<3;++j)r.m[i][j]=a.m[j][i];const float tx=a.m[3][0],ty=a.m[3][1],tz=a.m[3][2];r.m[3][0]=-(tx*r.m[0][0]+ty*r.m[1][0]+tz*r.m[2][0]);r.m[3][1]=-(tx*r.m[0][1]+ty*r.m[1][1]+tz*r.m[2][1]);r.m[3][2]=-(tx*r.m[0][2]+ty*r.m[1][2]+tz*r.m[2][2]);return r;}

bool parseBones(Block src,Mesh& mesh,std::string& error){
 Block names{};if(!chunk(src,OGF_S_BONE_NAMES,names))return true;
 if(names.size<4){error="OGF bone names chunk truncated";return false;}
 size_t at=4;const uint32_t count=u32(names.ptr);
 if(!count||count>256){error="OGF bone count invalid";return false;}
 mesh.bones.assign(count,{});std::unordered_map<std::string,int32_t> ids;
 for(uint32_t i=0;i<count;++i){
  auto& bone=mesh.bones[i];
  if(!zstr(names,at,bone.name)||!zstr(names,at,bone.parentName)){error="OGF bone name/parent truncated";return false;}
  if(bone.name.empty()||!ids.emplace(lowerAscii(bone.name),int32_t(i)).second){error="OGF bone names must be nonempty and unique";return false;}
  if(FOBB_BYTES>names.size-at){error="OGF bone OBB truncated";return false;}at+=FOBB_BYTES;
 }
 for(auto& bone:mesh.bones){
  if(bone.parentName.empty())bone.parent=-1;
  else{auto it=ids.find(lowerAscii(bone.parentName));if(it==ids.end()){error="OGF bone parent is missing";return false;}bone.parent=it->second;}
 }
 Block ik{};
 if(chunk(src,OGF_S_IKDATA,ik)){
  Reader r{ik};
  for(auto& bone:mesh.bones){
   uint32_t version=0;std::string material;
   if(!r.ru32(version)||!r.rz(material)){error="OGF IK record header truncated";return false;}
   if(version>1){error="Unsupported OGF IK record version";return false;}
   const size_t ikBytes=version?SJOINT_IK_V1_BYTES:SJOINT_IK_V0_BYTES;
   if(!r.skip(SBONE_SHAPE_BYTES+ikBytes)||!r.rv3(bone.localRotate)||!r.rv3(bone.localTranslate)||!r.skip(16)){error="OGF IK bind transform invalid or truncated";return false;}
   bone.hasBind=true;
  }
 }
 // Resolve/validate even when optional IK records are absent. A default
 // inverse bind must be identity, never the all-zero array that collapses skin.
 std::vector<Mat> global(count,identity());std::vector<uint8_t> done(count,0),visiting(count,0);
 auto solve=[&](auto&& self,size_t i)->bool{
  if(done[i])return true;if(visiting[i])return false;visiting[i]=1;
  const auto& bone=mesh.bones[i];const Mat local=bone.hasBind?localBind(bone.localRotate,bone.localTranslate):identity();
  if(bone.parent>=0){const size_t parent=size_t(bone.parent);if(parent>=count||!self(self,parent))return false;global[i]=mul43(global[parent],local);}else global[i]=local;
  mesh.bones[i].position={global[i].m[3][0],global[i].m[3][1],global[i].m[3][2]};
  const Mat inv=rigidInverse(global[i]);for(int row=0;row<4;++row)for(int col=0;col<4;++col)mesh.bones[i].inverseBind[size_t(row)*4+col]=inv.m[row][col];
  visiting[i]=0;done[i]=1;return true;
 };
 for(size_t i=0;i<count;++i)if(!solve(solve,i)){error="OGF bone hierarchy contains a cycle";return false;}
 return true;
}

void parseMotionRefs(Block src,Mesh& mesh){Block refs{};if(!chunk(src,OGF_S_MOTION_REFS,refs)||!refs.size)return;size_t at=0;std::string all;if(!zstr(refs,at,all))return;std::stringstream ss(all);std::string item;while(std::getline(ss,item,',')){while(!item.empty()&&std::isspace(static_cast<unsigned char>(item.front())))item.erase(item.begin());while(!item.empty()&&std::isspace(static_cast<unsigned char>(item.back())))item.pop_back();if(!item.empty())mesh.motionRefs.push_back(item);}}

struct MotionDef{std::string name;float speed=1.f;bool stopAtEnd=false;uint16_t motionId=0;};
Quat readQuat(const uint8_t* p){return {float(s16(p))/32767.f,float(s16(p+2))/32767.f,float(s16(p+4))/32767.f,float(s16(p+6))/32767.f};}

bool parseMotionContainer(Block src,Mesh& mesh,std::string& error,bool append){
 if(!validChunks(src,error))return false;
 Block params{},motions{};if(!chunk(src,OGF_S_SMPARAMS,params)||!chunk(src,OGF_S_MOTIONS,motions))return false;if(mesh.bones.empty()||mesh.bones.size()>256){error="Motion data requires 1..256 skeleton bones";return false;}if(!validChunks(motions,error))return false;
 Reader p{params};uint16_t version=0,partCount=0;if(!p.ru16(version)||!p.ru16(partCount)){error="SMPARAMS truncated";return false;}if(version<1||version>4){error="Unsupported SoC SMPARAMS version: "+std::to_string(version);return false;}if(partCount>16){error="SMPARAMS partition count invalid";return false;}
 std::unordered_map<std::string,uint16_t> boneByName;for(uint16_t i=0;i<mesh.bones.size();++i)boneByName[lowerAscii(mesh.bones[i].name)]=i;std::vector<int32_t> remap(mesh.bones.size(),-1);
 for(uint16_t part=0;part<partCount;++part){std::string partName;uint16_t bc=0;if(!p.rz(partName)||!p.ru16(bc)){error="SMPARAMS partition truncated";return false;}if(bc>mesh.bones.size()){error="SMPARAMS partition bone count invalid";return false;}for(uint16_t j=0;j<bc;++j){std::string bn;uint32_t motionIndex=0;if(!p.rz(bn)||!p.ru32(motionIndex)){error="SMPARAMS bone map truncated";return false;}auto it=boneByName.find(lowerAscii(bn));if(it==boneByName.end()||motionIndex>=remap.size()){error="SMPARAMS bone map does not match OGF skeleton";return false;}if(remap[motionIndex]>=0&&remap[motionIndex]!=it->second){error="SMPARAMS bone remap conflicts";return false;}remap[motionIndex]=it->second;}}
 std::vector<bool> mappedBone(mesh.bones.size(),false);
 for(size_t i=0;i<remap.size();++i){if(remap[i]<0||mappedBone[size_t(remap[i])]){error="SMPARAMS bone map is incomplete or duplicated";return false;}mappedBone[size_t(remap[i])]=true;}
 uint16_t defCount=0;if(!p.ru16(defCount)){error="SMPARAMS motion count missing";return false;}
 std::vector<MotionDef> defs(defCount);
 for(uint16_t i=0;i<defCount;++i){
  std::string name;uint32_t flags=0;uint16_t boneOrPart=0,motionId=0;float speed=1,power=1,accrue=0,falloff=0;
  if(!p.rz(name)||!p.ru32(flags)||!p.ru16(boneOrPart)||!p.ru16(motionId)||!p.rf(speed)||!p.rf(power)||!p.rf(accrue)||!p.rf(falloff)){error="SMPARAMS motion definition truncated";return false;}
  defs[i]={name,speed,(flags&(1u<<1))!=0,motionId};
  if(version>=4){
   // The viewer does not execute gameplay marks, but must consume their exact
   // layout before reading the next definition (KichRay CMotionDef::Load).
   uint32_t marks=0;
   if(!p.ru32(marks)||marks>4096){error="SMPARAMS motion mark count invalid";return false;}
   for(uint32_t mark=0;mark<marks;++mark){
    uint32_t intervals=0;
    if(!p.rline()||!p.ru32(intervals)||intervals>1000000||size_t(intervals)>(p.b.size-p.at)/8){error="SMPARAMS motion mark is truncated";return false;}
    for(uint32_t interval=0;interval<intervals;++interval){
     float begin=0,end=0;
     if(!p.rf(begin)||!p.rf(end)||end<begin){error="SMPARAMS motion mark interval invalid";return false;}
    }
   }
  }
 }
 Block countBlock{};if(!chunk(motions,0,countBlock)||countBlock.size<4){error="MOTIONS count chunk missing";return false;}const uint32_t motionCount=u32(countBlock.ptr);if(!motionCount||motionCount>16383){error="MOTIONS count invalid";return false;}
 std::vector<int32_t> definitionByMotion(motionCount,-1);
 std::unordered_map<std::string,size_t> definitionByName;
 for(size_t i=0;i<defs.size();++i){
  if(defs[i].motionId>=motionCount){error="SMPARAMS motion id is out of range";return false;}
  if(definitionByMotion[defs[i].motionId]<0)definitionByMotion[defs[i].motionId]=int32_t(i);
  definitionByName.emplace(lowerAscii(defs[i].name),i);
 }
 // Index once: repeatedly rescanning the MOTIONS chunk is quadratic for a
 // large OMF. Limit decoded key memory before allocating any individual track.
 std::vector<Block> motionBlocks(size_t(motionCount)+1);
 for(size_t off=0;off<motions.size;){const uint32_t id=u32(motions.ptr+off),length=u32(motions.ptr+off+4);off+=8;if(id<=motionCount)motionBlocks[id]={motions.ptr+off,length};off+=length;}
 constexpr size_t maxDecodedBytes=256u*1024u*1024u;size_t decodedBytes=0;
 auto account=[&](size_t bytes){if(bytes>maxDecodedBytes-decodedBytes){error="Decoded animation exceeds the 256 MiB limit";return false;}decodedBytes+=bytes;return true;};
 if(append)for(const auto& existing:mesh.motions){if(!account(existing.tracks.size()*sizeof(MotionTrack)))return false;for(const auto& track:existing.tracks)if(!account(track.rotations.size()*sizeof(Quat)+track.translations.size()*sizeof(V3)))return false;}
 std::vector<Motion> loaded;loaded.reserve(motionCount);
 for(uint32_t mi=0;mi<motionCount;++mi){Block mb=motionBlocks[mi+1];if(!mb.ptr){error="MOTIONS entry missing: "+std::to_string(mi);return false;}Reader r{mb};Motion motion;if(!r.rz(motion.name)||!r.ru32(motion.frames)){error="Motion header truncated";return false;}if(!motion.frames||motion.frames>100000){error="Motion frame count invalid";return false;}motion.fps=30.f;
  int32_t definition=definitionByMotion[mi];
  const auto named=definitionByName.find(lowerAscii(motion.name));
  if(named!=definitionByName.end()&&defs[named->second].motionId==mi)definition=int32_t(named->second);
  if(definition>=0){const auto& def=defs[size_t(definition)];if(motion.name.empty())motion.name=def.name;motion.speed=def.speed;motion.stopAtEnd=def.stopAtEnd;}
  if(!account(mesh.bones.size()*sizeof(MotionTrack)))return false;
  motion.tracks.resize(mesh.bones.size());
  for(size_t srcBone=0;srcBone<mesh.bones.size();++srcBone){uint8_t flags=0;if(!r.ru8(flags)){error="Motion bone flags truncated";return false;}if(flags&~(flTKeyPresent|flRKeyAbsent)){error="Unsupported SoC motion key flags";return false;}const int32_t mapped=remap[srcBone];if(mapped<0||size_t(mapped)>=motion.tracks.size()){error="Motion bone remap invalid";return false;}MotionTrack track;
   if(flags&flRKeyAbsent){if(!r.need(8)){error="Motion constant rotation truncated";return false;}if(!account(sizeof(Quat)))return false;track.rotations.push_back(readQuat(r.b.ptr+r.at));r.at+=8;}else{uint32_t crc=0;if(!r.ru32(crc)||!r.need(size_t(motion.frames)*8)){error="Motion rotation keys truncated";return false;}if(!account(size_t(motion.frames)*sizeof(Quat)))return false;track.rotations.resize(motion.frames);for(uint32_t k=0;k<motion.frames;++k)track.rotations[k]=readQuat(r.b.ptr+r.at+size_t(k)*8);r.at+=size_t(motion.frames)*8;}
   if(flags&flTKeyPresent){uint32_t crc=0;if(!r.ru32(crc)||!r.need(size_t(motion.frames)*3+24)){error="Motion translation keys truncated";return false;}const uint8_t* keys=r.b.ptr+r.at;r.at+=size_t(motion.frames)*3;V3 scale{},init{};if(!r.rv3(scale)||!r.rv3(init)){error="Motion translation quantizer truncated";return false;}if(!account(size_t(motion.frames)*sizeof(V3)))return false;track.translations.resize(motion.frames);for(uint32_t k=0;k<motion.frames;++k){const int8_t x=int8_t(keys[size_t(k)*3]),y=int8_t(keys[size_t(k)*3+1]),z=int8_t(keys[size_t(k)*3+2]);track.translations[k]={float(x)*scale.x+init.x,float(y)*scale.y+init.y,float(z)*scale.z+init.z};const auto& value=track.translations[k];if(!sane(value.x)||!sane(value.y)||!sane(value.z)){error="Decoded motion translation is out of range";return false;}}}else{V3 init{};if(!r.rv3(init)){error="Motion constant translation truncated";return false;}if(!account(sizeof(V3)))return false;track.translations.push_back(init);}
   motion.tracks[size_t(mapped)]=std::move(track);
  }
  loaded.push_back(std::move(motion));
 }
 std::unordered_set<std::string> motionNames;
 if(append)for(const auto& existing:mesh.motions)motionNames.insert(lowerAscii(existing.name));
 std::vector<size_t> additions;additions.reserve(loaded.size());
 for(size_t i=0;i<loaded.size();++i)if(motionNames.insert(lowerAscii(loaded[i].name)).second)additions.push_back(i);
 // Complete every allocation before mutating the existing animation list.
 mesh.motions.reserve((append?mesh.motions.size():0)+additions.size());
 if(!append)mesh.motions.clear();
 for(size_t i:additions)mesh.motions.push_back(std::move(loaded[i]));
 return !loaded.empty();
}

bool parse(Block src,Mesh& mesh,std::string& error,int level){const size_t partsBefore=mesh.parts.size();if(level>16){error="OGF nesting too deep";return false;}if(!validChunks(src,error))return false;Block hdr{};if(!chunk(src,OGF_HEADER,hdr)||hdr.size<44||hdr.ptr[0]!=4){error="Expected OGF version 4 header";return false;}const unsigned typ=hdr.ptr[1];const bool known=typ==MT_NORMAL||typ==MT_HIERRARHY||typ==MT_PROGRESSIVE||typ==MT_SKELETON_ANIM||typ==MT_SKELETON_GEOMDEF_PM||typ==MT_SKELETON_GEOMDEF_ST||typ==MT_SKELETON_RIGID;if(!known){error="Unsupported OGF v4 visual type: "+std::to_string(typ);return false;}if(typ==MT_SKELETON_ANIM||typ==MT_SKELETON_GEOMDEF_PM||typ==MT_SKELETON_GEOMDEF_ST||typ==MT_SKELETON_RIGID)mesh.skeletalSource=true;if(level==0&&mesh.skeletalSource){if(!parseBones(src,mesh,error))return false;parseMotionRefs(src,mesh);Block motions{},params{};const bool hasMotions=chunk(src,OGF_S_MOTIONS,motions),hasParams=chunk(src,OGF_S_SMPARAMS,params);if(hasMotions||hasParams){if(!hasMotions||!hasParams){error="Embedded motion chunks are incomplete";return false;}if(!parseMotionContainer(src,mesh,error,false))return false;}}
 Block vb{},ib{};const bool hasV=chunk(src,OGF_VERTICES,vb),hasI=chunk(src,OGF_INDICES,ib);if(hasV!=hasI){error="OGF has vertices without indices (or indices without vertices)";return false;}if(hasV&&hasI){if(mesh.parts.size()>=4096){error="OGF mesh part limit exceeded";return false;}MeshPart part;if(!readMaterial(src,part,error))return false;const bool skeletal=typ==MT_SKELETON_GEOMDEF_PM||typ==MT_SKELETON_GEOMDEF_ST||typ==MT_SKELETON_RIGID;if(skeletal){if(!appendSkeletal(vb,ib,part,error))return false;}else{if(!appendStatic(vb,ib,part,error))return false;}if((typ==MT_PROGRESSIVE||typ==MT_SKELETON_GEOMDEF_PM)&&!selectProgressiveWindow(src,part,error))return false;mesh.parts.push_back(std::move(part));++mesh.sourceParts;}
 Block children{};if(chunk(src,OGF_CHILDREN,children)){if(!validChunks(children,error))return false;size_t off=0;while(off+8<=children.size){const uint32_t length=u32(children.ptr+off+4);off+=8;if(length>children.size-off){error="OGF child size out of range";return false;}if(!parse({children.ptr+off,length},mesh,error,level+1))return false;off+=length;}if(off!=children.size){error="OGF children chunk has trailing bytes";return false;}}
 if(mesh.parts.size()>partsBefore) return true;
 Block links{};
 if(chunk(src,OGF_CHILDREN_L,links)){error="OGF uses external child references (OGF_CHILDREN_L); embedded mesh data is absent";return false;}
 if(level==0){error="OGF has no embedded previewable vertices/indices";return false;}
 return true;
}
}

bool LoadOGFMesh(const std::vector<uint8_t>& data,Mesh& mesh,std::string& error){
 error.clear();
 try{
  Mesh candidate;
  if(data.size()>128u*1024u*1024u||data.size()<16){error="OGF size invalid";return false;}
  if(!parse({data.data(),data.size()},candidate,error,0))return false;
  if(!candidate.valid()){error="OGF contains no triangles";return false;}
  for(const auto& part:candidate.parts)if(part.skeletal)for(const auto& vertex:part.vertices)if(vertex.links&&(vertex.bone0>=candidate.bones.size()||vertex.bone1>=candidate.bones.size())){error="OGF skeletal vertex references missing bone";return false;}
  mesh=std::move(candidate);return true;
 }catch(const std::bad_alloc&){error="Not enough memory to load OGF";return false;}
 catch(const std::length_error&){error="OGF allocation size is invalid";return false;}
}
bool LoadOGFMotions(const std::vector<uint8_t>& data,Mesh& mesh,std::string& error){
 error.clear();
 try{
  if(data.empty()||data.size()>128u*1024u*1024u){error="OMF size invalid";return false;}
  if(!parseMotionContainer({data.data(),data.size()},mesh,error,true)){if(error.empty())error="OMF has no SoC motion chunks";return false;}
  return true;
 }catch(const std::bad_alloc&){error="Not enough memory to load OMF";return false;}
 catch(const std::length_error&){error="OMF allocation size is invalid";return false;}
}
}
