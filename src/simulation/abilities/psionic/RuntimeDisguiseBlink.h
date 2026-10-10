#pragma once

#include <algorithm>
#include <cstdint>

// Retail TechnoClass::ClearlyVisibleTo (0x70EE87): owners briefly see the
// disguise during a 256-rule-frame cycle. Use simulation time so pausing,
// game speed changes and saved disguise ages retain the same presentation.
inline bool runtimeOwnerDisguiseImageVisible(int tick, int startedTick, int fixedTickHz) {
    const auto age = std::max<std::int64_t>(0,
        static_cast<std::int64_t>(tick) - startedTick);
    const int phase = static_cast<int>((age * 15 / std::max(1, fixedTickHz) + 64) % 256);
    return phase >= 72 && phase <= 119;
}
