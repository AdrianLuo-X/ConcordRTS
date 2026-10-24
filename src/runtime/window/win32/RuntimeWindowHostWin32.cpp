#include "runtime/window/win32/RuntimeWindowHostWin32.h"

namespace {
constexpr wchar_t hostProperty[] = L"ConcordRTS.RuntimeWindowHost.W013";
// CreateWindow dispatches synchronously before its return and before session
// attachment. Preserve that null-session path, including native defaults.
thread_local RuntimeWindowHostWin32* creatingHost = nullptr;
}
RuntimeWindowHostWin32::RuntimeWindowHostWin32(WNDPROC dispatch) noexcept : dispatch_(dispatch) {}
RuntimeWindowHostWin32::~RuntimeWindowHostWin32() {
    if (window_ != nullptr && IsWindow(window_)) DestroyWindow(window_);
}
RuntimeWindowCreateResult RuntimeWindowHostWin32::create(int width, int height) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"ConcordRTSRuntimeWindow";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = procedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    if (RegisterClassW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return RuntimeWindowCreateResult::RegistrationFailed;
    RECT windowRect{0, 0, width, height};
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);
    auto* previous = creatingHost;
    creatingHost = this;
    window_ = CreateWindowExW(0, className, L"ConcordRTS", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top, nullptr, nullptr, instance, nullptr);
    creatingHost = previous;
    return window_ != nullptr ? RuntimeWindowCreateResult::Created
        : RuntimeWindowCreateResult::CreationFailed;
}
void RuntimeWindowHostWin32::attachSession(LONG_PTR session) noexcept {
    SetWindowLongPtrW(window_, GWLP_USERDATA, session);
}
HWND RuntimeWindowHostWin32::window() const noexcept { return window_; }
bool RuntimeWindowHostWin32::drainMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) return false;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return true;
}
LRESULT CALLBACK RuntimeWindowHostWin32::procedure(HWND window, UINT message,
    WPARAM wParam, LPARAM lParam) {
    auto* host = reinterpret_cast<RuntimeWindowHostWin32*>(GetPropW(window, hostProperty));
    if (message == WM_NCCREATE && creatingHost != nullptr) {
        host = creatingHost;
        if (!SetPropW(window, hostProperty, host)) return FALSE;
    }
    const LRESULT result = host != nullptr
        ? host->dispatch_(window, message, wParam, lParam)
        : DefWindowProcW(window, message, wParam, lParam);
    if (message == WM_NCDESTROY && host != nullptr) {
        RemovePropW(window, hostProperty);
        host->window_ = nullptr;
    }
    return result;
}
