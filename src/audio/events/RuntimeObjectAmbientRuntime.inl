bool advanceRuntimeObjectAmbientAudio(RuntimeWindowState& state, std::uint64_t nowMilliseconds) {
    auto& audio = state.objectAmbientAudio;
    if (!state.rulesLoaded || !state.worldAudioCatalogLoaded) return false;
    if (!audio.prepared || audio.preparedRulesHash != state.rules.getDetailRulesHash()) {
        // Retain no controllers across a rules/catalog reload.
        const auto stops = audio.advance({}, nowMilliseconds, state.tick, [](const auto&, int) {return 1;});
        applyRuntimeWorldAudioBackendActions(state, stops);
        audio = {};
        audio.preparedRulesHash = state.rules.getDetailRulesHash(); audio.prepared = true;
        const auto registerTypes = [&](const auto& types, const std::string& prefix) {
            for (const auto& type : types) {
                const auto* sound = state.worldAudioCatalog.find(trimAsciiLocal(type.ambientSound));
                if (!sound || sound->samples.empty()) continue;
                audio.typeEvents[prefix + toLowerAsciiLocal(type.id)] = sound->eventId;
                if (audio.specs.contains(sound->eventId)) continue;
                RuntimeObjectAmbientSpec spec; spec.sound = *sound;
                const auto tokens = [&](const char* key) {
                    return splitRuntimeWhitespaceTokens(toLowerAsciiLocal(state.soundIni.getValue(sound->eventId, key,
                        state.soundIni.getValue("Defaults", key, ""))));
                };
                const auto controls = tokens("Control"), soundTypes = tokens("Type");
                const auto has = [](const auto& values, const char* word) {return std::find(values.begin(), values.end(), word) != values.end();};
                spec.all = has(controls, "all"); spec.attack = has(controls, "attack");
                spec.decay = has(controls, "decay"); spec.literalLoop = has(controls, "loop");
                spec.shroud = has(soundTypes, "shroud"); spec.global = has(soundTypes, "global");
                audio.specs.emplace(sound->eventId, std::move(spec));
            }
        };
        registerTypes(state.rules.getInfantryTypes(), "i:");
        registerTypes(state.rules.getVehicleTypes(), "u:");
        registerTypes(state.rules.getAircraftTypes(), "a:");
        registerTypes(state.rules.getBuildingTypes(), "b:");
        registerTypes(state.rules.getTerrainTypes(), "t:");
    }
    const auto gainAt = [&](const RuntimeObjectAmbientSpec& spec, const RuntimeSpatialAudioPosition& position) {
        return runtimeSpatialAudioGainPermille(state, spec.sound, position);
    };
    std::vector<RuntimeObjectAmbientSource> sources;
    if (!audio.typeEvents.empty()) {
        std::unordered_set<int> transported;
        for (const auto& object : state.world.getObjects())
            for (int id : object.transportPassengerIds) transported.insert(id);
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive || object.health <= 0 || object.rubble || object.instanceId < 0
                || object.x < 0 || object.y < 0 || runtimeGateObjectIsLimbo(object)
                || object.garrisonHostObjectId >= 0 || transported.contains(object.instanceId)) continue;
            if (state.specialMechanismsInitialized && (state.specialMechanisms.isChronoInTransit(object.instanceId)
                || state.specialMechanisms.isCarrierChildDocked(object.instanceId))) continue;
            const std::string prefix = object.kind == WorldObjectKind::Structure ? "b:"
                : object.kind == WorldObjectKind::Infantry ? "i:" : object.authoredAircraft ? "a:" : "u:";
            auto found = audio.typeEvents.find(prefix + toLowerAsciiLocal(object.type));
            if (found == audio.typeEvents.end() && object.kind == WorldObjectKind::Unit && !object.authoredAircraft
                && !state.rules.findUnitType("UnitType", object.type)) found = audio.typeEvents.find("a:" + toLowerAsciiLocal(object.type));
            if (found == audio.typeEvents.end()) continue;
            const auto& spec = audio.specs.at(found->second);
            sources.push_back({"object:" + std::to_string(object.instanceId), object.type, found->second,
                {object.x, object.y, object.level}, gainAt(spec, runtimeSpatialAudioPositionForObject(object)), object.instanceId});
        }
        for (const auto& terrain : state.world.getTerrainObjects()) {
            if (!terrain.alive || terrain.health <= 0 || terrain.x < 0 || terrain.y < 0) continue;
            const auto found = audio.typeEvents.find("t:" + toLowerAsciiLocal(terrain.type));
            if (found == audio.typeEvents.end()) continue;
            const auto& spec = audio.specs.at(found->second);
            sources.push_back({"terrain:" + terrain.mapId + ":" + std::to_string(terrain.cell), terrain.type, found->second,
                {terrain.x, terrain.y, terrain.level}, gainAt(spec, {double(terrain.x), double(terrain.y), double(terrain.level)})});
        }
    }
    const auto duration = [&](const std::string& sample, int frequency) {
        const std::string key = sample + ":" + std::to_string(frequency);
        auto found = audio.sampleDurationMilliseconds.find(key);
        if (found == audio.sampleDurationMilliseconds.end()) {
            const auto source = findRuntimeSoundSamplePath(state, sample);
            // Read the exact backend variant: its integer sample rate can differ
            // from a rounded duration divided by the requested pitch. Formats
            // that cannot be pitched retain their actual source duration.
            const auto path = source ? runtimeAudioPitchVariantPath(*source,
                std::filesystem::path(state.config.localDataRootPath) / "runtime_audio_cache", frequency) : "";
            found = audio.sampleDurationMilliseconds.emplace(key, path.empty() ? 1000 : runtimeWavDurationTicks(path, 1000)).first;
        }
        return found->second;
    };
    const auto actions = audio.advance(std::move(sources), nowMilliseconds, state.tick, duration);
    applyRuntimeWorldAudioBackendActions(state, actions);
    return !actions.empty();
}
