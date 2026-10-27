#include "AttachmentPreview.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool condition,const char* what){if(!condition)throw std::runtime_error(what);}
}
int main(){
    using namespace lab;
    try{
        // Export has a hard 1024 cap even if a caller sends a larger request.
        for(const auto& viewport:{std::pair<uint32_t,uint32_t>{1537,999},{999,1537},{4096,2160},{1,4096},{UINT32_MAX,UINT32_MAX}}){
            const auto size=PreviewCaptureSize(viewport.first,viewport.second,9999);
            check(size.first&&size.second&&std::max(size.first,size.second)<=1024,"screenshot cap");
            const double ratio=double(viewport.first)/viewport.second;
            check(std::fabs(double(size.first)-size.second*ratio)<=std::max(1.0,ratio),"screenshot keeps aspect within one rounding pixel");
        }
        check(PreviewCaptureSize(96,64,1024)==std::make_pair(96u,64u),"small captures are not enlarged");
        check(PreviewCaptureSize(10,20,0)==std::make_pair(1u,1u),"zero requested limit remains finite");
        check(PreviewCaptureSize(0,20,1024)==std::make_pair(0u,0u),"empty viewport is rejected");

        // Red opaque subpixel + three transparent subpixels: the saved edge
        // must remain RED at alpha 64, not dark red (premultiplication fringe).
        Image subpixel;subpixel.w=subpixel.h=2;
        subpixel.pixels={{255,0,0,255},{0,0,0,0},{0,0,0,0},{0,0,0,0}};
        const auto red=ResolvePreviewCapture(subpixel,1,1);
        check(red.valid()&&red.pixels[0].r==255&&red.pixels[0].g==0&&red.pixels[0].a==64,"straight-alpha silhouette has no black fringe");
        subpixel.pixels.assign(4,Pixel{32,64,96,128});
        const auto glass=ResolvePreviewCapture(subpixel,1,1);
        check(glass.pixels[0].r==64&&glass.pixels[0].g==128&&glass.pixels[0].b==191&&glass.pixels[0].a==128,"translucent material is unpremultiplied once");
        subpixel.pixels.assign(4,Pixel{0,0,0,255});
        const auto black=ResolvePreviewCapture(subpixel,1,1);
        check(black.pixels[0].r==0&&black.pixels[0].a==255,"black model pixels remain opaque");
        subpixel.pixels.assign(4,Pixel{25,45,65,0});
        const auto empty=ResolvePreviewCapture(subpixel,1,1);
        check(empty.pixels[0].r==0&&empty.pixels[0].g==0&&empty.pixels[0].b==0&&empty.pixels[0].a==0,"transparent background is canonical RGBA zero");
        check(!ResolvePreviewCapture(subpixel,3,1).valid(),"nonintegral resolve is rejected");

        AttachmentTransform transform;check(ValidAttachmentTransform(transform),"native prop scale is accepted");
        transform.scale=0;check(!ValidAttachmentTransform(transform),"collapsed attachment transform rejected");
        transform.scale=1;transform.rotation.y=std::numeric_limits<float>::quiet_NaN();
        check(!ValidAttachmentTransform(transform),"nonfinite attachment rotation rejected");
        transform={};transform.position.x=1e7f;check(!ValidAttachmentTransform(transform),"huge finite translation rejected");
        transform={};transform.scale=1000;check(ValidAttachmentTransform(transform),"maximum UI scale accepted");
        transform.scale=1001;check(!ValidAttachmentTransform(transform),"above-maximum scale rejected");
        std::cout<<"Renderer preview checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
