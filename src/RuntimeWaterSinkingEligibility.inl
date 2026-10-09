// Open-water survival follows authored movement permissions, independently
// of traffic and the old cell's bridge layer during a teleport.
bool runtimeVehicleCannotSurviveOpenWaterAtCell(const RuntimeWindowState& state,
    const WorldObjectInstance& object, RuntimeCellPoint cell) {
    const auto* unit = findRuntimeUnitType(state.rules, object);
    if (object.kind != WorldObjectKind::Unit || object.airborne || !unit
        || unit->naval || unit->underwater || unit->organic) return false;
    const auto found = state.terrainByCell.find(runtimeCellKey(cell.x, cell.y));
    const TerrainCell* terrain = found == state.terrainByCell.end() ? nullptr : found->second;
    if (!terrain) for (const auto& candidate : state.world.getTerrainCells()) {
        if (candidate.x == cell.x && candidate.y == cell.y) { terrain = &candidate; break; }
    }
    if (!terrain) return false;
    const auto info = runtimeCachedTerrainInfoForCell(state, *terrain);
    return info.water && !info.bridge && !runtimeAuthoredLandOrdinalAllowsMoverUncached(
        state, RuntimeTerrainLandType::Water, object);
}
