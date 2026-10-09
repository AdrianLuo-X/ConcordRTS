bool updateRuntimePublicBuildingLocations(RuntimeWindowState& state,
    const std::vector<RuntimeVisionObserverDefinition>& observers) {
    if (!state.rulesLoaded) return false;
    bool changed = false;
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || object.health <= 0 || object.rubble
            || object.kind != WorldObjectKind::Structure || object.buildAnimTicksRemaining > 0) continue;
        const auto* building = state.rules.findBuildingType(object.type);
        if (!building || !building->revealToAll) continue;
        const auto owner = runtimeCanonicalHumanHouseOwner(object.owner);
        if (owner.empty()) continue;
        const RuntimeCellPoint cell{object.x, object.y};
        auto found = state.triggers.publicBuildingLocations.find(object.instanceId);
        if (found == state.triggers.publicBuildingLocations.end()
            || found->second.owner != owner || found->second.cell != cell) {
            RuntimePublicBuildingLocation location;
            location.owner = owner; location.cell = cell; location.noticeTick = state.tick;
            state.triggers.publicBuildingLocations[object.instanceId] = location;
            if (owner != runtimeCanonicalHumanHouseOwner(state.economy.owner)) {
                state.playerLastRadarCell = RuntimeMultiplayerGameplayCell{cell.x, cell.y};
                state.repaintRequested = true;
            }
            changed = true;
        }
        // Retail exposes CurrentSight + 3 cells and reapplies public locations
        // after reshrouding. This is exploration, not continuing unit sight.
        const int radius = std::clamp(building->sight, 0, 509) + 3;
        for (const auto& observer : observers) {
            if (runtimeCanonicalHumanHouseOwner(observer.owner) == owner) continue;
            changed = state.vision.exploreArea(observer.owner, cell.x, cell.y, radius) || changed;
        }
    }
    return changed;
}

bool runtimePublicBuildingNoticeVisible(const RuntimeWindowState& state,
    const RuntimePublicBuildingLocation& location) {
    return location.noticeTick >= 0 && state.tick >= location.noticeTick
        && static_cast<long long>(state.tick) - location.noticeTick < std::max(1, state.config.fixedTickHz) * 8LL
        && location.owner != runtimeCanonicalHumanHouseOwner(state.economy.owner);
}
