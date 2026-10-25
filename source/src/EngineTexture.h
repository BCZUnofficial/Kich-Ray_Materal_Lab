#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <utility>

namespace lab {
struct EngineTextureMetadata {
    float material=1.f;
    bool hasMaterial=false,hasBumpMode=false,bumpEnabled=false;
    std::string bumpName;
};
// X-Ray THM is a sequence of little-endian (id, size, payload) chunks.
// Ignore thumbnail/unknown chunks; reject truncation before installing metadata.
inline bool ReadEngineTextureMetadata(const std::vector<uint8_t>& bytes,EngineTextureMetadata& output) {
    if(bytes.empty())return false;
    auto u32=[&](size_t p){return uint32_t(bytes[p])|(uint32_t(bytes[p+1])<<8)|(uint32_t(bytes[p+2])<<16)|(uint32_t(bytes[p+3])<<24);};
    EngineTextureMetadata next;
    for(size_t at=0;at<bytes.size();) {
        if(bytes.size()-at<8)return false;
        const uint32_t id=u32(at),size=u32(at+4);at+=8;
        if(size>bytes.size()-at)return false;
        if(id==0x0816) {
            if(size!=8||u32(at)>3)return false;
            const uint32_t bits=u32(at+4);float weight;std::memcpy(&weight,&bits,4);
            if(!std::isfinite(weight)||weight<0||weight>1)return false;
            next.material=float(u32(at))+weight;next.hasMaterial=true;
        }else if(id==0x0817) {
            if(size<9)return false;
            size_t end=at+8;while(end<at+size&&bytes[end])++end;
            if(end==at+size)return false;
            next.hasBumpMode=true;next.bumpEnabled=u32(at+4)==2;
            next.bumpName.assign(reinterpret_cast<const char*>(bytes.data()+at+8),end-at-8);
        }
        at+=size;
    }
    output=std::move(next);return true;
}
}
