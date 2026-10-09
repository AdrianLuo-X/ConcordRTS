#include "RuntimeSpecialBuildingSystem.h"
#include "RuntimeSecretLabReadOnlyMemo.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <utility>

namespace {

std::string trimAscii(std::string value) {
    const auto notSpace = [](unsigned char character) {
        return !std::isspace(character);
    };
    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), notSpace)
    );
    value.erase(
        std::find_if(value.rbegin(), value.rend(), notSpace).base(),
        value.end()
    );
    return value;
}

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

std::string canonicalOwner(
    const RuntimeSpecialBuildingContext& context,
    const std::string& owner
) {
    const std::string canonical = context.canonicalOwnerId
        ? context.canonicalOwnerId(owner)
        : owner;
    return lowerAscii(trimAscii(canonical));
}

bool isNeutralOwner(
    const RuntimeSpecialBuildingContext& context,
    const std::string& owner
) {
    if (context.ownerIsNeutral) {
        return context.ownerIsNeutral(owner);
    }
    const std::string canonical = canonicalOwner(context, owner);
    return canonical.empty()
        || canonical == "neutral"
        || canonical == "civilian"
        || canonical == "special";
}

const WorldObjectInstance* findObject(
    const RuntimeSpecialBuildingContext& context,
    int objectId
) {
    if (context.objects == nullptr) {
        return nullptr;
    }
    const auto found = std::find_if(
        context.objects->begin(),
        context.objects->end(),
        [objectId](const WorldObjectInstance& object) {
            return object.instanceId == objectId;
        }
    );
    return found == context.objects->end() ? nullptr : &*found;
}

const BuildingType* buildingRules(
    const RuntimeSpecialBuildingContext& context,
    const WorldObjectInstance& object
) {
    return context.rules == nullptr
        || object.kind != WorldObjectKind::Structure
        ? nullptr
        : context.rules->findBuildingType(object.type);
}

bool isLiveStructure(const WorldObjectInstance& object) {
    return object.alive
        && object.health > 0
        && !object.rubble
        && object.kind == WorldObjectKind::Structure;
}

bool isOperational(
    const WorldObjectInstance& object,
    const BuildingType& building
) {
    return isLiveStructure(object)
        && (!building.powered || object.poweredOperational);
}

double factoryPlantCostBonus(
    const BuildingType& building,
    RuntimeFactoryPlantCostCategory category
) {
    switch (category) {
    case RuntimeFactoryPlantCostCategory::Infantry:
        return building.infantryCostBonus;
    case RuntimeFactoryPlantCostCategory::Units:
        return building.unitsCostBonus;
    case RuntimeFactoryPlantCostCategory::Aircraft:
        return building.aircraftCostBonus;
    case RuntimeFactoryPlantCostCategory::Buildings:
        return building.buildingsCostBonus;
    case RuntimeFactoryPlantCostCategory::Defenses:
        return building.defensesCostBonus;
    }
    return 1.0;
}

int saturatingMultiply(int left, int right) {
    if (left <= 0 || right <= 0) {
        return 0;
    }
    const long long product = static_cast<long long>(left) * right;
    return product >= std::numeric_limits<int>::max()
        ? std::numeric_limits<int>::max()
        : static_cast<int>(product);
}

int saturatingAdd(int left, int right) {
    if (right <= 0) {
        return left;
    }
    return left > std::numeric_limits<int>::max() - right
        ? std::numeric_limits<int>::max()
        : left + right;
}

std::uint64_t stableHashAppend(std::uint64_t hash, const std::string& value) {
    for (unsigned char character : value) {
        hash ^= static_cast<std::uint64_t>(character);
        hash *= 1099511628211ULL;
    }
    return hash;
}

struct UnlockCandidate {
    RuntimeSpecialBuildingUnlockKind kind =
        RuntimeSpecialBuildingUnlockKind::Infantry;
    std::string typeId;
};

bool unlockCandidateExists(
    const RulesDatabase& rules,
    const UnlockCandidate& candidate
) {
    switch (candidate.kind) {
    case RuntimeSpecialBuildingUnlockKind::Infantry:
        return rules.findUnitType("InfantryType", candidate.typeId) != nullptr;
    case RuntimeSpecialBuildingUnlockKind::Unit:
        return rules.findUnitType("UnitType", candidate.typeId) != nullptr
            || rules.findUnitType("AircraftType", candidate.typeId) != nullptr;
    case RuntimeSpecialBuildingUnlockKind::Building:
        return rules.findBuildingType(candidate.typeId) != nullptr;
    }
    return false;
}

void appendUnlockCandidates(
    std::vector<UnlockCandidate>& output,
    const RulesDatabase& rules,
    RuntimeSpecialBuildingUnlockKind kind,
    const std::vector<std::string>& typeIds
) {
    for (const std::string& rawTypeId : typeIds) {
        UnlockCandidate candidate{kind, trimAscii(rawTypeId)};
        if (candidate.typeId.empty()
            || !unlockCandidateExists(rules, candidate)) {
            continue;
        }
        const std::string key = lowerAscii(candidate.typeId);
        const bool duplicate = std::any_of(
            output.begin(),
            output.end(),
            [kind, &key](const UnlockCandidate& existing) {
                return existing.kind == kind
                    && lowerAscii(existing.typeId) == key;
            }
        );
        if (!duplicate) {
            output.push_back(std::move(candidate));
        }
    }
}

std::vector<UnlockCandidate> secretLabCandidates(
    const RulesDatabase& rules,
    const BuildingType& building
) {
    std::vector<UnlockCandidate> candidates;
    // Westwood treats the local keys as a strict override chain.  A map that
    // specifies SecretInfantry must not also draw from its SecretUnit or
    // SecretBuilding values.
    if (!building.secretInfantry.empty()) {
        appendUnlockCandidates(
            candidates,
            rules,
            RuntimeSpecialBuildingUnlockKind::Infantry,
            building.secretInfantry
        );
        return candidates;
    }
    if (!building.secretUnits.empty()) {
        appendUnlockCandidates(
            candidates,
            rules,
            RuntimeSpecialBuildingUnlockKind::Unit,
            building.secretUnits
        );
        return candidates;
    }
    if (!building.secretBuildings.empty()) {
        appendUnlockCandidates(
            candidates,
            rules,
            RuntimeSpecialBuildingUnlockKind::Building,
            building.secretBuildings
        );
        return candidates;
    }

    const SpecialBuildingRules& globals = rules.getSpecialBuildingRules();
    appendUnlockCandidates(
        candidates,
        rules,
        RuntimeSpecialBuildingUnlockKind::Infantry,
        globals.secretInfantry
    );
    appendUnlockCandidates(
        candidates,
        rules,
        RuntimeSpecialBuildingUnlockKind::Unit,
        globals.secretUnits
    );
    appendUnlockCandidates(
        candidates,
        rules,
        RuntimeSpecialBuildingUnlockKind::Building,
        globals.secretBuildings
    );
    return candidates;
}

} // namespace

int RuntimeFactoryPlantCostSnapshot::adjustedProductionCost(
    RuntimeFactoryPlantCostCategory category,
    int baseCost
) const {
    if (baseCost <= 0) {
        return 0;
    }
    const auto index = static_cast<std::size_t>(category);
    // The scalar evaluator treats an unknown category as a unit multiplier.
    const long double bonus = index < bonuses.size() ? bonuses[index] : 1.0L;
    const long double adjusted = static_cast<long double>(baseCost) * bonus;
    if (!std::isfinite(adjusted)
        || adjusted >= static_cast<long double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    if (adjusted <= 0.0L) {
        return 0;
    }
    return static_cast<int>(adjusted);
}

RuntimeFactoryPlantCostSnapshot
RuntimeSpecialBuildingSystem::factoryPlantCostSnapshot(
    const RuntimeSpecialBuildingContext& context,
    const std::string& owner
) {
    RuntimeFactoryPlantCostSnapshot snapshot;
    if (context.rules == nullptr || context.objects == nullptr) {
        return snapshot;
    }
    const std::string ownerKey = canonicalOwner(context, owner);
    for (const WorldObjectInstance& object : *context.objects) {
        const BuildingType* building = buildingRules(context, object);
        if (building == nullptr
            || !building->factoryPlant
            || !isLiveStructure(object)
            || canonicalOwner(context, object.owner) != ownerKey) {
            continue;
        }
        // Every category retains the scalar evaluator's exact world order,
        // precision and nonnegative clamp, including overflow/zero behavior.
        for (std::size_t index = 0; index < snapshot.bonuses.size(); ++index) {
            snapshot.bonuses[index] *= std::max(
                0.0L,
                static_cast<long double>(factoryPlantCostBonus(
                    *building, static_cast<RuntimeFactoryPlantCostCategory>(index)))
            );
        }
    }
    return snapshot;
}

int RuntimeSpecialBuildingSystem::adjustedProductionCost(
    const RuntimeSpecialBuildingContext& context,
    const std::string& owner,
    RuntimeFactoryPlantCostCategory category,
    int baseCost
) {
    if (baseCost <= 0) {
        return 0;
    }
    if (context.rules == nullptr || context.objects == nullptr) {
        return baseCost;
    }

    const std::string ownerKey = canonicalOwner(context, owner);
    long double combinedBonus = 1.0L;
    for (const WorldObjectInstance& object : *context.objects) {
        const BuildingType* building = buildingRules(context, object);
        if (building == nullptr
            || !building->factoryPlant
            || !isLiveStructure(object)
            || canonicalOwner(context, object.owner) != ownerKey) {
            continue;
        }
        combinedBonus *= std::max(
            0.0L,
            static_cast<long double>(factoryPlantCostBonus(*building, category))
        );
    }

    const long double adjusted = static_cast<long double>(baseCost)
        * combinedBonus;
    if (!std::isfinite(adjusted)
        || adjusted >= static_cast<long double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    if (adjusted <= 0.0L) {
        return 0;
    }
    return static_cast<int>(adjusted);
}

int runtimeSpecialBuildingRuleFramesToTicks(int frames, int fixedTickHz) {
    if (frames <= 0 || fixedTickHz <= 0) {
        return 0;
    }
    const long long scaled =
        (static_cast<long long>(frames) * fixedTickHz) + 14LL;
    return static_cast<int>(std::min<long long>(
        std::numeric_limits<int>::max(),
        scaled / 15LL
    ));
}

const char* runtimeSpecialBuildingUnlockKindName(
    RuntimeSpecialBuildingUnlockKind kind
) {
    switch (kind) {
    case RuntimeSpecialBuildingUnlockKind::Infantry: return "Infantry";
    case RuntimeSpecialBuildingUnlockKind::Unit: return "Unit";
    case RuntimeSpecialBuildingUnlockKind::Building: return "Building";
    }
    return "Infantry";
}

std::vector<RuntimeSpecialBuildingUnlock>
RuntimeSpecialBuildingSystem::secretLabUnlocks(
    const RuntimeSpecialBuildingContext& context
) {
    std::vector<RuntimeSpecialBuildingUnlock> output;
    if (context.rules == nullptr || context.objects == nullptr) {
        return output;
    }

    for (const WorldObjectInstance& object : *context.objects) {
        const BuildingType* building = buildingRules(context, object);
        if (building == nullptr
            || !building->secretLab
            || !isLiveStructure(object)
            || isNeutralOwner(context, object.owner)) {
            continue;
        }
        const bool hasLocalOverride = !building->secretInfantry.empty()
            || !building->secretUnits.empty()
            || !building->secretBuildings.empty();
        // Original random boons are assigned to map-authored laboratories at
        // scenario load.  A later-built laboratory only participates when its
        // own rules explicitly name the technology it grants.
        if (!hasLocalOverride && trimAscii(object.mapId).empty()) {
            continue;
        }
        const std::vector<UnlockCandidate> candidates =
            secretLabCandidates(*context.rules, *building);
        if (candidates.empty()) {
            continue;
        }
        std::uint64_t hash = 1469598103934665603ULL;
        hash = stableHashAppend(hash, lowerAscii(trimAscii(object.type)));
        hash = stableHashAppend(hash, "|");
        // The boon belongs to the authored lab, not its current owner.  A
        // capture transfers the same technology instead of rerolling it.
        hash = stableHashAppend(hash, trimAscii(object.mapId));
        hash ^= static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(object.instanceId)
        );
        hash *= 1099511628211ULL;
        const UnlockCandidate& selected = candidates[
            static_cast<std::size_t>(hash % candidates.size())
        ];
        output.push_back({
            object.instanceId,
            object.owner,
            selected.kind,
            selected.typeId
        });
    }
    std::sort(
        output.begin(),
        output.end(),
        [](const RuntimeSpecialBuildingUnlock& left,
           const RuntimeSpecialBuildingUnlock& right) {
            return std::tie(left.sourceObjectId, left.typeId)
                < std::tie(right.sourceObjectId, right.typeId);
        }
    );
    return output;
}

bool RuntimeSpecialBuildingSystem::ownerHasSecretLabUnlock(
    const RuntimeSpecialBuildingContext& context,
    const std::string& owner,
    RuntimeSpecialBuildingUnlockKind kind,
    const std::string& typeId
) {
    const std::string ownerKey = canonicalOwner(context, owner);
    const std::string typeKey = lowerAscii(trimAscii(typeId));
    if (ownerKey.empty() || typeKey.empty()) {
        return false;
    }
    std::vector<RuntimeSpecialBuildingUnlock> originalScratch;
    const auto& unlocks = runtimeSecretLabUnlocksForQuery(context, originalScratch);
    return std::any_of(
        unlocks.begin(),
        unlocks.end(),
        [&](const RuntimeSpecialBuildingUnlock& unlock) {
            return unlock.kind == kind
                && canonicalOwner(context, unlock.owner) == ownerKey
                && lowerAscii(trimAscii(unlock.typeId)) == typeKey;
        }
    );
}

std::vector<RuntimeSpecialBuildingCredit>
RuntimeSpecialBuildingSystem::captureCredits(
    const RuntimeSpecialBuildingContext& context,
    int buildingObjectId,
    const std::string& previousOwner,
    const std::string& newOwner,
    int currentTick
) {
    const WorldObjectInstance* object = findObject(context, buildingObjectId);
    const BuildingType* building = object == nullptr
        ? nullptr
        : buildingRules(context, *object);
    if (object == nullptr
        || building == nullptr
        || !isLiveStructure(*object)
        || (building->produceCashStartup <= 0
            && (building->produceCashAmount <= 0
                || building->produceCashDelay <= 0))
        || !isNeutralOwner(context, previousOwner)
        || isNeutralOwner(context, newOwner)
        || canonicalOwner(context, previousOwner)
            == canonicalOwner(context, newOwner)) {
        return {};
    }
    const int delayTicks = runtimeSpecialBuildingRuleFramesToTicks(
        building->produceCashDelay,
        context.fixedTickHz
    );
    return {{
        buildingObjectId,
        newOwner,
        building->produceCashStartup,
        true,
        currentTick >= 0 && delayTicks > 0
            ? saturatingAdd(currentTick, delayTicks)
            : -1
    }};
}

std::vector<RuntimeSpecialBuildingCredit>
RuntimeSpecialBuildingSystem::periodicCredits(
    const RuntimeSpecialBuildingContext& context,
    int currentTick
) {
    std::vector<RuntimeSpecialBuildingCredit> output;
    if (context.rules == nullptr
        || context.objects == nullptr
        || currentTick <= 0) {
        return output;
    }
    for (const WorldObjectInstance& object : *context.objects) {
        const BuildingType* building = buildingRules(context, object);
        const int delayTicks = building == nullptr
            ? 0
            : runtimeSpecialBuildingRuleFramesToTicks(
                building->produceCashDelay,
                context.fixedTickHz
            );
        const bool paymentDue = object.specialCashNextTick >= 0
            ? currentTick >= object.specialCashNextTick
            : delayTicks > 0 && currentTick % delayTicks == 0;
        if (building == nullptr
            || !isLiveStructure(object)
            || isNeutralOwner(context, object.owner)
            || building->produceCashAmount <= 0
            || delayTicks <= 0
            || !paymentDue) {
            continue;
        }
        output.push_back({
            object.instanceId,
            object.owner,
            building->produceCashAmount,
            false,
            saturatingAdd(currentTick, delayTicks)
        });
    }
    std::sort(
        output.begin(),
        output.end(),
        [](const RuntimeSpecialBuildingCredit& left,
           const RuntimeSpecialBuildingCredit& right) {
            return left.sourceObjectId < right.sourceObjectId;
        }
    );
    return output;
}

std::vector<RuntimeSpecialBuildingHeal>
RuntimeSpecialBuildingSystem::periodicHealing(
    const RuntimeSpecialBuildingContext& context,
    int currentTick
) {
    std::vector<RuntimeSpecialBuildingHeal> output;
    if (context.rules == nullptr
        || context.objects == nullptr
        || currentTick <= 0) {
        return output;
    }
    const SpecialBuildingRules& globals =
        context.rules->getSpecialBuildingRules();
    const int infantryInterval = runtimeSpecialBuildingRuleFramesToTicks(
        globals.selfHealInfantryFrames,
        context.fixedTickHz
    );
    const int unitInterval = runtimeSpecialBuildingRuleFramesToTicks(
        globals.selfHealUnitFrames,
        context.fixedTickHz
    );
    const bool infantryPulse = infantryInterval > 0
        && currentTick % infantryInterval == 0;
    const bool unitPulse = unitInterval > 0
        && currentTick % unitInterval == 0;
    if (!infantryPulse && !unitPulse) {
        return output;
    }

    struct OwnerGrant {
        std::string owner;
        int infantry = 0;
        int units = 0;
    };
    std::map<std::string, OwnerGrant> grants;
    for (const WorldObjectInstance& object : *context.objects) {
        const BuildingType* building = buildingRules(context, object);
        if (building == nullptr
            || !isLiveStructure(object)
            || isNeutralOwner(context, object.owner)) {
            continue;
        }
        const int infantryGrant = std::max(
            0,
            building->infantryGainSelfHeal
        );
        const int unitGrant = std::max(0, building->unitsGainSelfHeal);
        if (infantryGrant <= 0 && unitGrant <= 0) {
            continue;
        }
        const std::string key = canonicalOwner(context, object.owner);
        if (key.empty()) {
            continue;
        }
        OwnerGrant& grant = grants[key];
        grant.owner = object.owner;
        grant.infantry = std::min(
            std::numeric_limits<int>::max(),
            grant.infantry > std::numeric_limits<int>::max() - infantryGrant
                ? std::numeric_limits<int>::max()
                : grant.infantry + infantryGrant
        );
        grant.units = std::min(
            std::numeric_limits<int>::max(),
            grant.units > std::numeric_limits<int>::max() - unitGrant
                ? std::numeric_limits<int>::max()
                : grant.units + unitGrant
        );
    }

    for (const WorldObjectInstance& object : *context.objects) {
        if (!object.alive || object.health <= 0) {
            continue;
        }
        const auto grant = grants.find(canonicalOwner(context, object.owner));
        if (grant == grants.end()) {
            continue;
        }
        int amount = 0;
        const UnitType* vehicleRules = object.kind == WorldObjectKind::Unit
            ? context.rules->findUnitType("UnitType", object.type)
            : nullptr;
        const bool receivesInfantryHealing =
            object.kind == WorldObjectKind::Infantry
            || (vehicleRules != nullptr && vehicleRules->organic);
        if (infantryPulse && receivesInfantryHealing) {
            amount = saturatingMultiply(
                globals.selfHealInfantryAmount,
                grant->second.infantry
            );
        } else if (unitPulse
            && object.kind == WorldObjectKind::Unit
            && (vehicleRules == nullptr || !vehicleRules->organic)
            && !object.authoredAircraft) {
            amount = saturatingMultiply(
                globals.selfHealUnitAmount,
                grant->second.units
            );
        }
        if (amount > 0) {
            output.push_back({object.instanceId, object.owner, amount});
        }
    }
    std::sort(
        output.begin(),
        output.end(),
        [](const RuntimeSpecialBuildingHeal& left,
           const RuntimeSpecialBuildingHeal& right) {
            return left.targetObjectId < right.targetObjectId;
        }
    );
    return output;
}

std::vector<RuntimeSpecialBuildingClone>
RuntimeSpecialBuildingSystem::clonesForProducedInfantry(
    const RuntimeSpecialBuildingContext& context,
    const std::string& owner,
    const std::string& infantryTypeId,
    int producingFactoryObjectId,
    int currentTick
) {
    // Requests are owed for this production completion even while the vat is
    // busy. Delivery clearance and its animation clock gate dispatch later.
    (void)currentTick;
    std::vector<RuntimeSpecialBuildingClone> output;
    if (context.rules == nullptr
        || context.objects == nullptr
        || context.rules->findUnitType("InfantryType", infantryTypeId)
            == nullptr) {
        return output;
    }
    const std::string ownerKey = canonicalOwner(context, owner);
    if (ownerKey.empty() || isNeutralOwner(context, owner)) {
        return output;
    }
    for (const WorldObjectInstance& object : *context.objects) {
        const BuildingType* building = buildingRules(context, object);
        if (building != nullptr
            && building->cloning
            && object.instanceId != producingFactoryObjectId
            && isLiveStructure(object)
            && canonicalOwner(context, object.owner) == ownerKey) {
            output.push_back({object.instanceId, owner, infantryTypeId});
        }
    }
    std::sort(
        output.begin(),
        output.end(),
        [](const RuntimeSpecialBuildingClone& left,
           const RuntimeSpecialBuildingClone& right) {
            return left.sourceObjectId < right.sourceObjectId;
        }
    );
    return output;
}

std::vector<RuntimeSpecialBuildingGap>
RuntimeSpecialBuildingSystem::activeGapGenerators(
    const RuntimeSpecialBuildingContext& context
) {
    std::vector<RuntimeSpecialBuildingGap> output;
    if (context.rules == nullptr || context.objects == nullptr) {
        return output;
    }
    for (const WorldObjectInstance& object : *context.objects) {
        const BuildingType* building = buildingRules(context, object);
        if (building == nullptr
            || !building->gapGenerator
            || !isOperational(object, *building)) {
            continue;
        }
        const int radius = std::clamp(
            building->gapRadiusInCells > 0
                ? building->gapRadiusInCells
                : building->superGapRadiusInCells,
            0,
            512
        );
        if (radius > 0) {
            output.push_back({
                object.instanceId,
                object.owner,
                object.x,
                object.y,
                radius
            });
        }
    }
    std::sort(
        output.begin(),
        output.end(),
        [](const RuntimeSpecialBuildingGap& left,
           const RuntimeSpecialBuildingGap& right) {
            return left.sourceObjectId < right.sourceObjectId;
        }
    );
    return output;
}
