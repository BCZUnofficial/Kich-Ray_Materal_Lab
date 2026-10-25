#pragma once
#include "Core.h"
#include "imgui.h"
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace lab {
enum class EditorTab { Files, Export, Material, Scene, Brush, Animation };
enum class EditorMode { Fly, Orbit, Choose, Light, Brush, Model };
enum class BrushTool { Freehand, SurfaceRelief };
enum class EditorAction {
    OpenModel, NewCube, LoadDiffuse, LoadNormal, LoadGloss, LoadHeight, LoadBump, LoadBumpX,
    RequestGenerate, GenerateSelected, Export, SaveSession, OpenSession, LoadPreset, SavePreset,
    ClearBrush, Undo, Redo, SetMode, Outline, Frame, Isolate, Wireframe, ResetView,
    SelectPart, Visibility, ShowAll, HideSelected, ClearSelection, PreviewSettings,
    LightSettings, AlphaSettings, Skeleton, AnimationSelect, AnimationPlay,
    AnimationSeek, AnimationSettings, AdjustMaterial, ApplyAdjustments, DiscardAdjustments,
    GlossSettings, BackgroundSettings, AxisView, SurfacePreset, SetBrushTool
};
struct EditorMeshRow {
    std::string label;
    bool visible=true,selected=false,hasDiffuse=false,hasBump=false;
};
struct EditorRect { float x=0,y=0,w=0,h=0; };
struct EditorUIState {
    EditorTab tab=EditorTab::Files;
    EditorMode mode=EditorMode::Fly;
    bool forceTab=false,editingLocked=false,canBrush=false,canUndo=false,canRedo=false,hasEdits=false;
    bool outline=true,wireframe=false,isolated=false,skeleton=false,hasSkeleton=false;
    bool bump=true,alpha=true;
    float alphaCutoff=.5f;
    int alphaMode=0;
    MaterialAdjustments adjustments;
    bool pendingAdjustments=false,canAdjust=false,glossEnabled=true;
    int surfacePreset=1,materialClass=0;
    BrushTool brushTool=BrushTool::Freehand;
    bool forceBrushTab=false,hasDiffuse=false;
    float lightIntensity=1.f;
    float background[3]={.095f,.105f,.12f};
    std::array<std::string,6> sourceNames;
    std::string modelName="Тестовый куб",status="Откройте OGF или загрузите diffuse.",documentInfo;
    std::array<char,256> exportName{};
    bool useTextureName=true;
    std::vector<EditorMeshRow> meshes;
    float lightAzimuth=52.f,lightElevation=37.f;
    float tint[3]={1,1,1},lightColor[3]={1,.93f,.78f},ambient[3]={.21f,.24f,.29f};
    int brushChannel=0;
    bool brushRemove=false;
    float brushRadiusPercent=3.f,brushStrengthPercent=35.f,brushHardnessPercent=65.f;
    std::string presetName="Круглая кисть";
    std::vector<std::string> animations;
    int animationIndex=0,animationFrame=0,animationFrames=1;
    float animationSpeed=1.f;
    bool animationPlaying=false,animationLoop=true;
    bool requestGeneratePopup=false;
    std::vector<bool> generateTargets;
    ImTextureID viewportTexture=0;
    EditorRect viewportRect,toolbarRect,inspectorRect;
    bool viewportHovered=false,viewportClicked=false;
    int renderedPaneCount=0;
    float uiScale=1.f;
};
using EditorUICallback=std::function<void(EditorAction,int)>;
// Rebuild from logical defaults so moving between different-DPI monitors is idempotent.
void ConfigureEditorUIStyle(float scale);
// Pure immediate-mode layout: no HWNDs, file dialogs, or graphics-device calls.
// The shell fills data before a frame and handles callbacks with the updated state.
void DrawEditorUI(EditorUIState& state,const EditorUICallback& action);
}
