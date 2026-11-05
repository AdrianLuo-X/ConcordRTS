











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
