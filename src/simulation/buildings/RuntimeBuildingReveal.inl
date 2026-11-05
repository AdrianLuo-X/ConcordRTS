bool updateRuntimePublicBuildingLocations(RuntimeWindowState& state,
    const std::vector<RuntimeVisionObserverDefinition>& observers) {
    RuntimeSessionVisionPhaseOperations operations(state);
    RuntimeVisionPhaseContext context = runtimeVisionPhaseContext(state);
    return updateRuntimePublicBuildingLocations(context, operations, observers);
}

bool runtimePublicBuildingNoticeVisible(const RuntimeWindowState& state,
    const RuntimePublicBuildingLocation& location) {
    return location.noticeTick >= 0 && state.tick >= location.noticeTick
        && static_cast<long long>(state.tick) - location.noticeTick < std::max(1, state.config.fixedTickHz) * 8LL
        && location.owner != runtimeCanonicalHumanHouseOwner(state.economy.owner);
}
