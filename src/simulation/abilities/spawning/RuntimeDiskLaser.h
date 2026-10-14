#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// YR 1.001: DiskLaser initialization at 0x4A6CF0, AI at 0x4A7340.
// These are the original 240-lepton points, including its asymmetric last
// two angles (235 and 257 degrees).
inline constexpr std::array<std::array<int, 2>, 16> runtimeDiskLaserPoints{{
    {0, -240}, {89, -222}, {169, -169}, {220, -93},
    {240, 0}, {222, 89}, {169, 169}, {93, 220},
    {0, 240}, {-89, 222}, {-169, 169}, {-220, 93},
    {-240, 0}, {-222, -89}, {-137, -196}, {-54, -233}
}};
inline constexpr int runtimeDiskLaserChargeFrames = 16;

inline int runtimeDiskLaserFrontIndex(double dx, double dy) {
    constexpr double pi = 3.14159265358979323846;
    return (static_cast<int>(std::lround(std::atan2(dx, -dy) * 8.0 / pi)) + 16) & 15;
}

inline int runtimeDiskLaserAuthoredFrame(int ticks, int fixedTickHz) {
    return static_cast<int>(static_cast<long long>(std::max(0, ticks)) * 15
        / std::max(1, fixedTickHz));
}
