// Historical include name retained; SFX voices now use independent XAudio2
// buffers. MCI waveaudio does not implement the setaudio volume command.
std::uint64_t runtimeSfxVoiceHandle(std::size_t channel) {
    return 0x6000000000000000ULL | channel;
}

struct RuntimeSfxVoiceBackend {
    RuntimeVoiceAudio& audio;
    std::optional<bool> isPlaying(std::size_t channel) const { return audio.isPlaying(runtimeSfxVoiceHandle(channel)); }
    bool stop(std::size_t channel) const { return audio.stop(runtimeSfxVoiceHandle(channel)); }
    void close(std::size_t channel) const { audio.close(runtimeSfxVoiceHandle(channel)); }
    bool open(std::size_t channel, const std::string& path) const { return audio.open(runtimeSfxVoiceHandle(channel), path); }
    bool setMix(std::size_t channel, int gain, int pan) const { return audio.setMix(runtimeSfxVoiceHandle(channel), gain, pan); }
    bool playFromStart(std::size_t channel) const { return audio.playFromStart(runtimeSfxVoiceHandle(channel)); }
    int durationTicks(const std::string& path, int hz) const {
        return runtimeEvaBusyTicksForSample(std::optional<std::string>{path}, hz);
    }
};

bool playRuntimeSfxOnDedicatedChannel(RuntimeWindowState& state,
    const std::string& path, const RuntimeSoundPlaybackRecord& record) {
    RuntimePerfTraceScope trace("playRuntimeSfxOnDedicatedChannel", state.tick);
    RuntimeSfxVoiceBackend backend{runtimeVoiceAudioFor(state)};
    const bool played = playRuntimeSfxPooled(state.runtimeSfxMciChannelCache,
        state.runtimeSfxMciChannelActive, state.runtimeSfxBusyUntilTick,
        state.runtimeNextSfxChannel, state.runtimeSfxChannelSteals,
        state.tick, state.config.fixedTickHz, path, record.volumePercent, backend,
        record.pan16384, record.gain16384);
    if (played && record.gain16384 > 0) {
        const auto channel = (state.runtimeNextSfxChannel + runtimeSfxChannelCount - 1) % runtimeSfxChannelCount;
        state.runtimeSfxSpatialRecords[channel] = record;
    }
    return played;
}

// Player communication already supplies its independent voice setting.
bool playRuntimeSfxOnDedicatedChannel(RuntimeWindowState& state, const std::string& path, int volume) {
    RuntimeSoundPlaybackRecord record; record.volumePercent = std::clamp(volume, 0, 100);
    record.gain16384 = record.volumePercent * 16384 / 100;
    record.authoredGainPermille = -1;
    return playRuntimeSfxOnDedicatedChannel(state, path, record);
}

void stopRuntimeSfxPlayback(RuntimeWindowState& state) {
    if (state.runtimeVoiceAudio) {
        RuntimeSfxVoiceBackend backend{*state.runtimeVoiceAudio};
        stopRuntimeSfxPooled(state.runtimeSfxMciChannelCache,
            state.runtimeSfxMciChannelActive, state.runtimeSfxBusyUntilTick, backend);
    } else {
        state.runtimeSfxMciChannelCache = {}; state.runtimeSfxMciChannelActive = {};
        state.runtimeSfxBusyUntilTick = {};
    }
    state.runtimeSfxSpatialRecords = {};
}

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
