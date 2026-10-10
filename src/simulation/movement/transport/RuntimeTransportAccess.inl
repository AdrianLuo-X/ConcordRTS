// A hover transport uses the cells touching its hull. A clear cell beyond a
// wall or across open water is not an embarkation/disembarkation point.
std::optional<RuntimeCellPoint> runtimeAmphibiousTransportAccessCell(
    RuntimeWindowState& state, const WorldObjectInstance& passenger,
    const WorldObjectInstance& transport, bool boarding,
    const std::unordered_set<int>* reservedCells = nullptr
) {
    const auto hull = runtimeCellPointForObject(transport);
    const auto origin = boarding ? runtimeCellPointForObject(passenger) : hull;
    std::vector<RuntimeCellPoint> candidates;
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        if (dx != 0 || dy != 0) candidates.push_back({hull.x + dx, hull.y + dy});
    }
    std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
        const double da = runtimeCellDistance(origin, a), db = runtimeCellDistance(origin, b);
        if (da != db) return da < db;
        if (a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    });
    for (const auto& cell : candidates) {
        if (!findRuntimePassableCellNear(state, passenger, cell, 0, -1, reservedCells)) continue;
        if (std::abs(terrainLevelAtRuntimeCell(state, cell.x, cell.y) - transport.level) > 1) continue;
        if (runtimeInfantryUsesGroundSubcells(passenger)) {
            ensureRuntimeSubcellsInitialized(state);
            if (state.subcells.availableSubCells(
                    state.world.getObjects(), state.world.getTerrainObjects(),
                    {cell.x, cell.y, runtimeCellKey(cell.x, cell.y),
                        runtimeCellIsBridge(state, cell.x, cell.y)},
                    {passenger.instanceId},
                    runtimeGateSubcellContext(state, &passenger, RuntimeGatePassageMode::Commit)).empty()) continue;
        }
        if (boarding && origin != cell
            && findRuntimePath(state, passenger, origin, cell, transport.instanceId).empty()) continue;
        if (boarding && transport.kind == WorldObjectKind::Unit
            && !runtimeTransportBoardingAccessBodyClear(state, passenger, transport, cell)) continue;
        return cell;
    }
    return std::nullopt;
}
