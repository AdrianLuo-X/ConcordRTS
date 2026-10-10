#pragma once

#include <cstddef>
#include <unordered_map>
#include <unordered_set>

// Presentation bookkeeping only. Call at the existing 300-animation-tick
// maintenance point, after this tick's emissions. Objects need instanceId/alive.
// The last-spawn value only suppresses another emission in that exact tick.
// Keep every living object's record, including paused/submerged/hidden objects,
// and keep current/future timestamps even for a missing or inactive object.
// Previously emitted wake actors have their own lifetime and are untouched.
template<class ObjectRange>
std::size_t pruneRuntimeWaterWakeSpawnHistory(
    std::unordered_map<int, int>& lastSpawnTickByObject,
    const ObjectRange& objects,
    int animationTick
) {
    if (animationTick < 0 || lastSpawnTickByObject.empty()) return 0;

    std::unordered_set<int> trackedAliveIds;
    trackedAliveIds.reserve(lastSpawnTickByObject.size());
    for (const auto& object : objects) {
        if (object.alive
            && lastSpawnTickByObject.find(object.instanceId)
                != lastSpawnTickByObject.end()) {
            trackedAliveIds.insert(object.instanceId);
        }
    }

    std::size_t removed = 0;
    for (auto entry = lastSpawnTickByObject.begin();
         entry != lastSpawnTickByObject.end();) {
        if (entry->second < animationTick
            && trackedAliveIds.find(entry->first) == trackedAliveIds.end()) {
            entry = lastSpawnTickByObject.erase(entry);
            ++removed;
        } else {
            ++entry;
        }
    }
    return removed;
}
