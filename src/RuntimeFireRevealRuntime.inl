void notifyRuntimeWeaponFired(RuntimeWindowState& state, const WorldObjectInstance& attacker,
    const WorldObjectInstance& target, const WeaponType& weapon) {
    if (!state.visionEnabled) return;
    const auto previous = state.vision.revisionFor(state.economy.owner);
    state.vision.notifyWeaponFired(attacker, target, weapon, state.tick, state.config.fixedTickHz);
    if (state.vision.revisionFor(state.economy.owner) != previous) {
        state.radarTerrainKey.clear();
        state.projectedObjectsCacheValid = false;
        state.repaintRequested = true;
    }
}
