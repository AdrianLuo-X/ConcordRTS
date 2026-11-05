#pragma once

#include <cstdint>

// Synchronous session-local request; the instance port outlives its scope.
// End is attempted once for a successful begin. A failed end is reported,
// retained by repeated close(), and never retried implicitly.
class RuntimeTimerResolutionPort {
public:
    virtual ~RuntimeTimerResolutionPort() = default;
    virtual bool begin(std::uint32_t periodMilliseconds) noexcept = 0;
    virtual bool end(std::uint32_t periodMilliseconds) noexcept = 0;
};

class RuntimeTimerResolutionScope {
public:
    explicit RuntimeTimerResolutionScope(RuntimeTimerResolutionPort& port,
        std::uint32_t periodMilliseconds = 1) noexcept
        : port_(port), period_(periodMilliseconds), began_(port_.begin(period_)), endPending_(began_) {}
    ~RuntimeTimerResolutionScope() noexcept { (void)close(); }
    RuntimeTimerResolutionScope(const RuntimeTimerResolutionScope&) = delete;
    RuntimeTimerResolutionScope& operator=(const RuntimeTimerResolutionScope&) = delete;
    RuntimeTimerResolutionScope(RuntimeTimerResolutionScope&&) = delete;
    RuntimeTimerResolutionScope& operator=(RuntimeTimerResolutionScope&&) = delete;

    bool beginSucceeded() const noexcept { return began_; }
    bool endPending() const noexcept { return endPending_; }
    bool endAttempted() const noexcept { return began_ && !endPending_; }
    bool close() noexcept {
        if (!endPending_) return endSucceeded_;
        endPending_ = false;
        endSucceeded_ = port_.end(period_);
        return endSucceeded_;
    }
private:
    RuntimeTimerResolutionPort& port_;
    const std::uint32_t period_;
    const bool began_;
    bool endPending_;
    bool endSucceeded_ = true;
};
