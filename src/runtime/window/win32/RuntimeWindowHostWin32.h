#pragma once
#include "runtime/window/RuntimeWindowLifecycle.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Private native interoperation between the Win32 window and the retained
// Win32 input/paint adapter. Never included by the lifecycle contract or core.
class RuntimeWindowHostWin32 final {
public:
    explicit RuntimeWindowHostWin32(WNDPROC dispatch) noexcept;
    ~RuntimeWindowHostWin32();
    RuntimeWindowHostWin32(const RuntimeWindowHostWin32&) = delete;
    RuntimeWindowHostWin32& operator=(const RuntimeWindowHostWin32&) = delete;
    RuntimeWindowCreateResult create(int width, int height);
    void attachSession(LONG_PTR session) noexcept;
    HWND window() const noexcept;
    // Drain the thread queue in the original Translate/Dispatch order. The
    // first quit ends the drain; no simulation or paint is advanced here.
    bool drainMessages();
private:
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    WNDPROC dispatch_;
    HWND window_ = nullptr;
};
