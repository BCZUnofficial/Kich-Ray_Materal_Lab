// Read-only analysis of a user-supplied SoC pair. Temporary exports are isolated
// under the system temporary directory and are removed on completion.
#include "Core.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
namespace {
struct TemporaryDirectory {
 std::filesystem::path path=std::filesystem::temp_directory_path()/("material-reference-audit-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 TemporaryDirectory(){std::filesystem::create_directory(path);}
 ~TemporaryDirectory(){std::error_code ec;std::filesystem::remove_all(path,ec);}
};
std::vector<uint8_t> read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot open reference");return {std::istreambuf_iterator<char>(f),{}};}
lab::Image decode(const std::vector<uint8_t>& bytes){std::string e;auto out=lab::DecodeDDS(bytes,e);if(!out.valid())throw std::runtime_error(e);return out;}
double angle(lab::V3 a,lab::V3 b){return std::acos(std::clamp(double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z,-1.,1.))*180./3.141592653589793;}
struct Metrics {double mean=0,p95=0,maximum=0,gloss=0,height=0;};
Metrics compare(const lab::BumpPair& a,const lab::BumpPair& b){
 if(a.bump.w!=b.bump.w||a.bump.h!=b.bump.h)throw std::runtime_error("Roundtrip dimensions changed");
 Metrics out;std::vector<double> angles;angles.reserve(a.bump.pixels.size());
 for(size_t i=0;i<a.bump.pixels.size();++i){const double e=angle(lab::DecodeBumpNormal(a.bump.pixels[i],a.bumpX.pixels[i],true),lab::DecodeBumpNormal(b.bump.pixels[i],b.bumpX.pixels[i],true));if(!std::isfinite(e))throw std::runtime_error("Normal angle is not finite");angles.push_back(e);out.mean+=e;const double ga=a.bump.pixels[i].r/255.,gb=b.bump.pixels[i].r/255.;out.gloss+=(ga*ga-gb*gb)*(ga*ga-gb*gb);const double dh=(int(a.bumpX.pixels[i].a)-int(b.bumpX.pixels[i].a))/255.;out.height+=dh*dh;}
 std::sort(angles.begin(),angles.end());out.mean/=angles.size();out.p95=angles[size_t(angles.size()*.95)];out.maximum=angles.back();out.gloss=std::sqrt(out.gloss/angles.size());out.height=std::sqrt(out.height/angles.size());return out;
}
void printMetrics(const Metrics& m){std::cout<<"{\"mean_angle_deg\":"<<m.mean<<",\"p95_angle_deg\":"<<m.p95<<",\"max_angle_deg\":"<<m.maximum<<",\"gloss_rmse\":"<<m.gloss<<",\"height_rmse\":"<<m.height<<"}";}
uint8_t channel(lab::Pixel p,int c){switch(c){case 0:return p.r;case 1:return p.g;case 2:return p.b;default:return p.a;}}
void stats(const lab::Image& image){std::cout<<"[";for(int c=0;c<4;++c){int lo=255,hi=0;double sum=0;for(auto p:image.pixels){const int v=channel(p,c);lo=std::min(lo,v);hi=std::max(hi,v);sum+=v;}if(c)std::cout<<",";std::cout<<"{\"min\":"<<lo<<",\"max\":"<<hi<<",\"mean\":"<<sum/image.pixels.size()<<"}";}std::cout<<"]";}
lab::BumpPair sdkResidual(lab::BumpPair pair){
const auto packed=decode(lab::EncodeBC3DDS(pair.bump,{pair.bump}));
 for(size_t i=0;i<packed.pixels.size();++i){const auto a=pair.bump.pixels[i],b=packed.pixels[i];auto& e=pair.bumpX.pixels[i];e.r=uint8_t(std::clamp(int(e.r)+2*(int(a.a)-int(b.a))/3,0,255));e.g=uint8_t(std::clamp(int(e.g)+2*(int(a.b)-int(b.b))/3,0,255));e.b=uint8_t(std::clamp(int(e.b)+2*(int(a.g)-int(b.g))/3,0,255));}
 pair.bump=packed;pair.bumpX=decode(lab::EncodeBC3DDS(pair.bumpX,{pair.bumpX}));return pair;
}
}
int main(int argc,char** argv){try{
 if(argc!=3){std::cerr<<"Usage: reference-audit <bump.dds> <bump#.dds>\n";return 2;}
 const auto raw=read(argv[1]),rawX=read(argv[2]);const lab::BumpPair reference{decode(raw),decode(rawX)};const auto& a=reference.bump;const auto& b=reference.bumpX;
 if(a.w!=b.w||a.h!=b.h)throw std::runtime_error("Reference pair dimensions differ");
 const auto n=lab::UnpackNormalMap(a,b);const auto g=lab::UnpackGlossGray(a),h=lab::UnpackHeightGray(b);std::string error;TemporaryDirectory temporary;
 const auto base=temporary.path/"roundtrip";if(!lab::ExportBumpLayers(n,g,h,a.w,a.h,base.wstring(),error))throw std::runtime_error(error);
 const lab::BumpPair exported{decode(read(temporary.path/"roundtrip_bump.dds")),decode(read(temporary.path/"roundtrip_bump#.dds"))};const auto layerRepack=compare(reference,exported);
 if(!lab::ExportPackedBumpPair(reference,(temporary.path/"packed").wstring(),error))throw std::runtime_error(error);const lab::BumpPair packedExport{decode(read(temporary.path/"packed_bump.dds")),decode(read(temporary.path/"packed_bump#.dds"))};const auto full=compare(reference,packedExport),sdk=compare(reference,sdkResidual(reference));
 const auto identity=lab::CompositeBumpLayersPreview(n,g,h,n,g,h,reference);size_t changed=0,normalChanged=0;double zBoost=0;auto editedGloss=g;editedGloss.pixels[editedGloss.pixels.size()/2]=uint8_t(editedGloss.pixels[editedGloss.pixels.size()/2]<238?editedGloss.pixels[editedGloss.pixels.size()/2]+17:0);const auto glossPaint=lab::CompositeBumpLayersPreview(n,editedGloss,h,n,g,h,reference);
 for(size_t i=0;i<a.pixels.size();++i){for(int c=0;c<4;++c){changed+=(channel(a.pixels[i],c)!=channel(identity.bump.pixels[i],c));changed+=(channel(b.pixels[i],c)!=channel(identity.bumpX.pixels[i],c));}normalChanged+=a.pixels[i].a!=glossPaint.bump.pixels[i].a;normalChanged+=a.pixels[i].b!=glossPaint.bump.pixels[i].b;normalChanged+=a.pixels[i].g!=glossPaint.bump.pixels[i].g;normalChanged+=b.pixels[i].r!=glossPaint.bumpX.pixels[i].r;normalChanged+=b.pixels[i].g!=glossPaint.bumpX.pixels[i].g;normalChanged+=b.pixels[i].b!=glossPaint.bumpX.pixels[i].b;zBoost+=angle(lab::DecodeBumpNormal(a.pixels[i],b.pixels[i],true),lab::DecodeBumpNormal(a.pixels[i],b.pixels[i]));}
 if(!lab::WriteBumpPairDDSBytesAtomic((temporary.path/"exact").wstring(),raw,rawX,error))throw std::runtime_error(error);const bool exact=read(temporary.path/"exact_bump.dds")==raw&&read(temporary.path/"exact_bump#.dds")==rawX;
 std::cout<<std::fixed<<std::setprecision(6)<<"{\n\"width\":"<<a.w<<",\"height\":"<<a.h<<",\n\"bump_rgba_stats\":";stats(a);std::cout<<",\n\"bumpX_rgba_stats\":";stats(b);std::cout<<",\n\"soc_z_boost_mean_angle_deg\":"<<zBoost/a.pixels.size()<<",\n\"layer_repack_roundtrip\":";printMetrics(layerRepack);std::cout<<",\n\"edited_export_full_residual\":";printMetrics(full);std::cout<<",\n\"edited_export_sdk_two_thirds_residual\":";printMetrics(sdk);std::cout<<",\n\"identity_preview_changed_bytes\":"<<changed<<",\n\"gloss_only_preview_changed_normal_bytes\":"<<normalChanged<<",\n\"unchanged_export_byte_exact\":"<<(exact?"true":"false")<<"\n}\n";
 return changed||normalChanged||!exact?1:0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
