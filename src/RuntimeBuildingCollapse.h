#pragma once

#include <algorithm>
#include <string>
#include <vector>

struct RuntimeBuildingCollapseState {
    int objectId = -1;
    std::string animId;
    int startTick = 0;
    int frameCount = 0;
    int rate = 150;
};

inline int runtimeBuildingCollapseFrame(const RuntimeBuildingCollapseState& collapse, int tick, int hz) {
    // Art Rate is a 256-based accumulator advanced by the original 15 Hz game clock.
    const long long age = std::max(0LL, static_cast<long long>(tick) - collapse.startTick);
    return static_cast<int>(std::min(2147483647LL,
        age * 15 * std::max(1, collapse.rate) / (256LL * std::max(1, hz))));
}

inline const RuntimeBuildingCollapseState* runtimeBuildingCollapseForObject(
    const std::vector<RuntimeBuildingCollapseState>* collapses, int objectId, int tick, int hz
) {
    if (collapses) for (const auto& collapse : *collapses)
        if (collapse.objectId == objectId && runtimeBuildingCollapseFrame(collapse, tick, hz) < collapse.frameCount)
            return &collapse;
    return nullptr;
}
