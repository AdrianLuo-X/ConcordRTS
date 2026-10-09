// Cold bootstrap is deliberately separate from normal in-session Load. Only
// an unattached, freshly initialized world may adopt a saved initial tick.
bool restoreRuntimeMultiplayerLobbySnapshot(RuntimeWindowState& state,
    RuntimeMultiplayerRuntimeBridge& bridge, std::string& errorMessage) {
    auto& lan = bridge.lan(); // Joins the loading-time transport worker first.
    const auto* descriptor = lan.lobbyResumeDescriptor();
    if (!descriptor) { errorMessage.clear();return true; }
    const auto& session = lan.session();
    if (state.multiplayerBridge || bridge.runtimeAttached() || state.tick != 0
        || !state.config.multiplayerActive || lan.phase() != RuntimeMultiplayerLanPhase::Running
        || !lan.lobbyResumeSnapshotReady() || !session.matchSetup()
        || session.startTick() != descriptor->identity.snapshotTick + 1
        || session.authoritativeTick() != session.startTick() || !session.commands().empty()
        || !session.saveLoadHandshakes().empty()) {
        errorMessage = "resume bootstrap requires a fresh world at the saved initial session barrier";return false;
    }
    RuntimeMultiplayerResumeSave saved;
    std::uint64_t setupFingerprint = 0;
    if (!readRuntimeMultiplayerResumeSave(lan.lobbyResumeSnapshotBytes(), saved, errorMessage)
        || saved.descriptor != *descriptor || saved.compatibility != session.config().compatibility
        || !runtimeMultiplayerGameplaySetupFingerprint(*session.matchSetup(), setupFingerprint, errorMessage)
        || setupFingerprint != saved.descriptor.identity.gameplaySetupFingerprint
        || setupFingerprint != state.config.multiplayerSetupHash
        || session.timelineEpoch() != state.config.multiplayerTimelineEpoch) {
        if (errorMessage.empty()) errorMessage = "resume bootstrap room, content, or snapshot identity differs";
        return false;
    }
    // The world is still private to loading. The normal Load validator stays
    // strict (saved tick <= live tick); only this guarded bootstrap supplies
    // the initial barrier promised by the newly started canonical room.
    struct RestoreTickOnFailure {
        RuntimeWindowState& state;
        int previousTick;
        bool committed = false;
        ~RestoreTickOnFailure() { if (!committed) state.tick = previousTick; }
    } rollback{state, state.tick};
    state.tick = saved.descriptor.identity.snapshotTick;
    RuntimeMultiplayerSnapshotMemoryImage rebound;
    RuntimeMultiplayerSnapshotStagedState staged;
    if (!rebindRuntimeMultiplayerSaveAsLoad(state, saved.bytes, 1, session.timelineEpoch(), state.tick, rebound, errorMessage)
        || !stageRuntimeMultiplayerSnapshot(state, rebound.bytes, rebound.descriptor, staged, errorMessage)) return false;
    commitRuntimeMultiplayerSnapshotAuthority(state, std::move(staged));
    rollback.committed = true;
    invalidateRuntimeObjectProjectionCache(state);
    state.sidebarStaticKey.clear();
    state.radarProjectionCacheValid = false;
    state.cachedRadarResourceRevision = std::numeric_limits<std::uint64_t>::max();
    errorMessage.clear();return true;
}
