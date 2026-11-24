#pragma once
#include "simulation/abilities/unit_special/RuntimeSpecialMechanismEvents.h"
#include <cstddef>
#include <string>
#include <vector>

enum class ParasiteTargetKind {
    Infantry,
    Vehicle
};

enum class ParasiteImpactKind {
    Hit,
    Miss
};

enum class ParasiteHostEventKind {
    Healed,
    SonicHit,
    Teleporting,
    DestroyedExternally,
    DestroyedByParasite
};

// The host resolves rules and live-world facts before beginning a launch.
// Keeping the complete value snapshot here prevents an in-flight parasite
// from changing behaviour if the source, weapon, or rules disappear later.
struct ParasiteLaunchRequest {
    int projectileId = -1;
    int currentTick = -1;
    int sourceObjectId = -1;
    int targetObjectId = -1;
    std::string sourceOwner;
    std::string sourceTypeId;
    std::string targetTypeId;
    std::string weaponId;
    std::string warheadId;
    ParasiteTargetKind targetKind = ParasiteTargetKind::Vehicle;
    SpecialCell targetCell;
    int damage = 0;
    int damageIntervalTicks = 1;
    int paralyzes = 0;
    int targetMaximumHealth = 1;
    bool targetParasiteable = false;
    bool sourceOrganic = false;
    bool sourceElite = false;
    bool culling = false;
    // Permyriad health ratios supplied from the authoritative rules snapshot.
    // Stock callers pass ConditionRed for normal and ConditionYellow for elite.
    int normalCullingThresholdPermyriad = 0;
    int eliteCullingThresholdPermyriad = 0;
};

struct ParasiteImpactRequest {
    int projectileId = -1;
    int currentTick = -1;
    ParasiteImpactKind impact = ParasiteImpactKind::Miss;
    int targetObjectId = -1;
    std::string targetTypeId;
    ParasiteTargetKind targetKind = ParasiteTargetKind::Vehicle;
    bool targetPresent = false;
    bool targetAlive = false;
    bool targetParasiteable = false;
    int targetMaximumHealth = 1;
    SpecialCell impactCell;
    bool returnCellAvailable = false;
    SpecialCell returnCell;
};

struct ParasiteHostEventRequest {
    int currentTick = -1;
    int targetObjectId = -1;
    ParasiteHostEventKind event = ParasiteHostEventKind::Healed;
    bool ejectCellAvailable = false;
    SpecialCell ejectCell;
    int stunTicks = 0;
};

struct ParasitePendingLaunchState {
    int projectileId = -1;
    int sourceObjectId = -1;
    int targetObjectId = -1;
    std::string owner;
    std::string sourceTypeId;
    std::string targetTypeId;
    std::string weaponId;
    std::string warheadId;
    ParasiteTargetKind targetKind = ParasiteTargetKind::Vehicle;
    SpecialCell targetCell;
    int launchedTick = 0;
    int damage = 0;
    int damageIntervalTicks = 1;
    int paralyzes = 0;
    int targetMaximumHealth = 1;
    int cullingThresholdPerMyriad = 0;
    bool targetParasiteable = false;
    bool organic = false;
    bool sourceElite = false;
    bool culling = false;
};

struct ParasiteResolvedLaunchState {
    int projectileId = -1;
    int sourceObjectId = -1;
    int targetObjectId = -1;
    int resolvedTick = -1;
    bool hostTerminated = false;
};

struct ParasiteLinkState {
    int projectileId = -1;
    int targetObjectId = -1;
    int sourceObjectId = -1;
    std::string owner;
    std::string sourceTypeId;
    std::string targetTypeId;
    std::string weaponId;
    std::string warheadId;
    int damage = 0;
    int attachedTick = 0;
    int nextDamageTick = 0;
    int damageIntervalTicks = 1;
    bool organic = false;
    bool sourceElite = false;
    int paralyzes = 0;
    bool culling = false;
    int cullingThresholdPerMyriad = 0;
    int targetMaximumHealth = 1;
    bool sourceExpectedInLimbo = true;
};

struct ParasiteOperationResult {
    bool accepted = false;
    bool idempotent = false;
    std::string reason;
    std::vector<SpecialMechanismAction> actions;
};

namespace runtime_parasite_values_detail {
inline constexpr std::size_t kMaximumParasiteRecords = 4096;
inline constexpr std::size_t kMaximumParasiteTextLength = 256;
inline constexpr int kParasiteRatioPerMyriad = 10000;

inline std::string parasiteTargetKindName(ParasiteTargetKind kind) {
    return kind == ParasiteTargetKind::Infantry ? "Infantry" : "Vehicle";
}

inline bool parasiteTargetKindFromName(
    const std::string& value,
    ParasiteTargetKind& kind
) {
    if (value == "Infantry") {
        kind = ParasiteTargetKind::Infantry;
        return true;
    }
    if (value == "Vehicle") {
        kind = ParasiteTargetKind::Vehicle;
        return true;
    }
    return false;
}

inline bool validParasiteText(const std::string& value) {
    return !value.empty() && value.size() <= kMaximumParasiteTextLength;
}

inline bool validParasiteRatio(int value) {
    return value >= 0 && value <= kParasiteRatioPerMyriad;
}
}
