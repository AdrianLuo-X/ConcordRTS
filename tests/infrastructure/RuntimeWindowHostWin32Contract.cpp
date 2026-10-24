#include "runtime/window/win32/RuntimeWindowHostWin32.h"
#include <iostream>
#include <string>
#include <vector>

namespace {
struct NativePort final : RuntimeWindowLifecyclePort {
    HWND window = nullptr;
    std::vector<std::string> trace;
    void resetFrameMeter() override { trace.push_back("meter"); }
    void reportDeparture() override { trace.push_back("departure"); }
    void destroyWindow() override { trace.push_back("destroy-enter"); DestroyWindow(window); trace.push_back("destroy-return"); }
    void stopEva() override { trace.push_back("eva"); }
    void stopMusic() override { trace.push_back("music"); }
    void releaseGraphics() override { trace.push_back("graphics"); }
    void postQuit() override { trace.push_back("quit"); PostQuitMessage(0); }
};
NativePort* current = nullptr;
bool rejectCreate = false;
bool sawUnattachedCreate = false;
bool sawResize = false;
LRESULT CALLBACK dispatch(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_NCCREATE) {
        sawUnattachedCreate = GetWindowLongPtrW(w, GWLP_USERDATA) == 0;
        if (rejectCreate) return FALSE;
    }
    const bool attached = GetWindowLongPtrW(w, GWLP_USERDATA) != 0;
    if (m == WM_SIZE) {
        sawResize = true;
        if (wp == SIZE_MINIMIZED) handleRuntimeWindowLifecycle(RuntimeWindowLifecycleEvent::Minimized, attached, *current);
    }
    if (m == WM_SHOWWINDOW && wp == FALSE)
        handleRuntimeWindowLifecycle(RuntimeWindowLifecycleEvent::Hidden, attached, *current);
    if (m == WM_CLOSE || m == WM_DESTROY) {
        current->window = w;
        handleRuntimeWindowLifecycle(m == WM_CLOSE ? RuntimeWindowLifecycleEvent::Close
            : RuntimeWindowLifecycleEvent::Destroyed, attached, *current);
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}
}
int main() {
    int checks = 0; bool ok = true;
    auto check = [&](bool value) { ++checks; if (!value) { ok = false; std::cerr << "failed " << checks << '\n'; } };
    for (int repeat = 0; repeat != 3; ++repeat) {
        NativePort port; current = &port;
        RuntimeWindowHostWin32 host(dispatch);
        check(host.create(360, 240) == RuntimeWindowCreateResult::Created);
        HWND w = host.window(); check(w != nullptr && IsWindow(w));
        check(sawUnattachedCreate);
        host.attachSession(1);
        check(GetWindowLongPtrW(w, GWLP_USERDATA) == 1);
        RECT rc{}; GetClientRect(w, &rc); check(rc.right == 360 && rc.bottom == 240);
        ShowWindow(w, SW_SHOWNOACTIVATE);
        SetWindowPos(w, nullptr, 0, 0, 480, 320, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        check(sawResize); check(host.drainMessages());
        port.trace.clear();
        ShowWindow(w, SW_MAXIMIZE);
        check(IsZoomed(w) != FALSE);
        ShowWindow(w, SW_RESTORE);
        check(IsZoomed(w) == FALSE);
        ShowWindow(w, SW_SHOWMINNOACTIVE);
        check(IsIconic(w) != FALSE);
        check(!port.trace.empty() && port.trace.back() == "meter");
        ShowWindow(w, SW_RESTORE);
        check(IsIconic(w) == FALSE);
        port.trace.clear(); SendMessageW(w, WM_SIZE, SIZE_MINIMIZED, 0);
        check(port.trace == std::vector<std::string>{"meter"});
        ShowWindow(w, SW_HIDE);
        port.trace.clear(); SendMessageW(w, WM_CLOSE, 0, 0);
        check(port.trace == std::vector<std::string>{"departure", "destroy-enter", "eva", "music", "graphics", "quit", "destroy-return"});
        check(!IsWindow(w)); check(host.window() == nullptr); check(!host.drainMessages());
        check(host.drainMessages());
    }
    // The owner must also clean up an open window on scope exit, after the
    // original synchronous device/graphics teardown and without a departure.
    NativePort scoped; current = &scoped;
    {
        RuntimeWindowHostWin32 scopedHost(dispatch);
        check(scopedHost.create(360, 240) == RuntimeWindowCreateResult::Created);
        scoped.window = scopedHost.window();
        scopedHost.attachSession(1);
    }
    check(!IsWindow(scoped.window));
    check(scoped.trace == std::vector<std::string>{"eva", "music", "graphics", "quit"});
    RuntimeWindowHostWin32 threadDrain(dispatch);
    check(!threadDrain.drainMessages());
    NativePort failure; current = &failure; rejectCreate = true;
    RuntimeWindowHostWin32 rejected(dispatch);
    check(rejected.create(360, 240) == RuntimeWindowCreateResult::CreationFailed);
    check(rejected.window() == nullptr);
    // An unbound native create failure must not invent session teardown.
    check(failure.trace.empty());
    std::cout << "Real Win32 lifetime: " << checks << " checks; create/null-userdata, repeat, resize, native close/reentry and rejected create\n";
    return ok ? 0 : 1;
}
