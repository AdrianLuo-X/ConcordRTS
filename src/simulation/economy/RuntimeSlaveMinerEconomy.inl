// Workers in a mobile miner retain their identity and health, but do not
// occupy a ground subcell or participate as independent active units.
































bool advanceRuntimeSlaveMinerWorkerTask(
    RuntimeWindowState& state, WorldObjectInstance& worker, RuntimeSlaveMinerTask& task
) {
    auto context = makeRuntimeHouseEconomyContext(state.economy, state.computerMiningScopeActive);
    return advanceRuntimeSlaveMinerWorkerTask(state, worker, task, context, state.economy);
}


