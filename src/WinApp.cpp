#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "Renderer.h"
#include "RendererSmoke.h"
#include "MaterialDocument.h"
#include "EditorUI.h"
#include "EditorWorkflow.h"
#include "FlightInput.h"
#include "AttachmentHistory.h"
#include "EngineTexture.h"
#include "Resource.h"
#include "StartupSplash.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);

namespace {
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
constexpr uint32_t PreviewSize=768;
constexpr const wchar_t* AppTitle=L"Kich-Ray Material Lab 0.13.0";

std::string utf8(const std::wstring& text) {
    if(text.empty())return {};
    int size=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    std::string result(size_t(size),'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),result.data(),size,nullptr,nullptr);
    return result;
}
std::wstring wide(const std::string& text,UINT page=CP_UTF8) {
    if(text.empty())return {};
    int size=MultiByteToWideChar(page,0,text.data(),int(text.size()),nullptr,0);
    std::wstring result(size_t(size),L'\0');
    MultiByteToWideChar(page,0,text.data(),int(text.size()),result.data(),size);
    return result;
}
std::wstring lower(std::wstring value) {
    std::transform(value.begin(),value.end(),value.begin(),towlower);return value;
}
std::wstring safeName(std::wstring value) {
    for(auto& c:value)if(c<32||wcschr(L"<>:\"/\\|?*",c))c=L'_';
    while(!value.empty()&&(value.back()==L'.'||value.back()==L' '))value.pop_back();
    if(value.empty()||value==L"."||value==L"..")value=L"material";
    return value;
}
fs::path executableDirectory() {
    std::vector<wchar_t> path(32768);
    DWORD length=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));
    return length&&length<path.size()?fs::path(path.data()).parent_path():fs::current_path();
}
std::vector<uint8_t> readBytes(const fs::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input)return {};
    auto size=input.tellg();
    if(size<=0||size>std::streamoff(512u*1024u*1024u))return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),size);
    return input?bytes:std::vector<uint8_t>{};
}
std::wstring chooseFile(HWND owner,const wchar_t* filter,const wchar_t* extension,bool save=false,const fs::path& initial={}) {
    std::array<wchar_t,32768> path{};
    std::error_code ec;const bool directory=!initial.empty()&&fs::is_directory(initial,ec);
    if(!initial.empty()&&!directory)wcsncpy_s(path.data(),path.size(),initial.c_str(),_TRUNCATE);
    const std::wstring initialDirectory=directory?initial.wstring():initial.parent_path().wstring();
    OPENFILENAMEW options{};options.lStructSize=sizeof(options);options.hwndOwner=owner;
    options.lpstrFilter=filter;options.lpstrFile=path.data();options.nMaxFile=DWORD(path.size());
    options.lpstrInitialDir=initialDirectory.empty()?nullptr:initialDirectory.c_str();
    options.lpstrDefExt=extension;options.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    options.Flags|=save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST;
    bool selected=save?GetSaveFileNameW(&options)!=FALSE:GetOpenFileNameW(&options)!=FALSE;
    return selected?std::wstring(path.data()):std::wstring{};
}
lab::Image loadImage(const fs::path& path,std::string& error) {
    error.clear();
    if(lower(path.extension().wstring())==L".dds")return lab::DecodeDDS(readBytes(path),error);
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf()));
    if(SUCCEEDED(hr))hr=factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,decoder.GetAddressOf());
    if(SUCCEEDED(hr))hr=decoder->GetFrame(0,frame.GetAddressOf());
    UINT w=0,h=0;if(SUCCEEDED(hr))hr=frame->GetSize(&w,&h);
    if(FAILED(hr)||!w||!h||w>4096||h>4096){error="Не удалось прочитать изображение (максимум 4096×4096).";return {};}
    hr=factory->CreateFormatConverter(converter.GetAddressOf());
    if(SUCCEEDED(hr))hr=converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
    lab::Image image;image.w=w;image.h=h;image.pixels.resize(size_t(w)*h);
    if(SUCCEEDED(hr))hr=converter->CopyPixels(nullptr,w*4,UINT(image.pixels.size()*4),reinterpret_cast<BYTE*>(image.pixels.data()));
    if(FAILED(hr)){error="Ошибка преобразования изображения в RGBA.";return {};}
    return image;
}
fs::path stagingPath(const fs::path& path) {
    return path.wstring()+L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());
}
bool saveTextAtomic(const fs::path& path,const std::string& text,std::string& error) {
    error.clear();std::error_code ec;fs::create_directories(path.parent_path(),ec);
    if(ec){error="Не удалось создать папку настроек.";return false;}
    const auto staged=stagingPath(path);
    HANDLE file=CreateFileW(staged.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){error="Не удалось записать историю предметов.";return false;}
    DWORD written=0;const bool success=text.size()<=MAXDWORD&&WriteFile(file,text.data(),DWORD(text.size()),&written,nullptr)&&written==text.size()&&FlushFileBuffers(file);
    CloseHandle(file);
    if(!success||!MoveFileExW(staged.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(staged.c_str());error="Не удалось сохранить историю предметов; прежний файл сохранён.";return false;}
    return true;
}
fs::path attachmentHistoryPath() {
    std::array<wchar_t,32768> local{};const DWORD size=GetEnvironmentVariableW(L"LOCALAPPDATA",local.data(),DWORD(local.size()));
    return size&&size<local.size()?fs::path(local.data())/L"KichRay"/L"MaterialLab"/L"AttachmentHistory.txt":executableDirectory()/L"MaterialLab.attachments";
}
bool saveTransparentPNG(const fs::path& path,const lab::Image& image,std::string& error) {
    error.clear();if(!image.valid()||image.w>1024||image.h>1024){error="Неверный размер снимка (максимум 1024×1024).";return false;}
    const fs::path staged=stagingPath(path);
    HRESULT hr=E_FAIL;
    {
        ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
        hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf()));
        if(SUCCEEDED(hr))hr=factory->CreateStream(stream.GetAddressOf());
        if(SUCCEEDED(hr))hr=stream->InitializeFromFilename(staged.c_str(),GENERIC_WRITE);
        if(SUCCEEDED(hr))hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.GetAddressOf());
        if(SUCCEEDED(hr))hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
        if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(frame.GetAddressOf(),nullptr);
        if(SUCCEEDED(hr))hr=frame->Initialize(nullptr);
        if(SUCCEEDED(hr))hr=frame->SetSize(image.w,image.h);
        WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
        if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&format);
        if(SUCCEEDED(hr)&&!IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA))hr=E_FAIL;
        if(SUCCEEDED(hr)){
            auto pixels=image.pixels;for(auto& pixel:pixels)std::swap(pixel.r,pixel.b);
            hr=frame->WritePixels(image.h,image.w*4,UINT(pixels.size()*sizeof(lab::Pixel)),reinterpret_cast<BYTE*>(pixels.data()));
        }
        if(SUCCEEDED(hr))hr=frame->Commit();
        if(SUCCEEDED(hr))hr=encoder->Commit();
    } // Release WIC's handle before replacing the chosen destination.
    if(FAILED(hr)||!MoveFileExW(staged.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(staged.c_str());error="Не удалось сохранить PNG. Существующий файл не изменён.";return false;}
    return true;
}
fs::path textureRoot(const fs::path& model) {
    for(fs::path p=model.parent_path();!p.empty();) {
        if(lower(p.filename().wstring())==L"meshes")return p.parent_path()/L"textures";
        std::error_code error;if(fs::is_directory(p/L"textures",error))return p/L"textures";
        fs::path next=p.parent_path();if(next==p)break;p=next;
    }
    return {};
}
fs::path relativeAsset(std::string source) {
    std::wstring text=wide(source,CP_ACP);std::replace(text.begin(),text.end(),L'/',L'\\');
    auto normalized=lower(text);
    for(const auto& prefix:{std::wstring(L"$game_textures$\\"),std::wstring(L"textures\\")})
        if(normalized.rfind(prefix,0)==0){text.erase(0,prefix.size());break;}
    fs::path result(text);
    if(result.is_absolute()||result.has_root_name())return {};
    for(const auto& component:result)if(component==L"..")return {};
    return result;
}
std::wstring materialIdentity(const fs::path& file) {
    std::error_code error;auto result=fs::weakly_canonical(file,error);
    return lower((error?file.lexically_normal():result).wstring());
}
void loadExternalMotions(const fs::path& model,lab::Mesh& mesh) {
    if(!mesh.motions.empty())return;
    std::vector<fs::path> references;
    for(auto ref:mesh.motionRefs){
        std::replace(ref.begin(),ref.end(),'/',char(92));std::string folded=ref;for(auto& c:folded)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
        for(const auto& prefix:{std::string("$game_meshes$\\"),std::string("meshes\\")})if(folded.rfind(prefix,0)==0){ref.erase(0,prefix.size());break;}
        auto file=relativeAsset(ref);if(file.empty())continue;if(file.extension().empty())file+=L".omf";references.push_back(file);
    }
    if(references.empty())references.push_back(model.stem().wstring()+L".omf");
    fs::path meshesRoot;
    for(auto p=model.parent_path();!p.empty();){if(lower(p.filename().wstring())==L"meshes"){meshesRoot=p;break;}const auto parent=p.parent_path();if(parent==p)break;p=parent;}
    std::vector<fs::path> bases={model.parent_path()};
    if(!meshesRoot.empty())bases.push_back(meshesRoot);
    bases.push_back(model.parent_path().parent_path());
    std::unordered_set<std::wstring> attempted;
    for(const auto& ref:references) {
        bool loaded=false;
        for(const auto& base:bases) {
          for(const auto& file:{base/ref,base/ref.filename()}) {
            if(!attempted.insert(materialIdentity(file)).second)continue;
            auto bytes=readBytes(file);if(bytes.empty())continue;
            std::string error;if(lab::LoadOGFMotions(bytes,mesh,error)){loaded=true;break;}
          }
          if(loaded)break;
        }
    }
}
COLORREF colorRef(const float* color) {
    return RGB(int(std::clamp(color[0],0.f,1.f)*255+.5f),int(std::clamp(color[1],0.f,1.f)*255+.5f),int(std::clamp(color[2],0.f,1.f)*255+.5f));
}
RECT monitorWorkArea(HWND window) {
    MONITORINFO info{};info.cbSize=sizeof(info);
    if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY),&info))return info.rcWork;
    return RECT{0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
}
struct SmokePromptResponse {HWND owner;UINT answer;bool sent=false;};
BOOL CALLBACK smokePromptWindow(HWND dialog,LPARAM data) {
    auto& response=*reinterpret_cast<SmokePromptResponse*>(data);wchar_t klass[32]{};GetClassNameW(dialog,klass,32);
    if(GetWindow(dialog,GW_OWNER)==response.owner&&std::wstring(klass)==L"#32770"){
        PostMessageW(dialog,WM_COMMAND,response.answer,0);response.sent=true;return FALSE;
    }return TRUE;
}
void CALLBACK smokePromptTimer(HWND owner,UINT,UINT_PTR id,DWORD) {
    SmokePromptResponse response{owner,UINT(id-900)};
    EnumThreadWindows(GetCurrentThreadId(),smokePromptWindow,reinterpret_cast<LPARAM>(&response));
    if(response.sent)KillTimer(owner,id);
}
struct Material {
    std::wstring key;
    std::string label="material";
    lab::Sources sources;
    std::array<fs::path,6> paths;
    lab::BumpPair loadedPair;
    std::unique_ptr<lab::MaterialDocument> document;
    std::vector<uint8_t> originalDDS,originalDDSX;
    fs::path sessionPath,modelTexturePath;
    lab::EncodedBumpPair encoded;
    bool encodedGloss=true,exactReady=false;
    lab::EngineTextureMetadata metadata;
    bool hasBump()const{return document?document->Valid():loadedPair.bump.valid()&&loadedPair.bumpX.valid();}
};
struct MeshRow {std::string label;std::shared_ptr<Material> material;std::string shader;};
enum class Drag {None,Orbit,Pan,Light,Model,Brush};

class App {
    HWND window_=nullptr;
    lab::Renderer renderer_;
    lab::EditorUIState ui_;
    std::vector<MeshRow> meshes_;
    fs::path modelPath_,lastModelDirectory_,lastExportDirectory_,lastAttachmentDirectory_,lastScreenshotDirectory_;
    lab::FlightInput flightInput_;
    lab::AttachmentHistory attachmentHistory_;
    std::unordered_map<size_t,lab::RecentAttachment> activeAttachments_;
    std::vector<lab::RecentAttachment> recentAttachments_;
    bool attachmentHistoryDirty_=false;
    ULONGLONG attachmentHistoryAfter_=0;
    std::vector<size_t> animationOrder_;
    std::unordered_map<std::wstring,std::string> lastAnimations_;
    std::vector<std::shared_ptr<Material>> exactQueue_;
    std::weak_ptr<Material> uiMaterial_;
    ULONGLONG exactAfter_=0;
    bool resolvingAdjustments_=false,focusResolution_=false;
    lab::Image preset_=lab::MakeDefaultNormalPreset();
    Drag drag_=Drag::None;
    int dragButton_=-1;
    bool smoke_=false,running_=true,imguiReady_=false,minimized_=false,viewportFocus_=false;
    bool deviceLost_=false,recoveryAttempted_=false;
    int exitCode_=0;
    float dpiScale_=1.f,animationTime_=0.f;
    POINT lastBrush_{};bool lastBrushValid_=false;
    std::shared_ptr<Material> strokeMaterial_;
    std::vector<bool> isolation_;
    ULONGLONG lastTick_=0,lastBrushUpload_=0;

    void status(std::string message){ui_.status=std::move(message);}
    void fail(const std::string& message){status(message);
        if(smoke_){std::ofstream out(executableDirectory()/L"smoke-tests.txt",std::ios::app);out<<"FAIL "<<message<<'\n';}
        else MessageBoxW(window_,wide(message).c_str(),AppTitle,MB_OK|MB_ICONERROR);
    }
    int selectedPart()const {
        int part=renderer_.SelectedPart();
        if(part>=0&&size_t(part)<meshes_.size())return part;
        for(size_t i=0;i<meshes_.size();++i)if(renderer_.PartVisible(i))return int(i);
        return -1;
    }
    std::shared_ptr<Material> selectedMaterial()const {
        int part=selectedPart();return part>=0?meshes_[size_t(part)].material:nullptr;
    }
    void rememberPath(const wchar_t* name,const fs::path& path) {
        const auto ini=executableDirectory()/L"MaterialLab.ini";
        WritePrivateProfileStringW(L"Paths",name,path.c_str(),ini.c_str());
    }
    bool ensureBase(const std::shared_ptr<Material>& material) {
        if(!material)return false;if(material->hasBump())return true;
        if(!material->sources.diffuse.valid())return false;
        const auto pair=lab::GenerateBumpPreview(material->sources,lab::MinimalBaseSettings(),0);
        auto document=std::make_unique<lab::MaterialDocument>();std::string error;
        if(!document->Load(pair,error,true)){fail(error);return false;}
        material->document=std::move(document);return true;
    }
    void queueExact(const std::shared_ptr<Material>& material) {
        material->encoded={};material->exactReady=false;
        if(material->hasBump()&&std::find(exactQueue_.begin(),exactQueue_.end(),material)==exactQueue_.end())exactQueue_.push_back(material);
        exactAfter_=GetTickCount64()+250;
    }
    bool encodeMaterial(const std::shared_ptr<Material>& material) {
        if(!material->encoded.bump.empty()&&material->encodedGloss==ui_.glossEnabled)return true;
        material->encodedGloss=ui_.glossEnabled;
        if(ui_.glossEnabled&&!material->originalDDS.empty()&&!material->originalDDSX.empty()&&(!material->document||!material->document->HasEdits())) {
            material->encoded={material->originalDDS,material->originalDDSX};return true;
        }
        auto pair=material->document?material->document->Preview(0):material->loadedPair;
        if(!ui_.glossEnabled)for(auto& pixel:pair.bump.pixels)pixel.r=0;
        std::string error;if(!lab::EncodePackedBumpPair(pair,material->encoded,error)){fail(error);return false;}return true;
    }
    bool uploadExact(const std::shared_ptr<Material>& material) {
        if(!material||!material->hasBump()||!encodeMaterial(material))return false;
        std::string error;const auto a=lab::DecodeDDSMipChain(material->encoded.bump,error),b=lab::DecodeDDSMipChain(material->encoded.bumpX,error);
        if(a.empty()||b.empty()){status("Проверочное DDS-превью недоступно: "+error);return false;}
        int first=-1;
        for(size_t i=0;i<meshes_.size();++i)if(meshes_[i].material==material){
            if(first<0){if(!renderer_.SetPartBumpMipImages(i,a,b))return false;first=int(i);}
            else if(!renderer_.CopyPartMaterial(i,size_t(first)))return false;
        }
        material->exactReady=first>=0;return material->exactReady;
    }
    bool resolveAdjustments() {
        if(resolvingAdjustments_)return true;
        auto material=selectedMaterial();if(!material||!material->document||!material->document->HasPendingAdjustments())return true;
        endInteraction();resolvingAdjustments_=true;
        const int answer=MessageBoxW(window_,L"Применить результат ползунков?\n\nДа — закрепить.\nНет — вернуть карты и мазки до правки.\nОтмена — продолжить редактирование.\n\nCtrl+S применяет без этого запроса.",AppTitle,MB_YESNOCANCEL|MB_ICONQUESTION);
        resolvingAdjustments_=false;focusResolution_=false;
        if(answer==IDCANCEL)return false;
        if(answer==IDYES)material->document->CommitAdjustments();else material->document->CancelAdjustments();
        ui_.adjustments={};upload(material);return true;
    }
    void applyAdjustments(bool discard=false) {
        auto material=selectedMaterial();if(!material||!material->document)return;
        endInteraction();if(discard)material->document->CancelAdjustments();else material->document->CommitAdjustments();
        ui_.adjustments={};upload(material);
        status(discard?"Правки ползунков отменены; прежние карты и мазки восстановлены.":"Правки закреплены одним шагом Undo. Экспорт DDS и сохранение сеанса доступны отдельно.");
    }
    void adjustMaterial() {
        auto material=selectedMaterial();if(ui_.editingLocked||!ensureBase(material)||!ensureDocument(material))return;
        endInteraction();
        if(material->document->Adjust(ui_.adjustments,&material->sources.diffuse)){upload(material);status("Предпросмотр правок. Ctrl+S — применить; переход к другому инструменту запросит решение.");}
    }
    bool ensureDocument(const std::shared_ptr<Material>& material) {
        if(!material)return false;if(material->document)return material->document->Valid();
        if(!material->hasBump())return false;
        auto document=std::make_unique<lab::MaterialDocument>();std::string error;
        if(!document->Load(material->loadedPair,error)){fail(error);return false;}
        material->document=std::move(document);material->loadedPair={};return true;
    }
    lab::BumpPair materialPreview(const std::shared_ptr<Material>& material)const {
        if(material->document)return material->document->Preview(PreviewSize);
        return {lab::ResizeImageMax(material->loadedPair.bump,PreviewSize),lab::ResizeImageMax(material->loadedPair.bumpX,PreviewSize)};
    }
    bool upload(const std::shared_ptr<Material>& material,bool diffuse=false) {
        if(!material)return false;
        if(!material->hasBump()&&!diffuse)return true;
        queueExact(material);
        lab::BumpPair pair=materialPreview(material);
        lab::PartImages images;
        if(diffuse){images.diffuse=material->sources.diffuse.valid()?material->sources.diffuse:lab::DefaultChecker(128);images.bump=pair.bump;images.bumpX=pair.bumpX;images.hasBump=material->hasBump();}
        bool success=true;
        for(size_t i=0;i<meshes_.size();++i)if(meshes_[i].material==material)
            success=(diffuse?renderer_.SetPartImages(i,images):renderer_.SetPartBumpImages(i,pair.bump,pair.bumpX,material->hasBump()))&&success;
        if(!success)status("Не удалось обновить материал на видеокарте.");
        return success;
    }
    bool uploadReplacement(const std::shared_ptr<Material>& material,const lab::BumpPair& replacement) {
        // Retain CPU ownership until every mesh sharing this material accepts the pair.
        const auto previous=materialPreview(material);
        for(size_t i=0;i<meshes_.size();++i)if(meshes_[i].material==material)
            if(!renderer_.SetPartBumpImages(i,replacement.bump,replacement.bumpX,true)) {
                bool restored=true;
                for(size_t j=0;j<meshes_.size();++j)if(meshes_[j].material==material)
                    restored=(material->hasBump()?renderer_.SetPartBumpImages(j,previous.bump,previous.bumpX,true):upload(material,true))&&restored;
                fail(restored?"Видеокарта не приняла новую bump-пару. Предыдущий материал сохранён.":"Ошибка Direct3D. Документ сохранён, но сцену нужно открыть заново.");
                return false;
            }
        return true;
    }
    bool saveSession(const std::shared_ptr<Material>& material,bool choosePath=false) {
        endInteraction();if(!ensureDocument(material)){status("Нет материала для сохранения.");return false;}
        fs::path path=material->sessionPath;
        if(path.empty()||choosePath){auto name=chooseFile(window_,L"Material Lab session (*.krlab)\0*.krlab\0",L"krlab",true,safeName(wide(material->label))+L".krlab");if(name.empty())return false;path=name;}
        std::string error;if(!material->document->Save(path,error)){fail(error);return false;}
        material->sessionPath=path;status("Сеанс сохранён: "+utf8(path.filename().wstring()));return true;
    }
    bool confirmDiscard(const std::shared_ptr<Material>& material) {
        if(!material||!material->document||!material->document->IsDirty())return true;
        const std::wstring text=L"В материале «"+wide(material->label)+L"» есть несохранённые изменения. Сохранить сеанс?";
        int answer=MessageBoxW(window_,text.c_str(),AppTitle,MB_YESNOCANCEL|MB_ICONQUESTION);
        return answer==IDNO||(answer==IDYES&&saveSession(material));
    }
    bool confirmAll() {
        endInteraction();if(!resolveAdjustments())return false;std::unordered_set<Material*> seen;
        for(const auto& mesh:meshes_)if(seen.insert(mesh.material.get()).second&&!confirmDiscard(mesh.material))return false;
        return true;
    }
    void synchronizeUI() {
        ui_.meshes.clear();
        for(size_t i=0;i<meshes_.size();++i){auto& mesh=meshes_[i];ui_.meshes.push_back({mesh.label,renderer_.PartVisible(i),renderer_.PartSelected(i),mesh.material->sources.diffuse.valid(),mesh.material->hasBump()});}
        const auto material=selectedMaterial();
        ui_.hasDiffuse=material&&material->sources.diffuse.valid();
        if(uiMaterial_.lock()!=material){ui_.adjustments={};uiMaterial_=material;}
        ui_.canBrush=material&&(material->hasBump()||material->sources.diffuse.valid());ui_.canAdjust=ui_.canBrush;ui_.pendingAdjustments=material&&material->document&&material->document->HasPendingAdjustments();ui_.canUndo=material&&material->document&&material->document->CanUndo();ui_.canRedo=material&&material->document&&material->document->CanRedo();
        ui_.hasEdits=material&&material->document&&material->document->HasEdits();
        ui_.sourceNames={};ui_.documentInfo.clear();
        if(material){for(size_t i=0;i<6;++i)ui_.sourceNames[i]=utf8(material->paths[i].filename().wstring());if(material->document)ui_.documentInfo=std::to_string(material->document->Width())+" × "+std::to_string(material->document->HeightPixels())+" · Undo: "+std::to_string(material->document->UndoCount())+(material->document->IsDirty()?" · не сохранено":"");}
        const int part=selectedPart();
        if(part>=0&&!meshes_[size_t(part)].shader.empty())ui_.documentInfo+=" · shader: "+meshes_[size_t(part)].shader;
        if(material&&material->hasBump()){
            ui_.documentInfo+=material->exactReady?" · DDS + mipmaps":" · быстрое превью; DDS обновляется";
            if(material->metadata.hasMaterial)ui_.documentInfo+=" · THM: "+std::to_string(material->metadata.material);
            if(material->metadata.hasBumpMode&&!material->metadata.bumpEnabled)ui_.documentInfo+="\nВ THM bump выключен. Для результата в игре включите bump в настройках текстуры.";
        }
        ui_.editingLocked=ui_.tab==lab::EditorTab::Animation;ui_.isolated=!isolation_.empty();ui_.hasSkeleton=renderer_.HasSkeleton();
        synchronizeAttachments();
    }
    void applyPreviewSettings() {
        renderer_.BumpEnabled(ui_.bump);renderer_.SetNormalSettings(1.f,false);
        renderer_.GlossEnabled(ui_.glossEnabled);renderer_.SetGlossStrength(.5f);renderer_.SetColor(0,colorRef(ui_.tint));
    }
    void applyAlphaSettings() {
        renderer_.AlphaEnabled(ui_.alpha);renderer_.SetAlphaPreview(lab::AlphaPreview(std::clamp(ui_.alphaMode,0,2)));renderer_.SetAlphaCutoff(ui_.alphaCutoff);
    }
    void applyLightSettings(bool save=true) {
        renderer_.SetLightIntensity(ui_.lightIntensity);renderer_.SetMaterialClass(ui_.materialClass-1);renderer_.SetLightOrbitDegrees(ui_.lightAzimuth,ui_.lightElevation);renderer_.SetColor(1,colorRef(ui_.lightColor));renderer_.SetColor(2,colorRef(ui_.ambient));
        renderer_.SetEnvironmentLighting({ui_.environment[0],ui_.environment[1],ui_.environment[2]},ui_.hemisphere,ui_.exposure,ui_.glossFactor);
        if(save){const auto ini=executableDirectory()/L"MaterialLab.ini";auto az=std::to_wstring(int(ui_.lightAzimuth)),el=std::to_wstring(int(ui_.lightElevation));WritePrivateProfileStringW(L"Scene",L"LightAzimuth",az.c_str(),ini.c_str());WritePrivateProfileStringW(L"Scene",L"LightElevation",el.c_str(),ini.c_str());
            const auto intensity=std::to_wstring(ui_.lightIntensity);WritePrivateProfileStringW(L"Scene",L"LightIntensity",intensity.c_str(),ini.c_str());
            const auto cls=std::to_wstring(ui_.materialClass);WritePrivateProfileStringW(L"Scene",L"MaterialClass",cls.c_str(),ini.c_str());
            const auto color=std::to_wstring(colorRef(ui_.lightColor)),ambient=std::to_wstring(colorRef(ui_.ambient));
            WritePrivateProfileStringW(L"Scene",L"LightColor",color.c_str(),ini.c_str());WritePrivateProfileStringW(L"Scene",L"AmbientColor",ambient.c_str(),ini.c_str());
            const auto environment=std::to_wstring(colorRef(ui_.environment)),hemisphere=std::to_wstring(ui_.hemisphere),exposure=std::to_wstring(ui_.exposure),glossFactor=std::to_wstring(ui_.glossFactor);
            WritePrivateProfileStringW(L"Scene",L"EnvironmentColor",environment.c_str(),ini.c_str());WritePrivateProfileStringW(L"Scene",L"Hemisphere",hemisphere.c_str(),ini.c_str());
            WritePrivateProfileStringW(L"Scene",L"Exposure",exposure.c_str(),ini.c_str());WritePrivateProfileStringW(L"Scene",L"GlossFactor",glossFactor.c_str(),ini.c_str());}
    }
    void setMode(lab::EditorMode mode) {
        if((mode!=ui_.mode||(mode==lab::EditorMode::Brush&&ui_.tab!=lab::EditorTab::Brush))&&!resolveAdjustments())return;
        if(ui_.editingLocked&&(mode==lab::EditorMode::Brush||mode==lab::EditorMode::Choose||mode==lab::EditorMode::Model))return;
        if(mode==lab::EditorMode::Model&&ui_.tab==lab::EditorTab::Animation)return;
        const bool createBase=mode==lab::EditorMode::Brush&&selectedMaterial()&&!selectedMaterial()->hasBump();
        if(mode==lab::EditorMode::Brush&&!ensureBase(selectedMaterial())){status("Сначала загрузите или создайте bump-пару.");return;}
        if(ui_.mode!=mode)flightInput_.Suspend();
        endInteraction();ui_.mode=mode;renderer_.FlyCameraEnabled(mode==lab::EditorMode::Fly);renderer_.ClearHover();renderer_.ClearBrushCursor();
        if(mode==lab::EditorMode::Brush){if(createBase)upload(selectedMaterial());stopAnimation(true);ui_.tab=lab::EditorTab::Brush;ui_.forceTab=true;}
    }
    void restoreIsolation() {
        for(size_t i=0;i<isolation_.size()&&i<meshes_.size();++i)renderer_.SetPartVisible(i,isolation_[i]);isolation_.clear();
    }
    void toggleIsolation() {
        endInteraction();if(!isolation_.empty()){restoreIsolation();return;}if(!renderer_.HasSelection())return;
        for(size_t i=0;i<meshes_.size();++i){isolation_.push_back(renderer_.PartVisible(i));renderer_.SetPartVisible(i,renderer_.PartSelected(i));}
    }
    void stopAnimation(bool bindPose) {
        ui_.animationPlaying=false;ui_.editingLocked=ui_.tab==lab::EditorTab::Animation;
        if(bindPose) {
            animationTime_=0;ui_.animationFrame=0;std::wstring error;
            if(!renderer_.SetBindPose(&error)){status(utf8(error));ui_.editingLocked=true;deviceLost_=true;exitCode_=1;}
        }
    }
    size_t motionIndex()const {return ui_.animationIndex>=0&&size_t(ui_.animationIndex)<animationOrder_.size()?animationOrder_[size_t(ui_.animationIndex)]:size_t(-1);}
    std::wstring animationKey()const {
        uint64_t key=14695981039346656037ull;
        for(wchar_t c:materialIdentity(modelPath_)){key^=uint32_t(c);key*=1099511628211ull;}
        return std::to_wstring(key);
    }
    void rememberAnimation(){
        if(modelPath_.empty())return;const auto* motion=renderer_.MotionAt(motionIndex());if(!motion)return;
        lastAnimations_[materialIdentity(modelPath_)]=motion->name;
        const auto name=wide(motion->name,CP_ACP);const auto ini=executableDirectory()/L"MaterialLab.ini";
        WritePrivateProfileStringW(L"Animations",animationKey().c_str(),name.c_str(),ini.c_str());
    }
    void refreshAnimations() {
        stopAnimation(true);ui_.animations.clear();ui_.animationIndex=0;
        std::vector<std::string> names;for(size_t i=0;i<renderer_.MotionCount();++i)names.push_back(utf8(wide(renderer_.MotionAt(i)->name,CP_ACP)));
        animationOrder_=lab::SortedAnimationIndices(names);
        const auto identity=materialIdentity(modelPath_);
        if(!modelPath_.empty()&&!lastAnimations_.count(identity)){
            std::array<wchar_t,4096> saved{};const auto ini=executableDirectory()/L"MaterialLab.ini";
            GetPrivateProfileStringW(L"Animations",animationKey().c_str(),L"",saved.data(),DWORD(saved.size()),ini.c_str());
            for(size_t i=0;i<names.size();++i)if(names[i]==utf8(saved.data()))lastAnimations_[identity]=renderer_.MotionAt(i)->name;
        }
        const auto previous=lastAnimations_.find(identity);
        for(size_t i=0;i<animationOrder_.size();++i){ui_.animations.push_back(names[animationOrder_[i]]);if(previous!=lastAnimations_.end()&&renderer_.MotionAt(animationOrder_[i])->name==previous->second)ui_.animationIndex=int(i);}
        const auto* motion=renderer_.MotionAt(motionIndex());ui_.animationFrames=motion?int(motion->frames):1;
    }
    void setAnimationPose() {
        const auto* motion=renderer_.MotionAt(motionIndex());if(!motion)return;
        std::wstring error;
        if(!renderer_.SetAnimationPose(motionIndex(),animationTime_,ui_.animationLoop,error)){stopAnimation(false);status(utf8(error));}
        ui_.animationFrames=int(motion->frames);ui_.animationFrame=std::clamp(int(animationTime_*motion->fps),0,std::max(0,ui_.animationFrames-1));
    }
    std::string attachmentBoneName(size_t index)const {
        const auto* bone=renderer_.BoneAt(index);return bone?utf8(wide(bone->name,CP_ACP)):std::string{};
    }
    void loadAttachmentHistory() {
        const auto path=attachmentHistoryPath();std::error_code ec;
        if(!fs::exists(path,ec))return;
        const auto size=fs::file_size(path,ec);
        if(ec||size>lab::AttachmentHistory::MaxBytes){status("История предметов не прочитана: неверный размер файла.");return;}
        const auto bytes=readBytes(path);std::string error;
        if(!attachmentHistory_.Deserialize(std::string(bytes.begin(),bytes.end()),error))status(error);
    }
    bool saveAttachmentHistory() {
        if(!attachmentHistoryDirty_)return true;
        std::string error;if(!saveTextAtomic(attachmentHistoryPath(),attachmentHistory_.Serialize(),error)){
            status(error);attachmentHistoryAfter_=GetTickCount64()+5000;return false;}
        attachmentHistoryDirty_=false;return true;
    }
    void rememberAttachment(const lab::RecentAttachment& item) {
        if(attachmentHistory_.Remember(item)){attachmentHistoryDirty_=true;attachmentHistoryAfter_=GetTickCount64()+400;}
    }
    void unloadAttachments(bool newModel=false) {
        saveAttachmentHistory();renderer_.ClearAttachments();activeAttachments_.clear();
        ui_.attachmentActive=false;ui_.attachmentName.clear();ui_.requestAttachmentPopup=false;ui_.boneMarkers.clear();
        if(newModel){ui_.attachmentBone=-1;ui_.attachmentBoneName.clear();ui_.attachmentBones.clear();}
    }
    void synchronizeAttachments() {
        ui_.attachmentBones.clear();ui_.boneMarkers.clear();
        bool selectedExists=false;int first=-1,rightHand=-1;
        for(size_t i=0;i<renderer_.BoneCount();++i){const auto name=attachmentBoneName(i);if(!lab::IsBipedBone(name))continue;
            ui_.attachmentBones.push_back({int(i),name});if(first<0)first=int(i);
            if(name.find("r_hand")!=std::string::npos)rightHand=int(i);
            if(int(i)==ui_.attachmentBone)selectedExists=true;
        }
        if(!selectedExists)ui_.attachmentBone=rightHand>=0?rightHand:first;
        ui_.attachmentBoneName=ui_.attachmentBone>=0?attachmentBoneName(size_t(ui_.attachmentBone)):std::string{};
        recentAttachments_=attachmentHistory_.ForBone(ui_.attachmentBoneName);ui_.recentAttachments.clear();
        for(const auto& item:recentAttachments_)ui_.recentAttachments.push_back(item.path);
        const auto active=activeAttachments_.find(size_t(ui_.attachmentBone));
        ui_.attachmentActive=ui_.attachmentBone>=0&&active!=activeAttachments_.end()&&renderer_.HasAttachment(size_t(ui_.attachmentBone));
        ui_.attachmentName=ui_.attachmentActive?utf8(fs::path(wide(active->second.path)).filename().wstring()):std::string{};
        if(ui_.attachmentActive)ui_.attachmentTransform=active->second.transform;
        else ui_.attachmentTransform={};
        if(ui_.tab==lab::EditorTab::Animation&&ui_.showBipedBones&&renderer_.SceneWidth()&&renderer_.SceneHeight())
            for(const auto& point:renderer_.BonePoints())if(point.visible&&lab::IsBipedBone(point.name))
                ui_.boneMarkers.push_back({int(point.index),attachmentBoneName(point.index),point.x/renderer_.SceneWidth(),point.y/renderer_.SceneHeight(),renderer_.HasAttachment(point.index)});
    }
    void selectAttachmentBone(int index) {
        if(ui_.tab!=lab::EditorTab::Animation||index<0||size_t(index)>=renderer_.BoneCount()||!lab::IsBipedBone(attachmentBoneName(size_t(index))))return;
        endInteraction();ui_.attachmentBone=index;synchronizeAttachments();ui_.requestAttachmentPopup=true;
    }
    bool loadAttachment(const fs::path& file,const lab::AttachmentTransform& transform) {
        if(ui_.tab!=lab::EditorTab::Animation||ui_.attachmentBone<0||!renderer_.BoneAt(size_t(ui_.attachmentBone)))return false;
        endInteraction();lab::Mesh mesh;std::string error;
        if(!lab::LoadOGFMesh(readBytes(file),mesh,error)){fail(error);return false;}
        const auto root=textureRoot(file);std::vector<lab::PartImages> images;std::vector<float> classes;size_t missing=0;
        for(const auto& part:mesh.parts)if(part.valid()){
            lab::PartImages image;lab::EngineTextureMetadata metadata;
            auto relative=relativeAsset(part.texture);if(!relative.empty()&&relative.extension().empty())relative+=L".dds";
            const auto diffuse=relative.empty()?fs::path{}:(root.empty()?file.parent_path():root)/relative;
            if(!diffuse.empty()){
                image.diffuse=loadImage(diffuse,error);auto thm=diffuse;thm.replace_extension(L".thm");
                lab::ReadEngineTextureMetadata(readBytes(thm),metadata);
                auto base=diffuse;base.replace_extension();fs::path bump=base.wstring()+L"_bump.dds";
                const auto named=relativeAsset(metadata.bumpName);
                if(metadata.bumpEnabled&&!named.empty()){bump=(root.empty()?file.parent_path():root)/named;if(bump.extension().empty())bump+=L".dds";}
                const auto bumpX=bump.parent_path()/(bump.stem().wstring()+L"#"+bump.extension().wstring());
                image.bump=lab::DecodeDDS(readBytes(bump),error);image.bumpX=lab::DecodeDDS(readBytes(bumpX),error);
                image.hasBump=image.bump.valid()&&image.bumpX.valid()&&image.bump.w==image.bumpX.w&&image.bump.h==image.bumpX.h;
            }
            if(!image.diffuse.valid()){image.diffuse=lab::DefaultChecker(128);++missing;}
            if(!image.hasBump){image.bump={};image.bumpX={};}
            images.push_back(std::move(image));classes.push_back(metadata.material);
        }
        std::wstring renderError;const size_t bone=size_t(ui_.attachmentBone);
        if(!renderer_.SetAttachment(bone,mesh,images,transform,renderError)){fail(utf8(renderError));return false;}
        for(size_t i=0;i<classes.size();++i)renderer_.SetAttachmentMaterialClass(bone,i,classes[i]);
        std::error_code ec;auto full=fs::absolute(file,ec);if(ec)full=file;
        lab::RecentAttachment item{attachmentBoneName(bone),utf8(full.wstring()),transform};
        activeAttachments_[bone]=item;rememberAttachment(item);const bool historySaved=saveAttachmentHistory();
        lastAttachmentDirectory_=file.parent_path();rememberPath(L"AttachmentDirectory",lastAttachmentDirectory_);synchronizeAttachments();
        status("Предмет загружен на "+item.bone+(missing?". Не найден diffuse: "+std::to_string(missing):".")+
            (historySaved?" Посадка сохранена в истории.":" Историю пока не удалось сохранить на диск; повторю запись."));return true;
    }
    void chooseAttachment() {
        endInteraction();const auto file=chooseFile(window_,L"X-Ray предмет (*.ogf)\0*.ogf\0",L"ogf",false,lastAttachmentDirectory_.empty()?lastModelDirectory_:lastAttachmentDirectory_);
        if(file.empty())return;
        lab::AttachmentTransform transform;
        const auto identity=materialIdentity(file);
        for(const auto& recent:attachmentHistory_.ForBone(ui_.attachmentBoneName))if(materialIdentity(fs::path(wide(recent.path)))==identity){transform=recent.transform;break;}
        loadAttachment(file,transform);
    }
    void changeAttachmentTransform(bool reset=false) {
        if(ui_.tab!=lab::EditorTab::Animation||ui_.attachmentBone<0)return;
        auto active=activeAttachments_.find(size_t(ui_.attachmentBone));if(active==activeAttachments_.end())return;
        const auto transform=reset?lab::AttachmentTransform{}:ui_.attachmentTransform;std::wstring error;
        if(!renderer_.SetAttachmentTransform(active->first,transform,error)){status(utf8(error));ui_.attachmentTransform=active->second.transform;return;}
        active->second.transform=transform;ui_.attachmentTransform=transform;rememberAttachment(active->second);
    }
    void captureAnimation() {
        if(ui_.tab!=lab::EditorTab::Animation)return;
        endInteraction();flightInput_.Suspend();
        const auto name=safeName(modelPath_.empty()?L"model":modelPath_.stem().wstring())+L"_frame_"+std::to_wstring(ui_.animationFrame)+L".png";
        const auto directory=lastScreenshotDirectory_.empty()?modelPath_.parent_path():lastScreenshotDirectory_;
        const auto path=chooseFile(window_,L"PNG с прозрачностью (*.png)\0*.png\0",L"png",true,directory/name);if(path.empty())return;
        std::unordered_set<Material*> seen;
        for(const auto& row:meshes_)if(seen.insert(row.material.get()).second&&row.material->hasBump()&&!row.material->exactReady)
            if(!uploadExact(row.material)){fail("Не удалось подготовить DDS для снимка.");return;}
        lab::Image capture;std::wstring error;
        if(!renderer_.CaptureTransparent(capture,1024,error)){fail(utf8(error));return;}
        std::string saveError;if(!saveTransparentPNG(path,capture,saveError)){fail(saveError);return;}
        lastScreenshotDirectory_=fs::path(path).parent_path();rememberPath(L"ScreenshotDirectory",lastScreenshotDirectory_);
        status("PNG сохранён: "+utf8(fs::path(path).filename().wstring())+" · "+std::to_string(capture.w)+"×"+std::to_string(capture.h)+" · прозрачный фон.");
    }
    bool newCube(bool confirm=true) {
        if(confirm&&!confirmAll())return false;
        rememberAnimation();exactQueue_.clear();ui_.adjustments={};
        const bool firstModel=meshes_.empty();lab::Mesh mesh=lab::MakeCube();std::wstring error;
        if(!renderer_.SetMesh(mesh,error)){fail(utf8(error));return false;}
        unloadAttachments(true);
        auto material=std::make_shared<Material>();material->key=L"manual";
        meshes_={{"Куб · material",material}};modelPath_.clear();isolation_.clear();
        ui_.modelName="Тестовый куб";upload(material,true);renderer_.SelectPart(0);
        refreshAnimations();setMode(ui_.mode==lab::EditorMode::Orbit?lab::EditorMode::Orbit:lab::EditorMode::Fly);if(firstModel)renderer_.ResetView();
        status("Тестовый куб. Загрузите diffuse или готовую bump-пару.");return true;
    }
    void openModel() {
        endInteraction();const auto file=chooseFile(window_,L"X-Ray mesh (*.ogf)\0*.ogf\0",L"ogf",false,lastModelDirectory_);if(file.empty())return;
        lab::Mesh mesh;std::string error;
        if(!lab::LoadOGFMesh(readBytes(file),mesh,error)){fail(error);return;}
        loadExternalMotions(file,mesh);
        std::vector<MeshRow> rows;std::unordered_map<std::wstring,std::shared_ptr<Material>> materials;
        const fs::path root=textureRoot(file);size_t missing=0;
        for(const auto& part:mesh.parts)if(part.valid()) {
            fs::path relative=relativeAsset(part.texture);if(relative.extension().empty()&&!relative.empty())relative+=L".dds";
            fs::path diffuse=relative.empty()?fs::path{}:(root.empty()?fs::path(file).parent_path():root)/relative;
            const std::wstring key=diffuse.empty()?L"missing:"+std::to_wstring(rows.size()):materialIdentity(diffuse);
            auto found=materials.find(key);std::shared_ptr<Material> material;
            if(found!=materials.end())material=found->second;
            else {
                material=std::make_shared<Material>();material->key=key;material->label=relative.empty()?"material_"+std::to_string(rows.size()+1):utf8(relative.stem().wstring());
                if(!diffuse.empty()) {
                    material->sources.diffuse=loadImage(diffuse,error);material->paths[0]=diffuse;material->modelTexturePath=diffuse;
                    if(!material->sources.diffuse.valid())++missing;
                    fs::path base=diffuse;base.replace_extension();
                    fs::path thm=diffuse;thm.replace_extension(L".thm");
                    lab::ReadEngineTextureMetadata(readBytes(thm),material->metadata);
                    fs::path bump=base.wstring()+L"_bump.dds";
                    const auto named=relativeAsset(material->metadata.bumpName);
                    if(material->metadata.bumpEnabled&&!named.empty()){bump=(root.empty()?fs::path(file).parent_path():root)/named;if(bump.extension().empty())bump+=L".dds";}
                    fs::path bumpX=bump.parent_path()/(bump.stem().wstring()+L"#"+bump.extension().wstring());
                    std::error_code ec;
                    if(fs::exists(bump,ec)&&fs::exists(bumpX,ec)) {
                        auto a=readBytes(bump),b=readBytes(bumpX);auto image=lab::DecodeDDS(a,error),imageX=lab::DecodeDDS(b,error);
                        if(image.valid()&&imageX.valid()&&image.w==imageX.w&&image.h==imageX.h&&image.w<=4096&&image.h<=4096){material->loadedPair={std::move(image),std::move(imageX)};material->originalDDS=std::move(a);material->originalDDSX=std::move(b);material->paths[4]=bump;material->paths[5]=bumpX;}
                    }
                }
                materials[key]=material;
            }
            rows.push_back({std::to_string(rows.size()+1)+" · "+material->label,material,utf8(wide(part.shader,CP_ACP))});
        }
        if(!confirmAll())return;
        rememberAnimation();
        std::wstring renderError;if(!renderer_.SetMesh(mesh,renderError)){fail(utf8(renderError));return;}
        unloadAttachments(true);
        exactQueue_.clear();ui_.adjustments={};meshes_=std::move(rows);modelPath_=file;isolation_.clear();
        lastModelDirectory_=modelPath_.parent_path();rememberPath(L"OGFDirectory",lastModelDirectory_);
        bool uploaded=true;for(const auto& entry:materials)uploaded=upload(entry.second,true)&&uploaded;
        for(size_t i=0;i<meshes_.size();++i)renderer_.SetPartMaterialClass(i,meshes_[i].material->metadata.material);
        if(!meshes_.empty())renderer_.SelectPart(0);
        ui_.modelName=utf8(modelPath_.filename().wstring());refreshAnimations();setMode(ui_.mode==lab::EditorMode::Orbit?lab::EditorMode::Orbit:lab::EditorMode::Fly);
        status(uploaded?"Модель загружена: "+std::to_string(meshes_.size())+" мешей"+(missing?". Не найден diffuse: "+std::to_string(missing):"."):"Модель открыта, но видеокарта не приняла часть текстур.");
    }
    void loadBump(bool correction) {
        endInteraction();auto material=selectedMaterial();if(!material)return;
        const auto file=chooseFile(window_,L"Текстуры (*.dds;*.png;*.bmp)\0*.dds;*.png;*.bmp\0Все файлы\0*.*\0",L"dds");if(file.empty())return;
        fs::path primary=file,partner;std::wstring stem=primary.stem().wstring();
        if(correction&&stem.size()>=6&&lower(stem.substr(stem.size()-6))==L"_bump#")partner=primary.parent_path()/(stem.substr(0,stem.size()-1)+primary.extension().wstring());
        if(!correction&&stem.size()>=5&&lower(stem.substr(stem.size()-5))==L"_bump")partner=primary.parent_path()/(stem+L"#"+primary.extension().wstring());
        std::error_code ec;if(partner.empty()||!fs::exists(partner,ec)) {
            auto second=chooseFile(window_,correction?L"Основной bump\0*.dds;*.png;*.bmp\0":L"Дополнительный bump#\0*.dds;*.png;*.bmp\0",L"dds");
            if(second.empty())return;partner=second;
        }
        const fs::path bump=correction?partner:primary,bumpX=correction?primary:partner;
        std::string error;lab::BumpPair pair{loadImage(bump,error),{}};
        if(!pair.bump.valid()){fail(error);return;}pair.bumpX=loadImage(bumpX,error);
        if(!pair.bumpX.valid()){fail(error);return;}
        if(pair.bump.w!=pair.bumpX.w||pair.bump.h!=pair.bumpX.h||pair.bump.w>4096||pair.bump.h>4096){fail("Обе части bump-пары должны иметь одинаковый размер до 4096×4096.");return;}
        if(!confirmDiscard(material))return;
        lab::BumpPair preview{lab::ResizeImageMax(pair.bump,PreviewSize),lab::ResizeImageMax(pair.bumpX,PreviewSize)};
        if(!uploadReplacement(material,preview))return;
        material->encoded={};material->document.reset();material->loadedPair=std::move(pair);material->sessionPath.clear();
        material->paths[4]=bump;material->paths[5]=bumpX;
        material->originalDDS=lower(bump.extension().wstring())==L".dds"?readBytes(bump):std::vector<uint8_t>{};
        material->originalDDSX=lower(bumpX.extension().wstring())==L".dds"?readBytes(bumpX):std::vector<uint8_t>{};
        if(material->paths[0].empty()){auto name=bump.stem().wstring();if(name.size()>5&&lower(name.substr(name.size()-5))==L"_bump")name.resize(name.size()-5);material->label=utf8(name);}
        queueExact(material);status("Bump-пара загружена без пересчёта нормалей.");
    }
    void loadSource(lab::EditorAction action) {
        if(action==lab::EditorAction::LoadBump||action==lab::EditorAction::LoadBumpX){loadBump(action==lab::EditorAction::LoadBumpX);return;}
        endInteraction();auto material=selectedMaterial();if(!material)return;
        const auto file=chooseFile(window_,L"Изображения\0*.dds;*.png;*.jpg;*.jpeg;*.bmp;*.tif\0Все файлы\0*.*\0",L"dds");if(file.empty())return;
        std::string error;auto image=loadImage(file,error);if(!image.valid()){fail(error);return;}
        if(image.w>4096||image.h>4096){fail("Максимальный размер источника — 4096×4096.");return;}
        const int index=int(action)-int(lab::EditorAction::LoadDiffuse);
        if(index<0||index>3)return;
        lab::Image* sources[]={&material->sources.diffuse,&material->sources.normal,&material->sources.gloss,&material->sources.height};
        auto previous=std::move(*sources[index]);*sources[index]=std::move(image);
        if(index==0&&!upload(material,true)){*sources[index]=std::move(previous);upload(material,true);return;}
        material->paths[size_t(index)]=file;
        if(index==0)material->label=utf8(fs::path(file).stem().wstring());
        if(!material->hasBump()&&material->sources.diffuse.valid()){
            if(ensureBase(material)){upload(material);status("Создана минимальная базовая нормаль. Ползунки регулируют эту карту.");}
        }else status("Источник загружен. «Создать bump» строит новую мягкую базу с выбранными входными картами.");
    }
    void generateSelected() {
        endInteraction();std::vector<std::shared_ptr<Material>> targets;std::unordered_set<Material*> seen;
        for(size_t i=0;i<meshes_.size()&&i<ui_.generateTargets.size();++i)if(ui_.generateTargets[i]&&renderer_.PartVisible(i)&&meshes_[i].material->sources.diffuse.valid()&&seen.insert(meshes_[i].material.get()).second)targets.push_back(meshes_[i].material);
        if(targets.empty()){status("Не выбран материал с diffuse.");return;}
        for(const auto& material:targets)if(!confirmDiscard(material))return;
        // Build all replacements before committing any material.
        std::vector<std::unique_ptr<lab::MaterialDocument>> documents;
        for(const auto& material:targets) {
            auto pair=lab::GenerateBumpPreview(material->sources,lab::MinimalBaseSettings(),0);
            auto document=std::make_unique<lab::MaterialDocument>();std::string error;
            if(!document->Load(pair,error,true)){fail(error);return;}documents.push_back(std::move(document));
        }
        for(size_t i=0;i<targets.size();++i)if(!uploadReplacement(targets[i],documents[i]->Preview(PreviewSize))) {
            for(size_t j=0;j<i;++j)upload(targets[j]);return;
        }
        for(size_t i=0;i<targets.size();++i){auto& material=targets[i];material->document=std::move(documents[i]);material->loadedPair={};material->originalDDS.clear();material->originalDDSX.clear();material->sessionPath.clear();material->paths[4].clear();material->paths[5].clear();queueExact(material);}
        ui_.adjustments={};status("Минимальная база создана. Ползунки редактируют текущие карты; применение можно отменить через Undo.");
    }
    void openSession() {
        endInteraction();auto material=selectedMaterial();if(!material)return;
        auto file=chooseFile(window_,L"Material Lab session (*.krlab)\0*.krlab\0",L"krlab");if(file.empty())return;
        auto document=std::make_unique<lab::MaterialDocument>();std::string error;
        if(!document->Open(file,error)){fail(error);return;}if(!confirmDiscard(material))return;
        if(!uploadReplacement(material,document->Preview(PreviewSize)))return;
        material->document=std::move(document);material->loadedPair={};material->originalDDS.clear();material->originalDDSX.clear();material->sessionPath=file;material->paths[4].clear();material->paths[5].clear();
        ui_.adjustments={};queueExact(material);status("Сеанс открыт; исходные каналы и слои восстановлены.");
    }
    void exportPair() {
        endInteraction();auto material=selectedMaterial();if(!material||!material->hasBump()){status("Сначала загрузите или создайте bump-пару.");return;}
        std::wstring name=ui_.useTextureName?(material->modelTexturePath.empty()?wide(material->label):material->modelTexturePath.stem().wstring()):wide(ui_.exportName.data());
        fs::path directory=lab::PreferredExportDirectory(material->modelTexturePath,lastExportDirectory_,modelPath_);
        std::error_code ec;if(!directory.empty())fs::create_directories(directory,ec);
        if(ec){status("Папка из модели недоступна; использован запасной путь.");directory=lab::PreferredExportDirectory({},lastExportDirectory_,modelPath_);}
        auto file=chooseFile(window_,L"X-Ray bump DDS pair\0*.dds\0",L"dds",true,directory/(safeName(name)+L"_bump.dds"));if(file.empty())return;
        fs::path base=file;base.replace_extension();auto stem=base.filename().wstring(),normalized=lower(stem);
        if(normalized.size()>=6&&normalized.substr(normalized.size()-6)==L"_bump#")stem.resize(stem.size()-6);
        else if(normalized.size()>=5&&normalized.substr(normalized.size()-5)==L"_bump")stem.resize(stem.size()-5);
        base=base.parent_path()/safeName(stem);std::string error;bool success=false;
        if(encodeMaterial(material))success=lab::WriteBumpPairDDSBytesAtomic(base.wstring(),material->encoded.bump,material->encoded.bumpX,error);
        if(!success){fail(error.empty()?"Не удалось экспортировать материал.":error);return;}
        lastExportDirectory_=base.parent_path();rememberPath(L"ExportDirectory",lastExportDirectory_);
        status("Экспортированы "+utf8(base.filename().wstring())+"_bump.dds и _bump#.dds. Сеанс сохраняется отдельно.");
    }
    void loadPreset() {
        endInteraction();auto file=chooseFile(window_,L"Кисти и изображения\0*.krnp;*.dds;*.png;*.bmp;*.tif\0Все файлы\0*.*\0",L"krnp");if(file.empty())return;
        std::string error;lab::Image preset;
        if(lower(fs::path(file).extension().wstring())==L".krnp"){if(!lab::LoadNormalPreset(file,preset,error)){fail(error);return;}}
        else preset=loadImage(file,error);
        if(!preset.valid()){fail(error);return;}preset_=std::move(preset);ui_.brushTool=lab::BrushTool::Freehand;ui_.forceBrushTab=true;ui_.presetName=utf8(fs::path(file).filename().wstring());
    }
    void savePreset() {
        auto file=chooseFile(window_,L"Normal preset (*.krnp)\0*.krnp\0",L"krnp",true,L"brush.krnp");if(file.empty())return;
        std::string error;if(!lab::SaveNormalPreset(preset_,file,error))fail(error);else status("Пресет сохранён.");
    }
    void endInteraction() {
        if(strokeMaterial_&&strokeMaterial_->document){const bool changed=strokeMaterial_->document->EndStroke();if(changed&&!deviceLost_)upload(strokeMaterial_);}
        if(drag_==Drag::Light&&!deviceLost_)applyLightSettings();
        strokeMaterial_.reset();lastBrushValid_=false;drag_=Drag::None;dragButton_=-1;
        if(!deviceLost_)renderer_.ClearBrushCursor();
    }
    void finishPointerInteraction() {
        // Win32 can release capture before the next ImGui frame delivers its
        // mouse-up. Include that final point instead of dropping the stroke tail.
        if(drag_==Drag::Brush&&strokeMaterial_) {
            POINT mouse{};
            if(GetCursorPos(&mouse)&&ScreenToClient(window_,&mouse)&&
               mouse.x>=ui_.viewportRect.x&&mouse.y>=ui_.viewportRect.y&&
               mouse.x<ui_.viewportRect.x+ui_.viewportRect.w&&mouse.y<ui_.viewportRect.y+ui_.viewportRect.h) {
                const POINT point=scenePoint(ImVec2(float(mouse.x),float(mouse.y)));
                stampAt(point.x,point.y);
            }
        }
        endInteraction();
    }
    bool beginStroke(int x,int y) {
        if(ui_.editingLocked||!resolveAdjustments())return false;
        lab::Renderer::PickHit hit;if(!renderer_.PickUV(x,y,hit)||hit.part<0||size_t(hit.part)>=meshes_.size())return false;
        // A stroke has one document owner. Crossing another material never edits it.
        auto material=meshes_[size_t(hit.part)].material;
        const bool createBase=!material->hasBump();
        if(!ensureBase(material)||!ensureDocument(material))return false;
        if(createBase)upload(material);
        renderer_.SelectPart(hit.part);lab::BrushSettings settings;
        settings.radiusPixels=std::max(1.f,ui_.brushRadiusPercent*.01f*std::max(material->document->Width(),material->document->HeightPixels()));
        settings.strength=ui_.brushStrengthPercent*.01f;settings.hardness=ui_.brushHardnessPercent*.01f;
        const auto operation=lab::BrushOperation(std::clamp(ui_.brushChannel,0,3)*2+(ui_.brushRemove?1:0));
        if(!material->document->BeginStroke(operation,settings))return false;
        strokeMaterial_=material;lastBrushValid_=false;lastBrushUpload_=GetTickCount64();return true;
    }
    void applySurfaceAt(int x,int y) {
        if(ui_.editingLocked||!resolveAdjustments())return;
        endInteraction();
        if(ui_.brushStrengthPercent<=0){status("Сила рельефа равна нулю. Увеличьте её колесом над ползунком.");return;}
        lab::Renderer::PickHit hit;
        if(!renderer_.PickUV(x,y,hit)||hit.part<0||size_t(hit.part)>=meshes_.size())return;
        const auto material=meshes_[size_t(hit.part)].material;
        if(!material->sources.diffuse.valid()){status("Для нанесения рельефа загрузите diffuse этого материала.");return;}
        const bool createBase=!material->hasBump();
        if(!ensureBase(material)||!ensureDocument(material))return;
        renderer_.SelectPart(hit.part);
        const lab::BrushSettings settings{std::max(1.f,ui_.brushRadiusPercent*.01f*std::max(material->document->Width(),material->document->HeightPixels())),ui_.brushStrengthPercent*.01f,ui_.brushHardnessPercent*.01f};
        const bool changed=material->document->ApplySurfaceRelief(material->sources.diffuse,lab::SurfacePreset(ui_.surfacePreset),hit.u,hit.v,settings);
        if(changed||createBase)upload(material);
        status(changed?"Рельеф нанесён одним отпечатком. Ctrl+Z — отменить.":"Участок не изменён: в diffuse здесь недостаточно деталей для выбранного размера и силы.");
    }
    void stampAt(int x,int y) {
        if(!strokeMaterial_||!strokeMaterial_->document)return;
        lab::SampleBrushStroke(lastBrush_.x,lastBrush_.y,x,y,lastBrushValid_,[&](int px,int py){
            lab::Renderer::PickHit hit;
            if(!renderer_.PickUV(px,py,hit)||hit.part<0||size_t(hit.part)>=meshes_.size()||meshes_[size_t(hit.part)].material!=strokeMaterial_)return;
            strokeMaterial_->document->Stamp(hit.u,hit.v,preset_);
        });
        lastBrush_={x,y};lastBrushValid_=true;
        if(GetTickCount64()-lastBrushUpload_>=100){upload(strokeMaterial_);lastBrushUpload_=GetTickCount64();}
    }
    void editHistory(lab::EditorAction action) {
        if(ui_.editingLocked)return;endInteraction();auto material=selectedMaterial();if(!ensureDocument(material))return;
        bool changed=action==lab::EditorAction::Undo?material->document->Undo():action==lab::EditorAction::Redo?material->document->Redo():material->document->Clear();
        if(changed)upload(material);
    }
    void action(lab::EditorAction kind,int value=0) {
        using A=lab::EditorAction;
        ui_.editingLocked=ui_.tab==lab::EditorTab::Animation;
        if(ui_.editingLocked&&(kind==A::LoadDiffuse||kind==A::LoadNormal||kind==A::LoadGloss||kind==A::LoadHeight||kind==A::LoadBump||kind==A::LoadBumpX||
            kind==A::RequestGenerate||kind==A::GenerateSelected||kind==A::OpenSession||kind==A::AdjustMaterial||kind==A::ApplyAdjustments||kind==A::DiscardAdjustments))return;
        if(kind!=A::AdjustMaterial&&kind!=A::ApplyAdjustments&&kind!=A::DiscardAdjustments&&kind!=A::SaveSession&&kind!=A::PreviewSettings&&kind!=A::LightSettings&&kind!=A::BackgroundSettings&&kind!=A::GlossSettings&&kind!=A::AlphaSettings)
            if(!resolveAdjustments())return;
        if(ui_.editingLocked&&(kind==A::Isolate||kind==A::Visibility||kind==A::ShowAll||kind==A::HideSelected||kind==A::ClearSelection))return;
        switch(kind) {
        case A::OpenModel:openModel();break;
        case A::NewCube:newCube();break;
        case A::LoadDiffuse:case A::LoadNormal:case A::LoadGloss:case A::LoadHeight:case A::LoadBump:case A::LoadBumpX:loadSource(kind);break;
        case A::RequestGenerate:
            endInteraction();ui_.generateTargets.assign(meshes_.size(),false);
            for(size_t i=0;i<meshes_.size();++i)ui_.generateTargets[i]=renderer_.PartSelected(i)&&meshes_[i].material->sources.diffuse.valid();
            ui_.requestGeneratePopup=true;break;
        case A::GenerateSelected:generateSelected();break;
        case A::Export:exportPair();break;
        case A::SaveSession:applyAdjustments();saveSession(selectedMaterial());break;
        case A::AdjustMaterial:adjustMaterial();break;
        case A::ApplyAdjustments:applyAdjustments();break;
        case A::DiscardAdjustments:applyAdjustments(true);break;
        case A::GlossSettings:{renderer_.GlossEnabled(ui_.glossEnabled);std::unordered_set<Material*> seen;for(const auto& row:meshes_)if(seen.insert(row.material.get()).second)upload(row.material);
            const auto ini=executableDirectory()/L"MaterialLab.ini";WritePrivateProfileStringW(L"Scene",L"GlossEnabled",ui_.glossEnabled?L"1":L"0",ini.c_str());break;}
        case A::BackgroundSettings:{renderer_.SetBackground(ui_.background);const auto ini=executableDirectory()/L"MaterialLab.ini";const auto backgroundValue=std::to_wstring(colorRef(ui_.background));WritePrivateProfileStringW(L"Scene",L"Background",backgroundValue.c_str(),ini.c_str());break;}
        case A::AxisView:endInteraction();renderer_.AlignAxis(value);break;
        case A::SetBrushTool:
            if(value>=0&&value<=int(lab::BrushTool::SurfaceRelief)){
                if(resolveAdjustments()){endInteraction();ui_.brushTool=lab::BrushTool(value);if(ui_.brushTool==lab::BrushTool::SurfaceRelief)setMode(lab::EditorMode::Brush);}
                else ui_.forceBrushTab=true;
            }break;
        case A::SurfacePreset:
            if(value>=0&&value<5&&!ui_.editingLocked&&resolveAdjustments()){
                setMode(lab::EditorMode::Brush);
                if(ui_.mode==lab::EditorMode::Brush){ui_.brushTool=lab::BrushTool::SurfaceRelief;ui_.forceBrushTab=true;ui_.surfacePreset=value;status("Пресет выбран. Один щелчок по модели — один отпечаток рельефа из diffuse.");}
            }break;
        case A::OpenSession:openSession();break;
        case A::LoadPreset:loadPreset();break;
        case A::SavePreset:savePreset();break;
        case A::Undo:case A::Redo:case A::ClearBrush:editHistory(kind);break;
        case A::SetMode:setMode(lab::EditorMode(value));break;
        case A::Outline:ui_.outline=!ui_.outline;renderer_.OutlineEnabled(ui_.outline);break;
        case A::Wireframe:ui_.wireframe=!ui_.wireframe;renderer_.WireframeEnabled(ui_.wireframe);break;
        case A::Frame:renderer_.FrameSelection();break;
        case A::Isolate:toggleIsolation();break;
        case A::ResetView:endInteraction();renderer_.ResetView();renderer_.ResetModelRotation();break;
        case A::SelectPart:
            if(!ui_.editingLocked&&value>=0&&size_t(value)<meshes_.size()){endInteraction();renderer_.SelectPart(value);}break;
        case A::Visibility:
            if(value>=0&&size_t(value)<meshes_.size()&&size_t(value)<ui_.meshes.size()){endInteraction();const bool visible=ui_.meshes[size_t(value)].visible;restoreIsolation();renderer_.SetPartVisible(size_t(value),visible);}break;
        case A::ShowAll:endInteraction();restoreIsolation();for(size_t i=0;i<meshes_.size();++i)renderer_.SetPartVisible(i,true);break;
        case A::HideSelected:
            endInteraction();restoreIsolation();for(size_t i=0;i<meshes_.size();++i)if(renderer_.PartSelected(i))renderer_.SetPartVisible(i,false);break;
        case A::ClearSelection:endInteraction();renderer_.ClearSelection();break;
        case A::PreviewSettings:applyPreviewSettings();break;
        case A::LightSettings:applyLightSettings();break;
        case A::AlphaSettings:applyAlphaSettings();break;
        case A::Skeleton:renderer_.SkeletonEnabled(ui_.skeleton);break;
        case A::BipedBones:{const auto ini=executableDirectory()/L"MaterialLab.ini";WritePrivateProfileStringW(L"Scene",L"BipedBones",ui_.showBipedBones?L"1":L"0",ini.c_str());break;}
        case A::SelectAttachmentBone:selectAttachmentBone(value);break;
        case A::LoadAttachment:chooseAttachment();break;
        case A::UseRecentAttachment:
            if(value>=0&&size_t(value)<recentAttachments_.size()){const auto recent=recentAttachments_[size_t(value)];loadAttachment(fs::path(wide(recent.path)),recent.transform);}break;
        case A::RemoveAttachment:
            if(ui_.tab==lab::EditorTab::Animation&&ui_.attachmentBone>=0){renderer_.RemoveAttachment(size_t(ui_.attachmentBone));activeAttachments_.erase(size_t(ui_.attachmentBone));synchronizeAttachments();status("Предмет выгружен. Запись в недавних сохранена.");}break;
        case A::AttachmentTransform:changeAttachmentTransform();break;
        case A::ResetAttachmentTransform:changeAttachmentTransform(true);break;
        case A::CaptureAnimation:captureAnimation();break;
        case A::AnimationSelect:rememberAnimation();endInteraction();stopAnimation(false);animationTime_=0;setAnimationPose();break;
        case A::AnimationPlay:
            if(renderer_.MotionCount()) {endInteraction();setMode(lab::EditorMode::Fly);ui_.animationPlaying=!ui_.animationPlaying;ui_.editingLocked=true;setAnimationPose();}break;
        case A::AnimationSeek:
            endInteraction();if(auto* motion=renderer_.MotionAt(motionIndex())){animationTime_=float(ui_.animationFrame)/std::max(1.f,motion->fps);setAnimationPose();}break;
        case A::AnimationSettings:setAnimationPose();break;
        }
    }
    POINT scenePoint(ImVec2 mouse)const {
        return {LONG(std::floor((mouse.x-ui_.viewportRect.x)*renderer_.SceneWidth()/std::max(1.f,ui_.viewportRect.w))),
                LONG(std::floor((mouse.y-ui_.viewportRect.y)*renderer_.SceneHeight()/std::max(1.f,ui_.viewportRect.h)))};
    }
    void keyboard(float delta) {
        ImGuiIO& io=ImGui::GetIO();
        // ImGui clears logical keys on focus loss. Only a physical key release
        // may clear the flight latch, otherwise a held S resumes on focus return.
        auto held=[](int vk){return (GetAsyncKeyState(vk)&0x8000)!=0;};
        const lab::FlightInput::Keys keys={held('A'),held('D'),held('Q'),held('E'),held('W'),held('S')};
        const bool popup=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
        if(io.WantTextInput||GetForegroundWindow()!=window_||popup||io.KeyAlt){flightInput_.Sample(keys,false,false);return;}
        if(io.KeyCtrl) {
            bool shortcut=false;
            if(ImGui::IsKeyPressed(ImGuiKey_Z,false)){action(io.KeyShift?lab::EditorAction::Redo:lab::EditorAction::Undo);shortcut=true;}
            if(ImGui::IsKeyPressed(ImGuiKey_Y,false)){action(lab::EditorAction::Redo);shortcut=true;}
            if(ImGui::IsKeyPressed(ImGuiKey_S,false)){flightInput_.Suspend();action(selectedMaterial()&&selectedMaterial()->document&&selectedMaterial()->document->HasPendingAdjustments()?lab::EditorAction::ApplyAdjustments:lab::EditorAction::SaveSession);shortcut=true;}
            if(shortcut){flightInput_.Sample(keys,false,true);return;}
        }
        const bool allowed=viewportFocus_&&GetForegroundWindow()==window_&&(!io.WantCaptureKeyboard||ui_.viewportActive)&&(!ImGui::IsAnyItemActive()||ui_.viewportActive);
        if(!allowed){flightInput_.Sample(keys,false,false);return;}
        auto pressed=[](ImGuiKey key){return ImGui::IsKeyPressed(key,false);};
        if(!io.KeyCtrl) {
            if(pressed(ImGuiKey_1)||pressed(ImGuiKey_Escape))setMode(lab::EditorMode::Fly);
            if(pressed(ImGuiKey_2))setMode(lab::EditorMode::Orbit);
            if(pressed(ImGuiKey_3)||pressed(ImGuiKey_C))setMode(lab::EditorMode::Choose);
            if(pressed(ImGuiKey_4))setMode(lab::EditorMode::Light);
            if(pressed(ImGuiKey_5)||pressed(ImGuiKey_B))setMode(lab::EditorMode::Brush);
            if(pressed(ImGuiKey_6)||pressed(ImGuiKey_R))setMode(lab::EditorMode::Model);
            if(pressed(ImGuiKey_F))action(lab::EditorAction::Frame);
            if(pressed(ImGuiKey_I))action(lab::EditorAction::Isolate);
            if(pressed(ImGuiKey_O))action(lab::EditorAction::Outline);
            if(pressed(ImGuiKey_V))action(lab::EditorAction::Wireframe);
            if(pressed(ImGuiKey_H))action(io.KeyShift?lab::EditorAction::ShowAll:lab::EditorAction::HideSelected);
            if(pressed(ImGuiKey_Home))action(lab::EditorAction::ResetView);
        }
        if(ui_.mode==lab::EditorMode::Fly) {
            const float speed=delta*(io.KeyShift?6.f:io.KeyCtrl?.6f:2.f);
            const auto movement=flightInput_.Sample(keys,true,io.KeyCtrl||held(VK_CONTROL));
            renderer_.MoveCamera(movement.right*speed,movement.up*speed,movement.forward*speed);
        }else flightInput_.Sample(keys,false,false);
    }
    void viewportInput(float delta) {
        ImGuiIO& io=ImGui::GetIO();const POINT point=scenePoint(io.MousePos);
        if(ImGui::IsMouseClicked(0)||ImGui::IsMouseClicked(1)||ImGui::IsMouseClicked(2))viewportFocus_=ui_.viewportHovered;
        keyboard(delta);
        if(GetForegroundWindow()!=window_){endInteraction();renderer_.ClearHover();renderer_.ClearBrushCursor();return;}
        if(drag_!=Drag::None&&(dragButton_<0||!ImGui::IsMouseDown(dragButton_))) {
            if(drag_==Drag::Brush&&ui_.viewportHovered)stampAt(point.x,point.y);
            endInteraction();
        }
        if(ui_.viewportHovered&&drag_==Drag::None&&!ui_.bonePointerCaptured) {
            if(ImGui::IsMouseClicked(2)){drag_=Drag::Pan;dragButton_=2;}
            else if(ImGui::IsMouseClicked(1)) {
                if(ui_.mode==lab::EditorMode::Choose)action(lab::EditorAction::ClearSelection);
                else {drag_=Drag::Orbit;dragButton_=1;}
            }
            else if(ImGui::IsMouseClicked(0)) {
                using M=lab::EditorMode;
                if(ui_.mode==M::Choose){int part=renderer_.PickGeometry(point.x,point.y);if(resolveAdjustments()){endInteraction();renderer_.SelectPart(part);ui_.adjustments={};}}
                else if(ui_.mode==M::Brush){
                    if(ui_.brushTool==lab::BrushTool::SurfaceRelief)applySurfaceAt(point.x,point.y);
                    else if(beginStroke(point.x,point.y)){drag_=Drag::Brush;dragButton_=0;stampAt(point.x,point.y);}
                }
                else {drag_=ui_.mode==M::Light?Drag::Light:ui_.mode==M::Model?Drag::Model:Drag::Orbit;dragButton_=0;}
            }
        }
        const float dx=io.MouseDelta.x/dpiScale_,dy=io.MouseDelta.y/dpiScale_;
        if(drag_==Drag::Orbit)renderer_.OrbitCamera(dx,dy);
        if(drag_==Drag::Pan)renderer_.PanCamera(dx,dy);
        if(drag_==Drag::Model)renderer_.RotateModel(dx,dy);
        if(drag_==Drag::Light){renderer_.OrbitLight(dx,dy);ui_.lightAzimuth=renderer_.LightAzimuthDegrees();ui_.lightElevation=renderer_.LightElevationDegrees();}
        if(drag_==Drag::Brush&&(io.MouseDelta.x!=0||io.MouseDelta.y!=0)) {
            if(ui_.viewportHovered)stampAt(point.x,point.y);else lastBrushValid_=false;
        }
        if(ui_.viewportHovered&&io.MouseWheel!=0) {
            if(ui_.mode==lab::EditorMode::Brush){endInteraction();ui_.brushRadiusPercent=std::clamp(ui_.brushRadiusPercent+io.MouseWheel*.2f,.1f,20.f);}
            else renderer_.Zoom(io.MouseWheel);
        }
        if(ui_.viewportHovered&&(ui_.mode==lab::EditorMode::Choose||ui_.mode==lab::EditorMode::Brush)) {
            lab::Renderer::PickHit hit;
            if(renderer_.PickUV(point.x,point.y,hit)) {
                if(ui_.mode==lab::EditorMode::Choose)renderer_.SetHoverPart(hit.part);
                else if(hit.part>=0&&size_t(hit.part)<meshes_.size()) {
                    auto material=meshes_[size_t(hit.part)].material;
                    uint32_t w=material->document?material->document->Width():material->loadedPair.bump.w;
                    uint32_t h=material->document?material->document->HeightPixels():material->loadedPair.bump.h;
                    const float radius=ui_.brushRadiusPercent*.01f*std::max(w,h);
                    if(w&&h)renderer_.SetBrushCursor(hit,radius/w,radius/h);else renderer_.ClearBrushCursor();
                }
            }else{renderer_.ClearHover();renderer_.ClearBrushCursor();}
        }else{renderer_.ClearHover();renderer_.ClearBrushCursor();}
    }
    void tickAnimation(float delta) {
        if(!ui_.animationPlaying)return;
        auto* motion=renderer_.MotionAt(motionIndex());if(!motion){stopAnimation(true);return;}
        animationTime_+=delta*ui_.animationSpeed*motion->speed;
        if(!ui_.animationLoop&&animationTime_>=motion->duration()){animationTime_=std::max(0.f,motion->duration()-1.f/std::max(1.f,motion->fps));stopAnimation(false);}
        else if(ui_.animationLoop&&motion->duration()>0)animationTime_=std::fmod(animationTime_,motion->duration());
        setAnimationPose();
    }
    void applyDpi() {
        ImGuiIO& io=ImGui::GetIO();
        if(imguiReady_)ImGui_ImplDX11_InvalidateDeviceObjects();
        lab::ConfigureEditorUIStyle(dpiScale_);ui_.uiScale=dpiScale_;
        io.Fonts->Clear();static ImVector<ImWchar> ranges;
        ImFontGlyphRangesBuilder builder;builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
        const ImWchar extra[]={0x2000,0x206f,0x2190,0x22ff,0};builder.AddRanges(extra);builder.BuildRanges(&ranges);
        wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);
        const auto font=fs::path(windows)/L"Fonts"/L"segoeui.ttf";
        if(!io.Fonts->AddFontFromFileTTF(utf8(font.wstring()).c_str(),16.f*dpiScale_,nullptr,ranges.Data))io.Fonts->AddFontDefault();
        if(imguiReady_&&!ImGui_ImplDX11_CreateDeviceObjects())throw std::runtime_error("Direct3D: font resources could not be created");
    }
    bool dpiPending_=false;
    void frame(bool input=true) {
        const auto now=GetTickCount64();const float delta=std::clamp(float(now-lastTick_)*.001f,.001f,.05f);lastTick_=now;
        if(dpiPending_){applyDpi();dpiPending_=false;}
        // Resize before submitting the Image draw command, whose texture must stay alive for this frame.
        if(ui_.viewportRect.w>1&&ui_.viewportRect.h>1) {
            std::wstring error;
            // ResizeScene keeps its previous surface on failure. Continue the
            // UI so the error is visible and the user can shrink the viewport.
            if(!renderer_.ResizeScene(UINT(ui_.viewportRect.w),UINT(ui_.viewportRect.h),error))status(utf8(error));
        }
        ui_.viewportTexture=static_cast<ImTextureID>(reinterpret_cast<intptr_t>(renderer_.SceneTexture()));
        synchronizeUI();const auto oldTab=ui_.tab;
        ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();
        std::vector<std::pair<lab::EditorAction,int>> actions;
        lab::DrawEditorUI(ui_,[&](lab::EditorAction kind,int value){actions.emplace_back(kind,value);});
        // Resolve the old document before any queued action can change its owner.
        if(ui_.tab!=oldTab&&!resolveAdjustments()){ui_.tab=oldTab;ui_.forceTab=true;actions.clear();}
        for(const auto& request:actions)action(request.first,request.second);
        if(ui_.tab!=oldTab){endInteraction();flightInput_.Suspend();if(oldTab==lab::EditorTab::Animation){unloadAttachments();stopAnimation(true);}if(ui_.tab==lab::EditorTab::Animation)setMode(lab::EditorMode::Fly);}
        if(focusResolution_){focusResolution_=false;resolveAdjustments();}
        if(input)viewportInput(delta);tickAnimation(delta);
        if(attachmentHistoryDirty_&&GetTickCount64()>=attachmentHistoryAfter_&&!ImGui::IsAnyItemActive())saveAttachmentHistory();
        if(!exactQueue_.empty()&&GetTickCount64()>=exactAfter_&&!ImGui::IsMouseDown(0)&&drag_!=Drag::Brush){
            auto material=exactQueue_.front();exactQueue_.erase(exactQueue_.begin());
            if(!uploadExact(material))status("Не удалось обновить проверочное DDS-превью.");
        }
        ImGui::Render();renderer_.DrawScene();renderer_.BeginUI();ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const HRESULT presented=renderer_.PresentUI();
        if(FAILED(presented)){status("Direct3D: устройство вывода потеряно.");deviceLost_=true;exitCode_=1;}
    }
    void recoverDocuments() {
        recoveryAttempted_=true;
        // Saving sessions is CPU-only and remains available after a device loss.
        bool saved=true;size_t count=0;fs::path directory;
        try {
            try{endInteraction();}catch(const std::exception&){saved=false;}
            directory=fs::temp_directory_path()/L"MaterialLab-recovery"/(std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
            std::unordered_set<Material*> seen;
            for(const auto& row:meshes_)if(seen.insert(row.material.get()).second&&row.material->document&&row.material->document->IsDirty()) {
                std::error_code ec;fs::create_directories(directory,ec);
                const fs::path file=directory/(std::to_wstring(++count)+L"-"+safeName(wide(row.material->label))+L".krlab");
                std::string error;
                if(ec||!row.material->document->Save(file,error))saved=false;
                else row.material->sessionPath=file;
            }
        }catch(const std::exception&){saved=false;}
        const std::wstring message=saved?(count?L"Ошибка вывода. Изменённые материалы сохранены для восстановления:\n\n"+directory.wstring()+L"\n\nПосле перезапуска откройте эти .krlab через «Открыть сеанс».":L"Ошибка вывода. Несохранённых изменений нет."):
            L"Ошибка вывода. Не удалось автоматически сохранить все материалы. Сохраните оставшиеся сеансы в следующем диалоге. При отмене данные останутся в памяти; закройте окно, чтобы повторить сохранение.";
        MessageBoxW(window_,message.c_str(),AppTitle,MB_OK|MB_ICONERROR);
        if(saved||confirmAll())running_=false;
    }
    static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
        App* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);app->window_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
        const bool handled=app&&app->imguiReady_&&ImGui_ImplWin32_WndProcHandler(window,message,wParam,lParam)!=0;
        if(!app)return DefWindowProcW(window,message,wParam,lParam);
        switch(message) {
        case WM_SIZE:
            app->minimized_=wParam==SIZE_MINIMIZED;
            if(app->renderer_.Device()&&!app->minimized_){std::wstring error;if(!app->renderer_.Resize(UINT(LOWORD(lParam)),UINT(HIWORD(lParam)),error))app->status(utf8(error));}
            return 0;
        case WM_DPICHANGED: {
            app->endInteraction();app->dpiScale_=float(HIWORD(wParam))/96.f;app->dpiPending_=true;
            const RECT& rect=*reinterpret_cast<const RECT*>(lParam);const RECT work=monitorWorkArea(window);
            const int width=std::min(rect.right-rect.left,work.right-work.left),height=std::min(rect.bottom-rect.top,work.bottom-work.top);
            SetWindowPos(window,nullptr,std::clamp(int(rect.left),int(work.left),int(work.right)-width),std::clamp(int(rect.top),int(work.top),int(work.bottom)-height),width,height,SWP_NOZORDER|SWP_NOACTIVATE);return 0;
        }
        case WM_GETMINMAXINFO: {
            const RECT work=monitorWorkArea(window);
            auto* limits=reinterpret_cast<MINMAXINFO*>(lParam);limits->ptMinTrackSize={std::min(LONG(960*app->dpiScale_),work.right-work.left),std::min(LONG(600*app->dpiScale_),work.bottom-work.top)};return 0;
        }
        case WM_CAPTURECHANGED:
            if(reinterpret_cast<HWND>(lParam)!=window)app->finishPointerInteraction();
            return 0;
        case WM_CANCELMODE:app->endInteraction();app->flightInput_.Suspend();return 0;
        case WM_ACTIVATEAPP:if(!wParam){app->endInteraction();app->flightInput_.Suspend();app->viewportFocus_=false;}break;
        case WM_KILLFOCUS:app->endInteraction();app->flightInput_.Suspend();app->viewportFocus_=false;if(!app->resolvingAdjustments_&&app->selectedMaterial()&&app->selectedMaterial()->document&&app->selectedMaterial()->document->HasPendingAdjustments())app->focusResolution_=true;return 0;
        case WM_CLOSE:
            if(app->confirmAll()){
                if(!app->saveAttachmentHistory()&&MessageBoxW(window,L"Не удалось записать последние предметы и их посадку. Прежняя история на диске сохранена.\n\nЗакрыть приложение без последних изменений истории?",AppTitle,MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES)return 0;
                DestroyWindow(window);
            }return 0;
        case WM_DESTROY:app->running_=false;PostQuitMessage(0);return 0;
        case WM_ERASEBKGND:return 1;
        case WM_SYSCOMMAND:if((wParam&0xfff0)==SC_KEYMENU)return 0;break;
        }
        return handled?1:DefWindowProcW(window,message,wParam,lParam);
    }
public:
    ~App() {
        if(imguiReady_){ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();imguiReady_=false;}
        if(ImGui::GetCurrentContext())ImGui::DestroyContext();
        if(window_&&IsWindow(window_))DestroyWindow(window_);
    }
    bool create(HINSTANCE instance,bool software=false) {
        smoke_=software;
        ImGui_ImplWin32_EnableDpiAwareness();
        WNDCLASSEXW klass{};klass.cbSize=sizeof(klass);klass.style=CS_CLASSDC;klass.lpfnWndProc=windowProc;klass.hInstance=instance;klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.lpszClassName=L"KichRayMaterialLabImGui";
        // Shared resource icons live for the lifetime of the module.
        klass.hIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_MATERIALLAB),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_SHARED));
        klass.hIconSm=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_MATERIALLAB),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
        if(!RegisterClassExW(&klass)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
        dpiScale_=ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY));
        const RECT work=monitorWorkArea(nullptr);
        const int width=std::min(int(1360*dpiScale_),int(work.right-work.left)),height=std::min(int(850*dpiScale_),int(work.bottom-work.top));
        window_=CreateWindowExW(0,klass.lpszClassName,AppTitle,WS_OVERLAPPEDWINDOW,work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,nullptr,nullptr,instance,this);
        if(!window_)return false;
        std::wstring error;if(!renderer_.Init(window_,error,software)){fail(utf8(error));return false;}
        IMGUI_CHECKVERSION();ImGui::CreateContext();ImGuiIO& io=ImGui::GetIO();
        io.ConfigFlags&=~ImGuiConfigFlags_NavEnableKeyboard;io.IniFilename=nullptr;io.LogFilename=nullptr;
        applyDpi();ImGui::GetStyle().HoverDelayNormal=.6f;
        if(!ImGui_ImplWin32_Init(window_)){fail("Не удалось создать интерфейс Win32.");return false;}
        if(!ImGui_ImplDX11_Init(renderer_.Device(),renderer_.Context())){ImGui_ImplWin32_Shutdown();fail("Не удалось создать интерфейс Direct3D.");return false;}
        imguiReady_=true;if(!ImGui_ImplDX11_CreateDeviceObjects()){fail("Не удалось загрузить шрифт на видеокарту.");return false;}
        if(!newCube(false))return false;
        const fs::path ini=executableDirectory()/L"MaterialLab.ini";
        ui_.lightAzimuth=float(GetPrivateProfileIntW(L"Scene",L"LightAzimuth",52,ini.c_str()));
        ui_.lightElevation=std::clamp(float(GetPrivateProfileIntW(L"Scene",L"LightElevation",37,ini.c_str())),-81.f,81.f);
        std::array<wchar_t,32768> remembered{};
        GetPrivateProfileStringW(L"Paths",L"OGFDirectory",L"",remembered.data(),DWORD(remembered.size()),ini.c_str());lastModelDirectory_=remembered.data();
        GetPrivateProfileStringW(L"Paths",L"ExportDirectory",L"",remembered.data(),DWORD(remembered.size()),ini.c_str());lastExportDirectory_=remembered.data();
        GetPrivateProfileStringW(L"Paths",L"AttachmentDirectory",L"",remembered.data(),DWORD(remembered.size()),ini.c_str());lastAttachmentDirectory_=remembered.data();
        GetPrivateProfileStringW(L"Paths",L"ScreenshotDirectory",L"",remembered.data(),DWORD(remembered.size()),ini.c_str());lastScreenshotDirectory_=remembered.data();
        GetPrivateProfileStringW(L"Scene",L"LightIntensity",L"1",remembered.data(),DWORD(remembered.size()),ini.c_str());ui_.lightIntensity=std::clamp(float(wcstod(remembered.data(),nullptr)),0.f,8.f);if(!std::isfinite(ui_.lightIntensity))ui_.lightIntensity=1;
        ui_.materialClass=std::clamp(int(GetPrivateProfileIntW(L"Scene",L"MaterialClass",0,ini.c_str())),0,4);
        ui_.glossEnabled=GetPrivateProfileIntW(L"Scene",L"GlossEnabled",1,ini.c_str())!=0;
        ui_.showBipedBones=GetPrivateProfileIntW(L"Scene",L"BipedBones",0,ini.c_str())!=0;
        auto readScalar=[&](const wchar_t* key,float initial,float maximum){
            const auto fallback=std::to_wstring(initial);GetPrivateProfileStringW(L"Scene",key,fallback.c_str(),remembered.data(),DWORD(remembered.size()),ini.c_str());
            const float value=float(wcstod(remembered.data(),nullptr));return std::isfinite(value)?std::clamp(value,0.f,maximum):initial;};
        ui_.hemisphere=readScalar(L"Hemisphere",1.f,1.f);ui_.exposure=readScalar(L"Exposure",1.f,4.f);ui_.glossFactor=readScalar(L"GlossFactor",1.f,4.f);
        auto readColor=[&](const wchar_t* key,float* color){const COLORREF value=GetPrivateProfileIntW(L"Scene",key,colorRef(color),ini.c_str());color[0]=GetRValue(value)/255.f;color[1]=GetGValue(value)/255.f;color[2]=GetBValue(value)/255.f;};
        readColor(L"Background",ui_.background);readColor(L"LightColor",ui_.lightColor);readColor(L"AmbientColor",ui_.ambient);readColor(L"EnvironmentColor",ui_.environment);renderer_.SetBackground(ui_.background);
        applyPreviewSettings();applyAlphaSettings();applyLightSettings(false);renderer_.OutlineEnabled(ui_.outline);renderer_.SkeletonEnabled(ui_.skeleton);
        loadAttachmentHistory();
        // Prepare the real viewport, font atlas and initial material while the
        // splash is visible. No blank editor window is shown during loading.
        lastTick_=GetTickCount64();frame(false);frame(false);
        if(deviceLost_){fail(ui_.status);return false;}return true;
    }
    void show() { if(window_&&IsWindow(window_)){ShowWindow(window_,SW_SHOWDEFAULT);UpdateWindow(window_);lastTick_=GetTickCount64();} }
    int run() {
        MSG message{};
        while(running_) {
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){running_=false;break;}TranslateMessage(&message);DispatchMessageW(&message);}
            if(!running_)break;
            if(deviceLost_){if(!recoveryAttempted_)recoverDocuments();else WaitMessage();continue;}
            if(minimized_){WaitMessage();continue;}
            try{frame();}catch(const std::exception& error){status(std::string("Ошибка: ")+error.what());deviceLost_=true;exitCode_=1;}
        }
        return exitCode_;
    }
    int smokeTest() {
        std::ofstream log(executableDirectory()/L"smoke-tests.txt",std::ios::trunc);bool success=true;
        auto check=[&](bool condition,const char* message){log<<(condition?"PASS ":"FAIL ")<<message<<'\n';log.flush();success=condition&&success;};
        auto pump=[&]{MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}};
        auto same=[](const lab::Image& a,const lab::Image& b){return a.valid()&&b.valid()&&a.w==b.w&&a.h==b.h&&std::memcmp(a.pixels.data(),b.pixels.data(),a.pixels.size()*sizeof(lab::Pixel))==0;};
        try {
            check(renderer_.SceneTexture()!=nullptr,"offscreen scene SRV exists");
            for(const auto dimensions:{POINT{960,600},POINT{1490,900},POINT{1120,640}}) {
                SetWindowPos(window_,nullptr,0,0,dimensions.x,dimensions.y,SWP_NOMOVE|SWP_NOZORDER);pump();
                for(int repeat=0;repeat<2;++repeat)for(int tab=5;tab>=0;--tab) {
                    ui_.tab=lab::EditorTab(tab);ui_.forceTab=true;frame(false);frame(false);
                    check(ui_.renderedPaneCount==1,"exactly one active inspector pane after tab switch");
                    check(ui_.toolbarRect.y>=ui_.viewportRect.y+ui_.viewportRect.h-1,"camera tools remain below the viewport");
                    check(ui_.viewportRect.x>=ui_.inspectorRect.x+ui_.inspectorRect.w-1,"inspector does not overlap the scene");
                    check(ui_.toolbarRect.x+ui_.toolbarRect.w<=ImGui::GetIO().DisplaySize.x+1,"toolbar fits in client width");
                }
            }
            // Changing tools must neither re-generate the material nor alter preview strength.
            auto material=selectedMaterial();material->sources.diffuse=lab::DefaultChecker(64);
            lab::Settings settings;settings.normalStrength=1.7f;
            const auto pair=lab::GenerateBumpPreview(material->sources,settings);
            material->document=std::make_unique<lab::MaterialDocument>();std::string error;
            check(material->document->Load(pair,error,true),"generated material initializes at source resolution");
            check(upload(material,true),"material uploads");renderer_.OutlineEnabled(false);renderer_.ClearBrushCursor();
            lab::Image initial,toolChange,painted,undone,redone;renderer_.CaptureFrame(initial);
            setMode(lab::EditorMode::Brush);renderer_.CaptureFrame(toolChange);
            check(same(initial,toolChange)&&renderer_.NormalStrength()==1.f,"entering brush leaves normals and preview gain unchanged");
            check(beginStroke(int(renderer_.SceneWidth()/2),int(renderer_.SceneHeight()/2)),"ray-picked brush stroke starts");
            stampAt(int(renderer_.SceneWidth()/2),int(renderer_.SceneHeight()/2));
            drag_=Drag::Brush;dragButton_=0;
            POINT previousCursor{};GetCursorPos(&previousCursor);
            POINT cursor{LONG(ui_.viewportRect.x+ui_.viewportRect.w*.5f),LONG(ui_.viewportRect.y+ui_.viewportRect.h*.5f)};ClientToScreen(window_,&cursor);SetCursorPos(cursor.x,cursor.y);
            SendMessageW(window_,WM_CAPTURECHANGED,0,0);SetCursorPos(previousCursor.x,previousCursor.y);
            check(!material->document->InStroke()&&drag_==Drag::None&&!strokeMaterial_,"capture loss commits and releases stroke ownership");
            renderer_.CaptureFrame(painted);
            check(material->document->CanUndo()&&!same(initial,painted),"stroke visibly changes scene and creates history");
            editHistory(lab::EditorAction::Undo);renderer_.CaptureFrame(undone);check(same(initial,undone),"undo restores exact rendered baseline");
            editHistory(lab::EditorAction::Redo);renderer_.CaptureFrame(redone);check(same(painted,redone),"redo restores exact rendered stroke");
            // Real Win32 modal responses: apply/discard/cancel must preserve draft ownership.
            auto answerPrompt=[&](int answer){
                SetTimer(window_,UINT_PTR(900+answer),20,smokePromptTimer);
            };
            const auto beforeDraft=material->document->Preview(0);const size_t history=material->document->UndoCount();
            ui_.adjustments.normalStrength=2;adjustMaterial();check(material->document->HasPendingAdjustments(),"slider creates live pending draft");
            lab::Image draftFrame;renderer_.CaptureFrame(draftFrame);check(!same(draftFrame,redone),"slider draft visibly changes D3D scene");
            answerPrompt(IDCANCEL);check(!resolveAdjustments()&&material->document->HasPendingAdjustments(),"Cancel keeps draft and blocks transition");
            ui_.mode=lab::EditorMode::Brush;ui_.tab=lab::EditorTab::Material;answerPrompt(IDCANCEL);setMode(lab::EditorMode::Brush);
            check(ui_.tab==lab::EditorTab::Material&&material->document->HasPendingAdjustments(),"brush hotkey must resolve draft even when brush mode was already active");
            answerPrompt(IDNO);check(resolveAdjustments()&&!material->document->HasPendingAdjustments()&&same(beforeDraft.bump,material->document->Preview(0).bump),"No restores previous brushed material");
            ui_.adjustments.normalStrength=2;adjustMaterial();answerPrompt(IDYES);
            check(resolveAdjustments()&&!material->document->HasPendingAdjustments()&&material->document->UndoCount()==history+1,"Yes commits one history step");
            editHistory(lab::EditorAction::Undo);check(same(beforeDraft.bump,material->document->Preview(0).bump),"native applied draft undoes to previous brush work");
            check(uploadExact(material)&&material->exactReady,"full encoded DDS mip chain uploads to D3D11");
            const auto cache=material->encoded;ui_.glossEnabled=false;action(lab::EditorAction::GlossSettings);
            check(uploadExact(material),"gloss-disabled exact DDS preview uploads");
            auto muted=lab::DecodeDDSMipChain(material->encoded.bump,error);bool zeroGloss=!muted.empty();
            for(const auto& mip:muted)for(auto pixel:mip.pixels)zeroGloss=zeroGloss&&pixel.r==0;
            check(zeroGloss,"disabled export cache contains zero gloss in every mip");
            ui_.glossEnabled=true;action(lab::EditorAction::GlossSettings);check(uploadExact(material)&&material->encoded.bump==cache.bump,"gloss re-enable restores encoded material");
            renderer_.ClearBrushCursor();renderer_.SetColor(2,RGB(0,0,0));renderer_.SetLightIntensity(0);renderer_.SetEnvironmentLighting({},1,1,1);lab::Image dark,lit;renderer_.CaptureFrame(dark);
            const size_t center=size_t(dark.h/2)*dark.w+dark.w/2;
            check(dark.pixels[center].r==0&&dark.pixels[center].g==0&&dark.pixels[center].b==0,"zero lighting produces no fake glow");
            renderer_.SetLightIntensity(1);renderer_.CaptureFrame(lit);check(!same(lit,dark),"light intensity changes rendered lighting");applyLightSettings(false);
            const float redBackground[3]={.5f,0,0};renderer_.SetBackground(redBackground);lab::Image background;renderer_.CaptureFrame(background);
            check(background.pixels[0].r>=127&&background.pixels[0].g==0&&background.pixels[0].b==0,"background color reaches viewport render target");renderer_.SetBackground(ui_.background);
            for(int axis=0;axis<3;++axis){renderer_.AlignAxis(axis);check(renderer_.PickGeometry(int(renderer_.SceneWidth()/2),int(renderer_.SceneHeight()/2))==0,"X/Y/Z centering retains a finite pickable view");}
            renderer_.ResetView();
            const fs::path session=fs::temp_directory_path()/(L"MaterialLab-smoke-"+std::to_wstring(GetCurrentProcessId())+L".krlab");
            check(material->document->Save(session,error)&&material->document->CanUndo(),"saving retains undo history");
            lab::MaterialDocument reopened;check(reopened.Open(session,error)&&same(reopened.Preview(0).bump,material->document->Preview(0).bump),"session round trip preserves edited packed channels");
            std::error_code cleanup;fs::remove(session,cleanup);
            std::wstring renderError;check(renderer_.ResizeScene(96,64,renderError),"scene resize independent from window");
            lab::Image resizedFrame;renderer_.CaptureFrame(resizedFrame);check(resizedFrame.w==96&&resizedFrame.h==64,"capture uses scene dimensions");
            RECT rect{};GetClientRect(window_,&rect);check(renderer_.Resize(UINT(rect.right),UINT(rect.bottom),renderError)&&renderer_.SceneWidth()==96&&renderer_.SceneHeight()==64,"window resize keeps scene dimensions");
            lab::Image invalid=pair.bumpX;invalid.w+=1;
            check(!renderer_.SetPartBumpImages(0,pair.bump,invalid,true),"invalid texture pair rejected transactionally");
            lab::Image afterInvalid;renderer_.CaptureFrame(afterInvalid);check(same(resizedFrame,afterInvalid),"invalid texture pair keeps previous framebuffer");
            lab::Mesh invalidMesh=lab::MakeCube();invalidMesh.parts[0].indices[0]=uint32_t(invalidMesh.parts[0].vertices.size()+10);
            check(!renderer_.SetMesh(invalidMesh,renderError)&&renderer_.PartCount()==meshes_.size(),"invalid mesh preserves current scene");
            // Surface stamps ignore the freehand channel/removal state and
            // finish on mouse-down, so move/release/capture loss cannot repeat them.
            const auto surfaceBefore=material->document->Normal();
            const auto surfaceGloss=material->document->Gloss().pixels,surfaceHeight=material->document->Height().pixels;
            const size_t surfaceHistory=material->document->UndoCount();
            ui_.brushChannel=2;ui_.brushRemove=true;ui_.brushRadiusPercent=12;ui_.brushStrengthPercent=60;
            action(lab::EditorAction::SurfacePreset,int(lab::SurfacePreset::Fabric));
            check(ui_.mode==lab::EditorMode::Brush&&ui_.brushTool==lab::BrushTool::SurfaceRelief,"preset selection activates the dedicated brush tool");
            applySurfaceAt(48,32);
            const auto surfacePainted=material->document->Normal();
            check(material->document->UndoCount()==surfaceHistory+1&&!same(surfaceBefore,surfacePainted),"one picked surface click paints one undo step");
            check(drag_==Drag::None&&!strokeMaterial_&&!material->document->InStroke(),"surface click cannot start a continuous stroke");
            stampAt(54,35);finishPointerInteraction();
            check(material->document->UndoCount()==surfaceHistory+1&&same(surfacePainted,material->document->Normal()),"move and release cannot add another surface stamp");
            check(material->document->Gloss().pixels==surfaceGloss&&material->document->Height().pixels==surfaceHeight,"surface preset cannot inherit gloss removal");
            editHistory(lab::EditorAction::Undo);check(same(surfaceBefore,material->document->Normal()),"surface click undoes without losing earlier brush work");
            // Actual draw and picking through diffuse alpha, independent of bump alpha.
            lab::Mesh alphaMesh;
            for(float z:{-.25f,.25f}){lab::MeshPart part;part.vertices={{{-1,-1,z},{0,0,-1},0,1},{{-1,1,z},{0,0,-1},0,0},{{1,1,z},{0,0,-1},1,0},{{1,-1,z},{0,0,-1},1,1}};part.indices={0,1,2,0,2,3};alphaMesh.parts.push_back(std::move(part));}
            check(renderer_.SetMesh(alphaMesh,renderError),"alpha fixture uploads");renderer_.ResetView();renderer_.OutlineEnabled(false);renderer_.SkeletonEnabled(false);
            lab::PartImages rear;rear.diffuse.w=rear.diffuse.h=4;rear.diffuse.pixels.assign(16,{40,180,70,255});check(renderer_.SetPartImages(0,rear),"opaque rear material uploads");
            lab::PartImages front=rear;front.diffuse.pixels.assign(16,{220,30,30,0});check(renderer_.SetPartImages(1,front),"transparent front material uploads");
            lab::Image clear,through,opaque,blended;renderer_.AlphaEnabled(true);renderer_.SetPartVisible(1,false);renderer_.CaptureFrame(clear);renderer_.SetPartVisible(1,true);renderer_.CaptureFrame(through);
            check(same(clear,through),"zero diffuse alpha leaves background unchanged");
            lab::Renderer::PickHit hit;check(renderer_.PickUV(48,32,hit)&&hit.part==0,"ray picking passes through transparent diffuse");
            renderer_.AlphaEnabled(false);renderer_.CaptureFrame(opaque);check(!same(opaque,through),"alpha can be disabled for inspection");
            front.diffuse.pixels.assign(16,{220,30,30,128});renderer_.SetPartImages(1,front);renderer_.AlphaEnabled(true);renderer_.SetAlphaPreview(lab::AlphaPreview::Blend);renderer_.CaptureFrame(blended);
            check(!same(blended,opaque)&&!same(blended,through),"fractional alpha blends scene");
            lab::RunRendererFeatureSmoke(renderer_,check);
            lab::Image png;check(renderer_.CaptureTransparent(png,1024,renderError),"PNG capture renders current actor");
            const auto pngFile=fs::temp_directory_path()/(L"MaterialLab-alpha-"+std::to_wstring(GetCurrentProcessId())+L".png");
            check(saveTransparentPNG(pngFile,png,error),"WIC writes straight-alpha PNG");
            const auto reopenedPNG=loadImage(pngFile,error);
            check(same(png,reopenedPNG),"PNG round trip preserves every RGBA byte and dimensions");
            const auto pngBytes=readBytes(pngFile);lab::Image tooLarge;tooLarge.w=1025;tooLarge.h=1;tooLarge.pixels.resize(1025);
            check(!saveTransparentPNG(pngFile,tooLarge,error)&&pngBytes==readBytes(pngFile),"invalid screenshot cannot overwrite an existing PNG");
            fs::remove(pngFile,cleanup);
            newCube(false);applyPreviewSettings();applyAlphaSettings();applyLightSettings(false);
        }catch(const std::exception& error){log<<"FAIL exception: "<<error.what()<<'\n';success=false;}
        log<<(success?"All native smoke tests passed.\n":"Native smoke tests failed.\n");log.close();return success?0:1;
    }
};
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR args,int) {
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 2;
    int result=3;const bool smoke=args&&std::wstring(args)==L"--smoke-test";
    try {
        ImGui_ImplWin32_EnableDpiAwareness();
        lab::StartupSplash splash;
        const bool splashReady=splash.Start(instance);
        if(smoke&&!splashReady)throw std::runtime_error("Startup splash resource could not be loaded");
        App app;
        if(app.create(instance,smoke)){splash.Finish();app.show();result=smoke?app.smokeTest():app.run();}
        else splash.Cancel();
    }
    catch(const std::exception& error){if(smoke){std::ofstream log(executableDirectory()/L"smoke-tests.txt",std::ios::app);log<<"FAIL exception: "<<error.what()<<'\n';}else MessageBoxW(nullptr,wide(error.what()).c_str(),AppTitle,MB_OK|MB_ICONERROR);}
    CoUninitialize();return result;
}
