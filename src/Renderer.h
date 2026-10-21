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
#include "Core.h"
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
  };
  Dev device_;Ctx context_;
  Microsoft::WRL::ComPtr<IDXGISwapChain> swap_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_,vsSkeleton_,vsOutline_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_,psSkeleton_,psOutline_;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> input_,inputSkeleton_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> cb_,skeletonVB_,brushVB_;
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_,outlineRaster_;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilState> skeletonDepth_;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
  std::vector<RenderPart> parts_;
  std::vector<Bone> bones_;
  std::vector<Motion> motions_;
  UINT skeletonVertexCount_=0,brushVertexCount_=0,width_=0,height_=0;
  V3 modelCenter_{}; float modelScale_=1.f;
  float yaw_=0.65f,pitch_=0.25f,dist_=3.8f,cameraY_=0.f,lightYaw_=0.9f,lightPitch_=0.65f;
  float normalStrength_=1.f,glossStrength_=0.5f;
  float tint_[3]={1.f,1.f,1.f},light_[3]={1.f,0.93f,0.78f},ambient_[3]={0.21f,0.24f,0.29f};
  bool invertNormalY_=false,bumpOn_=true,skeletonOn_=true;
  int selectedPrimary_=-1;
  bool createViews(UINT w,UINT h,std::wstring& error);
  bool makeTexture(const Image& img,Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& out);
  bool uploadBindPose(std::wstring* error=nullptr);
public:
  bool Init(HWND hwnd,std::wstring& error);
  bool Resize(UINT w,UINT h,std::wstring& error);
  bool SetMesh(Mesh& mesh,std::wstring& error);
  bool SetPartImages(size_t part,const PartImages& images);
  bool SetPartBumpImages(size_t part,const Image& bump,const Image& bumpX,bool hasBump=true);
  bool CopyPartMaterial(size_t dst,size_t src);
  void SetImages(const Image& diffuse,const Image& bump,const Image& bumpX);
  void SetBumpImages(const Image& bump,const Image& bumpX,bool hasBump=true);
  bool SetAnimationPose(size_t motionIndex,float seconds,bool loop,std::wstring& error);
  void SetBindPose();
  size_t PartCount()const{return parts_.size();}
  size_t MotionCount()const{return motions_.size();}
  const Motion* MotionAt(size_t index)const{return index<motions_.size()?&motions_[index]:nullptr;}
  bool HasSkeleton()const{return !bones_.empty()&&skeletonVB_;}
  void SetColor(int which,COLORREF color);
  COLORREF GetColor(int which) const;
  void OrbitCamera(float dx,float dy);
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
  void SetNormalSettings(float strength,bool invertY){normalStrength_=strength;invertNormalY_=invertY;}
  void SetGlossStrength(float strength){glossStrength_=std::max(0.f,std::min(strength,1.f));}
  void SetPartVisible(size_t part,bool visible);
  bool PartVisible(size_t part)const;
  void SelectPart(int part,bool withMirror=false);
  struct PickHit{int part=-1;float u=0.f,v=0.f;V3 position{},normal{},du{},dv{};};
  bool PickUV(int screenX,int screenY,PickHit& hit);
  void SetBrushCursor(const PickHit& hit,float radiusU,float radiusV);
  void ClearBrushCursor();
  int PickGeometry(int screenX,int screenY);
  void ClearSelection();
  bool HasSelection()const;
  int SelectedPart()const{return selectedPrimary_;}
  void Draw();
};
}
