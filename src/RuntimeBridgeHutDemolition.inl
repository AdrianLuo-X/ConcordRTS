// Huts address one registered bridge, independent of blast radius. Preserve
// the repair building and native bank tiles so engineers can restore it.
RuntimeBridgeDamageResult demolishRuntimeBridgeByHut(
    RuntimeWindowState& state, const WorldObjectInstance& hut,
    int sourceObjectId, const std::string& sourceOwner,
    RuntimeDeathWeaponCause deathWeaponCause = {}
) {
    RuntimeBridgeDamageResult result;
    if (!runtimeBridgeHutDemolitionAvailable(state, hut)) return result;
    const RuntimeBridgeRepairSpan span = state.bridgeRepairSpansByHut.at(hut.instanceId);
    result.hit = true;
    result.anchor = span.startHead;
    result.damage = state.rules.getBridgeStrength();
    int first = std::numeric_limits<int>::max(), last = std::numeric_limits<int>::min();
    for (const auto& cell : span.cells) if (cell.requiresOverlay) {
        const int axis = span.alongX ? cell.point.x : cell.point.y;
        first = std::min(first, axis); last = std::max(last, axis);
    }
    std::vector<RuntimeCellPoint> changedCells;
    std::unordered_set<int> collapsedKeys;
    for (const auto& cell : span.cells) {
        const auto found = state.terrainByCell.find(runtimeCellKey(cell.point.x, cell.point.y));
        if (found == state.terrainByCell.end() || !found->second) continue;
        auto& terrain = *const_cast<TerrainCell*>(found->second);
        if (span.high) {
            if (!cell.requiresOverlay) continue;
            const int axis = span.alongX ? cell.point.x : cell.point.y;
            if (runtimeOverlayLooksHighBridge(terrain)
                && runtimeHighBridgeOverlayMatchesAxis(terrain, span.alongX)
                && runtimeHighBridgeMaterialForOverlay(terrain) == span.material) {
                if (axis == first || axis == last) {
                    const int cap = axis == first ? (span.alongX ? 8 : 16) : (span.alongX ? 7 : 17);
                    result.destroyed = result.destroyed || terrain.overlayFrame != cap;
                    terrain.overlayFrame = cap;
                } else {
                    runtimeClearBridgeOverlay(terrain);
                    result.destroyed = true;
                }
            }
            state.bridgeHealthByCell[runtimeCellKey(cell.point.x, cell.point.y)] = 0;
            for (int lane = 0; lane < 3; ++lane) {
                const RuntimeCellPoint point{cell.point.x + (span.alongX ? 0 : lane),
                    cell.point.y + (span.alongX ? lane : 0)};
                changedCells.push_back(point);
                collapsedKeys.insert(runtimeCellKey(point.x, point.y));
            }
        } else {
            if (!runtimeLowBridgeOverlayMatches(terrain, span.material, span.alongX, true)) continue;
            const int oldOffset = runtimeLowBridgeOffset(terrain);
            const int offset = cell.requiresOverlay ? (span.alongX ? 26 : 27)
                : oldOffset + (oldOffset % 2 == 0 ? 1 : 0);
            if (!cell.requiresOverlay && !runtimeLowBridgeOverlayIsHead(terrain)) continue;
            result.destroyed = result.destroyed || oldOffset != offset;
            runtimeSetLowBridgeOverlayOffset(terrain, span.material, offset, cell.expectedFrame);
            changedCells.push_back(cell.point);
            collapsedKeys.insert(runtimeCellKey(cell.point.x, cell.point.y));
            if (cell.requiresOverlay && cell.expectedFrame == 0)
                state.bridgeHealthByCell[runtimeCellKey(cell.point.x, cell.point.y)] = 0;
        }
    }
    result.destroyedCellKeys.assign(collapsedKeys.begin(), collapsedKeys.end());
    std::sort(result.destroyedCellKeys.begin(), result.destroyedCellKeys.end());
    destroyRuntimeBridgeCollapseOccupants(state, collapsedKeys,
        sourceObjectId, sourceOwner, deathWeaponCause);
    if (result.destroyed) {
        refreshRuntimeBridgeMutationState(state, changedCells);
        if (state.campaignMode || state.cooperativeSession.active())
            for (int key : result.destroyedCellKeys) state.triggers.campaignBridgeDestroyedTickByCell[key] = state.tick;
    }
    return result;
}
