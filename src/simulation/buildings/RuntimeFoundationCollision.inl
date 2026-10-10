// Traffic maps can omit selected movers or retain a view from before a world
// mutation. A building's solid foundation is a separate movement constraint.
const std::unordered_map<int, std::vector<std::size_t>>& runtimeSolidFoundationsByCell(
    const RuntimeWindowState& state
) {
    const auto& objects = state.world.getObjects();
    if (state.foundationCollisionTick == state.tick
        && state.foundationCollisionObjects == objects.data()
        && state.foundationCollisionObjectCount == objects.size())
        return state.foundationCollisionByCell;
    auto& cells = state.foundationCollisionByCell;
    cells.clear();
    for (std::size_t index = 0; index < objects.size(); ++index) {
        const auto& object = objects[index];
        if (!object.alive || runtimeObjectOffGroundByMagBeam(state, object.instanceId)) continue;
        std::vector<RuntimeCellPoint> footprint;
        if (object.kind == WorldObjectKind::Structure) {
            footprint = runtimeStructureFootprintCellsForObject(state, object);
        } else if (runtimeMissionIsMcvDeployPending(object)) {
            const auto* unit = findRuntimeUnitType(state.rules, object);
            const auto pending = state.behaviors.find(object.instanceId);
            if (!unit || unit->deploysInto.empty() || pending == state.behaviors.end()) continue;
            WorldObjectInstance futureYard;
            futureYard.kind = WorldObjectKind::Structure; futureYard.type = unit->deploysInto;
            const RuntimeCellPoint corner = runtimePlacementTopLeftForAnchor(
                pending->second.moveTarget, runtimeStructureFootprintForObject(state, futureYard));
            futureYard.x = corner.x; futureYard.y = corner.y;
            footprint = runtimeStructureFootprintCellsForObject(state, futureYard);
        } else continue;
        for (const auto& cell : footprint) {
            const int key = runtimeCellKey(cell.x, cell.y);
            if (key >= 0) cells[key].push_back(index);
        }
    }
    state.foundationCollisionTick = state.tick;
    state.foundationCollisionObjects = objects.data();
    state.foundationCollisionObjectCount = objects.size();
    return cells;
}

bool runtimeFoundationBlocksMovement(
    const RuntimeWindowState& state, const WorldObjectInstance& mover,
    const RuntimeCellPoint& cell, int allowedOccupiedObjectId,
    bool allowCrushableOccupants, RuntimeGatePassageMode gateMode
) {
    const auto& foundations = runtimeSolidFoundationsByCell(state);
    const auto found = foundations.find(runtimeCellKey(cell.x, cell.y));
    if (found == foundations.end()) return false;
    const auto openGates = state.gateIdsByCell.contains(runtimeCellKey(cell.x, cell.y))
        ? runtimeTransparentGateObjectIds(state, mover, gateMode)
        : std::unordered_set<int>{};
    for (const auto index : found->second) {
        const auto& building = state.world.getObjects()[index];
        if (!building.alive || building.instanceId == mover.instanceId
            || openGates.contains(building.instanceId)) continue;
        // Reserve an accepted MCV's complete future foundation through its
        // turn, including diagonal side cells. Stop/death revoke the mission.
        if (runtimeMissionIsMcvDeployPending(building)) return true;
        if (building.instanceId == allowedOccupiedObjectId
            && runtimeCellIsAllowedOccupiedStructureFootprint(
                state, cell, allowedOccupiedObjectId, &mover)) continue;
        if (allowCrushableOccupants && runtimeCrusherUsesCrushingMovement(state, mover)
            && runtimeCrusherCanCrushObject(state, mover, building, cell)) continue;
        return true;
    }
    return false;
}
