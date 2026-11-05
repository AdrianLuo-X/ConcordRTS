#include "runtime/window/win32/RuntimePersistentMenuHostWin32.h"
#include "runtime/config/RuntimeConfig.h"
#include <algorithm>
namespace {
constexpr const wchar_t* runtimeMenuHostClassName =
    L"ConcordRTS.RuntimeMenuHost";

LRESULT CALLBACK runtimeMenuHostWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    switch (message) {
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(window, &client);
        FillRect(
            reinterpret_cast<HDC>(wParam),
            &client,
            static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))
        );
        return 1;
    }
    case WM_SIZE: {
        const int width = std::max(1, static_cast<int>(LOWORD(lParam)));
        const int height = std::max(1, static_cast<int>(HIWORD(lParam)));
        for (HWND child = GetWindow(window, GW_CHILD);
            child != nullptr;
            child = GetWindow(child, GW_HWNDNEXT)) {
            MoveWindow(child, 0, 0, width, height, TRUE);
        }
        return 0;
    }
    case WM_SETFOCUS:
        for (HWND child = GetWindow(window, GW_CHILD);
            child != nullptr;
            child = GetWindow(child, GW_HWNDNEXT)) {
            if (IsWindowVisible(child)) {
                SetFocus(child);
                break;
            }
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

bool registerRuntimeMenuHostClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = RuntimePersistentMenuHostWin32::procedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground =
        static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = runtimeMenuHostClassName;
    if (RegisterClassExW(&windowClass) != 0) {
        return true;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND createRuntimeMenuHostWindow(const RuntimeConfig& config, RuntimePersistentMenuHostWin32* owner) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!registerRuntimeMenuHostClass(instance)) {
        return nullptr;
    }

    const int clientWidth = std::max(
        800,
        config.windowWidth > 0 ? config.windowWidth : 800
    );
    const int clientHeight = std::max(
        600,
        config.windowHeight > 0 ? config.windowHeight : 600
    );
    const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    RECT windowRect{0, 0, clientWidth, clientHeight};
    AdjustWindowRectEx(&windowRect, style, FALSE, 0);
    int width = windowRect.right - windowRect.left;
    int height = windowRect.bottom - windowRect.top;
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    width = std::min(width, static_cast<int>(workArea.right - workArea.left));
    height = std::min(
        height,
        static_cast<int>(workArea.bottom - workArea.top)
    );
    const int x = workArea.left
        + (static_cast<int>(workArea.right - workArea.left) - width) / 2;
    const int y = workArea.top
        + (static_cast<int>(workArea.bottom - workArea.top) - height) / 2;
    return CreateWindowExW(
        0,
        runtimeMenuHostClassName,
        L"Command & Conquer: Yuri's Revenge",
        style,
        x,
        y,
        width,
        height,
        nullptr,
        nullptr,
        instance,
        owner
    );
}

} // namespace

RuntimePersistentMenuHostWin32::~RuntimePersistentMenuHostWin32() { close(); }
bool RuntimePersistentMenuHostWin32::create(const RuntimeConfig& config) {
    window_ = createRuntimeMenuHostWindow(config,this);
    return valid();
}
bool RuntimePersistentMenuHostWin32::valid() const {
    return window_ != nullptr && IsWindow(window_);
}
void RuntimePersistentMenuHostWin32::present() {
    ShowWindow(window_,SW_SHOW);
    UpdateWindow(window_);
    SetForegroundWindow(window_);
}
void RuntimePersistentMenuHostWin32::acquireParentBorrow() {
    parentBorrow_.emplace(window_);
}
RuntimeWindowId RuntimePersistentMenuHostWin32::parentId() const {
    return parentBorrow_ ? parentBorrow_->id() : RuntimeWindowId{};
}
void RuntimePersistentMenuHostWin32::revokeParentBorrow() noexcept {
    parentBorrow_.reset();
}
void RuntimePersistentMenuHostWin32::destroyParentWindow() noexcept {
    if(valid())DestroyWindow(window_);
    window_=nullptr;
}
void RuntimePersistentMenuHostWin32::close() noexcept {
    closeRuntimeMenuParent(*this);
}
LRESULT CALLBACK RuntimePersistentMenuHostWin32::procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* owner=reinterpret_cast<RuntimePersistentMenuHostWin32*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        const auto* creation=reinterpret_cast<const CREATESTRUCTW*>(lParam);
        owner=static_cast<RuntimePersistentMenuHostWin32*>(creation->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(owner));
    }
    // Explicit close revokes before DestroyWindow. Native close/direct parent
    // destruction also revoke before the original child-destroy callbacks.
    if(owner&&(message==WM_CLOSE||message==WM_DESTROY))owner->revokeParentBorrow();
    const LRESULT result=runtimeMenuHostWindowProc(window,message,wParam,lParam);
    if(owner&&message==WM_NCDESTROY) {
        owner->window_=nullptr;
        SetWindowLongPtrW(window,GWLP_USERDATA,0);
    }
    return result;
}
