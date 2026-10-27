#include "Renderer.h"
#include "EngineLighting.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <vector>
#include <cstring>
#include <exception>
#pragma comment(lib,"d3d11.lib")
namespace lab {
using namespace DirectX;
namespace {
using Microsoft::WRL::ComPtr;
template<class F>struct ScopeExit {F apply;~ScopeExit(){apply();}};
template<class F>ScopeExit<F> onExit(F apply){return {std::move(apply)};}
struct GPUVertex{XMFLOAT3 p,n;XMFLOAT2 uv;XMFLOAT3 t,b;};
struct BoneGPU{XMFLOAT3 p;XMFLOAT4 c;};
struct alignas(16) CB{XMFLOAT4X4 wvp,world;XMFLOAT4 eye,ld,lc,ambient,tint,params,outline,viewport,alpha,lighting,environment,lightingOptions;};
V3 sub(V3 a,V3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V3 add(V3 a,V3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V3 mul(V3 a,float s){return {a.x*s,a.y*s,a.z*s};}
float dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V3 cross(V3 a,V3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float len(V3 a){return std::sqrt(dot(a,a));}
V3 normalize(V3 a){float l=len(a);return l>1e-8f?mul(a,1.f/l):V3{0,0,1};}
bool rayTriangle(V3 ro,V3 rd,V3 a,V3 b,V3 c,float& t,float* outU=nullptr,float* outV=nullptr){const float eps=1e-7f;V3 e1=sub(b,a),e2=sub(c,a),h=cross(rd,e2);float det=dot(e1,h);if(std::fabs(det)<eps)return false;float inv=1.f/det;V3 q=sub(ro,a);float u=dot(q,h)*inv;if(u<0.f||u>1.f)return false;V3 r=cross(q,e1);float v=dot(rd,r)*inv;if(v<0.f||u+v>1.f)return false;float hit=dot(e2,r)*inv;if(hit<=eps)return false;t=hit;if(outU)*outU=u;if(outV)*outV=v;return true;}
bool rayAabb(V3 ro,V3 rd,V3 center,V3 half,float& nearT){float tmin=0.f,tmax=1e30f;const float o[3]={ro.x,ro.y,ro.z},d[3]={rd.x,rd.y,rd.z},c[3]={center.x,center.y,center.z},h[3]={half.x,half.y,half.z};for(int a=0;a<3;++a){const float mn=c[a]-h[a]-1e-4f,mx=c[a]+h[a]+1e-4f;if(std::fabs(d[a])<1e-8f){if(o[a]<mn||o[a]>mx)return false;continue;}float t0=(mn-o[a])/d[a],t1=(mx-o[a])/d[a];if(t0>t1)std::swap(t0,t1);tmin=std::max(tmin,t0);tmax=std::min(tmax,t1);if(tmin>tmax)return false;}nearT=tmin;return true;}
std::wstring hrtext(const wchar_t* context,HRESULT hr){wchar_t t[192];swprintf_s(t,L"%ls (HRESULT 0x%08X)",context,unsigned(hr));return t;}
XMFLOAT3 vec(const V3& v){return {v.x,v.y,v.z};}
XMFLOAT3 norm3(XMVECTOR v){XMFLOAT3 a;XMStoreFloat3(&a,XMVector3Normalize(v));return a;}
struct XRMat{float m[4][4];};
XRMat xrIdentity(){XRMat r={};for(int i=0;i<4;++i)r.m[i][i]=1.f;return r;}
XRMat xrFromArray(const std::array<float,16>& a){XRMat r{};std::memcpy(r.m,a.data(),sizeof(r.m));return r;}
XRMat xrFromQuat(const Quat& q,V3 t){
 // Exact X-Ray Fmatrix::mk_xform convention. DirectXMath's quaternion matrix
 // uses a different sign/transposition convention and corrupts SoC skeletons.
 const float xx=q.x*q.x,yy=q.y*q.y,zz=q.z*q.z,xy=q.x*q.y,xz=q.x*q.z,yz=q.y*q.z;
 const float wx=q.w*q.x,wy=q.w*q.y,wz=q.w*q.z;XRMat r=xrIdentity();
 r.m[0][0]=1.f-2.f*(yy+zz);r.m[0][1]=2.f*(xy-wz);r.m[0][2]=2.f*(xz+wy);
 r.m[1][0]=2.f*(xy+wz);r.m[1][1]=1.f-2.f*(xx+zz);r.m[1][2]=2.f*(yz-wx);
 r.m[2][0]=2.f*(xz-wy);r.m[2][1]=2.f*(yz+wx);r.m[2][2]=1.f-2.f*(xx+yy);
 r.m[3][0]=t.x;r.m[3][1]=t.y;r.m[3][2]=t.z;return r;
}
XRMat xrMul43(const XRMat& A,const XRMat& B){
 // Byte-for-byte algebraic equivalent of X-Ray Fmatrix::mul_43(A,B).
 XRMat r=xrIdentity();
 for(int row=0;row<3;++row)for(int col=0;col<3;++col)
  r.m[row][col]=A.m[0][col]*B.m[row][0]+A.m[1][col]*B.m[row][1]+A.m[2][col]*B.m[row][2];
 for(int col=0;col<3;++col)
  r.m[3][col]=A.m[0][col]*B.m[3][0]+A.m[1][col]*B.m[3][1]+A.m[2][col]*B.m[3][2]+A.m[3][col];
 return r;
}
V3 xrPoint(const XRMat& M,V3 v){return {v.x*M.m[0][0]+v.y*M.m[1][0]+v.z*M.m[2][0]+M.m[3][0],v.x*M.m[0][1]+v.y*M.m[1][1]+v.z*M.m[2][1]+M.m[3][1],v.x*M.m[0][2]+v.y*M.m[1][2]+v.z*M.m[2][2]+M.m[3][2]};}
V3 xrDir(const XRMat& M,V3 v){return {v.x*M.m[0][0]+v.y*M.m[1][0]+v.z*M.m[2][0],v.x*M.m[0][1]+v.y*M.m[1][1]+v.z*M.m[2][1],v.x*M.m[0][2]+v.y*M.m[1][2]+v.z*M.m[2][2]};}
Quat qnorm(Quat q){float m=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);if(m<1e-8f)return {};float k=1.f/m;return {q.x*k,q.y*k,q.z*k,q.w*k};}
Quat qslerp(Quat a,Quat b,float t){
 float cosom=a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z,sign=1.f;if(cosom<0.f){cosom=-cosom;sign=-1.f;}cosom=std::clamp(cosom,-1.f,1.f);
 float s0=1.f-t,s1=t;if(1.f-cosom>1e-6f){float omega=std::acos(cosom),inv=1.f/std::sin(omega);s0=std::sin((1.f-t)*omega)*inv;s1=std::sin(t*omega)*inv;}s1*=sign;
 return qnorm({a.x*s0+b.x*s1,a.y*s0+b.y*s1,a.z*s0+b.z*s1,a.w*s0+b.w*s1});
}
Quat sampleQ(const MotionTrack& t,uint32_t a,uint32_t b,float d){if(t.rotations.empty())return {};if(t.rotations.size()==1)return t.rotations[0];a%=uint32_t(t.rotations.size());b%=uint32_t(t.rotations.size());return qslerp(t.rotations[a],t.rotations[b],std::clamp(d,0.f,1.f));}
V3 sampleT(const MotionTrack& t,uint32_t a,uint32_t b,float d){if(t.translations.empty())return {};if(t.translations.size()==1)return t.translations[0];a%=uint32_t(t.translations.size());b%=uint32_t(t.translations.size());V3 x=t.translations[a],y=t.translations[b];return {x.x+(y.x-x.x)*d,x.y+(y.y-x.y)*d,x.z+(y.z-x.z)*d};}
}

bool Renderer::createViews(UINT,UINT,std::wstring& error){
 ComPtr<ID3D11Texture2D> back;ComPtr<ID3D11RenderTargetView> view;
 HRESULT hr=swap_->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(back.GetAddressOf()));
 if(FAILED(hr)){error=hrtext(L"Get UI backbuffer",hr);return false;}
 hr=device_->CreateRenderTargetView(back.Get(),nullptr,view.GetAddressOf());
 if(FAILED(hr)){error=hrtext(L"Create UI target",hr);return false;}
 D3D11_TEXTURE2D_DESC desc{};back->GetDesc(&desc);
 uiRtv_=std::move(view);windowWidth_=desc.Width;windowHeight_=desc.Height;return true;
}

bool Renderer::ResizeScene(UINT w,UINT h,std::wstring& error){
 error.clear();if(!device_||!context_){error=L"Renderer is not initialized";return false;}
 if(!w||!h)return true;
 if(w==width_&&h==height_&&scene_&&rtv_&&dsv_)return true;
 if(w>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION||h>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION){error=L"Scene surface is too large";return false;}
 ComPtr<ID3D11Texture2D> color,depth;ComPtr<ID3D11RenderTargetView> colorTarget;
 ComPtr<ID3D11ShaderResourceView> colorView;ComPtr<ID3D11DepthStencilView> depthView;
 D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=1;desc.ArraySize=1;
 desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
 desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
 HRESULT hr=device_->CreateTexture2D(&desc,nullptr,color.GetAddressOf());
 if(SUCCEEDED(hr))hr=device_->CreateRenderTargetView(color.Get(),nullptr,colorTarget.GetAddressOf());
 if(SUCCEEDED(hr))hr=device_->CreateShaderResourceView(color.Get(),nullptr,colorView.GetAddressOf());
 if(FAILED(hr)){error=hrtext(L"Create scene surface",hr);return false;}
 desc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
 hr=device_->CreateTexture2D(&desc,nullptr,depth.GetAddressOf());
 if(SUCCEEDED(hr))hr=device_->CreateDepthStencilView(depth.Get(),nullptr,depthView.GetAddressOf());
 if(FAILED(hr)){error=hrtext(L"Create scene depth",hr);return false;}
 // Do not discard a usable surface until the whole replacement exists.
 context_->OMSetRenderTargets(0,nullptr,nullptr);
 ID3D11ShaderResourceView* empty[3]{};context_->PSSetShaderResources(0,3,empty);
 scene_=std::move(color);sceneView_=std::move(colorView);rtv_=std::move(colorTarget);
 depth_=std::move(depth);dsv_=std::move(depthView);width_=w;height_=h;return true;
}

bool Renderer::Init(HWND hwnd,std::wstring& error,bool software){RECT rc{};GetClientRect(hwnd,&rc);UINT w=std::max<LONG>(1,rc.right),h=std::max<LONG>(1,rc.bottom);D3D_FEATURE_LEVEL features[]={D3D_FEATURE_LEVEL_11_0};D3D_FEATURE_LEVEL chosen;HRESULT hr=D3D11CreateDevice(nullptr,software?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE,nullptr,0,features,1,D3D11_SDK_VERSION,device_.GetAddressOf(),&chosen,context_.GetAddressOf());if(FAILED(hr)){error=hrtext(L"D3D11CreateDevice requires feature level 11_0",hr);return false;}ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIFactory> factory;hr=device_.As(&dx);if(SUCCEEDED(hr))hr=dx->GetAdapter(adapter.GetAddressOf());if(SUCCEEDED(hr))hr=adapter->GetParent(__uuidof(IDXGIFactory),reinterpret_cast<void**>(factory.GetAddressOf()));if(FAILED(hr)){error=hrtext(L"DXGI factory",hr);return false;}DXGI_SWAP_CHAIN_DESC sc={};sc.BufferCount=2;sc.BufferDesc.Width=w;sc.BufferDesc.Height=h;sc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sc.OutputWindow=hwnd;sc.SampleDesc.Count=1;sc.Windowed=TRUE;sc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;hr=factory->CreateSwapChain(device_.Get(),&sc,swap_.GetAddressOf());if(FAILED(hr)){error=hrtext(L"CreateSwapChain",hr);return false;}factory->MakeWindowAssociation(hwnd,DXGI_MWA_NO_ALT_ENTER);if(!createViews(w,h,error)||!ResizeScene(w,h,error))return false;
 wchar_t path[MAX_PATH];DWORD n=GetModuleFileNameW(nullptr,path,MAX_PATH);if(!n||n>=MAX_PATH){error=L"Cannot resolve program directory";return false;}std::filesystem::path shader=std::filesystem::path(path).parent_path()/L"assets"/L"Preview.hlsl";ComPtr<ID3DBlob> vsCode,psCode,vsBoneCode,psBoneCode,vsOutlineCode,psOutlineCode,msg;auto compile=[&](const char* entry,const char* target,ID3DBlob** output)->bool{msg.Reset();HRESULT result=D3DCompileFromFile(shader.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,target,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,output,msg.GetAddressOf());if(FAILED(result)){error=hrtext(L"HLSL compilation failed",result);if(msg){std::string log(reinterpret_cast<char*>(msg->GetBufferPointer()),msg->GetBufferSize());error+=L"\n"+std::wstring(log.begin(),log.end());}return false;}return true;};if(!compile("VSMain","vs_5_0",vsCode.GetAddressOf())||!compile("PSMain","ps_5_0",psCode.GetAddressOf())||!compile("VSSkeleton","vs_5_0",vsBoneCode.GetAddressOf())||!compile("PSSkeleton","ps_5_0",psBoneCode.GetAddressOf())||!compile("VSOutline","vs_5_0",vsOutlineCode.GetAddressOf())||!compile("PSOutline","ps_5_0",psOutlineCode.GetAddressOf()))return false;
 hr=device_->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,vs_.GetAddressOf());if(SUCCEEDED(hr))hr=device_->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,ps_.GetAddressOf());if(SUCCEEDED(hr))hr=device_->CreateVertexShader(vsBoneCode->GetBufferPointer(),vsBoneCode->GetBufferSize(),nullptr,vsSkeleton_.GetAddressOf());if(SUCCEEDED(hr))hr=device_->CreatePixelShader(psBoneCode->GetBufferPointer(),psBoneCode->GetBufferSize(),nullptr,psSkeleton_.GetAddressOf());if(SUCCEEDED(hr))hr=device_->CreateVertexShader(vsOutlineCode->GetBufferPointer(),vsOutlineCode->GetBufferSize(),nullptr,vsOutline_.GetAddressOf());if(SUCCEEDED(hr))hr=device_->CreatePixelShader(psOutlineCode->GetBufferPointer(),psOutlineCode->GetBufferSize(),nullptr,psOutline_.GetAddressOf());if(FAILED(hr)){error=hrtext(L"CreateShader",hr);return false;}
 D3D11_INPUT_ELEMENT_DESC layout[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},{"TANGENT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},{"BINORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,44,D3D11_INPUT_PER_VERTEX_DATA,0}};static_assert(sizeof(GPUVertex)==56,"GPU vertex stride");hr=device_->CreateInputLayout(layout,5,vsCode->GetBufferPointer(),vsCode->GetBufferSize(),input_.GetAddressOf());if(FAILED(hr)){error=hrtext(L"CreateInputLayout",hr);return false;}D3D11_INPUT_ELEMENT_DESC bl[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};hr=device_->CreateInputLayout(bl,2,vsBoneCode->GetBufferPointer(),vsBoneCode->GetBufferSize(),inputSkeleton_.GetAddressOf());if(FAILED(hr)){error=hrtext(L"Create skeleton input layout",hr);return false;}
 D3D11_BUFFER_DESC cbdesc={};cbdesc.ByteWidth=sizeof(CB);cbdesc.Usage=D3D11_USAGE_DEFAULT;cbdesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;hr=device_->CreateBuffer(&cbdesc,nullptr,cb_.GetAddressOf());if(FAILED(hr)){error=hrtext(L"Create constant buffer",hr);return false;}D3D11_BUFFER_DESC brushDesc={};brushDesc.ByteWidth=128u*sizeof(BoneGPU);brushDesc.Usage=D3D11_USAGE_DYNAMIC;brushDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER;brushDesc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;hr=device_->CreateBuffer(&brushDesc,nullptr,brushVB_.GetAddressOf());if(FAILED(hr)){error=hrtext(L"Create brush cursor buffer",hr);return false;}D3D11_RASTERIZER_DESC rs={};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;hr=device_->CreateRasterizerState(&rs,raster_.GetAddressOf());if(FAILED(hr)){error=hrtext(L"Rasterizer",hr);return false;}D3D11_RASTERIZER_DESC wrs=rs;wrs.FillMode=D3D11_FILL_WIREFRAME;hr=device_->CreateRasterizerState(&wrs,wireRaster_.GetAddressOf());if(FAILED(hr)){error=hrtext(L"Wireframe rasterizer",hr);return false;}D3D11_RASTERIZER_DESC ors=rs;ors.CullMode=D3D11_CULL_FRONT;hr=device_->CreateRasterizerState(&ors,outlineRaster_.GetAddressOf());if(FAILED(hr)){error=hrtext(L"Outline rasterizer",hr);return false;}D3D11_DEPTH_STENCIL_DESC ds={};ds.DepthEnable=FALSE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;ds.DepthFunc=D3D11_COMPARISON_ALWAYS;hr=device_->CreateDepthStencilState(&ds,skeletonDepth_.GetAddressOf());if(FAILED(hr)){error=hrtext(L"Skeleton depth state",hr);return false;}D3D11_SAMPLER_DESC sm={};sm.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sm.AddressU=sm.AddressV=sm.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sm.MaxLOD=D3D11_FLOAT32_MAX;hr=device_->CreateSamplerState(&sm,sampler_.GetAddressOf());if(FAILED(hr)){error=hrtext(L"Sampler",hr);return false;}
 D3D11_BLEND_DESC blend{};auto& target=blend.RenderTarget[0];target.BlendEnable=TRUE;
 target.SrcBlend=D3D11_BLEND_SRC_ALPHA;target.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;target.BlendOp=D3D11_BLEND_OP_ADD;
 target.SrcBlendAlpha=D3D11_BLEND_ONE;target.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;target.BlendOpAlpha=D3D11_BLEND_OP_ADD;target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
 hr=device_->CreateBlendState(&blend,alphaBlend_.GetAddressOf());if(FAILED(hr)){error=hrtext(L"Alpha blend state",hr);return false;}
 target.BlendEnable=FALSE;target.RenderTargetWriteMask=0;
 hr=device_->CreateBlendState(&blend,depthOnlyBlend_.GetAddressOf());if(FAILED(hr)){error=hrtext(L"Depth-only blend state",hr);return false;}
 D3D11_DEPTH_STENCIL_DESC alphaDepth{};alphaDepth.DepthEnable=TRUE;alphaDepth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;alphaDepth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
 hr=device_->CreateDepthStencilState(&alphaDepth,alphaDepth_.GetAddressOf());if(FAILED(hr)){error=hrtext(L"Alpha depth state",hr);return false;}
 sm.AddressU=sm.AddressV=sm.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
 hr=device_->CreateSamplerState(&sm,materialSampler_.GetAddressOf());if(FAILED(hr)){error=hrtext(L"Material sampler",hr);return false;}
 if(!makeTexture(MakeR2MaterialLut(),materialLut_)){error=L"Cannot create R2 material table";return false;}
 Mesh cube=MakeCube();if(!SetMesh(cube,error))return false;Image checker=DefaultChecker();PartImages initial;initial.diffuse=checker;initial.hasBump=false;if(!SetPartImages(0,initial)){error=L"Cannot create initial material";return false;}return true;}

bool Renderer::Resize(UINT w,UINT h,std::wstring& error){
 error.clear();if(!swap_||!w||!h)return true;
 if(w==windowWidth_&&h==windowHeight_&&uiRtv_)return true;
 context_->OMSetRenderTargets(0,nullptr,nullptr);uiRtv_.Reset();
 HRESULT hr=swap_->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,0);
 if(FAILED(hr)){
  // ResizeBuffers failure preserves the old swap chain; recover its view.
  std::wstring ignored;createViews(windowWidth_,windowHeight_,ignored);
  error=hrtext(L"Resize UI backbuffer",hr);return false;
 }
 return createViews(w,h,error);
}
void Renderer::BeginUI(){
 if(!context_||!uiRtv_)return;
 ID3D11RenderTargetView* target=uiRtv_.Get();context_->OMSetRenderTargets(1,&target,nullptr);
 const float clear[4]={.055f,.065f,.085f,1.f};context_->ClearRenderTargetView(target,clear);
}
HRESULT Renderer::PresentUI(){return swap_?swap_->Present(1,0):E_FAIL;}

bool Renderer::buildParts(const Mesh& mesh,V3 center,float scale,std::vector<RenderPart>& built,std::wstring& error,bool animate) {
 built.clear();built.reserve(mesh.parts.size());
 auto vector=[](V3 v){return XMVectorSet(v.x,v.y,v.z,0);};
 auto finite=[](V3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
 for(const auto& part:mesh.parts){
  if(!part.valid()||part.indices.size()%3){error=L"Mesh part has incomplete triangles";return false;}
  if(part.vertices.size()>UINT32_MAX/sizeof(GPUVertex)||part.indices.size()>UINT32_MAX/sizeof(uint32_t)){error=L"Mesh part too large";return false;}
  RenderPart rp;rp.skeletal=animate&&part.skeletal;
  std::string shader=part.shader;std::replace(shader.begin(),shader.end(),'/',char(92));
  if(shader=="models\\selflight"||shader=="models_selflight"){rp.emissive=8;rp.emissiveSpecular=8;rp.flatShader=true;}
  else if(shader=="models\\selflightl"||shader=="models_selflightl"){rp.emissive=.5f;rp.flatShader=true;}
  rp.source.resize(part.vertices.size());
  std::vector<XMVECTOR> tan(part.vertices.size(),XMVectorZero()),bit(part.vertices.size(),XMVectorZero()),geometric(part.vertices.size(),XMVectorZero());
  for(size_t k=0;k<part.vertices.size();++k){
   const auto& src=part.vertices[k];
   if(!finite(src.p)||!finite(src.n)||!std::isfinite(src.u)||!std::isfinite(src.v)){error=L"Mesh contains invalid vertex values";return false;}
   auto& dst=rp.source[k];dst.p=src.p;dst.n=src.n;dst.u=src.u;dst.v=src.v;
   dst.bone0=src.bone0;dst.bone1=src.bone1;dst.weight=src.weight;dst.links=src.links;
  }
  for(size_t i=0;i<part.indices.size();i+=3){
   const uint32_t ia=part.indices[i],ib=part.indices[i+1],ic=part.indices[i+2];
   if(ia>=rp.source.size()||ib>=rp.source.size()||ic>=rp.source.size()){error=L"Invalid mesh index";return false;}
   const auto& a=rp.source[ia];const auto& b=rp.source[ib];const auto& c=rp.source[ic];
   const XMVECTOR d1=vector(sub(b.p,a.p)),d2=vector(sub(c.p,a.p)),face=XMVector3Cross(d1,d2);
   for(auto idx:{ia,ib,ic})geometric[idx]+=face;
   const float s1=b.u-a.u,t1=b.v-a.v,s2=c.u-a.u,t2=c.v-a.v,det=s1*t2-s2*t1;
   if(std::fabs(det)<1e-7f)continue;
   const XMVECTOR tv=(d1*t2-d2*t1)/det,bv=(d2*s1-d1*s2)/det;
   for(auto idx:{ia,ib,ic}){tan[idx]+=tv;bit[idx]+=bv;}
  }
  std::vector<GPUVertex> gpu(rp.source.size());V3 lo{},hi{};bool first=true;
  for(size_t k=0;k<rp.source.size();++k){
   auto& src=rp.source[k];const auto& authored=part.vertices[k];
   XMVECTOR nv=vector(src.n);
   if(XMVectorGetX(XMVector3LengthSq(nv))<1e-12f)nv=geometric[k];
   if(XMVectorGetX(XMVector3LengthSq(nv))<1e-12f)nv=XMVectorSet(0,0,1,0);
   nv=XMVector3Normalize(nv);
   XMVECTOR rawT=tan[k],rawB=bit[k];
   if(authored.hasTangentBasis&&finite(authored.tangent)&&finite(authored.binormal)){
    const XMVECTOR at=vector(authored.tangent),ab=vector(authored.binormal);
    if(XMVectorGetX(XMVector3LengthSq(at))>1e-10f&&std::fabs(XMVectorGetX(XMVector3Dot(XMVector3Cross(nv,at),ab)))>1e-8f){rawT=at;rawB=ab;}
   }
   // R4 NormalWithTerrain orthogonalizes T and reconstructs B with the
   // authored handedness. Recomputing a valid exported basis changes shading.
   XMVECTOR tv=rawT-nv*XMVector3Dot(rawT,nv);
   if(XMVectorGetX(XMVector3LengthSq(tv))<1e-10f)tv=XMVector3Cross(std::fabs(XMVectorGetY(nv))<.9f?XMVectorSet(0,1,0,0):XMVectorSet(1,0,0,0),nv);
   tv=XMVector3Normalize(tv);XMVECTOR bv=XMVector3Cross(nv,tv);
   if(XMVectorGetX(XMVector3Dot(bv,rawB))<0)bv=-bv;
   src.n={XMVectorGetX(nv),XMVectorGetY(nv),XMVectorGetZ(nv)};
   src.t={XMVectorGetX(tv),XMVectorGetY(tv),XMVectorGetZ(tv)};
   src.b={XMVectorGetX(bv),XMVectorGetY(bv),XMVectorGetZ(bv)};
   const V3 position=mul(sub(src.p,center),scale);
   gpu[k]={vec(position),vec(src.n),{src.u,src.v},vec(src.t),vec(src.b)};
   if(first){lo=hi=position;first=false;}else{lo.x=std::min(lo.x,position.x);lo.y=std::min(lo.y,position.y);lo.z=std::min(lo.z,position.z);hi.x=std::max(hi.x,position.x);hi.y=std::max(hi.y,position.y);hi.z=std::max(hi.z,position.z);}
  }
  D3D11_BUFFER_DESC bd{};bd.ByteWidth=UINT(gpu.size()*sizeof(GPUVertex));bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
  bd.Usage=rp.skeletal?D3D11_USAGE_DYNAMIC:D3D11_USAGE_DEFAULT;bd.CPUAccessFlags=rp.skeletal?D3D11_CPU_ACCESS_WRITE:0;
  D3D11_SUBRESOURCE_DATA data{gpu.data(),0,0};HRESULT hr=device_->CreateBuffer(&bd,&data,rp.vb.GetAddressOf());
  if(FAILED(hr)){error=hrtext(L"Vertex buffer",hr);return false;}
  bd={};bd.ByteWidth=UINT(part.indices.size()*sizeof(uint32_t));bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_INDEX_BUFFER;
  data={part.indices.data(),0,0};hr=device_->CreateBuffer(&bd,&data,rp.ib.GetAddressOf());
  if(FAILED(hr)){error=hrtext(L"Index buffer",hr);return false;}
  rp.indexCount=UINT(part.indices.size());rp.cpuIndices=part.indices;rp.cpuPositions.resize(gpu.size());rp.cpuUV.resize(gpu.size());
  for(size_t k=0;k<gpu.size();++k){rp.cpuPositions[k]={gpu[k].p.x,gpu[k].p.y,gpu[k].p.z};rp.cpuUV[k]={gpu[k].uv.x,gpu[k].uv.y,0};}
  rp.center=mul(add(lo,hi),.5f);rp.half=mul(sub(hi,lo),.5f);
  if(!rp.skeletal){rp.source.clear();rp.source.shrink_to_fit();}
  built.push_back(std::move(rp));
 }
 return !built.empty();
}

void Renderer::cacheBindTransforms() {
 boneTransforms_.resize(bones_.size());
 for(size_t i=0;i<bones_.size();++i){
  const auto& bone=bones_[i];XMMATRIX matrix=XMMatrixTranslation(bone.position.x,bone.position.y,bone.position.z);
  if(bone.hasBind){
   XMFLOAT4X4 inverse{};std::memcpy(&inverse,bone.inverseBind.data(),sizeof(inverse));
   XMVECTOR determinant;const auto global=XMMatrixInverse(&determinant,XMLoadFloat4x4(&inverse));
   if(std::isfinite(XMVectorGetX(determinant))&&std::fabs(XMVectorGetX(determinant))>1e-12f)matrix=global;
  }
  XMStoreFloat4x4(&boneTransforms_[i],matrix);
 }
}

bool Renderer::SetMesh(Mesh& mesh,std::wstring& error){
 error.clear();if(!device_||!context_||!mesh.valid()){error=L"Empty mesh";return false;}
 bool first=true;V3 lo{},hi{};
 for(const auto& part:mesh.parts)for(const auto& vertex:part.vertices){const V3 p=vertex.p;
  if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)){error=L"Mesh contains invalid vertex values";return false;}
  if(first){lo=hi=p;first=false;}else{lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);}
 }
 if(first){error=L"Mesh has no vertices";return false;}
 const V3 newCenter=mul(add(lo,hi),.5f);const float newScale=2.f/std::max(.001f,std::max({hi.x-lo.x,hi.y-lo.y,hi.z-lo.z}));
 std::vector<RenderPart> built;
 if(!buildParts(mesh,newCenter,newScale,built,error,true))return false;
 ComPtr<ID3D11Buffer> skeletonBuffer;UINT skeletonCount=0;
 if(!mesh.bones.empty()){
  if(mesh.bones.size()>UINT32_MAX/(2*sizeof(BoneGPU))){error=L"Skeleton is too large";return false;}
  const UINT maxVerts=UINT(std::max<size_t>(2,mesh.bones.size()*2));std::vector<BoneGPU> lines(maxVerts);
  for(size_t i=0;i<mesh.bones.size();++i){const Bone& bone=mesh.bones[i];if(bone.parent<0)continue;
   if(size_t(bone.parent)>=mesh.bones.size()){error=L"Invalid skeleton parent";return false;}
   auto position=[&](const V3& p){return vec(mul(sub(p,newCenter),newScale));};
   lines[skeletonCount++]={position(mesh.bones[size_t(bone.parent)].position),{1.f,.75f,.15f,1.f}};
   lines[skeletonCount++]={position(bone.position),{.15f,1.f,.55f,1.f}};
  }
  D3D11_BUFFER_DESC bd{};bd.ByteWidth=maxVerts*sizeof(BoneGPU);bd.Usage=D3D11_USAGE_DYNAMIC;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  D3D11_SUBRESOURCE_DATA data{lines.data(),0,0};const HRESULT hr=device_->CreateBuffer(&bd,&data,skeletonBuffer.GetAddressOf());
  if(FAILED(hr)){error=hrtext(L"Skeleton vertex buffer",hr);return false;}
 }
 // Commit the new model only after all GPU resources exist. Camera, light and
 // model rotation belong to the scene, so opening a model never resets them.
 parts_=std::move(built);bones_=std::move(mesh.bones);motions_=std::move(mesh.motions);
 modelCenter_=newCenter;modelScale_=newScale;skeletonVB_=std::move(skeletonBuffer);skeletonVertexCount_=skeletonCount;
 selectedPrimary_=hoverPart_=-1;ClearBrushCursor();ClearAttachments();cacheBindTransforms();
 for(auto& part:mesh.parts){part.vertices.clear();part.vertices.shrink_to_fit();part.indices.clear();part.indices.shrink_to_fit();}
 return !parts_.empty();
}

bool Renderer::uploadBindPose(std::wstring* error){cacheBindTransforms();for(auto& part:parts_){if(!part.skeletal)continue;std::vector<GPUVertex> gpu(part.source.size());for(size_t k=0;k<part.source.size();++k){const auto& s=part.source[k];V3 p=mul(sub(s.p,modelCenter_),modelScale_);gpu[k].p=vec(p);gpu[k].n=vec(s.n);gpu[k].uv={s.u,s.v};gpu[k].t=vec(s.t);gpu[k].b=vec(s.b);}D3D11_MAPPED_SUBRESOURCE map{};HRESULT hr=context_->Map(part.vb.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map);if(FAILED(hr)){if(error)*error=hrtext(L"Map skeletal vertex buffer",hr);return false;}std::memcpy(map.pData,gpu.data(),gpu.size()*sizeof(GPUVertex));context_->Unmap(part.vb.Get(),0);part.alphaSortValid=false;
  for(size_t k=0;k<gpu.size();++k)part.cpuPositions[k]={gpu[k].p.x,gpu[k].p.y,gpu[k].p.z};
  if(!part.cpuPositions.empty()){V3 lo=part.cpuPositions.front(),hi=lo;for(const auto& p:part.cpuPositions){lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);}part.center=mul(add(lo,hi),.5f);part.half=mul(sub(hi,lo),.5f);}
 }
 if(skeletonVB_&&!bones_.empty()){std::vector<BoneGPU> lines;lines.reserve(bones_.size()*2);for(size_t i=0;i<bones_.size();++i){const Bone& b=bones_[i];if(b.parent<0||b.parent>=int(bones_.size()))continue;auto cv=[&](const V3& p){V3 q=mul(sub(p,modelCenter_),modelScale_);return XMFLOAT3{q.x,q.y,q.z};};lines.push_back({cv(bones_[size_t(b.parent)].position),{1.f,.75f,.15f,1.f}});lines.push_back({cv(b.position),{.15f,1.f,.55f,1.f}});}skeletonVertexCount_=UINT(lines.size());if(!lines.empty()){D3D11_MAPPED_SUBRESOURCE map{};HRESULT hr=context_->Map(skeletonVB_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map);if(FAILED(hr)){if(error)*error=hrtext(L"Map skeleton buffer",hr);return false;}std::memcpy(map.pData,lines.data(),lines.size()*sizeof(BoneGPU));context_->Unmap(skeletonVB_.Get(),0);}}
 return true;}

bool Renderer::SetAnimationPose(size_t motionIndex,float seconds,bool loop,std::wstring& error){
 error.clear();if(motionIndex>=motions_.size()){error=L"Animation index out of range";return false;}
 const Motion& motion=motions_[motionIndex];if(motion.frames==0||motion.tracks.size()!=bones_.size()||!std::isfinite(seconds)||!std::isfinite(motion.fps)||motion.fps<=0.f){error=L"Animation does not match skeleton";return false;}
 double frameTime=std::max(0.0,double(seconds))*double(motion.fps);const double maxFrame=double(motion.frames);
 if(loop&&maxFrame>0)frameTime=std::fmod(frameTime,maxFrame);else frameTime=std::min(frameTime,std::max(0.0,maxFrame-1.0));
 const uint32_t f0=uint32_t(std::floor(frameTime));const uint32_t f1=loop?(f0+1)%motion.frames:std::min(f0+1,motion.frames-1);const float delta=float(frameTime-double(f0));
 std::vector<XRMat> global(bones_.size(),xrIdentity()),skin(bones_.size(),xrIdentity());std::vector<uint8_t> done(bones_.size(),0),visiting(bones_.size(),0);
 auto solve=[&](auto&& self,size_t i)->bool{
  if(done[i])return true;if(visiting[i])return false;visiting[i]=1;
  const MotionTrack& tr=motion.tracks[i];const Quat q=sampleQ(tr,f0,f1,delta);const V3 t=sampleT(tr,f0,f1,delta);const XRMat local=xrFromQuat(q,t);
  const int parent=bones_[i].parent;
  if(parent>=0){if(size_t(parent)>=bones_.size()||!self(self,size_t(parent)))return false;global[i]=xrMul43(global[size_t(parent)],local);}else global[i]=local;
  // Exact CKinematicsAnimated::CLBone order:
  // mRenderTransform.mul_43(mTransform, bd->m2b_transform).
  skin[i]=xrMul43(global[i],xrFromArray(bones_[i].inverseBind));visiting[i]=0;done[i]=1;return true;
 };
 for(size_t i=0;i<bones_.size();++i)if(!solve(solve,i)){error=L"Animation bone hierarchy contains a cycle";return false;}
 for(auto& part:parts_){if(!part.skeletal)continue;D3D11_MAPPED_SUBRESOURCE map{};HRESULT hr=context_->Map(part.vb.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map);if(FAILED(hr)){error=hrtext(L"Map animated vertex buffer",hr);return false;}auto* gpu=reinterpret_cast<GPUVertex*>(map.pData);V3 lo{},hi{};bool first=true;
  for(size_t k=0;k<part.source.size();++k){const auto& s=part.source[k];auto transform=[&](V3 v,bool point,uint16_t bone)->V3{if(bone>=skin.size())return v;return point?xrPoint(skin[bone],v):xrDir(skin[bone],v);};V3 p,n,t,b;
   if(s.links==1||(s.links==2&&s.bone0==s.bone1)){p=transform(s.p,true,s.bone0);n=transform(s.n,false,s.bone0);t=transform(s.t,false,s.bone0);b=transform(s.b,false,s.bone0);}
   else if(s.links==2){const V3 p0=transform(s.p,true,s.bone0),p1=transform(s.p,true,s.bone1),n0=transform(s.n,false,s.bone0),n1=transform(s.n,false,s.bone1),t0=transform(s.t,false,s.bone0),t1=transform(s.t,false,s.bone1),b0=transform(s.b,false,s.bone0),b1=transform(s.b,false,s.bone1);const float w=std::clamp(s.weight,0.f,1.f);p=add(mul(p0,1-w),mul(p1,w));n=add(mul(n0,1-w),mul(n1,w));t=add(mul(t0,1-w),mul(t1,w));b=add(mul(b0,1-w),mul(b1,w));}
   else{p=s.p;n=s.n;t=s.t;b=s.b;}
   n=normalize(n);t=normalize(t);b=normalize(b);p=mul(sub(p,modelCenter_),modelScale_);gpu[k].p=vec(p);gpu[k].n=vec(n);gpu[k].uv={s.u,s.v};gpu[k].t=vec(t);gpu[k].b=vec(b);part.cpuPositions[k]=p;
   if(first){lo=hi=p;first=false;}else{lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);}
  }
  context_->Unmap(part.vb.Get(),0);part.alphaSortValid=false;if(!first){part.center={(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,(lo.z+hi.z)*.5f};part.half={(hi.x-lo.x)*.5f,(hi.y-lo.y)*.5f,(hi.z-lo.z)*.5f};}
 }
 if(skeletonVB_){std::vector<BoneGPU> lines;lines.reserve(bones_.size()*2);for(size_t i=0;i<bones_.size();++i){const int parent=bones_[i].parent;if(parent<0||parent>=int(bones_.size()))continue;auto pos=[&](size_t id){V3 q{global[id].m[3][0],global[id].m[3][1],global[id].m[3][2]};q=mul(sub(q,modelCenter_),modelScale_);return XMFLOAT3{q.x,q.y,q.z};};lines.push_back({pos(size_t(parent)),{1.f,.75f,.15f,1.f}});lines.push_back({pos(i),{.15f,1.f,.55f,1.f}});}skeletonVertexCount_=UINT(lines.size());if(!lines.empty()){D3D11_MAPPED_SUBRESOURCE map{};HRESULT hr=context_->Map(skeletonVB_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map);if(FAILED(hr)){error=hrtext(L"Map animated skeleton buffer",hr);return false;}std::memcpy(map.pData,lines.data(),lines.size()*sizeof(BoneGPU));context_->Unmap(skeletonVB_.Get(),0);}}
 boneTransforms_.resize(global.size());
 for(size_t i=0;i<global.size();++i)std::memcpy(&boneTransforms_[i],global[i].m,sizeof(XMFLOAT4X4));
 return true;
}

bool Renderer::SetBindPose(std::wstring* error){return uploadBindPose(error);}
bool Renderer::makeTexture(const Image& im,ComPtr<ID3D11ShaderResourceView>& out,ComPtr<ID3D11Texture2D>* textureOut,const std::vector<Image>* mips){
 if(!im.valid()||!device_)return false;
 std::vector<D3D11_SUBRESOURCE_DATA> data;
 if(mips){uint32_t w=im.w,h=im.h;for(const auto& mip:*mips){if(!mip.valid()||mip.w!=w||mip.h!=h)return false;data.push_back({mip.pixels.data(),mip.w*4,0});w=std::max(1u,w/2);h=std::max(1u,h/2);}if(data.empty())return false;}
 D3D11_TEXTURE2D_DESC d={};d.Width=im.w;d.Height=im.h;d.MipLevels=mips?UINT(data.size()):0;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;
 d.BindFlags=D3D11_BIND_SHADER_RESOURCE|(mips?0:D3D11_BIND_RENDER_TARGET);d.MiscFlags=mips?0:D3D11_RESOURCE_MISC_GENERATE_MIPS;
 ComPtr<ID3D11Texture2D> texture;HRESULT hr=device_->CreateTexture2D(&d,mips?data.data():nullptr,texture.GetAddressOf());if(FAILED(hr))return false;
 ComPtr<ID3D11ShaderResourceView> srv;hr=device_->CreateShaderResourceView(texture.Get(),nullptr,srv.GetAddressOf());if(FAILED(hr))return false;
 if(!mips){context_->UpdateSubresource(texture.Get(),0,nullptr,im.pixels.data(),im.w*4,0);context_->GenerateMips(srv.Get());}
 out=srv;if(textureOut)*textureOut=texture;return true;
}
bool Renderer::SetPartBumpMipImages(size_t part,const std::vector<Image>& bump,const std::vector<Image>& bumpX){
 if(part>=parts_.size()||bump.empty()||bump.size()!=bumpX.size()||bump[0].w!=bumpX[0].w||bump[0].h!=bumpX[0].h)return false;
 ComPtr<ID3D11Texture2D> textures[2];ComPtr<ID3D11ShaderResourceView> views[2];
 if(!makeTexture(bump[0],views[0],&textures[0],&bump)||!makeTexture(bumpX[0],views[1],&textures[1],&bumpX))return false;
 for(int i=0;i<2;++i){parts_[part].textureResources[i+1]=std::move(textures[i]);parts_[part].textures[i+1]=std::move(views[i]);}parts_[part].hasBump=true;return true;
}
bool Renderer::SetPartImages(size_t part,const PartImages& images){
 return part<parts_.size()&&setPartImages(parts_[part],images);
}
bool Renderer::setPartImages(RenderPart& rp,const PartImages& images){
 if(!images.diffuse.valid())return false;
 const bool hasPair=images.bump.valid()&&images.bumpX.valid();
 if(images.hasBump&&(!hasPair||images.bump.w!=images.bumpX.w||images.bump.h!=images.bumpX.h))return false;
 const AlphaProfile profile=InspectAlpha(images.diffuse);std::shared_ptr<GrayImage> opacity;
 if(profile.transparent){opacity=std::make_shared<GrayImage>();opacity->w=images.diffuse.w;opacity->h=images.diffuse.h;opacity->pixels.reserve(images.diffuse.pixels.size());for(const auto& pixel:images.diffuse.pixels)opacity->pixels.push_back(pixel.a);}
 ComPtr<ID3D11Texture2D> textures[3];ComPtr<ID3D11ShaderResourceView> views[3];
 if(!makeTexture(images.diffuse,views[0],&textures[0]))return false;
 Image flat,errorMap;flat.w=flat.h=errorMap.w=errorMap.h=4;
 flat.pixels.assign(16,Pixel{0,255,128,128});errorMap.pixels.assign(16,Pixel{128,128,128,128});
 if(!makeTexture(hasPair?images.bump:flat,views[1],&textures[1])||!makeTexture(hasPair?images.bumpX:errorMap,views[2],&textures[2]))return false;
 // Commit all channels together. A failed allocation cannot mix a new normal
 // map with an old correction map, or corrupt another part sharing the SRV.
 for(int i=0;i<3;++i){rp.textureResources[i]=std::move(textures[i]);rp.textures[i]=std::move(views[i]);}
 rp.hasBump=images.hasBump&&hasPair;rp.alpha=profile;rp.opacity=std::move(opacity);return true;
}
bool Renderer::SetPartBumpImages(size_t part,const Image& bump,const Image& bumpX,bool hasBump){
 if(part>=parts_.size()||!bump.valid()||!bumpX.valid()||bump.w!=bumpX.w||bump.h!=bumpX.h)return false;
 ComPtr<ID3D11Texture2D> normalTexture,errorTexture;ComPtr<ID3D11ShaderResourceView> normalView,errorView;
 if(!makeTexture(bump,normalView,&normalTexture)||!makeTexture(bumpX,errorView,&errorTexture))return false;
 auto& rp=parts_[part];rp.textureResources[1]=std::move(normalTexture);rp.textures[1]=std::move(normalView);
 rp.textureResources[2]=std::move(errorTexture);rp.textures[2]=std::move(errorView);rp.hasBump=hasBump;return true;
}
bool Renderer::CopyPartMaterial(size_t dst,size_t src){if(dst>=parts_.size()||src>=parts_.size()||!parts_[src].textures[0]||(parts_[src].hasBump&&(!parts_[src].textures[1]||!parts_[src].textures[2])))return false;for(int i=0;i<3;++i){parts_[dst].textures[i]=parts_[src].textures[i];parts_[dst].textureResources[i]=parts_[src].textureResources[i];}parts_[dst].hasBump=parts_[src].hasBump;parts_[dst].alpha=parts_[src].alpha;parts_[dst].opacity=parts_[src].opacity;return bool(parts_[dst].textures[0]);}
bool Renderer::SetImages(const Image& diffuse,const Image& bump,const Image& bumpX){
 if(parts_.empty())return false;PartImages p{diffuse,bump,bumpX,bump.valid()&&bumpX.valid()};
 if(!SetPartImages(0,p))return false;for(size_t i=1;i<parts_.size();++i)CopyPartMaterial(i,0);return true;
}
bool Renderer::SetBumpImages(const Image& bump,const Image& bumpX,bool hasBump){
 if(parts_.empty()||!bump.valid()||!bumpX.valid()||bump.w!=bumpX.w||bump.h!=bumpX.h)return false;
 ComPtr<ID3D11Texture2D> normalTexture,errorTexture;ComPtr<ID3D11ShaderResourceView> normalView,errorView;
 if(!makeTexture(bump,normalView,&normalTexture)||!makeTexture(bumpX,errorView,&errorTexture))return false;
 const bool selected=HasSelection();for(auto& part:parts_){if(selected&&!part.selected)continue;
  part.textureResources[1]=normalTexture;part.textures[1]=normalView;part.textureResources[2]=errorTexture;part.textures[2]=errorView;part.hasBump=hasBump;
 }return true;
}
bool Renderer::HasAttachment(size_t bone)const {
 return std::any_of(attachments_.begin(),attachments_.end(),[&](const Attachment& a){return a.bone==bone;});
}
void Renderer::RemoveAttachment(size_t bone) {
 attachments_.erase(std::remove_if(attachments_.begin(),attachments_.end(),[&](const Attachment& a){return a.bone==bone;}),attachments_.end());
}
bool Renderer::SetAttachment(size_t bone,const Mesh& mesh,const std::vector<PartImages>& images,const AttachmentTransform& transform,std::wstring& error) {
 error.clear();
 if(bone>=bones_.size()||bone>=boneTransforms_.size()){error=L"Attachment bone does not exist in the current model";return false;}
 if(!device_||!context_||!mesh.valid()||images.size()!=mesh.parts.size()){error=L"Attachment requires a mesh and one material per part";return false;}
 if(!ValidAttachmentTransform(transform)){error=L"Attachment transform is outside supported bounds (scale 0.0001..1000)";return false;}
 Attachment built;built.bone=bone;built.transform=transform;
 // A prop is a rigid preview in its authored bind pose. Its skeleton, if any,
 // does not replace the actor skeleton and never enters material editing.
 if(!buildParts(mesh,{},1.f,built.parts,error,false))return false;
 for(size_t i=0;i<built.parts.size();++i)if(!setPartImages(built.parts[i],images[i])){error=L"Cannot upload attachment material";return false;}
 for(auto& current:attachments_)if(current.bone==bone){current=std::move(built);return true;}
 attachments_.push_back(std::move(built));return true;
}
bool Renderer::SetAttachmentTransform(size_t bone,const AttachmentTransform& transform,std::wstring& error) {
 error.clear();if(!ValidAttachmentTransform(transform)){error=L"Invalid attachment transform";return false;}
 for(auto& attachment:attachments_)if(attachment.bone==bone){attachment.transform=transform;return true;}
 error=L"No preview mesh is attached to this bone";return false;
}
void Renderer::SetAttachmentMaterialClass(size_t bone,size_t part,float material) {
 if(!std::isfinite(material))return;
 for(auto& attachment:attachments_)if(attachment.bone==bone&&part<attachment.parts.size())attachment.parts[part].materialClass=std::clamp(material,0.f,3.f);
}
XMMATRIX Renderer::attachmentWorld(const Attachment& attachment)const {
 if(attachment.bone>=boneTransforms_.size())return XMMatrixIdentity();
 const auto& t=attachment.transform;const float degrees=XM_PI/180.f;
 const XMMATRIX local=XMMatrixScaling(t.scale,t.scale,t.scale)
     *XMMatrixRotationX(t.rotation.x*degrees)*XMMatrixRotationY(t.rotation.y*degrees)*XMMatrixRotationZ(t.rotation.z*degrees)
     *XMMatrixTranslation(t.position.x,t.position.y,t.position.z);
 const XMMATRIX normalizeModel=XMMatrixTranslation(-modelCenter_.x,-modelCenter_.y,-modelCenter_.z)*XMMatrixScaling(modelScale_,modelScale_,modelScale_);
 return local*XMLoadFloat4x4(&boneTransforms_[attachment.bone])*normalizeModel*XMMatrixRotationX(modelPitch_)*XMMatrixRotationY(modelYaw_);
}
std::vector<Renderer::BonePoint> Renderer::BonePoints()const {
 std::vector<BonePoint> points;if(!width_||!height_||boneTransforms_.size()!=bones_.size())return points;
 const V3 eye=camera_.eye(),up=camera_.up();
 const XMMATRIX view=XMMatrixLookAtLH(XMVectorSet(eye.x,eye.y,eye.z,1),XMVectorSet(camera_.pivot.x,camera_.pivot.y,camera_.pivot.z,1),XMVectorSet(up.x,up.y,up.z,0));
 const XMMATRIX projection=XMMatrixPerspectiveFovLH(XM_PIDIV4,float(width_)/float(height_),.005f,100.f);
 const XMMATRIX model=XMMatrixTranslation(-modelCenter_.x,-modelCenter_.y,-modelCenter_.z)*XMMatrixScaling(modelScale_,modelScale_,modelScale_)*XMMatrixRotationX(modelPitch_)*XMMatrixRotationY(modelYaw_);
 const XMMATRIX transform=model*view*projection;points.reserve(bones_.size());
 for(size_t i=0;i<bones_.size();++i){
  const auto& bone=boneTransforms_[i];XMFLOAT4 clip;
  XMStoreFloat4(&clip,XMVector4Transform(XMVectorSet(bone._41,bone._42,bone._43,1),transform));
  BonePoint point;point.index=i;point.name=bones_[i].name;
  if(std::isfinite(clip.w)&&clip.w>1e-6f){
   const float x=clip.x/clip.w,y=clip.y/clip.w,z=clip.z/clip.w;
   point.x=(x*.5f+.5f)*width_;point.y=(.5f-y*.5f)*height_;
   point.visible=std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&x>=-1&&x<=1&&y>=-1&&y<=1&&z>=0&&z<=1;
  }
  points.push_back(std::move(point));
 }
 return points;
}
void Renderer::SetEnvironmentLighting(V3 environment,float hemisphere,float exposure,float glossFactor) {
 auto value=[](float v,float fallback,float high){return std::isfinite(v)?std::clamp(v,0.f,high):fallback;};
 environment_={value(environment.x,0,4),value(environment.y,0,4),value(environment.z,0,4)};
 hemisphere_=value(hemisphere,1,1);exposure_=value(exposure,1,4);r4GlossFactor_=value(glossFactor,1,4);
}
void Renderer::SetColor(int which,COLORREF c){float* a=(which==0?tint_:which==1?light_:ambient_);a[0]=GetRValue(c)/255.f;a[1]=GetGValue(c)/255.f;a[2]=GetBValue(c)/255.f;}
COLORREF Renderer::GetColor(int which)const{const float* a=(which==0?tint_:which==1?light_:ambient_);return RGB(int(a[0]*255),int(a[1]*255),int(a[2]*255));}
void Renderer::OrbitCamera(float dx,float dy){camera_.look(dx,dy);}
void Renderer::RotateModel(float dx,float dy){modelYaw_=std::remainder(modelYaw_+dx*.012f,XM_2PI);modelPitch_=std::clamp(modelPitch_+dy*.012f,-XM_PIDIV2,XM_PIDIV2);}
void Renderer::PanVertical(float dy){camera_.pan(0,dy*2.5f);}
void Renderer::FrameSelection(){
 const bool selected=HasSelection();bool first=true;V3 lo{},hi{};
 const XMMATRIX world=XMMatrixRotationX(modelPitch_)*XMMatrixRotationY(modelYaw_);
 for(const auto& part:parts_){if(!part.visible||(selected&&!part.selected))continue;
  for(const auto& p:part.cpuPositions){XMFLOAT3 v;XMStoreFloat3(&v,XMVector3TransformCoord(XMVectorSet(p.x,p.y,p.z,1),world));
   if(first){lo=hi={v.x,v.y,v.z};first=false;}else{lo.x=std::min(lo.x,v.x);lo.y=std::min(lo.y,v.y);lo.z=std::min(lo.z,v.z);hi.x=std::max(hi.x,v.x);hi.y=std::max(hi.y,v.y);hi.z=std::max(hi.z,v.z);}}
 }
 if(first)return;
 camera_.frame(mul(add(lo,hi),.5f),len(sub(hi,lo))*.5f,float(width_)/std::max(1.f,float(height_)));
}
void Renderer::OrbitLight(float dx,float dy){lightYaw_+=dx*.01f;lightPitch_=std::clamp(lightPitch_+dy*.01f,-1.42f,1.42f);}
void Renderer::SetLightOrbitDegrees(float azimuth,float elevation){lightYaw_=azimuth*XM_PI/180.f;lightPitch_=std::clamp(elevation,-80.f,80.f)*XM_PI/180.f;}
float Renderer::LightAzimuthDegrees()const{float d=lightYaw_*180.f/XM_PI;d=std::fmod(d,360.f);if(d<0)d+=360.f;return d;}
float Renderer::LightElevationDegrees()const{return lightPitch_*180.f/XM_PI;}
void Renderer::Zoom(float steps){camera_.zoom(steps);}
void Renderer::ResetView(){camera_.reset();modelYaw_=0.f;modelPitch_=0.f;}
void Renderer::Reset(){ResetView();lightYaw_=0.9f;lightPitch_=0.65f;}
void Renderer::ClearSelection(){for(auto& p:parts_)p.selected=false;selectedPrimary_=-1;}
bool Renderer::HasSelection()const{for(const auto& p:parts_)if(p.selected&&p.visible)return true;return false;}
void Renderer::SetPartVisible(size_t part,bool visible){if(part>=parts_.size())return;parts_[part].visible=visible;if(!visible&&hoverPart_==int(part))hoverPart_=-1;if(!visible&&parts_[part].selected){parts_[part].selected=false;if(selectedPrimary_==int(part))selectedPrimary_=-1;}}
bool Renderer::PartVisible(size_t part)const{return part<parts_.size()&&parts_[part].visible;}
void Renderer::SelectPart(int part,bool withMirror){ClearSelection();if(part<0||size_t(part)>=parts_.size()||!parts_[size_t(part)].visible)return;parts_[size_t(part)].selected=true;selectedPrimary_=part;if(!withMirror)return;const auto& src=parts_[size_t(part)];V3 mirror{-src.center.x,src.center.y,src.center.z};if(std::fabs(src.center.x)<=.025f)return;float bestMirror=.14f;int mirrorIndex=-1;for(size_t j=0;j<parts_.size();++j){if(int(j)==part||!parts_[j].visible)continue;const auto& q=parts_[j];float cd=len(sub(q.center,mirror));float ed=len(sub(q.half,src.half));float score=cd+ed*.5f;if(score<bestMirror){bestMirror=score;mirrorIndex=int(j);}}if(mirrorIndex>=0)parts_[size_t(mirrorIndex)].selected=true;}
bool Renderer::PickUV(int screenX,int screenY,PickHit& out){
 out={};out.part=-1;if(parts_.empty()||!width_||!height_||screenX<0||screenY<0||UINT(screenX)>=width_||UINT(screenY)>=height_)return false;
 const V3 cameraEye=camera_.eye();XMVECTOR target=XMVectorSet(camera_.pivot.x,camera_.pivot.y,camera_.pivot.z,1);XMVECTOR eye=XMVectorSet(cameraEye.x,cameraEye.y,cameraEye.z,1);
 XMMATRIX view=XMMatrixLookAtLH(eye,target,XMVectorSet(camera_.up().x,camera_.up().y,camera_.up().z,0));XMMATRIX proj=XMMatrixPerspectiveFovLH(XM_PIDIV4,float(width_)/std::max(1.f,float(height_)),.005f,100.f);XMMATRIX world=XMMatrixRotationX(modelPitch_)*XMMatrixRotationY(modelYaw_);
 XMVECTOR n=XMVector3Unproject(XMVectorSet(float(screenX),float(screenY),0,1),0,0,float(width_),float(height_),0,1,proj,view,world);XMVECTOR f=XMVector3Unproject(XMVectorSet(float(screenX),float(screenY),1,1),0,0,float(width_),float(height_),0,1,proj,view,world);XMFLOAT3 nf,ff;XMStoreFloat3(&nf,n);XMStoreFloat3(&ff,f);V3 ro{nf.x,nf.y,nf.z},rd=normalize(V3{ff.x-nf.x,ff.y-nf.y,ff.z-nf.z});
 struct Candidate{float nearT;size_t part;};std::vector<Candidate> candidates;candidates.reserve(parts_.size());for(size_t pi=0;pi<parts_.size();++pi){const auto& p=parts_[pi];if(!p.visible||(alphaEnabled_&&p.alpha.invisible))continue;float nearT=0;if(rayAabb(ro,rd,p.center,p.half,nearT))candidates.push_back({nearT,pi});}std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.nearT<b.nearT;});
 float best=1e30f;
 for(const auto& candidate:candidates){if(candidate.nearT>=best)break;const size_t pi=candidate.part;const auto& p=parts_[pi];for(size_t k=0;k+2<p.cpuIndices.size();k+=3){uint32_t ia=p.cpuIndices[k],ib=p.cpuIndices[k+1],ic=p.cpuIndices[k+2];if(ia>=p.cpuPositions.size()||ib>=p.cpuPositions.size()||ic>=p.cpuPositions.size()||ia>=p.cpuUV.size()||ib>=p.cpuUV.size()||ic>=p.cpuUV.size())continue;float t=0,bu=0,bv=0;if(!rayTriangle(ro,rd,p.cpuPositions[ia],p.cpuPositions[ib],p.cpuPositions[ic],t,&bu,&bv)||t>=best)continue;const float bw=1.f-bu-bv;const float hitU=p.cpuUV[ia].x*bw+p.cpuUV[ib].x*bu+p.cpuUV[ic].x*bv,hitV=p.cpuUV[ia].y*bw+p.cpuUV[ib].y*bu+p.cpuUV[ic].y*bv;const AlphaMode mode=alphaMode(p);if(mode!=AlphaMode::Opaque&&p.opacity&&SampleOpacity(*p.opacity,hitU,hitV)<AlphaThreshold(mode,alphaCutoff_))continue;best=t;const V3 p0=p.cpuPositions[ia],p1=p.cpuPositions[ib],p2=p.cpuPositions[ic],e1=sub(p1,p0),e2=sub(p2,p0);const V3 uv0=p.cpuUV[ia],uv1=p.cpuUV[ib],uv2=p.cpuUV[ic];const float du1=uv1.x-uv0.x,dv1=uv1.y-uv0.y,du2=uv2.x-uv0.x,dv2=uv2.y-uv0.y,det=du1*dv2-du2*dv1;out.part=int(pi);out.u=uv0.x*bw+uv1.x*bu+uv2.x*bv;out.v=uv0.y*bw+uv1.y*bu+uv2.y*bv;out.position=add(ro,mul(rd,t));out.normal=normalize(cross(e1,e2));if(std::fabs(det)>1e-8f){const float inv=1.f/det;out.du=mul(sub(mul(e1,dv2),mul(e2,dv1)),inv);out.dv=mul(sub(mul(e2,du1),mul(e1,du2)),inv);}else{out.du=normalize(e1);out.dv=normalize(cross(out.normal,out.du));}}
 }
 return out.part>=0;
}
void Renderer::SetBrushCursor(const PickHit& hit,float radiusU,float radiusV){
 if(!brushVB_||hit.part<0||radiusU<=0.f||radiusV<=0.f){ClearBrushCursor();return;}constexpr int segments=64;std::array<BoneGPU,segments*2> v{};const V3 lift=mul(hit.normal,.003f);const XMFLOAT4 color{1.f,.88f,.18f,1.f};for(int i=0;i<segments;++i){const float a0=float(i)*XM_2PI/float(segments),a1=float(i+1)*XM_2PI/float(segments);auto point=[&](float a){return add(add(add(hit.position,lift),mul(hit.du,std::cos(a)*radiusU)),mul(hit.dv,std::sin(a)*radiusV));};V3 p0=point(a0),p1=point(a1);v[size_t(i)*2]={vec(p0),color};v[size_t(i)*2+1]={vec(p1),color};}D3D11_MAPPED_SUBRESOURCE map{};if(SUCCEEDED(context_->Map(brushVB_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map))){std::memcpy(map.pData,v.data(),sizeof(v));context_->Unmap(brushVB_.Get(),0);brushVertexCount_=UINT(v.size());}else brushVertexCount_=0;
}
void Renderer::ClearBrushCursor(){brushVertexCount_=0;}
int Renderer::PickGeometry(int screenX,int screenY){PickHit hit;if(!PickUV(screenX,screenY,hit))return -1;SelectPart(hit.part,false);hoverPart_=-1;return hit.part;}
ID3D11Buffer* Renderer::sortedAlphaIndices(RenderPart& part,const XMMATRIX& modelView){
 XMFLOAT4X4 transform;XMStoreFloat4x4(&transform,modelView);
 if(part.alphaSortValid&&part.sortedIB&&std::memcmp(&transform,&part.alphaSortView,sizeof(transform))==0)return part.sortedIB.Get();
 struct Triangle{float depth;size_t index;};std::vector<Triangle> order;order.reserve(part.cpuIndices.size()/3);
 for(size_t i=0;i+2<part.cpuIndices.size();i+=3){V3 center{};bool valid=true;
  for(size_t k=0;k<3;++k){const uint32_t at=part.cpuIndices[i+k];if(at>=part.cpuPositions.size()){valid=false;break;}center=add(center,part.cpuPositions[at]);}
  if(!valid)return part.ib.Get();center=mul(center,1.f/3.f);
  const float depth=XMVectorGetZ(XMVector3TransformCoord(XMVectorSet(center.x,center.y,center.z,1),modelView));order.push_back({depth,i});
 }
 std::stable_sort(order.begin(),order.end(),[](const Triangle& a,const Triangle& b){return a.depth>b.depth;});
 std::vector<uint32_t> indices;indices.reserve(part.cpuIndices.size());for(const auto& triangle:order)for(size_t k=0;k<3;++k)indices.push_back(part.cpuIndices[triangle.index+k]);
 if(!part.sortedIB){D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(indices.size()*sizeof(uint32_t));desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_INDEX_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  if(FAILED(device_->CreateBuffer(&desc,nullptr,part.sortedIB.GetAddressOf())))return part.ib.Get();}
 D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(context_->Map(part.sortedIB.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map)))return part.ib.Get();
 std::memcpy(map.pData,indices.data(),indices.size()*sizeof(uint32_t));context_->Unmap(part.sortedIB.Get(),0);part.alphaSortView=transform;part.alphaSortValid=true;return part.sortedIB.Get();
}
void Renderer::DrawScene(){drawScene(false,false);}
void Renderer::drawScene(bool clean,bool transparentBackground){
 if(!device_||!context_||!rtv_||!dsv_||!width_||!height_)return;
 // The previous UI frame sampled sceneView_. Unbind it before reusing a target.
 ID3D11ShaderResourceView* empty[3]{};context_->PSSetShaderResources(0,3,empty);
 context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
 const float back[4]={transparentBackground?0.f:background_[0],transparentBackground?0.f:background_[1],transparentBackground?0.f:background_[2],transparentBackground?0.f:1.f};
 context_->ClearRenderTargetView(rtv_.Get(),back);context_->ClearDepthStencilView(dsv_.Get(),D3D11_CLEAR_DEPTH,1.f,0);
 ID3D11RenderTargetView* target=rtv_.Get();context_->OMSetRenderTargets(1,&target,dsv_.Get());
 const D3D11_VIEWPORT viewport={0,0,float(width_),float(height_),0,1};context_->RSSetViewports(1,&viewport);
 const V3 cameraEye=camera_.eye(),up=camera_.up();
 const XMVECTOR eye=XMVectorSet(cameraEye.x,cameraEye.y,cameraEye.z,1);
 const XMMATRIX view=XMMatrixLookAtLH(eye,XMVectorSet(camera_.pivot.x,camera_.pivot.y,camera_.pivot.z,1),XMVectorSet(up.x,up.y,up.z,0));
 const XMMATRIX projection=XMMatrixPerspectiveFovLH(XM_PIDIV4,float(width_)/float(height_),.005f,100.f);
 const XMMATRIX world=XMMatrixRotationX(modelPitch_)*XMMatrixRotationY(modelYaw_);
 CB cb{};cb.eye={cameraEye.x,cameraEye.y,cameraEye.z,1};
 cb.ld={cosf(lightPitch_)*sinf(lightYaw_),sinf(lightPitch_),cosf(lightPitch_)*cosf(lightYaw_),0};
 cb.lc={light_[0]*lightIntensity_,light_[1]*lightIntensity_,light_[2]*lightIntensity_,1};
 cb.ambient={ambient_[0],ambient_[1],ambient_[2],1};cb.tint={tint_[0],tint_[1],tint_[2],1};
 cb.outline={1,.65f,.16f,2};cb.viewport={1.f/float(width_),1.f/float(height_),0,0};
 cb.environment={environment_.x,environment_.y,environment_.z,hemisphere_};
 auto matrices=[&](const XMMATRIX& model){XMStoreFloat4x4(&cb.wvp,model*view*projection);XMStoreFloat4x4(&cb.world,model);};
 matrices(world);
 ID3D11Buffer* buffer=cb_.Get();context_->VSSetConstantBuffers(0,1,&buffer);context_->PSSetConstantBuffers(0,1,&buffer);
 ID3D11SamplerState* sampler=sampler_.Get();context_->PSSetSamplers(0,1,&sampler);
 sampler=materialSampler_.Get();context_->PSSetSamplers(1,1,&sampler);
 ID3D11ShaderResourceView* lut=materialLut_.Get();context_->PSSetShaderResources(3,1,&lut);
 UINT stride=sizeof(GPUVertex),offset=0;
 context_->IASetInputLayout(input_.Get());context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 context_->RSSetState(!clean&&wireframeOn_?wireRaster_.Get():raster_.Get());
 context_->OMSetDepthStencilState(nullptr,0);context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(ps_.Get(),nullptr,0);
 struct Item{RenderPart* part;XMFLOAT4X4 world;int mainIndex=-1;};
 std::vector<Item> items;items.reserve(parts_.size()+attachments_.size());
 XMFLOAT4X4 baseWorld;XMStoreFloat4x4(&baseWorld,world);
 for(size_t i=0;i<parts_.size();++i)items.push_back({&parts_[i],baseWorld,int(i)});
 for(auto& attachment:attachments_){
  if(attachment.bone>=boneTransforms_.size())continue;
  XMFLOAT4X4 model;XMStoreFloat4x4(&model,attachmentWorld(attachment));
  // Resources are mutable only for the transparent-sort cache; material edits
  // never traverse this list, and props are absent from the picking geometry.
  for(auto& part:attachment.parts)items.push_back({&part,model,-1});
 }
 auto drawable=[&](const RenderPart& part){return part.visible&&part.vb&&part.ib&&part.textures[0]&&!(alphaEnabled_&&part.alpha.invisible);};
 auto material=[&](Item& item){
  auto& part=*item.part;const bool prop=item.mainIndex<0;
  const bool hovered=!clean&&!prop&&item.mainIndex==hoverPart_&&!part.selected;
  matrices(XMLoadFloat4x4(&item.world));
  cb.tint=prop?XMFLOAT4{1,1,1,1}:XMFLOAT4{tint_[0]*(hovered?1.12f:1.f),tint_[1]*(hovered?1.07f:1.f),tint_[2]*(hovered?.88f:1.f),1};
  cb.params={((prop||bumpOn_)&&part.hasBump&&!part.flatShader)?1.f:0.f,prop?1.f:normalStrength_,prop?.5f:glossStrength_,!prop&&invertNormalY_?1.f:0.f};
  cb.lighting={materialClass_<0?std::clamp(part.materialClass,0.f,3.f):float(materialClass_),glossEnabled_?1.f:0.f,part.emissive,part.emissiveSpecular};
  cb.lightingOptions={exposure_,r4GlossFactor_,part.hasBump&&!part.flatShader?1.f:0.f,0};
  cb.alpha={float(alphaMode(part)),alphaCutoff_,0,0};context_->UpdateSubresource(cb_.Get(),0,nullptr,&cb,0,0);
  ID3D11Buffer* vertexBuffer=part.vb.Get();context_->IASetVertexBuffers(0,1,&vertexBuffer,&stride,&offset);
  ID3D11ShaderResourceView* images[]={part.textures[0].Get(),part.textures[1].Get(),part.textures[2].Get()};context_->PSSetShaderResources(0,3,images);
 };
 context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
 struct Translucent{float depth;size_t item;};std::vector<Translucent> transparent;
 for(size_t i=0;i<items.size();++i){auto& item=items[i];auto& part=*item.part;if(!drawable(part))continue;
  if(alphaMode(part)==AlphaMode::Blend){
   const V3 c=part.center;const XMMATRIX modelView=XMLoadFloat4x4(&item.world)*view;
   transparent.push_back({XMVectorGetZ(XMVector3TransformCoord(XMVectorSet(c.x,c.y,c.z,1),modelView)),i});continue;
  }
  material(item);context_->IASetIndexBuffer(part.ib.Get(),DXGI_FORMAT_R32_UINT,0);context_->DrawIndexed(part.indexCount,0,0);
 }
 std::stable_sort(transparent.begin(),transparent.end(),[](const Translucent& a,const Translucent& b){return a.depth>b.depth;});
 context_->OMSetBlendState(alphaBlend_.Get(),nullptr,0xffffffff);context_->OMSetDepthStencilState(alphaDepth_.Get(),0);
 for(const auto& entry:transparent){auto& item=items[entry.item];auto& part=*item.part;material(item);
  context_->IASetIndexBuffer(sortedAlphaIndices(part,XMLoadFloat4x4(&item.world)*view),DXGI_FORMAT_R32_UINT,0);context_->DrawIndexed(part.indexCount,0,0);
 }
 context_->OMSetBlendState(nullptr,nullptr,0xffffffff);context_->OMSetDepthStencilState(nullptr,0);
 if(!clean&&outlineOn_&&HasSelection()&&vsOutline_&&psOutline_&&outlineRaster_){
  context_->RSSetState(raster_.Get());context_->OMSetBlendState(depthOnlyBlend_.Get(),nullptr,0xffffffff);
  for(auto& item:items){auto& part=*item.part;if(item.mainIndex<0||!drawable(part)||!part.selected||alphaMode(part)!=AlphaMode::Blend)continue;
   material(item);context_->IASetIndexBuffer(part.ib.Get(),DXGI_FORMAT_R32_UINT,0);context_->DrawIndexed(part.indexCount,0,0);
  }
  context_->OMSetBlendState(nullptr,nullptr,0xffffffff);context_->RSSetState(outlineRaster_.Get());
  context_->VSSetShader(vsOutline_.Get(),nullptr,0);context_->PSSetShader(psOutline_.Get(),nullptr,0);
  for(auto& item:items){auto& part=*item.part;if(item.mainIndex<0||!drawable(part)||!part.selected)continue;
   material(item);context_->IASetIndexBuffer(part.ib.Get(),DXGI_FORMAT_R32_UINT,0);context_->DrawIndexed(part.indexCount,0,0);
  }
  context_->RSSetState(raster_.Get());
 }
 matrices(world);cb.tint={tint_[0],tint_[1],tint_[2],1};
 auto lines=[&](ID3D11Buffer* vertices,UINT count){
  if(!vertices||!count)return;
  ID3D11ShaderResourceView* nulls[3]{};context_->PSSetShaderResources(0,3,nulls);cb.params={0,1,.5f,0};context_->UpdateSubresource(cb_.Get(),0,nullptr,&cb,0,0);
  UINT size=sizeof(BoneGPU),start=0;context_->IASetInputLayout(inputSkeleton_.Get());context_->IASetVertexBuffers(0,1,&vertices,&size,&start);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);context_->OMSetDepthStencilState(skeletonDepth_.Get(),0);
  context_->VSSetShader(vsSkeleton_.Get(),nullptr,0);context_->PSSetShader(psSkeleton_.Get(),nullptr,0);context_->Draw(count,0);
  context_->OMSetDepthStencilState(nullptr,0);
 };
 if(!clean&&skeletonOn_)lines(skeletonVB_.Get(),skeletonVertexCount_);
 if(!clean)lines(brushVB_.Get(),brushVertexCount_);
 context_->OMSetRenderTargets(0,nullptr,nullptr);
}
void Renderer::Draw(bool present){
 DrawScene();if(!present||!swap_||!scene_)return;
 // Compatibility for renderer-only callers using a full-window scene.
 ComPtr<ID3D11Texture2D> back;
 if(width_==windowWidth_&&height_==windowHeight_&&SUCCEEDED(swap_->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(back.GetAddressOf()))))
  context_->CopyResource(back.Get(),scene_.Get());
 PresentUI();
}

bool Renderer::readSceneImage(Image& image,std::wstring* error){
 image={};if(!device_||!scene_||!width_||!height_){if(error)*error=L"No scene surface to capture";return false;}
 ComPtr<ID3D11Texture2D> staging;
 D3D11_TEXTURE2D_DESC desc{};scene_->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
 HRESULT hr=device_->CreateTexture2D(&desc,nullptr,staging.GetAddressOf());
 if(FAILED(hr)){if(error)*error=hrtext(L"Create screenshot readback surface",hr);return false;}
 Image result;result.w=width_;result.h=height_;result.pixels.resize(size_t(width_)*height_);
 context_->CopyResource(staging.Get(),scene_.Get());D3D11_MAPPED_SUBRESOURCE map{};
 hr=context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&map);
 if(FAILED(hr)){if(error)*error=hrtext(L"Read screenshot surface",hr);return false;}
 for(UINT y=0;y<height_;++y)std::memcpy(result.pixels.data()+size_t(y)*width_,static_cast<const uint8_t*>(map.pData)+size_t(y)*map.RowPitch,size_t(width_)*sizeof(Pixel));
 context_->Unmap(staging.Get(),0);image=std::move(result);return true;
}
bool Renderer::CaptureFrame(Image& image){
 image={};if(!device_||!scene_)return false;DrawScene();return readSceneImage(image);
}
bool Renderer::CaptureTransparent(Image& image,uint32_t maxDimension,std::wstring& error){
 image={};error.clear();
 if(!device_||!context_||!scene_||!width_||!height_){error=L"No model viewport to capture";return false;}
 const auto size=PreviewCaptureSize(width_,height_,maxDimension);
 // Two by two supersampling preserves subpixel coverage in the PNG alpha,
 // independent of device MSAA support. Only the resolved image is exported.
 const UINT captureWidth=size.first*2,captureHeight=size.second*2;
 ComPtr<ID3D11Texture2D> color,depth;ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11DepthStencilView> depthView;
 D3D11_TEXTURE2D_DESC desc{};desc.Width=captureWidth;desc.Height=captureHeight;desc.MipLevels=1;desc.ArraySize=1;
 desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
 HRESULT hr=device_->CreateTexture2D(&desc,nullptr,color.GetAddressOf());
 if(SUCCEEDED(hr))hr=device_->CreateRenderTargetView(color.Get(),nullptr,target.GetAddressOf());
 if(FAILED(hr)){error=hrtext(L"Create transparent screenshot target",hr);return false;}
 desc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
 hr=device_->CreateTexture2D(&desc,nullptr,depth.GetAddressOf());
 if(SUCCEEDED(hr))hr=device_->CreateDepthStencilView(depth.Get(),nullptr,depthView.GetAddressOf());
 if(FAILED(hr)){error=hrtext(L"Create screenshot depth",hr);return false;}
 const auto originalScene=scene_,originalDepth=depth_;const auto originalTarget=rtv_;const auto originalDepthView=dsv_;
 const UINT originalWidth=width_,originalHeight=height_;
 std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> previousTargets{};
 ComPtr<ID3D11DepthStencilView> previousDepth;
 context_->OMGetRenderTargets(UINT(previousTargets.size()),previousTargets.data(),previousDepth.GetAddressOf());
 std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> previousViewports{};
 UINT viewportCount=UINT(previousViewports.size());context_->RSGetViewports(&viewportCount,previousViewports.data());
 const auto restore=onExit([&]{
  context_->OMSetRenderTargets(0,nullptr,nullptr);
  scene_=originalScene;depth_=originalDepth;rtv_=originalTarget;dsv_=originalDepthView;width_=originalWidth;height_=originalHeight;
  context_->RSSetViewports(viewportCount,previousViewports.data());
  context_->OMSetRenderTargets(UINT(previousTargets.size()),previousTargets.data(),previousDepth.Get());
  for(auto* view:previousTargets)if(view)view->Release();
 });
 scene_=std::move(color);depth_=std::move(depth);rtv_=std::move(target);dsv_=std::move(depthView);width_=captureWidth;height_=captureHeight;
 try{
  drawScene(true,true);Image premultiplied;
  if(!readSceneImage(premultiplied,&error))return false;
  image=ResolvePreviewCapture(premultiplied,size.first,size.second);
  if(!image.valid()){error=L"Cannot resolve transparent screenshot";return false;}
  return true;
 }catch(const std::exception&){error=L"Not enough memory to create screenshot";image={};return false;}
}

}
