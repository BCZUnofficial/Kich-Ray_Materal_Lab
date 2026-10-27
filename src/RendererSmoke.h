#pragma once
#include "Renderer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>

namespace lab {
namespace renderer_smoke_detail {
inline bool SameCamera(const ViewportCamera& a,const ViewportCamera& b) {
    return a.pivot.x==b.pivot.x&&a.pivot.y==b.pivot.y&&a.pivot.z==b.pivot.z
        &&a.yaw==b.yaw&&a.pitch==b.pitch&&a.distance==b.distance&&a.fly==b.fly;
}
inline bool SameImage(const Image& a,const Image& b) {
    return a.valid()&&b.valid()&&a.w==b.w&&a.h==b.h
        &&std::memcmp(a.pixels.data(),b.pixels.data(),a.pixels.size()*sizeof(Pixel))==0;
}
inline PartImages Material(Pixel color) {
    PartImages result;result.diffuse.w=result.diffuse.h=4;result.diffuse.pixels.assign(16,color);return result;
}
inline Mesh Actor() {
    Mesh actor;MeshPart body;body.skeletal=true;
    // Native actor height is four units: Renderer normalizes it by exactly .5.
    // One inclined edge guarantees subpixel coverage in the 2x screenshot.
    body.vertices={{{-1,-2,0},{0,0,1},0,1},{{-.85f,2,0},{0,0,1},0,0},
                   {{1,2,0},{0,0,1},1,0},{{1,-2,0},{0,0,1},1,1}};
    for(auto& vertex:body.vertices){vertex.links=1;vertex.bone0=0;}
    body.indices={0,2,1,0,3,2};actor.parts.push_back(std::move(body));
    Bone root;root.name="bip01";root.hasBind=true;
    root.inverseBind[0]=root.inverseBind[5]=root.inverseBind[10]=root.inverseBind[15]=1;
    Bone hand=root;hand.name="bip01_r_hand";hand.parentName=root.name;hand.parent=0;
    hand.position=hand.localTranslate={1.6f,0,0};hand.inverseBind[12]=-1.6f;
    actor.bones={root,hand};actor.sourceParts=1;actor.skeletalSource=true;
    Motion motion;motion.name="smoke_hand_translation";motion.frames=2;motion.fps=1;
    motion.tracks.resize(2);motion.tracks[0].rotations={{}};motion.tracks[0].translations={{}};
    motion.tracks[1].rotations={{}};motion.tracks[1].translations={{1.6f,0,0},{1.6f,.8f,0}};
    actor.motions.push_back(std::move(motion));return actor;
}
inline Mesh Prop() {
    Mesh mesh;MeshPart part;
    // .8 by .2 native units, with an authored origin .45 units left of its
    // centre. A prop-centering bug is observable separately from a scale bug.
    part.vertices={{{.05f,-.1f,0},{0,0,1},0,1},{{.05f,.1f,0},{0,0,1},0,0},
                   {{.85f,.1f,0},{0,0,1},1,0},{{.85f,-.1f,0},{0,0,1},1,1}};
    part.indices={0,2,1,0,3,2};mesh.parts.push_back(std::move(part));mesh.sourceParts=1;return mesh;
}
struct Bounds {
    int left=std::numeric_limits<int>::max(),top=std::numeric_limits<int>::max(),right=-1,bottom=-1;
    bool valid()const{return right>=left&&bottom>=top;}
    float width()const{return valid()?float(right-left+1):0;}
    float height()const{return valid()?float(bottom-top+1):0;}
    float x()const{return (float(left)+right+1)*.5f;}
    float y()const{return (float(top)+bottom+1)*.5f;}
};
template<class Predicate>inline Bounds Find(const Image& image,Predicate predicate) {
    Bounds bounds;if(!image.valid())return bounds;
    for(uint32_t y=0;y<image.h;++y)for(uint32_t x=0;x<image.w;++x)
        if(predicate(image.pixels[size_t(y)*image.w+x])){
            bounds.left=std::min(bounds.left,int(x));bounds.top=std::min(bounds.top,int(y));
            bounds.right=std::max(bounds.right,int(x));bounds.bottom=std::max(bounds.bottom,int(y));
        }
    return bounds;
}
inline bool Red(Pixel p){return p.a>200&&p.r>180&&p.g<60&&p.b<60;}
inline bool Gray(Pixel p){return p.a>200&&p.r>=60&&p.r<=100&&std::abs(int(p.r)-p.g)<=1&&std::abs(int(p.r)-p.b)<=1;}
inline uint64_t Energy(const Image& image){uint64_t sum=0;for(auto p:image.pixels)sum+=unsigned(p.r)+p.g+p.b;return sum;}
inline bool SameAlpha(const Image& a,const Image& b) {
    if(!a.valid()||!b.valid()||a.w!=b.w||a.h!=b.h)return false;
    for(size_t i=0;i<a.pixels.size();++i)if(a.pixels[i].a!=b.pixels[i].a)return false;return true;
}
}

// Runs against the real D3D11 renderer and runtime-compiled Preview.hlsl.
// Synthetic geometry needs no redistributed game assets. The caller may replace
// this test scene afterwards; this function never writes application settings.
inline void RunRendererFeatureSmoke(Renderer& renderer,const std::function<void(bool,const char*)>& check) {
    using namespace renderer_smoke_detail;
    auto require=[&](bool condition,const char* message){check(condition,message);return condition;};
    std::wstring error;
    if(!require(renderer.ResizeScene(320,240,error),"R4 feature smoke viewport uploads"))return;
    renderer.FlyCameraEnabled(true);renderer.OrbitCamera(17,-8);renderer.MoveCamera(.2f,-.1f,.15f);
    renderer.SetLightOrbitDegrees(137,-23);renderer.SetColor(1,RGB(31,201,177));renderer.SetLightIntensity(.75f);
    const auto initialCamera=renderer.CameraState();const float azimuth=renderer.LightAzimuthDegrees(),elevation=renderer.LightElevationDegrees();
    const auto lightColor=renderer.GetColor(1);
    Mesh actor=Actor();
    if(!require(renderer.SetMesh(actor,error),"synthetic biped actor uploads"))return;
    check(SameCamera(initialCamera,renderer.CameraState()),"opening model preserves free-flight camera state");
    check(renderer.LightAzimuthDegrees()==azimuth&&renderer.LightElevationDegrees()==elevation&&renderer.GetColor(1)==lightColor&&renderer.LightIntensity()==.75f,"opening model preserves scene light direction, color and intensity");
    check(renderer.BoneCount()==2&&renderer.MotionCount()==1,"synthetic actor exposes its own bones and clip");
    renderer.ResetView();renderer.AlignAxis(2);renderer.ClearSelection();renderer.ClearHover();renderer.ClearBrushCursor();
    renderer.OutlineEnabled(false);renderer.SkeletonEnabled(false);renderer.WireframeEnabled(false);
    renderer.AlphaEnabled(true);renderer.SetAlphaPreview(AlphaPreview::Automatic);renderer.SetAlphaCutoff(.5f);
    renderer.BumpEnabled(true);renderer.SetNormalSettings(1,false);renderer.SetGlossStrength(.5f);renderer.GlossEnabled(false);renderer.SetMaterialClass(-1);
    renderer.SetColor(0,RGB(255,255,255));renderer.SetColor(2,RGB(255,255,255));renderer.SetLightIntensity(0);
    renderer.SetEnvironmentLighting({},1,1,1);const float blackBackground[3]={0,0,0};renderer.SetBackground(blackBackground);
    const auto gray=Material({80,80,80,255});
    if(!require(renderer.SetPartImages(0,gray),"actor material uploads"))return;
    Image baseline;
    if(!require(renderer.CaptureFrame(baseline),"actor reference frame reads back"))return;
    const auto modelCamera=renderer.CameraState();const auto modelColor=renderer.GetColor(2);
    Mesh reload=Actor();
    if(!require(renderer.SetMesh(reload,error)&&renderer.SetPartImages(0,gray),"second actor load uploads"))return;
    Image reloaded;if(!require(renderer.CaptureFrame(reloaded),"reloaded actor reads back"))return;
    check(SameCamera(modelCamera,renderer.CameraState())&&renderer.GetColor(2)==modelColor&&SameImage(baseline,reloaded),"model reload preserves camera, ambient and rendered lighting");

    Image clean;
    auto* originalSurface=renderer.SceneTexture();const UINT originalWidth=renderer.SceneWidth(),originalHeight=renderer.SceneHeight();
    const auto captureCamera=renderer.CameraState();
    if(!require(renderer.CaptureTransparent(clean,320,error),"clean transparent screenshot reads back"))return;
    check(clean.w==320&&clean.h==240&&renderer.SceneTexture()==originalSurface&&renderer.SceneWidth()==originalWidth&&renderer.SceneHeight()==originalHeight&&SameCamera(captureCamera,renderer.CameraState()),"screenshot preserves live surface, dimensions and camera");
    bool transparent=false,opaque=false,edge=false;for(auto p:clean.pixels){transparent|=p.a==0;opaque|=p.a==255;edge|=p.a>0&&p.a<255;}
    check(transparent&&opaque&&edge,"clean screenshot has transparent background, solid model and antialiased coverage");
    const auto bodyBounds=Find(clean,Gray);
    if(!require(bodyBounds.valid(),"actor color fixture is visible"))return;
    const auto points=renderer.BonePoints();
    if(!require(points.size()==2&&points[1].visible&&points[1].name=="bip01_r_hand","biped point projects into scene pixels"))return;

    // Enable real scene guides, then prove capture excludes all of them and
    // leaves their viewport state intact after returning.
    renderer.SelectPart(0);renderer.OutlineEnabled(true);renderer.SkeletonEnabled(true);renderer.WireframeEnabled(true);
    Renderer::PickHit brush;
    if(renderer.PickUV(160,120,brush))renderer.SetBrushCursor(brush,.18f,.18f);
    Image guideFrame,guides,guideFrameAfter;
    if(!require(renderer.CaptureFrame(guideFrame)&&renderer.CaptureTransparent(guides,320,error)&&renderer.CaptureFrame(guideFrameAfter),"guide-enabled capture reads back"))return;
    check(!SameImage(baseline,guideFrame),"wireframe and skeleton fixture visibly changes live viewport");
    check(SameImage(clean,guides),"transparent screenshot excludes skeleton, outline, wireframe and brush cursor");
    check(SameImage(guideFrame,guideFrameAfter),"capture preserves enabled viewport guides");
    renderer.ClearSelection();renderer.ClearBrushCursor();renderer.SkeletonEnabled(false);renderer.OutlineEnabled(false);renderer.WireframeEnabled(false);renderer.SetHoverPart(0);
    Image hovered;if(!require(renderer.CaptureTransparent(hovered,320,error),"hovered clean capture reads back"))return;
    check(SameImage(clean,hovered),"screenshot excludes mesh hover tint");renderer.ClearHover();

    Mesh prop=Prop();const auto red=Material({230,25,25,255});AttachmentTransform transform;
    const size_t editableParts=renderer.PartCount();
    if(!require(renderer.SetAttachment(1,prop,{red},transform,error),"native-unit preview prop attaches to hand bone"))return;
    Image attached;if(!require(renderer.CaptureTransparent(attached,320,error),"attached prop capture reads back"))return;
    const auto propBounds=Find(attached,Red);
    if(!require(propBounds.valid(),"attached red prop is visible"))return;
    check(renderer.HasAttachment(1)&&renderer.PartCount()==editableParts&&!SameImage(clean,attached),"prop changes rendering without adding an editable mesh part");
    check(std::fabs(propBounds.width()/bodyBounds.height()-.2f)<.025f&&propBounds.width()>propBounds.height()*3,"prop keeps authored .8 by .2 units relative to four-unit actor");
    check(std::fabs(std::fabs(propBounds.x()-points[1].x)-bodyBounds.height()*(.45f/4))<2.5f&&std::fabs(propBounds.y()-points[1].y)<1.5f,"prop preserves authored origin instead of recentering itself");
    Renderer::PickHit propHit;
    check(!renderer.PickUV(int(propBounds.x()),int(propBounds.y()),propHit),"visible prop is excluded from material picking");
    check(renderer.PickUV(160,120,propHit)&&propHit.part==0,"base actor remains independently pickable");

    transform.rotation.z=90;
    if(!require(renderer.SetAttachmentTransform(1,transform,error),"prop local rotation updates"))return;
    Image rotated;if(!require(renderer.CaptureTransparent(rotated,320,error),"rotated prop reads back"))return;
    const auto rotatedBounds=Find(rotated,Red);
    check(rotatedBounds.valid()&&rotatedBounds.height()>rotatedBounds.width()*3&&std::fabs(rotatedBounds.height()-propBounds.width())<3,"90-degree local rotation rotates authored prop geometry");
    check(rotatedBounds.valid()&&std::fabs(rotatedBounds.x()-points[1].x)<1.5f&&std::fabs(points[1].y-rotatedBounds.y()-bodyBounds.height()*(.45f/4))<2.5f,"local rotation pivots about authored origin at the bone");
    transform={};transform.position={.3f,.4f,0};
    if(!require(renderer.SetAttachmentTransform(1,transform,error),"prop local offset updates"))return;
    Image shifted;if(!require(renderer.CaptureTransparent(shifted,320,error),"offset prop reads back"))return;
    const auto shiftedBounds=Find(shifted,Red);
    check(shiftedBounds.valid()&&std::fabs(std::fabs(shiftedBounds.x()-propBounds.x())-bodyBounds.height()*(.3f/4))<2.5f&&std::fabs(propBounds.y()-shiftedBounds.y()-bodyBounds.height()*(.4f/4))<2.5f,"prop offset uses native OGF units before actor normalization");
    transform={};transform.scale=2;
    if(!require(renderer.SetAttachmentTransform(1,transform,error),"prop scale updates"))return;
    Image scaled;if(!require(renderer.CaptureTransparent(scaled,320,error),"scaled prop reads back"))return;
    const auto scaledBounds=Find(scaled,Red);
    check(scaledBounds.valid()&&std::fabs(scaledBounds.width()-2*propBounds.width())<3,"explicit prop scale doubles authored dimensions");
    AttachmentTransform invalid=transform;invalid.scale=0;
    check(!renderer.SetAttachmentTransform(1,invalid,error),"invalid prop transform is rejected");
    Image afterInvalid;if(!require(renderer.CaptureTransparent(afterInvalid,320,error),"prop after invalid transform reads back"))return;
    check(SameImage(scaled,afterInvalid),"invalid prop transform leaves visible prop unchanged");

    transform={};if(!require(renderer.SetAttachmentTransform(1,transform,error)&&renderer.SetAnimationPose(0,0,false,error),"actor animation starts at bind pose"))return;
    Image pose0;if(!require(renderer.CaptureTransparent(pose0,320,error),"first attachment pose reads back"))return;
    const auto firstPoints=renderer.BonePoints();const auto firstProp=Find(pose0,Red);
    if(!require(renderer.SetAnimationPose(0,1,false,error),"hand animation advances"))return;
    Image pose1;if(!require(renderer.CaptureTransparent(pose1,320,error),"animated attachment pose reads back"))return;
    const auto lastPoints=renderer.BonePoints();const auto lastProp=Find(pose1,Red);
    check(firstPoints.size()==2&&lastPoints.size()==2&&firstProp.valid()&&lastProp.valid()&&!SameImage(pose0,pose1)&&std::fabs((lastProp.y()-firstProp.y())-(lastPoints[1].y-firstPoints[1].y))<2&&lastPoints[1].y<firstPoints[1].y-10,"prop follows animated bone and its clickable projected point");
    renderer.ClearAttachments();Image detached;
    if(!require(renderer.CaptureTransparent(detached,320,error),"detached actor reads back"))return;
    check(!renderer.HasAttachment(1)&&renderer.PartCount()==editableParts&&SameImage(clean,detached),"leaving attachment preview unloads props and restores actor image");

    if(!require(renderer.ResizeScene(1600,900,error),"large scene fixture resizes"))return;
    auto* largeSurface=renderer.SceneTexture();const auto largeCamera=renderer.CameraState();Image capped;
    if(!require(renderer.CaptureTransparent(capped,9999,error),"oversized screenshot request reads back"))return;
    check(capped.w==1024&&capped.h==576,"transparent screenshot enforces 1024 cap and preserves 16:9 aspect");
    check(renderer.SceneTexture()==largeSurface&&renderer.SceneWidth()==1600&&renderer.SceneHeight()==900&&SameCamera(largeCamera,renderer.CameraState()),"capped screenshot leaves large live viewport untouched");
    check(capped.pixels.front().a==0&&capped.pixels.front().r==0&&capped.pixels.front().g==0&&capped.pixels.front().b==0,"offscreen background has canonical transparent RGBA");
    if(!require(renderer.ResizeScene(320,240,error),"feature smoke viewport restores size"))return;
    if(!require(renderer.SetPartImages(0,Material({0,0,0,255})),"opaque black screenshot fixture uploads"))return;
    Image black;if(!require(renderer.CaptureTransparent(black,320,error),"opaque black model reads back"))return;
    const auto centre=size_t(black.h/2)*black.w+black.w/2;
    check(black.pixels[centre].r==0&&black.pixels[centre].a==255,"black material stays opaque in transparent PNG output");
    if(!require(renderer.SetPartImages(0,Material({220,25,25,128})),"translucent screenshot fixture uploads"))return;
    Image translucent;if(!require(renderer.CaptureTransparent(translucent,320,error),"translucent material reads back"))return;
    const auto glass=translucent.pixels[size_t(translucent.h/2)*translucent.w+translucent.w/2];
    check(std::abs(int(glass.a)-128)<=1&&std::abs(int(glass.r)-220)<=2&&std::abs(int(glass.g)-25)<=2,"GPU translucent capture resolves to straight alpha without darkening");

    if(!require(renderer.SetPartImages(0,gray),"R4 indirect-light fixture uploads"))return;
    renderer.SetColor(2,RGB(0,0,0));renderer.SetLightIntensity(0);renderer.SetEnvironmentLighting({},1,1,1);renderer.GlossEnabled(false);
    Image dark;if(!require(renderer.CaptureTransparent(dark,320,error),"zero-energy scene reads back"))return;
    check(Energy(dark)==0,"zero direct, ambient and environment produces zero model radiance");
    renderer.SetEnvironmentLighting({.6f,.6f,.6f},1,1,1);Image hemisphere;
    if(!require(renderer.CaptureTransparent(hemisphere,320,error),"R4 hemisphere scene reads back"))return;
    check(Energy(hemisphere)>1000&&SameAlpha(dark,hemisphere),"R4 hmodel environment lights actual model without changing coverage");
    renderer.SetEnvironmentLighting({.6f,.6f,.6f},0,1,1);Image noHemi;
    if(!require(renderer.CaptureTransparent(noHemi,320,error),"zero hemisphere fixture reads back"))return;
    check(Energy(noHemi)==0,"zero hemi visibility removes uniform environment diffuse");
    renderer.SetEnvironmentLighting({.6f,.6f,.6f},1,0,1);Image zeroExposure;
    if(!require(renderer.CaptureTransparent(zeroExposure,320,error),"zero exposure fixture reads back"))return;
    check(Energy(zeroExposure)==0&&SameAlpha(hemisphere,zeroExposure),"R4 exposure scales model radiance while preserving screenshot alpha");
    renderer.SetEnvironmentLighting({.6f,.6f,.6f},.25f,1,1);renderer.SetPartMaterialClass(0,0);
    Image oren;if(!require(renderer.CaptureTransparent(oren,320,error),"R4 Oren-Nayar material reads back"))return;
    renderer.SetPartMaterialClass(0,3);Image metal;
    if(!require(renderer.CaptureTransparent(metal,320,error),"R4 metal material reads back"))return;
    renderer.SetPartMaterialClass(0,4);Image clampedMetal;
    if(!require(renderer.CaptureTransparent(clampedMetal,320,error),"R4 clamped material reads back"))return;
    check(!SameImage(oren,metal)&&SameImage(metal,clampedMetal),"R4 class four clamps to metal instead of wrapping to class zero");

    // Fixed expected values come from upstream R4 SpecularIntensity and its
    // forced LUT endpoint: .125^(2/3)=.25, and an R=128 bump gloss is squared.
    // Black albedo removes the diffuse contribution, so a pixel measures only
    // the real compiled shader's specular response, including map binding.
    PartImages glossy=Material({0,0,0,255});glossy.hasBump=true;
    glossy.bump.w=glossy.bump.h=glossy.bumpX.w=glossy.bumpX.h=4;
    glossy.bump.pixels.assign(16,Pixel{255,255,128,128});glossy.bumpX.pixels.assign(16,Pixel{128,128,128,128});
    if(!require(renderer.SetPartImages(0,glossy),"R4 controlled specular material uploads"))return;
    renderer.SetMaterialClass(1);renderer.GlossEnabled(true);renderer.BumpEnabled(false);
    renderer.SetColor(1,RGB(255,255,255));renderer.SetLightOrbitDegrees(0,0);renderer.SetLightIntensity(.125f);renderer.SetEnvironmentLighting({},1,1,1);
    Image specular;if(!require(renderer.CaptureTransparent(specular,320,error),"R4 controlled specular reads back"))return;
    const auto pixel=size_t(specular.h/2)*specular.w+specular.w/2;
    check(std::abs(int(specular.pixels[pixel].r)-64)<=1,"R4 sub-unit light uses two-thirds specular power even with bump preview disabled");
    renderer.SetEnvironmentLighting({},1,1,2);Image twiceGloss;
    if(!require(renderer.CaptureTransparent(twiceGloss,320,error),"R4 gloss factor reads back"))return;
    check(std::abs(int(twiceGloss.pixels[pixel].r)-128)<=1,"R4 gloss factor scales actual direct specular");
    glossy.bump.pixels.assign(16,Pixel{128,255,128,128});
    if(!require(renderer.SetPartImages(0,glossy),"R4 half-encoded gloss uploads"))return;
    renderer.SetEnvironmentLighting({},1,1,1);Image squaredGloss;
    if(!require(renderer.CaptureTransparent(squaredGloss,320,error),"R4 half-encoded gloss reads back"))return;
    check(std::abs(int(squaredGloss.pixels[pixel].r)-16)<=1,"R4 squares packed bump gloss and retains GBuffer precision");
    renderer.SetEnvironmentLighting({},1,1,0);Image disabledSpecular;
    if(!require(renderer.CaptureTransparent(disabledSpecular,320,error),"R4 zero gloss factor reads back"))return;
    check(Energy(disabledSpecular)==0,"zero R4 gloss factor removes direct highlight without artificial glow");
    renderer.SetEnvironmentLighting({},1,1,1);renderer.SetColor(2,RGB(255,255,255));renderer.SetLightIntensity(0);
    renderer.GlossEnabled(true);renderer.BumpEnabled(true);renderer.SetMaterialClass(-1);renderer.SetBindPose();
}
}
