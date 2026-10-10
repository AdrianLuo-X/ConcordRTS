RuntimeSpatialAudioPosition runtimeWorldAudioSourcePosition(const RuntimeWindowState& state,
    const RuntimeWorldAudioAction& action) {
    if (action.sourceObjectId >= 0) if (const auto* object = findObjectById(state, action.sourceObjectId))
        return runtimeSpatialAudioPositionForObject(*object);
    return {double(action.cell.x), double(action.cell.y), double(action.cell.level)};
}

void rememberRuntimeWorldAudioBackendAction(RuntimeWindowState& state, const RuntimeWorldAudioAction& action) {
    state.runtimeWorldAudioBackendActionHistory.push_back(action);
    while (state.runtimeWorldAudioBackendActionHistory.size() > 32U)
        state.runtimeWorldAudioBackendActionHistory.pop_front();
}

void applyRuntimeWorldAudioBackendAction(RuntimeWindowState& state, const RuntimeWorldAudioAction& input) {
    RuntimeWorldAudioAction action = input;
    if (action.kind == RuntimeWorldAudioActionKind::Stop) {
        rememberRuntimeWorldAudioBackendAction(state, action);
        if (state.runtimeVoiceAudio) state.runtimeVoiceAudio->close(action.handleId);
        state.runtimeWorldAudioBackendBindings.erase(action.handleId);
        state.runtimeWorldAudioLimitEvictedHandles.erase(action.handleId); return;
    }
    auto found = state.runtimeWorldAudioBackendBindings.find(action.handleId);
    if (action.kind == RuntimeWorldAudioActionKind::SetGain && found != state.runtimeWorldAudioBackendBindings.end()) {
        const auto& previous = found->second.action;
        if (action.eventId.empty()) {action.eventId = previous.eventId; action.cell = previous.cell;}
        if (action.sourceObjectId < 0) action.sourceObjectId = previous.sourceObjectId;
        if (action.authoredGainPermille < 0) action.authoredGainPermille = previous.authoredGainPermille;
    }
    const auto position = runtimeWorldAudioSourcePosition(state, action);
    const auto spatial = runtimeSpatialAudioAt(state, action.eventId, position);
    action.pan16384 = spatial.pan16384; action.spatialGain16384 = spatial.gain16384;
    const int authored = action.authoredGainPermille >= 0 ? action.authoredGainPermille : action.gainPermille;
    action.authoredGainPermille = authored;
    action.gainPermille = authored * spatial.gain16384 / 16384;
    rememberRuntimeWorldAudioBackendAction(state, action);
    if (!state.runtimeSoundsEnabled) return;
    const int gain = !state.runtimeSoundEffectsEnabled ? 0
        : runtimeSpatialAudioCombinedGain(state, authored, spatial.gain16384);
    if (action.kind == RuntimeWorldAudioActionKind::SetGain) {
        if (found == state.runtimeWorldAudioBackendBindings.end()) return;
        auto& binding = found->second;
        if (state.runtimeVoiceAudio && state.runtimeVoiceAudio->setMix(action.handleId, gain, action.pan16384)) {
            binding.action.authoredGainPermille = authored;
            binding.action.cell = action.cell; binding.action.sourceObjectId = action.sourceObjectId;
            binding.action.spatialGain16384 = action.spatialGain16384; binding.action.pan16384 = action.pan16384;
            binding.position = position; binding.gainPermille = action.gainPermille;
            binding.gain16384 = gain; binding.pan16384 = action.pan16384;
        }
        return;
    }
    if (action.queueAfterCurrent && action.sourceObjectId >= 0
        && state.runtimeWorldAudioLimitEvictedHandles.contains(action.handleId)) return;
    std::optional<std::string> path = findRuntimeSoundSamplePath(state, action.sampleName);
    if (path) path = runtimeAudioPitchVariantPath(*path,
        std::filesystem::path(state.config.localDataRootPath) / "runtime_audio_cache", action.frequencyPermille);
    auto& audio = runtimeVoiceAudioFor(state);
    if (action.queueAfterCurrent && path && found != state.runtimeWorldAudioBackendBindings.end()
        && audio.queue(action.handleId, *path)) return;
    audio.close(action.handleId); state.runtimeWorldAudioBackendBindings.erase(action.handleId);
    if (!path || (spatial.gain16384 == 0 && action.backendLoopIterations != -1)) return;
    if (spatial.gain16384 > 0)
        enforceRuntimeSoundVoiceLimit(state, action.eventId, action.handleId);
    if (!audio.open(action.handleId, *path) || !audio.setMix(action.handleId, gain, action.pan16384)) {
        audio.close(action.handleId); return;
    }
    const bool active = spatial.gain16384 > 0;
    if (active && !audio.playFromStart(action.handleId, action.backendLoopIterations == -1, action.queueAfterCurrent)) {
        audio.close(action.handleId); return;
    }
    RuntimeWorldAudioBackendBinding binding;
    binding.handleId = action.handleId; binding.playbackOrder = ++state.runtimeSoundVoiceOrder;
    binding.active = active; binding.action = action;
    binding.position = position; binding.gainPermille = action.gainPermille;
    binding.gain16384 = gain; binding.pan16384 = action.pan16384;
    state.runtimeWorldAudioBackendBindings.emplace(action.handleId, std::move(binding));
}

void applyRuntimeWorldAudioBackendActions(RuntimeWindowState& state, const std::vector<RuntimeWorldAudioAction>& actions) {
    for (const auto& action : actions) applyRuntimeWorldAudioBackendAction(state, action);
}

void stopRuntimeWorldAudioBackend(RuntimeWindowState& state, bool preserveChronosphereSelection) {
    if (state.runtimeVoiceAudio) for (const auto& [id, binding] : state.runtimeWorldAudioBackendBindings)
        state.runtimeVoiceAudio->close(id);
    state.runtimeWorldAudioBackendBindings.clear();
    state.runtimeWorldAudioLimitEvictedHandles.clear();
    state.superWeaponBuildingAudio = {}; state.diskDrainAudio = {}; state.diskMoveAudio = {};
    state.gattlingAudio = {};
    if (preserveChronosphereSelection && state.chronosphereSelectionAudio.selected) {
        auto& sound = state.chronosphereSelectionAudio.sound;
        sound.active = false; sound.gainPermille = 0;
        sound.nextSampleMilliseconds = sound.queuedUntilMilliseconds = 0;
    } else {
        state.chronosphereSelectionAudio = {};
    }
    state.objectAmbientAudio = {}; state.aircraftAuxAudio = {}; state.turretSoundAudio = {};
    for (auto& object : state.world.accessObjects()) {
        object.pendingTurretSounds.clear(); object.pendingAircraftAuxSounds.clear();
    }
}

void reapRuntimeWorldAudioBackendBindings(RuntimeWindowState& state) {
    if (!state.runtimeVoiceAudio) return;
    auto& audio = *state.runtimeVoiceAudio;
    for (auto it = state.runtimeWorldAudioBackendBindings.begin(); it != state.runtimeWorldAudioBackendBindings.end();) {
        auto& binding = it->second;
        if (binding.active && !audio.isPlaying(it->first).value_or(false)) {
            audio.close(it->first); it = state.runtimeWorldAudioBackendBindings.erase(it); continue;
        }
        ++it;
    }
}

void advanceRuntimeSpatialAudioMixes(RuntimeWindowState& state) {
    advanceRuntimeSfxSpatialMixes(state);
    if (!state.runtimeVoiceAudio) return;
    auto& audio = *state.runtimeVoiceAudio;
    for (auto it = state.runtimeWorldAudioBackendBindings.begin(); it != state.runtimeWorldAudioBackendBindings.end();) {
        auto& binding = it->second; auto& action = binding.action;
        if (action.trackCamera) {
            if (action.sourceObjectId >= 0) binding.position = runtimeWorldAudioSourcePosition(state, action);
            const auto spatial = runtimeSpatialAudioAt(state, action.eventId, binding.position);
            action.spatialGain16384 = spatial.gain16384; action.pan16384 = spatial.pan16384;
        }
        if (action.spatialGain16384 == 0) {
            if (binding.active) audio.stop(it->first);
            binding.active = false;
            if (action.backendLoopIterations != -1) {
                audio.close(it->first); it = state.runtimeWorldAudioBackendBindings.erase(it); continue;
            }
        } else if (!binding.active && action.backendLoopIterations == -1) {
            enforceRuntimeSoundVoiceLimit(state, action.eventId, it->first);
            binding.active = audio.playFromStart(it->first, true);
            if (binding.active) {
                binding.action.eventTick = state.tick;
                binding.playbackOrder = ++state.runtimeSoundVoiceOrder;
            }
        }
        const int gain = !state.runtimeSoundEffectsEnabled ? 0
            : runtimeSpatialAudioCombinedGain(state, action.authoredGainPermille, action.spatialGain16384);
        if (binding.gain16384 != gain || binding.pan16384 != action.pan16384) {
            if (audio.setMix(it->first, gain, action.pan16384)) {
                binding.gain16384 = gain; binding.pan16384 = action.pan16384;
                auto updated = action; updated.kind = RuntimeWorldAudioActionKind::SetGain;
                updated.gainPermille = action.authoredGainPermille * action.spatialGain16384 / 16384;
                updated.reason = "local listener mix changed";
                rememberRuntimeWorldAudioBackendAction(state, updated);
            }
        }
        ++it;
    }
    reapRuntimeWorldAudioBackendBindings(state);
}
