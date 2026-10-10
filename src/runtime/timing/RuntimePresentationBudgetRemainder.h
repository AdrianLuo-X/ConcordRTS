#pragma once

#include "runtime/timing/RuntimePresentationWorkBudget.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// Presentation scheduler state, never simulation authority.
// Keep original readiness/credit/consume/catch-up logic outside this helper.
inline thread_local int runtimePresentationRemainderReferenceOverride = -1;
inline bool runtimePresentationRemainderReference() {
    if (runtimePresentationRemainderReferenceOverride >= 0)
        return runtimePresentationRemainderReferenceOverride != 0;
    static const bool reference = [] {
        const char* flag = std::getenv("CONCORD_PRESENTATION_REMAINDER_REFERENCE");
        return flag && flag[0] == '1' && flag[1] == '\0';
    }();
    return reference;
}
class RuntimePresentationRemainderReferenceScope {
    int previous_;
public:
    explicit RuntimePresentationRemainderReferenceScope(bool reference)
        : previous_(runtimePresentationRemainderReferenceOverride) {
        runtimePresentationRemainderReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimePresentationRemainderReferenceScope() {
        runtimePresentationRemainderReferenceOverride = previous_;
    }
    RuntimePresentationRemainderReferenceScope(const RuntimePresentationRemainderReferenceScope&) = delete;
    RuntimePresentationRemainderReferenceScope& operator=(const RuntimePresentationRemainderReferenceScope&) = delete;
};

// ONE instance lives in the local outer main loop. Each begin starts one
// bounded simulation service batch. The caller resets for modal/movie/input
// clock discontinuities, network, minimized state and reference mode.
// Extra service means all REQUIRED non-simulation/non-paint service since the
// prior actual presentation; exclude modal/movie/pause/idle pacing waits.
// An estimate cannot give an absolute TPS guarantee under unknown future work.
class RuntimePresentationBudgetRemainder {
public:
    void reset() noexcept {
        remainder_ = 0.0;
        enabled_ = false;
        haveContext_ = false;
        recorded_ = false;
    }

    void begin(const RuntimePresentationWorkBudget& original, double frameSeconds,
        double previousPaintMs, double extraServiceMs, double simulationMs,
        double requestedTickHz, bool eligibleLocalFinite, bool unlimited,
        int modeToken = 0) noexcept {
        originalSeconds_ = original.seconds;
        targetSeconds_ = original.seconds;
        recorded_ = false;
        enabled_ = false;
        const bool valid = std::isfinite(frameSeconds) && frameSeconds > 0.0
            && std::isfinite(original.seconds) && original.seconds > 0.0
            && std::isfinite(previousPaintMs) && previousPaintMs >= 0.0
            && std::isfinite(extraServiceMs) && extraServiceMs >= 0.0
            && std::isfinite(simulationMs) && simulationMs > 0.0
            && std::isfinite(requestedTickHz) && requestedTickHz > 0.0;
        if (!valid || !eligibleLocalFinite || unlimited || runtimePresentationRemainderReference()) {
            reset();
            originalSeconds_ = original.seconds;
            targetSeconds_ = original.seconds;
            return;
        }
        const double tickSeconds = simulationMs / 1000.0;
        const double share = tickSeconds * requestedTickHz;
        if (!std::isfinite(share) || share > 0.8) {
            reset();
            return;
        }
        const double serviceSeconds = (previousPaintMs + extraServiceMs) / 1000.0;
        const double cadence = serviceSeconds * share / (1.0 - share);
        // If even the estimated finite cadence needs more than the original
        // two-frame service ceiling, preserve old amortization and throughput.
        // Do not reduce overloaded slow-paint throughput by fractional rounding.
        if (!std::isfinite(cadence) || cadence > frameSeconds * 2.0) {
            reset();
            return;
        }
        const double target = std::max(original.seconds, cadence);
        if (!haveContext_ || frame_ != frameSeconds || requestedHz_ != requestedTickHz
            || modeToken_ != modeToken || std::abs(target - priorTarget_) > frameSeconds * 0.25)
            remainder_ = 0.0;
        haveContext_ = true;
        enabled_ = true;
        frame_ = frameSeconds;
        requestedHz_ = requestedTickHz;
        modeToken_ = modeToken;
        priorTarget_ = target;
        targetSeconds_ = target;
        nextTickSeconds_ = tickSeconds;
    }

    bool shouldYield(int completedTicks, double elapsedSeconds, bool paintDue) noexcept {
        if (runtimePresentationRemainderReference()) {
            reset();
            return originalYield(completedTicks, elapsedSeconds, paintDue);
        }
        if (!enabled_) return originalYield(completedTicks, elapsedSeconds, paintDue);
        return completedTicks > 0 && paintDue && std::isfinite(elapsedSeconds)
            && elapsedSeconds >= 0.0
            && elapsedSeconds + nextTickSeconds_ >= targetSeconds_ + remainder_;
    }

    // Call ONLY after shouldYield returned true and the caller will actually
    // break for this budget. A ready/benchmark/catch-up exit never records debt.
    // One batch can commit exactly once, even if a diagnostic asks twice.
    void recordActualBudgetYield(double elapsedSeconds) noexcept {
        if (!enabled_ || recorded_ || runtimePresentationRemainderReference()
            || !std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0) return;
        remainder_ = std::clamp(remainder_ + targetSeconds_ - elapsedSeconds,
            -2.0 * frame_, 2.0 * frame_);
        recorded_ = true;
    }

    bool enabled() const noexcept { return enabled_; }
    double carriedRemainderSeconds() const noexcept { return remainder_; }
    double targetSeconds() const noexcept { return targetSeconds_; }

private:
    bool originalYield(int ticks, double elapsed, bool due) const noexcept {
        return ticks > 0 && due && std::isfinite(elapsed) && elapsed >= originalSeconds_;
    }
    double remainder_ = 0.0, frame_ = 1.0 / 60.0;
    double requestedHz_ = 0.0, priorTarget_ = 0.0;
    double originalSeconds_ = 1.0 / 60.0, targetSeconds_ = 1.0 / 60.0;
    double nextTickSeconds_ = 0.0;
    int modeToken_ = 0;
    bool enabled_ = false, haveContext_ = false, recorded_ = false;
};
