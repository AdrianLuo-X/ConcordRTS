template<class ResolveDuration, class SpatialGain, class PublishAction, class PublishActions>
bool advanceRuntimeAircraftAuxPlayback(
    GameWorld& world,
    RuntimeAircraftAuxAudioState& audio,
    const RuntimeWorldAudioCatalog* catalog,
    std::uint64_t now,
    ResolveDuration&& resolveDuration,
    SpatialGain&& spatialGain,
    PublishAction&& publishAction,
    PublishActions&& publishActions
) {
    if (catalog == nullptr) {
        for (const auto& entry : audio.pending) if (entry.playing) {
            auto stop = entry.action; stop.kind = RuntimeWorldAudioActionKind::Stop;
            stop.reason = "aircraft auxiliary audio disabled";
            publishAction(stop);
        }
        audio = {};
        for (auto& object : world.accessObjects()) object.pendingAircraftAuxSounds.clear();
        return false;
    }
    const auto duration = [&](const std::string& sample, int frequency) {
        const auto key = sample + ":" + std::to_string(frequency);
        auto found = audio.durationMilliseconds.find(key);
        if (found == audio.durationMilliseconds.end()) {
            found = audio.durationMilliseconds.emplace(key, resolveDuration(sample, frequency)).first;
        }
        return found->second;
    };
    auto actions = audio.advance(now, duration);
    for (auto& object : world.accessObjects()) {
        for (const auto& event : object.pendingAircraftAuxSounds) {
            const auto* sound = catalog->find(event.event);
            if (!sound || sound->samples.empty()) continue;
            const int gain = spatialGain(*sound, event);
            audio.enqueue(*sound, event, object.instanceId, now, gain);
        }
        object.pendingAircraftAuxSounds.clear();
    }
    const auto immediate = audio.advance(now, duration);
    actions.insert(actions.end(), immediate.begin(), immediate.end());
    publishActions(actions);
    return !actions.empty();
}

bool advanceRuntimeAircraftAuxAudio(RuntimeWindowState& state, std::uint64_t now) {
    const bool enabled = state.worldAudioCatalogLoaded
        && state.runtimeSoundEffectsEnabled && state.runtimeSoundVolume > 0;
    return advanceRuntimeAircraftAuxPlayback(
        state.world, state.aircraftAuxAudio, enabled ? &state.worldAudioCatalog : nullptr, now,
        [&](const std::string& sample, int frequency) {
            const auto original = findRuntimeSoundSamplePath(state, sample);
            const auto path = original ? runtimeAudioPitchVariantPath(*original,
                std::filesystem::path(state.config.localDataRootPath) / "runtime_audio_cache", frequency) : "";
            return path.empty() ? 1 : runtimeWavDurationTicks(path, 1000);
        },
        [&](const auto& sound, const auto& event) {
            return runtimeSpatialAudioGainPermille(state, sound,
                {double(event.x), double(event.y), double(event.level)});
        },
        [&](const RuntimeWorldAudioAction& action) {
            applyRuntimeWorldAudioBackendAction(state, action);
        },
        [&](const std::vector<RuntimeWorldAudioAction>& actions) {
            applyRuntimeWorldAudioBackendActions(state, actions);
        }
    );
}
