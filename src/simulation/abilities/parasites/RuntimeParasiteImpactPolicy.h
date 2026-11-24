#pragma once
#include "simulation/abilities/parasites/RuntimeParasiteAdmissionPolicy.h"
#include "simulation/abilities/parasites/RuntimeParasiteAtomicPolicy.h"
#include "data/rules/RulesDatabase.h"
#include <map>
namespace concord::simulation::abilities {
class RuntimeParasiteImpactQueries {
public:
 virtual ~RuntimeParasiteImpactQueries() = default;
 virtual const std::map<int, ParasitePendingLaunchState>& pendingLaunches() const = 0;
 virtual const WorldObjectInstance* findObject(int) const = 0;
 virtual const WarheadType* warheadType(const std::string&) const = 0;
 virtual const WeaponType* weaponType(const std::string&) const = 0;
 virtual const UnitType* unitType(const WorldObjectInstance&) const = 0;
 virtual bool projectileCanHitTarget(const RuntimeProjectileEffect&, const WorldObjectInstance&) const = 0;
 virtual std::string targetArmor(const WorldObjectInstance&) const = 0;
 virtual double warheadArmorMultiplier(const WarheadType*, const std::string&) const = 0;
 virtual const RuntimeParasiteAdmissionQueries& admission() const noexcept = 0;
 virtual bool superWeaponsInitialized() const noexcept = 0;
 virtual bool isObjectProtected(int, int) const = 0;
 virtual RuntimeDeathWeaponCause deathWeaponCause(const RuntimeProjectileEffect&) const = 0;
 virtual void recordReplay(const RuntimeProjectileEffect&, const std::string&) = 0;
 virtual int& impactsThisTick() noexcept = 0;
 virtual void drainObjectExitQueue() = 0;
 virtual std::string& status() noexcept = 0;
};
template<class OwnedCandidateFactory>
bool applyRuntimeParasiteProjectileImpactExclusive(
    RuntimeParasiteImpactQueries& queries,
    RuntimeParasiteAtomicPorts& atomicPorts, OwnedCandidateFactory& factory,
    const RuntimeProjectileEffect& projectile) {
    const auto pendingIt =
        queries.pendingLaunches().find(
            projectile.actorId
        );
    if (pendingIt
        == queries.pendingLaunches().end()) {
        return false;
    }
    const ParasitePendingLaunchState pending = pendingIt->second;
    const WorldObjectInstance* source =
        atomicPorts.objectIncludingInactive(
            pending.sourceObjectId
        );
    const WorldObjectInstance* target = queries.findObject(
        projectile.targetObjectId
    );
    const WarheadType* warhead =
        queries.warheadType(projectile.warheadId);
    const WeaponType* weapon =
        queries.weaponType(projectile.weaponId);
    const UnitType* targetType = target == nullptr
        ? nullptr
        : queries.unitType(*target);
    const bool targetKindMatches = target != nullptr
        && ((pending.targetKind == ParasiteTargetKind::Infantry
                && target->kind == WorldObjectKind::Infantry)
            || (pending.targetKind == ParasiteTargetKind::Vehicle
                && target->kind == WorldObjectKind::Unit
                && !target->authoredAircraft && !target->airborne));
    const bool causalIdentityMatches = source != nullptr
        && source->health > 0 && !source->alive
        && source->mission == "ParasiteLimbo"
        && source->type == pending.sourceTypeId
        && projectile.attackerObjectId == pending.sourceObjectId
        && projectile.targetObjectId == pending.targetObjectId
        && projectile.owner == pending.owner
        && projectile.weaponId == pending.weaponId
        && projectile.warheadId == pending.warheadId;
    const bool strictWeapon = weapon != nullptr && warhead != nullptr
        && runtimeParasiteWarheadIsExclusive(*weapon, *warhead);
    const bool legalHit = causalIdentityMatches && strictWeapon
        && target != nullptr && target->alive && target->health > 0
        && target->type == pending.targetTypeId && targetKindMatches
        && targetType != nullptr
        && (pending.targetKind == ParasiteTargetKind::Infantry
            || targetType->parasiteable)
        && queries.projectileCanHitTarget(projectile, *target)
        && queries.warheadArmorMultiplier(
            warhead,
            queries.targetArmor(*target)
        ) > 0.0
        && !runtimeParasiteObjectHasExclusiveAuthority(queries.admission(), *target, true)
        && (!queries.superWeaponsInitialized()
            || !queries.isObjectProtected(
                target->instanceId,
                atomicPorts.liveTick()
            ));

    const RuntimeCellPoint impactCell = target != nullptr && target->alive
        ? atomicPorts.objectCell(*target)
        : projectile.toCell;
    const std::optional<RuntimeCellPoint> returnCell = source == nullptr
        ? std::nullopt
        : atomicPorts.placementCell(
            *source,
            {impactCell.x, impactCell.y},
            nullptr,
            legalHit && pending.targetKind == ParasiteTargetKind::Infantry
                ? target->instanceId : -1
        );
    ParasiteImpactRequest request;
    request.projectileId = projectile.actorId;
    request.currentTick = atomicPorts.liveTick();
    request.impact = legalHit
        ? ParasiteImpactKind::Hit
        : ParasiteImpactKind::Miss;
    request.targetObjectId = target == nullptr
        ? pending.targetObjectId
        : target->instanceId;
    request.targetTypeId = target == nullptr
        ? pending.targetTypeId
        : target->type;
    request.targetKind = pending.targetKind;
    request.targetPresent = target != nullptr;
    request.targetAlive = target != nullptr && target->alive
        && target->health > 0;
    request.targetParasiteable = targetType != nullptr
        && (target->kind == WorldObjectKind::Infantry
            || targetType->parasiteable);
    request.targetMaximumHealth = targetType == nullptr
        ? std::max(1, pending.targetMaximumHealth)
        : std::max(1, targetType->strength);
    request.impactCell = {impactCell.x, impactCell.y};
    request.returnCellAvailable = returnCell.has_value();
    if (returnCell.has_value()) {
        request.returnCell = {returnCell->x, returnCell->y};
    }

    auto specialCandidate = factory.copyCandidate();
    const ParasiteOperationResult resolved =
        specialCandidate.resolveParasiteImpact(
            request,
            SpecialEventSink{}
        );
    const std::vector<SpecialMechanismAction> prepared =
        prepareRuntimeParasiteActions(resolved.actions, atomicPorts);
    if (!resolved.accepted
        || !validateRuntimeParasiteActionBatch(prepared, atomicPorts.pendingExitIds(), atomicPorts)) {
        queries.status() = resolved.reason.empty()
            ? "parasite impact action rejected atomically"
            : "parasite impact rejected: " + resolved.reason;
        return true;
    }

    factory.commitCandidate(specialCandidate);
    const RuntimeParasiteActionApplyResult applied =
        applyRuntimeParasiteActionsAtomically(
            atomicPorts, factory,
            prepared,
            queries.deathWeaponCause(projectile),
            pending.sourceObjectId, false
        );
    if (!applied.accepted) {
        queries.status() =
            "parasite impact commit rejected unexpectedly";
        return true;
    }
    RuntimeProjectileEffect relationshipImpact = projectile;
    relationshipImpact.impactAnimId.clear();
    relationshipImpact.particleId.clear();
    relationshipImpact.damage = 0;
    relationshipImpact.cellSpread = 0.0;
    relationshipImpact.bright = false;
    relationshipImpact.reportSoundId.clear();
    queries.recordReplay(
        relationshipImpact,
        "ProjectileImpact"
    );
    ++queries.impactsThisTick();
    queries.drainObjectExitQueue();
    queries.status() = legalHit
        ? "parasite impact accepted"
        : "parasite impact resolved as miss";
    return true;
}

}
