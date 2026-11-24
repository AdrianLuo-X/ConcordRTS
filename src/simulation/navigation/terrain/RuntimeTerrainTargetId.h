#pragma once
#include <cstdint>
namespace concord::simulation::navigation::terrain_target {
// The original caller still owns validity/terrain checks. Preserve raw key
// arithmetic, including the old -1/out-of-family behavior; do not tighten it.
constexpr std::int32_t encodeGroundKey(std::int32_t key) { return -1960000 - key; }
constexpr std::int32_t encodeBridgeKey(std::int32_t key) { return -960000 - key; }
// Cell remains the existing value type. No duplicate cell DTO is introduced.
template<class Cell>
constexpr bool decodeTerrainTargetId(std::int32_t targetId, Cell& cell, bool& bridge) {
    int key = -1;
    if (targetId >= -2959999 && targetId <= -1960000) {
        key = -1960000 - targetId;
        bridge = false;
    } else if (targetId >= -1959999 && targetId <= -960000) {
        key = -960000 - targetId;
        bridge = true;
    } else {
        return false;
    }
    cell = {key % 1000, key / 1000};
    return true;
}

}
