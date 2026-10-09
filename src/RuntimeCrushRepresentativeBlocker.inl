// The occupancy entry is only a hint. A current world object must independently
// pass the original scanner's complete blocking predicate at this destination.
// Stale entries can lose this shortcut, but can never manufacture a blocker.
bool runtimeCrushRepresentativeProvesBlocked(
    const RuntimeWindowState& state,
    const WorldObjectInstance& crusher,
    const RuntimeCellPoint& cell,
    const std::unordered_set<int>* additionallyIgnoredObjectIds = nullptr
) {
    if (state.tickOccupancyCacheTick != state.tick) return false;
    const int key = runtimeCellKey(cell.x, cell.y);
    if (key < 0) return false;
    const auto found = state.tickOccupiedByCell.find(key);
    if (found == state.tickOccupiedByCell.end()
        || runtimeOccupantIdIsTerrain(found->second)) return false;
    const auto* target = findObjectById(state, found->second);
    if (target == nullptr) return false;
    const RuntimeCrushCandidateList candidate{target};
    return runtimeScanCrushTargetsAtCell(state, crusher, cell, -1,
        additionallyIgnoredObjectIds, &candidate).blocked;
}
