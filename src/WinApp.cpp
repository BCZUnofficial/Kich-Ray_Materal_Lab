#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <cwctype>
#include <commctrl.h>
#include <uxtheme.h>
#include <commdlg.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <memory>
#include <cstddef>
#include <cstring>
#include "Renderer.h"
#include "Core.h"
#include "ViewportToolbar.h"
#pragma comment(lib,"uxtheme.lib")

namespace {
using Microsoft::WRL::ComPtr;
constexpr int PANEL=410,TAB_COUNT=6,BRUSH_TAB=4,ANIM_TAB=5,PAGE_TOP=48;
constexpr UINT_PTR ANIM_TIMER=1,VIEW_TIMER=2;
int uiDpi=96;
int ui(int value){return MulDiv(value,uiDpi,96);}
HBRUSH panelBrush(){static HBRUSH b=CreateSolidBrush(RGB(246,248,251));return b;}
HBRUSH toolBrush(){static HBRUSH b=CreateSolidBrush(RGB(29,35,46));return b;}
constexpr uint32_t PREVIEW_MAX=768,BRUSH_PREVIEW_MAX=384,BRUSH_MAX=1024;
constexpr ULONGLONG BRUSH_PREVIEW_MS=180;

enum IDs{
 TAB_MAIN=100,LOAD_OGF=101,LOAD_DIFF,LOAD_NORMAL,LOAD_GLOSS,LOAD_HEIGHT,LOAD_BUMP,LOAD_BUMPX,
 GENERATE,EXPORT,EXPORT_NAME,USE_TEX_NAME,
 SLIDER_NORMAL,SLIDER_GLOSS,SLIDER_HEIGHT,SLIDER_BLUR,SLIDER_SHARP,INVERT_Y,BUMP_ENABLED,COLOR_TINT,SELECT_MODE,CLEAR_SELECTION,
 SLIDER_LIGHT_AZ,SLIDER_LIGHT_EL,COLOR_LIGHT,COLOR_AMBIENT,RESET,SKELETON_ENABLED,ALPHA_ENABLED,ALPHA_MODE,ALPHA_CUTOFF,
 ANIM_COMBO=200,ANIM_PLAY,ANIM_LOOP,ANIM_TIMELINE,ANIM_SPEED,
 MESH_LIST=250,MESH_SHOW_ALL,MESH_HIDE_SELECTED,
 BRUSH_MODE=300,BRUSH_RADIUS,BRUSH_STRENGTH,BRUSH_HARDNESS,PRESET_LOAD,PRESET_SAVE,BRUSH_CLEAR,BRUSH_APPLY,BRUSH_RADIUS_TEXT,BRUSH_STRENGTH_TEXT,BRUSH_HARDNESS_TEXT,BRUSH_INVERT,
 BRUSH_TARGET_RELIEF,BRUSH_TARGET_HEIGHT_ADD,BRUSH_TARGET_HEIGHT_SUB,BRUSH_TARGET_GLOSS_ADD,BRUSH_TARGET_GLOSS_SUB,BRUSH_TARGET_SHARP,BRUSH_TARGET_BLUR,
 TOOL_FLY=400,TOOL_ORBIT,TOOL_CHOOSE,TOOL_OUTLINE,TOOL_LIGHT,TOOL_BRUSH,TOOL_MODEL,
 TOOL_FRAME,TOOL_ISOLATE,TOOL_WIRE,TOOL_UNDO,TOOL_APPLY,TOOL_RESET,
 STATUS=500
};

struct PartMaterial{
 std::filesystem::path diffuse,bump,bumpX;
 std::wstring key;
 bool hasDiffuse=false,hasBump=false;
};
enum class BrushTarget{Relief=0,HeightAdd=1,HeightSub=2,GlossAdd=3,GlossSub=4,Sharp=5,Blur=6};

constexpr uint32_t UNDO_TILE=64;
constexpr size_t UNDO_MAX_BYTES=12u*1024u*1024u;
struct BrushUndoTile{uint32_t tx=0,ty=0,w=0,h=0;BrushTarget target=BrushTarget::Relief;std::vector<lab::Pixel> normal;std::vector<uint8_t> scalar;};
struct BrushUndoStep{std::wstring key;int part=-1;BrushTarget target=BrushTarget::Relief;std::vector<BrushUndoTile> tiles;size_t bytes()const{size_t n=0;for(const auto& t:tiles)n+=t.normal.size()*sizeof(lab::Pixel)+t.scalar.size();return n;}};

std::vector<uint8_t> readFile(const std::wstring& path){std::vector<uint8_t> out;std::ifstream f(std::filesystem::path(path),std::ios::binary|std::ios::ate);if(!f)return out;auto end=f.tellg();if(end<0||end>std::streamoff(256*1024*1024))return out;out.resize(size_t(end));f.seekg(0);if(!out.empty())f.read(reinterpret_cast<char*>(out.data()),std::streamsize(out.size()));if(!f)return {};return out;}
std::wstring pick(HWND hwnd,const wchar_t* filter,const wchar_t* extension){wchar_t file[32768]={};OPENFILENAMEW of={};of.lStructSize=sizeof(of);of.hwndOwner=hwnd;of.lpstrFilter=filter;of.lpstrFile=file;of.nMaxFile=32768;of.lpstrDefExt=extension;of.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;if(GetOpenFileNameW(&of))return file;return {};}
std::wstring savePick(HWND hwnd,const wchar_t* filter,const wchar_t* extension){wchar_t file[32768]={};OPENFILENAMEW of={};of.lStructSize=sizeof(of);of.hwndOwner=hwnd;of.lpstrFilter=filter;of.lpstrFile=file;of.nMaxFile=32768;of.lpstrDefExt=extension;of.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;if(GetSaveFileNameW(&of))return file;return {};}
lab::Image loadWIC(const std::wstring& path,std::wstring& error){lab::Image out;ComPtr<IWICImagingFactory> factory;HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf()));if(FAILED(hr)){error=L"WIC";return out;}ComPtr<IWICBitmapDecoder> decoder;hr=factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,decoder.GetAddressOf());if(FAILED(hr)){error=L"Decode";return out;}ComPtr<IWICBitmapFrameDecode> frame;hr=decoder->GetFrame(0,frame.GetAddressOf());if(FAILED(hr))return out;UINT w=0,h=0;frame->GetSize(&w,&h);if(!w||!h||w>8192||h>8192||size_t(w)*h>64u*1024u*1024u){error=L"Size";return out;}ComPtr<IWICFormatConverter> converter;hr=factory->CreateFormatConverter(converter.GetAddressOf());if(FAILED(hr))return out;hr=converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom);if(FAILED(hr)){error=L"RGBA";return out;}out.w=w;out.h=h;out.pixels.resize(size_t(w)*h);static_assert(sizeof(lab::Pixel)==4,"RGBA8");hr=converter->CopyPixels(nullptr,w*4,UINT(out.pixels.size()*4),reinterpret_cast<BYTE*>(out.pixels.data()));if(FAILED(hr)){error=L"Pixels";return {};}return out;}
lab::Image loadImage(const std::wstring& path,std::wstring& error){std::wstring ext=std::filesystem::path(path).extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),towlower);if(ext==L".dds"){auto data=readFile(path);std::string reason;auto im=lab::DecodeDDS(data,reason);if(!im.valid())error=std::wstring(reason.begin(),reason.end());return im;}return loadWIC(path,error);}
std::wstring lower(std::wstring s){std::transform(s.begin(),s.end(),s.begin(),towlower);return s;}
std::wstring widenPath(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_ACP,0,s.data(),int(s.size()),nullptr,0);if(n<=0)return std::wstring(s.begin(),s.end());std::wstring out(size_t(n),L'\0');MultiByteToWideChar(CP_ACP,0,s.data(),int(s.size()),out.data(),n);return out;}
bool findTexturesRoot(const std::filesystem::path& ogf,std::filesystem::path& root){std::filesystem::path p=ogf.parent_path();while(!p.empty()){if(lower(p.filename().wstring())==L"meshes"){root=p.parent_path()/L"textures";return true;}auto parent=p.parent_path();if(parent==p)break;p=parent;}p=ogf.parent_path();while(!p.empty()){std::filesystem::path candidate=p/L"textures";std::error_code ec;if(std::filesystem::is_directory(candidate,ec)){root=candidate;return true;}auto parent=p.parent_path();if(parent==p)break;p=parent;}return false;}
bool resolveOGFMaterialPaths(const std::filesystem::path& texturesRoot,const std::string& ogfTexture,std::filesystem::path& diffuse,std::filesystem::path& bump,std::filesystem::path& bumpX){std::wstring raw=widenPath(ogfTexture);std::replace(raw.begin(),raw.end(),L'/',L'\\');while(!raw.empty()&&(raw.front()==L'\\'||raw.front()==L'/'))raw.erase(raw.begin());std::wstring low=lower(raw);const std::wstring alias=L"$game_textures$\\",folder=L"textures\\";if(low.rfind(alias,0)==0)raw=raw.substr(alias.size());else if(low.rfind(folder,0)==0)raw=raw.substr(folder.size());std::filesystem::path rel(raw);if(rel.is_absolute())return false;diffuse=texturesRoot/rel;if(diffuse.extension().empty())diffuse+=L".dds";std::wstring stem=diffuse.stem().wstring();bump=diffuse.parent_path()/(stem+L"_bump.dds");bumpX=diffuse.parent_path()/(stem+L"_bump#.dds");return true;}
std::filesystem::path exeDir(){wchar_t path[32768]={};DWORD n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)return std::filesystem::current_path();return std::filesystem::path(path).parent_path();}
std::wstring cleanName(std::wstring s){for(auto& c:s)if(c==L'<'||c==L'>'||c==L':'||c==L'"'||c==L'/'||c==L'\\'||c==L'|'||c==L'?'||c==L'*')c=L'_';while(!s.empty()&&(s.back()==L'.'||s.back()==L' '))s.pop_back();return s;}
std::wstring textureStem(const std::string& ref){std::wstring w=widenPath(ref);std::replace(w.begin(),w.end(),L'/',L'\\');std::filesystem::path p(w);return cleanName(p.stem().wstring());}
std::filesystem::path prefsPath(){return exeDir()/L"MaterialLab.ini";}
std::wstring pathKey(const std::filesystem::path& p){std::error_code ec;auto q=std::filesystem::weakly_canonical(p,ec);if(ec)q=p.lexically_normal();return lower(q.wstring());}
uint64_t fnv1a64(const std::wstring& s){uint64_t h=1469598103934665603ull;for(wchar_t c:s){uint32_t v=uint32_t(c);for(int i=0;i<4;++i){h^=uint8_t(v&0xffu);h*=1099511628211ull;v>>=8;}}return h;}

enum class ViewMode{Fly,Orbit,Choose,Light,Brush,Model};
const lab::ViewTool VIEW_TOOLS[]={
 {TOOL_FLY,L"FC",L"Fly Camera — полёт камеры (1). ЛКМ/ПКМ: осмотр; WASD: движение; Q/E: вниз/вверх. Shift: быстрее, Ctrl: точнее.",true},
 {TOOL_ORBIT,L"OC",L"Orbit Camera — орбитальная камера (2). ЛКМ: обойти модель; колесо: приближение; СКМ: панорама.",true},
 {TOOL_CHOOSE,L"CM",L"Choose Mesh — выбор меша (3 / C). ЛКМ: выбрать видимую часть; ПКМ: снять выделение.",true},
 {0,L"",L""},
 {TOOL_OUTLINE,L"OL",L"Outline — контур выбранного меша (O). Включается независимо от режима управления.",true},
 {TOOL_LIGHT,L"RL",L"Rotate Light — вращение света (4). ЛКМ: азимут/высота; ПКМ: камера.",true},
 {TOOL_BRUSH,L"BR",L"Brush — рисование материала (5 / B). ЛКМ: штрих; ПКМ: камера; [ / ] или Ctrl+колесо: размер; X: инверсия. Сначала нужен bump.",true},
 {TOOL_MODEL,L"RM",L"Rotate Model — вращение модели (6 / R). ЛКМ: поворот; ПКМ: камера. В анимации недоступно.",true},
 {0,L"",L""},
 {TOOL_FRAME,L"FT",L"Frame Target — приблизить выбранную геометрию (F). Без выделения: вместить все видимые меши."},
 {TOOL_ISOLATE,L"IS",L"Isolate Selection — оставить только выбранную геометрию (I). Повторное нажатие восстанавливает прежнюю видимость.",true},
 {TOOL_WIRE,L"WF",L"Wireframe — проверить треугольники модели (V). Повторное нажатие возвращает материал.",true},
 {0,L"",L""},
 {TOOL_UNDO,L"UN",L"Undo — отменить последний штрих (Ctrl+Z)."},
 {TOOL_APPLY,L"AP",L"Apply — применить слой кисти (Ctrl+S). Финальная пара DDS сохраняется кнопкой «Экспорт»."},
 {TOOL_RESET,L"RS",L"Reset View — вернуть начальную камеру и ориентацию модели (Home)."}
};
struct PageControl{HWND window=nullptr;RECT bounds{};bool decoration=false;};
struct PageScroll{int contentHeight=0,offset=0;bool layingOut=false;std::vector<PageControl> items;};
void scrollPage(HWND hwnd,PageScroll& page,int requested){
 if(page.layingOut)return;page.layingOut=true;
 RECT r{};GetClientRect(hwnd,&r);page.offset=std::clamp(requested,0,std::max(0,page.contentHeight-int(r.bottom)));
 SCROLLINFO si={sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS};si.nMin=0;si.nMax=std::max(0,page.contentHeight-1);si.nPage=UINT(std::max(1L,r.bottom));si.nPos=page.offset;
 SetScrollInfo(hwnd,SB_VERT,&si,TRUE);GetClientRect(hwnd,&r);
 auto horizontal=[&](LONG x){return MulDiv(int(x),int(r.right),ui(PANEL));};
 // Rebuild positions from immutable layout rectangles. Scrolling cached pixels
 // with SW_SCROLLCHILDREN corrupts partially clipped controls and group boxes.
 HDWP batch=BeginDeferWindowPos(int(page.items.size()));
 for(const auto& item:page.items){if(!batch)break;const RECT& b=item.bounds;
  batch=DeferWindowPos(batch,item.window,nullptr,horizontal(b.left),b.top-page.offset,horizontal(b.right)-horizontal(b.left),b.bottom-b.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);
 }
 const bool placed=batch&&EndDeferWindowPos(batch);
 if(!placed)for(const auto& item:page.items){const RECT& b=item.bounds;SetWindowPos(item.window,nullptr,horizontal(b.left),b.top-page.offset,horizontal(b.right)-horizontal(b.left),b.bottom-b.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);}
 page.layingOut=false;
 RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
}
LRESULT CALLBACK materialPageProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 auto* page=reinterpret_cast<PageScroll*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
 if(msg==WM_NCCREATE){page=reinterpret_cast<PageScroll*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(page));}
 switch(msg){
  case WM_ERASEBKGND:{RECT r{};GetClientRect(hwnd,&r);FillRect(reinterpret_cast<HDC>(wp),&r,panelBrush());return 1;}
  case WM_PAINT:{PAINTSTRUCT ps;BeginPaint(hwnd,&ps);FillRect(ps.hdc,&ps.rcPaint,panelBrush());EndPaint(hwnd,&ps);return 0;}
  case WM_SIZE:if(page)scrollPage(hwnd,*page,page->offset);return 0;
  case WM_MOUSEWHEEL:if(page)scrollPage(hwnd,*page,page->offset-GET_WHEEL_DELTA_WPARAM(wp)*ui(48)/WHEEL_DELTA);return 0;
  case WM_VSCROLL:if(page){int pos=page->offset;RECT r{};GetClientRect(hwnd,&r);switch(LOWORD(wp)){
   case SB_LINEUP:pos-=ui(24);break;case SB_LINEDOWN:pos+=ui(24);break;
   case SB_PAGEUP:pos-=r.bottom;break;case SB_PAGEDOWN:pos+=r.bottom;break;
   case SB_THUMBTRACK:case SB_THUMBPOSITION:{SCROLLINFO si={sizeof(si),SIF_TRACKPOS};GetScrollInfo(hwnd,SB_VERT,&si);pos=si.nTrackPos;break;}
   case SB_TOP:pos=0;break;case SB_BOTTOM:pos=page->contentHeight;break;
  }scrollPage(hwnd,*page,pos);}return 0;
  case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_COMMAND:case WM_NOTIFY:case WM_HSCROLL:case WM_DRAWITEM:case WM_MEASUREITEM:
   return SendMessageW(GetParent(hwnd),msg,wp,lp);
 }
 return DefWindowProcW(hwnd,msg,wp,lp);
}

struct BumpTargetDialogState{
 std::vector<size_t> ids;
 std::vector<std::wstring> labels;
 std::vector<size_t> result;
 int preferred=-1;
 HWND list=nullptr;
 bool done=false,accepted=false;
};
LRESULT CALLBACK bumpTargetProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 auto* st=reinterpret_cast<BumpTargetDialogState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
 if(msg==WM_NCCREATE){st=reinterpret_cast<BumpTargetDialogState*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(st));}
 if(!st)return DefWindowProcW(hwnd,msg,wp,lp);
 switch(msg){
  case WM_CREATE:{
   HFONT font=reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
   st->list=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_NOCOLUMNHEADER|LVS_SHOWSELALWAYS,12,12,430,240,hwnd,reinterpret_cast<HMENU>(1000),GetModuleHandleW(nullptr),nullptr);
   SendMessageW(st->list,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);ListView_SetExtendedListViewStyle(st->list,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);LVCOLUMNW col={};col.mask=LVCF_WIDTH;col.cx=400;ListView_InsertColumn(st->list,0,&col);
   for(size_t i=0;i<st->ids.size();++i){LVITEMW item={};item.mask=LVIF_TEXT;item.iItem=int(i);item.pszText=st->labels[i].data();ListView_InsertItem(st->list,&item);const bool checked=st->preferred>=0?int(st->ids[i])==st->preferred:true;ListView_SetCheckState(st->list,int(i),checked?TRUE:FALSE);}
   auto mk=[&](int id,const wchar_t* txt,int x,int w){HWND b=CreateWindowExW(0,L"BUTTON",txt,WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,x,264,w,30,hwnd,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(b,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);};
   mk(1001,L"Все",12,100);mk(IDOK,L"ОК",250,90);mk(IDCANCEL,L"Отмена",350,90);return 0;
  }
  case WM_COMMAND:{const int id=LOWORD(wp);if(id==1001){for(int i=0;i<ListView_GetItemCount(st->list);++i)ListView_SetCheckState(st->list,i,TRUE);return 0;}if(id==IDOK){st->result.clear();for(int i=0;i<ListView_GetItemCount(st->list);++i)if(ListView_GetCheckState(st->list,i))st->result.push_back(st->ids[size_t(i)]);st->accepted=true;st->done=true;DestroyWindow(hwnd);return 0;}if(id==IDCANCEL){st->accepted=false;st->done=true;DestroyWindow(hwnd);return 0;}break;}
  case WM_CLOSE:st->accepted=false;st->done=true;DestroyWindow(hwnd);return 0;
 }
 return DefWindowProcW(hwnd,msg,wp,lp);
}
bool chooseBumpTargets(HWND owner,const std::vector<size_t>& ids,const std::vector<std::wstring>& labels,int preferred,std::vector<size_t>& result){
 result.clear();if(ids.empty())return false;if(ids.size()==1){result=ids;return true;}
 static bool registered=false;if(!registered){WNDCLASSEXW wc={sizeof(wc)};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=bumpTargetProc;wc.lpszClassName=L"KichRayBumpTargetDialog";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;registered=true;}
 BumpTargetDialogState st;st.ids=ids;st.labels=labels;st.preferred=preferred;RECT orc{};GetWindowRect(owner,&orc);const int w=466,h=336,x=int(orc.left)+std::max(0,(int(orc.right-orc.left)-w)/2),y=int(orc.top)+std::max(0,(int(orc.bottom-orc.top)-h)/2);
 HWND dlg=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,L"KichRayBumpTargetDialog",L"Создать bump для мешей",WS_POPUP|WS_CAPTION|WS_SYSMENU,x,y,w,h,owner,nullptr,GetModuleHandleW(nullptr),&st);if(!dlg)return false;
 EnableWindow(owner,FALSE);ShowWindow(dlg,SW_SHOW);UpdateWindow(dlg);MSG msg;while(!st.done&&GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(dlg,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}EnableWindow(owner,TRUE);SetForegroundWindow(owner);if(st.accepted)result=std::move(st.result);return st.accepted&&!result.empty();
}

class App {
 HWND window_=nullptr,preview_=nullptr,status_=nullptr,tab_=nullptr,pageHost_=nullptr,pages_[TAB_COUNT]={},controls_[640]={};HFONT font_=nullptr;lab::ViewportToolbar toolbar_;PageScroll pageScroll_[TAB_COUNT];
 ViewMode viewMode_=ViewMode::Fly;bool outline_=true,wireframe_=false;std::vector<bool> isolatedVisibility_;
 POINT brushScreen_{};bool brushScreenValid_=false;ULONGLONG viewLastTick_=0,hoverLastTick_=0;
 bool ready_=false,useImported_=false,autoModelMaterials_=false,selectionMode_=false,lDown_=false,rDown_=false,mDown_=false,animationTab_=false,animPlaying_=false,generatedBumpMode_=false;
 bool meshListUpdating_=false,brushMode_=false,brushDown_=false,brushDirty_=false,manualBumpAvailable_=false,modelRotateMode_=false;POINT last_{};
 std::vector<HWND> tabItems_[TAB_COUNT];lab::Sources sources_;lab::Image importedBump_,importedBumpX_;lab::Settings settings_;lab::Renderer render_;lab::Mesh currentMesh_;std::filesystem::path modelPath_,manualTexturePath_;
 std::vector<PartMaterial> materials_;std::unordered_map<std::wstring,std::shared_ptr<lab::Image>> paintedNormals_;std::unordered_map<std::wstring,std::shared_ptr<lab::GrayImage>> paintedGloss_,paintedHeight_;std::unordered_set<std::wstring> generatedBumpKeys_;
 lab::Image brushPreset_=lab::MakeDefaultNormalPreset();lab::Image brushDiffuse_,brushBaseNormal_;lab::GrayImage brushCoverage_,brushBaseScalar_;std::wstring brushDiffuseKey_,brushCoverageKey_;BrushTarget brushTarget_=BrushTarget::Relief;float brushRadiusUv_=0.03f,brushStrength_=0.35f,brushHardness_=0.65f;bool brushInvert_=false;ULONGLONG lastBrushPreview_=0;
 std::deque<BrushUndoStep> brushUndo_;BrushUndoStep currentUndo_;std::unordered_set<uint64_t> currentUndoTiles_;std::unordered_set<std::wstring> brushDirtyKeys_;size_t brushUndoBytes_=0;
 size_t animIndex_=0;float animTime_=0.f,animSpeed_=1.f;ULONGLONG animLastTick_=0;

 void status(const std::wstring& s){if(status_)SetWindowTextW(status_,s.c_str());syncTools();}
 float brushStrengthFromSlider(int value)const{const float t=std::clamp(value/1000.f,0.f,1.f);return t*t*t;}
 HWND control(int tab,int id,const wchar_t* klass,const wchar_t* text,int x,int y,int w,int h,DWORD style=0){HWND parent=window_;int localY=y;if(tab>=0&&tab<TAB_COUNT&&pages_[tab]){parent=pages_[tab];localY=std::max(0,y-PAGE_TOP);}HWND c=CreateWindowExW(0,klass,text,WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|style,ui(x),ui(localY),ui(w),ui(h),parent,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);if(c){const bool decoration=lstrcmpiW(klass,L"BUTTON")==0&&(style&BS_TYPEMASK)==BS_GROUPBOX;SetWindowPos(c,decoration?HWND_BOTTOM:HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);if(id>0&&id<640)controls_[id]=c;if(tab>=0&&tab<TAB_COUNT){tabItems_[tab].push_back(c);pageScroll_[tab].items.push_back({c,{ui(x),ui(localY),ui(x+w),ui(localY+h)},decoration});RECT bounds{};GetWindowRect(c,&bounds);pageScroll_[tab].contentHeight=std::max(pageScroll_[tab].contentHeight,ui(localY+16)+int(bounds.bottom-bounds.top));}SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);if(lstrcmpiW(klass,WC_LISTVIEWW)==0)SetWindowTheme(c,L"Explorer",nullptr);}return c;}
 void label(int tab,const wchar_t* txt,int y){control(tab,0,L"STATIC",txt,12,y,PANEL-36,20,SS_LEFT);}
 void button(int tab,int id,const wchar_t* text,int y,int h=31){control(tab,id,L"BUTTON",text,12,y,PANEL-24,h,BS_PUSHBUTTON|WS_TABSTOP);}
 void group(int tab,const wchar_t* text,int y,int h){control(tab,0,L"BUTTON",text,6,y,PANEL-12,h,BS_GROUPBOX);}
 HWND slider(int tab,int id,int y,int lo,int hi,int value){HWND h=control(tab,id,TRACKBAR_CLASSW,L"",12,y,PANEL-24,32,TBS_NOTICKS|WS_TABSTOP);SendMessageW(h,TBM_SETRANGEMIN,FALSE,lo);SendMessageW(h,TBM_SETRANGEMAX,TRUE,hi);SendMessageW(h,TBM_SETPOS,TRUE,value);return h;}
 bool animationPageActive()const{return animationTab_&&tab_&&TabCtrl_GetCurSel(tab_)==ANIM_TAB;}
 bool editingLocked()const{return animPlaying_;}
 void showTab(int selected){
  const int count=tab_?TabCtrl_GetItemCount(tab_):0;if(selected<0||selected>=count)selected=0;
  if(tab_)TabCtrl_SetCurSel(tab_,selected);
  HWND focus=GetFocus();
  for(int t=0;t<TAB_COUNT;++t){if(t==selected||!pages_[t])continue;
   if(focus&&(focus==pages_[t]||IsChild(pages_[t],focus)))SetFocus(tab_);
   // Hide the controls as well as their page. A stale repaint or focus change
   // must never expose controls belonging to a different tab.
   ShowWindow(pages_[t],SW_HIDE);EnableWindow(pages_[t],FALSE);
   for(HWND control:tabItems_[t])ShowWindow(control,SW_HIDE);
  }
  if(pages_[selected]){
   for(const auto& item:pageScroll_[selected].items)SetWindowPos(item.window,item.decoration?HWND_BOTTOM:HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
   EnableWindow(pages_[selected],TRUE);
   scrollPage(pages_[selected],pageScroll_[selected],pageScroll_[selected].offset);
   SetWindowPos(pages_[selected],HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
   RedrawWindow(pages_[selected],nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
  }
  if(pageHost_)RedrawWindow(pageHost_,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
 }
 void layout(){
  if(!window_||!preview_)return;RECT r{};GetClientRect(window_,&r);
  const int w=std::max(1L,r.right),h=std::max(1L,r.bottom),panel=ui(PANEL);
  const int contentH=std::max(1,h-ui(32)),viewH=std::max(1,contentH-ui(lab::ViewportToolbar::Height));
  const int pageHeight=std::max(1,contentH-ui(PAGE_TOP));
  if(pageHost_)MoveWindow(pageHost_,0,ui(PAGE_TOP),panel,pageHeight,TRUE);
  for(int t=0;t<TAB_COUNT;++t)if(pages_[t])MoveWindow(pages_[t],0,0,panel,pageHeight,IsWindowVisible(pages_[t]));
  MoveWindow(preview_,panel,0,std::max(1,w-panel),viewH,TRUE);
  toolbar_.layout(panel,viewH,std::max(1,w-panel));
  if(status_)MoveWindow(status_,ui(12),contentH+ui(7),std::max(1,w-ui(24)),ui(22),TRUE);
  InvalidateRect(window_,nullptr,TRUE);
 }
 void syncTools(){
  if(!toolbar_.window())return;
  const int modes[]={TOOL_FLY,TOOL_ORBIT,TOOL_CHOOSE,TOOL_LIGHT,TOOL_BRUSH,TOOL_MODEL};
  for(int i=0;i<6;++i)toolbar_.check(modes[i],int(viewMode_)==i);
  toolbar_.check(TOOL_OUTLINE,outline_);toolbar_.check(TOOL_WIRE,wireframe_);toolbar_.check(TOOL_ISOLATE,!isolatedVisibility_.empty());
  toolbar_.enable(TOOL_CHOOSE,!editingLocked());toolbar_.enable(TOOL_BRUSH,!editingLocked()&&anyEditableBump());
  toolbar_.enable(TOOL_MODEL,!editingLocked()&&!animationPageActive());
  toolbar_.enable(TOOL_ISOLATE,!editingLocked()&&(render_.HasSelection()||!isolatedVisibility_.empty()));
  toolbar_.enable(TOOL_UNDO,!editingLocked()&&!brushUndo_.empty());toolbar_.enable(TOOL_APPLY,!editingLocked()&&brushDirty_);
  toolbar_.enable(TOOL_RESET,!editingLocked());
  if(controls_[SELECT_MODE])SendMessageW(controls_[SELECT_MODE],BM_SETCHECK,selectionMode_?BST_CHECKED:BST_UNCHECKED,0);
  if(controls_[BRUSH_MODE])SendMessageW(controls_[BRUSH_MODE],BM_SETCHECK,brushMode_?BST_CHECKED:BST_UNCHECKED,0);
  const wchar_t* hints[]={L"FC · Полёт  |  WASD / QE · Shift быстрее  |  Мышь с ЛКМ/ПКМ: осмотр",
   L"OC · Орбита  |  ЛКМ: камера  |  СКМ: панорама  |  Колесо: масштаб",
   L"CM · Выбор меша  |  ЛКМ: выбрать  |  ПКМ: снять  |  F: приблизить",
   L"RL · Свет  |  ЛКМ: вращение света  |  ПКМ: камера  |  СКМ: панорама",
   L"BR · Кисть  |  ЛКМ: штрих  |  ПКМ: камера  |  [ ]: размер  |  X: инверсия",
   L"RM · Модель  |  ЛКМ: вращение модели  |  ПКМ: камера  |  Home: сброс"};
  std::wstring hint=hints[int(viewMode_)];if(render_.SelectedPart()>=0)hint+=L"  · Mesh "+std::to_wstring(render_.SelectedPart()+1);
  if(brushDirty_)hint+=L"  · * слой не применён";toolbar_.text(hint.c_str());
 }
 void endInteraction(bool release=true){
  const bool lightMoved=(lDown_&&viewMode_==ViewMode::Light);
  if(brushDown_){const int part=currentUndo_.part;brushDown_=false;finishBrushStroke();if(part>=0)updateBrushPreview(size_t(part));}
  lDown_=rDown_=mDown_=false;brushScreenValid_=false;
  if(release&&GetCapture()==preview_)ReleaseCapture();
  if(lightMoved)syncLightFromRenderer();
 }
 void setMode(ViewMode mode){
  if(editingLocked()&&(mode==ViewMode::Choose||mode==ViewMode::Brush||mode==ViewMode::Model)){syncTools();return;}
  if(mode==ViewMode::Model&&animationPageActive()){status(L"Вращение модели недоступно во вкладке анимации");return;}
  if(mode==ViewMode::Brush&&!anyEditableBump()){status(L"Кисть недоступна: сначала загрузите или создайте bump");return;}
  endInteraction();viewMode_=mode;selectionMode_=mode==ViewMode::Choose;brushMode_=mode==ViewMode::Brush;modelRotateMode_=mode==ViewMode::Model;
  render_.FlyCameraEnabled(mode==ViewMode::Fly);render_.ClearHover();render_.ClearBrushCursor();
  render_.SetNormalSettings(brushMode_?1.f:settings_.normalStrength,brushMode_?false:settings_.invertY);
  render_.SetGlossStrength(brushMode_?.5f:settings_.gloss);
  if(brushMode_&&TabCtrl_GetCurSel(tab_)!=BRUSH_TAB){returnToBindPose();TabCtrl_SetCurSel(tab_,BRUSH_TAB);showTab(BRUSH_TAB);}
  syncTools();InvalidateRect(preview_,nullptr,FALSE);
 }
 void activateTab(int selected){
  endInteraction();
  if(selected!=ANIM_TAB)returnToBindPose();else{setMode(ViewMode::Fly);returnToBindPose();}
  showTab(selected);syncTools();InvalidateRect(preview_,nullptr,FALSE);
 }
 void tickView(){
  const ULONGLONG now=GetTickCount64();const float dt=std::min(.05f,float(now-viewLastTick_)/1000.f);viewLastTick_=now;
  if(!ready_||viewMode_!=ViewMode::Fly||GetFocus()!=preview_||GetForegroundWindow()!=window_)return;
  auto down=[](int key){return (GetAsyncKeyState(key)&0x8000)!=0;};
  float x=float(down('D'))-float(down('A')),y=float(down('E'))-float(down('Q')),z=float(down('W'))-float(down('S'));
  const float length=std::sqrt(x*x+y*y+z*z);if(length==0)return;
  const float amount=dt*1.4f*(down(VK_SHIFT)?4.f:1.f)*(down(VK_CONTROL)?.2f:1.f)/length;
  render_.MoveCamera(x*amount,y*amount,z*amount);InvalidateRect(preview_,nullptr,FALSE);
 }
 void restoreIsolation(){
  if(isolatedVisibility_.empty())return;meshListUpdating_=true;
  for(size_t i=0;i<isolatedVisibility_.size()&&i<render_.PartCount();++i){render_.SetPartVisible(i,isolatedVisibility_[i]);ListView_SetCheckState(controls_[MESH_LIST],int(i),isolatedVisibility_[i]);}
  meshListUpdating_=false;isolatedVisibility_.clear();syncTools();
 }
 void toggleIsolation(){
  endInteraction();if(!isolatedVisibility_.empty())restoreIsolation();else{
   if(!render_.HasSelection())return;meshListUpdating_=true;
   for(size_t i=0;i<render_.PartCount();++i){isolatedVisibility_.push_back(render_.PartVisible(i));const bool visible=render_.PartSelected(i);render_.SetPartVisible(i,visible);ListView_SetCheckState(controls_[MESH_LIST],int(i),visible);}
   meshListUpdating_=false;
  }refreshBrushAvailability();syncTools();InvalidateRect(preview_,nullptr,FALSE);
 }
 void adjustBrushRadius(int delta){
  if(!brushMode_||brushDown_)return;const int value=int(SendMessageW(controls_[BRUSH_RADIUS],TBM_GETPOS,0,0));
  SendMessageW(controls_[BRUSH_RADIUS],TBM_SETPOS,TRUE,std::clamp(value+delta,1,200));movedSlider(BRUSH_RADIUS);
 }
 void saveLight(){if(!controls_[SLIDER_LIGHT_AZ]||!controls_[SLIDER_LIGHT_EL])return;wchar_t a[32],e[32];swprintf_s(a,L"%d",int(SendMessageW(controls_[SLIDER_LIGHT_AZ],TBM_GETPOS,0,0)));swprintf_s(e,L"%d",int(SendMessageW(controls_[SLIDER_LIGHT_EL],TBM_GETPOS,0,0)));auto p=prefsPath();WritePrivateProfileStringW(L"Scene",L"LightAzimuth",a,p.c_str());WritePrivateProfileStringW(L"Scene",L"LightElevation",e,p.c_str());}
 void loadLight(){auto p=prefsPath();int az=GetPrivateProfileIntW(L"Scene",L"LightAzimuth",52,p.c_str());int el=GetPrivateProfileIntW(L"Scene",L"LightElevation",37,p.c_str());az=std::max(0,std::min(360,az));el=std::max(-80,std::min(80,el));SendMessageW(controls_[SLIDER_LIGHT_AZ],TBM_SETPOS,TRUE,az);SendMessageW(controls_[SLIDER_LIGHT_EL],TBM_SETPOS,TRUE,el);render_.SetLightOrbitDegrees(float(az),float(el));}
 void syncLightFromRenderer(){int az=int(std::lround(render_.LightAzimuthDegrees()));int el=int(std::lround(render_.LightElevationDegrees()));SendMessageW(controls_[SLIDER_LIGHT_AZ],TBM_SETPOS,TRUE,az);SendMessageW(controls_[SLIDER_LIGHT_EL],TBM_SETPOS,TRUE,el);saveLight();}

 std::filesystem::path tempModelDir()const{std::wstring label=!modelPath_.empty()?cleanName(modelPath_.stem().wstring()):(!manualTexturePath_.empty()?cleanName(manualTexturePath_.stem().wstring()):L"manual");if(label.empty())label=L"material";const std::wstring identity=!modelPath_.empty()?pathKey(modelPath_):(!manualTexturePath_.empty()?pathKey(manualTexturePath_):L"manual");wchar_t hash[24]={};swprintf_s(hash,L"_%016llx",static_cast<unsigned long long>(fnv1a64(identity)));return exeDir()/L"temp"/(label+hash);}
 std::filesystem::path tempLayerPath(const std::wstring& key,const wchar_t* stage)const{wchar_t hash[24]={};swprintf_s(hash,L"%016llx",static_cast<unsigned long long>(fnv1a64(key)));return tempModelDir()/(std::wstring(hash)+L"_"+stage+L".krlt");}
 bool saveTempLayers(const std::wstring& key,const wchar_t* stage,const lab::Image& normal,const lab::GrayImage& gloss,const lab::GrayImage& height){if(!normal.valid()||!gloss.valid()||!height.valid()||normal.w!=gloss.w||normal.h!=gloss.h||normal.w!=height.w||normal.h!=height.h)return false;std::error_code ec;std::filesystem::create_directories(tempModelDir(),ec);if(ec)return false;const auto final=tempLayerPath(key,stage);auto staging=final;staging+=L".writing";std::ofstream out(staging,std::ios::binary|std::ios::trunc);if(!out)return false;const char magic[4]={'K','R','L','T'};const uint32_t version=1;out.write(magic,4);out.write(reinterpret_cast<const char*>(&version),4);out.write(reinterpret_cast<const char*>(&normal.w),4);out.write(reinterpret_cast<const char*>(&normal.h),4);out.write(reinterpret_cast<const char*>(normal.pixels.data()),std::streamsize(normal.pixels.size()*sizeof(lab::Pixel)));out.write(reinterpret_cast<const char*>(gloss.pixels.data()),std::streamsize(gloss.pixels.size()));out.write(reinterpret_cast<const char*>(height.pixels.data()),std::streamsize(height.pixels.size()));out.flush();const bool written=bool(out);out.close();if(!written||!out){std::filesystem::remove(staging,ec);return false;}if(!MoveFileExW(staging.c_str(),final.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::filesystem::remove(staging,ec);return false;}return true;}
 bool loadTempLayers(const std::wstring& key,const wchar_t* stage,lab::Image& normal,lab::GrayImage& gloss,lab::GrayImage& height)const{normal={};gloss={};height={};std::ifstream in(tempLayerPath(key,stage),std::ios::binary);if(!in)return false;char magic[4]{};uint32_t version=0,w=0,h=0;in.read(magic,4);in.read(reinterpret_cast<char*>(&version),4);in.read(reinterpret_cast<char*>(&w),4);in.read(reinterpret_cast<char*>(&h),4);if(!in||std::memcmp(magic,"KRLT",4)!=0||version!=1||!w||!h||w>4096||h>4096||size_t(w)*h>16777216u)return false;normal.w=w;normal.h=h;normal.pixels.resize(size_t(w)*h);gloss.w=w;gloss.h=h;gloss.pixels.resize(size_t(w)*h);height.w=w;height.h=h;height.pixels.resize(size_t(w)*h);in.read(reinterpret_cast<char*>(normal.pixels.data()),std::streamsize(normal.pixels.size()*sizeof(lab::Pixel)));in.read(reinterpret_cast<char*>(gloss.pixels.data()),std::streamsize(gloss.pixels.size()));in.read(reinterpret_cast<char*>(height.pixels.data()),std::streamsize(height.pixels.size()));if(!in){normal={};gloss={};height={};return false;}return true;}
 bool saveBaseFromPair(const std::wstring& key,const lab::BumpPair& pair){if(!pair.bump.valid()||!pair.bumpX.valid())return false;lab::Image n=lab::UnpackNormalMap(pair.bump,pair.bumpX);lab::GrayImage g=lab::UnpackGlossGray(pair.bump),h=lab::UnpackHeightGray(pair.bumpX);return saveTempLayers(key,L"base",n,g,h);}
 void clearBrushForKey(const std::wstring& key){paintedNormals_.erase(key);paintedGloss_.erase(key);paintedHeight_.erase(key);brushDirtyKeys_.erase(key);std::error_code ec;std::filesystem::remove(tempLayerPath(key,L"brush"),ec);resetBrushCoverage();brushUndo_.clear();currentUndo_={};currentUndoTiles_.clear();brushUndoBytes_=0;brushDirty_=!brushDirtyKeys_.empty();}

 void resetBrushCoverage(){brushCoverage_={};brushCoverageKey_.clear();brushBaseNormal_={};brushBaseScalar_={};}
 bool brushNormalTarget()const{return brushTarget_==BrushTarget::Relief||brushTarget_==BrushTarget::Sharp||brushTarget_==BrushTarget::Blur;}
 bool brushGlossTarget()const{return brushTarget_==BrushTarget::GlossAdd||brushTarget_==BrushTarget::GlossSub;}
 const wchar_t* brushTargetName()const{switch(brushTarget_){case BrushTarget::Relief:return L"Рельеф";case BrushTarget::HeightAdd:return L"Высота +";case BrushTarget::HeightSub:return L"Впадина";case BrushTarget::GlossAdd:return L"Блеск +";case BrushTarget::GlossSub:return L"Блеск -";case BrushTarget::Sharp:return L"Sharp";case BrushTarget::Blur:return L"Blur";}return L"Кисть";}
 BrushTarget effectiveBrushTarget()const{if(!brushInvert_)return brushTarget_;switch(brushTarget_){case BrushTarget::HeightAdd:return BrushTarget::HeightSub;case BrushTarget::HeightSub:return BrushTarget::HeightAdd;case BrushTarget::GlossAdd:return BrushTarget::GlossSub;case BrushTarget::GlossSub:return BrushTarget::GlossAdd;case BrushTarget::Sharp:return BrushTarget::Blur;case BrushTarget::Blur:return BrushTarget::Sharp;default:return brushTarget_;}}
 void setBrushTarget(BrushTarget target,int controlId){brushTarget_=target;resetBrushCoverage();const int ids[]={BRUSH_TARGET_RELIEF,BRUSH_TARGET_HEIGHT_ADD,BRUSH_TARGET_HEIGHT_SUB,BRUSH_TARGET_GLOSS_ADD,BRUSH_TARGET_GLOSS_SUB,BRUSH_TARGET_SHARP,BRUSH_TARGET_BLUR};for(int id:ids)if(controls_[id])SendMessageW(controls_[id],BM_SETCHECK,id==controlId?BST_CHECKED:BST_UNCHECKED,0);status(std::wstring(L"Кисть: ")+brushTargetName());}
 void ensureBrushBase(const std::wstring& key,const lab::Image& normal,const lab::GrayImage& gloss,const lab::GrayImage& height){if(brushCoverageKey_!=key){resetBrushCoverage();brushCoverageKey_=key;}if(brushNormalTarget()){if(!brushBaseNormal_.valid()||brushBaseNormal_.w!=normal.w||brushBaseNormal_.h!=normal.h)brushBaseNormal_=normal;}else{const lab::GrayImage& src=brushGlossTarget()?gloss:height;if(!brushBaseScalar_.valid()||brushBaseScalar_.w!=src.w||brushBaseScalar_.h!=src.h)brushBaseScalar_=src;}}
 bool hasEditableBump(size_t part)const{
  if(materials_.empty())return sources_.diffuse.valid()&&(manualBumpAvailable_||(useImported_&&importedBump_.valid()&&importedBumpX_.valid()));
  if(part>=materials_.size()||!render_.PartVisible(part))return false;const std::wstring key=materialKey(part);return materials_[part].hasBump||generatedBumpKeys_.find(key)!=generatedBumpKeys_.end();
 }
 bool anyEditableBump()const{if(materials_.empty())return hasEditableBump(0);if(render_.HasSelection()){for(size_t i=0;i<materials_.size();++i)if(render_.PartSelected(i)&&hasEditableBump(i))return true;return false;}for(size_t i=0;i<materials_.size();++i)if(hasEditableBump(i))return true;return false;}
 void refreshBrushAvailability(){if(!controls_[BRUSH_MODE])return;const bool ok=anyEditableBump()&&!editingLocked();EnableWindow(controls_[BRUSH_MODE],ok?TRUE:FALSE);if(!ok&&brushMode_)setMode(ViewMode::Fly);syncTools();}
 std::wstring materialKey(size_t part)const{if(part<materials_.size()&&!materials_[part].key.empty())return materials_[part].key;if(!manualTexturePath_.empty())return pathKey(manualTexturePath_);return L"__manual__";}
 bool loadPartDiffuse(size_t part,lab::Image& out,bool preview=false){out={};if(!materials_.empty()&&(part>=materials_.size()||!render_.PartVisible(part)))return false;if(part<materials_.size()&&materials_[part].hasDiffuse){std::wstring e;out=loadImage(materials_[part].diffuse.wstring(),e);if(!out.valid()){status(e);return false;}if(preview&&std::max(out.w,out.h)>PREVIEW_MAX)out=lab::ResizeImageMax(out,PREVIEW_MAX);return out.valid();}if(sources_.diffuse.valid()){out=sources_.diffuse;if(preview&&std::max(out.w,out.h)>PREVIEW_MAX)out=lab::ResizeImageMax(out,PREVIEW_MAX);return true;}return false;}
 bool ensurePaintLayers(size_t part,bool create,std::shared_ptr<lab::Image>& normal,std::shared_ptr<lab::GrayImage>& gloss,std::shared_ptr<lab::GrayImage>& height){
  if(!hasEditableBump(part))return false;const std::wstring key=materialKey(part);auto ni=paintedNormals_.find(key);auto gi=paintedGloss_.find(key);auto hi=paintedHeight_.find(key);if(ni!=paintedNormals_.end()&&gi!=paintedGloss_.end()&&hi!=paintedHeight_.end()){normal=ni->second;gloss=gi->second;height=hi->second;return true;}if(!create)return false;
  lab::Image baseNormal;lab::GrayImage baseGloss,baseHeight;
  // A committed brush snapshot is already the full composite. Otherwise use
  // the frozen slider-generated base. Neither path is re-run through Sobel.
  if(loadTempLayers(key,L"brush",baseNormal,baseGloss,baseHeight)){
   // loaded exact composite from temp
  }else if(loadTempLayers(key,L"base",baseNormal,baseGloss,baseHeight)){
   // loaded exact slider-generated base from temp
  }else if(part<materials_.size()&&materials_[part].hasDiffuse){
   if(brushDiffuseKey_!=key||!brushDiffuse_.valid()){std::wstring e;brushDiffuse_=loadImage(materials_[part].diffuse.wstring(),e);if(brushDiffuse_.valid()&&std::max(brushDiffuse_.w,brushDiffuse_.h)>BRUSH_MAX)brushDiffuse_=lab::ResizeImageMax(brushDiffuse_,BRUSH_MAX);brushDiffuseKey_=key;if(!brushDiffuse_.valid()){status(e);return false;}}
   if(materials_[part].hasBump){std::wstring e1,e2;auto b=loadImage(materials_[part].bump.wstring(),e1);if(b.valid()&&std::max(b.w,b.h)>BRUSH_MAX)b=lab::ResizeImageMax(b,BRUSH_MAX);auto bx=loadImage(materials_[part].bumpX.wstring(),e2);if(bx.valid()&&std::max(bx.w,bx.h)>BRUSH_MAX)bx=lab::ResizeImageMax(bx,BRUSH_MAX);if(b.valid()&&bx.valid()){baseNormal=lab::UnpackNormalMap(b,bx);baseGloss=lab::UnpackGlossGray(b);baseHeight=lab::UnpackHeightGray(bx);}}
   if(!baseNormal.valid())return false;
  }else if(sources_.diffuse.valid()){
   if(importedBump_.valid()&&importedBumpX_.valid()){lab::Image b=lab::ResizeImageMax(importedBump_,BRUSH_MAX),bx=lab::ResizeImageMax(importedBumpX_,BRUSH_MAX);baseNormal=lab::UnpackNormalMap(b,bx);baseGloss=lab::UnpackGlossGray(b);baseHeight=lab::UnpackHeightGray(bx);}
   if(!baseNormal.valid())return false;
  }
  if(!baseNormal.valid())return false;
  if(!baseGloss.valid()||baseGloss.w!=baseNormal.w||baseGloss.h!=baseNormal.h){baseGloss.w=baseNormal.w;baseGloss.h=baseNormal.h;const uint8_t g=uint8_t(std::clamp(settings_.gloss,0.f,1.f)*255.f+0.5f);baseGloss.pixels.assign(size_t(baseGloss.w)*baseGloss.h,g);}
  if(!baseHeight.valid()||baseHeight.w!=baseNormal.w||baseHeight.h!=baseNormal.h){baseHeight.w=baseNormal.w;baseHeight.h=baseNormal.h;baseHeight.pixels.assign(size_t(baseHeight.w)*baseHeight.h,128);}
  if(!std::filesystem::exists(tempLayerPath(key,L"base")))saveTempLayers(key,L"base",baseNormal,baseGloss,baseHeight);
  normal=std::make_shared<lab::Image>(std::move(baseNormal));gloss=std::make_shared<lab::GrayImage>(std::move(baseGloss));height=std::make_shared<lab::GrayImage>(std::move(baseHeight));paintedNormals_[key]=normal;paintedGloss_[key]=gloss;paintedHeight_[key]=height;return true;
 }
 bool buildPartSources(size_t part,lab::Sources& src,bool preview,std::shared_ptr<lab::Image>& paintNormal,std::shared_ptr<lab::GrayImage>& paintGloss,std::shared_ptr<lab::GrayImage>& paintHeight){src={};paintNormal={};paintGloss={};paintHeight={};if(!loadPartDiffuse(part,src.diffuse,preview))return false;ensurePaintLayers(part,false,paintNormal,paintGloss,paintHeight);if(!paintNormal&&sources_.normal.valid())src.normal=preview?lab::ResizeImageMax(sources_.normal,PREVIEW_MAX):sources_.normal;if(!paintGloss&&sources_.gloss.valid())src.gloss=preview?lab::ResizeImageMax(sources_.gloss,PREVIEW_MAX):sources_.gloss;if(!paintHeight&&sources_.height.valid())src.height=preview?lab::ResizeImageMax(sources_.height,PREVIEW_MAX):sources_.height;return true;}
 lab::BumpPair generateForPart(size_t part){lab::Sources src;std::shared_ptr<lab::Image> pn;std::shared_ptr<lab::GrayImage> pg,ph;if(!buildPartSources(part,src,true,pn,pg,ph))return {};if(pn&&pg&&ph)return lab::PackBumpLayersPreview(*pn,*pg,*ph,PREVIEW_MAX);return lab::GenerateBumpPreview(src,settings_);}
 lab::BumpPair generateSliderBase(size_t part){lab::Sources src;lab::Image diffuse;if(!loadPartDiffuse(part,diffuse,false))return {};src.diffuse=lab::ResizeImageMax(diffuse,BRUSH_MAX);if(sources_.normal.valid())src.normal=lab::ResizeImageMax(sources_.normal,BRUSH_MAX);if(sources_.gloss.valid())src.gloss=lab::ResizeImageMax(sources_.gloss,BRUSH_MAX);if(sources_.height.valid())src.height=lab::ResizeImageMax(sources_.height,BRUSH_MAX);return lab::GenerateBumpPreview(src,settings_);}
 void applyBumpToMaterial(size_t part,const lab::BumpPair& b){if(!b.bump.valid())return;const std::wstring key=materialKey(part);for(size_t i=0;i<render_.PartCount();++i)if(materialKey(i)==key)render_.SetPartBumpImages(i,b.bump,b.bumpX,true);}

 lab::BumpPair manualBump(){
  if(!sources_.diffuse.valid())return {};const std::wstring key=materialKey(0);auto ni=paintedNormals_.find(key);auto gi=paintedGloss_.find(key);auto hi=paintedHeight_.find(key);
  if(ni!=paintedNormals_.end()&&gi!=paintedGloss_.end()&&hi!=paintedHeight_.end()&&ni->second&&gi->second&&hi->second)return lab::PackBumpLayersPreview(*ni->second,*gi->second,*hi->second,PREVIEW_MAX);
  if(useImported_&&importedBump_.valid()&&importedBumpX_.valid())return {lab::ResizeImageMax(importedBump_,PREVIEW_MAX),lab::ResizeImageMax(importedBumpX_,PREVIEW_MAX)};lab::Sources tmp;tmp.diffuse=lab::ResizeImageMax(sources_.diffuse,PREVIEW_MAX);if(sources_.normal.valid())tmp.normal=lab::ResizeImageMax(sources_.normal,PREVIEW_MAX);if(sources_.gloss.valid())tmp.gloss=lab::ResizeImageMax(sources_.gloss,PREVIEW_MAX);if(sources_.height.valid())tmp.height=lab::ResizeImageMax(sources_.height,PREVIEW_MAX);return lab::GenerateBumpPreview(tmp,settings_);
 }
 void updateManualPreview(bool replaceDiffuse=false){if(!ready_)return;lab::BumpPair b=manualBump();if(!b.bump.valid()){status(L"Diffuse отсутствует");return;}if(replaceDiffuse)render_.SetImages(lab::ResizeImageMax(sources_.diffuse,PREVIEW_MAX),b.bump,b.bumpX);else render_.SetBumpImages(b.bump,b.bumpX,true);render_.SetNormalSettings(settings_.normalStrength,settings_.invertY);render_.SetGlossStrength(std::max(settings_.gloss,0.9f));generatedBumpMode_=!useImported_;autoModelMaterials_=false;InvalidateRect(preview_,nullptr,FALSE);}
 void loadSlot(int id){auto filename=pick(window_,L"Image (*.dds;*.png;*.jpg;*.bmp;*.tif)\0*.dds;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0All files\0*.*\0",L"dds");if(filename.empty())return;std::wstring error;auto im=loadImage(filename,error);if(!im.valid()){MessageBoxW(window_,error.c_str(),L"Image",MB_ICONERROR);return;}if(id==LOAD_DIFF){sources_.diffuse=std::move(im);manualTexturePath_=filename;manualBumpAvailable_=false;}else if(id==LOAD_NORMAL){sources_.normal=std::move(im);manualBumpAvailable_=false;}else if(id==LOAD_GLOSS){sources_.gloss=std::move(im);manualBumpAvailable_=false;}else if(id==LOAD_HEIGHT){sources_.height=std::move(im);manualBumpAvailable_=false;}else if(id==LOAD_BUMP)importedBump_=std::move(im);else if(id==LOAD_BUMPX)importedBumpX_=std::move(im);if(id==LOAD_DIFF||id==LOAD_NORMAL||id==LOAD_GLOSS||id==LOAD_HEIGHT){paintedNormals_.clear();paintedGloss_.clear();paintedHeight_.clear();resetBrushCoverage();brushUndo_.clear();currentUndo_={};currentUndoTiles_.clear();brushUndoBytes_=0;brushDirtyKeys_.clear();brushDirty_=false;brushDiffuse_={};brushDiffuseKey_.clear();}if(id==LOAD_BUMP||id==LOAD_BUMPX){useImported_=importedBump_.valid()&&importedBumpX_.valid();manualBumpAvailable_=useImported_;if(useImported_)generatedBumpMode_=false;if(!useImported_){status(L"Нужна пара bump/bump#");return;}updateManualPreview(false);}else{useImported_=false;if(sources_.diffuse.valid())updateManualPreview(id==LOAD_DIFF);}refreshBrushAvailability();status(std::filesystem::path(filename).filename().wstring());}

 bool tryLoadOMF(const std::filesystem::path& file,lab::Mesh& mesh,std::wstring& loaded){std::error_code ec;if(!std::filesystem::is_regular_file(file,ec))return false;auto bytes=readFile(file.wstring());if(bytes.empty())return false;std::string reason;if(!lab::LoadOGFMotions(bytes,mesh,reason))return false;loaded=file.wstring();return true;}
 void loadExternalMotions(const std::filesystem::path& ogf,lab::Mesh& mesh,std::wstring& loaded){if(!mesh.motions.empty())return;std::vector<std::filesystem::path> refs;for(const auto& r:mesh.motionRefs){std::wstring w=widenPath(r);std::replace(w.begin(),w.end(),L'/',L'\\');std::filesystem::path p(w);if(p.extension().empty())p+=L".omf";refs.push_back(p);}if(refs.empty())refs.push_back(std::filesystem::path(ogf.stem().wstring()+L".omf"));const auto here=ogf.parent_path(),up=here.parent_path();for(const auto& ref:refs){std::vector<std::filesystem::path> candidates={here/ref,here/ref.filename(),up/ref,up/ref.filename()};for(const auto& c:candidates)if(tryLoadOMF(c,mesh,loaded))return;}}
 void setAnimationTab(bool show){if(show==animationTab_)return;if(show){TCITEMW ti={};ti.mask=TCIF_TEXT;ti.pszText=const_cast<LPWSTR>(L"Аним.");TabCtrl_InsertItem(tab_,ANIM_TAB,&ti);animationTab_=true;}else{if(animationPageActive())returnToBindPose();else stopAnimation();if(TabCtrl_GetCurSel(tab_)==ANIM_TAB){TabCtrl_SetCurSel(tab_,0);showTab(0);}TabCtrl_DeleteItem(tab_,ANIM_TAB);animationTab_=false;if(pages_[ANIM_TAB])ShowWindow(pages_[ANIM_TAB],SW_HIDE);}}
 void stopAnimation(){animPlaying_=false;syncTools();KillTimer(window_,ANIM_TIMER);if(controls_[ANIM_PLAY])SetWindowTextW(controls_[ANIM_PLAY],L"Старт");refreshBrushAvailability();}
 void returnToBindPose(){stopAnimation();animTime_=0.f;if(controls_[ANIM_TIMELINE])SendMessageW(controls_[ANIM_TIMELINE],TBM_SETPOS,TRUE,0);render_.SetBindPose();InvalidateRect(preview_,nullptr,FALSE);}
 void configureAnimation(size_t index,bool resetTime){if(!animationPageActive())return;const lab::Motion* m=render_.MotionAt(index);if(!m)return;animIndex_=index;if(resetTime)animTime_=0.f;SendMessageW(controls_[ANIM_TIMELINE],TBM_SETRANGEMIN,FALSE,0);SendMessageW(controls_[ANIM_TIMELINE],TBM_SETRANGEMAX,TRUE,int(std::max<uint32_t>(1,m->frames)-1));SendMessageW(controls_[ANIM_TIMELINE],TBM_SETPOS,TRUE,int(std::min<float>(m->frames?float(m->frames-1):0.f,animTime_*m->fps)));std::wstring err;if(!render_.SetAnimationPose(index,animTime_,SendMessageW(controls_[ANIM_LOOP],BM_GETCHECK,0,0)==BST_CHECKED,err)&&!err.empty())status(err);InvalidateRect(preview_,nullptr,FALSE);}
 void populateAnimations(){SendMessageW(controls_[ANIM_COMBO],CB_RESETCONTENT,0,0);setAnimationTab(render_.MotionCount()>0);if(!render_.MotionCount())return;for(size_t i=0;i<render_.MotionCount();++i){const auto* m=render_.MotionAt(i);std::wstring n=m?widenPath(m->name):L"motion";SendMessageW(controls_[ANIM_COMBO],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(n.c_str()));}SendMessageW(controls_[ANIM_COMBO],CB_SETCURSEL,0,0);SendMessageW(controls_[ANIM_LOOP],BM_SETCHECK,BST_CHECKED,0);SendMessageW(controls_[ANIM_SPEED],TBM_SETPOS,TRUE,100);animSpeed_=1.f;animIndex_=0;animTime_=0.f;stopAnimation();const auto* m=render_.MotionAt(0);if(m){SendMessageW(controls_[ANIM_TIMELINE],TBM_SETRANGEMIN,FALSE,0);SendMessageW(controls_[ANIM_TIMELINE],TBM_SETRANGEMAX,TRUE,int(std::max<uint32_t>(1,m->frames)-1));SendMessageW(controls_[ANIM_TIMELINE],TBM_SETPOS,TRUE,0);}render_.SetBindPose();InvalidateRect(preview_,nullptr,FALSE);}
 void toggleAnimation(){if(!render_.MotionCount()||!animationPageActive())return;animPlaying_=!animPlaying_;SetWindowTextW(controls_[ANIM_PLAY],animPlaying_?L"Пауза":L"Старт");if(animPlaying_){setMode(ViewMode::Fly);refreshBrushAvailability();animLastTick_=GetTickCount64();SetTimer(window_,ANIM_TIMER,16,nullptr);status(L"Анимация: редактирование заблокировано");}else{KillTimer(window_,ANIM_TIMER);refreshBrushAvailability();}syncTools();}
 void tickAnimation(){if(!animPlaying_)return;if(!animationPageActive()){returnToBindPose();return;}const auto* m=render_.MotionAt(animIndex_);if(!m)return;ULONGLONG now=GetTickCount64();float dt=float(now-animLastTick_)/1000.f;animLastTick_=now;dt=std::min(dt,.1f);bool loop=SendMessageW(controls_[ANIM_LOOP],BM_GETCHECK,0,0)==BST_CHECKED;animTime_+=dt*animSpeed_*std::max(.01f,m->speed);float duration=m->duration();if(duration>0.f&&animTime_>=duration){if(loop)animTime_=std::fmod(animTime_,duration);else{animTime_=std::max(0.f,duration-1.f/m->fps);stopAnimation();}}configureAnimation(animIndex_,false);}

 void generateBump(bool quiet=false,bool persistBase=true){
  useImported_=false;
  if(sources_.diffuse.valid()&&materials_.empty()){
   const std::wstring key=materialKey(0);lab::BumpPair base=generateSliderBase(0);if(!base.bump.valid()){if(!quiet)status(L"Diffuse отсутствует");return;}if(persistBase){if(!saveBaseFromPair(key,base)){status(L"Не удалось сохранить base: прежняя кисть сохранена");return;}clearBrushForKey(key);}lab::BumpPair display{lab::ResizeImageMax(base.bump,PREVIEW_MAX),lab::ResizeImageMax(base.bumpX,PREVIEW_MAX)};render_.SetBumpImages(display.bump,display.bumpX,true);render_.SetGlossStrength(.5f);generatedBumpMode_=true;manualBumpAvailable_=true;autoModelMaterials_=false;refreshBrushAvailability();if(!quiet)status(persistBase?L"Bump создан; база сохранена в temp":L"Bump preview");InvalidateRect(preview_,nullptr,FALSE);return;
  }
  if(materials_.empty()){if(!quiet)status(L"Diffuse отсутствует");return;}
  std::vector<size_t> candidates;for(size_t i=0;i<materials_.size();++i)if(render_.PartVisible(i)&&materials_[i].hasDiffuse)candidates.push_back(i);if(candidates.empty()){if(!quiet)status(L"Нет доступных мешей с diffuse");return;}
  std::vector<size_t> targets;const int selected=render_.SelectedPart();
  if(!quiet&&candidates.size()>1){std::vector<std::wstring> labels;labels.reserve(candidates.size());for(size_t part:candidates){std::wstring name=L"Mesh "+std::to_wstring(part+1);if(part<currentMesh_.parts.size()&&!currentMesh_.parts[part].texture.empty())name+=L"   "+widenPath(currentMesh_.parts[part].texture);labels.push_back(std::move(name));}if(!chooseBumpTargets(window_,candidates,labels,selected,targets)){status(L"Создание bump отменено");return;}}
  else if(selected>=0&&size_t(selected)<materials_.size()&&render_.PartVisible(size_t(selected))&&materials_[size_t(selected)].hasDiffuse)targets.push_back(size_t(selected));
  else targets=candidates;
  std::unordered_map<std::wstring,bool> done;size_t count=0;for(size_t part:targets){if(part>=materials_.size()||!render_.PartVisible(part)||!materials_[part].hasDiffuse)continue;const auto key=materialKey(part);if(done[key])continue;done[key]=true;lab::BumpPair base=generateSliderBase(part);if(!base.bump.valid())continue;if(persistBase){if(!saveBaseFromPair(key,base))continue;clearBrushForKey(key);}lab::BumpPair display{lab::ResizeImageMax(base.bump,PREVIEW_MAX),lab::ResizeImageMax(base.bumpX,PREVIEW_MAX)};applyBumpToMaterial(part,display);generatedBumpKeys_.insert(key);++count;}
  if(count){generatedBumpMode_=true;render_.SetGlossStrength(.5f);refreshBrushAvailability();if(!quiet)status(persistBase?(L"Bump создан: "+std::to_wstring(count)+L" | база сохранена в temp"):(L"Bump preview: "+std::to_wstring(count)));InvalidateRect(preview_,nullptr,FALSE);}else if(!quiet)status(L"Diffuse отсутствует");
 }
 void populateMeshList(){HWND list=controls_[MESH_LIST];if(!list)return;meshListUpdating_=true;ListView_DeleteAllItems(list);for(size_t i=0;i<render_.PartCount();++i){std::wstring name=L"Mesh "+std::to_wstring(i+1);if(i<currentMesh_.parts.size()&&!currentMesh_.parts[i].texture.empty())name+=L"  "+widenPath(currentMesh_.parts[i].texture);LVITEMW item={};item.mask=LVIF_TEXT;item.iItem=int(i);item.pszText=name.data();ListView_InsertItem(list,&item);ListView_SetCheckState(list,int(i),TRUE);}meshListUpdating_=false;}
 void syncMeshListSelection(int part){HWND list=controls_[MESH_LIST];if(!list||part<0)return;meshListUpdating_=true;ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);ListView_SetItemState(list,part,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_EnsureVisible(list,part,FALSE);meshListUpdating_=false;}

 void openOGF(){auto filename=pick(window_,L"S.T.A.L.K.E.R. OGF (*.ogf)\0*.ogf\0All files\0*.*\0",L"ogf");if(filename.empty())return;auto bytes=readFile(filename);std::string reason;lab::Mesh m;if(!lab::LoadOGFMesh(bytes,m,reason)){std::wstring why(reason.begin(),reason.end());MessageBoxW(window_,why.c_str(),L"OGF",MB_ICONWARNING);status(why);return;}bytes.clear();bytes.shrink_to_fit();std::wstring omfLoaded;loadExternalMotions(std::filesystem::path(filename),m,omfLoaded);std::wstring err;if(!render_.SetMesh(m,err)){MessageBoxW(window_,err.c_str(),L"OGF",MB_ICONERROR);return;}
  isolatedVisibility_.clear();setMode(ViewMode::Fly);currentMesh_=std::move(m);for(auto& p:currentMesh_.parts){p.vertices.clear();p.vertices.shrink_to_fit();p.indices.clear();p.indices.shrink_to_fit();}currentMesh_.bones.clear();currentMesh_.bones.shrink_to_fit();currentMesh_.motions.clear();currentMesh_.motions.shrink_to_fit();currentMesh_.motionRefs.clear();currentMesh_.motionRefs.shrink_to_fit();
  modelPath_=filename;manualTexturePath_.clear();render_.ResetView();render_.ClearSelection();sources_=lab::Sources{};materials_.assign(currentMesh_.parts.size(),PartMaterial{});paintedNormals_.clear();paintedGloss_.clear();paintedHeight_.clear();generatedBumpKeys_.clear();resetBrushCoverage();brushUndo_.clear();currentUndo_={};currentUndoTiles_.clear();brushUndoBytes_=0;brushDirtyKeys_.clear();brushDirty_=false;brushDiffuse_={};brushDiffuseKey_.clear();importedBump_={};importedBumpX_={};useImported_=false;generatedBumpMode_=false;manualBumpAvailable_=false;autoModelMaterials_=true;render_.SetNormalSettings(settings_.normalStrength,settings_.invertY);render_.SetGlossStrength(settings_.gloss);
  std::filesystem::path texturesRoot;bool hasRoot=findTexturesRoot(std::filesystem::path(filename),texturesRoot);size_t texOk=0,bumpOk=0;std::unordered_map<std::wstring,size_t> gpuOwner;
  for(size_t i=0;i<currentMesh_.parts.size();++i){auto& mat=materials_[i];const auto& part=currentMesh_.parts[i];std::filesystem::path d,b,bx;if(!part.texture.empty()&&hasRoot&&resolveOGFMaterialPaths(texturesRoot,part.texture,d,b,bx)){mat.diffuse=d;mat.bump=b;mat.bumpX=bx;mat.hasDiffuse=std::filesystem::exists(d);mat.hasBump=mat.hasDiffuse&&std::filesystem::exists(b)&&std::filesystem::exists(bx);mat.key=mat.hasDiffuse?pathKey(d):(L"missing:"+lower(widenPath(part.texture)));}else mat.key=L"part:"+std::to_wstring(i);
   const std::wstring gpuKey=mat.key+(mat.hasBump?L"|b":L"|n");auto own=gpuOwner.find(gpuKey);if(own!=gpuOwner.end()){render_.CopyPartMaterial(i,own->second);if(mat.hasDiffuse)++texOk;if(mat.hasBump)++bumpOk;continue;}
   lab::PartImages display;if(mat.hasDiffuse){std::wstring e;auto full=loadImage(mat.diffuse.wstring(),e);if(full.valid()){display.diffuse=lab::ResizeImageMax(full,PREVIEW_MAX);++texOk;}}
   if(!display.diffuse.valid())display.diffuse=lab::DefaultChecker(128);if(mat.hasBump){std::wstring e1,e2;auto bi=loadImage(mat.bump.wstring(),e1);if(bi.valid())bi=lab::ResizeImageMax(bi,PREVIEW_MAX);auto bxi=loadImage(mat.bumpX.wstring(),e2);if(bxi.valid())bxi=lab::ResizeImageMax(bxi,PREVIEW_MAX);if(bi.valid()&&bxi.valid()){display.bump=std::move(bi);display.bumpX=std::move(bxi);display.hasBump=true;++bumpOk;}}render_.SetPartImages(i,display);gpuOwner[gpuKey]=i;
  }
  bool skeleton=render_.HasSkeleton();SendMessageW(controls_[SKELETON_ENABLED],BM_SETCHECK,skeleton?BST_CHECKED:BST_UNCHECKED,0);EnableWindow(controls_[SKELETON_ENABLED],skeleton);render_.SkeletonEnabled(skeleton);populateMeshList();populateAnimations();refreshBrushAvailability();std::wstring s=L"OGF: "+std::to_wstring(currentMesh_.parts.size())+L" | Tex: "+std::to_wstring(texOk)+L" | Bump: "+std::to_wstring(bumpOk)+L" | Anim: "+std::to_wstring(render_.MotionCount());if(!omfLoaded.empty())s+=L" | OMF";status(s);InvalidateRect(preview_,nullptr,FALSE);}

 void chooseColor(int which){static COLORREF saved[16]={};CHOOSECOLORW cc={};cc.lStructSize=sizeof(cc);cc.hwndOwner=window_;cc.rgbResult=render_.GetColor(which);cc.lpCustColors=saved;cc.Flags=CC_RGBINIT|CC_FULLOPEN;if(ChooseColorW(&cc)){render_.SetColor(which,cc.rgbResult);InvalidateRect(preview_,nullptr,FALSE);}}
 std::wstring selectedTextureName()const{if(!manualTexturePath_.empty())return cleanName(manualTexturePath_.stem().wstring());int selected=render_.SelectedPart();if(selected>=0&&size_t(selected)<currentMesh_.parts.size()&&render_.PartVisible(size_t(selected))){auto n=textureStem(currentMesh_.parts[size_t(selected)].texture);if(!n.empty())return n;}for(size_t i=0;i<currentMesh_.parts.size();++i){if(!render_.PartVisible(i))continue;auto n=textureStem(currentMesh_.parts[i].texture);if(!n.empty())return n;}return {};}
 std::wstring customName()const{wchar_t text[512]={};if(controls_[EXPORT_NAME])GetWindowTextW(controls_[EXPORT_NAME],text,512);return cleanName(text);}
 bool exportPartSources(size_t part,lab::Sources& src,std::shared_ptr<lab::Image>& paintNormal,std::shared_ptr<lab::GrayImage>& paintGloss,std::shared_ptr<lab::GrayImage>& paintHeight){return buildPartSources(part,src,false,paintNormal,paintGloss,paintHeight);}
 void exportDDS(){
  bool useTexture=controls_[USE_TEX_NAME]&&SendMessageW(controls_[USE_TEX_NAME],BM_GETCHECK,0,0)==BST_CHECKED;std::wstring stem;if(useTexture)stem=selectedTextureName();if(stem.empty())stem=customName();if(stem.empty()&&!modelPath_.empty())stem=cleanName(modelPath_.stem().wstring());if(stem.empty())stem=L"material";std::wstring modelFolder=!modelPath_.empty()?cleanName(modelPath_.stem().wstring()):stem;if(modelFolder.empty())modelFolder=L"material";std::filesystem::path folder=exeDir()/L"export"/modelFolder;std::error_code ec;std::filesystem::create_directories(folder,ec);if(ec){std::wstring ws=L"Не удалось создать "+folder.wstring();MessageBoxW(window_,ws.c_str(),L"Export",MB_ICONERROR);status(ws);return;}
  std::filesystem::path base=folder/stem;std::string err;bool ok=false;
  if(materials_.empty()){
   if(!sources_.diffuse.valid()){status(L"Diffuse отсутствует");return;}const std::wstring key=materialKey(0);auto ni=paintedNormals_.find(key);auto gi=paintedGloss_.find(key);auto hi=paintedHeight_.find(key);if(ni!=paintedNormals_.end()&&gi!=paintedGloss_.end()&&hi!=paintedHeight_.end()&&ni->second&&gi->second&&hi->second){lab::Image bn;lab::GrayImage bg,bh;const lab::GrayImage* baseHeight=loadTempLayers(key,L"base",bn,bg,bh)?&bh:nullptr;ok=lab::ExportBumpLayers(*ni->second,*gi->second,*hi->second,sources_.diffuse.w,sources_.diffuse.h,base.wstring(),err,baseHeight);}else ok=lab::ExportBumpPair(sources_,settings_,base.wstring(),err);
  }else{
   lab::Sources src;std::shared_ptr<lab::Image> pn;std::shared_ptr<lab::GrayImage> pg,ph;int part=render_.SelectedPart();if(part<0||!render_.PartVisible(size_t(part))){part=-1;for(size_t i=0;i<materials_.size();++i)if(render_.PartVisible(i)&&materials_[i].hasDiffuse){part=int(i);break;}}if(part<0||!exportPartSources(size_t(part),src,pn,pg,ph)){status(L"Нет доступного меша для экспорта");return;}if(pn&&pg&&ph){lab::Image bn;lab::GrayImage bg,bh;const std::wstring key=materialKey(size_t(part));const lab::GrayImage* baseHeight=loadTempLayers(key,L"base",bn,bg,bh)?&bh:nullptr;ok=lab::ExportBumpLayers(*pn,*pg,*ph,src.diffuse.w,src.diffuse.h,base.wstring(),err,baseHeight);}else ok=lab::ExportBumpPair(src,settings_,base.wstring(),err);
  }
  if(!ok){std::wstring ws(err.begin(),err.end());MessageBoxW(window_,ws.c_str(),L"Export",MB_ICONERROR);status(ws);return;}status((folder/(stem+L"_bump.dds")).wstring()+L" | base + brush");
 }
 void resetScene(){render_.Reset();SendMessageW(controls_[SLIDER_LIGHT_AZ],TBM_SETPOS,TRUE,52);SendMessageW(controls_[SLIDER_LIGHT_EL],TBM_SETPOS,TRUE,37);saveLight();InvalidateRect(preview_,nullptr,FALSE);status(L"Сброс");}
 void clearSelection(){endInteraction();restoreIsolation();render_.ClearSelection();render_.ClearHover();if(controls_[MESH_LIST]){meshListUpdating_=true;ListView_SetItemState(controls_[MESH_LIST],-1,0,LVIS_SELECTED|LVIS_FOCUSED);meshListUpdating_=false;}refreshBrushAvailability();InvalidateRect(preview_,nullptr,FALSE);status(L"Выделение снято");}
 void showAllMeshes(){endInteraction();isolatedVisibility_.clear();meshListUpdating_=true;for(size_t i=0;i<render_.PartCount();++i){render_.SetPartVisible(i,true);ListView_SetCheckState(controls_[MESH_LIST],int(i),TRUE);}meshListUpdating_=false;refreshBrushAvailability();InvalidateRect(preview_,nullptr,FALSE);}
 void hideSelectedMesh(){endInteraction();restoreIsolation();int p=render_.SelectedPart();if(p<0)return;render_.SetPartVisible(size_t(p),false);meshListUpdating_=true;ListView_SetCheckState(controls_[MESH_LIST],p,FALSE);ListView_SetItemState(controls_[MESH_LIST],p,0,LVIS_SELECTED|LVIS_FOCUSED);meshListUpdating_=false;refreshBrushAvailability();InvalidateRect(preview_,nullptr,FALSE);}

 void beginBrushStroke(){currentUndo_={};currentUndoTiles_.clear();}
 void finishBrushStroke(){if(currentUndo_.tiles.empty()){currentUndo_={};currentUndoTiles_.clear();return;}const size_t bytes=currentUndo_.bytes();brushUndo_.push_back(std::move(currentUndo_));brushUndoBytes_+=bytes;currentUndo_={};currentUndoTiles_.clear();while((brushUndoBytes_>UNDO_MAX_BYTES||brushUndo_.size()>16)&&brushUndo_.size()>1){brushUndoBytes_-=brushUndo_.front().bytes();brushUndo_.pop_front();}}
 void captureUndoTiles(const std::wstring& key,int part,BrushTarget target,const lab::Image& normal,const lab::GrayImage& gloss,const lab::GrayImage& height,float u,float v,float radiusPixels){
  if(!normal.valid()||!gloss.valid()||!height.valid()||normal.w!=gloss.w||normal.h!=gloss.h||normal.w!=height.w||normal.h!=height.h)return;if(currentUndo_.key.empty()){currentUndo_.key=key;currentUndo_.part=part;currentUndo_.target=target;}else if(currentUndo_.key!=key||currentUndo_.target!=target){finishBrushStroke();currentUndo_.key=key;currentUndo_.part=part;currentUndo_.target=target;}
  const int w=int(normal.w),h=int(normal.h),cx=int(std::floor((u-std::floor(u))*w)),cy=int(std::floor((v-std::floor(v))*h)),r=int(std::ceil(radiusPixels));const auto tilesX=lab::WrappedBrushTiles(cx,r,w,int(UNDO_TILE)),tilesY=lab::WrappedBrushTiles(cy,r,h,int(UNDO_TILE));
  for(int tileY:tilesY)for(int tileX:tilesX){const uint32_t tx=uint32_t(tileX),ty=uint32_t(tileY);const uint64_t id=(uint64_t(ty)<<32)|tx;if(!currentUndoTiles_.insert(id).second)continue;BrushUndoTile t;t.tx=tx;t.ty=ty;t.w=std::min<uint32_t>(UNDO_TILE,normal.w-tx*UNDO_TILE);t.h=std::min<uint32_t>(UNDO_TILE,normal.h-ty*UNDO_TILE);t.target=target;const bool normalTarget=target==BrushTarget::Relief||target==BrushTarget::Sharp||target==BrushTarget::Blur;if(normalTarget)t.normal.resize(size_t(t.w)*t.h);else t.scalar.resize(size_t(t.w)*t.h);for(uint32_t y=0;y<t.h;++y){const size_t src=size_t(ty*UNDO_TILE+y)*normal.w+tx*UNDO_TILE;if(normalTarget)std::copy_n(normal.pixels.begin()+std::ptrdiff_t(src),t.w,t.normal.begin()+std::ptrdiff_t(size_t(y)*t.w));else if(target==BrushTarget::GlossAdd||target==BrushTarget::GlossSub)std::copy_n(gloss.pixels.begin()+std::ptrdiff_t(src),t.w,t.scalar.begin()+std::ptrdiff_t(size_t(y)*t.w));else std::copy_n(height.pixels.begin()+std::ptrdiff_t(src),t.w,t.scalar.begin()+std::ptrdiff_t(size_t(y)*t.w));}currentUndo_.tiles.push_back(std::move(t));}
 }
 void undoBrush(){if(!currentUndo_.tiles.empty())finishBrushStroke();if(brushUndo_.empty()){status(L"Нет действий кисти");return;}BrushUndoStep step=std::move(brushUndo_.back());brushUndo_.pop_back();brushUndoBytes_-=std::min(brushUndoBytes_,step.bytes());auto ni=paintedNormals_.find(step.key);auto gi=paintedGloss_.find(step.key);auto hi=paintedHeight_.find(step.key);if(ni==paintedNormals_.end()||gi==paintedGloss_.end()||hi==paintedHeight_.end()||!ni->second||!gi->second||!hi->second){status(L"Undo недоступен");return;}lab::Image& n=*ni->second;lab::GrayImage& g=*gi->second;lab::GrayImage& h=*hi->second;for(const auto& t:step.tiles)for(uint32_t y=0;y<t.h;++y){const size_t dst=size_t(t.ty*UNDO_TILE+y)*n.w+t.tx*UNDO_TILE;if(t.target==BrushTarget::Relief||t.target==BrushTarget::Sharp||t.target==BrushTarget::Blur)std::copy_n(t.normal.begin()+std::ptrdiff_t(size_t(y)*t.w),t.w,n.pixels.begin()+std::ptrdiff_t(dst));else if(t.target==BrushTarget::GlossAdd||t.target==BrushTarget::GlossSub)std::copy_n(t.scalar.begin()+std::ptrdiff_t(size_t(y)*t.w),t.w,g.pixels.begin()+std::ptrdiff_t(dst));else std::copy_n(t.scalar.begin()+std::ptrdiff_t(size_t(y)*t.w),t.w,h.pixels.begin()+std::ptrdiff_t(dst));}int part=step.part;if(part<0||size_t(part)>=render_.PartCount()||materialKey(size_t(part))!=step.key){part=-1;for(size_t i=0;i<render_.PartCount();++i)if(materialKey(i)==step.key){part=int(i);break;}}if(part>=0)updateBrushPreview(size_t(part),false);resetBrushCoverage();brushDirtyKeys_.insert(step.key);brushDirty_=true;status(L"Кисть: отменено");}
 void commitBrushChanges(){
  endInteraction();if(!currentUndo_.tiles.empty())finishBrushStroke();
  size_t saved=0;std::unordered_set<std::wstring> failed;
  for(const auto& key:brushDirtyKeys_){
   auto ni=paintedNormals_.find(key);auto gi=paintedGloss_.find(key);auto hi=paintedHeight_.find(key);
   const bool ok=ni!=paintedNormals_.end()&&gi!=paintedGloss_.end()&&hi!=paintedHeight_.end()&&ni->second&&gi->second&&hi->second&&saveTempLayers(key,L"brush",*ni->second,*gi->second,*hi->second);
   if(!ok){failed.insert(key);continue;}++saved;
   for(size_t i=0;i<render_.PartCount();++i)if(materialKey(i)==key){updateBrushPreview(i,true);break;}
  }
  brushDirtyKeys_=std::move(failed);brushDirty_=!brushDirtyKeys_.empty();
  for(auto it=brushUndo_.begin();it!=brushUndo_.end();){if(brushDirtyKeys_.count(it->key)==0)it=brushUndo_.erase(it);else ++it;}
  brushUndoBytes_=0;for(const auto& step:brushUndo_)brushUndoBytes_+=step.bytes();
  currentUndo_={};currentUndoTiles_.clear();resetBrushCoverage();
  if(brushDirty_)status(L"Не удалось сохранить слоёв: "+std::to_wstring(brushDirtyKeys_.size())+L". Правки и отмена сохранены в памяти; повторите Ctrl+S.");
  else status(L"Кисть: изменения применены | temp: "+std::to_wstring(saved));
 }
 void updateBrushCursor(int x,int y){if(!brushMode_){render_.ClearBrushCursor();return;}lab::Renderer::PickHit hit;if(!render_.PickUV(x,y,hit)||!hasEditableBump(size_t(hit.part))||(render_.HasSelection()&&!render_.PartSelected(size_t(hit.part)))){render_.ClearBrushCursor();return;}float ru=brushRadiusUv_,rv=brushRadiusUv_;std::shared_ptr<lab::Image> pn;std::shared_ptr<lab::GrayImage> pg,ph;if(ensurePaintLayers(size_t(hit.part),false,pn,pg,ph)&&pn&&pn->valid()){const float px=std::max(1.35f,brushRadiusUv_*float(std::max(pn->w,pn->h)));ru=px/float(pn->w);rv=px/float(pn->h);}render_.SetBrushCursor(hit,ru,rv);}
 void loadBrushPreset(){auto file=pick(window_,L"Normal preset (*.krnp;*.dds;*.png;*.jpg;*.bmp)\0*.krnp;*.dds;*.png;*.jpg;*.jpeg;*.bmp\0All files\0*.*\0",L"krnp");if(file.empty())return;std::wstring ext=lower(std::filesystem::path(file).extension().wstring());if(ext==L".krnp"){std::string e;lab::Image p;float legacyGloss=.45f;if(!lab::LoadNormalPreset(file,p,legacyGloss,e)){std::wstring w(e.begin(),e.end());MessageBoxW(window_,w.c_str(),L"Preset",MB_ICONERROR);return;}brushPreset_=std::move(p);}else{std::wstring e;auto p=loadImage(file,e);if(!p.valid()){MessageBoxW(window_,e.c_str(),L"Preset",MB_ICONERROR);return;}brushPreset_=std::move(p);}resetBrushCoverage();status(std::filesystem::path(file).filename().wstring());}
 void saveBrushPreset(){auto file=savePick(window_,L"KichRay normal preset (*.krnp)\0*.krnp\0",L"krnp");if(file.empty())return;std::string e;if(!lab::SaveNormalPreset(brushPreset_,.45f,file,e)){std::wstring w(e.begin(),e.end());MessageBoxW(window_,w.c_str(),L"Preset",MB_ICONERROR);return;}status(std::filesystem::path(file).filename().wstring());}
 void updateBrushPreview(size_t part,bool finalQuality=false){std::shared_ptr<lab::Image> pn;std::shared_ptr<lab::GrayImage> pg,ph;if(!hasEditableBump(part)||!ensurePaintLayers(part,false,pn,pg,ph)||!pn||!pg||!ph)return;const uint32_t limit=finalQuality?PREVIEW_MAX:BRUSH_PREVIEW_MAX;lab::Image baseN;lab::GrayImage baseG,baseH;const std::wstring key=materialKey(part);const lab::GrayImage* baseHeight=loadTempLayers(key,L"base",baseN,baseG,baseH)?&baseH:nullptr;lab::BumpPair b=lab::PackBumpLayersPreview(*pn,*pg,*ph,limit,baseHeight);if(b.bump.valid()){applyBumpToMaterial(part,b);generatedBumpMode_=true;render_.SetNormalSettings(1.f,false);render_.SetGlossStrength(.5f);InvalidateRect(preview_,nullptr,FALSE);}}
 void paintStrokeTo(int x,int y,bool forcePreview=false){
  const POINT from=brushScreenValid_?brushScreen_:POINT{x,y};
  lab::SampleBrushStroke(from.x,from.y,x,y,brushScreenValid_,[&](int px,int py){paintAt(px,py);});
  brushScreen_={x,y};brushScreenValid_=true;
  const ULONGLONG now=GetTickCount64();
  if(currentUndo_.part>=0&&(forcePreview||now-lastBrushPreview_>=BRUSH_PREVIEW_MS)){lastBrushPreview_=now;updateBrushPreview(size_t(currentUndo_.part));}
  if(brushDirty_)status(std::wstring(L"Кисть: ")+brushTargetName()+L" | Ctrl+Z: отмена | Ctrl+S: применить");
 }
 void paintAt(int x,int y){
  lab::Renderer::PickHit hit;if(!render_.PickUV(x,y,hit)){render_.ClearBrushCursor();return;}const size_t part=size_t(hit.part);if(part>=render_.PartCount())return;if(render_.HasSelection()&&!render_.PartSelected(part)){render_.ClearBrushCursor();status(L"Кисть работает только с выделенным мешем");return;}if(!hasEditableBump(part)){render_.ClearBrushCursor();status(L"Кисть недоступна: сначала загрузите или создайте bump");return;}if(!render_.HasSelection()){render_.SelectPart(hit.part,false);syncMeshListSelection(hit.part);refreshBrushAvailability();}
  std::shared_ptr<lab::Image> normal;std::shared_ptr<lab::GrayImage> gloss,height;if(!ensurePaintLayers(part,true,normal,gloss,height)||!normal||!gloss||!height)return;
  const float radius=std::max(1.35f,brushRadiusUv_*float(std::max(normal->w,normal->h))),radiusU=radius/float(normal->w),radiusV=radius/float(normal->h);render_.SetBrushCursor(hit,radiusU,radiusV);const std::wstring key=materialKey(part);ensureBrushBase(key,*normal,*gloss,*height);const BrushTarget op=effectiveBrushTarget();const float force=std::clamp(brushStrength_,0.f,1.f);
  auto stamp=[&](float u,float v){captureUndoTiles(key,hit.part,op,*normal,*gloss,*height,u,v,radius);switch(op){case BrushTarget::Relief:lab::StampNormalStrengthLayer(*normal,brushBaseNormal_,brushCoverage_,brushPreset_,u,v,radius,force,brushInvert_,brushHardness_);break;case BrushTarget::HeightAdd:lab::StampScalarDeltaLayer(*height,brushBaseScalar_,brushCoverage_,brushPreset_,u,v,radius,force,brushHardness_);break;case BrushTarget::HeightSub:lab::StampScalarDeltaLayer(*height,brushBaseScalar_,brushCoverage_,brushPreset_,u,v,radius,-force,brushHardness_);break;case BrushTarget::GlossAdd:lab::StampScalarDeltaLayer(*gloss,brushBaseScalar_,brushCoverage_,brushPreset_,u,v,radius,force,brushHardness_);break;case BrushTarget::GlossSub:lab::StampScalarDeltaLayer(*gloss,brushBaseScalar_,brushCoverage_,brushPreset_,u,v,radius,-force,brushHardness_);break;case BrushTarget::Sharp:lab::StampNormalFilterLayer(*normal,brushBaseNormal_,brushCoverage_,brushPreset_,u,v,radius,force,true,brushHardness_);break;case BrushTarget::Blur:lab::StampNormalFilterLayer(*normal,brushBaseNormal_,brushCoverage_,brushPreset_,u,v,radius,force,false,brushHardness_);break;}};
  stamp(hit.u,hit.v);
  brushDirty_=true;brushDirtyKeys_.insert(key);
 }
 void clearBrushLayer(){int p=render_.SelectedPart();if(p<0)return;const std::wstring key=materialKey(size_t(p));paintedNormals_.erase(key);paintedGloss_.erase(key);paintedHeight_.erase(key);resetBrushCoverage();brushUndo_.clear();currentUndo_={};currentUndoTiles_.clear();brushUndoBytes_=0;brushDirtyKeys_.erase(key);brushDirty_=!brushDirtyKeys_.empty();brushDiffuse_={};brushDiffuseKey_.clear();std::error_code ec;std::filesystem::remove(tempLayerPath(key,L"brush"),ec);lab::Image n;lab::GrayImage g,h;if(loadTempLayers(key,L"base",n,g,h)){lab::BumpPair b=lab::PackBumpLayersPreview(n,g,h,PREVIEW_MAX);if(b.bump.valid())applyBumpToMaterial(size_t(p),b);}else if(size_t(p)<materials_.size()&&materials_[size_t(p)].hasBump){std::wstring e1,e2;auto b=loadImage(materials_[size_t(p)].bump.wstring(),e1),bx=loadImage(materials_[size_t(p)].bumpX.wstring(),e2);if(b.valid()&&bx.valid()){b=lab::ResizeImageMax(b,PREVIEW_MAX);bx=lab::ResizeImageMax(bx,PREVIEW_MAX);for(size_t i=0;i<materials_.size();++i)if(materialKey(i)==key)render_.SetPartBumpImages(i,b,bx,true);}}else if(materials_.empty()&&useImported_&&importedBump_.valid()&&importedBumpX_.valid()){render_.SetBumpImages(lab::ResizeImageMax(importedBump_,PREVIEW_MAX),lab::ResizeImageMax(importedBumpX_,PREVIEW_MAX),true);}InvalidateRect(preview_,nullptr,FALSE);status(L"Кисть очищена; восстановлена base");}


 void click(int id){
  const bool animationControl=id==ANIM_PLAY||id==ANIM_LOOP||id==COLOR_LIGHT||id==COLOR_AMBIENT||id==ALPHA_ENABLED||id==TOOL_FLY||id==TOOL_ORBIT||id==TOOL_OUTLINE||id==TOOL_LIGHT||id==TOOL_FRAME||id==TOOL_WIRE;
  if(editingLocked()&&!animationControl){status(L"Редактирование заблокировано во время анимации");return;}
  if(id>=TOOL_FLY&&id<=TOOL_RESET){
   switch(id){
    case TOOL_FLY:setMode(ViewMode::Fly);break;case TOOL_ORBIT:setMode(ViewMode::Orbit);break;
    case TOOL_CHOOSE:setMode(ViewMode::Choose);break;case TOOL_LIGHT:setMode(ViewMode::Light);break;
    case TOOL_BRUSH:setMode(ViewMode::Brush);break;case TOOL_MODEL:setMode(viewMode_==ViewMode::Model?ViewMode::Fly:ViewMode::Model);break;
    case TOOL_OUTLINE:outline_=!outline_;render_.OutlineEnabled(outline_);break;
    case TOOL_WIRE:wireframe_=!wireframe_;render_.WireframeEnabled(wireframe_);break;
    case TOOL_FRAME:endInteraction();render_.FrameSelection();break;case TOOL_ISOLATE:toggleIsolation();break;
    case TOOL_UNDO:endInteraction();undoBrush();break;case TOOL_APPLY:endInteraction();commitBrushChanges();break;
    case TOOL_RESET:endInteraction();render_.ResetView();break;
   }syncTools();SetFocus(preview_);InvalidateRect(preview_,nullptr,FALSE);return;
  }
  switch(id){
   case LOAD_DIFF:case LOAD_NORMAL:case LOAD_GLOSS:case LOAD_HEIGHT:case LOAD_BUMP:case LOAD_BUMPX:loadSlot(id);break;
   case LOAD_OGF:openOGF();break;case GENERATE:generateBump(false);break;case EXPORT:exportDDS();break;case RESET:resetScene();break;case CLEAR_SELECTION:clearSelection();break;
   case COLOR_TINT:chooseColor(0);break;case COLOR_LIGHT:chooseColor(1);break;case COLOR_AMBIENT:chooseColor(2);break;
   case INVERT_Y:settings_.invertY=SendMessageW(controls_[INVERT_Y],BM_GETCHECK,0,0)==BST_CHECKED;if(!brushMode_)render_.SetNormalSettings(settings_.normalStrength,settings_.invertY);InvalidateRect(preview_,nullptr,FALSE);break;
   case ALPHA_ENABLED:updateAlphaSettings();break;
   case BUMP_ENABLED:render_.BumpEnabled(SendMessageW(controls_[BUMP_ENABLED],BM_GETCHECK,0,0)==BST_CHECKED);InvalidateRect(preview_,nullptr,FALSE);break;
   case SKELETON_ENABLED:render_.SkeletonEnabled(SendMessageW(controls_[SKELETON_ENABLED],BM_GETCHECK,0,0)==BST_CHECKED);InvalidateRect(preview_,nullptr,FALSE);break;
   case SELECT_MODE:setMode(SendMessageW(controls_[SELECT_MODE],BM_GETCHECK,0,0)==BST_CHECKED?ViewMode::Choose:ViewMode::Fly);break;
   case ANIM_PLAY:toggleAnimation();break;case MESH_SHOW_ALL:showAllMeshes();break;case MESH_HIDE_SELECTED:hideSelectedMesh();break;
   case BRUSH_MODE:setMode(SendMessageW(controls_[BRUSH_MODE],BM_GETCHECK,0,0)==BST_CHECKED?ViewMode::Brush:ViewMode::Fly);break;
   case BRUSH_TARGET_RELIEF:setBrushTarget(BrushTarget::Relief,BRUSH_TARGET_RELIEF);break;
   case BRUSH_TARGET_HEIGHT_ADD:setBrushTarget(BrushTarget::HeightAdd,BRUSH_TARGET_HEIGHT_ADD);break;
   case BRUSH_TARGET_HEIGHT_SUB:setBrushTarget(BrushTarget::HeightSub,BRUSH_TARGET_HEIGHT_SUB);break;
   case BRUSH_TARGET_GLOSS_ADD:setBrushTarget(BrushTarget::GlossAdd,BRUSH_TARGET_GLOSS_ADD);break;
   case BRUSH_TARGET_GLOSS_SUB:setBrushTarget(BrushTarget::GlossSub,BRUSH_TARGET_GLOSS_SUB);break;
   case BRUSH_TARGET_SHARP:setBrushTarget(BrushTarget::Sharp,BRUSH_TARGET_SHARP);break;
   case BRUSH_TARGET_BLUR:setBrushTarget(BrushTarget::Blur,BRUSH_TARGET_BLUR);break;
   case BRUSH_INVERT:brushInvert_=SendMessageW(controls_[BRUSH_INVERT],BM_GETCHECK,0,0)==BST_CHECKED;resetBrushCoverage();status(std::wstring(L"Кисть: ")+brushTargetName()+(brushInvert_?L" [инверсия]":L""));break;
   case PRESET_LOAD:loadBrushPreset();break;case PRESET_SAVE:saveBrushPreset();break;case BRUSH_CLEAR:clearBrushLayer();break;case BRUSH_APPLY:commitBrushChanges();break;
  }
 }
 void updateAlphaSettings(){
  const bool enabled=SendMessageW(controls_[ALPHA_ENABLED],BM_GETCHECK,0,0)==BST_CHECKED;
  const int mode=std::clamp(int(SendMessageW(controls_[ALPHA_MODE],CB_GETCURSEL,0,0)),0,2);
  render_.AlphaEnabled(enabled);render_.SetAlphaPreview(lab::AlphaPreview(mode));render_.SetAlphaCutoff(float(SendMessageW(controls_[ALPHA_CUTOFF],TBM_GETPOS,0,0))/255.f);
  EnableWindow(controls_[ALPHA_MODE],enabled);EnableWindow(controls_[ALPHA_CUTOFF],enabled&&mode!=2);
  render_.ClearHover();render_.ClearBrushCursor();InvalidateRect(preview_,nullptr,FALSE);
 }
 void movedSlider(int id,bool persistBase=false){
  if(id==SLIDER_NORMAL){settings_.normalStrength=SendMessageW(controls_[id],TBM_GETPOS,0,0)/100.f;if(!brushMode_)render_.SetNormalSettings(settings_.normalStrength,settings_.invertY);InvalidateRect(preview_,nullptr,FALSE);}
  else if(id==SLIDER_GLOSS){settings_.gloss=SendMessageW(controls_[id],TBM_GETPOS,0,0)/200.f;if(generatedBumpMode_&&!brushMode_)generateBump(true,persistBase);else if(!brushMode_){render_.SetGlossStrength(settings_.gloss);InvalidateRect(preview_,nullptr,FALSE);}}
  else if(id==SLIDER_HEIGHT){settings_.height=SendMessageW(controls_[id],TBM_GETPOS,0,0)/200.f;if(generatedBumpMode_&&!brushMode_)generateBump(true,persistBase);}
  else if(id==SLIDER_BLUR){settings_.blur=SendMessageW(controls_[id],TBM_GETPOS,0,0)/100.f;if(generatedBumpMode_&&!brushMode_)generateBump(true,persistBase);}
  else if(id==SLIDER_SHARP){settings_.sharpness=SendMessageW(controls_[id],TBM_GETPOS,0,0)/100.f;if(generatedBumpMode_&&!brushMode_)generateBump(true,persistBase);}
  else if(id==ALPHA_CUTOFF){updateAlphaSettings();}
  else if(id==SLIDER_LIGHT_AZ||id==SLIDER_LIGHT_EL){float az=float(SendMessageW(controls_[SLIDER_LIGHT_AZ],TBM_GETPOS,0,0));float el=float(SendMessageW(controls_[SLIDER_LIGHT_EL],TBM_GETPOS,0,0));render_.SetLightOrbitDegrees(az,el);saveLight();InvalidateRect(preview_,nullptr,FALSE);}
  else if(id==ANIM_TIMELINE){const auto* m=render_.MotionAt(animIndex_);if(m){stopAnimation();animTime_=float(SendMessageW(controls_[ANIM_TIMELINE],TBM_GETPOS,0,0))/m->fps;configureAnimation(animIndex_,false);}}
  else if(id==ANIM_SPEED){animSpeed_=SendMessageW(controls_[ANIM_SPEED],TBM_GETPOS,0,0)/100.f;}
  else if(id==BRUSH_RADIUS){resetBrushCoverage();const int value=int(SendMessageW(controls_[BRUSH_RADIUS],TBM_GETPOS,0,0));brushRadiusUv_=value/1000.f;wchar_t text[64];swprintf_s(text,L"Размер: %.1f%%",value/10.f);SetWindowTextW(controls_[BRUSH_RADIUS_TEXT],text);POINT p{};GetCursorPos(&p);ScreenToClient(preview_,&p);updateBrushCursor(p.x,p.y);InvalidateRect(preview_,nullptr,FALSE);}
  else if(id==BRUSH_STRENGTH){resetBrushCoverage();const int value=int(SendMessageW(controls_[BRUSH_STRENGTH],TBM_GETPOS,0,0));brushStrength_=brushStrengthFromSlider(value);wchar_t text[64];const float pct=brushStrength_*100.f;if(pct<1.f)swprintf_s(text,L"Сила: %.3f%%",pct);else swprintf_s(text,L"Сила: %.1f%%",pct);SetWindowTextW(controls_[BRUSH_STRENGTH_TEXT],text);}
  else if(id==BRUSH_HARDNESS){resetBrushCoverage();const int value=int(SendMessageW(controls_[BRUSH_HARDNESS],TBM_GETPOS,0,0));brushHardness_=value/100.f;wchar_t text[64];swprintf_s(text,L"Резкость края: %d%%",value);SetWindowTextW(controls_[BRUSH_HARDNESS_TEXT],text);}
 }

public:
 static LRESULT CALLBACK mainProc(HWND hwnd,UINT message,WPARAM w,LPARAM l){auto* a=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(message==WM_NCCREATE){a=reinterpret_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));a->window_=hwnd;}return a?a->handle(hwnd,message,w,l):DefWindowProcW(hwnd,message,w,l);}
 static LRESULT CALLBACK viewProc(HWND hwnd,UINT message,WPARAM w,LPARAM l){auto* a=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(message==WM_NCCREATE){a=reinterpret_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));}return a?a->view(hwnd,message,w,l):DefWindowProcW(hwnd,message,w,l);}
 LRESULT view(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
  switch(msg){
   case WM_ERASEBKGND:return 1;
   case WM_PAINT:{PAINTSTRUCT ps;BeginPaint(hwnd,&ps);if(ready_)render_.Draw();EndPaint(hwnd,&ps);return 0;}
   case WM_SIZE:{if(ready_&&LOWORD(lp)>0&&HIWORD(lp)>0){std::wstring err;if(!render_.Resize(LOWORD(lp),HIWORD(lp),err))status(err);}return 0;}
   case WM_LBUTTONDOWN:{
    endInteraction();SetFocus(hwnd);last_={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
    if(!animPlaying_&&selectionMode_){const int hit=render_.PickGeometry(last_.x,last_.y);
     if(hit>=0){syncMeshListSelection(hit);refreshBrushAvailability();status(L"Mesh "+std::to_wstring(hit+1)+L" выбран");}
     InvalidateRect(hwnd,nullptr,FALSE);return 0;
    }
    if(!animPlaying_&&brushMode_){beginBrushStroke();brushDown_=true;brushScreenValid_=false;paintStrokeTo(last_.x,last_.y,true);}
    else lDown_=true;
    SetCapture(hwnd);return 0;
   }
   case WM_RBUTTONDOWN:{
    endInteraction();SetFocus(hwnd);last_={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
    if(selectionMode_&&!animPlaying_){clearSelection();return 0;}
    rDown_=true;render_.ClearBrushCursor();SetCapture(hwnd);return 0;
   }
   case WM_MBUTTONDOWN:endInteraction();SetFocus(hwnd);mDown_=true;last_={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};render_.ClearBrushCursor();SetCapture(hwnd);return 0;
   case WM_MOUSEMOVE:{
    TRACKMOUSEEVENT tme={sizeof(tme),TME_LEAVE,hwnd,0};TrackMouseEvent(&tme);
    const POINT at={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};const float dx=float(at.x-last_.x),dy=float(at.y-last_.y);last_=at;
    bool changed=false;
    if(!animPlaying_&&brushDown_&&(wp&MK_LBUTTON)){paintStrokeTo(at.x,at.y);changed=true;}
    else if(lDown_&&(wp&MK_LBUTTON)){
     if(viewMode_==ViewMode::Light)render_.OrbitLight(dx,dy);
     else if(modelRotateMode_&&!animationPageActive()&&!animPlaying_)render_.RotateModel(dx,dy);
     else render_.OrbitCamera(dx,dy);changed=true;
    }else if(rDown_&&(wp&MK_RBUTTON)){render_.OrbitCamera(dx,dy);changed=true;}
    else if(mDown_&&(wp&MK_MBUTTON)){render_.PanCamera(dx,dy);changed=true;}
    else if(!animPlaying_&&brushMode_){updateBrushCursor(at.x,at.y);changed=true;}
    else if(!animPlaying_&&selectionMode_&&GetTickCount64()-hoverLastTick_>=33){
     hoverLastTick_=GetTickCount64();lab::Renderer::PickHit hit;
     if(render_.PickUV(at.x,at.y,hit))render_.SetHoverPart(hit.part);else render_.ClearHover();changed=true;
    }
    if(changed)InvalidateRect(hwnd,nullptr,FALSE);return 0;
   }
   case WM_MOUSELEAVE:if(!brushDown_)render_.ClearBrushCursor();render_.ClearHover();InvalidateRect(hwnd,nullptr,FALSE);return 0;
   case WM_LBUTTONUP:if(brushDown_)paintStrokeTo(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));endInteraction();if(brushMode_)updateBrushCursor(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));InvalidateRect(hwnd,nullptr,FALSE);syncTools();return 0;
   case WM_RBUTTONUP:case WM_MBUTTONUP:endInteraction();return 0;
   case WM_CAPTURECHANGED:if(reinterpret_cast<HWND>(lp)!=hwnd)endInteraction(false);return 0;
   case WM_CANCELMODE:case WM_KILLFOCUS:endInteraction();render_.ClearBrushCursor();render_.ClearHover();InvalidateRect(hwnd,nullptr,FALSE);return 0;
   case WM_MOUSEWHEEL:if(brushMode_&&(GET_KEYSTATE_WPARAM(wp)&MK_CONTROL))adjustBrushRadius(GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*2);else render_.Zoom(float(GET_WHEEL_DELTA_WPARAM(wp))/WHEEL_DELTA);InvalidateRect(hwnd,nullptr,FALSE);return 0;
   case WM_SETCURSOR:if(LOWORD(lp)==HTCLIENT){const wchar_t* cursor=brushMode_||selectionMode_?IDC_CROSS:(viewMode_==ViewMode::Light||viewMode_==ViewMode::Model?IDC_SIZEALL:IDC_ARROW);SetCursor(LoadCursorW(nullptr,cursor));return TRUE;}break;
  }
  return DefWindowProcW(hwnd,msg,wp,lp);
 }
 LRESULT handle(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
  switch(msg){
   case WM_GETMINMAXINFO:{auto* info=reinterpret_cast<MINMAXINFO*>(lp);info->ptMinTrackSize={ui(1140),ui(580)};return 0;}
   case WM_PAINT:{PAINTSTRUCT ps;BeginPaint(hwnd,&ps);RECT r{};GetClientRect(hwnd,&r);FillRect(ps.hdc,&r,panelBrush());RECT tools={ui(PANEL),std::max(0L,r.bottom-ui(32+lab::ViewportToolbar::Height)),r.right,std::max(0L,r.bottom-ui(32))};FillRect(ps.hdc,&tools,toolBrush());EndPaint(hwnd,&ps);return 0;}
   case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:{HDC dc=reinterpret_cast<HDC>(wp);if(reinterpret_cast<HWND>(lp)==toolbar_.hint()){SetTextColor(dc,RGB(179,192,211));SetBkColor(dc,RGB(29,35,46));return reinterpret_cast<LRESULT>(toolBrush());}SetTextColor(dc,RGB(48,59,76));SetBkColor(dc,RGB(246,248,251));return reinterpret_cast<LRESULT>(panelBrush());}
   case WM_SIZE:layout();return 0;
   case WM_COMMAND:{int id=LOWORD(wp),code=HIWORD(wp);if(code==BN_CLICKED)click(id);else if(id==ALPHA_MODE&&code==CBN_SELCHANGE){updateAlphaSettings();}else if(id==ANIM_COMBO&&code==CBN_SELCHANGE&&animationPageActive()){int sel=int(SendMessageW(controls_[ANIM_COMBO],CB_GETCURSEL,0,0));if(sel>=0){stopAnimation();configureAnimation(size_t(sel),true);}}return 0;}
   case WM_NOTIFY:{auto* h=reinterpret_cast<NMHDR*>(lp);LRESULT toolbarResult=0;if(toolbar_.notify(h,toolbarResult))return toolbarResult;if(h&&h->hwndFrom==tab_&&h->code==TCN_SELCHANGE){activateTab(TabCtrl_GetCurSel(tab_));return 0;}if(h&&h->hwndFrom==controls_[MESH_LIST]&&h->code==LVN_ITEMCHANGED&&!meshListUpdating_){if(animPlaying_){status(L"Редактирование заблокировано во время анимации");return 0;}auto* lv=reinterpret_cast<NMLISTVIEW*>(lp);if(lv->iItem>=0&&size_t(lv->iItem)<render_.PartCount()){const int oldCheck=(lv->uOldState&LVIS_STATEIMAGEMASK)>>12,newCheck=(lv->uNewState&LVIS_STATEIMAGEMASK)>>12;if(oldCheck!=newCheck&&newCheck){const bool visible=newCheck==2;restoreIsolation();render_.SetPartVisible(size_t(lv->iItem),visible);if(!visible){meshListUpdating_=true;ListView_SetItemState(controls_[MESH_LIST],lv->iItem,0,LVIS_SELECTED|LVIS_FOCUSED);meshListUpdating_=false;}refreshBrushAvailability();InvalidateRect(preview_,nullptr,FALSE);}if((lv->uNewState&LVIS_SELECTED)&&!(lv->uOldState&LVIS_SELECTED)&&render_.PartVisible(size_t(lv->iItem))){render_.SelectPart(lv->iItem,false);render_.ClearHover();refreshBrushAvailability();InvalidateRect(preview_,nullptr,FALSE);}}return 0;}break;}
   case WM_HSCROLL:{HWND ctrl=reinterpret_cast<HWND>(lp);if(ctrl){int id=GetDlgCtrlID(ctrl);const bool animControl=id==ANIM_TIMELINE||id==ANIM_SPEED||id==ALPHA_CUTOFF||id==SLIDER_LIGHT_AZ||id==SLIDER_LIGHT_EL;if(animPlaying_&&!animControl){status(L"Редактирование заблокировано во время анимации");return 0;}const int scrollCode=LOWORD(wp);const bool persistBase=scrollCode==TB_ENDTRACK||scrollCode==TB_THUMBPOSITION;if(id==SLIDER_NORMAL||id==SLIDER_GLOSS||id==SLIDER_HEIGHT||id==SLIDER_BLUR||id==SLIDER_SHARP||id==SLIDER_LIGHT_AZ||id==SLIDER_LIGHT_EL||id==ANIM_TIMELINE||id==ANIM_SPEED||id==BRUSH_RADIUS||id==BRUSH_STRENGTH||id==BRUSH_HARDNESS||id==ALPHA_CUTOFF)movedSlider(id,persistBase);}return 0;}
   case WM_TIMER:if(wp==ANIM_TIMER)tickAnimation();else if(wp==VIEW_TIMER)tickView();return 0;
   case WM_ACTIVATEAPP:if(!wp)endInteraction();return 0;
   case WM_DESTROY:KillTimer(window_,VIEW_TIMER);endInteraction();stopAnimation();PostQuitMessage(0);return 0;
  }
  return DefWindowProcW(hwnd,msg,wp,lp);
 }

 bool create(HINSTANCE instance,bool software=false){HDC screen=GetDC(nullptr);if(screen){uiDpi=GetDeviceCaps(screen,LOGPIXELSX);ReleaseDC(nullptr,screen);}INITCOMMONCONTROLSEX ic={sizeof(ic),ICC_BAR_CLASSES|ICC_TAB_CLASSES|ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&ic);WNDCLASSEXW wc={sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=&App::mainProc;wc.lpszClassName=L"KichRayMaterialLab";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=panelBrush();if(!RegisterClassExW(&wc))return false;wc.lpfnWndProc=&App::viewProc;wc.lpszClassName=L"KichRayMaterialView";wc.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));if(!RegisterClassExW(&wc))return false;WNDCLASSEXW pc={sizeof(pc)};pc.hInstance=instance;pc.lpfnWndProc=&materialPageProc;pc.lpszClassName=L"KichRayMaterialPage";pc.hCursor=LoadCursorW(nullptr,IDC_ARROW);pc.hbrBackground=panelBrush();if(!RegisterClassExW(&pc))return false;font_=CreateFontW(ui(-15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");if(!font_)font_=reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));window_=CreateWindowExW(0,L"KichRayMaterialLab",L"KichRay Material Lab 0.9.1",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,ui(1490),ui(860),nullptr,nullptr,instance,this);if(!window_)return false;
  tab_=CreateWindowExW(0,WC_TABCONTROLW,L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|TCS_SINGLELINE|TCS_RAGGEDRIGHT,ui(8),ui(8),ui(PANEL-16),ui(38),window_,reinterpret_cast<HMENU>(TAB_MAIN),instance,nullptr);SendMessageW(tab_,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);SendMessageW(tab_,TCM_SETPADDING,0,MAKELPARAM(ui(5),ui(4)));const wchar_t* tabs[]={L"Импорт",L"Вывод",L"Материал",L"Сцена",L"Кисть"};for(int i=0;i<5;++i){TCITEMW ti={};ti.mask=TCIF_TEXT;ti.pszText=const_cast<LPWSTR>(tabs[i]);TabCtrl_InsertItem(tab_,i,&ti);}
  pageHost_=CreateWindowExW(WS_EX_COMPOSITED|WS_EX_CONTROLPARENT,L"KichRayMaterialPage",L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,ui(PAGE_TOP),ui(PANEL),ui(700),window_,nullptr,instance,nullptr);if(!pageHost_)return false;
  for(int i=0;i<TAB_COUNT;++i){pages_[i]=CreateWindowExW(WS_EX_CONTROLPARENT,L"KichRayMaterialPage",L"",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS|WS_VSCROLL,0,0,ui(PANEL),ui(700),pageHost_,nullptr,instance,&pageScroll_[i]);if(!pages_[i])return false;}
  group(0,L"Модель",54,66);control(0,LOAD_OGF,L"BUTTON",L"Открыть OGF",18,76,PANEL-36,32,BS_PUSHBUTTON|WS_TABSTOP);
  group(0,L"Карты материала",128,198);const int half=(PANEL-48)/2;control(0,LOAD_DIFF,L"BUTTON",L"Diffuse",18,154,half,31,BS_PUSHBUTTON);control(0,LOAD_NORMAL,L"BUTTON",L"Normal",24+half,154,half,31,BS_PUSHBUTTON);control(0,LOAD_GLOSS,L"BUTTON",L"Gloss",18,193,half,31,BS_PUSHBUTTON);control(0,LOAD_HEIGHT,L"BUTTON",L"Height",24+half,193,half,31,BS_PUSHBUTTON);control(0,LOAD_BUMP,L"BUTTON",L"_bump",18,232,half,31,BS_PUSHBUTTON);control(0,LOAD_BUMPX,L"BUTTON",L"_bump#",24+half,232,half,31,BS_PUSHBUTTON);

  group(1,L"Генерация",54,76);control(1,GENERATE,L"BUTTON",L"Создать bump",18,79,PANEL-36,36,BS_PUSHBUTTON|WS_TABSTOP);
  group(1,L"Экспорт",140,132);control(1,EXPORT_NAME,L"EDIT",L"",18,166,PANEL-36,31,WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP);SendMessageW(controls_[EXPORT_NAME],EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Имя экспорта"));control(1,USE_TEX_NAME,L"BUTTON",L"Использовать имя текстуры",18,205,PANEL-36,27,BS_AUTOCHECKBOX);SendMessageW(controls_[USE_TEX_NAME],BM_SETCHECK,BST_CHECKED,0);control(1,EXPORT,L"BUTTON",L"Экспорт",18,238,PANEL-36,31,BS_PUSHBUTTON|WS_TABSTOP);

  group(2,L"Материал",54,344);label(2,L"Сила нормалей",78);slider(2,SLIDER_NORMAL,98,0,200,100);label(2,L"Блеск",136);slider(2,SLIDER_GLOSS,156,0,200,90);label(2,L"Высота",194);slider(2,SLIDER_HEIGHT,214,0,200,100);label(2,L"Blur",252);slider(2,SLIDER_BLUR,272,0,100,12);label(2,L"Sharp",310);slider(2,SLIDER_SHARP,330,0,200,35);control(2,INVERT_Y,L"BUTTON",L"Инверсия Y",18,364,PANEL-36,24,BS_AUTOCHECKBOX);
  group(2,L"Просмотр",408,168);control(2,BUMP_ENABLED,L"BUTTON",L"Показывать bump",18,432,PANEL-36,24,BS_AUTOCHECKBOX);SendMessageW(controls_[BUMP_ENABLED],BM_SETCHECK,BST_CHECKED,0);control(2,COLOR_TINT,L"BUTTON",L"Цвет материала",18,464,PANEL-36,30,BS_PUSHBUTTON);control(2,SELECT_MODE,L"BUTTON",L"Режим выделения",18,502,PANEL-36,24,BS_AUTOCHECKBOX);control(2,CLEAR_SELECTION,L"BUTTON",L"Снять выделение",18,532,PANEL-36,28,BS_PUSHBUTTON);
  group(2,L"Прозрачность diffuse",588,176);
  control(2,ALPHA_ENABLED,L"BUTTON",L"Учитывать альфа-канал",18,612,PANEL-36,24,BS_AUTOCHECKBOX|WS_TABSTOP);
  SendMessageW(controls_[ALPHA_ENABLED],BM_SETCHECK,BST_CHECKED,0);
  control(2,ALPHA_MODE,L"COMBOBOX",L"",18,646,PANEL-36,140,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
  for(const wchar_t* mode:{L"Авто: маска / полупрозрачность",L"Отсечение: листва, сетки",L"Смешивание: стекло"})SendMessageW(controls_[ALPHA_MODE],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(mode));
  SendMessageW(controls_[ALPHA_MODE],CB_SETCURSEL,0,0);label(2,L"Порог отсечения альфы",684);slider(2,ALPHA_CUTOFF,704,0,255,128);


  group(3,L"Освещение",54,238);label(3,L"Азимут",78);slider(3,SLIDER_LIGHT_AZ,98,0,360,52);label(3,L"Высота света",136);slider(3,SLIDER_LIGHT_EL,156,-80,80,37);control(3,COLOR_LIGHT,L"BUTTON",L"Цвет света",18,202,half,30,BS_PUSHBUTTON);control(3,COLOR_AMBIENT,L"BUTTON",L"Окружение",24+half,202,half,30,BS_PUSHBUTTON);control(3,SKELETON_ENABLED,L"BUTTON",L"Скелет",18,244,half,24,BS_AUTOCHECKBOX);SendMessageW(controls_[SKELETON_ENABLED],BM_SETCHECK,BST_UNCHECKED,0);EnableWindow(controls_[SKELETON_ENABLED],FALSE);control(3,RESET,L"BUTTON",L"Сброс вида",24+half,242,half,28,BS_PUSHBUTTON);
  group(3,L"Геометрия",302,326);HWND list=control(3,MESH_LIST,WC_LISTVIEWW,L"",18,328,PANEL-36,246,LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_NOCOLUMNHEADER|WS_BORDER|WS_TABSTOP);ListView_SetExtendedListViewStyle(list,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);LVCOLUMNW col={};col.mask=LVCF_WIDTH;col.cx=ui(PANEL-62);ListView_InsertColumn(list,0,&col);control(3,MESH_SHOW_ALL,L"BUTTON",L"Показать все",18,584,half,28,BS_PUSHBUTTON);control(3,MESH_HIDE_SELECTED,L"BUTTON",L"Скрыть выбранный",24+half,584,half,28,BS_PUSHBUTTON);

  control(BRUSH_TAB,BRUSH_MODE,L"BUTTON",L"Включить кисть",12,56,PANEL-24,30,BS_AUTOCHECKBOX);EnableWindow(controls_[BRUSH_MODE],FALSE);
  group(BRUSH_TAB,L"Операция",94,156);control(BRUSH_TAB,BRUSH_TARGET_RELIEF,L"BUTTON",L"Рельеф",18,118,176,24,BS_AUTORADIOBUTTON);control(BRUSH_TAB,BRUSH_TARGET_SHARP,L"BUTTON",L"Sharp",210,118,176,24,BS_AUTORADIOBUTTON);control(BRUSH_TAB,BRUSH_TARGET_HEIGHT_ADD,L"BUTTON",L"Высота +",18,146,176,24,BS_AUTORADIOBUTTON);control(BRUSH_TAB,BRUSH_TARGET_BLUR,L"BUTTON",L"Blur",210,146,176,24,BS_AUTORADIOBUTTON);control(BRUSH_TAB,BRUSH_TARGET_HEIGHT_SUB,L"BUTTON",L"Впадина",18,174,176,24,BS_AUTORADIOBUTTON);control(BRUSH_TAB,BRUSH_TARGET_GLOSS_ADD,L"BUTTON",L"Блеск +",210,174,176,24,BS_AUTORADIOBUTTON);control(BRUSH_TAB,BRUSH_TARGET_GLOSS_SUB,L"BUTTON",L"Блеск -",210,202,176,24,BS_AUTORADIOBUTTON);SendMessageW(controls_[BRUSH_TARGET_RELIEF],BM_SETCHECK,BST_CHECKED,0);
  group(BRUSH_TAB,L"Параметры кисти",258,226);control(BRUSH_TAB,BRUSH_RADIUS_TEXT,L"STATIC",L"Размер: 3.0%",18,282,176,20,SS_LEFT);slider(BRUSH_TAB,BRUSH_RADIUS,302,1,200,30);control(BRUSH_TAB,BRUSH_STRENGTH_TEXT,L"STATIC",L"Сила: 35.0%",18,340,176,20,SS_LEFT);slider(BRUSH_TAB,BRUSH_STRENGTH,360,0,1000,705);control(BRUSH_TAB,BRUSH_HARDNESS_TEXT,L"STATIC",L"Резкость края: 65%",18,398,220,20,SS_LEFT);slider(BRUSH_TAB,BRUSH_HARDNESS,418,0,100,65);control(BRUSH_TAB,BRUSH_INVERT,L"BUTTON",L"Инвертировать действие",18,456,PANEL-36,24,BS_AUTOCHECKBOX);
  group(BRUSH_TAB,L"Форма / пресет",496,78);control(BRUSH_TAB,PRESET_LOAD,L"BUTTON",L"Загрузить",18,522,half,30,BS_PUSHBUTTON);control(BRUSH_TAB,PRESET_SAVE,L"BUTTON",L"Сохранить",24+half,522,half,30,BS_PUSHBUTTON);
  control(BRUSH_TAB,BRUSH_APPLY,L"BUTTON",L"Применить  Ctrl+S",18,588,half,34,BS_PUSHBUTTON);control(BRUSH_TAB,BRUSH_CLEAR,L"BUTTON",L"Очистить",24+half,588,half,34,BS_PUSHBUTTON);

  control(ANIM_TAB,ANIM_COMBO,L"COMBOBOX",L"",12,62,PANEL-24,240,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);button(ANIM_TAB,ANIM_PLAY,L"Старт",112,36);control(ANIM_TAB,ANIM_LOOP,L"BUTTON",L"Цикл",12,158,PANEL-24,27,BS_AUTOCHECKBOX);label(ANIM_TAB,L"Кадр",199);slider(ANIM_TAB,ANIM_TIMELINE,221,0,1,0);label(ANIM_TAB,L"Скорость",263);slider(ANIM_TAB,ANIM_SPEED,285,25,200,100);
  status_=control(-1,STATUS,L"STATIC",L"",12,650,PANEL-24,24,SS_LEFT|SS_NOPREFIX|SS_ENDELLIPSIS);if(!toolbar_.create(window_,font_,VIEW_TOOLS,sizeof(VIEW_TOOLS)/sizeof(VIEW_TOOLS[0]),uiDpi))return false;preview_=CreateWindowExW(0,L"KichRayMaterialView",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,PANEL,0,980,760,window_,nullptr,instance,this);if(!preview_)return false;std::wstring err;if(!render_.Init(preview_,err,software)){MessageBoxW(window_,err.c_str(),L"D3D11",MB_ICONERROR);return false;}render_.SetNormalSettings(settings_.normalStrength,settings_.invertY);render_.SetGlossStrength(settings_.gloss);loadLight();updateAlphaSettings();ready_=true;setMode(ViewMode::Fly);refreshBrushAvailability();viewLastTick_=GetTickCount64();SetTimer(window_,VIEW_TIMER,16,nullptr);showTab(0);layout();status(L"Material Lab · Откройте OGF или загрузите diffuse для тестового куба");ShowWindow(window_,SW_SHOW);UpdateWindow(window_);return true;}
 int smokeTest(){
  // Real HWNDs, D3D11 WARP and runtime HLSL compilation on the Windows runner.
  std::ofstream log(exeDir()/L"smoke-tests.txt");bool ok=true;
  auto check=[&](bool condition,const char* name){log<<(condition?"PASS: ":"FAIL: ")<<name<<std::endl;ok=ok&&condition;};
  check(viewMode_==ViewMode::Fly,"flight is the default mode");
  auto pagesCorrect=[&](int active){
   bool valid=true;
   for(int t=0;t<TAB_COUNT;++t){valid=valid&&IsWindowVisible(pages_[t])==(t==active);
    for(HWND child:tabItems_[t])valid=valid&&((GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)!=0)==(t==active);
   }return valid;
  };
  bool tabsOK=true,positionsOK=true;
  SetWindowPos(window_,nullptr,0,0,ui(1140),ui(580),SWP_NOMOVE|SWP_NOZORDER);
  for(int repeat=0;repeat<8;++repeat)for(int t=0;t<5;++t){
   showTab(t);tabsOK=tabsOK&&pagesCorrect(t);scrollPage(pages_[t],pageScroll_[t],repeat%2?100000:0);
   for(const auto& item:pageScroll_[t].items){RECT rect{};GetWindowRect(item.window,&rect);MapWindowPoints(nullptr,pages_[t],reinterpret_cast<POINT*>(&rect),2);
    RECT pageRect{};GetClientRect(pages_[t],&pageRect);positionsOK=positionsOK&&rect.top==item.bounds.top-pageScroll_[t].offset&&rect.left==MulDiv(int(item.bounds.left),int(pageRect.right),ui(PANEL));
   }
  }
  check(tabsOK,"40 tab switches expose only the active page and controls");
  check(positionsOK,"scrolling and resizing preserve absolute control positions");
  showTab(0);SetWindowPos(window_,nullptr,0,0,ui(1490),ui(860),SWP_NOMOVE|SWP_NOZORDER);
  const int toolIds[]={TOOL_FLY,TOOL_ORBIT,TOOL_CHOOSE,TOOL_LIGHT,TOOL_MODEL};
  const ViewMode modes[]={ViewMode::Fly,ViewMode::Orbit,ViewMode::Choose,ViewMode::Light,ViewMode::Model};
  for(int i=0;i<5;++i){click(toolIds[i]);check(viewMode_==modes[i],"toolbar switches the active interaction mode");}
  click(TOOL_OUTLINE);check(!outline_,"outline is independent from the active mode");click(TOOL_OUTLINE);
  TabCtrl_SetCurSel(tab_,3);activateTab(3);check(viewMode_==ViewMode::Model,"ordinary tab changes preserve navigation");
  render_.SelectPart(0);click(TOOL_ISOLATE);check(!isolatedVisibility_.empty(),"selection can be isolated");
  click(TOOL_ISOLATE);check(isolatedVisibility_.empty()&&render_.PartVisible(0),"isolation restores previous visibility");
  click(TOOL_FRAME);click(TOOL_WIRE);render_.Draw();click(TOOL_WIRE);click(TOOL_RESET);
  sources_.diffuse=lab::DefaultChecker(64);generateBump(true);click(TOOL_BRUSH);
  check(viewMode_==ViewMode::Brush&&TabCtrl_GetCurSel(tab_)==BRUSH_TAB,"brush opens its settings page after bump generation");
  RECT viewRect{};GetClientRect(preview_,&viewRect);const int x=int(viewRect.right)/2,y=int(viewRect.bottom)/2;
  view(preview_,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
  check(brushDown_&&!currentUndo_.tiles.empty(),"a brush stroke raycasts and captures undo tiles");
  SetCapture(window_);ReleaseCapture();
  check(!brushDown_&&!lDown_&&!rDown_&&!mDown_&&!brushUndo_.empty(),"capture loss completes the brush stroke and releases input state");
  click(TOOL_UNDO);check(brushDirty_&&!brushDirtyKeys_.empty(),"undo remains a saveable edit");
  click(TOOL_APPLY);check(!brushDirty_&&brushDirtyKeys_.empty(),"apply writes the layer snapshot");
  animPlaying_=true;setMode(ViewMode::Fly);refreshBrushAvailability();click(TOOL_MODEL);
  check(viewMode_==ViewMode::Fly,"animation blocks model editing");stopAnimation();refreshBrushAvailability();
  SetWindowPos(window_,nullptr,0,0,ui(1140),ui(580),SWP_NOMOVE|SWP_NOZORDER);
  scrollPage(pages_[BRUSH_TAB],pageScroll_[BRUSH_TAB],ui(100));
  check(pageScroll_[BRUSH_TAB].offset>0,"short windows can scroll the brush controls");
  RECT bar{},lastButton{};GetClientRect(toolbar_.window(),&bar);
  SendMessageW(toolbar_.window(),TB_GETRECT,TOOL_RESET,reinterpret_cast<LPARAM>(&lastButton));
  check(lastButton.right<=bar.right,"all viewport tools fit at the minimum window width");
  showTab(2);check(pagesCorrect(2),"alpha controls do not resurrect other tabs");
  // An invisible front quad must reveal and allow picking an opaque rear quad.
  lab::Mesh alphaMesh;
  for(float z:{-.25f,.25f}){lab::MeshPart part;part.vertices={{{-1,-1,z},{0,0,-1},0,1},{{-1,1,z},{0,0,-1},0,0},{{1,1,z},{0,0,-1},1,0},{{1,-1,z},{0,0,-1},1,1}};part.indices={0,1,2,0,2,3};alphaMesh.parts.push_back(std::move(part));}
  std::wstring alphaError;check(render_.SetMesh(alphaMesh,alphaError),"alpha fixture uploads");
  render_.ResetView();render_.OutlineEnabled(false);render_.SkeletonEnabled(false);
  lab::PartImages rear;rear.diffuse.w=rear.diffuse.h=4;rear.diffuse.pixels.assign(16,{40,180,70,255});render_.SetPartImages(0,rear);
  lab::PartImages front=rear;front.diffuse.pixels.assign(16,{220,30,30,0});front.diffuse.pixels[0].a=255;render_.SetPartImages(1,front);
  lab::Image clear,through,opaque,blended;
  render_.AlphaEnabled(true);render_.SetPartVisible(1,false);render_.CaptureFrame(clear);render_.SetPartVisible(1,true);render_.CaptureFrame(through);
  auto center=[](const lab::Image& im){return im.valid()?im.pixels[size_t(im.h/2)*im.w+im.w/2]:lab::Pixel{};};
  auto difference=[](lab::Pixel a,lab::Pixel b){return std::abs(int(a.r)-int(b.r))+std::abs(int(a.g)-int(b.g))+std::abs(int(a.b)-int(b.b));};
  check(clear.valid()&&through.valid()&&difference(center(clear),center(through))==0,"zero alpha leaves the rendered background unchanged");
  RECT alphaView{};GetClientRect(preview_,&alphaView);lab::Renderer::PickHit hit;
  check(render_.PickUV(int(alphaView.right/2),int(alphaView.bottom/2),hit)&&hit.part==0,"picking passes through invisible diffuse texels");
  render_.AlphaEnabled(false);render_.CaptureFrame(opaque);
  check(difference(center(opaque),center(through))>10,"disabling alpha restores the otherwise invisible front surface");
  front.diffuse.pixels.assign(16,{220,30,30,128});render_.SetPartImages(1,front);render_.AlphaEnabled(true);render_.SetAlphaPreview(lab::AlphaPreview::Blend);render_.CaptureFrame(blended);
  check(difference(center(blended),center(opaque))>5&&difference(center(blended),center(through))>5,"fractional alpha blends the foreground and background");
  render_.Draw();log.close();DestroyWindow(window_);return ok?0:1;
 }
 int run(){
  MSG msg{};int result=0;
  while((result=GetMessageW(&msg,nullptr,0,0))>0){
   if(msg.message==WM_KEYDOWN){
    const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
    wchar_t klass[32]{};GetClassNameW(GetFocus(),klass,32);const bool textEditing=lstrcmpiW(klass,L"EDIT")==0;
    const bool repeat=(msg.lParam&(LPARAM(1)<<30))!=0;
    if(!textEditing&&ctrl&&(msg.wParam=='Z'||msg.wParam=='S')){if(!repeat)click(msg.wParam=='Z'?TOOL_UNDO:TOOL_APPLY);continue;}
    if(GetFocus()==preview_&&!ctrl){
     int action=0;
     switch(msg.wParam){
      case '1':case VK_ESCAPE:action=TOOL_FLY;break;case '2':action=TOOL_ORBIT;break;
      case '3':case 'C':action=TOOL_CHOOSE;break;case '4':action=TOOL_LIGHT;break;
      case '5':case 'B':action=TOOL_BRUSH;break;case '6':case 'R':action=TOOL_MODEL;break;
      case 'O':action=TOOL_OUTLINE;break;case 'F':action=TOOL_FRAME;break;case 'I':action=TOOL_ISOLATE;break;
      case 'V':action=TOOL_WIRE;break;case VK_HOME:action=TOOL_RESET;break;
      case VK_OEM_4:adjustBrushRadius(-2);continue;case VK_OEM_6:adjustBrushRadius(2);continue;
      case 'X':if(!repeat&&brushMode_&&!brushDown_){SendMessageW(controls_[BRUSH_INVERT],BM_SETCHECK,brushInvert_?BST_UNCHECKED:BST_CHECKED,0);click(BRUSH_INVERT);}continue;
      case 'H':if(!repeat&&!editingLocked()){if(GetKeyState(VK_SHIFT)&0x8000)showAllMeshes();else hideSelectedMesh();}continue;
     }
     if(action){if(!repeat)click(action);continue;}
    }
   }
   if(IsDialogMessageW(window_,&msg))continue;
   TranslateMessage(&msg);DispatchMessageW(&msg);
  }
  return result<0?1:int(msg.wParam);
 }

};
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR args,int){HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(hr))return 2;int result=3;{const bool smoke=args&&std::wstring(args)==L"--smoke-test";App app;if(app.create(instance,smoke))result=smoke?app.smokeTest():app.run();}CoUninitialize();return result;}
