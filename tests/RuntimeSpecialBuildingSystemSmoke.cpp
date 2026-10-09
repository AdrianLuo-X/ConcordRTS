#include "GameWorld.h"
#include "IniFile.h"
#include "RulesDatabase.h"
#include "RuntimeSpecialBuildingSystem.h"
#include "RuntimeVision.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "special-building runtime smoke failure: "
                  << message << '\n';
    }
    return condition;
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

WorldObjectInstance makeObject(
    int id,
    WorldObjectKind kind,
    const std::string& type,
    const std::string& owner,
    int x = -1,
    int y = -1
) {
    WorldObjectInstance object;
    object.instanceId = id;
    object.kind = kind;
    object.type = type;
    object.owner = owner;
    object.health = 100;
    object.alive = true;
    object.poweredOperational = true;
    object.x = x;
    object.y = y;
    object.cell = y >= 0 && x >= 0 ? y * 512 + x : -1;
    return object;
}

RuntimeSpecialBuildingContext makeContext(
    const RulesDatabase& rules,
    const std::vector<WorldObjectInstance>& objects
) {
    RuntimeSpecialBuildingContext context;
    context.rules = &rules;
    context.objects = &objects;
    context.canonicalOwnerId = [](const std::string& owner) {
        return lowerAscii(owner);
    };
    context.ownerIsNeutral = [](const std::string& owner) {
        const std::string key = lowerAscii(owner);
        return key.empty() || key == "neutral" || key == "civilian";
    };
    return context;
}

bool hasHeal(
    const std::vector<RuntimeSpecialBuildingHeal>& actions,
    int objectId,
    int amount
) {
    return std::any_of(
        actions.begin(),
        actions.end(),
        [objectId, amount](const RuntimeSpecialBuildingHeal& action) {
            return action.targetObjectId == objectId
                && action.amount == amount;
        }
    );
}

std::optional<RuntimeSpecialBuildingUnlock> unlockFor(
    const std::vector<RuntimeSpecialBuildingUnlock>& unlocks,
    int sourceObjectId
) {
    const auto found = std::find_if(
        unlocks.begin(),
        unlocks.end(),
        [sourceObjectId](const RuntimeSpecialBuildingUnlock& unlock) {
            return unlock.sourceObjectId == sourceObjectId;
        }
    );
    return found == unlocks.end()
        ? std::nullopt
        : std::optional<RuntimeSpecialBuildingUnlock>(*found);
}

bool testHealing(const RulesDatabase& rules) {
    std::vector<WorldObjectInstance> objects{
        makeObject(10, WorldObjectKind::Structure, "HOSPITAL", "Alpha"),
        makeObject(11, WorldObjectKind::Structure, "HOSPITAL", "Alpha"),
        makeObject(20, WorldObjectKind::Structure, "MACHINE", "Alpha"),
        makeObject(21, WorldObjectKind::Structure, "MACHINE", "Alpha"),
        makeObject(100, WorldObjectKind::Infantry, "RIFLE", "Alpha"),
        makeObject(101, WorldObjectKind::Unit, "ORGANICVEH", "Alpha"),
        makeObject(102, WorldObjectKind::Unit, "TANK", "Alpha"),
        makeObject(103, WorldObjectKind::Infantry, "RIFLE", "Beta")
    };
    // These special buildings intentionally keep working at low power.
    for (std::size_t index = 0; index < 4; ++index) {
        objects[index].poweredOperational = false;
    }
    const RuntimeSpecialBuildingContext context = makeContext(rules, objects);
    bool success = true;
    success &= expect(
        RuntimeSpecialBuildingSystem::periodicHealing(context, 4).empty(),
        "self-heal fired before either configured interval"
    );

    const std::vector<RuntimeSpecialBuildingHeal> infantryPulse =
        RuntimeSpecialBuildingSystem::periodicHealing(context, 5);
    success &= expect(
        infantryPulse.size() == 2
            && hasHeal(infantryPulse, 100, 14)
            && hasHeal(infantryPulse, 101, 14)
            && !hasHeal(infantryPulse, 102, 14),
        "two hospitals did not stack for infantry and Organic vehicles"
    );

    const std::vector<RuntimeSpecialBuildingHeal> unitPulse =
        RuntimeSpecialBuildingSystem::periodicHealing(context, 8);
    success &= expect(
        unitPulse.size() == 1 && hasHeal(unitPulse, 102, 6),
        "two machine shops did not stack on the unit interval"
    );

    const std::vector<RuntimeSpecialBuildingHeal> combinedPulse =
        RuntimeSpecialBuildingSystem::periodicHealing(context, 40);
    success &= expect(
        combinedPulse.size() == 3
            && hasHeal(combinedPulse, 100, 14)
            && hasHeal(combinedPulse, 101, 14)
            && hasHeal(combinedPulse, 102, 6),
        "coincident infantry and unit healing pulses"
    );
    RuntimeSpecialBuildingContext doubledRate = makeContext(rules, objects);
    doubledRate.fixedTickHz = 30;
    success &= expect(
        RuntimeSpecialBuildingSystem::periodicHealing(
            doubledRate,
            5
        ).empty()
            && hasHeal(
                RuntimeSpecialBuildingSystem::periodicHealing(
                    doubledRate,
                    10
                ),
                100,
                14
            )
            && hasHeal(
                RuntimeSpecialBuildingSystem::periodicHealing(
                    doubledRate,
                    16
                ),
                102,
                6
            ),
        "15fps rule frames were not converted to the fixed tick rate"
    );
    success &= expect(
        runtimeSpecialBuildingRuleFramesToTicks(5, 15) == 5
            && runtimeSpecialBuildingRuleFramesToTicks(5, 30) == 10
            && runtimeSpecialBuildingRuleFramesToTicks(5, 60) == 20,
        "15/30/60Hz frame conversion contract"
    );
    return success;
}

bool testCloning(const RulesDatabase& rules) {
    std::vector<WorldObjectInstance> objects{
        makeObject(30, WorldObjectKind::Structure, "CLONER", "Alpha"),
        makeObject(31, WorldObjectKind::Structure, "CLONER", "Alpha"),
        makeObject(32, WorldObjectKind::Structure, "CLONER", "Beta"),
        makeObject(33, WorldObjectKind::Structure, "CLONER", "Alpha")
    };
    objects[0].poweredOperational = false;
    objects[1].poweredOperational = false;
    objects[3].alive = false;
    const std::vector<RuntimeSpecialBuildingClone> clones =
        RuntimeSpecialBuildingSystem::clonesForProducedInfantry(
            makeContext(rules, objects),
            "ALPHA",
            "RIFLE"
        );
    bool success = expect(
        clones.size() == 2
            && clones[0].sourceObjectId == 30
            && clones[1].sourceObjectId == 31
            && clones[0].infantryTypeId == "RIFLE"
            && clones[1].infantryTypeId == "RIFLE",
        "each live owned Cloning Vats must clone even during low power"
    );
    success &= expect(
        [&]() {
            objects[0].factoryExitAnimationEndTick = 100;
            const auto busyRequests =
                RuntimeSpecialBuildingSystem::clonesForProducedInfantry(
                    makeContext(rules, objects),
                    "Alpha",
                    "RIFLE",
                    -1,
                    50
                );
            const auto producerFiltered =
                RuntimeSpecialBuildingSystem::clonesForProducedInfantry(
                    makeContext(rules, objects),
                    "Alpha",
                    "RIFLE",
                    31,
                    101
                );
            objects[0].factoryExitAnimationEndTick = -1;
            objects[0].specialCloneDeliveryObjectId = 900;
            const auto deliveryLeaseRequests =
                RuntimeSpecialBuildingSystem::clonesForProducedInfantry(
                    makeContext(rules, objects),
                    "Alpha",
                    "RIFLE",
                    -1,
                    101
                );
            return busyRequests.size() == 2
                && busyRequests[0].sourceObjectId == 30
                && busyRequests[1].sourceObjectId == 31
                && producerFiltered.size() == 1
                && producerFiltered[0].sourceObjectId == 30
                && deliveryLeaseRequests.size() == 2
                && deliveryLeaseRequests[0].sourceObjectId == 30
                && deliveryLeaseRequests[1].sourceObjectId == 31;
        }(),
        "busy or delivering vats lost clone obligations, or the producer was cloned"
    );
    const std::vector<WorldObjectInstance> noVats;
    success &= expect(
        RuntimeSpecialBuildingSystem::clonesForProducedInfantry(
            makeContext(rules, noVats), "Alpha", "RIFLE"
        ).empty()
            && RuntimeSpecialBuildingSystem::clonesForProducedInfantry(
                makeContext(rules, objects), "Gamma", "RIFLE"
            ).empty()
            && RuntimeSpecialBuildingSystem::clonesForProducedInfantry(
                makeContext(rules, objects), "Alpha", "TANK"
            ).empty(),
        "missing owned vat or non-infantry production generated a clone obligation"
    );
    objects[0].owner = "Neutral";
    success &= expect(
        RuntimeSpecialBuildingSystem::clonesForProducedInfantry(
            makeContext(rules, objects), "Neutral", "RIFLE"
        ).empty(),
        "neutral production generated a clone obligation"
    );
    return success;
}

bool testFactoryPlants(const RulesDatabase& rules) {
    const BuildingType* primary = rules.findBuildingType("PLANT_PRIMARY");
    bool success = true;
    success &= expect(
        primary != nullptr && primary->factoryPlant
            && primary->powered
            && primary->infantryCostBonus == 0.5
            && primary->unitsCostBonus == 0.75
            && primary->aircraftCostBonus == 0.25
            && primary->buildingsCostBonus == 0.5
            && primary->defensesCostBonus == 0.125,
        "FactoryPlant cost bonuses were not parsed"
    );
    success &= expect(
        primary != nullptr
            && primary->buildingCapability.factoryPlant
            && primary->buildingCapability.infantryCostBonus == 0.5
            && primary->buildingCapability.unitsCostBonus == 0.75
            && primary->buildingCapability.aircraftCostBonus == 0.25
            && primary->buildingCapability.buildingsCostBonus == 0.5
            && primary->buildingCapability.defensesCostBonus == 0.125,
        "FactoryPlant cost bonuses were not mirrored into capabilities"
    );

    std::vector<WorldObjectInstance> objects{
        makeObject(300, WorldObjectKind::Structure, "PLANT_PRIMARY", "Alpha"),
        makeObject(301, WorldObjectKind::Structure, "PLANT_SECONDARY", "Alpha"),
        makeObject(302, WorldObjectKind::Structure, "PLANT_PRIMARY", "Beta"),
        makeObject(303, WorldObjectKind::Structure, "PLANT_PRIMARY", "Alpha"),
        makeObject(304, WorldObjectKind::Structure, "PLANT_PRIMARY", "Alpha")
    };
    objects[0].poweredOperational = false;
    objects[1].poweredOperational = false;
    objects[3].alive = false;
    objects[4].rubble = true;
    const RuntimeSpecialBuildingContext context = makeContext(rules, objects);

    success &= expect(
        RuntimeSpecialBuildingSystem::adjustedProductionCost(
            context,
            "ALPHA",
            RuntimeFactoryPlantCostCategory::Infantry,
            1000
        ) == 250
            && RuntimeSpecialBuildingSystem::adjustedProductionCost(
                context,
                "Alpha",
                RuntimeFactoryPlantCostCategory::Units,
                1000
            ) == 375
            && RuntimeSpecialBuildingSystem::adjustedProductionCost(
                context,
                "Alpha",
                RuntimeFactoryPlantCostCategory::Aircraft,
                1000
            ) == 125
            && RuntimeSpecialBuildingSystem::adjustedProductionCost(
                context,
                "Alpha",
                RuntimeFactoryPlantCostCategory::Buildings,
                1000
            ) == 250
            && RuntimeSpecialBuildingSystem::adjustedProductionCost(
                context,
                "Alpha",
                RuntimeFactoryPlantCostCategory::Defenses,
                1000
            ) == 62,
        "owned FactoryPlants did not stack all five category bonuses"
    );
    success &= expect(
        RuntimeSpecialBuildingSystem::adjustedProductionCost(
            context,
            "Beta",
            RuntimeFactoryPlantCostCategory::Units,
            1000
        ) == 750,
        "enemy FactoryPlants affected the requesting owner"
    );
    success &= expect(
        RuntimeSpecialBuildingSystem::adjustedProductionCost(
            context,
            "Alpha",
            RuntimeFactoryPlantCostCategory::Defenses,
            1
        ) == 0
            && RuntimeSpecialBuildingSystem::adjustedProductionCost(
                context,
                "Alpha",
                RuntimeFactoryPlantCostCategory::Units,
                0
            ) == 0,
        "FactoryPlant sub-credit truncation and zero-cost contracts"
    );
    return success;
}

bool testFactoryPlantCostSnapshots(const RulesDatabase& fixtureRules) {
    const std::array<RuntimeFactoryPlantCostCategory, 7> categories{{
        RuntimeFactoryPlantCostCategory::Infantry,
        RuntimeFactoryPlantCostCategory::Units,
        RuntimeFactoryPlantCostCategory::Aircraft,
        RuntimeFactoryPlantCostCategory::Buildings,
        RuntimeFactoryPlantCostCategory::Defenses,
        static_cast<RuntimeFactoryPlantCostCategory>(-1),
        static_cast<RuntimeFactoryPlantCostCategory>(99)
    }};
    const std::array<int, 8> costs{{
        -17, 0, 1, 7, 999, 10000,
        std::numeric_limits<int>::max() - 1,
        std::numeric_limits<int>::max()
    }};
    const auto equivalent = [&](const RuntimeSpecialBuildingContext& context,
                                const std::string& owner) {
        const auto snapshot = RuntimeSpecialBuildingSystem::factoryPlantCostSnapshot(
            context, owner);
        for (const auto category : categories) {
            for (const int cost : costs) {
                if (snapshot.adjustedProductionCost(category, cost)
                    != RuntimeSpecialBuildingSystem::adjustedProductionCost(
                        context, owner, category, cost)) return false;
            }
        }
        return true;
    };
    bool success = true;
    success &= expect(equivalent({}, "Alpha"), "empty cost snapshot changed prices");
    std::vector<WorldObjectInstance> objects;
    auto context = makeContext(fixtureRules, objects);
    success &= expect(equivalent(context, "Alpha"), "plant-free snapshot changed prices");
    for (int index = 0; index < 129; ++index) {
        auto object = makeObject(index, index % 5 == 0 ? WorldObjectKind::Unit
            : WorldObjectKind::Structure, index % 3 == 0 ? "PLANT_PRIMARY"
            : index % 3 == 1 ? "PLANT_SECONDARY" : "UNKNOWN",
            index % 4 == 0 ? " BETA " : index % 4 == 1 ? "ALPHA" : "Alpha");
        object.alive = index % 7 != 0;
        object.rubble = index % 11 == 0;
        object.health = index % 13 == 0 ? 0 : 100;
        object.poweredOperational = index % 2 == 0;
        objects.push_back(std::move(object));
        if (index % 8 == 0) {
            success &= expect(equivalent(context, " alpha ")
                && equivalent(context, "Beta") && equivalent(context, "Unknown"),
                "snapshot disagreed for ownership, inactive plants or growing world");
        }
    }
    std::reverse(objects.begin(), objects.end());
    success &= expect(equivalent(context, "Alpha"), "reordered snapshot disagreed");

    // Decimal factors expose intermediate truncation; extreme factors expose
    // reassociation, premature saturation and zero-after-overflow changes.
    IniFile edgeIni;
    RulesDatabase edgeRules;
    success &= expect(edgeIni.loadFromString(
        "[BuildingTypes]\n0=DECIMAL\n1=HUGE\n2=TINY\n3=ZERO\n"
        "[DECIMAL]\nFactoryPlant=yes\nInfantryCostBonus=.91\nUnitsCostBonus=1.09\n"
        "AircraftCostBonus=.333333333333\nBuildingsCostBonus=1.333333333333\nDefensesCostBonus=-1\n"
        "[HUGE]\nFactoryPlant=yes\nInfantryCostBonus=1e200\nUnitsCostBonus=1e200\n"
        "AircraftCostBonus=1e200\nBuildingsCostBonus=1e200\nDefensesCostBonus=1e200\n"
        "[TINY]\nFactoryPlant=yes\nInfantryCostBonus=1e-200\nUnitsCostBonus=1e-200\n"
        "AircraftCostBonus=1e-200\nBuildingsCostBonus=1e-200\nDefensesCostBonus=1e-200\n"
        "[ZERO]\nFactoryPlant=yes\nInfantryCostBonus=0\nUnitsCostBonus=0\n"
        "AircraftCostBonus=0\nBuildingsCostBonus=0\nDefensesCostBonus=0\n")
        && edgeRules.loadFromIni(edgeIni), "cost edge rules did not load");
    objects.clear();
    context.rules = &edgeRules;
    for (int index = 0; index < 32; ++index) {
        objects.push_back(makeObject(index, WorldObjectKind::Structure, "DECIMAL", "Alpha"));
        success &= expect(equivalent(context, "Alpha"), "decimal multiplication order changed");
    }
    objects.clear();
    for (int index = 0; index < 32; ++index) {
        objects.push_back(makeObject(index, WorldObjectKind::Structure,
            index < 30 ? "HUGE" : index == 30 ? "TINY" : "ZERO", "Alpha"));
        success &= expect(equivalent(context, "Alpha"), "overflow or zero clamp changed");
    }
    std::reverse(objects.begin(), objects.end());
    success &= expect(equivalent(context, "Alpha"), "reversed extreme factors changed");
    objects = {makeObject(1, WorldObjectKind::Structure, "DECIMAL", "Alpha")};
    const auto prior = RuntimeSpecialBuildingSystem::factoryPlantCostSnapshot(context, "Alpha");
    objects[0].owner = "Beta";
    const auto captured = RuntimeSpecialBuildingSystem::factoryPlantCostSnapshot(context, "Alpha");
    success &= expect(prior.adjustedProductionCost(RuntimeFactoryPlantCostCategory::Infantry, 1000)
            == RuntimeSpecialBuildingSystem::adjustedProductionCost(
                context, "Beta", RuntimeFactoryPlantCostCategory::Infantry, 1000)
        && captured.adjustedProductionCost(RuntimeFactoryPlantCostCategory::Infantry, 1000) == 1000,
        "captured plant reused stale cost or changed an existing snapshot");
    objects.clear();
    context.objects = nullptr;
    success &= expect(equivalent(context, "Alpha"), "missing objects snapshot changed prices");
    context.objects = &objects;
    context.rules = nullptr;
    success &= expect(equivalent(context, "Alpha"), "missing rules snapshot changed prices");
    return success;
}

bool testFactoryPlantCostScaling(const RulesDatabase& rules, bool benchmark) {
    std::vector<WorldObjectInstance> objects;
    constexpr int objectCount = 8192;
    constexpr int catalogCount = 256;
    for (int index = 0; index < objectCount; ++index) {
        objects.push_back(makeObject(index,
            index % 64 == 0 ? WorldObjectKind::Structure : WorldObjectKind::Unit,
            index % 1024 == 0 ? "PLANT_PRIMARY" : index % 64 == 0 ? "OIL" : "TANK",
            index % 2048 == 0 ? "Beta" : "Alpha"));
    }
    auto context = makeContext(rules, objects);
    std::uint64_t ownerQueries = 0;
    context.canonicalOwnerId = [&ownerQueries](const std::string& owner) {
        ++ownerQueries;
        return lowerAscii(owner);
    };
    std::vector<int> expected;
    expected.reserve(catalogCount);
    const auto scalarStart = std::chrono::steady_clock::now();
    for (int index = 0; index < catalogCount; ++index) {
        expected.push_back(RuntimeSpecialBuildingSystem::adjustedProductionCost(context,
            "Alpha", static_cast<RuntimeFactoryPlantCostCategory>(index % 5), 1000 + index));
    }
    const auto scalarEnd = std::chrono::steady_clock::now();
    const auto scalarOwnerQueries = ownerQueries;
    ownerQueries = 0;
    const auto snapshotStart = std::chrono::steady_clock::now();
    const auto snapshot = RuntimeSpecialBuildingSystem::factoryPlantCostSnapshot(context, "Alpha");
    bool identical = true;
    for (int index = 0; index < catalogCount; ++index) {
        identical = (expected[index] == snapshot.adjustedProductionCost(
            static_cast<RuntimeFactoryPlantCostCategory>(index % 5), 1000 + index)) && identical;
    }
    const auto snapshotEnd = std::chrono::steady_clock::now();
    const auto snapshotOwnerQueries = ownerQueries;
    bool success = expect(identical, "bulk catalog prices differ from scalar evaluation");
    success &= expect(snapshotOwnerQueries > 0
        && scalarOwnerQueries == snapshotOwnerQueries * catalogCount,
        "catalog snapshot did not eliminate per-item owner/world scans");
    if (benchmark) {
        std::cout << "Factory cost benchmark: objects=" << objectCount
                  << " catalog=" << catalogCount
                  << " scalarOwnerQueries=" << scalarOwnerQueries
                  << " snapshotOwnerQueries=" << snapshotOwnerQueries
                  << " scalarMs=" << std::chrono::duration<double, std::milli>(scalarEnd - scalarStart).count()
                  << " snapshotMs=" << std::chrono::duration<double, std::milli>(snapshotEnd - snapshotStart).count()
                  << " identical=" << (identical ? "yes" : "no") << '\n';
    }
    return success;
}

bool testOil(const RulesDatabase& rules) {
    std::vector<WorldObjectInstance> objects{
        makeObject(40, WorldObjectKind::Structure, "OIL", "Alpha")
    };
    objects[0].poweredOperational = false;
    RuntimeSpecialBuildingContext context = makeContext(rules, objects);
    bool success = true;
    const std::vector<RuntimeSpecialBuildingCredit> startup =
        RuntimeSpecialBuildingSystem::captureCredits(
            context,
            40,
            "Neutral",
            "Alpha",
            99
        );
    success &= expect(
        startup.size() == 1 && startup[0].startup
            && startup[0].owner == "Alpha" && startup[0].amount == 1000
            && startup[0].nextTick == 109,
        "Neutral capture did not award Oil Derrick startup credits"
    );
    success &= expect(
        RuntimeSpecialBuildingSystem::captureCredits(
            context,
            40,
            "Alpha",
            "Beta"
        ).empty(),
        "player-to-player capture incorrectly repeated startup credits"
    );
    std::vector<WorldObjectInstance> zeroStartupObjects{
        makeObject(41, WorldObjectKind::Structure, "OILZERO", "Alpha")
    };
    const auto zeroStartup = RuntimeSpecialBuildingSystem::captureCredits(
        makeContext(rules, zeroStartupObjects),
        41,
        "Neutral",
        "Alpha",
        99
    );
    success &= expect(
        zeroStartup.size() == 1
            && zeroStartup[0].amount == 0
            && zeroStartup[0].nextTick == 109,
        "Startup=0 did not start the Oil periodic timer"
    );
    success &= expect(
        RuntimeSpecialBuildingSystem::periodicCredits(context, 9).empty(),
        "Oil Derrick paid before its full delay"
    );
    const std::vector<RuntimeSpecialBuildingCredit> firstPayment =
        RuntimeSpecialBuildingSystem::periodicCredits(context, 10);
    success &= expect(
        firstPayment.size() == 1 && !firstPayment[0].startup
            && firstPayment[0].owner == "Alpha"
            && firstPayment[0].amount == 25,
        "Oil Derrick periodic payment"
    );
    objects[0].owner = "Beta";
    context = makeContext(rules, objects);
    const std::vector<RuntimeSpecialBuildingCredit> transferredPayment =
        RuntimeSpecialBuildingSystem::periodicCredits(context, 20);
    success &= expect(
        transferredPayment.size() == 1
            && transferredPayment[0].owner == "Beta"
            && transferredPayment[0].amount == 25,
        "Oil Derrick periodic income did not follow its current owner"
    );
    objects[0].owner = "Neutral";
    success &= expect(
        RuntimeSpecialBuildingSystem::periodicCredits(
            makeContext(rules, objects),
            30
        ).empty(),
        "Neutral Oil Derrick produced player income"
    );
    RuntimeSpecialBuildingContext doubledRate = makeContext(rules, objects);
    doubledRate.fixedTickHz = 30;
    objects[0].owner = "Alpha";
    objects[0].specialCashNextTick = -1;
    const bool legacyPhasePays =
        RuntimeSpecialBuildingSystem::periodicCredits(
            makeContext(rules, objects),
            100
        ).size() == 1;
    objects[0].specialCashNextTick = 119;
    success &= expect(
        legacyPhasePays
            && RuntimeSpecialBuildingSystem::periodicCredits(
                doubledRate,
                118
            ).empty()
            && RuntimeSpecialBuildingSystem::periodicCredits(
                doubledRate,
                119
            ).front().nextTick == 139,
        "Oil capture deadline or 15fps rule-frame conversion"
    );
    return success;
}

bool testSecretLabs(const RulesDatabase& rules) {
    std::vector<WorldObjectInstance> objects{
        makeObject(50, WorldObjectKind::Structure, "LAB_PRIORITY", "Alpha"),
        makeObject(51, WorldObjectKind::Structure, "LAB_UNIT", "Alpha"),
        makeObject(52, WorldObjectKind::Structure, "LAB_BUILDING", "Alpha"),
        makeObject(53, WorldObjectKind::Structure, "LAB_GLOBAL", "Alpha"),
        makeObject(54, WorldObjectKind::Structure, "LAB_GLOBAL", "Alpha")
    };
    objects[0].poweredOperational = false;
    objects[1].poweredOperational = false;
    objects[2].poweredOperational = false;
    objects[3].poweredOperational = false;
    objects[3].mapId = "authored-global-lab";
    objects[4].poweredOperational = false;

    RuntimeSpecialBuildingContext context = makeContext(rules, objects);
    const std::vector<RuntimeSpecialBuildingUnlock> before =
        RuntimeSpecialBuildingSystem::secretLabUnlocks(context);
    const auto priorityBefore = unlockFor(before, 50);
    const auto unitBefore = unlockFor(before, 51);
    const auto buildingBefore = unlockFor(before, 52);
    const auto globalBefore = unlockFor(before, 53);
    bool success = true;
    success &= expect(
        before.size() == 4
            && priorityBefore.has_value()
            && priorityBefore->kind
                == RuntimeSpecialBuildingUnlockKind::Infantry
            && (priorityBefore->typeId == "LOCALINF"
                || priorityBefore->typeId == "GLOBALINF")
            && unitBefore.has_value()
            && unitBefore->kind == RuntimeSpecialBuildingUnlockKind::Unit
            && unitBefore->typeId == "LOCALUNIT"
            && buildingBefore.has_value()
            && buildingBefore->kind
                == RuntimeSpecialBuildingUnlockKind::Building
            && buildingBefore->typeId == "LOCALBUILD"
            && globalBefore.has_value()
            && !unlockFor(before, 54).has_value(),
        "Secret Lab local Infantry > Unit > Building priority"
    );
    if (!priorityBefore.has_value()) {
        return false;
    }
    success &= expect(
        RuntimeSpecialBuildingSystem::ownerHasSecretLabUnlock(
            context,
            "alpha",
            priorityBefore->kind,
            priorityBefore->typeId
        ),
        "Secret Lab did not authorize its original owner"
    );

    objects[0].owner = "Beta";
    context = makeContext(rules, objects);
    const auto priorityAfter = unlockFor(
        RuntimeSpecialBuildingSystem::secretLabUnlocks(context),
        50
    );
    success &= expect(
        priorityAfter.has_value()
            && priorityAfter->kind == priorityBefore->kind
            && priorityAfter->typeId == priorityBefore->typeId
            && priorityAfter->owner == "Beta"
            && !RuntimeSpecialBuildingSystem::ownerHasSecretLabUnlock(
                context,
                "Alpha",
                priorityBefore->kind,
                priorityBefore->typeId
            )
            && RuntimeSpecialBuildingSystem::ownerHasSecretLabUnlock(
                context,
                "Beta",
                priorityBefore->kind,
                priorityBefore->typeId
            ),
        "Secret Lab transfer rerolled its reward or retained old authorization"
    );
    return success;
}

bool testGapVision(const RulesDatabase& rules) {
    GameWorld world;
    for (int y = 0; y <= 30; ++y) {
        for (int x = 0; x <= 40; ++x) {
            TerrainCell cell;
            cell.x = x;
            cell.y = y;
            cell.cellIndex512 = y * 512 + x;
            world.accessTerrainCells().push_back(cell);
        }
    }
    world.accessObjects().push_back(makeObject(
        200, WorldObjectKind::Structure, "SPYSAT", "Alpha", 1, 1
    ));
    world.accessObjects().push_back(makeObject(
        201, WorldObjectKind::Infantry, "RIFLE", "Alpha", 2, 2
    ));
    world.accessObjects().push_back(makeObject(
        210, WorldObjectKind::Structure, "GAP", "Beta", 15, 15
    ));
    world.accessObjects().push_back(makeObject(
        211, WorldObjectKind::Structure, "GAP", "Beta", 23, 15
    ));

    const std::vector<RuntimeVisionObserverDefinition> observers{
        {"Alpha", {"Alpha"}}
    };
    RuntimeVisionSystem vision;
    bool success = true;
    (void)vision.update(world, &rules, observers, 1);
    const RuntimeVisionObserverStats* poweredStats = vision.statsFor("Alpha");
    success &= expect(
        poweredStats != nullptr && poweredStats->spySatSources == 1
            && poweredStats->gapGeneratorSources == 2
            && poweredStats->gapShroudedCells > 0
            && vision.isCellVisibleTo("Alpha", 35, 25)
            && !vision.isCellVisibleTo("Alpha", 10, 15)
            && !vision.isCellVisibleTo("Alpha", 18, 15),
        "powered overlapping Gap Generators did not suppress SpySat"
    );

    world.accessObjects()[1].x = 15;
    world.accessObjects()[1].y = 15;
    (void)vision.update(world, &rules, observers, 2);
    success &= expect(
        vision.isCellVisibleTo("Alpha", 15, 15)
            && vision.isCellVisibleTo("Alpha", 17, 15)
            && !vision.isCellVisibleTo("Alpha", 10, 15),
        "friendly field vision did not locally pierce generated shroud"
    );

    world.accessObjects()[1].x = 2;
    world.accessObjects()[1].y = 2;
    world.accessObjects()[2].poweredOperational = false;
    (void)vision.update(world, &rules, observers, 3);
    const RuntimeVisionObserverStats* oneGapStats = vision.statsFor("Alpha");
    success &= expect(
        oneGapStats != nullptr && oneGapStats->gapGeneratorSources == 1
            && vision.isCellVisibleTo("Alpha", 10, 15)
            && !vision.isCellVisibleTo("Alpha", 18, 15),
        "depowering one Gap cleared another generator's overlap"
    );

    world.accessObjects()[3].poweredOperational = false;
    (void)vision.update(world, &rules, observers, 4);
    const RuntimeVisionObserverStats* noGapStats = vision.statsFor("Alpha");
    success &= expect(
        noGapStats != nullptr && noGapStats->gapGeneratorSources == 0
            && vision.isCellVisibleTo("Alpha", 18, 15),
        "depowered Gap Generator continued to suppress SpySat"
    );
    return success;
}

} // namespace

int main(int argc, char** argv) {
    const bool benchmark = argc == 3 && std::string(argv[2]) == "--benchmark";
    if (argc != 2 && !benchmark) {
        std::cerr << "usage: concord_special_building_system_smoke "
                     "<rules-fixture> [--benchmark]\n";
        return 2;
    }
    IniFile ini;
    RulesDatabase rules;
    if (!ini.loadFromFile(argv[1]) || !rules.loadFromIni(ini)) {
        std::cerr << "special-building runtime smoke: fixture load failed\n";
        return 3;
    }

    bool success = true;
    success &= testHealing(rules);
    success &= testCloning(rules);
    success &= testFactoryPlants(rules);
    success &= testFactoryPlantCostSnapshots(rules);
    success &= testFactoryPlantCostScaling(rules, benchmark);
    success &= testOil(rules);
    success &= testSecretLabs(rules);
    success &= testGapVision(rules);
    return success ? 0 : 1;
}
