#pragma once
#include <array>

struct RuntimeVoxelDebrisState {
    bool enabled = false;
    int framesLeft = 0;
    int frameRemainder = 0;
    int frameCount = 0;
    int bounces = 0;
    double elasticity = 0.8;
    std::array<float, 3> position{};
    std::array<float, 3> velocity{};
    std::array<float, 4> orientation{0, 0, 0, 1};
    std::array<float, 4> spin{0, 0, 0, 1};
};
