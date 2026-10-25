#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "StartupSplash.h"
#include "Resource.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>

namespace lab {
namespace {
using Microsoft::WRL::ComPtr;
constexpr wchar_t SplashClass[] = L"KichRayMaterialLabStartup";
constexpr double FadeInMs = 900., MinimumVisibleMs = 1200., FadeOutMs = 250.;

struct SplashSurface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    ~SplashSurface() {
        if (previous && dc) SelectObject(dc, previous);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
};
LRESULT CALLBACK SplashProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_ERASEBKGND) return 1;
    return DefWindowProcW(window, message, wparam, lparam);
}
bool LoadSurface(HINSTANCE instance, int side, SplashSurface& surface) {
    const auto resource = FindResourceW(instance, MAKEINTRESOURCEW(IDR_STARTUP_PNG), RT_RCDATA);
    if (!resource) return false;
    const DWORD bytes = SizeofResource(instance, resource);
    const auto loaded = LoadResource(instance, resource);
    auto* png = static_cast<BYTE*>(loaded ? LockResource(loaded) : nullptr);
    if (!png || !bytes) return false;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.GetAddressOf()))) || FAILED(factory->CreateStream(stream.GetAddressOf())) ||
        FAILED(stream->InitializeFromMemory(png, bytes)) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())) ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf())) ||
        FAILED(factory->CreateBitmapScaler(scaler.GetAddressOf())) ||
        FAILED(scaler->Initialize(frame.Get(), UINT(side), UINT(side), WICBitmapInterpolationModeFant)) ||
        FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = side;
    info.bmiHeader.biHeight = -side;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    surface.dc = CreateCompatibleDC(nullptr);
    surface.bitmap = CreateDIBSection(surface.dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!surface.dc || !surface.bitmap || !pixels) return false;
    surface.previous = SelectObject(surface.dc, surface.bitmap);
    if (!surface.previous || surface.previous == HGDI_ERROR) { surface.previous = nullptr; return false; }
    // Layered windows require premultiplied BGRA; WIC performs that conversion.
    return SUCCEEDED(converter->CopyPixels(nullptr, UINT(side * 4), UINT(side * side * 4), static_cast<BYTE*>(pixels)));
}
double Ease(double t) { t = std::clamp(t, 0., 1.); return t * t * (3. - 2. * t); }
void WaitWithMessages(HANDLE event) {
    bool quit = false; int quitCode = 0;
    for (;;) {
        const DWORD result = MsgWaitForMultipleObjectsEx(1, &event, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (result != WAIT_OBJECT_0 + 1) break;
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { quit = true; quitCode = int(message.wParam); }
            else { TranslateMessage(&message); DispatchMessageW(&message); }
        }
    }
    if (quit) PostQuitMessage(quitCode);
}
}

StartupSplash::~StartupSplash() { Cancel(); }
bool StartupSplash::Start(HINSTANCE instance) {
    if (thread_.joinable()) return false;
    finish_ = cancel_ = shown_ = false;
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    done_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready_ || !done_) { Cancel(); return false; }
    try { thread_ = std::thread(&StartupSplash::Run, this, instance); }
    catch (...) { Cancel(); return false; }
    // Wait only for the first transparent frame, not for the fade to finish.
    WaitWithMessages(ready_);
    return shown_;
}
void StartupSplash::Finish() {
    finish_ = true;
    // Once the hidden editor HWND exists, keep its message queue serviced
    // while waiting for the other GUI/COM thread to finish.
    if (thread_.joinable()) { WaitWithMessages(done_); thread_.join(); }
    if (ready_) { CloseHandle(ready_); ready_ = nullptr; }
    if (done_) { CloseHandle(done_); done_ = nullptr; }
}
void StartupSplash::Cancel() { cancel_ = true; Finish(); }
void StartupSplash::Run(HINSTANCE instance) noexcept {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HWND window = nullptr;
    ATOM registered = 0;
    try {
        // All WIC objects are released before CoUninitialize on this thread.
        if (SUCCEEDED(com)) {
            POINT cursor{}; GetCursorPos(&cursor);
            const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO monitorInfo{}; monitorInfo.cbSize = sizeof(monitorInfo);
            if (!GetMonitorInfoW(monitor, &monitorInfo)) monitorInfo.rcWork = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
            const RECT work = monitorInfo.rcWork;
            // Scaling to the work area also keeps the emblem inside small/RDP screens.
            const int side = std::clamp(int(std::min(work.right-work.left, work.bottom-work.top) * .42f), 128, 640);
            POINT destination{work.left+(work.right-work.left-side)/2, work.top+(work.bottom-work.top-side)/2};
            SplashSurface surface;
            if (LoadSurface(instance, side, surface)) {
                WNDCLASSEXW klass{}; klass.cbSize = sizeof(klass); klass.hInstance = instance;
                klass.lpfnWndProc = SplashProc; klass.lpszClassName = SplashClass;
                registered = RegisterClassExW(&klass);
                if (registered) window = CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
                    SplashClass, L"Material Lab", WS_POPUP, destination.x, destination.y, side, side,
                    nullptr, nullptr, instance, nullptr);
                if (window) {
                    SIZE size{side, side}; POINT source{};
                    BLENDFUNCTION blend{AC_SRC_OVER, 0, 0, AC_SRC_ALPHA};
                    if (UpdateLayeredWindow(window, nullptr, &destination, &size, surface.dc, &source, 0, &blend, ULW_ALPHA)) {
                        ShowWindow(window, SW_SHOWNOACTIVATE);
                        shown_ = true;
                    }
                    SetEvent(ready_);
                    const ULONGLONG started = GetTickCount64();
                    ULONGLONG fadeOut = 0;
                    while (shown_ && !cancel_) {
                        MSG message{};
                        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                            if (message.message == WM_QUIT) { cancel_ = true; break; }
                            TranslateMessage(&message); DispatchMessageW(&message);
                        }
                        if (cancel_ || !IsWindow(window)) break;
                        const ULONGLONG now = GetTickCount64();
                        const double elapsed = double(now-started);
                        if (finish_ && elapsed >= MinimumVisibleMs && !fadeOut) fadeOut = now;
                        const double opacity = fadeOut ? 1.-Ease(double(now-fadeOut)/FadeOutMs) : Ease(elapsed/FadeInMs);
                        blend.SourceConstantAlpha = BYTE(std::lround(opacity*255.));
                        if (!UpdateLayeredWindow(window, nullptr, &destination, &size, surface.dc, &source, 0, &blend, ULW_ALPHA)) break;
                        if (fadeOut && double(now-fadeOut) >= FadeOutMs) break;
                        MsgWaitForMultipleObjectsEx(0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                    }
                }
            }
        }
    } catch (...) { /* A splash failure must never prevent the editor starting. */ }
    if (window && IsWindow(window)) DestroyWindow(window);
    if (registered) UnregisterClassW(SplashClass, instance);
    if (SUCCEEDED(com)) CoUninitialize();
    SetEvent(ready_); // also unblock startup after any early failure
    SetEvent(done_);
}
}
