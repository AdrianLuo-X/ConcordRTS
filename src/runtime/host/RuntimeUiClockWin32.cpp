#include "runtime/host/RuntimeUiClockWin32.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {
class RuntimeWin32UiClock final : public RuntimeUiClock {
public:
    RuntimeUiTimeMs nowMs() const noexcept override {
        return GetTickCount64();
    }
};
} // namespace

const RuntimeUiClock& runtimeWin32UiClock() noexcept {
    static const RuntimeWin32UiClock clock;
    return clock;
}

