#pragma once
#include "RulesDatabase.h"
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>

// Cached once by RulesDatabase::loadFromIni; never scan all rules per tick.
inline std::uint64_t runtimeDetailRulesHash(const RulesDatabase& rules) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << "FunctionalDetails/1\n";
    out << rules.getModExtensionRulesHash() << '\n';
    out << rules.getProductionRules().buildupTime << '\n';
    const auto& harvesting = rules.getHarvesterCadenceRules();
    out << harvesting.loadRate << ' ' << harvesting.dumpRateMinutes << '\n';
    const auto& slopes = rules.getTerrainMovementRules();
    out << slopes.trackedUphill << ' ' << slopes.trackedDownhill << ' '
        << slopes.wheeledUphill << ' ' << slopes.wheeledDownhill << '\n';
    const auto& combat = rules.getCombatDetailRules();
    const auto& special = rules.getSpecialMechanismRules();
    out << std::quoted(combat.metallicDebris) << '\n';
    out << special.chronoDelay << ' ' << special.chronoReinforcementDelay << '\n';
    const auto& crew = rules.getCrewSurvivorRules();
    out << crew.escapeChance << ' ' << std::quoted(crew.technician) << ' ' << std::quoted(crew.engineer);
    for (std::size_t side = 0; side < 3; ++side)
        out << ' ' << crew.survivorDivisors[side] << ' ' << std::quoted(crew.crewTypes[side]);
    out << '\n';
    const auto& mining = rules.getComputerMiningRules();
    for (std::size_t side = 0; side < 3; ++side)
        out << mining.virtualPurifiers[side] << ' ' << mining.harvestersPerRefinery[side]
            << ' ' << mining.slaveMinerNumber[side] << ' ';
    out << '\n';
    const auto& base = rules.getComputerBaseBuildingRules();
    for (std::size_t difficulty = 0; difficulty < 3; ++difficulty)
        out << base.alliedDefenseCounts[difficulty] << ' ' << base.sovietDefenseCounts[difficulty]
            << ' ' << base.thirdDefenseCounts[difficulty] << ' ' << base.extraRefineries[difficulty] << ' ';
    out << '\n' << base.refineryTypes.size();
    for (const auto& id : base.refineryTypes) out << ' ' << std::quoted(id);
    out << '\n' << base.harvesterTypes.size();
    for (const auto& id : base.harvesterTypes) out << ' ' << std::quoted(id);
    out << '\n';
    const auto& tactics = rules.getComputerTacticsRules();
    for (std::size_t difficulty = 0; difficulty < 3; ++difficulty)
        out << tactics.teamDelays[difficulty] << ' ' << tactics.minimumDefensiveTeams[difficulty]
            << ' ' << tactics.maximumDefensiveTeams[difficulty] << ' ' << tactics.totalTeamCap[difficulty]
            << ' ' << tactics.fillEarliestTeamProbability[difficulty] << ' ';
    out << tactics.dissolveUnfilledTeamDelay << ' ' << tactics.useMinimumDefenseRule << ' '
        << tactics.minorSuperReadyPercent << ' ' << tactics.safeDistance << ' ' << tactics.stray << ' ' << tactics.relaxedStray << ' '
        << tactics.successWeightDelta << ' '
        << tactics.failureWeightDelta << ' ' << tactics.trackRecordCoefficient << '\n';
    for (const auto& weights : {tactics.captureNormal, tactics.captureWounded,
            tactics.captureLowPower, tactics.captureLowMoney})
        for (int weight : weights) out << weight << ' ';
    out << tactics.captureLowMoneyMark << ' ' << tactics.captureWoundedMark << '\n';
    out << combat.ballisticScatter << ' ' << combat.projectileGravity << ' ' << combat.turboBoost << ' '
        << combat.openToppedRangeBonus << ' ' << combat.openToppedDamageMultiplier << ' '
        << combat.openToppedWarpDistance << ' ' << special.buildingRepairRateMinutes << ' '
        << special.repairPercent << ' ' << special.repairStep << ' ' << rules.getRefundPercent() << '\n';
    const auto sorted = [](const auto& values) {
        using Type = typename std::decay_t<decltype(values)>::value_type;
        std::vector<const Type*> result;
        for (const auto& value : values) result.push_back(&value);
        std::sort(result.begin(), result.end(), [](const auto* a, const auto* b) { return a->id < b->id; });
        return result;
    };
    for (const auto* land : sorted(rules.getLandTypes())) out << std::quoted(land->id) << ' '
        << land->foot << ' ' << land->track << ' ' << land->wheel << ' '
        << land->floatSpeed << ' ' << land->floatBeach << ' ' << land->hover << ' '
        << land->amphibious << ' ' << land->buildable << '\n';
    for (const auto* type : sorted(rules.getTerrainTypes())) out << std::quoted(type->id) << ' ' << std::quoted(type->ambientSound) << '\n';
    out << rules.getSpecialMechanismRules().conditionYellow << '\n';
    const auto units = [&](const auto& values) {
        for (const auto* type : sorted(values)) out << std::quoted(type->id) << ' '
            << std::quoted(type->ambientSound) << ' ' << std::quoted(type->turretRotateSound) << ' ' << std::quoted(type->auxSound1) << ' ' << std::quoted(type->auxSound2) << ' ' << type->pixelSelectionBracketDelta << ' '
            << type->minDebris << ' ' << type->maxDebris << ' '
            << std::quoted(type->debrisTypes) << ' ' << std::quoted(type->debrisMaximums) << ' ' << std::quoted(type->debrisAnims) << ' '
            << type->openTopped << ' ' << type->opportunityFire << ' ' << type->preventAttackMove << ' ' << type->distributedFire << ' '
            << type->radialFireSegments << ' ' << type->crewed << ' ' << type->carriesCrate << ' ' << type->isTrain << ' '
            << type->isSelectableCombatant << ' ' << type->alternateArcticArt << ' '
            << type->immune << ' ' << type->typeImmune << ' ' << type->immuneToPoison << ' ' << type->moveToShroud << ' '
            << type->fearless << ' ' << type->fraidycat << ' '
            << type->threatPosed << ' ' << type->threatAvoidanceCoefficient << ' '
            << type->crashable << ' ' << type->tiltCrashJumpjet << ' ' << type->jumpjetCrash << ' '
            << type->jumpjetSpeed << ' ' << type->jumpjetClimb << ' ' << type->jumpjetAccel << ' '
            << type->jumpjetTurnRate << ' ' << type->jumpJetHeight << ' ' << type->jumpjetWobbles << ' '
            << type->jumpjetDeviation << ' ' << type->jumpjetNoWobbles << ' '
            << type->crashGravity << ' ' << type->shipSinkingWeight << ' '
            << std::quoted(type->crashingSound) << ' ' << std::quoted(type->sinkingSound) << ' '
            << std::quoted(type->impactLandSound) << ' ' << std::quoted(type->impactWaterSound) << '\n';
    };
    units(rules.getInfantryTypes()); units(rules.getVehicleTypes()); units(rules.getAircraftTypes());
    for (const auto* type : sorted(rules.getVoxelAnimTypes())) out << std::quoted(type->id) << ' '
        << std::quoted(type->image) << ' ' << type->duration << ' ' << type->elasticity << ' '
        << type->minAngularVelocity << ' ' << type->maxAngularVelocity << ' '
        << type->minZVelocity << ' ' << type->maxZVelocity << ' ' << type->maxXYVelocity << ' '
        << std::quoted(type->bounceAnim) << ' ' << std::quoted(type->expireAnim) << ' ' << std::quoted(type->trailerAnim) << ' '
        << type->damage << ' ' << type->damageRadius << ' ' << std::quoted(type->warhead) << '\n';
    for (const auto* type : sorted(rules.getBuildingTypes())) out << std::quoted(type->id) << ' '
        << std::quoted(type->ambientSound) << ' ' << std::quoted(type->turretRotateSound) << ' ' << type->needsEngineer << ' ' << std::quoted(type->captureEvaEvent) << ' '
        << type->alternateArcticArt << ' ' << type->typeImmune << ' ' << type->immuneToPoison << ' ' << type->adjacent << ' ' << type->baseNormal << ' ' << type->eligibleForAllyBuilding << ' '
        << type->psychicDetectionRadius << ' ' << type->repairable << ' ' << type->clickRepairable << ' '
        << type->soylent << ' ' << std::quoted(type->freeUnit) << ' ' << type->crewed << ' ' << type->threatPosed << ' '
        << type->crateBeneath << ' ' << type->crateBeneathIsMoney << ' ' << type->revealToAll << ' '
        << std::quoted(type->destroyAnim) << ' ' << type->leaveRubble << ' '
        << type->minDebris << ' ' << type->maxDebris << ' ' << std::quoted(type->debrisAnims) << ' '
        << std::quoted(type->debrisTypes) << ' ' << std::quoted(type->debrisMaximums) << ' '
        << type->invisibleInGame << ' ' << type->lightVisibility << ' ' << type->lightIntensity << ' '
        << type->lightRedTint << ' ' << type->lightGreenTint << ' ' << type->lightBlueTint << '\n';
    for (const auto* type : sorted(rules.getWeaponTypes())) {
        out << std::quoted(type->id) << ' ' << type->burst << ' ' << type->burstDelay << ' ' << type->burstDelaySpecified << ' ' << type->turboBoost
            << ' ' << type->revealOnFire << ' ' << type->fireInTransport
            << ' ' << type->sonic << ' ' << type->ambientDamage
            << ' ' << std::quoted(type->occupantAnim) << ' ' << std::quoted(type->openToppedAnim);
        for (int delay : type->burstDelays) out << ' ' << delay;
        const auto* lifecycle = rules.findWeaponLifecycleProfile(type->id);
        out << ' ' << (lifecycle && lifecycle->omniFire) << '\n';
    }
    for (const auto* type : sorted(rules.getWarheadTypes())) out << std::quoted(type->id) << ' '
        << type->poison << '\n';
    for (const auto* type : sorted(rules.getProjectileTypes())) out << std::quoted(type->id) << ' '
        << type->inaccurate << ' ' << type->flakScatter << ' ' << type->floater << '\n';
    for (const auto* type : sorted(rules.getSuperWeaponTypes())) out << std::quoted(type->id) << ' '
        << type->fireAtPercentage << ' ' << type->separation << ' ' << std::quoted(type->cloudAnim) << '\n';
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char ch : out.str()) hash = (hash ^ ch) * 1099511628211ULL;
    return hash;
}
