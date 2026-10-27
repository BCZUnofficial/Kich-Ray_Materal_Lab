#pragma once
#include "Core.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace lab {
// A preview prop keeps its native OGF units. The parent skeleton supplies the
// bone transform; there is no independent centering/normalization of the prop.
struct AttachmentTransform {
    V3 position{};
    V3 rotation{}; // local XYZ Euler angles, in degrees
    float scale=1.f;
};
inline bool ValidAttachmentTransform(const AttachmentTransform& t) {
    auto finite=[](float v){return std::isfinite(v)&&std::fabs(v)<=1e6f;};
    return finite(t.position.x)&&finite(t.position.y)&&finite(t.position.z)
        &&finite(t.rotation.x)&&finite(t.rotation.y)&&finite(t.rotation.z)
        &&std::isfinite(t.scale)&&t.scale>=.0001f&&t.scale<=1000.f;
}
inline std::pair<uint32_t,uint32_t> PreviewCaptureSize(uint32_t width,uint32_t height,uint32_t requested) {
    if(!width||!height)return {0,0};
    const uint32_t limit=std::clamp(requested,1u,1024u),largest=std::max(width,height);
    if(largest<=limit)return {width,height};
    return {std::max(1u,uint32_t((uint64_t(width)*limit+largest/2)/largest)),
            std::max(1u,uint32_t((uint64_t(height)*limit+largest/2)/largest))};
}
// The transparent render target contains premultiplied RGB. Average there,
// then unpremultiply once for a straight-alpha PNG. This avoids black fringes
// at the supersampled silhouette and around translucent material texels.
inline Image ResolvePreviewCapture(const Image& source,uint32_t width,uint32_t height) {
    if(!source.valid()||!width||!height||source.w%width||source.h%height)return {};
    const uint32_t sx=source.w/width,sy=source.h/height;
    if(!sx||!sy)return {};
    Image result;result.w=width;result.h=height;result.pixels.resize(size_t(width)*height);
    const uint64_t samples=uint64_t(sx)*sy;
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){
        uint64_t r=0,g=0,b=0,a=0;
        for(uint32_t dy=0;dy<sy;++dy)for(uint32_t dx=0;dx<sx;++dx){
            const auto& p=source.pixels[size_t(y*sy+dy)*source.w+x*sx+dx];
            r+=p.r;g+=p.g;b+=p.b;a+=p.a;
        }
        auto& out=result.pixels[size_t(y)*width+x];
        out={0,0,0,uint8_t((a+samples/2)/samples)};
        if(a&&out.a){
            auto straight=[&](uint64_t c){return uint8_t(std::min<uint64_t>(255,(c*255+a/2)/a));};
            out.r=straight(r);out.g=straight(g);out.b=straight(b);
        }
    }
    return result;
}
}
