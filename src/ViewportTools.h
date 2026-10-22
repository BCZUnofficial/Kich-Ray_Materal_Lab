#pragma once
#include "Core.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace lab {
// Kept independent of Win32/D3D so navigation and stroke edge cases can be tested.
struct ViewportCamera {
    V3 pivot{};
    float yaw=.65f, pitch=.25f, distance=3.8f;
    bool fly=true;

    V3 offset() const {
        return {distance*std::cos(pitch)*std::sin(yaw), distance*std::sin(pitch),
                distance*std::cos(pitch)*std::cos(yaw)};
    }
    V3 eye() const { const auto o=offset(); return {pivot.x+o.x,pivot.y+o.y,pivot.z+o.z}; }
    void look(float dx,float dy) {
        const auto before=eye();
        yaw=std::remainder(yaw+dx*.01f,6.283185307f);
        pitch=std::clamp(pitch+dy*.01f,-1.42f,1.42f);
        if(fly){const auto o=offset();pivot={before.x-o.x,before.y-o.y,before.z-o.z};}
    }
    void move(float right,float up,float forward) {
        pivot.x+=-std::cos(yaw)*right-std::cos(pitch)*std::sin(yaw)*forward;
        pivot.y+=up-std::sin(pitch)*forward;
        pivot.z+=std::sin(yaw)*right-std::cos(pitch)*std::cos(yaw)*forward;
    }
    void pan(float dx,float dy) { move(-dx*.0018f*distance,-dy*.0018f*distance,0); }
    void zoom(float steps) {
        if(fly) move(0,0,steps*std::max(.025f,distance*.12f));
        else distance=std::clamp(distance*std::pow(.86f,steps),.04f,24.f);
    }
    void frame(V3 center,float radius,float aspect) {
        pivot=center;
        const float halfFov=std::atan(std::tan(.392699082f)*std::min(1.f,std::max(.1f,aspect)));
        distance=std::max(.06f,radius*1.12f/std::sin(halfFov));
    }
    void reset() { const bool wasFly=fly; *this=ViewportCamera{}; fly=wasFly; }
};

// Undo tiles must wrap at the real texture edge, not at a rounded-up tile grid.
// In particular, a stamp at x=69 on a 70px texture also touches tile zero.
inline std::vector<int> WrappedBrushTiles(int center,int radius,int dimension,int tileSize=64) {
    std::vector<int> result;
    if(dimension<=0||tileSize<=0||radius<0)return result;
    const int count=(dimension+tileSize-1)/tileSize;
    if(radius>=dimension/2){for(int i=0;i<count;++i)result.push_back(i);return result;}
    std::vector<bool> seen(size_t(count),false);
    for(int x=center-radius;x<=center+radius;++x){
        int wrapped=x%dimension;if(wrapped<0)wrapped+=dimension;
        const int tile=wrapped/tileSize;
        if(!seen[size_t(tile)]){seen[size_t(tile)]=true;result.push_back(tile);}
    }
    return result;
}

// Resample in screen space: every dab is raycast onto the visible surface.
// UV island jumps and gaps between parts must never become painted UV lines.
template<class Paint>
void SampleBrushStroke(int fromX,int fromY,int toX,int toY,bool connected,Paint paint) {
    const float dx=float(toX-fromX),dy=float(toY-fromY);
    const float length=std::sqrt(dx*dx+dy*dy);
    const int steps=connected&&length<=288.f?std::max(1,int(std::ceil(length/3.f))):1;
    for(int i=1;i<=steps;++i){const float t=float(i)/float(steps);
        paint(int(std::lround(fromX+dx*t)),int(std::lround(fromY+dy*t)));
    }
}
}
