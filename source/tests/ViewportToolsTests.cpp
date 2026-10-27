#include "ViewportTools.h"
#include "MaterialAlpha.h"
#include <iostream>
#include <stdexcept>
#include <set>

namespace {
void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
float distance(lab::V3 a,lab::V3 b){const float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return std::sqrt(x*x+y*y+z*z);}

void cameraNavigation(){
    lab::ViewportCamera c;
    const auto eye=c.eye();c.look(120,30);
    check(distance(eye,c.eye())<1e-5f,"Looking around in flight must keep the eye stationary");
    const auto before=c.eye();c.move(0,0,1);
    check(std::fabs(distance(before,c.eye())-1.f)<1e-5f,"Flight forward must move one world unit");
    c.fly=false;const auto pivot=c.pivot;c.look(-15,10);
    check(distance(pivot,c.pivot)<1e-6f,"Orbit must keep the target fixed");
    const auto orbitEye=c.eye();c.fly=true;
    check(distance(orbitEye,c.eye())<1e-6f,"Switching flight/orbit must not jump the camera");
    c.look(100000,100000);check(std::isfinite(c.eye().x),"Large mouse motion must keep the camera finite");
    c.reset();check(c.fly&&distance(c.pivot,{})<1e-6f,"Reset must preserve the selected navigation mode");
}
void framePortraitViewport(){
    lab::ViewportCamera c;
    c.frame({2,3,4},.75f,2.f);const float wide=c.distance;
    c.frame({2,3,4},.75f,.5f);
    check(c.distance>wide,"A narrow viewport must move farther away to frame the same mesh");
    check(distance(c.pivot,{2,3,4})<1e-6f,"Focus must center the selected mesh, not the world origin");
}
void flightDirection(){
    // Guard the reported backward-flight symptom at the math/input boundary.
    // Forward input must move in the actual view direction at every yaw/pitch;
    // a released (zero) input must never retain velocity from the last frame.
    for(float yaw:{-3.141592654f,-1.570796327f,0.f,.65f,1.570796327f,3.141592654f})
        for(float pitch:{-1.570796327f,-1.3f,0.f,1.3f,1.570796327f}){
            lab::ViewportCamera c;c.pivot={2,-3,5};c.yaw=yaw;c.pitch=pitch;
            const auto before=c.eye();const auto offset=c.offset();
            const lab::V3 forward{-offset.x/c.distance,-offset.y/c.distance,-offset.z/c.distance};
            c.move(0,0,.25f);const auto after=c.eye();
            const lab::V3 expected{before.x+forward.x*.25f,before.y+forward.y*.25f,before.z+forward.z*.25f};
            check(distance(after,expected)<2e-6f,"W movement must follow view direction, including vertical flight");
            c.move(0,0,-.25f);
            check(distance(c.eye(),before)<2e-6f,"S movement must be the exact reverse of W");
            const auto stopped=c.eye();
            for(int frame=0;frame<100;++frame)c.move(0,0,0);
            check(distance(c.eye(),stopped)==0,"no input must mean no movement or residual backward velocity");
            c.zoom(1);const auto zoomed=c.eye();
            const float along=(zoomed.x-stopped.x)*forward.x+(zoomed.y-stopped.y)*forward.y+(zoomed.z-stopped.z)*forward.z;
            check(along>0,"positive fly zoom must advance along the same view direction as W");
            const auto eye=c.eye();c.look(30,-20);
            check(distance(c.eye(),eye)<2e-6f,"looking after movement must not add backward translation");
        }
}
void periodicUndoCoverage(){
    // Check the actual scalar brush against the selected undo tiles, including
    // thin rectangular maps and the partial final tile of a 70px texture.
    const int sizes[][2]={{70,130},{130,70},{7,129},{128,128}};
    for(const auto& size:sizes)for(int corner=0;corner<4;++corner){
        const int w=size[0],h=size[1];
        lab::GrayImage base;base.w=uint32_t(w);base.h=uint32_t(h);base.pixels.assign(size_t(w)*h,100);
        auto painted=base;lab::GrayImage coverage;
        const float u=corner&1?.999f:.001f,v=corner&2?.999f:.001f;
        const float radius=9.f;
        lab::StampScalarDeltaLayer(painted,base,coverage,lab::MakeDefaultNormalPreset(32),u,v,radius,.3f,.5f);
        auto xs=lab::WrappedBrushTiles(int(std::floor(u*w)),int(std::ceil(radius)),w);
        auto ys=lab::WrappedBrushTiles(int(std::floor(v*h)),int(std::ceil(radius)),h);
        std::set<int> xTiles(xs.begin(),xs.end()),yTiles(ys.begin(),ys.end());
        size_t changed=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){const size_t i=size_t(y)*w+x;
            if(base.pixels[i]!=painted.pixels[i]){++changed;
                check(xTiles.count(x/64)&&yTiles.count(y/64),"Undo skipped a modified texel at a wrapped NPOT edge");
            }
        }
        check(changed>0,"The test stamp must actually modify pixels");
        for(int ty:ys)for(int tx:xs)for(int y=ty*64;y<std::min(h,(ty+1)*64);++y)
            for(int x=tx*64;x<std::min(w,(tx+1)*64);++x)painted.pixels[size_t(y)*w+x]=base.pixels[size_t(y)*w+x];
        check(painted.pixels==base.pixels,"Undo must restore the complete corner stroke");
    }
}
void screenSpaceStroke(){
    int samples=0,lastX=0,lastY=0;
    lab::SampleBrushStroke(0,0,60,30,true,[&](int x,int y){
        check(std::hypot(float(x-lastX),float(y-lastY))<=4.f,"Stroke spacing left a gap");
        // Simulated UV islands: reject screen hits in a surface gap. There is
        // intentionally no operation interpolating the two islands' UVs.
        if(x<20||x>40)++samples;lastX=x;lastY=y;
    });
    check(lastX==60&&lastY==30&&samples>1,"Stroke must reach its endpoint through real ray samples");
    samples=0;lab::SampleBrushStroke(0,0,900,0,true,[&](int x,int){++samples;check(x==900,"A cursor warp painted a long unintended line");});
    check(samples==1,"Cursor warps must be bounded to one dab");
}
void diffuseAlpha(){
    lab::Image im;im.w=im.h=4;im.pixels.assign(16,{70,100,150,255});
    auto info=lab::InspectAlpha(im);
    check(lab::ResolveAlpha(info,true,lab::AlphaPreview::Automatic)==lab::AlphaMode::Opaque,"opaque textures must retain depth writes");
    im.pixels[0].a=0;info=lab::InspectAlpha(im);
    check(lab::ResolveAlpha(info,true,lab::AlphaPreview::Automatic)==lab::AlphaMode::Mask,"binary alpha must use cutout rendering");
    im.pixels[1].a=128;info=lab::InspectAlpha(im);
    check(lab::ResolveAlpha(info,true,lab::AlphaPreview::Automatic)==lab::AlphaMode::Blend,"fractional alpha must use blending");
    check(lab::ResolveAlpha(info,false,lab::AlphaPreview::Blend)==lab::AlphaMode::Opaque,"alpha checkbox must override automatic and forced modes");
    check(lab::ResolveAlpha(info,true,lab::AlphaPreview::Mask)==lab::AlphaMode::Mask,"mask override must remain available for antialiased foliage");
    lab::GrayImage opacity;opacity.w=opacity.h=2;opacity.pixels={0,255,0,255};
    check(lab::SampleOpacity(opacity,.25f,.25f)==0&&lab::SampleOpacity(opacity,.75f,.25f)==1,"picking must distinguish holes and solid texels");
    check(std::fabs(lab::SampleOpacity(opacity,.5f,.5f)-.5f)<1e-6f,"picking must match bilinear alpha filtering");
    check(lab::SampleOpacity(opacity,1.25f,-.75f)==0,"alpha picking must wrap tiled UVs");
    auto bytes=lab::EncodeBC3DDS(im,{im});std::string error;auto decoded=lab::DecodeDDS(bytes,error);
    check(decoded.valid()&&decoded.pixels[0].a==0&&std::abs(int(decoded.pixels[1].a)-128)<=20,"DXT5 alpha must survive decoding");
    auto write=[&](size_t offset,uint32_t value){for(int i=0;i<4;++i)bytes[offset+size_t(i)]=uint8_t(value>>(i*8));};
    bytes.resize(136);write(84,0x31545844);bytes[128]=bytes[129]=0;bytes[130]=bytes[131]=255;write(132,0xffffffff);
    decoded=lab::DecodeDDS(bytes,error);check(decoded.valid()&&lab::InspectAlpha(decoded).invisible,"DXT1 transparent selector must produce holes");
    bytes.resize(144);write(84,0x33545844);for(size_t i=128;i<136;++i)bytes[i]=0x80;bytes[136]=bytes[137]=0;bytes[138]=bytes[139]=255;write(140,0);
    decoded=lab::DecodeDDS(bytes,error);check(decoded.valid()&&decoded.pixels[0].a==0&&decoded.pixels[1].a==136,"DXT3 explicit alpha must be retained");
}

}
int main(){
    try{cameraNavigation();framePortraitViewport();flightDirection();periodicUndoCoverage();screenSpaceStroke();diffuseAlpha();
        std::cout<<"PASS: camera, framing, 16 periodic undo cases, surface stroke sampling, diffuse alpha\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
