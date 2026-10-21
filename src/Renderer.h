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
#include <vector>
#include "Core.h"
namespace lab {
class Renderer {
  using Dev=Microsoft::WRL::ComPtr<ID3D11Device>;
  using Ctx=Microsoft::WRL::ComPtr<ID3D11DeviceContext>;
  struct RenderPart {
    Microsoft::WRL::ComPtr<ID3D11Buffer> vb,ib;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> textures[3];
    std::vector<V3> cpuPositions;
    std::vector<uint32_t> cpuIndices;
    V3 center{},half{};
    UINT indexCount=0;
    bool hasBump=false;
    bool selected=false;
  };
  Dev device_;Ctx context_;
  Microsoft::WRL::ComPtr<IDXGISwapChain> swap_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_,vsSkeleton_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_,psSkeleton_;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> input_,inputSkeleton_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> cb_,skeletonVB_;
  Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilState> skeletonDepth_;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
  std::vector<RenderPart> parts_;
  UINT skeletonVertexCount_=0,width_=0,height_=0;
  float yaw_=0.65f,pitch_=0.25f,dist_=3.8f,cameraY_=0.f,lightYaw_=0.9f,lightPitch_=0.65f;
  float normalStrength_=1.f;
  float tint_[3]={1.f,1.f,1.f},light_[3]={1.f,0.93f,0.78f},ambient_[3]={0.21f,0.24f,0.29f};
  bool invertNormalY_=false,bumpOn_=true,skeletonOn_=true;
  int selectedPrimary_=-1;
  bool createViews(UINT w,UINT h,std::wstring& error);
  bool makeTexture(const Image& img,Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& out);
public:
  bool Init(HWND hwnd,std::wstring& error);
  bool Resize(UINT w,UINT h,std::wstring& error);
  bool SetMesh(const Mesh& mesh,std::wstring& error);
  bool SetPartImages(size_t part,const PartImages& images);
  void SetImages(const Image& diffuse,const Image& bump,const Image& bumpX);
  void SetBumpImages(const Image& bump,const Image& bumpX,bool hasBump=true);
  size_t PartCount()const{return parts_.size();}
  bool HasSkeleton()const{return skeletonVertexCount_>0;}
  void SetColor(int which,COLORREF color);
  COLORREF GetColor(int which) const;
  void OrbitCamera(float dx,float dy);
  void PanVertical(float dy);
  void OrbitLight(float dx,float dy);
  void SetLightOrbitDegrees(float azimuth,float elevation);
  void Zoom(float steps);
  void Reset();
  void BumpEnabled(bool on){bumpOn_=on;}
  void SkeletonEnabled(bool on){skeletonOn_=on;}
  void SetNormalSettings(float strength,bool invertY){normalStrength_=strength;invertNormalY_=invertY;}
  int PickGeometry(int screenX,int screenY);
  void ClearSelection();
  bool HasSelection()const;
  int SelectedPart()const{return selectedPrimary_;}
  void Draw();
};
}
