// Private presentation for the incomplete two-click Chronosphere command.
// Its original SHP loops independently of mouse position and world effects.


void clearRuntimeLocalChronosphereMarker(RuntimeWindowState& state) {
    stopRuntimeChronosphereSelectionAudio(state);
    if (!state.localChronosphereMarker) return;
    state.localChronosphereMarker.reset();
    state.repaintRequested = true;
}

void beginRuntimeLocalChronosphereMarker(
    RuntimeWindowState& state, const std::string& weaponId, RuntimeCellPoint cell
) {
    const auto* rule = state.rules.findSuperWeaponType(weaponId);
    if (!rule || rule->placementAnim.empty()
        || toLowerAsciiLocal(rule->type + " " + rule->action).find("chrono") == std::string::npos)
        return;
    const auto& previous = state.localChronosphereMarker;
    if (previous && previous->animation.owner == state.economy.owner
        && previous->animation.weaponId == weaponId
        && previous->animation.cell.x == cell.x && previous->animation.cell.y == cell.y) return;
    RuntimeLocalChronosphereMarker marker;
    marker.startedTick = state.tick;
    auto& animation = marker.animation;
    animation.owner = state.economy.owner;
    animation.weaponId = weaponId;
    animation.cell = cell;
    animation.level = terrainLevelAtRuntimeCell(state, cell.x, cell.y);
    animation.animId = rule->placementAnim;
    animation.lifecycleState = "LocalChronosphereSelection";
    animation.renderIntent = "superWeaponEffect";
    animation.hasWorldPosition = true;
    animation.worldX = cell.x;
    animation.worldY = cell.y;
    animation.worldLevel = animation.level + 5.0 * 2.449489742783178 / 256.0;
    initializeRuntimeSuperWeaponAnimation(state, animation);
    if (animation.animationFrameCount <= 0) return;
    state.localChronosphereMarker = std::move(marker);
    state.repaintRequested = true;
    startRuntimeChronosphereSelectionAudio(state,
        state.localChronosphereMarker->animation, runtimeWorldAudioClockNow(state));
}




