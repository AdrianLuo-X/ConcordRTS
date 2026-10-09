// The authored bridge art has intact, light and heavy damage families.
// Reuse the existing health conditions and one stable native variant per band;
// the six shape variants are not six invented percentage thresholds.
int runtimeBridgeSegmentDamageBand(const RuntimeWindowState& state, int health) {
    const auto& conditions = state.rules.getSpecialMechanismRules();
    const double yellow = std::clamp(conditions.conditionYellow, 0.0, 1.0);
    const double red = std::clamp(conditions.conditionRed, 0.0, yellow);
    const double fraction = static_cast<double>(health)
        / std::max(1, state.rules.getBridgeStrength());
    return fraction <= red ? 4 : (fraction <= yellow ? 1 : 0);
}

int runtimeInitialBridgeSegmentHealth(
    const RuntimeWindowState& state, const RuntimeBridgeDamageTarget& target
) {
    const int strength = std::max(1, state.rules.getBridgeStrength());
    const auto found = state.terrainByCell.find(
        runtimeCellKey(target.anchor.x, target.anchor.y));
    if (found == state.terrainByCell.end() || found->second == nullptr)
        return strength;
    const auto& terrain = *found->second;
    const int shape = target.high
        ? terrain.overlayFrame - (target.alongX ? 0 : 9)
        : runtimeLowBridgeOffset(terrain) - (target.alongX ? 0 : 9);
    // Maps and older saves can author damage without a saved HP record.
    // A first cannon hit must not heal that appearance back to intact.
    if (shape >= 1 && shape <= 6) {
        const auto& conditions = state.rules.getSpecialMechanismRules();
        const double yellow = std::clamp(conditions.conditionYellow, 0.0, 1.0);
        const double fraction = shape >= 4
            ? std::clamp(conditions.conditionRed, 0.0, yellow) : yellow;
        return std::max(1, static_cast<int>(std::floor(strength * fraction)));
    }
    return strength;
}

void updateRuntimeBridgeSegmentDamageFrame(
    RuntimeWindowState& state, const RuntimeBridgeDamageTarget& target, int health
) {
    const int band = runtimeBridgeSegmentDamageBand(state, health);
    bool changed = false;
    if (target.high) {
        const auto found = state.terrainByCell.find(
            runtimeCellKey(target.anchor.x, target.anchor.y));
        if (found == state.terrainByCell.end() || found->second == nullptr) return;
        auto& terrain = *const_cast<TerrainCell*>(found->second);
        const int frame = (target.alongX ? 0 : 9) + band;
        if (terrain.overlayFrame != frame) {
            terrain.overlayFrame = frame;
            changed = true;
        }
    } else {
        for (int lane = 0; lane < 3; ++lane) {
            const auto& point = target.physicalCells[static_cast<std::size_t>(lane)];
            const auto found = state.terrainByCell.find(runtimeCellKey(point.x, point.y));
            if (found == state.terrainByCell.end() || found->second == nullptr) continue;
            auto& terrain = *const_cast<TerrainCell*>(found->second);
            // Preserve authored gap ends until this segment itself collapses.
            const int oldShape = runtimeLowBridgeOffset(terrain) - (target.alongX ? 0 : 9);
            if (oldShape >= 7 && oldShape <= 8) continue;
            const int offset = (target.alongX ? 0 : 9) + band;
            if (runtimeLowBridgeOffset(terrain) != offset || terrain.overlayFrame != lane) {
                runtimeSetLowBridgeOverlayOffset(terrain, target.material, offset, lane);
                changed = true;
            }
        }
    }
    if (changed) refreshRuntimeBridgeMutationState(state, target.physicalCells);
}
