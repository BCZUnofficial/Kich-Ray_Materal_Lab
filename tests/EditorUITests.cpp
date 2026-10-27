#include "EditorUI.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value,const std::string& message) { if(!value)throw std::runtime_error(message); }
struct UIContext {
    ImVector<ImWchar> glyphRanges;
    explicit UIContext(float scale) {
        IMGUI_CHECKVERSION();ImGui::CreateContext();
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DeltaTime=1.f/60;
        io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;
#ifdef _WIN32
        const char* font="C:/Windows/Fonts/segoeui.ttf";
#else
        const char* font="/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif
        ImFontGlyphRangesBuilder glyphs;glyphs.AddRanges(io.Fonts->GetGlyphRangesCyrillic());glyphs.AddText("…−—");glyphs.BuildRanges(&glyphRanges);
        if(!io.Fonts->AddFontFromFileTTF(font,16.f*scale,nullptr,glyphRanges.Data))io.Fonts->AddFontDefault();
        unsigned char* pixels=nullptr;int w=0,h=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);io.Fonts->SetTexID(1);
        lab::ConfigureEditorUIStyle(scale);
    }
    ~UIContext(){ImGui::DestroyContext();}
};
lab::EditorUIState fixture(float scale) {
    lab::EditorUIState s;s.uiScale=scale;s.canBrush=true;s.canAdjust=true;s.canUndo=true;s.canRedo=true;s.hasSkeleton=true;s.hasDiffuse=true;
    s.modelName="act_bandit.ogf";s.status="Материал загружен. Кисть готова к работе.";
    s.sourceNames={"act_bandit.dds","","","","act_bandit_bump.dds","act_bandit_bump#.dds"};
    s.documentInfo="1024 × 1024 · исходная пара ТЧ";
    s.animations={"idle", "walk", "run"};s.animationFrames=120;
    for(int i=0;i<48;++i)s.meshes.push_back({"Mesh "+std::to_string(i+1)+"  act_bandit",true,i==0,true,true});
    return s;
}
void frame(lab::EditorUIState& s,int width,int height,const lab::EditorUICallback& callback=[](lab::EditorAction,int){}) {
    auto& io=ImGui::GetIO();io.DisplaySize=ImVec2(float(width),float(height));
    ImGui::NewFrame();lab::DrawEditorUI(s,callback);ImGui::Render();
}
bool inside(const lab::EditorRect& r,int w,int h) {return r.w>0&&r.h>0&&r.x>=0&&r.y>=0&&r.x+r.w<=w+.5f&&r.y+r.h<=h+.5f;}
void validate(const lab::EditorUIState& s,int w,int h) {
    require(s.renderedPaneCount==1,"exactly one inspector pane must be rendered");
    require(inside(s.inspectorRect,w,h),"inspector exceeds client area");
    require(inside(s.viewportRect,w,h),"viewport exceeds client area");
    require(inside(s.toolbarRect,w,h),"toolbar exceeds client area");
    require(s.viewportRect.x>=s.inspectorRect.x+s.inspectorRect.w,"viewport overlaps inspector");
    require(s.toolbarRect.y>=s.viewportRect.y+s.viewportRect.h,"toolbar overlaps the viewport");
    auto* data=ImGui::GetDrawData();require(data&&data->Valid&&data->TotalVtxCount>0,"frame produced no draw data");
    for(int li=0;li<data->CmdListsCount;++li)for(const auto& command:data->CmdLists[li]->CmdBuffer){
        require(std::isfinite(command.ClipRect.x)&&std::isfinite(command.ClipRect.w),"non-finite scissor rectangle");
        require(command.ClipRect.z>=command.ClipRect.x&&command.ClipRect.w>=command.ClipRect.y,"invalid scissor rectangle");
    }
}
void interactions() {
    UIContext context(1.f);auto s=fixture(1.f);
    const ImGuiStyle initialStyle=ImGui::GetStyle();
    for(float scale:{1.5f,2.f,1.25f,1.f})lab::ConfigureEditorUIStyle(scale);
    require(ImGui::GetStyle().ScrollbarSize==initialStyle.ScrollbarSize&&ImGui::GetStyle().FramePadding.x==initialStyle.FramePadding.x&&ImGui::GetStyle().GrabMinSize==initialStyle.GrabMinSize,"DPI changes must not accumulate style scaling");
    for(int i=0;i<3;++i)frame(s,1490,860);
    auto& io=ImGui::GetIO();
    for(ImWchar glyph : {ImWchar(0x2026),ImWchar(0x2212),ImWchar(0x2014)})
        require(ImGui::GetFont()->FindGlyphNoFallback(glyph)!=nullptr,"interface punctuation missing from font atlas");
    std::vector<lab::EditorAction> actions;
    auto callback=[&](lab::EditorAction action,int arg){actions.push_back(action);if(action==lab::EditorAction::SetMode)s.mode=lab::EditorMode(arg);};
    const float button=std::max(30.f,ImGui::CalcTextSize("WW").x+ImGui::GetStyle().FramePadding.x*2);
    auto click=[&](float x,float y){
        io.AddMousePosEvent(x,y);frame(s,1490,860,callback);
        io.AddMouseButtonEvent(0,true);frame(s,1490,860,callback);
        io.AddMouseButtonEvent(0,false);frame(s,1490,860,callback);
        validate(s,1490,860);
    };
    click(s.toolbarRect.x+5*(button+ImGui::GetStyle().ItemSpacing.x)+button*.5f,s.toolbarRect.y+ImGui::GetFrameHeight()*.5f);
    require(actions.size()==1&&actions.back()==lab::EditorAction::SetMode&&s.mode==lab::EditorMode::Brush,"BR toolbar click must dispatch once");
    actions.clear();s.canBrush=false;frame(s,1490,860,callback);
    click(s.toolbarRect.x+5*(button+ImGui::GetStyle().ItemSpacing.x)+button*.5f,s.toolbarRect.y+ImGui::GetFrameHeight()*.5f);
    require(actions.empty(),"disabled brush tool dispatched an action");
    s.tab=lab::EditorTab::Scene;s.forceTab=true;for(int i=0;i<4;++i)frame(s,1490,860);
    auto activePane=[]()->ImGuiWindow*{for(auto* w:GImGui->Windows)if(w->LastFrameActive==GImGui->FrameCount&&std::string(w->Name).find("Active pane")!=std::string::npos)return w;return nullptr;};
    auto* scene=activePane();require(scene&&scene->ScrollMax.y>0,"long scene list must scroll");
    io.AddMousePosEvent(scene->Pos.x+40,scene->Pos.y+100);frame(s,1490,860);
    io.AddMouseWheelEvent(0,-7);frame(s,1490,860);frame(s,1490,860);validate(s,1490,860);
    require(scene->Scroll.y>0,"mouse wheel did not scroll scene pane");
    const float sceneScroll=scene->Scroll.y;
    s.tab=lab::EditorTab::Brush;s.forceTab=true;for(int i=0;i<4;++i)frame(s,1490,860);validate(s,1490,860);
    auto* brush=activePane();require(brush&&brush!=scene&&brush->Scroll.y==0,"tab scroll state leaked into brush pane");
    s.tab=lab::EditorTab::Scene;s.forceTab=true;for(int i=0;i<4;++i)frame(s,1490,860);validate(s,1490,860);
    require(scene->Scroll.y==sceneScroll,"scene scroll position lost after tab switch");
    s.requestGeneratePopup=true;for(int i=0;i<4;++i)frame(s,1100,640);
    auto* popup=ImGui::FindWindowByName("Создать новую базу");
    require(popup&&popup->Active&&popup->Pos.x>=0&&popup->Pos.y>=0&&popup->Pos.x+popup->Size.x<=1100&&popup->Pos.y+popup->Size.y<=640,"generation popup must fit small windows with many targets");
}
void wheelAndAnimationInteractions() {
    UIContext context(1.f);auto s=fixture(1.f);auto& io=ImGui::GetIO();
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    s.tab=lab::EditorTab::Material;s.forceTab=true;s.pendingAdjustments=true;
    std::vector<lab::EditorAction> actions;
    auto callback=[&](lab::EditorAction kind,int){actions.push_back(kind);};
    auto step=[&]{frame(s,1490,860,callback);};for(int i=0;i<4;++i)step();
    auto pane=[](){for(auto* w:GImGui->Windows)if(w->LastFrameActive==GImGui->FrameCount&&std::string(w->Name).find("Active pane")!=std::string::npos)return w;return static_cast<ImGuiWindow*>(nullptr);};
    auto hover=[&](const char* label){auto* w=pane();require(w!=nullptr,"active inspector exists");const auto id=w->GetID(label);
        for(float y=w->Pos.y+10;y<w->Pos.y+w->Size.y-10;y+=4){io.AddMousePosEvent(w->Pos.x+70,y);step();if(GImGui->HoveredId==id)return ImVec2(w->Pos.x+70,y);}
        throw std::runtime_error(std::string("cannot find interactive control: ")+label);
    };
    hover("Сила нормалей");const float scroll=pane()->Scroll.y,before=s.adjustments.normalStrength;actions.clear();
    io.AddMouseWheelEvent(0,1);step();step();
    require(s.adjustments.normalStrength>before&&std::count(actions.begin(),actions.end(),lab::EditorAction::AdjustMaterial)==1,"wheel must alter normal slider and dispatch one live edit");
    require(pane()->Scroll.y==scroll,"wheel over slider must not also scroll inspector");
    io.AddMouseButtonEvent(0,true);step();io.AddMouseButtonEvent(0,false);step();const float afterClick=s.adjustments.normalStrength;
    io.AddKeyEvent(ImGuiKey_RightArrow,true);step();io.AddKeyEvent(ImGuiKey_RightArrow,false);step();
    require(s.adjustments.normalStrength==afterClick,"arrow keys must not adjust mouse-only slider");
    hover("Применить  Ctrl+S");actions.clear();io.AddMouseButtonEvent(0,true);step();io.AddMouseButtonEvent(0,false);step();
    require(std::count(actions.begin(),actions.end(),lab::EditorAction::ApplyAdjustments)==1,"apply button dispatches once for pending draft");
    hover("Сила нормалей");s.canAdjust=false;step();const float disabled=s.adjustments.normalStrength;actions.clear();io.AddMouseWheelEvent(0,1);step();
    require(s.adjustments.normalStrength==disabled&&actions.empty(),"disabled slider must ignore wheel");
    s.tab=lab::EditorTab::Animation;s.forceTab=true;s.animations.clear();for(int i=0;i<160;++i)s.animations.push_back("animation_"+std::to_string(i));s.animationIndex=125;
    for(int i=0;i<4;++i)step();hover("Клип");io.AddMouseButtonEvent(0,true);step();io.AddMouseButtonEvent(0,false);step();for(int i=0;i<3;++i)step();
    ImGuiWindow* popup=nullptr;for(auto* w:GImGui->Windows)if(w->Active&&(w->Flags&ImGuiWindowFlags_Popup))popup=w;
    require(popup&&popup->ScrollMax.y>0&&popup->Scroll.y>0&&s.animationIndex==125,"long animation popup must open scrolled to last selected clip");
    const float opened=popup->Scroll.y;io.AddMousePosEvent(popup->Pos.x+40,popup->Pos.y+50);step();io.AddMouseWheelEvent(0,-2);step();step();
    require(popup->Scroll.y>opened&&s.animationIndex==125,"animation list wheel scroll must preserve selection");
}
void surfaceReliefInteractions() {
    UIContext context(1.f);auto s=fixture(1.f);auto& io=ImGui::GetIO();
    s.tab=lab::EditorTab::Brush;s.forceTab=true;s.brushChannel=2;s.brushRemove=true;
    std::vector<std::pair<lab::EditorAction,int>> actions;
    auto callback=[&](lab::EditorAction action,int value){actions.emplace_back(action,value);
        if(action==lab::EditorAction::SetBrushTool)s.brushTool=lab::BrushTool(value);
        if(action==lab::EditorAction::SurfacePreset)s.surfacePreset=value;
    };
    auto step=[&]{frame(s,1490,1000,callback);};for(int i=0;i<4;++i)step();
    auto pane=[](){for(auto* w:GImGui->Windows)if(w->LastFrameActive==GImGui->FrameCount&&std::string(w->Name).find("Active pane")!=std::string::npos)return w;return static_cast<ImGuiWindow*>(nullptr);};
    auto* w=pane();require(w!=nullptr,"brush pane exists");
    auto* tabs=GImGui->TabBars.GetByKey(w->GetID("Brush tools"));require(tabs&&tabs->Tabs.Size==2,"brush has two independent tool tabs");
    ImGuiTabItem* surface=nullptr;
    for(auto& tab:tabs->Tabs)if(std::string(ImGui::TabBarGetTabName(tabs,&tab))=="Нанесение рельефа поверхности")surface=&tab;
    require(surface!=nullptr,"dedicated surface relief tab is named explicitly");
    const ImVec2 tabPoint(tabs->BarRect.Min.x+surface->Offset+surface->Width*.5f,tabs->BarRect.GetCenter().y);
    io.AddMousePosEvent(tabPoint.x,tabPoint.y);step();io.AddMouseButtonEvent(0,true);step();io.AddMouseButtonEvent(0,false);step();step();
    require(s.brushTool==lab::BrushTool::SurfaceRelief,"click switches into surface relief tab");
    require(s.brushChannel==2&&s.brushRemove,"surface tab preserves independent freehand channel and remove settings");
    auto hover=[&](const char* label){const auto id=ImHashStr(label,0,surface->ID);
        for(float y=w->Pos.y+10;y<w->Pos.y+w->Size.y-10;y+=4){io.AddMousePosEvent(w->Pos.x+70,y);step();if(GImGui->HoveredId==id)return;}
        throw std::runtime_error(std::string("missing surface control: ")+label);
    };
    hover("Кожа");actions.clear();io.AddMouseButtonEvent(0,true);step();io.AddMouseButtonEvent(0,false);step();
    require(actions.size()==1&&actions[0]==std::make_pair(lab::EditorAction::SurfacePreset,4),"one mouse click selects one surface preset");
    require(s.brushChannel==2&&s.brushRemove,"preset selection cannot change freehand operation");
    s.hasDiffuse=false;step();actions.clear();io.AddMouseButtonEvent(0,true);step();io.AddMouseButtonEvent(0,false);step();
    require(actions.empty(),"surface presets require a diffuse source");
    s.hasDiffuse=true;s.brushTool=lab::BrushTool::Freehand;s.forceBrushTab=true;for(int i=0;i<4;++i)step();
    require(s.brushTool==lab::BrushTool::Freehand&&s.brushChannel==2&&s.brushRemove,"return to freehand restores its untouched settings");
}
void diffuseHeightControls() {
    UIContext context(1.f);auto s=fixture(1.f);auto& io=ImGui::GetIO();
    s.tab=lab::EditorTab::Material;s.forceTab=true;
    std::vector<lab::EditorAction> actions;
    auto step=[&]{frame(s,1490,1100,[&](lab::EditorAction kind,int){actions.push_back(kind);});};
    for(int i=0;i<4;++i)step();
    auto pane=[](){for(auto* w:GImGui->Windows)if(w->LastFrameActive==GImGui->FrameCount&&std::string(w->Name).find("Active pane")!=std::string::npos)return w;return static_cast<ImGuiWindow*>(nullptr);};
    auto hover=[&](const char* label){auto* w=pane();require(w!=nullptr,"material inspector exists");const auto id=w->GetID(label);
        for(float y=w->Pos.y+10;y<w->Pos.y+w->Size.y-10;y+=3){io.AddMousePosEvent(w->Pos.x+70,y);step();if(GImGui->HoveredId==id)return ImVec2(w->Pos.x+70,y);}
        throw std::runtime_error(std::string("missing height control: ")+label);
    };
    auto click=[&](ImVec2 p){io.AddMousePosEvent(p.x,p.y);step();io.AddMouseButtonEvent(0,true);step();io.AddMouseButtonEvent(0,false);step();};
    const auto checkbox=hover("Поднять по базовой текстуре");
    s.hasDiffuse=false;step();actions.clear();click(checkbox);
    require(!s.adjustments.heightFromDiffuse&&actions.empty(),"height-from-diffuse checkbox must be disabled when the selected material has no diffuse");
    s.hasDiffuse=true;step();actions.clear();click(checkbox);
    require(s.adjustments.heightFromDiffuse&&std::count(actions.begin(),actions.end(),lab::EditorAction::AdjustMaterial)==1,
            "enabling diffuse height must request exactly one live material adjustment");
    hover("Подъём из diffuse");const float before=s.adjustments.diffuseHeightStrength,scroll=pane()->Scroll.y;actions.clear();
    io.AddMouseWheelEvent(0,1);step();step();
    require(s.adjustments.diffuseHeightStrength>before&&std::count(actions.begin(),actions.end(),lab::EditorAction::AdjustMaterial)==1,
            "height strength must respond to the mouse wheel through the material adjustment action");
    require(pane()->Scroll.y==scroll,"height strength wheel must not also scroll the material inspector");
    s.canAdjust=false;step();const float disabled=s.adjustments.diffuseHeightStrength;actions.clear();
    io.AddMouseWheelEvent(0,1);step();step();
    require(s.adjustments.diffuseHeightStrength==disabled&&actions.empty(),"a locked material cannot change its diffuse height strength");
    validate(s,1490,1100);
}
void animationAttachmentsAndCapture() {
    UIContext context(1.f);auto s=fixture(1.f);auto& io=ImGui::GetIO();
    s.tab=lab::EditorTab::Animation;s.forceTab=true;s.animations.clear();s.animationPlaying=false;
    s.showBipedBones=true;s.attachmentBones={{7,"bip01_r_hand"},{9,"bip01_l_hand"}};
    s.boneMarkers={{7,"bip01_r_hand",.42f,.44f,false},{9,"bip01_l_hand",.61f,.46f,false}};
    // Shell availability is deliberately true: Animation itself must keep
    // editing tools disabled even when the current pose is paused.
    s.canBrush=s.canUndo=s.canRedo=s.pendingAdjustments=true;
    std::vector<std::pair<lab::EditorAction,int>> actions;
    auto callback=[&](lab::EditorAction kind,int value){
        actions.emplace_back(kind,value);
        if(kind==lab::EditorAction::SelectAttachmentBone){
            s.attachmentBone=value;s.attachmentBoneName=value==7?"bip01_r_hand":"bip01_l_hand";
            s.recentAttachments={u8R"(C:\Предметы\водка.ogf)",u8R"(C:\Предметы\хлеб.ogf)"};
            s.requestAttachmentPopup=true;
        }
        if(kind==lab::EditorAction::UseRecentAttachment){s.attachmentActive=true;s.attachmentName=value==0?"водка.ogf":"хлеб.ogf";}
        if(kind==lab::EditorAction::SetMode)s.mode=lab::EditorMode(value);
    };
    auto step=[&]{
        // This is production shell state supplied before DrawEditorUI, not an
        // inference the pure UI renderer is expected to make for the caller.
        s.editingLocked=s.animationPlaying||s.tab==lab::EditorTab::Animation;
        frame(s,1490,1100,callback);
    };
    auto count=[&](lab::EditorAction kind){return std::count_if(actions.begin(),actions.end(),[&](const auto& a){return a.first==kind;});};
    for(int i=0;i<4;++i)step();
    const ImVec2 bonePoint(s.viewportRect.x+s.boneMarkers[0].x*s.viewportRect.w,s.viewportRect.y+s.boneMarkers[0].y*s.viewportRect.h);
    io.AddMousePosEvent(bonePoint.x,bonePoint.y);step();actions.clear();
    io.AddMouseButtonEvent(0,true);step();
    require(count(lab::EditorAction::SelectAttachmentBone)==1&&actions.back().second==7&&s.bonePointerCaptured,
            "clicking a projected biped point must select its real bone and consume the camera pointer");
    for(int i=0;i<12;++i)step();
    require(count(lab::EditorAction::SelectAttachmentBone)==1,"holding a biped point cannot open its menu repeatedly");
    io.AddMouseButtonEvent(0,false);step();for(int i=0;i<3;++i)step();
    auto popup=[](){for(auto* w:GImGui->Windows)if(w->LastFrameActive==GImGui->FrameCount&&(w->Flags&ImGuiWindowFlags_Popup))return w;return static_cast<ImGuiWindow*>(nullptr);};
    auto* recentPopup=popup();
    require(recentPopup&&s.attachmentBone==7&&!s.recentAttachments.empty(),"biped point click opens the selected bone's recent-prop menu");
    require(recentPopup->Pos.x>=0&&recentPopup->Pos.y>=0&&recentPopup->Pos.x+recentPopup->Size.x<=1490&&recentPopup->Pos.y+recentPopup->Size.y<=1100,
            "bone menu remains inside the application window");
    auto hover=[&](ImGuiWindow* w,ImGuiID id){
        for(float y=w->Pos.y+5;y<w->Pos.y+w->Size.y-5;y+=3){io.AddMousePosEvent(w->Pos.x+70,y);step();if(GImGui->HoveredId==id)return;}
        throw std::runtime_error("cannot find attachment-menu action");
    };
    const auto recentId=ImHashStr("хлеб.ogf",0,recentPopup->GetID(1));hover(recentPopup,recentId);actions.clear();
    io.AddMouseButtonEvent(0,true);step();for(int i=0;i<8;++i)step();io.AddMouseButtonEvent(0,false);step();step();
    require(count(lab::EditorAction::UseRecentAttachment)==1&&
            std::find(actions.begin(),actions.end(),std::make_pair(lab::EditorAction::UseRecentAttachment,1))!=actions.end(),
            "recent-prop click dispatches its exact list index once, including a held mouse button");
    require(!popup()&&s.attachmentActive&&s.attachmentName=="хлеб.ogf","choosing a recent prop closes its menu and displays the attachment state");

    auto pane=[](){for(auto* w:GImGui->Windows)if(w->LastFrameActive==GImGui->FrameCount&&std::string(w->Name).find("Active pane")!=std::string::npos)return w;return static_cast<ImGuiWindow*>(nullptr);};
    auto* animation=pane();require(animation!=nullptr,"animation inspector exists");
    hover(animation,animation->GetID("Сохранить PNG с прозрачным фоном…"));actions.clear();
    io.AddMouseButtonEvent(0,true);step();io.AddMouseButtonEvent(0,false);step();
    require(s.animations.empty()&&count(lab::EditorAction::CaptureAnimation)==1,
            "transparent PNG export remains available for bind pose without animation clips");

    const float button=std::max(30.f,ImGui::CalcTextSize("WW").x+ImGui::GetStyle().FramePadding.x*2);
    auto toolClick=[&](int index){
        io.AddMousePosEvent(s.toolbarRect.x+index*(button+ImGui::GetStyle().ItemSpacing.x)+button*.5f,s.toolbarRect.y+ImGui::GetFrameHeight()*.5f);step();
        io.AddMouseButtonEvent(0,true);step();io.AddMouseButtonEvent(0,false);step();
    };
    actions.clear();for(int index:{2,5,6,8,10,11,12})toolClick(index);
    require(actions.empty()&&!s.animationPlaying&&s.editingLocked,
            "paused Animation must disable mesh selection, brush, model rotation, isolation, undo/redo and material apply");
    toolClick(1);toolClick(4);
    require(std::find(actions.begin(),actions.end(),std::make_pair(lab::EditorAction::SetMode,int(lab::EditorMode::Orbit)))!=actions.end()&&
            std::find(actions.begin(),actions.end(),std::make_pair(lab::EditorAction::SetMode,int(lab::EditorMode::Light)))!=actions.end(),
            "paused Animation keeps camera and lighting controls available");
    const auto remembered=s.recentAttachments;
    s.tab=lab::EditorTab::Scene;s.forceTab=true;for(int i=0;i<4;++i)step();actions.clear();
    io.AddMousePosEvent(bonePoint.x,bonePoint.y);step();io.AddMouseButtonEvent(0,true);step();io.AddMouseButtonEvent(0,false);step();
    require(count(lab::EditorAction::SelectAttachmentBone)==0&&!s.bonePointerCaptured&&s.recentAttachments==remembered,
            "leaving Animation removes bone-point interaction without clearing the recent list");
    validate(s,1490,1100);
}
// Render the actual ImGui draw lists into an internal audit screenshot. This
// exercises the production portable UI; it is not a separate mock-up layout.
void snapshot(const std::string& path,int width,int height) {
    unsigned char* atlas=nullptr;int aw=0,ah=0;ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&atlas,&aw,&ah);
    std::vector<unsigned char> rgb(size_t(width)*height*3,15);
    auto edge=[](ImVec2 a,ImVec2 b,float x,float y){return (x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x);};
    auto* data=ImGui::GetDrawData();
    for(int li=0;li<data->CmdListsCount;++li){const auto& list=*data->CmdLists[li];
        for(const auto& cmd:list.CmdBuffer){if(cmd.UserCallback)continue;
            for(unsigned k=0;k+2<cmd.ElemCount;k+=3){
                const auto& a=list.VtxBuffer[list.IdxBuffer[cmd.IdxOffset+k]+cmd.VtxOffset];
                const auto& b=list.VtxBuffer[list.IdxBuffer[cmd.IdxOffset+k+1]+cmd.VtxOffset];
                const auto& c=list.VtxBuffer[list.IdxBuffer[cmd.IdxOffset+k+2]+cmd.VtxOffset];
                const float area=edge(a.pos,b.pos,c.pos.x,c.pos.y);if(std::fabs(area)<1e-6f)continue;
                int x0=std::max({0,int(std::floor(std::min({a.pos.x,b.pos.x,c.pos.x}))),int(std::ceil(cmd.ClipRect.x))});
                int y0=std::max({0,int(std::floor(std::min({a.pos.y,b.pos.y,c.pos.y}))),int(std::ceil(cmd.ClipRect.y))});
                int x1=std::min({width,int(std::ceil(std::max({a.pos.x,b.pos.x,c.pos.x}))),int(cmd.ClipRect.z)});
                int y1=std::min({height,int(std::ceil(std::max({a.pos.y,b.pos.y,c.pos.y}))),int(cmd.ClipRect.w)});
                for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){
                    const float wa=edge(b.pos,c.pos,x+.5f,y+.5f)/area,wb=edge(c.pos,a.pos,x+.5f,y+.5f)/area,wc=1-wa-wb;
                    if(wa<0||wb<0||wc<0)continue;
                    const float u=a.uv.x*wa+b.uv.x*wb+c.uv.x*wc,v=a.uv.y*wa+b.uv.y*wb+c.uv.y*wc;
                    const int tx=std::clamp(int(u*aw),0,aw-1),ty=std::clamp(int(v*ah),0,ah-1);
                    const auto* tex=atlas+(size_t(ty)*aw+tx)*4;
                    const float alpha=(((a.col>>24)&255)*wa+((b.col>>24)&255)*wb+((c.col>>24)&255)*wc)*tex[3]/65025.f;
                    for(int ch=0;ch<3;++ch){const float color=(((a.col>>(ch*8))&255)*wa+((b.col>>(ch*8))&255)*wb+((c.col>>(ch*8))&255)*wc)*tex[ch]/255.f;
                        auto& out=rgb[(size_t(y)*width+x)*3+ch];out=static_cast<unsigned char>(std::clamp(color*alpha+out*(1-alpha),0.f,255.f));}
                }
            }
        }
    }
    std::ofstream out(path,std::ios::binary);out<<"P6\n"<<width<<' '<<height<<"\n255\n";out.write(reinterpret_cast<const char*>(rgb.data()),std::streamsize(rgb.size()));
    require(bool(out),"cannot write audit screenshot");
}
}
int main(int argc,char** argv) {
    try {
        if(argc>=7&&std::string(argv[1])=="--snapshot"){
            int w=std::stoi(argv[3]),h=std::stoi(argv[4]),tab=std::stoi(argv[6]);float scale=std::stof(argv[5]);
            UIContext context(scale);auto state=fixture(scale);
            if(argc>7&&std::string(argv[7])=="surface"){state.brushTool=lab::BrushTool::SurfaceRelief;state.forceBrushTab=true;}
            if(argc>7&&std::string(argv[7])=="attachments"){
                state.showBipedBones=true;state.attachmentActive=true;state.attachmentBone=7;
                state.attachmentBoneName="bip01_r_hand";state.attachmentName="wpn_ak74.ogf";
                state.attachmentBones={{0,"bip01_pelvis"},{1,"bip01_spine"},{4,"bip01_head"},{7,"bip01_r_hand"},{9,"bip01_l_hand"}};
                state.boneMarkers={{0,"bip01_pelvis",.50f,.58f,false},{1,"bip01_spine",.50f,.42f,false},
                    {4,"bip01_head",.50f,.24f,false},{7,"bip01_r_hand",.35f,.46f,true},{9,"bip01_l_hand",.65f,.46f,false}};
                state.recentAttachments={R"(C:\S.T.A.L.K.E.R.\gamedata\meshes\weapons\ak74\wpn_ak74.ogf)",u8R"(C:\Мой мод\gamedata\meshes\items\водка.ogf)"};
                state.attachmentTransform.position={.015f,.03f,.02f};state.attachmentTransform.rotation={0,90,0};
                state.status="wpn_ak74.ogf закреплён на bip01_r_hand. Материал основного объекта доступен после выхода из анимации.";
            }
            for(int i=0;i<5;++i){state.tab=lab::EditorTab(tab);state.forceTab=true;
                state.editingLocked=state.animationPlaying||state.tab==lab::EditorTab::Animation;frame(state,w,h);}
            validate(state,w,h);snapshot(argv[2],w,h);return 0;
        }
        interactions();wheelAndAnimationInteractions();surfaceReliefInteractions();diffuseHeightControls();animationAttachmentsAndCapture();
        int frames=0;
        for(float scale:{1.f,1.25f,1.5f,2.f}){
            UIContext context(scale);auto state=fixture(scale);
            for(const auto& size:std::vector<ImVec2>{{1100,640},{1490,860},{1920,1080},{1280,720}}){
                for(int repeat=0;repeat<3;++repeat)for(int tab=0;tab<6;++tab){
                    for(int settle=0;settle<2;++settle){state.tab=lab::EditorTab(tab);state.forceTab=true;frame(state,int(size.x),int(size.y));validate(state,int(size.x),int(size.y));++frames;}
                    require(state.tab==lab::EditorTab(tab),"requested tab was not activated");
                    if(tab==int(lab::EditorTab::Brush)){
                        state.brushTool=lab::BrushTool::SurfaceRelief;state.forceBrushTab=true;
                        for(int settle=0;settle<2;++settle){frame(state,int(size.x),int(size.y));validate(state,int(size.x),int(size.y));++frames;}
                        state.brushTool=lab::BrushTool::Freehand;state.forceBrushTab=true;
                    }
                }
            }
        }
        std::cout<<"PASS: "<<frames<<" actual UI frames, six tabs, four sizes, four DPI scales; isolated pane and viewport/toolbar bounds; real mouse input, disabled actions and independent scrolling\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
