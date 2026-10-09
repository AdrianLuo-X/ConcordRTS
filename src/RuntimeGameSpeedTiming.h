#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

// Retail GameOptions::GameSpeed is a wait count, the reverse of the UI slider.
// 0x55D79E stores it in the frame timer; 0x6C8C40 reads timeGetTime() >> 4.
// These are 16 ms clock quanta, not exact 1/60-second intervals. Zero means
// no frame wait. The user-selected faster pacing runs finite settings at 1.5x
// this retail baseline; authored durations still use 15 frames per second.
struct RuntimeGameSpeedTiming {
    int nativeWaitQuanta = 0;
    double secondsPerTick = 0.0;

    bool unlimited() const {
        return nativeWaitQuanta == 0;
    }

    double accumulate(double previous, double elapsed, int maximumTicks) const {
        if (unlimited()) return 0.0;
        return std::min(
            std::max(0.0, previous) + std::max(0.0, elapsed),
            secondsPerTick * std::max(0, maximumTicks)
        );
    }

    bool ready(double accumulated, int advanced, int maximumTicks) const {
        // Repeated subtraction must not lose the last tick of an exact
        // catch-up batch solely to a few binary floating-point ULPs. Account
        // for the larger local batch as well as the network's smaller budget.
        const double roundoff = secondsPerTick
            * (16.0 * std::numeric_limits<double>::epsilon()
                * std::max(1, maximumTicks));
        return advanced < maximumTicks
            && (unlimited() || accumulated + roundoff >= secondsPerTick);
    }

    double consume(double accumulated, int ticks = 1) const {
        return unlimited() ? 0.0 : std::max(
            0.0, accumulated - secondsPerTick * std::max(0, ticks)
        );
    }

    // Diagnostic only: the actual scheduler must use secondsPerTick so that
    // rounding, for example 93.75 to 94 Hz, cannot alter simulation speed.
    int roundedTickRate() const {
        if (unlimited()) return 0;
        return static_cast<int>(std::min(
            std::floor(1.0 / secondsPerTick + 0.5),
            static_cast<double>(std::numeric_limits<int>::max())
        ));
    }
};

inline RuntimeGameSpeedTiming runtimeGameSpeedTiming(int slider, int fixedTickHz) {
    const int waitQuanta = 6 - std::clamp(slider, 0, 6);
    constexpr double pacingMultiplier = 1.5;
    return {
        waitQuanta,
        static_cast<double>(waitQuanta) * 0.016 * 15.0
            / static_cast<double>(std::max(1, fixedTickHz)) / pacingMultiplier
    };
}
