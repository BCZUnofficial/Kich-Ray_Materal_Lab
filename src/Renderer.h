#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <string>
#include <algorithm>
#include <vector>
#include <memory>
#include "Core.h"
#include "ViewportTools.h"
#include "MaterialAlpha.h"
#include "AttachmentPreview.h"
namespace lab {
class Renderer {
  using Dev=Microsoft::WRL::ComPtr<ID3D11Device>;
  using Ctx=Microsoft::WRL::ComPtr<ID3D11DeviceContext>;
  struct CPUVertex {
    V3 p,n,t,b; float u=0,v=0;
    uint16_t bone0=0,bone1=0; float weight=0.f; uint8_t links=0;
  };
  struct RenderPart {
    Microsoft::WRL::ComPtr<ID3D11Buffer> vb,ib;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> textures[3];
    Microsoft::WRL::ComPtr<ID3D11Texture2D> textureResources[3];
    std::vector<CPUVertex> source;
    std::vector<V3> cpuPositions;
    std::vector<V3> cpuUV;
    std::vector<uint32_t> cpuIndices;
    V3 center{},half{};
    UINT indexCount=0;
    bool hasBump=false;
    bool selected=false;
    bool skeletal=false;
    bool visible=true;
    float emissive=0.f,emissiveSpecular=0.f,materialClass=1.f;
    bool flatShader=false;
    AlphaProfile alpha;
    std::shared_ptr<GrayImage> opacity;
    Microsoft::WRL::ComPtr<ID3D11Buffer> sortedIB;
    DirectX::XMFLOAT4X4 alphaSortView{};
    bool alphaSortValid=false;
  };
  struct Attachment {
    size_t bone=0;
    AttachmentTransform transform;
    std::vector<RenderPart> parts;
  };
  Dev device_;Ctx context_;
  Microsoft::WRL::ComPtr<IDXGISwapChain> swap_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_,uiRtv_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> scene_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sceneView_;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_,vsSkeleton_,vsOutline_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_,psSkeleton_,psOutline_;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> input_,inputSkeleton_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> cb_,skeletonVB_,brushVB_;
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_,outlineRaster_,wireRaster_;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilState> skeletonDepth_;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilState> alphaDepth_;
  Microsoft::WRL::ComPtr<ID3D11BlendState> alphaBlend_,depthOnlyBlend_;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> materialLut_;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> materialSampler_;
  std::vector<RenderPart> parts_;
  std::vector<Bone> bones_;
  std::vector<Motion> motions_;
  std::vector<DirectX::XMFLOAT4X4> boneTransforms_;
  std::vector<Attachment> attachments_;
  UINT skeletonVertexCount_=0,brushVertexCount_=0,width_=0,height_=0,windowWidth_=0,windowHeight_=0;
  V3 modelCenter_{}; float modelScale_=1.f;
  ViewportCamera camera_;
  float lightYaw_=0.9f,lightPitch_=0.65f;
  float modelYaw_=0.f,modelPitch_=0.f;
  float normalStrength_=1.f,glossStrength_=0.5f;
  float lightIntensity_=1.f;
  V3 environment_{};
  float hemisphere_=1.f,exposure_=1.f,r4GlossFactor_=1.f;
  int materialClass_=-1;
  bool glossEnabled_=true;
  float background_[3]={.095f,.105f,.12f};
  float tint_[3]={1.f,1.f,1.f},light_[3]={1.f,0.93f,0.78f},ambient_[3]={0.21f,0.24f,0.29f};
  bool invertNormalY_=false,bumpOn_=true,skeletonOn_=true,outlineOn_=true,wireframeOn_=false;
  int selectedPrimary_=-1,hoverPart_=-1;
  bool alphaEnabled_=true;
  AlphaPreview alphaPreview_=AlphaPreview::Automatic;
  float alphaCutoff_=.5f;
  AlphaMode alphaMode(const RenderPart& part)const{return ResolveAlpha(part.alpha,alphaEnabled_,alphaPreview_);}
  ID3D11Buffer* sortedAlphaIndices(RenderPart& part,const DirectX::XMMATRIX& modelView);
  bool createViews(UINT w,UINT h,std::wstring& error);
  bool makeTexture(const Image& img,Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& out,Microsoft::WRL::ComPtr<ID3D11Texture2D>* textureOut=nullptr,const std::vector<Image>* mips=nullptr);
  bool uploadBindPose(std::wstring* error=nullptr);
  void cacheBindTransforms();
  bool buildParts(const Mesh& mesh,V3 center,float scale,std::vector<RenderPart>& parts,std::wstring& error,bool animate);
  bool setPartImages(RenderPart& part,const PartImages& images);
  DirectX::XMMATRIX attachmentWorld(const Attachment& attachment)const;
  void drawScene(bool clean,bool transparent);
  bool readSceneImage(Image& image,std::wstring* error=nullptr);
public:
  bool Init(HWND hwnd,std::wstring& error,bool software=false);
  // Window and scene surfaces are independent: the scene is displayed by ImGui.
  bool Resize(UINT w,UINT h,std::wstring& error);
  bool ResizeScene(UINT w,UINT h,std::wstring& error);
  ID3D11Device* Device()const{return device_.Get();}
  ID3D11DeviceContext* Context()const{return context_.Get();}
  ID3D11ShaderResourceView* SceneTexture()const{return sceneView_.Get();}
  UINT SceneWidth()const{return width_;}
  UINT SceneHeight()const{return height_;}
  void DrawScene();
  void BeginUI();
  HRESULT PresentUI();
  bool SetMesh(Mesh& mesh,std::wstring& error);
  bool SetPartImages(size_t part,const PartImages& images);
  bool SetPartBumpImages(size_t part,const Image& bump,const Image& bumpX,bool hasBump=true);
  bool SetPartBumpMipImages(size_t part,const std::vector<Image>& bump,const std::vector<Image>& bumpX);
  bool CopyPartMaterial(size_t dst,size_t src);
  bool SetImages(const Image& diffuse,const Image& bump,const Image& bumpX);
  bool SetBumpImages(const Image& bump,const Image& bumpX,bool hasBump=true);
  bool SetAnimationPose(size_t motionIndex,float seconds,bool loop,std::wstring& error);
  bool SetBindPose(std::wstring* error=nullptr);
  size_t PartCount()const{return parts_.size();}
  size_t MotionCount()const{return motions_.size();}
  const Motion* MotionAt(size_t index)const{return index<motions_.size()?&motions_[index]:nullptr;}
  bool HasSkeleton()const{return !bones_.empty()&&skeletonVB_;}
  size_t BoneCount()const{return bones_.size();}
  const Bone* BoneAt(size_t index)const{return index<bones_.size()?&bones_[index]:nullptr;}
  struct BonePoint {size_t index=0;std::string name;float x=0,y=0;bool visible=false;};
  std::vector<BonePoint> BonePoints()const;
  bool SetAttachment(size_t bone,const Mesh& mesh,const std::vector<PartImages>& images,const AttachmentTransform& transform,std::wstring& error);
  bool SetAttachmentTransform(size_t bone,const AttachmentTransform& transform,std::wstring& error);
  void SetAttachmentMaterialClass(size_t bone,size_t part,float material);
  void RemoveAttachment(size_t bone);
  void ClearAttachments(){attachments_.clear();}
  bool HasAttachment(size_t bone)const;
  void SetColor(int which,COLORREF color);
  COLORREF GetColor(int which) const;
  void OrbitCamera(float dx,float dy);
  ViewportCamera CameraState()const{return camera_;}
  void FlyCameraEnabled(bool on){camera_.fly=on;}
  void MoveCamera(float right,float up,float forward){camera_.move(right,up,forward);}
  void PanCamera(float dx,float dy){camera_.pan(dx,dy);}
  void FrameSelection();
  void AlignAxis(int axis){camera_.alignAxis(axis);FrameSelection();}
  void SetLightIntensity(float intensity){lightIntensity_=std::clamp(intensity,0.f,8.f);}
  float LightIntensity()const{return lightIntensity_;}
  void SetEnvironmentLighting(V3 environment,float hemisphere,float exposure,float glossFactor);
  void SetMaterialClass(int material){materialClass_=std::clamp(material,-1,3);}
  void SetPartMaterialClass(size_t part,float material){if(part<parts_.size()&&std::isfinite(material))parts_[part].materialClass=std::clamp(material,0.f,3.f);}
  void SetBackground(const float* rgb){for(int i=0;i<3;++i)background_[i]=std::clamp(rgb[i],0.f,1.f);}
  void GlossEnabled(bool enabled){glossEnabled_=enabled;}
  void RotateModel(float dx,float dy);
  void ResetModelRotation(){modelYaw_=0.f;modelPitch_=0.f;}
  void PanVertical(float dy);
  void OrbitLight(float dx,float dy);
  void SetLightOrbitDegrees(float azimuth,float elevation);
  float LightAzimuthDegrees()const;
  float LightElevationDegrees()const;
  void Zoom(float steps);
  void ResetView();
  void Reset();
  void BumpEnabled(bool on){bumpOn_=on;}
  void SkeletonEnabled(bool on){skeletonOn_=on;}
  void OutlineEnabled(bool on){outlineOn_=on;}
  void WireframeEnabled(bool on){wireframeOn_=on;}
  void AlphaEnabled(bool on){alphaEnabled_=on;}
  void SetAlphaPreview(AlphaPreview mode){alphaPreview_=mode;}
  void SetAlphaCutoff(float value){alphaCutoff_=std::clamp(value,0.f,1.f);}
  void SetNormalSettings(float strength,bool invertY){normalStrength_=strength;invertNormalY_=invertY;}
  void SetGlossStrength(float strength){glossStrength_=std::max(0.f,std::min(strength,1.f));}
  float NormalStrength()const{return normalStrength_;}
  bool NormalYInverted()const{return invertNormalY_;}
  float GlossStrength()const{return glossStrength_;}
  void SetPartVisible(size_t part,bool visible);
  bool PartVisible(size_t part)const;
  bool PartSelected(size_t part)const{return part<parts_.size()&&parts_[part].selected&&parts_[part].visible;}
  void SelectPart(int part,bool withMirror=false);
  void SetHoverPart(int part){hoverPart_=(part>=0&&size_t(part)<parts_.size()&&parts_[size_t(part)].visible)?part:-1;}
  void ClearHover(){hoverPart_=-1;}
  int HoverPart()const{return hoverPart_;}
  // Coordinates are local pixels inside SceneTexture, not the main window.
  struct PickHit{int part=-1;float u=0.f,v=0.f;V3 position{},normal{},du{},dv{};};
  bool PickUV(int screenX,int screenY,PickHit& hit);
  void SetBrushCursor(const PickHit& hit,float radiusU,float radiusV);
  void ClearBrushCursor();
  int PickGeometry(int screenX,int screenY);
  void ClearSelection();
  bool HasSelection()const;
  int SelectedPart()const{return selectedPrimary_;}
  void Draw(bool present=true);
  bool CaptureFrame(Image& image);
  bool CaptureTransparent(Image& image,uint32_t maxDimension,std::wstring& error);
};
}
