std::uint64_t runtimeGattlingSoundHandle(int objectId, int channel = 0) {
    return 0x0400000000000000ULL
        | (std::uint64_t(std::uint32_t(objectId)) << 2) | unsigned(channel);
}

bool advanceRuntimeGattlingWeaponAudio(RuntimeWindowState& state, std::uint64_t nowMilliseconds) {
    if (!state.rulesLoaded || !state.worldAudioCatalogLoaded) return false;
    std::vector<RuntimeDiskLoopSource> sources;
    for (const auto& [id, behavior] : state.behaviors) {
        if (behavior.kind != RuntimeBehaviorKind::Attack || !runtimeGattlingFiringRunActive(state, id)) continue;
        const auto* object = findObjectById(state, id);
        if (!object) continue;
        // A load rehydrates an already-hot run from saved gameplay clocks.
        // Otherwise the first successful discharge arms the local voice.
        const auto heat = state.gattlingWeaponTimers.find(id);
        const bool restoredHotRun = !state.gattlingAudio.primed
            && heat != state.gattlingWeaponTimers.end() && heat->second > 0;
        if (!restoredHotRun && !state.gattlingAudio.sources.contains(id)
            && !state.gattlingAudio.newlyCreatedSources.contains(id)) continue;
        std::optional<WorldObjectInstance> terrain;
        if (behavior.attackGround)
            terrain = runtimeGroundAttackTargetObject(state, behavior.attackBridgeCell);
        else if (behavior.attackBridge)
            terrain = runtimeBridgeAttackTargetObject(state, behavior.attackBridgeCell, 0);
        const auto* target = terrain ? &*terrain : findObjectById(state, behavior.targetObjectId);
        const auto profile = runtimeWeaponProfileForObject(state, *object, target, behavior.forceFire);
        if (!profile.weapon) continue;
        const auto* sound = state.worldAudioCatalog.find(profile.weapon->report);
        if (!sound || !sound->loop || sound->samples.empty()) continue;
        sources.push_back({object, sound->eventId, sound->eventId});
    }
    // The existing source-voice sequencer queues body buffers ahead of their
    // device boundary and honors the original random/all/attack/decay data.
    return advanceRuntimeDiskLoopSources(state, state.gattlingAudio, sources,
        nowMilliseconds, runtimeGattlingSoundHandle);
}

bool manageRuntimeGattlingWeaponReport(RuntimeWindowState& state,
    const WorldObjectInstance& attacker, const WeaponType& weapon) {
    if (!runtimeCachedCapabilityProfileForObject(state, attacker).gattling) return false;
    const auto* sound = state.worldAudioCatalog.find(weapon.report);
    if (!sound || !sound->loop || sound->samples.empty()) return false;
    const auto heat = state.gattlingWeaponTimers.find(attacker.instanceId);
    const bool restoredHotRun = !state.gattlingAudio.primed
        && heat != state.gattlingWeaponTimers.end() && heat->second > 0;
    // A resumed pending burst can fire before the first presentation pass.
    // Saved heat must still resume the body instead of replaying the intro.
    if (!restoredHotRun && !state.gattlingAudio.sources.contains(attacker.instanceId))
        state.gattlingAudio.newlyCreatedSources.insert(attacker.instanceId);
    // Retail 0x70DE70 owns the report for the run/stage; 0x6FF365 skips
    // ordinary per-round reports. A successful first shot starts the voice.
    advanceRuntimeGattlingWeaponAudio(state, GetTickCount64());
    return true;
}
