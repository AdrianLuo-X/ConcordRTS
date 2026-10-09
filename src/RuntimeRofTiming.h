#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

// Retail GetROF uses x87 with the CRT control word 0x0E7F: double precision,
// toward-zero rounding. A nearest-rounded product such as 5 * binary64(0.6)
// can become 3 before the integer cast, whereas retail returns 2. The fused
// residual identifies an upward-rounded product without changing the host's
// process-wide floating-point environment.
inline int runtimeRofScaledFrames(int frames, double multiplier) {
    const double count = static_cast<double>(std::max(0, frames));
    if (!std::isfinite(multiplier)) multiplier = 1.0;
    multiplier = std::max(0.0, multiplier);
    double product = count * multiplier;
    if (product >= static_cast<double>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    if (std::fma(count, multiplier, -product) < 0.0)
        product = std::nextafter(product, 0.0);
    return static_cast<int>(product);
}
