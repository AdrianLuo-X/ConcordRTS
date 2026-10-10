// Included after executeRuntimePlayerTargetOrder, before the public dispatcher.
bool validateRuntimePlayerWaypointCommand(const RuntimeWindowState& state,
    const std::string& owner, const RuntimeMultiplayerGameplayWaypointCommand& body,
    std::string& error
) {
    using Op = RuntimePlayerWaypointOperation;
    const auto ownerKey = runtimeCanonicalHumanHouseOwner(owner);
    if (body.operation == Op::Begin || body.operation == Op::Commit) return true;
    const auto& waypoints = state.playerCommands.waypoints;
    if (!waypoints.planningOwners.contains(ownerKey) || !waypoints.ownsPoint(ownerKey, body.waypointId)) {
        error = "waypoint is not part of the issuing house's current plan"; return false;
    }
    return true;
}

void pauseRuntimePlayerWaypointActor(RuntimeWindowState& state, const std::string& owner, int id) {
    const auto* actor = findObjectById(state, id);
    if (!actor || !actor->alive || runtimeObjectIsEnslavedSlaveWorker(state, *actor)
        || runtimeCanonicalHumanHouseOwner(actor->owner) != owner) return;
    state.playerCommands.travelling.erase(id);
    state.playerCommands.following.erase(id);
    state.playerCommands.queues.erase(id);
    applyRuntimePlayerSelectionCommand(state, owner, {id}, [&]() {
        issueRuntimeStopCommand(state);
        return state.lastCommand.valid && state.lastCommand.acceptedCount > 0;
    });
    const auto found = state.playerCommands.waypoints.routes.find(id);
    if (found != state.playerCommands.waypoints.routes.end()) {
        found->second.active = false; found->second.nextAttemptTick = 0;
    }
}

bool applyRuntimePlayerWaypointCommand(RuntimeWindowState& state,
    const std::string& owner, const RuntimeMultiplayerGameplayWaypointCommand& body
) {
    std::string error;
    if (!validateRuntimePlayerWaypointCommand(state, owner, body, error)) return false;
    const auto ownerKey = runtimeCanonicalHumanHouseOwner(owner);
    auto& waypoints = state.playerCommands.waypoints;
    using Op = RuntimePlayerWaypointOperation;
    if (body.operation == Op::Commit) {
        waypoints.planningOwners.erase(ownerKey);
        for (auto& [id, route] : waypoints.routes) if (route.owner == ownerKey)
            route.loopCommitted = route.loopStart != 0;
        return true;
    }
    if (body.operation == Op::Begin) {
        if (!waypoints.planningOwners.insert(ownerKey).second) return true;
        std::vector<int> actors;
        for (const auto& [id, route] : waypoints.routes) if (route.owner == ownerKey) actors.push_back(id);
        for (int id : actors) pauseRuntimePlayerWaypointActor(state, ownerKey, id);
        return true;
    }
    std::vector<int> interrupted;
    bool changed = false;
    for (auto it = waypoints.routes.begin(); it != waypoints.routes.end();) {
        auto& route = it->second;
        const auto index = route.indexOf(body.waypointId);
        if (route.owner != ownerKey || index == route.points.size()) { ++it; continue; }
        if (body.operation == Op::CloseLoop) {
            if (index + 1 < route.points.size()) {
                if (route.loopStart != body.waypointId) route.loopCommitted = false;
                route.loopStart = body.waypointId; changed = true;
            }
        } else if (body.operation == Op::DeletePoint) {
            if (route.erasePoint(body.waypointId)) interrupted.push_back(it->first);
            changed = true;
        }
        if (route.points.empty()) it = waypoints.routes.erase(it); else ++it;
    }
    for (int id : interrupted) pauseRuntimePlayerWaypointActor(state, ownerKey, id);
    return changed;
}

bool appendRuntimePlayerWaypointOrder(RuntimeWindowState& state, const std::string& owner,
    const RuntimeMultiplayerGameplayTargetOrderCommand& body
) {
    auto accepted = body;
    std::erase_if(accepted.objectIds, [&](int id) {
        const auto* actor = findObjectById(state, id);
        return !actor || !actor->alive || runtimeObjectIsEnslavedSlaveWorker(state, *actor)
            || !runtimeObjectCanMove(state, *actor);
    });
    if (const auto* cell = std::get_if<RuntimeMultiplayerGameplayCellTarget>(&body.target))
        if (!findRuntimeTerrainCellAt(state, {cell->cell.x, cell->cell.y})) return false;
    if (!state.playerCommands.waypoints.canAppend(owner, accepted.objectIds)) return false;
    std::vector<int> newActors;
    for (int id : accepted.objectIds) {
        const auto route = state.playerCommands.waypoints.routes.find(id);
        if (route == state.playerCommands.waypoints.routes.end() || route->second.owner != owner)
            newActors.push_back(id);
    }
    if (!state.playerCommands.waypoints.append(owner, accepted)) return false;
    for (int id : newActors) pauseRuntimePlayerWaypointActor(state, owner, id);
    return true;
}

bool advanceRuntimePlayerWaypointOrders(RuntimeWindowState& state) {
    std::vector<int> actors;
    for (const auto& [id, route] : state.playerCommands.waypoints.routes) actors.push_back(id);
    bool changed = false;
    for (int id : actors) {
        auto found = state.playerCommands.waypoints.routes.find(id);
        if (found == state.playerCommands.waypoints.routes.end()) continue;
        const auto* actor = findObjectById(state, id);
        if (!actor || !actor->alive || runtimeObjectIsEnslavedSlaveWorker(state, *actor)
            || runtimeCanonicalHumanHouseOwner(actor->owner) != found->second.owner) {
            state.playerCommands.waypoints.routes.erase(id); changed = true; continue;
        }
        if (state.playerCommands.waypoints.planningOwners.contains(found->second.owner)
            || runtimePlayerActorTemporarilyCommandLocked(state, *actor)
            || runtimeFactoryDeliveryOwnsEgress(state, id)
            || state.combatDetails.hasPendingBurst(id)
            || state.pendingEngineerCaptures.contains(id) || state.pendingEngineerActions.contains(id)
            || state.pendingOccupyTargets.contains(id) || state.pendingTransportTargets.contains(id)
            || state.pendingTimedBombIntents.contains(id)) continue;
        const auto travel = state.playerCommands.travelling.find(id);
        if ((travel != state.playerCommands.travelling.end() && !travel->second.guarding)
            || state.playerCommands.following.contains(id)) continue;
        const auto behavior = state.behaviors.find(id);
        const bool idle = behavior == state.behaviors.end()
            || behavior->second.kind == RuntimeBehaviorKind::None
            || behavior->second.state == RuntimeBehaviorState::Complete
            || behavior->second.state == RuntimeBehaviorState::Idle
            || (behavior->second.autoAcquired && behavior->second.kind == RuntimeBehaviorKind::Attack);
        if (!idle) continue;
        auto& route = found->second;
        if (route.active) { route.finishPoint(); changed = true; }
        if (route.points.empty()) { state.playerCommands.waypoints.routes.erase(id); continue; }
        if (state.tick < route.nextAttemptTick) continue;
        auto next = route.points[route.cursor].order;
        if (const auto* targetId = std::get_if<RuntimeMultiplayerGameplayObjectTarget>(&next.target)) {
            const auto* target = findObjectById(state, targetId->objectId);
            if (!target || !runtimeGameplayObjectVisibleToOwner(state, route.owner, *target)) {
                route.finishPoint();
                if (route.points.empty()) state.playerCommands.waypoints.routes.erase(id);
                changed = true; continue;
            }
        }
        // Patrol legs use the attack-move executor, which resumes the leg after combat.
        if (route.loopStart && std::holds_alternative<RuntimeMultiplayerGameplayCellTarget>(next.target)
            && (next.order == RuntimeMultiplayerGameplayTargetOrderKind::Move
                || next.order == RuntimeMultiplayerGameplayTargetOrderKind::Primary))
            next.order = RuntimeMultiplayerGameplayTargetOrderKind::AttackMove;
        const auto pointId = route.points[route.cursor].id;
        const auto owner = route.owner;
        struct QuietWaypoint {
            RuntimeWindowState& state; bool previous;
            explicit QuietWaypoint(RuntimeWindowState& value)
                : state(value), previous(value.runtimeSuppressPlayerAudioFeedback) {
                state.runtimeSuppressPlayerAudioFeedback = true;
            }
            ~QuietWaypoint() { state.runtimeSuppressPlayerAudioFeedback = previous; }
        } quiet(state);
        const bool accepted = executeRuntimePlayerTargetOrder(state, owner, next);
        // Contextual orders may replace command state or remove the acting unit.
        found = state.playerCommands.waypoints.routes.find(id);
        if (found != state.playerCommands.waypoints.routes.end()
            && found->second.owner == owner && found->second.points[found->second.cursor].id == pointId) {
            found->second.active = accepted;
            found->second.nextAttemptTick = accepted ? 0 : state.tick + std::max(1, state.config.fixedTickHz);
        }
        changed = accepted || changed;
    }
    return changed;
}
