// Include before queueRuntimeTargetAbility, after the worker movement helpers.
// A slave's delivery order belongs to its retained home relationship, rather
// than to the ordinary vehicle-harvester dock system.
bool runtimeSlaveMinerDeliveryTargetLegal(
    const RuntimeWindowState& state,
    const WorldObjectInstance& worker,
    const WorldObjectInstance& target
) {
    const auto task = state.slaveMinerTasks.find(worker.instanceId);
    if (!worker.alive || worker.health <= 0
        || worker.kind != WorldObjectKind::Infantry
        || runtimeGateObjectIsLimbo(worker)
        || runtimeObjectIsEnslavedSlaveWorker(state, worker)
        || !runtimeObjectCanReceiveMoveCommand(state, worker)
        || !target.alive || target.health <= 0
        || runtimeGateObjectIsLimbo(target)
        || task == state.slaveMinerTasks.end()
        || task->second.refineryId != target.instanceId
        || runtimeCanonicalHumanHouseOwner(worker.owner)
            != runtimeCanonicalHumanHouseOwner(state.economy.owner)
        || runtimeCanonicalHumanHouseOwner(target.owner)
            != runtimeCanonicalHumanHouseOwner(worker.owner)) return false;

    const auto* workerType = findRuntimeUnitType(state.rules, worker);
    if (!workerType || !workerType->slaved) return false;

    std::string enslaves;
    if (target.kind == WorldObjectKind::Unit) {
        const auto* unit = findRuntimeUnitType(state.rules, target);
        if (!unit || !runtimeUnitIsMobileSlaveMiner(*unit)) return false;
        enslaves = unit->enslaves;
        if (enslaves.empty()) {
            const auto* building = findRuntimeBuildingTypeById(state.rules, unit->deploysInto);
            if (building) enslaves = building->enslaves;
        }
    } else if (target.kind == WorldObjectKind::Structure) {
        const auto* building = findRuntimeBuildingTypeById(state.rules, target.type);
        if (!building || building->storage <= 0
            || !runtimeBuildingCanGatherResources(*building)) return false;
        enslaves = building->enslaves;
    } else return false;

    return !enslaves.empty()
        && toUpperAsciiLocal(enslaves) == toUpperAsciiLocal(worker.type);
}

bool runtimeSelectionOffersSlaveDelivery(
    const RuntimeWindowState& state,
    const WorldObjectInstance& target
) {
    for (const int objectId : state.selectedObjectIds) {
        const auto* worker = findObjectById(state, objectId);
        if (worker && runtimeSlaveMinerDeliveryTargetLegal(state, *worker, target))
            return true;
    }
    return false;
}

bool queueRuntimeSlaveMinerDeliveryTarget(
    RuntimeWindowState& state,
    const WorldObjectInstance& target
) {
    if (!runtimeSelectionOffersSlaveDelivery(state, target)) return false;
    const int targetId = target.instanceId;
    const std::string targetType = target.type;
    resetLastRuntimeCommand(state, "return", runtimeCellPointForObject(target), targetId, targetType);
    auto selectedIds = state.selectedObjectIds;
    std::sort(selectedIds.begin(), selectedIds.end());
    selectedIds.erase(std::unique(selectedIds.begin(), selectedIds.end()), selectedIds.end());
    std::optional<std::string> acceptedVoice;
    for (const int objectId : selectedIds) {
        auto* worker = findMutableObjectById(state, objectId);
        const auto* home = findObjectById(state, targetId);
        if (!worker || !home || !runtimeSlaveMinerDeliveryTargetLegal(state, *worker, *home)) {
            ++state.lastCommand.rejectedCount;
            continue;
        }

        auto& task = state.slaveMinerTasks.at(objectId);
        task.phase = RuntimeSlaveMinerPhase::Returning;
        task.targetOre = {-1, -1};
        task.workTicksRemaining = 0;
        task.status = "player delivery";
        worker->harvestAutoSearchSuspended = false;
        // The shared return helper cancels the old order while retaining the
        // current walking segment. Cargo and home stay in the existing task.
        if (!issueRuntimeSlaveMinerReturn(state, *worker, *home)) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        ++state.lastCommand.acceptedCount;
        state.specialMechanisms.notifyActivity(objectId, state.tick, "move");
        if (!acceptedVoice) {
            if (const auto* unit = findRuntimeUnitType(state.rules, *worker))
                acceptedVoice = runtimeVoiceEventFromField(state, *worker, "move", unit->voiceMove);
        }
    }

    state.interactionStatus = state.lastCommand.acceptedCount > 0
        ? "returning to " + targetType + " #" + std::to_string(targetId)
            + " workers=" + std::to_string(state.lastCommand.acceptedCount)
        : "workers cannot reach " + targetType + " #" + std::to_string(targetId);
    state.economy.status = state.interactionStatus;
    if (!state.runtimeSuppressPlayerAudioFeedback)
        emitRuntimeSound(state, state.lastCommand.acceptedCount > 0
            ? acceptedVoice.value_or("command move") : "blocked");
    recordRuntimeCommandReplayEvent(state);
    refreshRuntimeActionSummary(state);
    return true;
}
