// Historical include name retained; SFX voices now use independent XAudio2
// buffers. MCI waveaudio does not implement the setaudio volume command.










template<class ReadSpatial>
void advanceRuntimeSfxSpatialVoices(RuntimeHostAudioState& audio, ReadSpatial&& readSpatial) {
    if (!audio.runtimeVoiceAudio) return;
    for (std::size_t i = 0; i < runtimeSfxChannelCount; ++i) {
        if (!audio.runtimeSfxMciChannelActive[i]) continue;
        auto& record = audio.runtimeSfxSpatialRecords[i];
        auto& cached = audio.runtimeSfxMciChannelCache[i];
        if (!audio.runtimeVoiceAudio->isPlaying(runtimeSfxVoiceHandle(i)).value_or(false)) {
            audio.runtimeSfxMciChannelActive[i] = false; continue;
        }
        if (record.spatial && record.trackCamera) {
            const auto spatial = readSpatial(record);
            record.spatialGain16384 = spatial.gain16384; record.pan16384 = spatial.pan16384;
            // Retail's managed fixed-position one-shot stops at the hearing
            // boundary; returning the camera does not resurrect that shot.
            if (spatial.gain16384 == 0) {
                audio.runtimeVoiceAudio->stop(runtimeSfxVoiceHandle(i));
                audio.runtimeSfxMciChannelActive[i] = false;
                audio.runtimeSfxBusyUntilTick[i] = 0;
                continue;
            }
        }
        const int gain = !audio.runtimeSoundEffectsEnabled ? 0 : record.authoredGainPermille < 0
            ? record.gain16384 : runtimeSpatialAudioCombinedGain(audio, record.authoredGainPermille, record.spatialGain16384);
        if ((cached.gain16384 != gain || cached.pan16384 != record.pan16384)
            && audio.runtimeVoiceAudio->setMix(runtimeSfxVoiceHandle(i), gain, record.pan16384)) {
            cached.gain16384 = gain; cached.pan16384 = record.pan16384;
        }
    }
}


void advanceRuntimeSfxSpatialMixes(RuntimeProductionHostState& state) {
    advanceRuntimeSfxSpatialVoices(state, [&](const RuntimeSoundPlaybackRecord& record) {
        return runtimeSpatialAudioAt(state, record.section, record.sourcePosition);
    });
}
