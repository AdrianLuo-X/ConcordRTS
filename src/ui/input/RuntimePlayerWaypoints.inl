// Included after executeRuntimePlayerTargetOrder, before the public dispatcher.
bool validateRuntimePlayerWaypointCommand(const RuntimeProductionHostState& state,
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

void pauseRuntimePlayerWaypointActor(RuntimeProductionHostState& state, const std::string& owner, int id) {
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

bool applyRuntimePlayerWaypointCommand(RuntimeProductionHostState& state,
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

bool appendRuntimePlayerWaypointOrder(RuntimeProductionHostState& state, const std::string& owner,
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


