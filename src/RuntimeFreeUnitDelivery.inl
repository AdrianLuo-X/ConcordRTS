void scaleRuntimeComputerAiObjectHealth(const RuntimeWindowState& state,
    const RuntimeComputerAiState& ai, WorldObjectInstance& object);

bool advanceRuntimeFreeUnitDeliveries(RuntimeWindowState& state) {
    if (state.factoryDeliveries.pendingFreeUnits().empty()) return false;
    // Attempts are bounded and stable across peers. The entitlement survives
    // a blocked exit and save/load, and is consumed only after creation.
    if (state.tick % std::max(1, state.config.fixedTickHz / 4) != 0) return false;
    bool changed = false;
    const auto grants = state.factoryDeliveries.pendingFreeUnits();
    for (const auto& grant : grants) {
        const auto* host = findObjectById(state, grant.hostId);
        const auto* unitType = findRuntimeUnitTypeById(state.rules, grant.unitType);
        if (host == nullptr || !host->alive || host->kind != WorldObjectKind::Structure
            || host->owner != grant.owner || host->type != grant.buildingType
            || unitType == nullptr) {
            state.factoryDeliveries.completeFreeUnit(grant.hostId);
            changed = true;
            continue;
        }
        const WorldObjectInstance anchor = *host;
        RuntimeBuildCatalogItem item;
        item.kind = runtimeBuildKindForUnitTypeId(state.rules, grant.unitType);
        item.typeId = grant.unitType;
        item.cost = 0;
        item.buildTicks = 1;
        item.buildLimit = unitType->buildLimit;
        const auto cell = findRuntimeSpawnCellNearObjectForOwner(state, item, anchor, grant.owner);
        if (!cell) continue;
        auto* unit = addRuntimeObjectInstanceForOwner(state, grant.owner, item.kind, item.typeId, *cell);
        if (unit == nullptr) continue;
        unit->mission = "Guard";
        applyRuntimeProducedObjectVeterancy(state, *unit);
        recordRuntimeCompletedBuild(state, grant.owner, item);
        for (auto& [ownerKey, ai] : state.computerAiByOwner) {
            if (ai.owner == grant.owner) {
                scaleRuntimeComputerAiObjectHealth(state, ai, *unit);
                ++ai.freeUnitsReceived;
                break;
            }
        }
        queueRuntimeProductionExitMove(state, *unit, anchor);
        state.factoryDeliveries.completeFreeUnit(grant.hostId);
        changed = true;
    }
    return changed;
}
