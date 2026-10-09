bool advanceRuntimeAircraftAuxAudio(RuntimeWindowState& state, std::uint64_t now) {
    auto& audio = state.aircraftAuxAudio;
    if (!state.worldAudioCatalogLoaded || !state.runtimeSoundEffectsEnabled || state.runtimeSoundVolume <= 0) {
        for (const auto& entry : audio.pending) if (entry.playing) {
            auto stop = entry.action; stop.kind = RuntimeWorldAudioActionKind::Stop;
            stop.reason = "aircraft auxiliary audio disabled";
            applyRuntimeWorldAudioBackendAction(state, stop);
        }
        audio = {};
        for (auto& object : state.world.accessObjects()) object.pendingAircraftAuxSounds.clear();
        return false;
    }
    const auto duration = [&](const std::string& sample, int frequency) {
        const auto key = sample + ":" + std::to_string(frequency);
        auto found = audio.durationMilliseconds.find(key);
        if (found == audio.durationMilliseconds.end()) {
            const auto original = findRuntimeSoundSamplePath(state, sample);
            const auto path = original ? runtimeAudioPitchVariantPath(*original,
                std::filesystem::path(state.config.localDataRootPath) / "runtime_audio_cache", frequency) : "";
            found = audio.durationMilliseconds.emplace(key, path.empty() ? 1 : runtimeWavDurationTicks(path, 1000)).first;
        }
        return found->second;
    };
    auto actions = audio.advance(now, duration);
    for (auto& object : state.world.accessObjects()) {
        for (const auto& event : object.pendingAircraftAuxSounds) {
            const auto* sound = state.worldAudioCatalog.find(event.event);
            if (!sound || sound->samples.empty()) continue;
            const int gain = runtimeSpatialAudioGainPermille(state, *sound,
                {double(event.x), double(event.y), double(event.level)});
            audio.enqueue(*sound, event, object.instanceId, now, gain);
        }
        object.pendingAircraftAuxSounds.clear();
    }
    const auto immediate = audio.advance(now, duration);
    actions.insert(actions.end(), immediate.begin(), immediate.end());
    applyRuntimeWorldAudioBackendActions(state, actions);
    return !actions.empty();
}
