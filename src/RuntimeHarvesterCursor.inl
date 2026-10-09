// The ore command uses the native attack reticle, including unarmed CMIN.
// Keep object actions, tool modes and force fire ahead of this cell preview.
bool runtimeSelectionOffersHarvestCursor(const RuntimeWindowState& state, RuntimeCellPoint cell) {
    if (!runtimeCellHasHarvestableResource(state, cell)
        || (runtimeLocalVisionRestricted(state)
            && !state.vision.isCellExploredBy(state.economy.owner, cell.x, cell.y))) return false;
    for (const int id : state.selectedObjectIds) {
        const auto* object = findObjectById(state, id);
        if (!object || object->owner != state.economy.owner
            || !runtimeObjectAllowsMoveToCell(state, *object, cell)) continue;
        if (runtimeObjectIsAttachedSlaveWorker(state, *object)
            && runtimeObjectCanReceiveMoveCommand(state, *object)) return true;
        const UnitType* unit = nullptr;
        if (object->kind == WorldObjectKind::Structure) {
            // A deployed Slave Miner handles an ore order by packing and
            // moving to the new field. Preview its resulting mobile type.
            if (!runtimeStructureCanRepack(state, *object)) continue;
            const auto* building = findRuntimeBuildingTypeById(state.rules, object->type);
            if (building) unit = findRuntimeUnitTypeById(state.rules, building->undeploysInto);
        } else {
            if (!runtimeObjectCanReceiveMoveCommand(state, *object)) continue;
            unit = findRuntimeUnitType(state.rules, *object);
        }
        if (unit && (unit->harvester || runtimeUnitIsMobileSlaveMiner(*unit))) return true;
    }
    return false;
}
