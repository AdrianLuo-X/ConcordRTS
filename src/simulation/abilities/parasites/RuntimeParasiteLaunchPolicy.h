#pragma once
#include "simulation/abilities/parasites/RuntimeParasiteAdmissionPolicy.h"
#include "simulation/abilities/parasites/RuntimeParasiteAtomicPolicy.h"
#include "data/rules/RulesDatabase.h"
#include "core/io/RuntimeSaveTextFields.h"
#include <algorithm>
#include <cmath>
#include <string>
namespace concord::simulation::abilities {
// Synchronous borrows and primitive canonical lookups/spawning only.
// No private weapon profile, host state, mechanism aggregate or retained index.
class RuntimeParasiteLaunchQueries {
public:
    virtual ~RuntimeParasiteLaunchQueries() = default;
    virtual const UnitType* unitType(const WorldObjectInstance&) const = 0;
    virtual const ProjectileType* projectileType(const std::string&) const = 0;
    virtual bool hasSpawnedMissileProfile(const WorldObjectInstance&, const WeaponType&) const = 0;
    virtual bool weaponCanTargetObject(const WeaponType&, const WorldObjectInstance&) const = 0;
    virtual bool superWeaponsInitialized() const noexcept = 0;
    virtual bool isObjectProtected(int objectId, int tick) const = 0;
    virtual const RuntimeParasiteAdmissionQueries& admission() const noexcept = 0;
    virtual int& nextActorId() noexcept = 0;
    virtual int allocateActorId() = 0;
    virtual const int& fixedTickHz() const noexcept = 0;
    virtual const double& conditionRed() const noexcept = 0;
    virtual const double& conditionYellow() const noexcept = 0;
    virtual std::string& status() noexcept = 0;
    virtual void spawnProjectile(const WorldObjectInstance&, const WorldObjectInstance&,
        const WeaponType&, int damage, int projectileActorId) = 0;
};
int runtimeParasiteRuleFramesToTicks(int frames, int fixedTickHz);
using runtime_save_text::trimAsciiLocal;
template<class OwnedCandidateFactory>
bool launchRuntimeParasiteProjectile(
    RuntimeParasiteLaunchQueries& queries,
    RuntimeParasiteAtomicPorts& atomicPorts, OwnedCandidateFactory& factory,
    const WorldObjectInstance& source, const WorldObjectInstance& target,
    const WeaponType& weapon, const WarheadType* warhead) {
    const UnitType* sourceType = queries.unitType(source);
    const UnitType* targetType = queries.unitType(target);
    const ProjectileType* projectileType =
        queries.projectileType(weapon.projectile);
    const bool validTargetKind = target.kind == WorldObjectKind::Infantry
        || (target.kind == WorldObjectKind::Unit
            && !target.authoredAircraft && !target.airborne);
    if (warhead == nullptr || sourceType == nullptr || targetType == nullptr
        || !runtimeParasiteWarheadIsExclusive(weapon, *warhead)
        || projectileType == nullptr
        || queries.hasSpawnedMissileProfile(source, weapon)
        || projectileType->airburst || projectileType->shrapnelCount > 0
        || !trimAsciiLocal(projectileType->airburstWeapon).empty()
        || !trimAsciiLocal(projectileType->shrapnelWeapon).empty()
        || !source.alive || source.health <= 0
        || !target.alive || target.health <= 0
        || source.instanceId == target.instanceId
        || !validTargetKind
        || (target.kind == WorldObjectKind::Unit
            && !targetType->parasiteable)
        || runtimeParasiteObjectHasExclusiveAuthority(queries.admission(), source)
        || runtimeParasiteObjectHasExclusiveAuthority(queries.admission(), target, true)
        || !queries.weaponCanTargetObject(weapon, target)
        || (queries.superWeaponsInitialized()
            && queries.isObjectProtected(
                target.instanceId,
                atomicPorts.liveTick()
            ))) {
        queries.status() = "parasite launch rejected by host";
        return false;
    }

    const int previousNextActorId = queries.nextActorId();
    const int projectileActorId = queries.allocateActorId();
    ParasiteLaunchRequest request;
    request.projectileId = projectileActorId;
    request.currentTick = atomicPorts.liveTick();
    request.sourceObjectId = source.instanceId;
    request.targetObjectId = target.instanceId;
    request.sourceOwner = source.owner;
    request.sourceTypeId = source.type;
    request.targetTypeId = target.type;
    request.weaponId = weapon.id;
    request.warheadId = warhead->id;
    request.targetKind = target.kind == WorldObjectKind::Infantry
        ? ParasiteTargetKind::Infantry
        : ParasiteTargetKind::Vehicle;
    const RuntimeCellPoint targetCell = atomicPorts.objectCell(target);
    request.targetCell = {targetCell.x, targetCell.y};
    request.damage = std::max(0, weapon.damage);
    request.sourceOrganic = sourceType->organic;
    request.sourceElite = source.veterancy >= 2;
    request.damageIntervalTicks = runtimeParasiteRuleFramesToTicks(
        request.sourceOrganic ? 36 : std::max(1, weapon.rof),
        queries.fixedTickHz()
    );
    request.paralyzes = std::max(0, warhead->paralyzes);
    request.targetMaximumHealth = std::max(1, targetType->strength);
    request.targetParasiteable = target.kind == WorldObjectKind::Infantry
        || targetType->parasiteable;
    request.culling = warhead->culling;
    request.normalCullingThresholdPermyriad =
        static_cast<int>(std::lround(
            std::clamp(
                queries.conditionRed(),
                0.0,
                1.0
            ) * 10000.0
        ));
    request.eliteCullingThresholdPermyriad =
        static_cast<int>(std::lround(
            std::clamp(
                queries.conditionYellow(),
                0.0,
                1.0
            ) * 10000.0
        ));

    auto specialCandidate = factory.copyCandidate();
    const ParasiteOperationResult began = specialCandidate.beginParasiteLaunch(
        request,
        SpecialEventSink{}
    );
    if (!began.accepted || began.idempotent || began.actions.size() != 1U
        || !validateRuntimeParasiteActionBatch(
            prepareRuntimeParasiteActions(began.actions, atomicPorts),
            atomicPorts.pendingExitIds(), atomicPorts
        )) {
        queries.nextActorId() = previousNextActorId;
        queries.status() = began.reason.empty()
            ? "parasite launch action rejected atomically"
            : "parasite launch rejected: " + began.reason;
        return false;
    }

    queries.spawnProjectile(
        source, target, weapon, request.damage, projectileActorId
    );
    factory.commitCandidate(specialCandidate);
    const RuntimeParasiteActionApplyResult applied =
        applyRuntimeParasiteActionsAtomically(
            atomicPorts, factory, began.actions, {}, source.instanceId, false
        );
    if (!applied.accepted) {
        queries.status() =
            "parasite launch commit rejected unexpectedly";
        return false;
    }
    queries.status() = "parasite launched #"
        + std::to_string(source.instanceId)
        + " -> #" + std::to_string(target.instanceId);
    return true;
}

}
