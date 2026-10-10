bool advanceRuntimeTurretSoundAudio(RuntimeWindowState& state, std::uint64_t now) {
    auto& audio = state.turretSoundAudio;
    if (!state.rulesLoaded || !state.worldAudioCatalogLoaded) return false;
    if (!audio.prepared || audio.preparedRulesHash != state.rules.getDetailRulesHash()) {
        applyRuntimeWorldAudioBackendActions(state, audio.advance({}, {}, now, state.tick, [](const auto&, int) {return 1;}, false));
        audio = {}; audio.prepared = true; audio.preparedRulesHash = state.rules.getDetailRulesHash();
        const auto registerTypes = [&](const auto& types) {
            for (const auto& type : types) {
                const auto* sound = state.worldAudioCatalog.find(trimAsciiLocal(type.turretRotateSound));
                if (!type.turret || !sound || sound->samples.empty() || audio.specs.contains(sound->eventId)) continue;
                RuntimeObjectAmbientSpec spec; spec.sound = *sound;
                const auto tokens = [&](const char* key) {
                    return splitRuntimeWhitespaceTokens(toLowerAsciiLocal(state.soundIni.getValue(sound->eventId, key,
                        state.soundIni.getValue("Defaults", key, ""))));
                };
                const auto controls = tokens("Control"), types = tokens("Type");
                const auto has = [](const auto& words, const char* name) {return std::find(words.begin(), words.end(), name) != words.end();};
                spec.attack = has(controls, "attack"); spec.decay = has(controls, "decay");
                spec.all = has(controls, "all"); spec.literalLoop = has(controls, "loop");
                spec.global = has(types, "global"); spec.shroud = has(types, "shroud");
                audio.specs.emplace(sound->eventId, std::move(spec));
            }
        };
        registerTypes(state.rules.getInfantryTypes()); registerTypes(state.rules.getVehicleTypes());
        registerTypes(state.rules.getAircraftTypes()); registerTypes(state.rules.getBuildingTypes());
    }
    const auto source = [&](const WorldObjectInstance& object, const std::string& event, int x, int y, int level) {
        RuntimeTurretSoundSource result;
        result.objectId = object.instanceId;
        result.source = {"object:" + std::to_string(object.instanceId), object.type, event, {x, y, level}, 0};
        const auto found = audio.specs.find(event); if (found == audio.specs.end()) return result;
        const auto& spec = found->second;
        result.source.gainPermille = runtimeSpatialAudioGainPermille(state, spec.sound,
            {double(x), double(y), double(level)});
        return result;
    };
    std::vector<RuntimeTurretSoundSource> sources;
    std::vector<RuntimeTurretSoundMotion> motions;
    std::unordered_set<int> transported;
    for (const auto& object : state.world.getObjects()) for (int id : object.transportPassengerIds) transported.insert(id);
    for (auto& object : state.world.accessObjects()) {
        for (const auto& event : object.pendingTurretSounds)
            motions.push_back({source(object, event.event, event.x, event.y, event.level), event.rotating});
        object.pendingTurretSounds.clear();
        if (!object.activeTurretSoundEvent.empty() && runtimeTurretSoundObjectPresent(state, object)
            && !transported.contains(object.instanceId)
            && runtimeTurretSoundForObject(state, object) == object.activeTurretSoundEvent)
            sources.push_back(source(object, object.activeTurretSoundEvent, object.x, object.y, object.level));
    }
    const auto duration = [&](const std::string& sample, int frequency) {
        const std::string key = sample + ":" + std::to_string(frequency);
        auto found = audio.durations.find(key);
        if (found == audio.durations.end()) {
            const auto original = findRuntimeSoundSamplePath(state, sample);
            const auto path = original ? runtimeAudioPitchVariantPath(*original,
                std::filesystem::path(state.config.localDataRootPath) / "runtime_audio_cache", frequency) : "";
            found = audio.durations.emplace(key, path.empty() ? 1 : runtimeWavDurationTicks(path, 1000)).first;
        }
        return found->second;
    };
    const auto actions = audio.advance(sources, motions, now, state.tick, duration,
        state.runtimeSoundEffectsEnabled && state.runtimeSoundVolume > 0);
    applyRuntimeWorldAudioBackendActions(state, actions);
    return !actions.empty();
}
