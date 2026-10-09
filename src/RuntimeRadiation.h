#pragma once

#include <algorithm>
#include <cmath>

// Retail RadSite uses radial distance and (spread - distance) / spread.
// Share the footprint between damage, navigation and the projected tint.
inline double runtimeRadiationFalloff(double dx, double dy, double radius) {
    if (radius <= 0.0) return dx == 0.0 && dy == 0.0 ? 1.0 : 0.0;
    return std::clamp(1.0 - std::hypot(dx, dy) / radius, 0.0, 1.0);
}
