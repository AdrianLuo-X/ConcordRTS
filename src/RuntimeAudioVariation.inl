void applyRuntimeAudioVariation(RuntimeWindowState& state, RuntimeSoundPlaybackRecord& record) {
    if (record.eva || !state.soundLoaded) return;
    const auto* catalogSound = state.worldAudioCatalogLoaded
        ? state.worldAudioCatalog.find(record.section) : nullptr;
    const auto spec = catalogSound ? catalogSound->variation
        : runtimeAudioVariationRules(state.soundIni, record.section);
    const auto variation = runtimeAudioVariationFor(spec,
        runtimeAudioVariationSeed(record.section,
            static_cast<std::uint64_t>(state.runtimeSoundEvents), state.tick));
    const int authoredVolume = catalogSound ? catalogSound->volumePercent
        : std::clamp(parseRuntimeIntOrDefault(stripRuntimeSoundComment(
            state.soundIni.getValue(record.section, "Volume",
                state.soundIni.getValue("Defaults", "Volume", "80"))), 80), 0, 100);
    const int varied = std::clamp(authoredVolume * 10 + variation.volumeDeltaPermille, 0, 1000);
    record.authoredGainPermille = varied;
    record.gain16384 = runtimeSpatialAudioCombinedGain(state, varied, 16384);
    record.volumePercent = std::clamp((varied * state.runtimeSoundVolume + 500) / 1000, 0, 100);
    if (!record.path.empty() && state.runtimeSoundsEnabled) {
        record.path = runtimeAudioPitchVariantPath(record.path,
            std::filesystem::path(state.config.localDataRootPath) / "runtime_audio_cache",
            variation.frequencyPermille);
    }
}
