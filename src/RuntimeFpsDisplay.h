#pragma once

#include <cmath>
#include <cstdint>

// Application-window submissions, measured independently of simulation ticks
// and scene redraws. The first successful submission establishes the clock;
// subsequent submissions count the completed frame intervals in each sample.
class RuntimeFpsDisplayMeter {
public:
    void reset() noexcept {
        started_ = false;
        sampled_ = false;
        sampleStart_ = 0.0;
        lastPresentation_ = 0.0;
        framesInWindow_ = 0;
        framesPerSecond_ = 0.0;
    }

    void recordPresentedFrame(double monotonicSeconds) noexcept {
        if (!std::isfinite(monotonicSeconds)) {
            reset();
            return;
        }
        if (!started_ || monotonicSeconds < lastPresentation_) {
            reset();
            started_ = true;
            sampleStart_ = monotonicSeconds;
            lastPresentation_ = monotonicSeconds;
            return;
        }
        lastPresentation_ = monotonicSeconds;
        ++framesInWindow_;
        const double elapsed = monotonicSeconds - sampleStart_;
        if (elapsed >= 1.0) {
            framesPerSecond_ = static_cast<double>(framesInWindow_) / elapsed;
            sampled_ = true;
            sampleStart_ = monotonicSeconds;
            framesInWindow_ = 0;
        }
    }

    bool hasSample() const noexcept { return sampled_; }
    double framesPerSecond() const noexcept { return framesPerSecond_; }

private:
    bool started_ = false;
    bool sampled_ = false;
    double sampleStart_ = 0.0;
    double lastPresentation_ = 0.0;
    std::uint64_t framesInWindow_ = 0;
    double framesPerSecond_ = 0.0;
};

inline bool runtimeFpsDisplayShouldCountFrame(
    bool visible,
    bool minimized,
    bool hasClientArea,
    bool hasPaintArea,
    bool submissionSucceeded
) noexcept {
    return visible && !minimized && hasClientArea && hasPaintArea
        && submissionSucceeded;
}
