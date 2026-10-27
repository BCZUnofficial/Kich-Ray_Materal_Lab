#pragma once
#include "Core.h"
#include <algorithm>
#include <cmath>

namespace lab {
// SoC R2 r2_rendertarget.cpp s_material / KichR4MaterialLut.h.
// Verified unchanged in BCZUnofficial/kichray DX11/R4 8f34fb7a0fbf8c5633494545130153031a78f953.
// Four quantized slices, not a gloss-dependent, invented Blinn exponent.
inline Image MakeR2MaterialLut() {
    Image image;image.w=128;image.h=1024;image.pixels.resize(size_t(image.w)*image.h);
    auto quantize=[](float v){return uint8_t(std::clamp(int(std::floor(v*255.5f)),0,255));};
    for(unsigned slice=0;slice<4;++slice)for(unsigned y=0;y<256;++y)for(unsigned x=0;x<128;++x){
        const float ld=float(x)/127.f;float ls=(float(y)/255.f+.0000001f)*std::pow(ld,1.f/32.f);
        float d=0,s=0;
        switch(slice){
        case 0:d=std::pow(ld,.75f);s=std::pow(ls,16.f)*.5f;break;
        case 1:d=std::pow(ld,.90f);s=std::pow(ls,24.f);break;
        case 2:d=ld;s=std::pow(ls*1.01f,128.f);break;
        case 3:{const float a=std::fabs(1-std::fabs(.05f*std::sin(33.f*ld)+ld-ls));
            const float b=std::fabs(1-std::fabs(.05f*std::cos(33.f*ld*ls)+ld-ls));
            const float c=std::fabs(1-std::fabs(ld-ls));d=ld;s=std::pow(std::max({a,b,c}),24.f)*std::pow(ld,1.f/7.f);break;}
        }
        if(x==127&&y==255)d=s=1;
        const uint8_t value=quantize(d);image.pixels[size_t(slice*256+y)*128+x]={value,value,value,quantize(s)};
    }
    return image;
}
inline float R2SpecularIntensity(V3 color,float glossFactor=1.f) {
    const float v=std::max(0.f,(color.x+color.y+color.z)/3.f);
    return glossFactor*(v<1?std::pow(v,2.f/3.f):v);
}
}
