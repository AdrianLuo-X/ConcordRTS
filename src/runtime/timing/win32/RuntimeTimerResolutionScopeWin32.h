#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include "runtime/timing/RuntimeTimerResolutionScope.h"

static_assert(sizeof(UINT) == sizeof(std::uint32_t));
// Private, read-only diagnostic values. No timer measurement or global owner.
struct RuntimeWin32TimerResolutionDiagnostics {
    std::uint32_t periodMilliseconds = 0;
    std::uint32_t beginCalls = 0;
    std::uint32_t endCalls = 0;
    std::uint32_t beginResult = 0xffffffffu;
    std::uint32_t endResult = 0xffffffffu;
};
class RuntimeWin32TimerResolutionPort final : public RuntimeTimerResolutionPort {
public:
    bool begin(std::uint32_t period) noexcept override {
        ++diagnostics_.beginCalls;
        diagnostics_.periodMilliseconds = period;
        const auto result = timeBeginPeriod(static_cast<UINT>(period));
        diagnostics_.beginResult = result;
        return result == TIMERR_NOERROR;
    }
    bool end(std::uint32_t period) noexcept override {
        ++diagnostics_.endCalls;
        const auto result = timeEndPeriod(static_cast<UINT>(period));
        diagnostics_.endResult = result;
        return result == TIMERR_NOERROR;
    }
    const RuntimeWin32TimerResolutionDiagnostics& diagnostics() const noexcept { return diagnostics_; }
private:
    RuntimeWin32TimerResolutionDiagnostics diagnostics_;
};
class RuntimeWin32TimerResolutionScope {
public:
    explicit RuntimeWin32TimerResolutionScope(std::uint32_t period = 1) noexcept : scope_(port_, period) {}
    RuntimeWin32TimerResolutionScope(const RuntimeWin32TimerResolutionScope&) = delete;
    RuntimeWin32TimerResolutionScope& operator=(const RuntimeWin32TimerResolutionScope&) = delete;
    RuntimeWin32TimerResolutionScope(RuntimeWin32TimerResolutionScope&&) = delete;
    RuntimeWin32TimerResolutionScope& operator=(RuntimeWin32TimerResolutionScope&&) = delete;
    bool beginSucceeded() const noexcept { return scope_.beginSucceeded(); }
    bool endPending() const noexcept { return scope_.endPending(); }
    bool close() noexcept { return scope_.close(); }
    const RuntimeWin32TimerResolutionDiagnostics& diagnostics() const noexcept { return port_.diagnostics(); }
private:
    RuntimeWin32TimerResolutionPort port_;
    RuntimeTimerResolutionScope scope_;
};
