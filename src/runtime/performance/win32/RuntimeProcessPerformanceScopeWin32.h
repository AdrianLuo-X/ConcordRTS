#pragma once

// Private Windows assembly; never included by the standard policy header.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "runtime/performance/RuntimeProcessPerformanceScope.h"

static_assert(PROCESS_POWER_THROTTLING_CURRENT_VERSION == runtimeProcessPowerPolicyVersion);
static_assert(PROCESS_POWER_THROTTLING_EXECUTION_SPEED == runtimeProcessExecutionPolicy);
static_assert(PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION == runtimeProcessTimerPolicy);
static_assert(sizeof(ULONG) == sizeof(std::uint32_t));
static_assert(sizeof(PROCESS_POWER_THROTTLING_STATE) == sizeof(RuntimeProcessPowerPolicy));

class RuntimeWin32ProcessPerformancePort final : public RuntimeProcessPerformancePort {
public:
    RuntimeWin32ProcessPerformancePort() noexcept : api_(nativeApi()) {}
    bool available() const noexcept override { return api_.get != nullptr && api_.set != nullptr; }
    bool read(RuntimeProcessPowerPolicy& value) noexcept override {
        PROCESS_POWER_THROTTLING_STATE native{value.version, value.controlMask, value.stateMask};
        if (!api_.get(GetCurrentProcess(), ProcessPowerThrottling, &native, sizeof(native))) return false;
        value = {native.Version, native.ControlMask, native.StateMask};
        return true;
    }
    bool write(const RuntimeProcessPowerPolicy& value) noexcept override {
        PROCESS_POWER_THROTTLING_STATE native{value.version, value.controlMask, value.stateMask};
        return api_.set(GetCurrentProcess(), ProcessPowerThrottling, &native, sizeof(native)) != FALSE;
    }
    RuntimeProcessPolicyWriteError lastWriteError() const noexcept override {
        const DWORD error = GetLastError();
        if (error == ERROR_INVALID_PARAMETER) return RuntimeProcessPolicyWriteError::InvalidParameter;
        if (error == ERROR_NOT_SUPPORTED) return RuntimeProcessPolicyWriteError::NotSupported;
        return RuntimeProcessPolicyWriteError::Other;
    }
private:
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

    Api api_;
};

// Construction probes the original native API once per scope. Member order
// ensures the borrowed port remains alive through the policy destructor.
class RuntimeWin32ProcessPerformanceScope {
public:
    RuntimeWin32ProcessPerformanceScope() noexcept : scope_(port_) {}
    RuntimeWin32ProcessPerformanceScope(const RuntimeWin32ProcessPerformanceScope&) = delete;
    RuntimeWin32ProcessPerformanceScope& operator=(const RuntimeWin32ProcessPerformanceScope&) = delete;
    RuntimeWin32ProcessPerformanceScope(RuntimeWin32ProcessPerformanceScope&&) = delete;
    RuntimeWin32ProcessPerformanceScope& operator=(RuntimeWin32ProcessPerformanceScope&&) = delete;
    std::uint32_t appliedMask() const noexcept { return scope_.appliedMask(); }
    bool restore() noexcept { return scope_.restore(); }
private:
    RuntimeWin32ProcessPerformancePort port_;
    RuntimeProcessPerformanceScope scope_;
};
