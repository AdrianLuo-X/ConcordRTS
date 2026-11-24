#pragma once
#include "simulation/abilities/parasites/RuntimeParasiteActionPolicy.h"
#include "simulation/abilities/parasites/RuntimeParasiteHostDeathPolicy.h"
#include "simulation/abilities/parasites/RuntimeParasiteStunState.h"
#include "simulation/abilities/parasites/RuntimeParasiteActionResult.h"
#include "simulation/lifecycle/RuntimeObjectExitValues.h"
#include "simulation/projectiles/RuntimeProjectileValues.h"
#include "world/model/GameWorld.h"
#include <algorithm>
#include <cstddef>
namespace concord::simulation::abilities {
using concord::runtime::session_detail::RuntimeParasiteActionApplyResult;
using concord::runtime::session_detail::RuntimeObjectExitRequest;
using concord::runtime::session_detail::RuntimeObjectExitKind;
using concord::runtime::session_detail::RuntimeObjectExitQueueResult;
using concord::runtime::session_detail::RuntimeProjectileEffect;
// Synchronous primitive operations only; no action-dispatch callback or host state API.
// The three noexcept properties expose live borrows and must have no side effects.
class RuntimeParasiteAtomicPorts : public RuntimeParasiteActionQueries, public RuntimeParasiteHostDeathQueries {
public:
    virtual const WorldObjectInstance* objectIncludingInactive(int id) const override = 0;
    virtual std::optional<RuntimeCellPoint> placementCell(const WorldObjectInstance&, const SpecialCell&,
        const std::unordered_set<int>*, int) const override = 0;
    virtual const int& liveTick() const noexcept = 0;
    virtual const std::unordered_set<int>& pendingExitIds() const noexcept = 0;
    virtual RuntimeParasiteStunState& stuns() noexcept = 0;
    virtual const std::vector<RuntimeProjectileEffect>& projectiles() const noexcept = 0;
    virtual const std::string& localOwner() const noexcept = 0;
    virtual void setStatus(const char*) = 0;
    virtual WorldObjectInstance* mutableObjectIncludingInactive(int id) = 0;
    virtual bool selected(int id) const = 0;
    virtual std::size_t eraseReselect(int id) = 0;
    virtual void insertReselect(int id) = 0;
    virtual bool reselectsAfterLimbo(const WorldObjectInstance&) const = 0;
    virtual void removeSelection(int id) = 0;
    virtual bool rejoinsTeamAfterLimbo(const WorldObjectInstance&) const = 0;
    virtual void detachFromTeam(int id) = 0;
    virtual void eraseBehavior(int id) = 0;
    virtual void clearRenderPosition(WorldObjectInstance&) = 0;
    virtual void cancelPendingOccupy(int id, const std::string& reason) = 0;
    virtual void activateObject(WorldObjectInstance&) = 0;
    virtual void setObjectCell(WorldObjectInstance&, const RuntimeCellPoint&, bool) = 0;
    virtual void assignInfantrySubcell(WorldObjectInstance&) = 0;
    virtual WorldObjectCellOffset subcellOffset(const WorldObjectInstance&) const noexcept = 0;
    virtual double renderX(const WorldObjectInstance&) const = 0;
    virtual double renderY(const WorldObjectInstance&) const = 0;
    virtual double drawLevel(const WorldObjectInstance&) const = 0;
    virtual bool ownersAllied(const std::string&, const std::string&) const = 0;
    virtual void addSelection(int id) = 0;
    virtual RuntimeObjectExitQueueResult queueExit(RuntimeObjectExitRequest&&) = 0;
    virtual RuntimeSpatialAudioPosition spatialPosition(const WorldObjectInstance&) const = 0;
    virtual bool projectileIsParasiteLeap(const RuntimeProjectileEffect&) const = 0;
    virtual void invalidateTickOccupancy() = 0;
    virtual void invalidateObjectProjection() = 0;
    virtual void requestNavigationRefresh() = 0;
};
class RuntimeParasiteAtomicRejection final : public RuntimeParasiteHostDeathRejection {
    RuntimeParasiteAtomicPorts& ports_;
    RuntimeParasiteActionApplyResult& result_;
public:
    RuntimeParasiteAtomicRejection(RuntimeParasiteAtomicPorts& ports, RuntimeParasiteActionApplyResult& result) noexcept : ports_(ports), result_(result) {}
    void rejectAtomically() override { result_.accepted=false; ports_.setStatus("parasite-caused host death rejected atomically"); }
};
// Factory returns an owned complete original mechanism candidate by guaranteed elision.
// copyCandidate is called only at the original post-validation point. commitCandidate
// performs only the original move, retaining the moved-from owned candidate to scope end.
template<class OwnedCandidateFactory>
RuntimeParasiteActionApplyResult applyRuntimeParasiteActionsAtomically(
    RuntimeParasiteAtomicPorts& ports, OwnedCandidateFactory& factory,
    const std::vector<SpecialMechanismAction>& actions,
    const RuntimeDeathWeaponCause& inheritedDeathWeaponCause,
    int fallbackSourceObjectId,
    bool suppressUnitDeathSounds) {
    RuntimeParasiteActionApplyResult result;
    if (actions.empty()) {
        return result;
    }
    const std::vector<SpecialMechanismAction> prepared =
        prepareRuntimeParasiteActions(actions, ports);
    if (!validateRuntimeParasiteActionBatch(prepared, ports.pendingExitIds(), ports)) {
        result.accepted = false;
        ports.setStatus("parasite action batch rejected atomically");
        return result;
    }

    auto parasiteDeathCandidate = factory.copyCandidate();
    std::vector<SpecialMechanismAction> parasiteDeathActions;
    bool hasParasiteCausedHostDeath = false;
    RuntimeParasiteAtomicRejection hostDeathRejection(ports, result);
    if (!concord::simulation::abilities::collectRuntimeParasiteHostDeathActions(
            prepared, ports.liveTick(), ports, parasiteDeathCandidate,
            parasiteDeathActions, hasParasiteCausedHostDeath, hostDeathRejection)) {
        return result;
    }
    if (hasParasiteCausedHostDeath) {
        parasiteDeathActions = prepareRuntimeParasiteActions(parasiteDeathActions, ports);
        std::vector<SpecialMechanismAction> combined = prepared;
        combined.insert(
            combined.end(),
            parasiteDeathActions.begin(),
            parasiteDeathActions.end()
        );
        if (!validateRuntimeParasiteActionBatch(combined, ports.pendingExitIds(), ports)) {
            result.accepted = false;
            ports.setStatus("parasite death/eject batch rejected atomically");
            return result;
        }
        factory.commitCandidate(parasiteDeathCandidate);
        const RuntimeParasiteActionApplyResult ejected =
            applyRuntimeParasiteActionsAtomically(
                ports, factory,
                parasiteDeathActions,
                inheritedDeathWeaponCause,
                fallbackSourceObjectId,
                suppressUnitDeathSounds
            );
        if (!ejected.accepted) {
            result.accepted = false;
            return result;
        }
        result.changed = ejected.changed;
    }

    for (const SpecialMechanismAction& action : prepared) {
        if (action.kind == SpecialMechanismActionKind::TimedBombDetonate) {
            continue;
        }
        WorldObjectInstance* source =
            ports.mutableObjectIncludingInactive(action.objectId
            );
        WorldObjectInstance* target =
            ports.mutableObjectIncludingInactive(action.targetObjectId
            );
        switch (action.kind) {
        case SpecialMechanismActionKind::ParasiteEnterLimbo: {
            const bool wasSelected = ports.selected(action.objectId
            );
            ports.eraseReselect(
                action.objectId
            );
            if (wasSelected
                && ports.reselectsAfterLimbo(*source
                )) {
                ports.insertReselect(
                    action.objectId
                );
            }
            ports.removeSelection(action.objectId);
            if (!ports.rejoinsTeamAfterLimbo(*source
                )) {
                ports.detachFromTeam(action.objectId
                );
            }
            ports.eraseBehavior(action.objectId);
            ports.clearRenderPosition(*source);
            source->alive = false;
            source->mission = "ParasiteLimbo";
            source->subCell = -1;
            source->dockedToObjectId = -1;
            source->dockIndex = -1;
            result.changed = true;
            break;
        }
        case SpecialMechanismActionKind::ParasiteAttachHost:
            ports.eraseBehavior(action.targetObjectId);
            ports.cancelPendingOccupy(action.targetObjectId,
                "parasite attached"
            );
            target->mission = action.paralyzes > 0
                ? "ParasiteParalyzed"
                : target->mission;
            result.changed = true;
            break;
        case SpecialMechanismActionKind::ParasiteReturnFromLimbo:
        case SpecialMechanismActionKind::ParasiteEject: {
            const bool reselect =
                ports.eraseReselect(
                    action.objectId
                ) > 0;
            ports.activateObject(*source);
            source->health = std::max(1, source->health);
            ports.setObjectCell(*source,
                {action.cell.x, action.cell.y},
                false
            );
            source->mission = action.stunTicks > 0
                ? "ParasiteStunned"
                : "Guard";
            ports.assignInfantrySubcell(*source);
            if (action.replacesTerminatingHost && target != nullptr
                && action.cell.x == target->x && action.cell.y == target->y) {
                // The host has not been drained yet. Preserve its last visible
                // feet, including subcells and an in-progress slope/bridge pose.
                const auto hostSubcell = ports.subcellOffset(*target);
                const auto sourceSubcell = ports.subcellOffset(*source);
                const bool hostHasContinuousLevel = target->renderMoveActive
                    || worldObjectHasStoppedGroundPose(*target);
                source->level = target->level;
                source->onBridge = target->onBridge;
                source->hasRenderPosition = true;
                source->renderMoveActive = false;
                source->renderX = ports.renderX(*target)
                    + hostSubcell.x - sourceSubcell.x;
                source->renderY = ports.renderY(*target)
                    + hostSubcell.y - sourceSubcell.y;
                source->renderLevel = ports.drawLevel(*target);
                source->renderOnBridge = hostHasContinuousLevel
                    ? target->renderOnBridge : target->onBridge;
            }
            ports.stuns().applyEjectionStun(*source, ports.liveTick(), action);
            if (reselect
                && ports.ownersAllied(ports.localOwner(),
                    source->owner
                )) {
                ports.addSelection(source->instanceId);
            }
            if (action.kind == SpecialMechanismActionKind::ParasiteEject
                && target != nullptr && target->alive
                && target->mission == "ParasiteParalyzed") {
                target->mission = "Guard";
            }
            result.changed = true;
            break;
        }
        case SpecialMechanismActionKind::ParasiteDamage:
            if (target != nullptr && !ports.damageIsTypeImmune(*target, action.objectId, action.damage)) {
                const int appliedDamage = ports.authoredDamage(*target,
                    action.damage,
                    action.warheadId,
                    0.0,
                    1.0
                );
                target->health = std::max(0, target->health - appliedDamage);
                result.changed = appliedDamage > 0 || result.changed;
                if (target->health <= 0) {
                    RuntimeObjectExitRequest targetExit;
                    targetExit.objectId = target->instanceId;
                    targetExit.kind = RuntimeObjectExitKind::Destroyed;
                    targetExit.tick = action.tick;
                    targetExit.mission = "Destroyed";
                    targetExit.reason = action.reason.empty()
                        ? "parasite bite was lethal"
                        : action.reason;
                    targetExit.sourceObjectId = action.objectId;
                    targetExit.sourceOwner = action.owner;
                    targetExit.deathWeaponCause =
                        inheritedDeathWeaponCause;
                    (void)ports.queueExit(std::move(targetExit)
                    );
                }
            }
            break;
        case SpecialMechanismActionKind::ParasiteKillTarget:
        case SpecialMechanismActionKind::ParasiteKillParasite: {
            const bool killSource = action.kind
                == SpecialMechanismActionKind::ParasiteKillParasite;
            if (killSource) {
                ports.eraseReselect(
                    action.objectId
                );
            }
            if (killSource && target != nullptr && target->alive
                && target->mission == "ParasiteParalyzed") {
                target->mission = "Guard";
                result.changed = true;
            }
            const int objectId = killSource
                ? action.objectId
                : action.targetObjectId;
            const WorldObjectInstance* killed =
                ports.objectIncludingInactive(objectId);
            if (killed == nullptr || killed->health <= 0) {
                break;
            }
            RuntimeObjectExitRequest dependentExit;
            dependentExit.objectId = objectId;
            dependentExit.kind = RuntimeObjectExitKind::Destroyed;
            dependentExit.tick = action.tick;
            dependentExit.mission = "Destroyed";
            dependentExit.reason = action.reason;
            dependentExit.sourceObjectId = fallbackSourceObjectId >= 0
                ? fallbackSourceObjectId
                : action.objectId;
            dependentExit.sourceOwner = action.owner;
            dependentExit.deathWeaponCause = inheritedDeathWeaponCause;
            dependentExit.suppressDeathWeapon =
                action.suppressDeathWeapon;
            dependentExit.unitDeathSoundHandled = suppressUnitDeathSounds;
            if (killSource && action.sourceWasAttachedToHost
                && target != nullptr) {
                // Attached parasites retain their old launch cell in limbo.
                // Capture the current host before its exit clears that pose.
                dependentExit.unitDeathSoundPosition =
                    ports.spatialPosition(*target);
            } else if (killSource && !action.sourceWasAttachedToHost) {
                const auto actor = std::find_if(ports.projectiles().begin(), ports.projectiles().end(),
                    [&](const RuntimeProjectileEffect& projectile) {
                        return projectile.actorId == action.projectileId
                            && projectile.attackerObjectId == objectId
                            && ports.projectileIsParasiteLeap(projectile);
                    });
                if (actor != ports.projectiles().end()) {
                    dependentExit.unitDeathSoundPosition = RuntimeSpatialAudioPosition{
                        actor->currentX, actor->currentY, actor->currentZ};
                }
            }
            result.changed = ports.queueExit(std::move(dependentExit)
            ) == RuntimeObjectExitQueueResult::Queued || result.changed;
            break;
        }
        case SpecialMechanismActionKind::ParasiteReleaseTarget:
            if (target != nullptr && target->alive
                && target->mission == "ParasiteParalyzed") {
                target->mission = "Guard";
                result.changed = true;
            }
            break;
        case SpecialMechanismActionKind::TimedBombDetonate:
            break;
        }
    }
    if (result.changed) {
        ports.invalidateTickOccupancy();
        ports.invalidateObjectProjection();
        ports.requestNavigationRefresh();
    }
    return result;
}

}
