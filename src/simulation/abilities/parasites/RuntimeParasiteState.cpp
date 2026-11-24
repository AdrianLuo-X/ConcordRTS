#include "RuntimeParasiteState.h"
#include "world/model/GameWorld.h"
#include <algorithm>
#include <limits>
#include <set>
#include <utility>
namespace {
using namespace runtime_parasite_values_detail;
// The original null-to-default cell conversion, local to this state policy.
SpecialCell objectCell(const WorldObjectInstance* object) {
    return object == nullptr ? SpecialCell{} : SpecialCell{object->x, object->y};
}
std::string parasiteHostEventName(ParasiteHostEventKind event) {
    switch (event) {
    case ParasiteHostEventKind::Healed:
        return "healed";
    case ParasiteHostEventKind::SonicHit:
        return "sonic hit";
    case ParasiteHostEventKind::Teleporting:
        return "teleporting";
    case ParasiteHostEventKind::DestroyedExternally:
        return "destroyed externally";
    case ParasiteHostEventKind::DestroyedByParasite:
        return "destroyed by parasite";
    }
    return "unknown host event";
}
}
void RuntimeParasiteState::reset() {
    pendingByProjectile_.clear();
    byTarget_.clear();
    resolvedByProjectile_.clear();
}
void RuntimeParasiteState::swap(RuntimeParasiteState& other) noexcept {
    pendingByProjectile_.swap(other.pendingByProjectile_);
    byTarget_.swap(other.byTarget_);
    resolvedByProjectile_.swap(other.resolvedByProjectile_);
}
const std::map<int, ParasitePendingLaunchState>& RuntimeParasiteState::pendingParasiteLaunches() const { return pendingByProjectile_; }
const std::map<int, ParasiteLinkState>& RuntimeParasiteState::parasiteLinks() const { return byTarget_; }
const std::map<int, ParasiteResolvedLaunchState>& RuntimeParasiteState::resolvedParasiteLaunches() const { return resolvedByProjectile_; }

ParasiteOperationResult RuntimeParasiteState::beginParasiteLaunch(
    const ParasiteLaunchRequest& request,
    const SpecialEventSink& emit
) {
    ParasiteOperationResult result;
    const auto reject = [&](const std::string& reason) {
        result.reason = reason;
        if (emit != nullptr) {
            emit({
                "ParasiteLaunchRejected",
                request.currentTick,
                request.sourceObjectId,
                request.targetObjectId,
                request.projectileId,
                request.targetCell,
                reason
            });
        }
        return result;
    };
    for (auto resolved = resolvedByProjectile_.begin();
         resolved != resolvedByProjectile_.end();) {
        if (resolved->second.resolvedTick < request.currentTick) {
            resolved = resolvedByProjectile_.erase(resolved);
        } else {
            ++resolved;
        }
    }
    if (request.currentTick < 0 || request.projectileId == 0
        || request.sourceObjectId < 0 || request.targetObjectId < 0
        || request.sourceObjectId == request.targetObjectId) {
        return reject("invalid parasite launch identity");
    }
    if (!validParasiteText(request.sourceOwner)
        || !validParasiteText(request.sourceTypeId)
        || !validParasiteText(request.targetTypeId)
        || !validParasiteText(request.weaponId)
        || !validParasiteText(request.warheadId)) {
        return reject("invalid parasite launch text snapshot");
    }
    if (request.damage < 0 || request.damageIntervalTicks <= 0
        || request.paralyzes < 0 || request.targetMaximumHealth <= 0
        || !validParasiteRatio(request.normalCullingThresholdPermyriad)
        || !validParasiteRatio(request.eliteCullingThresholdPermyriad)) {
        return reject("invalid parasite launch rules snapshot");
    }
    if (request.targetKind == ParasiteTargetKind::Vehicle
        && !request.targetParasiteable) {
        return reject("vehicle target is not parasiteable");
    }
    const auto existingProjectile = pendingByProjectile_.find(
        request.projectileId
    );
    if (existingProjectile != pendingByProjectile_.end()) {
        const ParasitePendingLaunchState& pending = existingProjectile->second;
        if (pending.sourceObjectId == request.sourceObjectId
            && pending.targetObjectId == request.targetObjectId
            && pending.sourceTypeId == request.sourceTypeId
            && pending.targetTypeId == request.targetTypeId
            && pending.weaponId == request.weaponId
            && pending.warheadId == request.warheadId) {
            result.accepted = true;
            result.idempotent = true;
            return result;
        }
        return reject("projectile identity already belongs to another launch");
    }
    const auto resolved = resolvedByProjectile_.find(
        request.projectileId
    );
    if (resolved != resolvedByProjectile_.end()
        && resolved->second.resolvedTick == request.currentTick) {
        if (resolved->second.sourceObjectId == request.sourceObjectId
            && resolved->second.targetObjectId == request.targetObjectId) {
            result.accepted = true;
            result.idempotent = true;
            return result;
        }
        return reject("resolved projectile identity conflicts with launch");
    }
    if (pendingByProjectile_.size()
            + resolvedByProjectile_.size()
        >= kMaximumParasiteRecords) {
        return reject("parasite launch/resolution capacity reached");
    }
    for (const auto& [unusedProjectileId, pending]
         : pendingByProjectile_) {
        (void)unusedProjectileId;
        if (pending.sourceObjectId == request.sourceObjectId) {
            return reject("parasite source already owns an in-flight launch");
        }
        if (pending.targetObjectId == request.targetObjectId) {
            return reject("parasite target is already being boarded");
        }
    }
    for (const auto& [targetId, link] : byTarget_) {
        if (link.sourceObjectId == request.sourceObjectId) {
            return reject("parasite source is already attached");
        }
        if (targetId == request.targetObjectId) {
            return reject("parasite target already has a parasite");
        }
    }

    ParasitePendingLaunchState pending;
    pending.projectileId = request.projectileId;
    pending.sourceObjectId = request.sourceObjectId;
    pending.targetObjectId = request.targetObjectId;
    pending.owner = request.sourceOwner;
    pending.sourceTypeId = request.sourceTypeId;
    pending.targetTypeId = request.targetTypeId;
    pending.weaponId = request.weaponId;
    pending.warheadId = request.warheadId;
    pending.targetKind = request.targetKind;
    pending.targetCell = request.targetCell;
    pending.launchedTick = request.currentTick;
    pending.damage = request.damage;
    pending.damageIntervalTicks = request.damageIntervalTicks;
    pending.paralyzes = request.paralyzes;
    pending.targetMaximumHealth = request.targetMaximumHealth;
    pending.cullingThresholdPerMyriad = request.sourceElite
        ? request.eliteCullingThresholdPermyriad
        : request.normalCullingThresholdPermyriad;
    pending.targetParasiteable = request.targetParasiteable;
    pending.organic = request.sourceOrganic;
    pending.sourceElite = request.sourceElite;
    pending.culling = request.culling;
    pendingByProjectile_[pending.projectileId] = pending;

    SpecialMechanismAction enterLimbo;
    enterLimbo.kind = SpecialMechanismActionKind::ParasiteEnterLimbo;
    enterLimbo.tick = request.currentTick;
    enterLimbo.projectileId = request.projectileId;
    enterLimbo.objectId = request.sourceObjectId;
    enterLimbo.targetObjectId = request.targetObjectId;
    enterLimbo.owner = request.sourceOwner;
    enterLimbo.damage = request.damage;
    enterLimbo.warheadId = request.warheadId;
    enterLimbo.paralyzes = request.paralyzes;
    enterLimbo.culling = request.culling;
    enterLimbo.cell = request.targetCell;
    enterLimbo.reason = "LimboLaunch projectile began";
    result.actions.push_back(std::move(enterLimbo));
    result.accepted = true;
    if (emit != nullptr) {
        emit({
            "ParasiteLaunchBegan",
            request.currentTick,
            request.sourceObjectId,
            request.targetObjectId,
            request.projectileId,
            request.targetCell,
            request.weaponId
        });
    }
    return result;
}

ParasiteOperationResult RuntimeParasiteState::resolveParasiteImpact(
    const ParasiteImpactRequest& request,
    const SpecialEventSink& emit
) {
    ParasiteOperationResult result;
    const auto reject = [&](const std::string& reason) {
        result.reason = reason;
        return result;
    };
    for (auto resolved = resolvedByProjectile_.begin();
         resolved != resolvedByProjectile_.end();) {
        if (resolved->second.resolvedTick < request.currentTick) {
            resolved = resolvedByProjectile_.erase(resolved);
        } else {
            ++resolved;
        }
    }
    if (request.currentTick < 0 || request.projectileId == 0) {
        return reject("invalid parasite impact identity");
    }
    const auto pendingIt = pendingByProjectile_.find(
        request.projectileId
    );
    if (pendingIt == pendingByProjectile_.end()) {
        const auto resolved = resolvedByProjectile_.find(
            request.projectileId
        );
        if (resolved != resolvedByProjectile_.end()
            && resolved->second.resolvedTick == request.currentTick) {
            result.accepted = true;
            result.idempotent = true;
            return result;
        }
        return reject("parasite projectile has no pending launch");
    }
    const ParasitePendingLaunchState pending = pendingIt->second;
    if (request.currentTick < pending.launchedTick) {
        return reject("parasite impact precedes launch");
    }
    if (request.targetPresent
        && (!validParasiteText(request.targetTypeId)
            || request.targetMaximumHealth <= 0)) {
        return reject("invalid parasite impact target snapshot");
    }
    if (pending.damageIntervalTicks > 0
        && request.currentTick
            > std::numeric_limits<int>::max()
                - pending.damageIntervalTicks) {
        return reject("parasite impact tick would overflow bite interval");
    }

    const bool identityMatches = request.targetPresent
        && request.targetObjectId == pending.targetObjectId
        && request.targetTypeId == pending.targetTypeId
        && request.targetKind == pending.targetKind;
    const bool liveHit = request.impact == ParasiteImpactKind::Hit
        && identityMatches && request.targetAlive;
    const bool vehicleHit = liveHit
        && pending.targetKind == ParasiteTargetKind::Vehicle
        && pending.targetParasiteable && request.targetParasiteable;
    const bool infantryHit = liveHit
        && pending.targetKind == ParasiteTargetKind::Infantry;

    const auto appendReturnOrKill = [&](const std::string& reason,
                                      bool replacesTerminatingHost = false) {
        SpecialMechanismAction action;
        action.kind = request.returnCellAvailable
            ? SpecialMechanismActionKind::ParasiteReturnFromLimbo
            : SpecialMechanismActionKind::ParasiteKillParasite;
        action.tick = request.currentTick;
        action.projectileId = pending.projectileId;
        action.objectId = pending.sourceObjectId;
        action.targetObjectId = pending.targetObjectId;
        action.owner = pending.owner;
        action.paralyzes = pending.paralyzes;
        action.cell = request.returnCellAvailable
            ? request.returnCell
            : request.impactCell;
        action.suppressDeathWeapon = !request.returnCellAvailable;
        action.replacesTerminatingHost = request.returnCellAvailable
            && replacesTerminatingHost;
        action.reason = request.returnCellAvailable
            ? reason
            : reason + "; no legal return cell";
        result.actions.push_back(std::move(action));
    };

    if (vehicleHit) {
        if (byTarget_.size() >= kMaximumParasiteRecords
            || byTarget_.find(pending.targetObjectId)
                != byTarget_.end()) {
            return reject("parasite link capacity or target conflict");
        }
        ParasiteLinkState link;
        link.projectileId = pending.projectileId;
        link.targetObjectId = pending.targetObjectId;
        link.sourceObjectId = pending.sourceObjectId;
        link.owner = pending.owner;
        link.sourceTypeId = pending.sourceTypeId;
        link.targetTypeId = pending.targetTypeId;
        link.weaponId = pending.weaponId;
        link.warheadId = pending.warheadId;
        link.damage = pending.damage;
        link.attachedTick = request.currentTick;
        link.damageIntervalTicks = pending.damageIntervalTicks;
        link.nextDamageTick = pending.organic
            ? request.currentTick + pending.damageIntervalTicks
            : request.currentTick;
        link.organic = pending.organic;
        link.sourceElite = pending.sourceElite;
        link.paralyzes = pending.paralyzes;
        link.culling = pending.culling;
        link.cullingThresholdPerMyriad = pending.cullingThresholdPerMyriad;
        link.targetMaximumHealth = std::max(
            1,
            request.targetMaximumHealth
        );
        link.sourceExpectedInLimbo = true;
        byTarget_[link.targetObjectId] = link;

        SpecialMechanismAction attached;
        attached.kind = SpecialMechanismActionKind::ParasiteAttachHost;
        attached.tick = request.currentTick;
        attached.projectileId = pending.projectileId;
        attached.objectId = pending.sourceObjectId;
        attached.targetObjectId = pending.targetObjectId;
        attached.owner = pending.owner;
        attached.damage = pending.damage;
        attached.warheadId = pending.warheadId;
        attached.paralyzes = pending.paralyzes;
        attached.culling = pending.culling;
        attached.cell = request.impactCell;
        attached.reason = "LimboLaunch parasite attached to vehicle";
        result.actions.push_back(std::move(attached));
    } else if (infantryHit) {
        SpecialMechanismAction kill;
        kill.kind = SpecialMechanismActionKind::ParasiteKillTarget;
        kill.tick = request.currentTick;
        kill.projectileId = pending.projectileId;
        kill.objectId = pending.sourceObjectId;
        kill.targetObjectId = pending.targetObjectId;
        kill.owner = pending.owner;
        kill.damage = std::numeric_limits<int>::max();
        kill.warheadId = pending.warheadId;
        kill.forceDestruction = true;
        kill.cell = request.impactCell;
        kill.reason = "parasite warhead instantly kills infantry";
        result.actions.push_back(std::move(kill));
        appendReturnOrKill("parasite returned after infantry impact", true);
    } else {
        appendReturnOrKill(
            identityMatches
                ? "parasite returned after invalid or dead target"
                : "parasite returned after miss or identity mismatch"
        );
    }

    pendingByProjectile_.erase(pendingIt);
    resolvedByProjectile_[request.projectileId] = {
        request.projectileId,
        pending.sourceObjectId,
        pending.targetObjectId,
        request.currentTick
    };
    result.accepted = true;
    if (emit != nullptr) {
        emit({
            vehicleHit ? "ParasiteAttached"
                : (infantryHit ? "ParasiteInfantryKilled"
                               : "ParasiteReturned"),
            request.currentTick,
            pending.sourceObjectId,
            pending.targetObjectId,
            request.projectileId,
            request.impactCell,
            pending.warheadId
        });
    }
    return result;
}

ParasiteOperationResult RuntimeParasiteState::handleParasiteHostEvent(
    const ParasiteHostEventRequest& request,
    const SpecialEventSink& emit
) {
    ParasiteOperationResult result;
    if (request.currentTick < 0 || request.targetObjectId < 0
        || request.stunTicks < 0) {
        result.reason = "invalid parasite host event";
        return result;
    }
    for (auto resolved = resolvedByProjectile_.begin();
         resolved != resolvedByProjectile_.end();) {
        if (resolved->second.resolvedTick < request.currentTick) {
            resolved = resolvedByProjectile_.erase(resolved);
        } else {
            ++resolved;
        }
    }
    const auto linkIt = byTarget_.find(request.targetObjectId);
    if (linkIt == byTarget_.end()) {
        const auto repeated = std::find_if(
            resolvedByProjectile_.begin(),
            resolvedByProjectile_.end(),
            [&request](const auto& entry) {
                return entry.second.targetObjectId == request.targetObjectId
                    && entry.second.resolvedTick == request.currentTick
                    && entry.second.hostTerminated;
            }
        );
        if (repeated != resolvedByProjectile_.end()) {
            result.accepted = true;
            result.idempotent = true;
            return result;
        }
        result.reason = "target has no attached parasite";
        return result;
    }
    const ParasiteLinkState link = linkIt->second;
    const bool killDirectly =
        request.event == ParasiteHostEventKind::DestroyedExternally
        || (request.event == ParasiteHostEventKind::Healed && !link.organic);
    const bool canEject = !killDirectly && request.ejectCellAvailable;

    SpecialMechanismAction action;
    action.kind = canEject
        ? SpecialMechanismActionKind::ParasiteEject
        : SpecialMechanismActionKind::ParasiteKillParasite;
    action.tick = request.currentTick;
    action.projectileId = link.projectileId;
    action.objectId = link.sourceObjectId;
    action.targetObjectId = link.targetObjectId;
    action.owner = link.owner;
    action.paralyzes = link.paralyzes;
    action.cell = canEject ? request.ejectCell : SpecialCell{};
    action.suppressDeathWeapon = !canEject;
    action.replacesTerminatingHost = canEject
        && request.event == ParasiteHostEventKind::DestroyedByParasite;
    action.sourceWasAttachedToHost = true;
    const bool stunned = canEject
        && (request.event == ParasiteHostEventKind::Healed
            || request.event == ParasiteHostEventKind::SonicHit
            || request.event == ParasiteHostEventKind::Teleporting);
    action.stunTicks = stunned ? request.stunTicks : 0;
    action.reason = canEject
        ? "parasite ejected because host was "
            + parasiteHostEventName(request.event)
        : "parasite destroyed without death weapon because host was "
            + parasiteHostEventName(request.event)
            + (request.ejectCellAvailable || killDirectly
                ? std::string()
                : "; no legal eject cell");
    result.actions.push_back(std::move(action));
    byTarget_.erase(linkIt);
    resolvedByProjectile_[link.projectileId] = {
        link.projectileId,
        link.sourceObjectId,
        link.targetObjectId,
        request.currentTick,
        true
    };
    result.accepted = true;
    if (emit != nullptr) {
        emit({
            canEject ? "ParasiteEjected" : "ParasiteKilled",
            request.currentTick,
            link.sourceObjectId,
            link.targetObjectId,
            0,
            canEject ? request.ejectCell : SpecialCell{},
            parasiteHostEventName(request.event)
        });
    }
    return result;
}

std::vector<SpecialMechanismAction>
RuntimeParasiteState::reconcileParasites(
    const RuntimeParasiteObjectQueries& queries,
    const std::vector<int>& activeProjectileIds,
    int tick,
    const SpecialEventSink& emit
) {
    std::vector<SpecialMechanismAction> actions;
    const std::set<int> activeProjectiles(
        activeProjectileIds.begin(),
        activeProjectileIds.end()
    );
    for (auto pending = pendingByProjectile_.begin();
         pending != pendingByProjectile_.end();) {
        const ParasitePendingLaunchState state = pending->second;
        const WorldObjectInstance* source = queries.findObject(state.sourceObjectId
        );
        const WorldObjectInstance* target = queries.findObject(state.targetObjectId
        );
        const bool sourceValid = source != nullptr && source->health > 0
            && source->type == state.sourceTypeId
            && ((!source->alive && source->mission == "ParasiteLimbo")
                || (source->alive && tick == state.launchedTick));
        const bool targetValid = target != nullptr && target->alive
            && target->health > 0 && target->type == state.targetTypeId;
        const bool projectileValid = activeProjectiles.find(state.projectileId)
            != activeProjectiles.end();
        if (sourceValid && targetValid && projectileValid) {
            ++pending;
            continue;
        }
        SpecialMechanismAction action;
        action.kind = sourceValid
            ? SpecialMechanismActionKind::ParasiteReturnFromLimbo
            : SpecialMechanismActionKind::ParasiteKillParasite;
        action.tick = tick;
        action.projectileId = state.projectileId;
        action.objectId = state.sourceObjectId;
        action.targetObjectId = state.targetObjectId;
        action.owner = state.owner;
        action.paralyzes = state.paralyzes;
        action.cell = target == nullptr ? state.targetCell : objectCell(target);
        action.suppressDeathWeapon = !sourceValid;
        action.reason = !sourceValid
            ? "pending parasite source invalid during reconciliation"
            : (!targetValid
                ? "pending parasite target invalid during reconciliation"
                : "pending parasite projectile missing during reconciliation");
        actions.push_back(std::move(action));
        resolvedByProjectile_[state.projectileId] = {
            state.projectileId,
            state.sourceObjectId,
            state.targetObjectId,
            tick
        };
        if (emit != nullptr) {
            emit({
                sourceValid ? "ParasiteReturned" : "ParasiteKilled",
                tick,
                state.sourceObjectId,
                state.targetObjectId,
                state.projectileId,
                target == nullptr ? state.targetCell : objectCell(target),
                "pending launch reconciled"
            });
        }
        pending = pendingByProjectile_.erase(pending);
    }
    for (auto link = byTarget_.begin();
         link != byTarget_.end();) {
        const ParasiteLinkState state = link->second;
        const WorldObjectInstance* source = queries.findObject(state.sourceObjectId
        );
        const WorldObjectInstance* target = queries.findObject(state.targetObjectId
        );
        const bool sourceValid = source != nullptr && source->health > 0
            && (state.sourceTypeId.empty()
                || source->type == state.sourceTypeId)
            && (!state.sourceExpectedInLimbo
                || (!source->alive && source->mission == "ParasiteLimbo"));
        const bool targetValid = target != nullptr && target->alive
            && target->health > 0
            && (state.targetTypeId.empty()
                || target->type == state.targetTypeId);
        if (sourceValid && targetValid) {
            ++link;
            continue;
        }
        SpecialMechanismAction action;
        action.kind = sourceValid
            ? SpecialMechanismActionKind::ParasiteKillParasite
            : SpecialMechanismActionKind::ParasiteReleaseTarget;
        action.tick = tick;
        action.projectileId = state.projectileId;
        action.objectId = state.sourceObjectId;
        action.targetObjectId = state.targetObjectId;
        action.owner = state.owner;
        action.paralyzes = state.paralyzes;
        action.cell = objectCell(target);
        action.suppressDeathWeapon = sourceValid;
        action.sourceWasAttachedToHost = sourceValid;
        action.reason = sourceValid
            ? "parasite host invalid during reconciliation"
            : "parasite source invalid during reconciliation";
        actions.push_back(std::move(action));
        resolvedByProjectile_[state.projectileId] = {
            state.projectileId,
            state.sourceObjectId,
            state.targetObjectId,
            tick,
            sourceValid
        };
        link = byTarget_.erase(link);
    }
    return actions;
}

std::vector<SpecialMechanismAction>
RuntimeParasiteState::tickParasites(
    const RuntimeParasiteObjectQueries& queries,
    int tick,
    const SpecialEventSink& emit
) {
    std::vector<SpecialMechanismAction> actions;
    for (auto resolved = resolvedByProjectile_.begin();
         resolved != resolvedByProjectile_.end();) {
        if (resolved->second.resolvedTick < tick) {
            resolved = resolvedByProjectile_.erase(resolved);
        } else {
            ++resolved;
        }
    }
    for (auto entry = byTarget_.begin();
         entry != byTarget_.end();) {
        ParasiteLinkState& link = entry->second;
        const WorldObjectInstance* source = queries.findObject(link.sourceObjectId
        );
        const WorldObjectInstance* target = queries.findObject(link.targetObjectId
        );
        const bool sourceDestroyed = source == nullptr || source->health <= 0
            || (!link.sourceTypeId.empty()
                && source->type != link.sourceTypeId)
            || (!source->alive
                && (!link.sourceExpectedInLimbo
                    || source->mission != "ParasiteLimbo"));
        if (sourceDestroyed) {
            SpecialMechanismAction action;
            action.kind = SpecialMechanismActionKind::ParasiteReleaseTarget;
            action.tick = tick;
            action.projectileId = link.projectileId;
            action.objectId = link.sourceObjectId;
            action.targetObjectId = link.targetObjectId;
            action.owner = link.owner;
            action.paralyzes = link.paralyzes;
            action.cell = objectCell(target);
            action.reason = "parasite source died or left the world";
            actions.push_back(std::move(action));
            resolvedByProjectile_[link.projectileId] = {
                link.projectileId,
                link.sourceObjectId,
                link.targetObjectId,
                tick,
                false
            };
            entry = byTarget_.erase(entry);
            continue;
        }
        if (target == nullptr || !target->alive || target->health <= 0
            || target->kind != WorldObjectKind::Unit
            || (!link.targetTypeId.empty()
                && target->type != link.targetTypeId)) {
            SpecialMechanismAction action;
            action.kind = SpecialMechanismActionKind::ParasiteKillParasite;
            action.tick = tick;
            action.projectileId = link.projectileId;
            action.objectId = link.sourceObjectId;
            action.targetObjectId = link.targetObjectId;
            action.owner = link.owner;
            action.paralyzes = link.paralyzes;
            action.cell = objectCell(target);
            action.suppressDeathWeapon = true;
            action.sourceWasAttachedToHost = true;
            action.reason = "parasite host died externally or became invalid";
            actions.push_back(std::move(action));
            resolvedByProjectile_[link.projectileId] = {
                link.projectileId,
                link.sourceObjectId,
                link.targetObjectId,
                tick,
                true
            };
            entry = byTarget_.erase(entry);
            continue;
        }
        if (tick >= link.nextDamageTick) {
            const bool culled = link.culling
                && link.cullingThresholdPerMyriad > 0
                && static_cast<long long>(target->health)
                    * kParasiteRatioPerMyriad
                    <= static_cast<long long>(link.targetMaximumHealth)
                        * link.cullingThresholdPerMyriad;
            SpecialMechanismAction action;
            action.kind = culled
                ? SpecialMechanismActionKind::ParasiteKillTarget
                : SpecialMechanismActionKind::ParasiteDamage;
            action.tick = tick;
            action.projectileId = link.projectileId;
            action.objectId = link.sourceObjectId;
            action.targetObjectId = link.targetObjectId;
            action.owner = link.owner;
            action.damage = culled
                ? std::numeric_limits<int>::max()
                : link.damage;
            action.warheadId = link.warheadId;
            action.forceDestruction = culled;
            action.paralyzes = link.paralyzes;
            action.culling = link.culling;
            action.cell = objectCell(target);
            action.reason = culled
                ? (link.sourceElite ? "elite parasite culling threshold"
                                    : "parasite culling threshold")
                : "parasite damage interval";
            actions.push_back(std::move(action));
            link.nextDamageTick = tick
                    > std::numeric_limits<int>::max()
                        - std::max(1, link.damageIntervalTicks)
                ? std::numeric_limits<int>::max()
                : tick + std::max(1, link.damageIntervalTicks);
            if (emit != nullptr) {
                emit({
                    culled ? "ParasiteCulling" : "ParasiteDamage",
                    tick,
                    link.sourceObjectId,
                    link.targetObjectId,
                    culled ? target->health : link.damage,
                    objectCell(target),
                    link.warheadId
                });
            }
        }
        ++entry;
    }
    return actions;
}

bool RuntimeParasiteState::isParasiteSourceLimboed(
    int objectId
) const {
    for (const auto& [unusedProjectileId, pending]
         : pendingByProjectile_) {
        (void)unusedProjectileId;
        if (pending.sourceObjectId == objectId) {
            return true;
        }
    }
    for (const auto& [unusedTargetId, link] : byTarget_) {
        (void)unusedTargetId;
        if (link.sourceObjectId == objectId) {
            return true;
        }
    }
    return false;
}

int RuntimeParasiteState::parasiteTargetForSource(
    int objectId
) const {
    for (const auto& [unusedProjectileId, pending]
         : pendingByProjectile_) {
        (void)unusedProjectileId;
        if (pending.sourceObjectId == objectId) {
            return pending.targetObjectId;
        }
    }
    for (const auto& [targetId, link] : byTarget_) {
        if (link.sourceObjectId == objectId) {
            return targetId;
        }
    }
    return -1;
}

bool RuntimeParasiteState::isParasiteHostParalyzed(
    int objectId
) const {
    const auto found = byTarget_.find(objectId);
    return found != byTarget_.end() && found->second.paralyzes != 0;
}

void RuntimeParasiteState::onObjectRemoved(const RuntimeParasiteObjectQueries& queries, int objectId, int tick,
    const SpecialEventSink& emit, const std::string& reason, std::vector<SpecialMechanismAction>* actions) {
    for (auto pending = pendingByProjectile_.begin();
         pending != pendingByProjectile_.end();) {
        const ParasitePendingLaunchState state = pending->second;
        if (state.sourceObjectId != objectId
            && state.targetObjectId != objectId) {
            ++pending;
            continue;
        }
        const bool sourceRemoved = state.sourceObjectId == objectId;
        if (actions != nullptr) {
            SpecialMechanismAction action;
            action.kind = sourceRemoved
                ? SpecialMechanismActionKind::ParasiteKillParasite
                : SpecialMechanismActionKind::ParasiteReturnFromLimbo;
            action.tick = tick;
            action.projectileId = state.projectileId;
            action.objectId = state.sourceObjectId;
            action.targetObjectId = state.targetObjectId;
            action.owner = state.owner;
            action.paralyzes = state.paralyzes;
            action.cell = state.targetCell;
            action.suppressDeathWeapon = sourceRemoved;
            action.reason = sourceRemoved
                ? "pending parasite source removed"
                : "pending parasite target removed";
            actions->push_back(std::move(action));
        }
        resolvedByProjectile_[state.projectileId] = {
            state.projectileId,
            state.sourceObjectId,
            state.targetObjectId,
            tick
        };
        if (emit != nullptr) {
            emit({
                sourceRemoved ? "ParasiteKilled" : "ParasiteReturned",
                tick,
                state.sourceObjectId,
                state.targetObjectId,
                state.projectileId,
                state.targetCell,
                reason
            });
        }
        pending = pendingByProjectile_.erase(pending);
    }

    for (auto parasite = byTarget_.begin();
         parasite != byTarget_.end();) {
        const ParasiteLinkState state = parasite->second;
        if (state.sourceObjectId != objectId
            && state.targetObjectId != objectId) {
            ++parasite;
            continue;
        }
        const bool sourceRemoved = state.sourceObjectId == objectId;
        if (actions != nullptr) {
            SpecialMechanismAction action;
            action.kind = sourceRemoved
                ? SpecialMechanismActionKind::ParasiteReleaseTarget
                : SpecialMechanismActionKind::ParasiteKillParasite;
            action.tick = tick;
            action.projectileId = state.projectileId;
            action.objectId = state.sourceObjectId;
            action.targetObjectId = state.targetObjectId;
            action.owner = state.owner;
            action.paralyzes = state.paralyzes;
            action.cell = objectCell(queries.findObject(state.targetObjectId));
            action.suppressDeathWeapon = !sourceRemoved;
            action.sourceWasAttachedToHost = !sourceRemoved;
            action.reason = sourceRemoved
                ? "parasite source removed"
                : "parasite host removed externally";
            actions->push_back(std::move(action));
        }
        if (emit != nullptr) {
            emit({
                sourceRemoved ? "ParasiteLinkCleared" : "ParasiteKilled",
                tick,
                state.sourceObjectId,
                state.targetObjectId,
                0,
                objectCell(queries.findObject(state.targetObjectId)),
                reason
            });
        }
        resolvedByProjectile_[state.projectileId] = {
            state.projectileId,
            state.sourceObjectId,
            state.targetObjectId,
            tick,
            !sourceRemoved
        };
        parasite = byTarget_.erase(parasite);
    }

}
