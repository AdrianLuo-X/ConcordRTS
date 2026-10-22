#include "ui/time/RuntimeUiClock.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(WINAPI)
#error UI clock header leaked Windows before the private adapter
#endif
#include "runtime/host/RuntimeUiClockWin32.h"
#if !defined(_WIN32)
#error This reference requires the real production Windows clock provider
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>

static_assert(std::is_same_v<decltype(runtimeWin32UiClock()), const RuntimeUiClock&>);
static_assert(noexcept(runtimeWin32UiClock()));
static_assert(noexcept(std::declval<const RuntimeUiClock&>().nowMs()));
static_assert(sizeof(RuntimeUiTimeMs) == sizeof(ULONGLONG));

namespace {
std::size_t checks = 0;
bool expect(bool condition, const char* message) {
    ++checks;
    if (!condition) std::cerr << "UI clock Win32 reference failed: " << message << '\n';
    return condition;
}
} // namespace

int main() {
    const RuntimeUiClock& source = runtimeWin32UiClock();
    bool ok = expect(std::addressof(source) == std::addressof(runtimeWin32UiClock()),
        "stateless production provider reference must have stable lifetime");
    constexpr std::size_t brackets = 128;
    for (std::size_t i = 0; i < brackets; ++i) {
        const auto before = GetTickCount64();
        const RuntimeUiTimeMs actual = source.nowMs();
        const auto after = GetTickCount64();
        ok &= expect(actual >= before && actual <= after,
            "actual production provider must retain GetTickCount64 epoch and millisecond units");
    }
    if (!ok) return 1;
    std::cout << "UI clock Win32 reference passed: checks=" << checks
        << " sdkBrackets=" << brackets
        << " epoch=GetTickCount64 units=milliseconds stableProvider=1 shellIntegration=not-covered\n";
    return 0;
}

