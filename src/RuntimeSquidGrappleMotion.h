#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// SQDG has an emergence (10 x 3 retail frames), then two ten-frame
// rocking strips (4 retail frames per pose). Keep the existing alternating
// strip selection deterministic; painting never consumes the gameplay RNG.
inline int runtimeSquidGrappleNativeAge(int age, int fixedTickHz) {
    return static_cast<int>(static_cast<std::int64_t>(std::max(0, age))
        * 15 / std::max(1, fixedTickHz));
}

struct RuntimeSquidGrapplePose {
    int action = 0;
    int frame = 0;
    double roll = 0.0;
};

inline RuntimeSquidGrapplePose runtimeSquidGrapplePose(int age, int fixedTickHz) {
    const int nativeAge = runtimeSquidGrappleNativeAge(age, fixedTickHz);
    if (nativeAge < 30) return {0, nativeAge / 3, 0.0};
    const int holdAge = nativeAge - 30;
    const int action = 1 + (holdAge / 40) % 2;
    const int frame = (holdAge % 40) / 4;
    // Retail 0x629A1A: sin(signed pose * pi/10) * float(pi/4), written
    // to the victim's AngleRotatedSideways. This is a hull-local roll.
    const double angle = static_cast<float>(std::sin(frame
        * (action == 1 ? -0.3141592653589793 : 0.3141592653589793))
        * 0.7853981852531433);
    return {action, frame, angle};
}
