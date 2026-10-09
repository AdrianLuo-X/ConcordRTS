#pragma once

#include "GameWorld.h"
#include "RulesDatabase.h"
#include "new_features/RuntimeBioReactorAnimation.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>

// Occupants are retained in the same authoritative manifest as transports.
// Queued entrants do not supply power until their entry completes.
inline int runtimeBuildingAbsorbedCount(
    const BuildingType& building,
    const WorldObjectInstance& object
) {
    return building.infantryAbsorb
        ? static_cast<int>(std::min(object.transportPassengerIds.size(),
            static_cast<std::size_t>(std::max(0, building.passengers))))
        : 0;
}

inline int runtimeBuildingPowerOutput(
    const BuildingType& building,
    const WorldObjectInstance& object
) {
    const int count = runtimeBuildingAbsorbedCount(building, object);
    const std::int64_t power = static_cast<std::int64_t>(building.power)
        + static_cast<std::int64_t>(building.extraPower)
            * static_cast<std::int64_t>(count);
    return static_cast<int>(std::clamp<std::int64_t>(power,
        std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
}

inline bool runtimeBuildingPowerAnimationVisible(
    const BuildingType& building,
    const WorldObjectInstance& object,
    std::string_view lowerAnimationKey,
    bool shadowPass = false
) {
    if (!building.infantryAbsorb) return true;
    // Blend the original empty/energized art by actual occupancy. Keep one
    // authored shadow at intermediate levels instead of darkening it twice.
    const int count = runtimeBuildingAbsorbedCount(building, object);
    const bool full = building.passengers > 0 && count == building.passengers;
    if (lowerAnimationKey == "activeanim") return !full;
    if (lowerAnimationKey == "activeanimtwo") return shadowPass ? full : count > 0;
    return true;
}

inline int runtimeBuildingPowerAnimationOpacity(
    const BuildingType& building,
    const WorldObjectInstance& object,
    std::string_view lowerAnimationKey
) {
    if (!building.infantryAbsorb || lowerAnimationKey != "activeanimtwo") return 255;
    if (building.passengers <= 0) return 0;
    return static_cast<int>(static_cast<std::int64_t>(runtimeBuildingAbsorbedCount(building, object))
        * 255 / building.passengers);
}
