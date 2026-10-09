// Include after applyRuntimeGameplaySelectionCommand and the existing command
// executors; declarations for the early factory call sites are in the wiring note.
std::string runtimePlayerFactoryCategory(
    const RuntimeWindowState& state, const WorldObjectInstance& object
) {
    if (!object.alive || object.kind != WorldObjectKind::Structure) return {};
    const auto* building = findRuntimeBuildingTypeById(state.rules, object.type);
    if (!building || building->factory.empty()) return {};
    return toUpperAsciiLocal(building->factory)
        + ((building->naval || building->waterBound) ? ":NAVAL" : ":LAND");
}

bool validateRuntimePlayerFactoryCommand(const RuntimeWindowState& state,
    const std::string& owner, int objectId, std::string& error
) {
    const auto* object = findObjectById(state, objectId);
    if (!object || runtimeCanonicalHumanHouseOwner(object->owner)
        != runtimeCanonicalHumanHouseOwner(owner)
        || runtimePlayerFactoryCategory(state, *object).empty()) {
        error = "factory is not owned by the issuing house"; return false;
    }
    return true;
}

bool applyRuntimePlayerPrimaryFactory(RuntimeWindowState& state,
    const std::string& owner, int objectId
) {
    std::string error;
    if (!validateRuntimePlayerFactoryCommand(state, owner, objectId, error)) return false;
    const auto* factory = findObjectById(state, objectId);
    state.playerCommands.primaryFactories[{
        runtimeCanonicalHumanHouseOwner(owner), runtimePlayerFactoryCategory(state, *factory)}] = objectId;
    return true;
}

bool applyRuntimePlayerRallyPoint(RuntimeWindowState& state,
    const std::string& owner, int objectId, RuntimeMultiplayerGameplayCell cell
) {
    std::string error;
    if (!validateRuntimePlayerFactoryCommand(state, owner, objectId, error)
        || !findRuntimeTerrainCellAt(state, {cell.x, cell.y})) return false;
    state.playerCommands.rallyPoints[objectId] = {
        runtimeCanonicalHumanHouseOwner(owner), cell};
    return true;
}

void prioritizeRuntimePlayerFactory(const RuntimeWindowState& state,
    const std::string& owner, std::vector<const WorldObjectInstance*>& candidates
) {
    const auto ownerKey = runtimeCanonicalHumanHouseOwner(owner);
    std::stable_partition(candidates.begin(), candidates.end(), [&](const auto* object) {
        if (!object) return false;
        const auto found = state.playerCommands.primaryFactories.find({
            ownerKey, runtimePlayerFactoryCategory(state, *object)});
        return found != state.playerCommands.primaryFactories.end()
            && found->second == object->instanceId;
    });
}

void queueRuntimePlayerFactoryRally(RuntimeWindowState& state, int factoryId, int producedId) {
    const auto* object = findObjectById(state, producedId);
    const auto rally = state.playerCommands.rallyPoints.find(factoryId);
    if (!object || rally == state.playerCommands.rallyPoints.end()
        || runtimeCanonicalHumanHouseOwner(object->owner) != rally->second.owner) return;
    RuntimeMultiplayerGameplayTargetOrderCommand order;
    order.order = RuntimeMultiplayerGameplayTargetOrderKind::Move;
    order.objectIds = {producedId};
    order.target = RuntimeMultiplayerGameplayCellTarget{rally->second.cell};
    // The factory's exit lease still owns the first move. Execute the player's
    // rally only after clearing it, so a far rally cannot hold the factory door.
    const bool freshQueue = !state.playerCommands.queues.contains(producedId);
    if (state.playerCommands.append(rally->second.owner, order) && freshQueue) {
        state.playerCommands.queues.at(producedId).factoryRallyOrderIndex = 0;
    }
}

// These gates represent an action that can resume later. Deliberately do not
// use !runtimeObjectCanMove: a deployed actor may still accept an attack or
// repack order, and an unsupported permanent locomotor must not block a queue.
bool runtimePlayerActorTemporarilyCommandLocked(
    const RuntimeWindowState& state, const WorldObjectInstance& actor
) {
    if (runtimeObjectDisabledByParasite(state, actor.instanceId)
        || runtimeMissionIsLandingToDeploy(actor)
        || runtimeMissionIsTakingOffAfterUndeploy(actor)
        || runtimeMissionIsDeploying(actor) || runtimeMissionIsUndeploying(actor)
        || runtimeStructureIsRepacking(actor)
        || actor.mission == "ParadropDrop"
        || runtimeGateObjectIsLimbo(actor)
        || runtimeObjectManagedByAirstrike(state, actor.instanceId)
        || !runtimePoweredUnitIsOperational(state, actor)) return true;
    return state.specialMechanismsInitialized
        && (state.specialMechanisms.isChronoMovementLocked(actor.instanceId, state.tick)
            || state.specialMechanisms.isTemporalFrozen(actor.instanceId)
            || state.specialMechanisms.isCarrierChildDocked(actor.instanceId));
}

template<class Apply>
bool applyRuntimePlayerSelectionCommand(RuntimeWindowState& state,
    const std::string& owner, const std::vector<std::int32_t>& objectIds, Apply&& apply
) {
    // Recorded network commands retain their issuing house even when viewed
    // offline. A live socket must not determine simulation ownership.
    if (state.multiplayerBridge || state.config.multiplayerActive)
        return applyRuntimeGameplaySelectionCommand(state, owner, objectIds, std::forward<Apply>(apply));
    const auto selected = state.selectedObjectIds;
    state.selectedObjectIds.assign(objectIds.begin(), objectIds.end());
    try {
        const bool accepted = apply();
        state.selectedObjectIds = selected;
        return accepted;
    } catch (...) { state.selectedObjectIds = selected; throw; }
}

#include "RuntimePlayerTravelOrders.inl"

bool executeRuntimePlayerTargetOrder(RuntimeWindowState& state,
    const std::string& owner, const RuntimeMultiplayerGameplayTargetOrderCommand& body,
    bool useCurrentUiSelection = false,
    bool suppressCommandVoiceFeedback = false
) {
    std::string actorError;
    if (!validateRuntimeGameplayOwnedObjects(state, owner, body.objectIds, actorError)) return false;
    const auto* cell = std::get_if<RuntimeMultiplayerGameplayCellTarget>(&body.target);
    const auto* targetId = std::get_if<RuntimeMultiplayerGameplayObjectTarget>(&body.target);
    const auto* target = targetId ? findObjectById(state, targetId->objectId) : nullptr;
    if (targetId && (!target || !runtimeGameplayObjectVisibleToOwner(state, owner, *target))) return false;
    for (int id : body.objectIds) state.playerCommands.travelling.erase(id);
    if (body.order == RuntimeMultiplayerGameplayTargetOrderKind::AttackMove
        || body.order == RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination)
        return beginRuntimePlayerTravelOrder(state, owner, body);
    if (body.order == RuntimeMultiplayerGameplayTargetOrderKind::Follow) {
        if (!target || !runtimeOwnersAreAllied(state, owner, target->owner)) return false;
        const int followedId = target->instanceId;
        bool accepted = false;
        for (int id : body.objectIds) {
            const auto* object = findObjectById(state, id);
            if (!object || id == followedId || !runtimeObjectCanMove(state, *object)
                || runtimeCanonicalHumanHouseOwner(object->owner) != runtimeCanonicalHumanHouseOwner(owner)) continue;
            // Stop through the existing command executor so old movement,
            // pending weapon bursts and contextual entry intents end together.
            // Call the executor directly: the UI Stop wrapper would erase the
            // subsequent route while a queued Follow is being activated.
            const bool stopped = applyRuntimePlayerSelectionCommand(state, owner, {id}, [&]() {
                issueRuntimeStopCommand(state);
                return state.lastCommand.valid && state.lastCommand.acceptedCount > 0;
            });
            if (!stopped) continue;
            state.playerCommands.following[id] = {
                runtimeCanonicalHumanHouseOwner(owner), followedId, {-1, -1}};
            accepted = true;
        }
        return accepted;
    }
    const auto apply = [&]() {
        if (body.order == RuntimeMultiplayerGameplayTargetOrderKind::Move && cell)
            issueRuntimeMoveCommand(state, {cell->cell.x, cell->cell.y}, nullptr, suppressCommandVoiceFeedback);
        else if (body.order == RuntimeMultiplayerGameplayTargetOrderKind::Attack && target)
            issueRuntimeAttackCommand(state, *target, true);
        else if (body.order == RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell && cell)
            return issueRuntimeCellForceFireCommand(state, {cell->cell.x, cell->cell.y});
        else if (body.order == RuntimeMultiplayerGameplayTargetOrderKind::Primary)
            issueRuntimeCommand(state, cell ? RuntimeCellPoint{cell->cell.x, cell->cell.y}
                : runtimeCellPointForObject(*target), target);
        else return false;
        return state.lastCommand.valid && state.lastCommand.acceptedCount > 0;
    };
    // Immediate local UI commands may replace a selected construction yard
    // with its new MCV. Preserve the executor's selection change in that path;
    // queued/authoritative commands still use a temporary actor selection.
    if (useCurrentUiSelection && !state.multiplayerBridge) return apply();
    return applyRuntimePlayerSelectionCommand(state, owner, body.objectIds, apply);
}

#include "RuntimePlayerWaypoints.inl"

bool applyRuntimePlayerTargetOrder(RuntimeWindowState& state,
    const std::string& owner, const RuntimeMultiplayerGameplayTargetOrderCommand& body,
    bool useCurrentUiSelection = false
) {
    std::string error;
    RuntimeMultiplayerGameplayCommand canonicalCommand;
    canonicalCommand.body = body;
    if (!canonicalizeRuntimeMultiplayerGameplayCommand(canonicalCommand, error)) return false;
    auto canonical = std::get<RuntimeMultiplayerGameplayTargetOrderCommand>(canonicalCommand.body);
    if (!validateRuntimeGameplayOwnedObjects(state, owner, canonical.objectIds, error)) return false;
    {
        const auto* cell = std::get_if<RuntimeMultiplayerGameplayCellTarget>(&canonical.target);
        using Order = RuntimeMultiplayerGameplayTargetOrderKind;
        if (cell && (canonical.order == Order::Move || canonical.order == Order::Primary
                || canonical.order == Order::AttackMove || canonical.order == Order::GuardDestination)) {
            std::erase_if(canonical.objectIds, [&](int id) {
                const auto* actor = findObjectById(state, id);
                return !actor || !runtimeObjectAllowsMoveToCell(state, *actor, {cell->cell.x, cell->cell.y});
            });
            if (canonical.objectIds.empty()) return false;
        }
    }
    if (canonical.append && state.playerCommands.waypoints.planningOwners.contains(runtimeCanonicalHumanHouseOwner(owner)))
        return appendRuntimePlayerWaypointOrder(state, runtimeCanonicalHumanHouseOwner(owner), canonical);
    if (canonical.append)
        return state.playerCommands.append(runtimeCanonicalHumanHouseOwner(owner), canonical);
    state.playerCommands.cancel(canonical.objectIds);
    return executeRuntimePlayerTargetOrder(state, owner, canonical, useCurrentUiSelection);
}

bool advanceRuntimePlayerCommands(RuntimeWindowState& state) {
    RuntimePerfTraceScope stageTrace("advanceRuntimePlayerCommands", state.tick);
    bool changed = advanceRuntimePlayerTravelOrders(state);
    const auto actorValid = [&](int id, const std::string& owner) {
        const auto* object = findObjectById(state, id);
        return object && object->alive
            && !runtimeObjectIsEnslavedSlaveWorker(state, *object)
            && runtimeCanonicalHumanHouseOwner(object->owner) == owner;
    };
    const auto snapshotActorIds = [](const auto& commands) {
        std::vector<int> ids;
        ids.reserve(commands.size());
        for (const auto& [id, command] : commands) {
            (void)command; ids.push_back(id);
        }
        return ids;
    };
    // Contextual executors may atomically replace playerCommands (for example,
    // a grinding transaction). Never carry its iterators or references across
    // a command call; the actor snapshot preserves stable deterministic order.
    for (const int actorId : snapshotActorIds(state.playerCommands.following)) {
        const auto found = state.playerCommands.following.find(actorId);
        if (found == state.playerCommands.following.end()) continue;
        const auto follow = found->second;
        const auto* actor = findObjectById(state, actorId);
        const auto* target = findObjectById(state, follow.targetId);
        if (!actorValid(actorId, follow.owner) || !target
            || !runtimeOwnersAreAllied(state, follow.owner, target->owner)) {
            state.playerCommands.following.erase(actorId); changed = true; continue;
        }
        if (runtimePlayerActorTemporarilyCommandLocked(state, *actor)
            || runtimeGateObjectIsLimbo(*target)) continue;
        const RuntimeMultiplayerGameplayCell destination{target->x, target->y};
        const int distance = std::max(std::abs(actor->x - target->x), std::abs(actor->y - target->y));
        const auto behavior = state.behaviors.find(actorId);
        const bool idle = behavior == state.behaviors.end()
            || behavior->second.kind == RuntimeBehaviorKind::None
            || behavior->second.state == RuntimeBehaviorState::Complete
            || behavior->second.state == RuntimeBehaviorState::Idle;
        if (distance > 2 && (destination != follow.lastTargetCell || idle)) {
            const RuntimeMultiplayerGameplayTargetOrderCommand order{
                RuntimeMultiplayerGameplayTargetOrderKind::Move, {actorId},
                RuntimeMultiplayerGameplayCellTarget{destination}};
            if (executeRuntimePlayerTargetOrder(state, follow.owner, order)) {
                const auto resumed = state.playerCommands.following.find(actorId);
                if (resumed != state.playerCommands.following.end()
                    && resumed->second.owner == follow.owner
                    && resumed->second.targetId == follow.targetId)
                    resumed->second.lastTargetCell = destination;
                changed = true;
            }
        }
    }
    for (const int id : snapshotActorIds(state.playerCommands.queues)) {
        const auto it = state.playerCommands.queues.find(id);
        if (it == state.playerCommands.queues.end()) continue;
        if (!actorValid(id, it->second.owner)) {
            state.playerCommands.queues.erase(id); changed = true; continue;
        }
        if (state.playerCommands.waypoints.routes.contains(id)) continue;
        const auto travel = state.playerCommands.travelling.find(id);
        if ((travel != state.playerCommands.travelling.end() && !travel->second.guarding)
            || state.playerCommands.following.contains(id) || state.combatDetails.hasPendingBurst(id)
            || runtimeFactoryDeliveryOwnsEgress(state, id)
            || state.pendingEngineerCaptures.contains(id)
            || state.pendingEngineerActions.contains(id)
            || state.pendingOccupyTargets.contains(id)
            || state.pendingTransportTargets.contains(id)
            || state.pendingTimedBombIntents.contains(id)) {
            continue;
        }
        const auto behavior = state.behaviors.find(id);
        const bool idle = behavior == state.behaviors.end()
            || behavior->second.kind == RuntimeBehaviorKind::None
            || behavior->second.state == RuntimeBehaviorState::Complete
            || behavior->second.state == RuntimeBehaviorState::Idle
            || (behavior->second.autoAcquired && behavior->second.kind == RuntimeBehaviorKind::Attack);
        if (!idle) continue;
        const auto next = it->second.orders.front();
        const std::string owner = it->second.owner;
        bool targetValid = true;
        if (const auto* targetId = std::get_if<RuntimeMultiplayerGameplayObjectTarget>(&next.target)) {
            const auto* target = findObjectById(state, targetId->objectId);
            targetValid = target && runtimeGameplayObjectVisibleToOwner(state, owner, *target)
                && (next.order != RuntimeMultiplayerGameplayTargetOrderKind::Follow
                    || runtimeOwnersAreAllied(state, owner, target->owner));
        }
        // Expired targets may be discarded even during a freeze; a valid
        // route survives temporary action locks without consuming a step.
        const auto* actor = findObjectById(state, id);
        if (targetValid && actor && runtimePlayerActorTemporarilyCommandLocked(state, *actor)) {
            continue;
        }
        const bool factoryRally = it->second.factoryRallyOrderIndex == 0;
        if (it->second.factoryRallyOrderIndex >= 0) --it->second.factoryRallyOrderIndex;
        it->second.orders.pop_front();
        if (it->second.orders.empty()) state.playerCommands.queues.erase(id);
        // Missing/dead targets are skipped; a queued attack cannot track a
        // target after it leaves the issuer's vision.
        if (targetValid) (void)executeRuntimePlayerTargetOrder(state, owner, next, false, factoryRally);
        changed = true;
    }
    changed = advanceRuntimePlayerWaypointOrders(state) || changed;
    for (auto it = state.playerCommands.primaryFactories.begin(); it != state.playerCommands.primaryFactories.end();) {
        const auto* factory = findObjectById(state, it->second);
        if (!factory || !actorValid(it->second, it->first.first)
            || runtimePlayerFactoryCategory(state, *factory) != it->first.second) {
            it = state.playerCommands.primaryFactories.erase(it); changed = true;
        } else ++it;
    }
    for (auto it = state.playerCommands.rallyPoints.begin(); it != state.playerCommands.rallyPoints.end();) {
        if (!actorValid(it->first, it->second.owner)) {
            it = state.playerCommands.rallyPoints.erase(it); changed = true;
        } else ++it;
    }
    return changed;
}
