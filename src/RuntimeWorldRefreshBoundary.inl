bool completeRuntimeWorldRefresh(RuntimeWindowState& state) {
    RuntimePerfTraceScope stageTrace("completeRuntimeWorldRefresh", state.tick);
    // Reconcile deaths, captures, selling and replaced orders before power is
    // rebuilt. Only the main simulation pass may perform a money transfer.
    bool changed = advanceRuntimeDiskDrains(state, false);
    if (state.navigationRefreshPending) {
        refreshRuntimeNavigationOccupancyStats(state);
        state.navigationRefreshPending = false;
        changed = true;
    }
    if (state.powerRefreshPending) {
        rebuildRuntimeEconomyPower(state);
        // A map action can give a factory to either human after the ordinary
        // economy pass. Both accounts must observe it before this tick ends.
        for (const auto& authority : state.humanHouseAuthorities) {
            RuntimeHumanHouseScope house(state, authority.owner);
            if (house) {
                refreshRuntimeEconomyMetricsForCurrentOwner(state);
                (void)refreshRuntimeFactoryPlantCatalog(state);
            }
        }
        state.powerRefreshPending = false;
        changed = true;
    }
    return changed;
}
