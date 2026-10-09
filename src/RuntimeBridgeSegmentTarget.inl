// A high bridge SHP owns its two transverse side lanes, not the previous
// or next axial segment. Resolve those lanes before the radial fallback.
std::optional<RuntimeBridgeDamageTarget> runtimeHighBridgeDamageTargetForSideLane(
    const RuntimeWindowState& state, const RuntimeCellPoint& point
) {
    const auto laneCell = state.terrainByCell.find(runtimeCellKey(point.x, point.y));
    if (laneCell == state.terrainByCell.end() || laneCell->second == nullptr
        || laneCell->second->hasOverlay)
        return std::nullopt;
    for (int lane = 1; lane < 3; ++lane) {
        for (bool alongX : {true, false}) {
            const RuntimeCellPoint anchor{
                point.x - (alongX ? 0 : lane), point.y - (alongX ? lane : 0)};
            const auto found = state.terrainByCell.find(runtimeCellKey(anchor.x, anchor.y));
            if (found == state.terrainByCell.end() || found->second == nullptr) continue;
            const auto& terrain = *found->second;
            if (terrain.level != laneCell->second->level
                || !runtimeOverlayLooksHighBridge(terrain)
                || !runtimeOverlayProvidesBridgeDeck(terrain)
                || !runtimeHighBridgeOverlayMatchesAxis(terrain, alongX)) continue;
            RuntimeBridgeDamageTarget target;
            target.anchor = anchor; target.high = true; target.alongX = alongX;
            target.material = runtimeHighBridgeMaterialForOverlay(terrain);
            for (int side = 0; side < 3; ++side)
                target.physicalCells.push_back({
                    anchor.x + (alongX ? 0 : side), anchor.y + (alongX ? side : 0)});
            return target;
        }
    }
    return std::nullopt;
}
