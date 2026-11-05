#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// A silent or occluded Windows 11 window can otherwise lose both HighQoS and
// its requested timer resolution. This owns only the current process's policy
// during a game session; it never changes priority, affinity or a power plan.
// https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation
class RuntimeProcessPerformanceScope {
public:
    using InformationFunction = BOOL (WINAPI*)(
        HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
    struct Api {
        InformationFunction get = nullptr;
        InformationFunction set = nullptr;
    };

    static Api nativeApi() noexcept {
        const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        if (kernel == nullptr) return {};
        return {
            reinterpret_cast<InformationFunction>(GetProcAddress(kernel, "GetProcessInformation")),
            reinterpret_cast<InformationFunction>(GetProcAddress(kernel, "SetProcessInformation"))
        };
    }

    RuntimeProcessPerformanceScope() noexcept : RuntimeProcessPerformanceScope(nativeApi()) {}
    explicit RuntimeProcessPerformanceScope(Api api) noexcept : api_(api) {
        if (api_.get == nullptr || api_.set == nullptr) return;
        previous_.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        if (!api_.get(GetCurrentProcess(), ProcessPowerThrottling, &previous_, sizeof(previous_))
            || previous_.Version != PROCESS_POWER_THROTTLING_CURRENT_VERSION) return;

        constexpr ULONG execution = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        constexpr ULONG timers = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        if (apply(execution | timers)) return;
        const DWORD error = GetLastError();
        // Windows versions predating timer-resolution policy may still support
        // explicit HighQoS. Other failures leave the process policy untouched.
        if (error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_SUPPORTED)
            (void)apply(execution);
    }

    ~RuntimeProcessPerformanceScope() noexcept { (void)restore(); }
    RuntimeProcessPerformanceScope(const RuntimeProcessPerformanceScope&) = delete;
    RuntimeProcessPerformanceScope& operator=(const RuntimeProcessPerformanceScope&) = delete;
    RuntimeProcessPerformanceScope(RuntimeProcessPerformanceScope&&) = delete;
    RuntimeProcessPerformanceScope& operator=(RuntimeProcessPerformanceScope&&) = delete;

    ULONG appliedMask() const noexcept { return appliedMask_; }
    bool restore() noexcept {
        if (!changed_) return true;
        if (!api_.set(GetCurrentProcess(), ProcessPowerThrottling, &previous_, sizeof(previous_)))
            return false;
        changed_ = false;
        appliedMask_ = 0;
        return true;
    }

private:
    bool apply(ULONG mask) noexcept {
        auto requested = previous_;
        requested.ControlMask |= mask;
        requested.StateMask &= ~mask;
        const bool differs = requested.ControlMask != previous_.ControlMask
            || requested.StateMask != previous_.StateMask;
        if (differs && !api_.set(GetCurrentProcess(), ProcessPowerThrottling,
                &requested, sizeof(requested))) return false;
        changed_ = differs;
        appliedMask_ = mask;
        return true;
    }

    Api api_;
    PROCESS_POWER_THROTTLING_STATE previous_{};
    ULONG appliedMask_ = 0;
    bool changed_ = false;
};
