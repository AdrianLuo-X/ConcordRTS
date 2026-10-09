#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

struct RuntimeBuildupTiming {
    int frames = 0;
    int legacyFrameDelay = 0;
    int legacyTicks = 1;
    int fixedTicks = 2;
};

inline int runtimeBuildupFrameCount(int rawFrames, bool gate, int gateStages) {
    // Retail BuildingType 0x45F2B4 / 0x465A53 halves the SHP count, even
    // when drawing shadows is disabled. Gates use GateStages + 1 instead.
    const int available = std::max(0, rawFrames / 2);
    if (!gate || available == 0) return available;
    return static_cast<int>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(gateStages) + 1, 1, available));
}

inline RuntimeBuildupTiming runtimeBuildupTiming(double minutes, int frames, int fixedTickHz) {
    RuntimeBuildupTiming result;
    result.frames = std::max(0, frames);
    // Retail Rules constructor: 0.05. Stock rulesmd.ini overrides it to 0.06.
    if (!std::isfinite(minutes) || minutes < 0.0 || minutes > 100.0) minutes = 0.05;
    if (result.frames > 1) {
        result.legacyFrameDelay = static_cast<int>(minutes * 900.0 / result.frames);
        // Keep the terminal SHP frame for its full interval. Stock turret
        // buildups use it to settle their barrels into the completed pose;
        // ending on entry to that frame used to skip it entirely.
        // Zero rates and absent/degenerate art complete at the next boundary.
        result.legacyTicks = std::max(1, result.frames * result.legacyFrameDelay);
    }
    const std::int64_t scaled = static_cast<std::int64_t>(result.legacyTicks)
        * std::max(1, fixedTickHz);
    result.fixedTicks = static_cast<int>(std::min<std::int64_t>(
        (scaled + 14) / 15, std::numeric_limits<int>::max()));
    return result;
}

inline int runtimeBuildupFrameAtProgress(int frames, int total, int remaining,
    int fixedTickHz = 15, bool gate = false) {
    if (frames <= 1 || total <= 0) return 0;
    const bool zeroRateBoundary = remaining > 0
        && total <= (static_cast<std::int64_t>(std::max(1,fixedTickHz))+14)/15;
    const std::int64_t elapsed = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(total) - remaining, 0, total);
    const int stage = zeroRateBoundary ? 0
        : std::min(frames - 1, static_cast<int>(elapsed * frames / total));
    // BuildingClass::GetCurrentFrame, retail 0x43EFD0: construction gates
    // traverse their authored stages in the opposite direction.
    return gate ? frames - 1 - stage : stage;
}
