#pragma once

#include "GameWorld.h"
#include "RulesDatabase.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

enum class RuntimeSpecialBuildingUnlockKind {
    Infantry,
    Unit,
    Building
};

enum class RuntimeFactoryPlantCostCategory {
    Infantry,
    Units,
    Aircraft,
    Buildings,
    Defenses
};

// A single read-only world pass can price a complete catalog. This value is
// deliberately local to that pass: ownership, rule and world changes require
// a fresh snapshot instead of a persistent invalidation protocol.
struct RuntimeFactoryPlantCostSnapshot {
    std::array<long double, 5> bonuses{{1.0L, 1.0L, 1.0L, 1.0L, 1.0L}};

    int adjustedProductionCost(
        RuntimeFactoryPlantCostCategory category,
        int baseCost
    ) const;
};

struct RuntimeSpecialBuildingContext {
    const RulesDatabase* rules = nullptr;
    const std::vector<WorldObjectInstance>* objects = nullptr;
    std::function<std::string(const std::string&)> canonicalOwnerId;
    std::function<bool(const std::string&)> ownerIsNeutral;
    // Westwood rule "frames" are based on 15 simulation frames per second.
    int fixedTickHz = 15;
};

struct RuntimeSpecialBuildingUnlock {
    int sourceObjectId = -1;
    std::string owner;
    RuntimeSpecialBuildingUnlockKind kind =
        RuntimeSpecialBuildingUnlockKind::Infantry;
    std::string typeId;
};

struct RuntimeSpecialBuildingCredit {
    int sourceObjectId = -1;
    std::string owner;
    int amount = 0;
    bool startup = false;
    int nextTick = -1;
};

struct RuntimeSpecialBuildingHeal {
    int targetObjectId = -1;
    std::string owner;
    int amount = 0;
};

struct RuntimeSpecialBuildingClone {
    int sourceObjectId = -1;
    std::string owner;
    std::string infantryTypeId;
};

struct RuntimeSpecialBuildingGap {
    int sourceObjectId = -1;
    std::string owner;
    int x = -1;
    int y = -1;
    int radius = 0;
};

// Special-building effects are derived from the authoritative rules and live
// world snapshot.  The evaluator deliberately owns no hidden clock or random
// state: fixed-tick income/healing schedules and Secret Lab choices therefore
// survive save/load and replay without adding another mutable authority.
class RuntimeSpecialBuildingSystem {
public:
    static RuntimeFactoryPlantCostSnapshot factoryPlantCostSnapshot(
        const RuntimeSpecialBuildingContext& context,
        const std::string& owner
    );

    static int adjustedProductionCost(
        const RuntimeSpecialBuildingContext& context,
        const std::string& owner,
        RuntimeFactoryPlantCostCategory category,
        int baseCost
    );

    static std::vector<RuntimeSpecialBuildingUnlock> secretLabUnlocks(
        const RuntimeSpecialBuildingContext& context
    );

    static bool ownerHasSecretLabUnlock(
        const RuntimeSpecialBuildingContext& context,
        const std::string& owner,
        RuntimeSpecialBuildingUnlockKind kind,
        const std::string& typeId
    );

    static std::vector<RuntimeSpecialBuildingCredit> captureCredits(
        const RuntimeSpecialBuildingContext& context,
        int buildingObjectId,
        const std::string& previousOwner,
        const std::string& newOwner,
        int currentTick = -1
    );

    static std::vector<RuntimeSpecialBuildingCredit> periodicCredits(
        const RuntimeSpecialBuildingContext& context,
        int currentTick
    );

    static std::vector<RuntimeSpecialBuildingHeal> periodicHealing(
        const RuntimeSpecialBuildingContext& context,
        int currentTick
    );

    static std::vector<RuntimeSpecialBuildingClone> clonesForProducedInfantry(
        const RuntimeSpecialBuildingContext& context,
        const std::string& owner,
        const std::string& infantryTypeId,
        int producingFactoryObjectId = -1,
        int currentTick = -1
    );

    static std::vector<RuntimeSpecialBuildingGap> activeGapGenerators(
        const RuntimeSpecialBuildingContext& context
    );
};

const char* runtimeSpecialBuildingUnlockKindName(
    RuntimeSpecialBuildingUnlockKind kind
);

int runtimeSpecialBuildingRuleFramesToTicks(int frames, int fixedTickHz);
