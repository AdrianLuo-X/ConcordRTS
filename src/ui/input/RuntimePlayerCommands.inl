// Include after applyRuntimeGameplaySelectionCommand and the existing command
// executors; declarations for the early factory call sites are in the wiring note.


bool validateRuntimePlayerFactoryCommand(const RuntimeProductionHostState& state,
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

bool applyRuntimePlayerPrimaryFactory(RuntimeProductionHostState& state,
    const std::string& owner, int objectId
) {
    std::string error;
    if (!validateRuntimePlayerFactoryCommand(state, owner, objectId, error)) return false;
    const auto* factory = findObjectById(state, objectId);
    state.playerCommands.primaryFactories[{
        runtimeCanonicalHumanHouseOwner(owner), runtimePlayerFactoryCategory(state, *factory)}] = objectId;
    return true;
}

bool applyRuntimePlayerRallyPoint(RuntimeProductionHostState& state,
    const std::string& owner, int objectId, RuntimeMultiplayerGameplayCell cell
) {
    std::string error;
    if (!validateRuntimePlayerFactoryCommand(state, owner, objectId, error)
        || !findRuntimeTerrainCellAt(state, {cell.x, cell.y})) return false;
    state.playerCommands.rallyPoints[objectId] = {
        runtimeCanonicalHumanHouseOwner(owner), cell};
    return true;
}





// These gates represent an action that can resume later. Deliberately do not
// use !runtimeObjectCanMove: a deployed actor may still accept an attack or
// repack order, and an unsupported permanent locomotor must not block a queue.




#include "ui/input/RuntimePlayerTravelOrders.inl"



#include "ui/input/RuntimePlayerWaypoints.inl"

bool applyRuntimePlayerTargetOrder(RuntimeProductionHostState& state,
    const std::string& owner, const RuntimeMultiplayerGameplayTargetOrderCommand& body,
    bool useCurrentUiSelection
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


