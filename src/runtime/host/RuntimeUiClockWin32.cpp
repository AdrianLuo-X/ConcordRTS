#include "runtime/host/RuntimeUiClockWin32.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <new>

namespace {
class RuntimeWin32UiClock final : public RuntimeUiClock {
public:
    RuntimeUiTimeMs nowMs() const noexcept override {
        return GetTickCount64();
    }
};
} // namespace

const RuntimeUiClock& runtimeWin32UiClock() noexcept {
    // Global prewarmed menu owners are destroyed after function-local statics.
    // This resource-free provider must remain alive throughout that teardown.
    // Fixed static storage avoids a destructor registration and any heap use;
    // the const pointer's initialization retains the language's once-only guard.
    alignas(RuntimeWin32UiClock) static unsigned char storage[sizeof(RuntimeWin32UiClock)]{};
    static const RuntimeWin32UiClock* const clock =
        ::new (static_cast<void*>(storage)) RuntimeWin32UiClock;
    return *clock;
}

