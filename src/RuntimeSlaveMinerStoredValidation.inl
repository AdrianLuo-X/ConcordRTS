bool validateRuntimeStoredSlaveMinerState(
    const RuntimeWindowState& state, std::string* error
) {
    const auto fail = [&](int id, const std::string& reason) {
        if (error != nullptr) *error = "invalid stored slave miner worker #"
            + std::to_string(id) + ": " + reason;
        return false;
    };
    const auto inside = [](const WorldObjectInstance& object) {
        return toLowerAsciiLocal(trimAsciiLocal(object.mission)) == "insideslaveminer";
    };
    for (const auto& [workerId, task] : state.slaveMinerTasks) {
        const auto* worker = findRuntimeObjectByIdIncludingInactive(state, workerId);
        const bool stored = task.phase == RuntimeSlaveMinerPhase::Stored;
        const bool hidden = worker != nullptr && !worker->alive
            && worker->health > 0 && inside(*worker);
        if (stored != hidden) return fail(workerId, "phase and containment disagree");
        // Other existing limbo/transport states retain their own validation.
        if (!stored) continue;
        const auto* parent = findRuntimeObjectByIdIncludingInactive(state, task.refineryId);
        const auto* workerRules = worker->kind == WorldObjectKind::Infantry
            ? findRuntimeUnitType(state.rules, *worker) : nullptr;
        const UnitType* mobile = parent != nullptr && parent->kind == WorldObjectKind::Unit
            ? findRuntimeUnitType(state.rules, *parent) : nullptr;
        const BuildingType* building = parent != nullptr && parent->kind == WorldObjectKind::Structure
            ? findRuntimeBuildingTypeById(state.rules, parent->type)
            : mobile != nullptr && mobile->resourceGatherer && mobile->resourceDestination
                && mobile->storage > 0 && mobile->slavesNumber > 0 && !mobile->deploysInto.empty()
                ? findRuntimeBuildingTypeById(state.rules, mobile->deploysInto) : nullptr;
        if (parent == nullptr || parent == worker || !parent->alive || parent->health <= 0
            || parent->owner != worker->owner || building == nullptr || !runtimeBuildingCanGatherResources(*building)
            || building->storage <= 0 || building->slavesNumber <= 0 || building->enslaves.empty()
            || workerRules == nullptr || !workerRules->slaved
            || toUpperAsciiLocal(trimAsciiLocal(building->enslaves))
                != toUpperAsciiLocal(trimAsciiLocal(worker->type))
            || (mobile != nullptr && !mobile->enslaves.empty()
                && toUpperAsciiLocal(trimAsciiLocal(mobile->enslaves))
                    != toUpperAsciiLocal(trimAsciiLocal(worker->type))))
            return fail(workerId, "invalid worker or parent authority");
        if (worker->x != parent->x || worker->y != parent->y || worker->cell != parent->cell
            || worker->level != parent->level || worker->onBridge != parent->onBridge
            || worker->subCell != -1 || worker->airborne
            || worker->hasRenderPosition || worker->renderMoveActive
            || worker->garrisonHostObjectId >= 0 || worker->tankBunkerHostId >= 0
            || worker->dockedToObjectId >= 0 || worker->dockIndex >= 0
            || !worker->transportPassengerIds.empty() || !worker->garrisonOccupantIds.empty()
            || task.cargoValue != 0 || task.cargoUnits != 0 || task.cargoGemUnits != 0
            || task.workTicksRemaining != 0 || state.behaviors.contains(workerId)
            || state.deathWeapons.hasTriggered(workerId) || state.deathWeapons.hasTriggered(parent->instanceId)
            || state.pendingObjectExitIds.contains(workerId) || state.pendingObjectExitIds.contains(parent->instanceId))
            return fail(workerId, "invalid stored pose, cargo, or active exit");
    }
    for (const auto& object : state.world.getObjects()) {
        if (!inside(object)) continue;
        const auto task = state.slaveMinerTasks.find(object.instanceId);
        if (task == state.slaveMinerTasks.end() || task->second.phase != RuntimeSlaveMinerPhase::Stored)
            return fail(object.instanceId, "containment has no stored task");
    }
    return true;
}
