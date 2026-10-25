#pragma once
#include <windows.h>
#include <atomic>
#include <thread>

namespace lab {
// Only this thread owns the splash HWND/GDI objects. The editor and its D3D
// context stay on the main thread while the splash animation remains responsive.
class StartupSplash {
public:
    StartupSplash() = default;
    ~StartupSplash();
    StartupSplash(const StartupSplash&) = delete;
    StartupSplash& operator=(const StartupSplash&) = delete;
    bool Start(HINSTANCE instance);
    void Finish();
    void Cancel();
private:
    std::thread thread_;
    HANDLE ready_ = nullptr, done_ = nullptr;
    std::atomic<bool> finish_{false}, cancel_{false}, shown_{false};
    void Run(HINSTANCE instance) noexcept;
};
}
