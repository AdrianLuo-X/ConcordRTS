// Historical include name retained; SFX voices now use independent XAudio2
// buffers. MCI waveaudio does not implement the setaudio volume command.










void advanceRuntimeSfxSpatialMixes(RuntimeWindowState& state) {
    if (!state.runtimeVoiceAudio) return;
    for (std::size_t i = 0; i < runtimeSfxChannelCount; ++i) {
        if (!state.runtimeSfxMciChannelActive[i]) continue;
        auto& record = state.runtimeSfxSpatialRecords[i];
        auto& cached = state.runtimeSfxMciChannelCache[i];
        if (!state.runtimeVoiceAudio->isPlaying(runtimeSfxVoiceHandle(i)).value_or(false)) {
            state.runtimeSfxMciChannelActive[i] = false; continue;
        }
        if (record.spatial && record.trackCamera) {
            const auto spatial = runtimeSpatialAudioAt(state, record.section, record.sourcePosition);
            record.spatialGain16384 = spatial.gain16384; record.pan16384 = spatial.pan16384;
            // Retail's managed fixed-position one-shot stops at the hearing
            // boundary; returning the camera does not resurrect that shot.
            if (spatial.gain16384 == 0) {
                state.runtimeVoiceAudio->stop(runtimeSfxVoiceHandle(i));
                state.runtimeSfxMciChannelActive[i] = false;
                state.runtimeSfxBusyUntilTick[i] = 0;
                continue;
            }
        }
        const int gain = !state.runtimeSoundEffectsEnabled ? 0 : record.authoredGainPermille < 0
            ? record.gain16384 : runtimeSpatialAudioCombinedGain(state, record.authoredGainPermille, record.spatialGain16384);
        if ((cached.gain16384 != gain || cached.pan16384 != record.pan16384)
            && state.runtimeVoiceAudio->setMix(runtimeSfxVoiceHandle(i), gain, record.pan16384)) {
            cached.gain16384 = gain; cached.pan16384 = record.pan16384;
        }
    }
}
