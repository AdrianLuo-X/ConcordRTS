bool verifyRuntimeAudioVariation() {
    IniFile ini;
    if (!ini.loadFromString("[Defaults]\nVolume=80\nFShift=-5 5\nVShift=10\n"
        "[SoundList]\n0=Varied\n1=Fixed\n[Varied]\nSounds=tone\nControl=loop\n"
        "[Fixed]\nSounds=tone\nFShift=0 0\nVShift=0\n")) return false;
    RuntimeWorldAudioCatalog catalog;
    if (!catalog.loadFromIni(ini)) return false;
    const auto* varied = catalog.find("Varied");
    const auto* fixed = catalog.find("Fixed");
    if (!varied || !fixed || varied->variation.minimumFrequencyPercent != -5
        || varied->variation.maximumFrequencyPercent != 5
        || varied->variation.maximumVolumeDropPercent != 10
        || fixed->variation.minimumFrequencyPercent != 0) return false;
    std::set<int> pitches, volumes;
    for (int ordinal = 0; ordinal < 100; ++ordinal) {
        const auto seed = runtimeAudioVariationSeed("Varied", 123, ordinal);
        const auto value = runtimeAudioVariationFor(varied->variation, seed);
        if (value.frequencyPermille < 950 || value.frequencyPermille > 1050
            || value.volumeDeltaPermille > 0 || value.volumeDeltaPermille < -100
            || runtimeAudioVariationFor(varied->variation, seed).frequencyPermille
                != value.frequencyPermille) return false;
        pitches.insert(value.frequencyPermille);
        volumes.insert(value.volumeDeltaPermille);
    }
    if (pitches.size() < 5 || volumes.size() < 10) return false;
    RuntimeWorldAudioContext context;
    context.catalog = &catalog;
    RuntimeWorldAudioSystemState system;
    auto request = playRequest("Varied", 3, 0, 0, "variation-loop");
    if (!system.playAt(context, request, 0).accepted) return false;
    const auto first = system.tick(context, 0);
    if (first.size() != 1 || first.front().gainPermille < 700 || first.front().gainPermille > 800
        || first.front().frequencyPermille < 950 || first.front().frequencyPermille > 1050
        || !system.tick(context, 1).empty()) return false;
    std::ostringstream save;
    system.save(save);
    IniFile saved;
    RuntimeWorldAudioSystemState restored;
    if (!saved.loadFromString(save.str()) || !restored.load(saved, context)
        || restored.stateHash() != system.stateHash()) return false;
    const auto rehydrated = restored.rehydrateActions(context, 2);
    if (rehydrated.size() != 1 || rehydrated.front().gainPermille != first.front().gainPermille
        || rehydrated.front().frequencyPermille != first.front().frequencyPermille) return false;

    // This is real PCM playback metadata, not an unused pitch field.
    std::vector<std::uint8_t> wave(52, 0);
    const auto putTag = [&](int offset, const char* tag) {
        std::copy(tag, tag + 4, wave.begin() + offset);
    };
    const auto put32 = [&](int offset, std::uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) wave[offset + byte] = static_cast<std::uint8_t>(value >> (8 * byte));
    };
    putTag(0, "RIFF"); put32(4, 44); putTag(8, "WAVE");
    putTag(12, "fmt "); put32(16, 16);
    wave[20] = 1; wave[22] = 1; put32(24, 22050); put32(28, 44100);
    wave[32] = 2; wave[34] = 16; putTag(36, "data"); put32(40, 8);
    for (int index = 44; index < 52; ++index) wave[index] = static_cast<std::uint8_t>(index * 3);
    const auto original = wave;
    if (!runtimeAudioPitchWave(wave, 970) || runtimeAudioReadLe32(wave, 24) != 21389
        || runtimeAudioReadLe32(wave, 28) != 42778
        || !std::equal(wave.begin() + 44, wave.end(), original.begin() + 44)) return false;
    std::vector<std::uint8_t> corrupt = original;
    corrupt[16] = 255;
    const auto before = corrupt;
    if (runtimeAudioPitchWave(corrupt, 1100) || corrupt != before) return false;
    std::cout << "audio variation parsing/pitch PCM/loop save-resume passed\n";
    return true;
}
