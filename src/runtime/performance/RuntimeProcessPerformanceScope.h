#pragma once

#include <cstdint>

// Current-process, session-local optional policy. Values are owned; the port
// is borrowed and must outlive the scope. No priority, affinity or power plan.
struct RuntimeProcessPowerPolicy {
    std::uint32_t version = 0;
    std::uint32_t controlMask = 0;
    std::uint32_t stateMask = 0;
    bool operator==(const RuntimeProcessPowerPolicy&) const = default;
};
inline constexpr std::uint32_t runtimeProcessPowerPolicyVersion = 1;
inline constexpr std::uint32_t runtimeProcessExecutionPolicy = 1;
inline constexpr std::uint32_t runtimeProcessTimerPolicy = 4;

enum class RuntimeProcessPolicyWriteError { Other, InvalidParameter, NotSupported };

class RuntimeProcessPerformancePort {
public:
    virtual ~RuntimeProcessPerformancePort() = default;
    virtual bool available() const noexcept = 0;
    virtual bool read(RuntimeProcessPowerPolicy& value) noexcept = 0;
    virtual bool write(const RuntimeProcessPowerPolicy& value) noexcept = 0;
    // Queried only after the first failed apply, never after a failed restore.
    virtual RuntimeProcessPolicyWriteError lastWriteError() const noexcept = 0;
};

class RuntimeProcessPerformanceScope {
public:
    explicit RuntimeProcessPerformanceScope(RuntimeProcessPerformancePort& api) noexcept : api_(api) {
        if (!api_.available()) return;
        previous_.version = runtimeProcessPowerPolicyVersion;
        if (!api_.read(previous_) || previous_.version != runtimeProcessPowerPolicyVersion) return;

        constexpr std::uint32_t execution = runtimeProcessExecutionPolicy;
        constexpr std::uint32_t timers = runtimeProcessTimerPolicy;
        if (apply(execution | timers)) return;
        const auto error = api_.lastWriteError();
        if (error == RuntimeProcessPolicyWriteError::InvalidParameter
            || error == RuntimeProcessPolicyWriteError::NotSupported)
            (void)apply(execution);
    }

    ~RuntimeProcessPerformanceScope() noexcept { (void)restore(); }
    RuntimeProcessPerformanceScope(const RuntimeProcessPerformanceScope&) = delete;
    RuntimeProcessPerformanceScope& operator=(const RuntimeProcessPerformanceScope&) = delete;
    RuntimeProcessPerformanceScope(RuntimeProcessPerformanceScope&&) = delete;
    RuntimeProcessPerformanceScope& operator=(RuntimeProcessPerformanceScope&&) = delete;

    std::uint32_t appliedMask() const noexcept { return appliedMask_; }
    bool restore() noexcept {
        if (!changed_) return true;
        if (!api_.write(previous_)) return false;
        changed_ = false;
        appliedMask_ = 0;
        return true;
    }

private:
    bool apply(std::uint32_t mask) noexcept {
        auto requested = previous_;
        requested.controlMask |= mask;
        requested.stateMask &= ~mask;
        const bool differs = requested.controlMask != previous_.controlMask
            || requested.stateMask != previous_.stateMask;
        if (differs && !api_.write(requested)) return false;
        changed_ = differs;
        appliedMask_ = mask;
        return true;
    }

    RuntimeProcessPerformancePort& api_;
    RuntimeProcessPowerPolicy previous_{};
    std::uint32_t appliedMask_ = 0;
    bool changed_ = false;
};
