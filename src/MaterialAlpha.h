#pragma once
#include "Core.h"
#include <algorithm>
#include <cmath>

namespace lab {
enum class AlphaMode {Opaque=0,Mask=1,Blend=2};
enum class AlphaPreview {Automatic=0,Mask=1,Blend=2};
struct AlphaProfile {bool transparent=false,fractional=false,invisible=false;};
inline AlphaProfile InspectAlpha(const Image& image){
    AlphaProfile info;info.invisible=image.valid();
    for(const auto& pixel:image.pixels){
        info.transparent|=pixel.a<255;
        info.fractional|=pixel.a>0&&pixel.a<255;
        info.invisible&=pixel.a==0;
    }
    return info;
}
inline AlphaMode ResolveAlpha(const AlphaProfile& info,bool enabled,AlphaPreview preview){
    if(!enabled||!info.transparent)return AlphaMode::Opaque;
    if(preview==AlphaPreview::Mask)return AlphaMode::Mask;
    if(preview==AlphaPreview::Blend)return AlphaMode::Blend;
    return info.fractional?AlphaMode::Blend:AlphaMode::Mask;
}
inline float AlphaThreshold(AlphaMode mode,float cutoff){
    return mode==AlphaMode::Mask?std::max(1.f/255.f,std::clamp(cutoff,0.f,1.f)):1.f/255.f;
}
// Match the GPU's normalized linear-wrap texture sampling for picking through
// transparent holes; the bump texture's alpha is normal data and is never used.
inline float SampleOpacity(const GrayImage& image,float u,float v){
    if(!image.valid()||!std::isfinite(u)||!std::isfinite(v))return 1.f;
    const float x=(u-std::floor(u))*image.w-.5f,y=(v-std::floor(v))*image.h-.5f;
    const int ix=int(std::floor(x)),iy=int(std::floor(y));const float fx=x-ix,fy=y-iy;
    auto wrap=[](int value,int size){value%=size;return value<0?value+size:value;};
    auto at=[&](int px,int py){return image.pixels[size_t(wrap(py,int(image.h)))*image.w+wrap(px,int(image.w))]/255.f;};
    return (at(ix,iy)*(1-fx)+at(ix+1,iy)*fx)*(1-fy)+(at(ix,iy+1)*(1-fx)+at(ix+1,iy+1)*fx)*fy;
}
}
