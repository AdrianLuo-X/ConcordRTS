const BuildingType* runtimeInfantryServiceBuildingRules(
    const RuntimeWindowState& state,
    const WorldObjectInstance& target
) {
    if (target.kind != WorldObjectKind::Structure) {
        return nullptr;
    }
    const BuildingType* building = findRuntimeBuildingTypeById(
        state.rules,
        target.type
    );
    return building != nullptr && (building->hospital || building->armory)
        ? building
        : nullptr;
}

bool runtimeInfantryServiceOwnersMatch(
    const RuntimeWindowState& state,
    const WorldObjectInstance& infantry,
    const WorldObjectInstance& target
) {
    return runtimeRepairDepotCanonicalOwnerId(state, infantry.owner)
        == runtimeRepairDepotCanonicalOwnerId(state, target.owner);
}

RuntimeInfantryServiceEvaluation runtimeInfantryServiceEvaluationFor(
    const RuntimeWindowState& state,
    const WorldObjectInstance& infantry,
    const WorldObjectInstance& target
) {
    return evaluateRuntimeInfantryService(
        infantry,
        target,
        runtimeInfantryServiceBuildingRules(state, target),
        runtimeMaxHealthForObject(state, infantry),
        runtimeInfantryServiceOwnersMatch(state, infantry, target)
    );
}

bool runtimeBehaviorIsPendingInfantryService(
    const RuntimeWindowState& state,
    int objectId,
    const RuntimeObjectBehavior& behavior
) {
    if (behavior.kind != RuntimeBehaviorKind::Move
        || behavior.targetObjectId < 0
        || behavior.pendingAllowedOccupiedObjectId
            != behavior.targetObjectId) {
        return false;
    }
    const WorldObjectInstance* infantry = findObjectById(state, objectId);
    const WorldObjectInstance* target = findObjectById(
        state,
        behavior.targetObjectId
    );
    return infantry != nullptr
        && infantry->kind == WorldObjectKind::Infantry
        && target != nullptr
        && runtimeInfantryServiceBuildingRules(state, *target) != nullptr;
}

bool runtimeSelectionHasInfantryServiceRelationship(
    RuntimeWindowState& state,
    const WorldObjectInstance& target
) {
    if (runtimeInfantryServiceBuildingRules(state, target) == nullptr) {
        return false;
    }
    for (int objectId : state.selectedObjectIds) {
        const WorldObjectInstance* infantry = findObjectById(state, objectId);
        if (infantry != nullptr
            && infantry->alive
            && infantry->kind == WorldObjectKind::Infantry
            && infantry->instanceId != target.instanceId
            && !runtimeObjectDisabledByParasite(state, objectId)
            && runtimeInfantryServiceOwnersMatch(
                state,
                *infantry,
                target
            )) {
            return true;
        }
    }
    return false;
}

bool queueRuntimeInfantryServiceTarget(
    RuntimeWindowState& state,
    const WorldObjectInstance& targetStructure
) {
    if (runtimeInfantryServiceBuildingRules(
            state,
            targetStructure
        ) == nullptr) {
        return false;
    }

    std::vector<int> selectedIds = state.selectedObjectIds;
    std::sort(selectedIds.begin(), selectedIds.end());
    selectedIds.erase(
        std::unique(selectedIds.begin(), selectedIds.end()),
        selectedIds.end()
    );
    int queuedCount = 0;
    for (int objectId : selectedIds) {
        WorldObjectInstance* infantry = findMutableObjectById(state, objectId);
        WorldObjectInstance* target = findMutableObjectById(
            state,
            targetStructure.instanceId
        );
        if (infantry == nullptr || target == nullptr
            || runtimeObjectDisabledByParasite(state, objectId)
            || runtimeInfantryRequiresManualUndeploy(state, *infantry)
            || !runtimeInfantryServiceEvaluationFor(
                state,
                *infantry,
                *target
            ).usable) {
            continue;
        }
        const std::optional<RuntimeCellPoint> entryCell =
            findRuntimeEngineerEntryCell(
                state,
                *infantry,
                *target,
                "infantry-service-enter",
                2
            );
        if (!entryCell.has_value()
            || !releaseRuntimeRepairDepotForCommand(
                state,
                infantry->instanceId,
                "infantry service command"
            )) {
            continue;
        }

        if (!eraseRuntimeBehavior(state, infantry->instanceId)) continue;
        RuntimeObjectBehavior behavior;
        behavior.kind = RuntimeBehaviorKind::Move;
        behavior.state = RuntimeBehaviorState::Queued;
        behavior.targetObjectId = target->instanceId;
        behavior.moveTarget = *entryCell;
        behavior.pendingAllowedOccupiedObjectId = target->instanceId;
        setRuntimeMissionIntent(
            state,
            *infantry,
            RuntimeMissionIntent::Entering,
            "infantryServiceQueued"
        );
        state.behaviors[infantry->instanceId] = std::move(behavior);
        ++queuedCount;
    }

    if (queuedCount <= 0) {
        return false;
    }
    state.interactionStatus = "entering " + targetStructure.type
        + " #" + std::to_string(targetStructure.instanceId)
        + " infantry=" + std::to_string(queuedCount);
    state.economy.status = state.interactionStatus;
    if (!state.runtimeSuppressPlayerAudioFeedback) {
        emitRuntimeSound(
            state,
            runtimeVoiceEventForSelectedUnits(state, "enter")
                .value_or("command target")
        );
    }
    refreshRuntimeActionSummary(state);
    return true;
}

bool consumeRuntimeBlockedInfantryServiceTarget(
    RuntimeWindowState& state,
    const WorldObjectInstance& targetStructure
) {
    if (!runtimeSelectionHasInfantryServiceRelationship(
            state,
            targetStructure
        )) {
        return false;
    }
    state.interactionStatus = targetStructure.type + " #"
        + std::to_string(targetStructure.instanceId)
        + " cannot service selected infantry";
    state.economy.status = state.interactionStatus;
    if (!state.runtimeSuppressPlayerAudioFeedback) {
        emitRuntimeSound(state, "blocked");
    }
    refreshRuntimeActionSummary(state);
    return true;
}

bool completeRuntimeInfantryService(
    RuntimeWindowState& state,
    int infantryObjectId
) {
    const auto behavior = state.behaviors.find(infantryObjectId);
    if (behavior == state.behaviors.end()
        || !runtimeBehaviorIsPendingInfantryService(
            state,
            infantryObjectId,
            behavior->second
        )) {
        return false;
    }
    WorldObjectInstance* infantry = findMutableObjectById(
        state,
        infantryObjectId
    );
    WorldObjectInstance* target = findMutableObjectById(
        state,
        behavior->second.targetObjectId
    );
    if (infantry == nullptr || target == nullptr
        || runtimeCellDistanceToObjectFootprint(
            state,
            runtimeCellPointForObject(*infantry),
            *target
        ) > 0.001) {
        return false;
    }

    // Original RA2 places the returning soldier at the upper-left side of
    // the service building. Use that authored direction, then let the normal
    // deterministic passability search select the nearest legal subcell.
    const std::optional<RuntimeCellPoint> exitCell =
        findRuntimePassableCellNear(
            state,
            *infantry,
            {target->x - 1, target->y - 1},
            4
        );
    if (!exitCell.has_value()) {
        state.simulationStatus = "infantry service exit blocked #"
            + std::to_string(infantryObjectId);
        return false;
    }

    const BuildingType* building = runtimeInfantryServiceBuildingRules(
        state,
        *target
    );
    const RuntimeInfantryServiceResult result = applyRuntimeInfantryService(
        *infantry,
        *target,
        building,
        runtimeMaxHealthForObject(state, *infantry),
        runtimeInfantryServiceOwnersMatch(state, *infantry, *target)
    );
    const int targetId = target->instanceId;
    const std::string targetType = target->type;

    releaseRuntimeInfantrySubcellReservation(state, infantryObjectId);
    setRuntimeObjectCell(state, *infantry, *exitCell);
    setRuntimeMissionIntent(
        state,
        *infantry,
        RuntimeMissionIntent::Idle,
        result.accepted
            ? "infantryServiceComplete"
            : "infantryServiceNoEffect"
    );
    clearRuntimeRenderPosition(*infantry);
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    state.repaintRequested = true;
    state.interactionStatus = result.accepted
        ? targetType + " serviced infantry #"
            + std::to_string(infantryObjectId)
            + " charges=" + std::to_string(result.ammoAfter)
        : targetType + " released infantry #"
            + std::to_string(infantryObjectId)
            + " without service ("
            + runtimeInfantryServiceVetoName(result.veto) + ")";
    state.economy.status = state.interactionStatus;
    state.simulationStatus = state.interactionStatus;
    invalidateRuntimeObjectProjectionCache(state);
    (void)targetId;
    refreshRuntimeActionSummary(state);
    return true;
}
