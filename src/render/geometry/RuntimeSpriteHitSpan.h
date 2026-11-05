#pragma once

#include <cstdint>

// Horizontal nontransparent pixel run. right is exclusive; rows are sorted by y/left.
struct RuntimeSpriteHitSpan {
    std::int32_t y = 0;
    std::int32_t left = 0;
    std::int32_t right = 0;
};
