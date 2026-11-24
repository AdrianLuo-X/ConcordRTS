#include "RuntimeParasiteSavedAuthorityPolicy.h"
#include "core/io/IniFile.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "data/rules/RulesDatabase.h"
#include "world/model/GameWorld.h"
#include "simulation/abilities/parasites/RuntimeParasiteValues.h"
#include "simulation/abilities/parasites/RuntimeParasiteAdmissionPolicy.h"
#include "simulation/abilities/parasites/RuntimeParasiteLaunchPolicy.h"
#include "simulation/movement/RuntimeObjectBehavior.h"
#include "simulation/projectiles/RuntimeProjectileValues.h"
#include "simulation/buildings/RuntimeRepairDepotSystem.h"
#include "simulation/production/RuntimeFactoryDeliveryState.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>
namespace concord::simulation::abilities {
using concord::runtime::session_detail::RuntimeProjectileLifecycleState;
using runtime_save_text::toLowerAsciiLocal;
using runtime_save_text::trimAsciiLocal;
using runtime_save_text::splitRuntimeSaveFields;
using runtime_save_text::parseRuntimeInt;
bool validateRuntimeSavedParasiteAuthority(
    const RuntimeParasiteSavedAuthorityQueries& queries,
    const RulesDatabase& rules,
    const int& fixedTickHz,
    const IniFile& ini,
    int saveVersion,
    const std::vector<WorldObjectInstance>& savedObjects,
    const std::vector<RuntimeProjectileEffect>& savedProjectiles,
    const int& savedNextProjectileActorId,
    int savedTick,
    const RuntimeRepairDepotSystemState& savedRepairDepots,
    const RuntimeFactoryDeliveryState& savedFactoryDeliveries,
    std::unordered_map<int, int>& savedStuns,
    std::string& errorMessage) {
    savedStuns.clear();
    if (saveVersion < 11) {
        return true;
    }
    const auto sameText = [](const std::string& left,
                             const std::string& right) {
        return toLowerAsciiLocal(trimAsciiLocal(left))
            == toLowerAsciiLocal(trimAsciiLocal(right));
    };
    const auto fail = [&errorMessage](const std::string& detail) {
        errorMessage = "invalid persisted parasite state: " + detail;
        return false;
    };

    std::vector<WorldObjectInstance> completeObjects = savedObjects;
    std::unordered_map<int, WorldObjectInstance*> objectsById;
    objectsById.reserve(completeObjects.size());
    for (WorldObjectInstance& object : completeObjects) {
        objectsById.emplace(object.instanceId, &object);
    }
    int objectCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("Objects", "Count", ""),
            objectCount
        )
        || objectCount != static_cast<int>(completeObjects.size())) {
        return fail("object set is incomplete");
    }
    for (int index = 0; index < objectCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("Objects", std::to_string(index), "")
        );
        int objectId = -1;
        int veterancy = -1;
        int dockedToObjectId = -2;
        int dockIndex = -2;
        int flightLevel = -1;
        bool airborne = false;
        // v14 accepts the published 39-field authority and its append-only
        // runtime extensions (crate, special-building, prone, and weapon
        // cooldown state).
        const bool validObjectFieldCount = tokens.size() == 39U
            || (saveVersion >= 14
                && (tokens.size() == 42U
                    || tokens.size() == 43U
                    || tokens.size() == 44U
                    || tokens.size() == 46U
                    || tokens.size() == 47U
                    || tokens.size() == 48U
                    || tokens.size() == 49U
                    || tokens.size() == 50U
                    || tokens.size() == 54U
                    || tokens.size() == 55U || tokens.size() == 56U || tokens.size() == 57U || tokens.size() == 58U || tokens.size() == 59U || tokens.size() == 60U || tokens.size() == 62U || tokens.size() == 63U || tokens.size() == 64U));
        bool prone = false;
        int proneTicksRemaining = 0;
        const bool proneAuthorityValid = tokens.size() < 46U
            || (queries.parseRuntimeStrictBool(tokens[44], prone)
                && parseRuntimeInt(tokens[45], proneTicksRemaining)
                && proneTicksRemaining >= 0
                && prone == (proneTicksRemaining > 0));
        int weaponCooldownTicksRemaining = 0;
        const bool weaponCooldownAuthorityValid = tokens.size() < 47U
            || (parseRuntimeInt(
                    tokens[46],
                    weaponCooldownTicksRemaining
                )
                && weaponCooldownTicksRemaining >= 0);
        bool crateCloakable = false;
        const bool crateCloakAuthorityValid = tokens.size() < 48U
            || queries.parseRuntimeStrictBool(tokens[47], crateCloakable);
        int deployStateTicksRemaining = -1;
        const bool deployStateTimerAuthorityValid = tokens.size() < 49U
            || (parseRuntimeInt(tokens[48], deployStateTicksRemaining)
                && deployStateTicksRemaining >= -1);
        int chargeTurretStartTick = -1;
        const bool chargeTurretAuthorityValid = tokens.size() < 50U
            || (parseRuntimeInt(tokens[49], chargeTurretStartTick)
                && chargeTurretStartTick >= -1);
        if (!validObjectFieldCount
            || !proneAuthorityValid
            || !weaponCooldownAuthorityValid
            || !crateCloakAuthorityValid
            || !deployStateTimerAuthorityValid
            || !chargeTurretAuthorityValid
            || !parseRuntimeInt(tokens[1], objectId)
            || !parseRuntimeInt(tokens[20], veterancy)
            || !parseRuntimeInt(tokens[23], dockedToObjectId)
            || !parseRuntimeInt(tokens[24], dockIndex)
            || !queries.parseRuntimeStrictBool(tokens[25], airborne)
            || !parseRuntimeInt(tokens[26], flightLevel)
            || veterancy < 0 || veterancy > 2
            || dockedToObjectId < -1 || dockIndex < -1
            || ((dockedToObjectId < 0) != (dockIndex < 0))
            || flightLevel < 0) {
            return fail("object authority fields are malformed");
        }
        const auto object = objectsById.find(objectId);
        if (object == objectsById.end() || object->second == nullptr) {
            return fail("object authority identity is missing");
        }
        object->second->veterancy = veterancy;
        object->second->dockedToObjectId = dockedToObjectId;
        object->second->dockIndex = dockIndex;
        object->second->airborne = airborne;
        object->second->flightLevel = flightLevel;
        if (tokens.size() >= 46U) {
            object->second->prone = prone;
            object->second->proneTicksRemaining = proneTicksRemaining;
        }
        if (tokens.size() >= 47U) {
            object->second->weaponCooldownTicksRemaining =
                weaponCooldownTicksRemaining;
        }
        if (tokens.size() >= 48U) {
            object->second->crateCloakable = crateCloakable;
        }
        if (tokens.size() >= 49U) {
            object->second->deployStateTicksRemaining =
                deployStateTicksRemaining;
        }
        if (tokens.size() >= 50U) {
            object->second->chargeTurretStartTick = chargeTurretStartTick;
        }
        if (tokens.size() >= 54U) {
            auto& item = *object->second;
            if (!parseRuntimeInt(tokens[50], item.tankBunkerHostId)
                || !parseRuntimeInt(tokens[51], item.tankBunkerOccupantId)
                || !parseRuntimeInt(tokens[52], item.tankBunkerAnimationStartTick)
                || !queries.parseRuntimeStrictBool(tokens[53], item.tankBunkerLowering)
                || item.tankBunkerHostId < -1 || item.tankBunkerOccupantId < -1
                || item.tankBunkerAnimationStartTick < -1) return fail("malformed tank bunker authority");
        }
        if (tokens.size() >= 55U
            && !queries.parseRuntimeStrictBool(tokens[54], object->second->harvestAutoSearchSuspended)) {
            return fail("malformed harvester command authority");
        }
        if (tokens.size() >= 56U && (!parseRuntimeInt(tokens[55], object->second->factoryExitHostId)
                || object->second->factoryExitHostId < -1)) {
            return fail("malformed factory exit host authority");
        }
        if (tokens.size() >= 57U && (!parseRuntimeInt(tokens[56], object->second->radiationEruptionCooldownTicksRemaining)
                || object->second->radiationEruptionCooldownTicksRemaining < 0)) {
            return fail("malformed radiation eruption cooldown authority");
        }
        if (tokens.size() >= 58U && !queries.parseRuntimeStrictBool(tokens[57], object->second->hasBeenCaptured)) {
            return fail("malformed capture history authority");
        }
        if (tokens.size() >= 59U
            && (!queries.parseRuntimeStrictBool(tokens[58], object->second->rubble)
                || (object->second->rubble && (object->second->kind != WorldObjectKind::Structure
                    || object->second->alive || object->second->health != 0)))) {
            return fail("malformed building rubble authority");
        }
        if (tokens.size() >= 63U) {
            const BuildingType* absorber = object->second->kind == WorldObjectKind::Structure
                ? queries.findRuntimeBuildingTypeById(rules, object->second->type) : nullptr;
            if (!parseRuntimeInt(tokens[62], object->second->bioReactorAnimationStartTick)
                || object->second->bioReactorAnimationStartTick < -1
                || object->second->bioReactorAnimationStartTick > savedTick
                || (object->second->bioReactorAnimationStartTick >= 0
                    && (absorber == nullptr || !absorber->infantryAbsorb))) {
                return fail("malformed bio-reactor animation authority");
            }
        }
        if (tokens.size() >= 64U
            && (!parseRuntimeInt(tokens[63], object->second->bioReactorAnimationPoweredStartTick)
                || object->second->bioReactorAnimationPoweredStartTick < -1
                || object->second->bioReactorAnimationPoweredStartTick > savedTick
                || (object->second->bioReactorAnimationPoweredStartTick >= 0
                    && (object->second->bioReactorAnimationStartTick < 0
                        || object->second->bioReactorAnimationPoweredStartTick > object->second->bioReactorAnimationStartTick)))) {
            return fail("malformed bio-reactor powered animation authority");
        }
    }

    std::unordered_set<int> behaviorObjectIds;
    std::unordered_map<int, RuntimeObjectBehavior> mobileParasiteHostMoves;
    std::unordered_set<int> mobileParasiteHostAttacks;
    int behaviorCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("Behaviors", "Count", ""),
            behaviorCount
        )
        || behaviorCount < 0 || behaviorCount > 100000) {
        return fail("behavior header is malformed");
    }
    for (int index = 0; index < behaviorCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("Behaviors", std::to_string(index), "")
        );
        int objectId = -1;
        if (tokens.empty() || !parseRuntimeInt(tokens[0], objectId)
            || !behaviorObjectIds.insert(objectId).second) {
            return fail("behavior identity is malformed");
        }
        const auto object = objectsById.find(objectId);
        RuntimeObjectBehavior behavior;
        int parsedId = -1;
        if (object != objectsById.end() && object->second != nullptr
            && queries.parseRuntimeSavedBehaviorRecord(tokens, *object->second, saveVersion, parsedId, behavior)
            && parsedId == objectId) {
            if (behavior.kind == RuntimeBehaviorKind::Move
                && behavior.targetObjectId < 0 && !behavior.attackBridge
                && !behavior.attackGround && !behavior.forceFire
                && behavior.moveTarget.x >= 0 && behavior.moveTarget.x < 512
                && behavior.moveTarget.y >= 0 && behavior.moveTarget.y < 512) {
                mobileParasiteHostMoves.emplace(objectId, std::move(behavior));
            } else if (behavior.kind == RuntimeBehaviorKind::Attack
                && !behavior.attackBridge && !behavior.attackGround
                && behavior.targetObjectId >= 0 && behavior.targetObjectId != objectId
                && (behavior.pendingAllowedOccupiedObjectId < 0
                    || behavior.pendingAllowedOccupiedObjectId == behavior.targetObjectId)) {
                // Attack-path requests reserve their target's occupied cell.
                // This is ordinary combat authority, not a second host for
                // the parasite. A Limbo/dead/missing target is still invalid.
                const auto attacked = objectsById.find(behavior.targetObjectId);
                if (attacked != objectsById.end() && attacked->second != nullptr
                    && attacked->second->alive && attacked->second->health > 0) {
                    mobileParasiteHostAttacks.insert(objectId);
                }
            }
        }
    }

    std::unordered_set<int> containedOrHostingObjectIds;
    int manifestCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("TransportManifests", "Count", "0"),
            manifestCount
        )
        || manifestCount < 0 || manifestCount > 100000) {
        return fail("transport authority header is malformed");
    }
    for (int index = 0; index < manifestCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("TransportManifests", std::to_string(index), "")
        );
        int hostId = -1;
        if (tokens.size() != 2U || !parseRuntimeInt(tokens[0], hostId)
            || hostId < 0) {
            return fail("transport authority record is malformed");
        }
        containedOrHostingObjectIds.insert(hostId);
        std::stringstream passengers(tokens[1]);
        std::string passengerToken;
        while (std::getline(passengers, passengerToken, ',')) {
            int passengerId = -1;
            if (!parseRuntimeInt(passengerToken, passengerId)
                || passengerId < 0) {
                return fail("transport passenger identity is malformed");
            }
            containedOrHostingObjectIds.insert(passengerId);
        }
    }
    if (ini.getSection("GarrisonRuntime") != nullptr) {
        int hostCount = -1;
        if (!parseRuntimeInt(
                ini.getValue("GarrisonRuntime", "HostCount", ""),
                hostCount
            )
            || hostCount < 0 || hostCount > 100000) {
            return fail("garrison authority header is malformed");
        }
        for (int index = 0; index < hostCount; ++index) {
            const std::string section = "GarrisonRuntime.Host."
                + std::to_string(index);
            int hostId = -1;
            if (ini.getSection(section) == nullptr
                || !parseRuntimeInt(
                    ini.getValue(section, "HostId", ""),
                    hostId
                )
                || hostId < 0) {
                return fail("garrison host identity is malformed");
            }
            containedOrHostingObjectIds.insert(hostId);
            std::stringstream occupants(
                ini.getValue(section, "Occupants", "")
            );
            std::string occupantToken;
            while (std::getline(occupants, occupantToken, ',')) {
                if (trimAsciiLocal(occupantToken).empty()) {
                    continue;
                }
                int occupantId = -1;
                if (!parseRuntimeInt(occupantToken, occupantId)
                    || occupantId < 0) {
                    return fail("garrison occupant identity is malformed");
                }
                containedOrHostingObjectIds.insert(occupantId);
            }
        }
    }

    std::unordered_set<int> intentObjectIds;
    for (const std::string& section : {
            std::string("EngineerIntents"),
            std::string("OccupyIntents"),
            std::string("TransportIntents"),
            std::string("TimedBombIntents")}) {
        int count = -1;
        if (!parseRuntimeInt(
                ini.getValue(section, "Count", "0"),
                count
            )
            || count < 0 || count > 100000) {
            return fail("exclusive intent header is malformed");
        }
        for (int index = 0; index < count; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue(section, std::to_string(index), "")
            );
            int sourceId = -1;
            int targetId = -1;
            if (tokens.size() < 2U
                || !parseRuntimeInt(tokens[0], sourceId)
                || !parseRuntimeInt(tokens[1], targetId)
                || sourceId < 0 || targetId < 0) {
                return fail("exclusive intent identity is malformed");
            }
            intentObjectIds.insert(sourceId);
            (void)targetId;
        }
    }

    std::unordered_set<int> repairOrDeliveryObjectIds;
    for (const RuntimeRepairDepotRelation& relation :
            savedRepairDepots.relations()) {
        repairOrDeliveryObjectIds.insert(relation.hostId);
        repairOrDeliveryObjectIds.insert(relation.unitId);
    }
    for (const RuntimeFactoryDeliveryLease& lease :
            savedFactoryDeliveries.leases()) {
        repairOrDeliveryObjectIds.insert(lease.hostId);
        repairOrDeliveryObjectIds.insert(lease.producedObjectId);
    }
    const auto hasExclusiveAuthority = [
        &objectsById,
        &behaviorObjectIds,
        &containedOrHostingObjectIds,
        &intentObjectIds,
        &repairOrDeliveryObjectIds,
        &queries
    ](int objectId, bool rejectBehavior, bool asHost = false) {
        const auto object = objectsById.find(objectId);
        if (object == objectsById.end() || object->second == nullptr) {
            return true;
        }
        return object->second->dockedToObjectId >= 0
            || object->second->dockIndex >= 0
            || containedOrHostingObjectIds.contains(objectId)
            || intentObjectIds.contains(objectId)
            || repairOrDeliveryObjectIds.contains(objectId)
            || (rejectBehavior && behaviorObjectIds.contains(objectId))
            || queries.savedIsCarrierChild(objectId)
            || (!asHost && queries.savedIsCarrierParent(objectId));
    };

    // A mechanical, non-paralyzing parasite does not own its host's movement
    // or ordinary object attack. Repair still requires its own exclusive
    // move/dock authority; accepting Attack must never bypass that relation.
    const auto mobileParasiteHostAuthorityIsSafe = [&](const WorldObjectInstance& target) {
        const int id = target.instanceId;
        if (containedOrHostingObjectIds.contains(id) || intentObjectIds.contains(id)
            || queries.savedIsCarrierChild(id)) return false;
        for (const auto& lease : savedFactoryDeliveries.leases())
            if (lease.hostId == id || lease.producedObjectId == id) return false;
        const RuntimeRepairDepotRelation* repair = nullptr;
        for (const auto& relation : savedRepairDepots.relations()) {
            if (relation.hostId == id) return false;
            if (relation.unitId == id) {
                if (repair != nullptr) return false;
                repair = &relation;
            }
        }
        const auto move = mobileParasiteHostMoves.find(id);
        if (repair == nullptr) {
            return target.dockedToObjectId < 0 && target.dockIndex < 0
                && ((move != mobileParasiteHostMoves.end()
                        && move->second.pendingAllowedOccupiedObjectId < 0)
                    || mobileParasiteHostAttacks.contains(id));
        }
        if (behaviorObjectIds.contains(id) && move == mobileParasiteHostMoves.end()) return false;
        const auto host = objectsById.find(repair->hostId);
        if (host == objectsById.end() || host->second == nullptr || !host->second->alive
            || host->second->kind != WorldObjectKind::Structure
            || repair->domain != RuntimeRepairDepotDomain::Ground
            || !sameText(repair->unitType, target.type)) return false;
        if (repair->phase == RuntimeRepairDepotRelationPhase::Docked) {
            return !behaviorObjectIds.contains(id)
                && target.dockedToObjectId == repair->hostId && target.dockIndex == repair->dockIndex
                && target.x == repair->dockCell.x && target.y == repair->dockCell.y
                && target.onBridge == repair->dockCell.onBridge;
        }
        const bool queued = repair->phase == RuntimeRepairDepotRelationPhase::Queued;
        const auto& cell = queued ? repair->queueCell : repair->dockCell;
        if (target.dockedToObjectId >= 0 || target.dockIndex >= 0) return false;
        if (move == mobileParasiteHostMoves.end())
            return target.x == cell.x && target.y == cell.y && target.onBridge == cell.onBridge;
        return move->second.moveTarget == RuntimeCellPoint{cell.x, cell.y}
            && move->second.pendingAllowedOccupiedObjectId == (queued ? -1 : repair->hostId);
    };

    std::unordered_map<int, const RuntimeProjectileEffect*> projectilesById;
    projectilesById.reserve(savedProjectiles.size());
    for (const RuntimeProjectileEffect& projectile :
            savedProjectiles) {
        if (!projectilesById.emplace(
                projectile.actorId,
                &projectile
            ).second) {
            return fail("duplicate projectile actor identity");
        }
    }

    const int normalThreshold = static_cast<int>(std::lround(
        std::clamp(
            queries.savedConditionRed(),
            0.0,
            1.0
        ) * 10000.0
    ));
    const int eliteThreshold = static_cast<int>(std::lround(
        std::clamp(
            queries.savedConditionYellow(),
            0.0,
            1.0
        ) * 10000.0
    ));
    const auto strictParasiteRules = [
        &rules,
        &queries,
        &sameText
    ](
        const WorldObjectInstance& source,
        const std::string& weaponId,
        const std::string& warheadId,
        const WeaponType*& weapon,
        const WarheadType*& warhead,
        const ProjectileType*& projectile
    ) {
        weapon = queries.findRuntimeWeaponType(rules, weaponId);
        warhead = queries.findRuntimeWarheadType(rules, warheadId);
        projectile = weapon == nullptr
            ? nullptr
            : queries.findRuntimeProjectileType(rules, weapon->projectile);
        return weapon != nullptr && warhead != nullptr
            && projectile != nullptr
            && sameText(weapon->id, weaponId)
            && sameText(weapon->warhead, warheadId)
            && sameText(warhead->id, warheadId)
            && runtimeParasiteWarheadIsExclusive(*weapon, *warhead)
            && queries.runtimeSpawnedMissileProfileForWeapon(
                rules,
                source,
                *weapon
            ) == nullptr
            && !projectile->airburst
            && projectile->shrapnelCount <= 0
            && trimAsciiLocal(projectile->airburstWeapon).empty()
            && trimAsciiLocal(projectile->shrapnelWeapon).empty();
    };

    std::unordered_set<int> limboSourceIds;
    for (const auto& [projectileId, pending] :
            queries.savedPendingParasiteLaunches()) {
        const auto sourceIt = objectsById.find(pending.sourceObjectId);
        const auto targetIt = objectsById.find(pending.targetObjectId);
        const auto projectileIt = projectilesById.find(projectileId);
        if (projectileId <= 0
            || projectileId >= savedNextProjectileActorId
            || sourceIt == objectsById.end()
            || targetIt == objectsById.end()
            || projectileIt == projectilesById.end()
            || sourceIt->second == nullptr || targetIt->second == nullptr
            || projectileIt->second == nullptr) {
            return fail("pending launch lacks its exact causal objects");
        }
        const WorldObjectInstance& source = *sourceIt->second;
        const WorldObjectInstance& target = *targetIt->second;
        const RuntimeProjectileEffect& projectile = *projectileIt->second;
        const UnitType* sourceType = queries.findRuntimeUnitType(rules, source);
        const UnitType* targetType = queries.findRuntimeUnitType(rules, target);
        const WeaponType* weapon = nullptr;
        const WarheadType* warhead = nullptr;
        const ProjectileType* projectileRules = nullptr;
        const bool infantryTarget = target.kind
            == WorldObjectKind::Infantry;
        const bool vehicleTarget = target.kind == WorldObjectKind::Unit
            && !target.authoredAircraft && !target.airborne;
        if (!strictParasiteRules(
                source,
                pending.weaponId,
                pending.warheadId,
                weapon,
                warhead,
                projectileRules
            )) {
            return fail("pending launch has an unsafe rules combination");
        }
        const int strictInterval = runtimeParasiteRuleFramesToTicks(
            sourceType != nullptr && sourceType->organic
                ? 36
                : std::max(1, weapon->rof),
            fixedTickHz
        );
        const bool expectedParasiteable = infantryTarget
            || (vehicleTarget && targetType != nullptr
                && targetType->parasiteable);
        const int expectedCullingThreshold = source.veterancy >= 2
            ? eliteThreshold
            : normalThreshold;
        if (sourceType == nullptr || targetType == nullptr
            || source.alive || source.health <= 0
            || source.mission != "ParasiteLimbo"
            || !target.alive || target.health <= 0
            || (!infantryTarget && !vehicleTarget)
            || !expectedParasiteable
            || hasExclusiveAuthority(source.instanceId, true)
            || hasExclusiveAuthority(target.instanceId, false, true)
            || !sameText(source.owner, pending.owner)
            || !sameText(source.type, pending.sourceTypeId)
            || !sameText(target.type, pending.targetTypeId)
            || pending.targetKind != (infantryTarget
                ? ParasiteTargetKind::Infantry
                : ParasiteTargetKind::Vehicle)
            || pending.launchedTick < 0
            || pending.launchedTick > savedTick
            || pending.damage != std::max(0, weapon->damage)
            || pending.damageIntervalTicks != strictInterval
            || pending.paralyzes != std::max(0, warhead->paralyzes)
            || pending.targetMaximumHealth
                != std::max(1, targetType->strength)
            || pending.targetParasiteable != expectedParasiteable
            || pending.organic != sourceType->organic
            || pending.sourceElite != (source.veterancy >= 2)
            || pending.culling != warhead->culling
            || pending.cullingThresholdPerMyriad
                != expectedCullingThreshold) {
            return fail("pending launch rules or world snapshot diverged");
        }
        if (projectile.actorId != projectileId
            || projectile.parentActorId != -1
            || projectile.generation != 0
            || projectile.attackerObjectId != source.instanceId
            || projectile.targetObjectId != target.instanceId
            || projectile.sourceAnchorObjectId != -1
            || !sameText(projectile.owner, pending.owner)
            || !sameText(projectile.weaponId, pending.weaponId)
            || !sameText(projectile.warheadId, pending.warheadId)
            || !sameText(projectile.projectileId, weapon->projectile)
            || projectile.spawnTick != pending.launchedTick
            || projectile.damage != pending.damage
            || projectile.spawnedMissileDamage
            || projectile.shrapnel || projectile.shrapnelReleased
            || projectile.deathWeaponCascadeId != 0
            || projectile.deathWeaponRequestId != 0
            || projectile.deathWeaponCascadeDepth != 0
            || projectile.airstrikeStrikeId != 0
            || projectile.airstrikePlaneToken != 0
            || projectile.particleDamagePulse
            || projectile.suppressWarheadParticleSpawn
            || !projectile.launchSourceValidated
            || projectile.launchSourceKind
                != static_cast<int>(source.kind)
            || projectile.launchSourceAuthoredAircraft
                != source.authoredAircraft
            || !sameText(projectile.launchSourceType, source.type)
            || (projectile.lifecycle
                    != RuntimeProjectileLifecycleState::Spawned
                && projectile.lifecycle
                    != RuntimeProjectileLifecycleState::Traveling)
            || projectile.aa != projectileRules->aa
            || projectile.ag != projectileRules->ag) {
            return fail("pending launch projectile identity diverged");
        }
        limboSourceIds.insert(source.instanceId);
    }

    for (const RuntimeProjectileEffect& projectile :
            savedProjectiles) {
        const WeaponType* weapon = queries.findRuntimeWeaponType(
            rules,
            projectile.weaponId
        );
        const WarheadType* warhead = queries.findRuntimeWarheadType(
            rules,
            projectile.warheadId
        );
        const bool parasiteDeclaration = (weapon != nullptr
                && weapon->limboLaunch)
            || (warhead != nullptr && warhead->parasite);
        if (parasiteDeclaration
            && queries.savedPendingParasiteLaunches().find(
                projectile.actorId
            ) == queries.savedPendingParasiteLaunches().end()) {
            return fail("parasite projectile has no pending authority");
        }
    }

    std::unordered_set<int> paralyzedLinkTargetIds;
    for (const auto& [targetId, link] :
            queries.savedParasiteLinks()) {
        const auto sourceIt = objectsById.find(link.sourceObjectId);
        const auto targetIt = objectsById.find(targetId);
        if (link.projectileId <= 0
            || link.projectileId >= savedNextProjectileActorId
            || sourceIt == objectsById.end()
            || targetIt == objectsById.end()
            || sourceIt->second == nullptr || targetIt->second == nullptr
            || projectilesById.contains(link.projectileId)) {
            return fail("attached parasite has conflicting causal identity");
        }
        const WorldObjectInstance& source = *sourceIt->second;
        const WorldObjectInstance& target = *targetIt->second;
        const UnitType* sourceType = queries.findRuntimeUnitType(rules, source);
        const UnitType* targetType = queries.findRuntimeUnitType(rules, target);
        const WeaponType* weapon = nullptr;
        const WarheadType* warhead = nullptr;
        const ProjectileType* projectileRules = nullptr;
        if (!strictParasiteRules(
                source,
                link.weaponId,
                link.warheadId,
                weapon,
                warhead,
                projectileRules
            )) {
            return fail("attached parasite has an unsafe rules combination");
        }
        (void)projectileRules;
        const int expectedInterval = runtimeParasiteRuleFramesToTicks(
            sourceType != nullptr && sourceType->organic
                ? 36
                : std::max(1, weapon->rof),
            fixedTickHz
        );
        const int expectedThreshold = source.veterancy >= 2
            ? eliteThreshold
            : normalThreshold;
        if (sourceType == nullptr || targetType == nullptr
            || source.alive || source.health <= 0
            || source.mission != "ParasiteLimbo"
            || !target.alive || target.health <= 0
            || target.kind != WorldObjectKind::Unit
            || target.authoredAircraft || target.airborne
            || !targetType->parasiteable
            || hasExclusiveAuthority(source.instanceId, true)
            || (hasExclusiveAuthority(target.instanceId, true, true)
                && !(!sourceType->organic && !link.organic && link.paralyzes == 0
                    && mobileParasiteHostAuthorityIsSafe(target)))
            || link.targetObjectId != targetId
            || !sameText(source.owner, link.owner)
            || !sameText(source.type, link.sourceTypeId)
            || !sameText(target.type, link.targetTypeId)
            || link.damage != std::max(0, weapon->damage)
            || link.damageIntervalTicks != expectedInterval
            || link.paralyzes != std::max(0, warhead->paralyzes)
            || link.targetMaximumHealth
                != std::max(1, targetType->strength)
            || link.organic != sourceType->organic
            || link.sourceElite != (source.veterancy >= 2)
            || link.culling != warhead->culling
            || link.cullingThresholdPerMyriad != expectedThreshold
            || !link.sourceExpectedInLimbo
            || link.attachedTick < 0 || link.attachedTick > savedTick
            || link.nextDamageTick <= savedTick
            || static_cast<long long>(link.nextDamageTick)
                    - static_cast<long long>(savedTick)
                > link.damageIntervalTicks
            || ((link.paralyzes > 0)
                != (target.mission == "ParasiteParalyzed"))) {
            return fail("attached parasite rules or world snapshot diverged");
        }
        if (link.paralyzes > 0) {
            paralyzedLinkTargetIds.insert(targetId);
        }
        limboSourceIds.insert(source.instanceId);
    }
    for (const WorldObjectInstance& object : completeObjects) {
        const bool limboMission = object.mission == "ParasiteLimbo";
        if (limboMission != limboSourceIds.contains(object.instanceId)
            || (limboMission && (object.alive || object.health <= 0))) {
            return fail("limbo source mission lacks an exact authority");
        }
        if ((object.mission == "ParasiteParalyzed")
            != paralyzedLinkTargetIds.contains(object.instanceId)) {
            return fail("paralyzed host mission lacks an exact link");
        }
    }

    int resolvedCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("SpecialParasiteResolved", "Count", ""),
            resolvedCount
        )
        || resolvedCount < 0 || resolvedCount > 4096) {
        return fail("resolved receipt header is malformed");
    }
    struct SavedParasiteResolvedReceipt {
        int sourceId = -1;
        int targetId = -1;
        int resolvedTick = -1;
        bool hostTerminated = false;
    };
    std::unordered_map<int, SavedParasiteResolvedReceipt>
        resolvedReceiptsByProjectile;
    resolvedReceiptsByProjectile.reserve(
        static_cast<std::size_t>(resolvedCount)
    );
    std::unordered_set<int> resolvedProjectileIds;
    for (int index = 0; index < resolvedCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue(
                "SpecialParasiteResolved",
                std::to_string(index),
                ""
            )
        );
        int projectileId = -1;
        int sourceId = -1;
        int targetId = -1;
        int resolvedTick = -1;
        bool hostTerminated = false;
        if (tokens.size() != 5U
            || !parseRuntimeInt(tokens[0], projectileId)
            || !parseRuntimeInt(tokens[1], sourceId)
            || !parseRuntimeInt(tokens[2], targetId)
            || !parseRuntimeInt(tokens[3], resolvedTick)
            || !queries.parseRuntimeStrictBool(tokens[4], hostTerminated)
            || projectileId <= 0
            || projectileId >= savedNextProjectileActorId
            || sourceId < 0 || targetId < 0 || sourceId == targetId
            || resolvedTick < 0 || resolvedTick > savedTick
            || !resolvedProjectileIds.insert(projectileId).second
            || projectilesById.contains(projectileId)
            || queries.savedPendingParasiteLaunches().contains(
                projectileId
            )) {
            return fail("resolved receipt identity is malformed");
        }
        const auto source = objectsById.find(sourceId);
        const auto target = objectsById.find(targetId);
        if (source == objectsById.end() || target == objectsById.end()
            || source->second == nullptr || target->second == nullptr) {
            return fail("resolved receipt object identity is missing");
        }
        const auto link = queries.savedParasiteLinks().find(
            targetId
        );
        if (link != queries.savedParasiteLinks().end()) {
            if (hostTerminated
                || link->second.projectileId != projectileId
                || link->second.sourceObjectId != sourceId
                || link->second.targetObjectId != targetId
                || resolvedTick != link->second.attachedTick) {
                return fail("resolved receipt conflicts with attached link");
            }
        } else if (source->second->mission == "ParasiteLimbo"
            && !limboSourceIds.contains(sourceId)) {
            return fail("terminal receipt leaves an orphaned limbo source");
        }
        resolvedReceiptsByProjectile.emplace(
            projectileId,
            SavedParasiteResolvedReceipt{
                sourceId,
                targetId,
                resolvedTick,
                hostTerminated
            }
        );
    }
    for (const auto& [targetId, link] :
            queries.savedParasiteLinks()) {
        if (link.attachedTick != savedTick) {
            continue;
        }
        const auto receipt = resolvedReceiptsByProjectile.find(
            link.projectileId
        );
        if (receipt == resolvedReceiptsByProjectile.end()
            || receipt->second.sourceId != link.sourceObjectId
            || receipt->second.targetId != targetId
            || receipt->second.resolvedTick != link.attachedTick
            || receipt->second.hostTerminated) {
            return fail("new attached link lacks its terminal receipt");
        }
    }

    const bool hasStunSection = ini.getSection("ParasiteStuns") != nullptr;
    if (hasStunSection) {
        int schema = -1;
        int count = -1;
        if (!parseRuntimeInt(
                ini.getValue("ParasiteStuns", "Schema", ""),
                schema
            )
            || schema != 1
            || !parseRuntimeInt(
                ini.getValue("ParasiteStuns", "Count", ""),
                count
            )
            || count < 0 || count > 4096) {
            return fail("stun receipt header is malformed");
        }
        for (int index = 0; index < count; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue("ParasiteStuns", std::to_string(index), "")
            );
            int objectId = -1;
            int untilTick = -1;
            const auto pendingForObject = [
                &queries,
                &objectId
            ]() {
                for (const auto& [unusedId, pending] :
                        queries.savedPendingParasiteLaunches()) {
                    (void)unusedId;
                    if (pending.sourceObjectId == objectId
                        || pending.targetObjectId == objectId) {
                        return true;
                    }
                }
                for (const auto& [unusedId, link] :
                        queries.savedParasiteLinks()) {
                    (void)unusedId;
                    if (link.sourceObjectId == objectId
                        || link.targetObjectId == objectId) {
                        return true;
                    }
                }
                return false;
            };
            if (tokens.size() != 2U
                || !parseRuntimeInt(tokens[0], objectId)
                || !parseRuntimeInt(tokens[1], untilTick)
                || objectId < 0 || untilTick <= savedTick
                || static_cast<long long>(untilTick)
                        - static_cast<long long>(savedTick)
                    > 1000000LL
                || savedStuns.contains(objectId)) {
                return fail("stun receipt identity is malformed");
            }
            const auto object = objectsById.find(objectId);
            if (object == objectsById.end() || object->second == nullptr
                || !object->second->alive || object->second->health <= 0
                || object->second->mission != "ParasiteStunned"
                || hasExclusiveAuthority(objectId, true)
                || pendingForObject()) {
                return fail("stun receipt conflicts with world authority");
            }
            savedStuns.emplace(objectId, untilTick);
        }
    }
    for (const WorldObjectInstance& object : completeObjects) {
        if ((object.mission == "ParasiteStunned")
            != savedStuns.contains(object.instanceId)) {
            return fail("stun receipt/world mission is not bidirectional");
        }
    }
    return true;
}

}
