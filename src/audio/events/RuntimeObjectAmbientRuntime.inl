template<class SpatialGain, class ResolveDuration, class PublishActions>
bool advanceRuntimeObjectAmbientPlayback(
    const RuntimeGameSessionState& session,
    RuntimeObjectAmbientAudioState& audio,
    const RuntimeWorldAudioCatalog& catalog,
    const IniFile& soundIni,
    std::uint64_t nowMilliseconds,
    SpatialGain&& spatialGain,
    ResolveDuration&& resolveDuration,
    PublishActions&& publishActions
) {
    if (!audio.prepared || audio.preparedRulesHash != session.rules.getDetailRulesHash()) {
        // Retain no controllers across a rules/catalog reload.
        const auto stops = audio.advance({}, nowMilliseconds, session.tick, [](const auto&, int) {return 1;});
        publishActions(stops);
        audio = {};
        audio.preparedRulesHash = session.rules.getDetailRulesHash(); audio.prepared = true;
        const auto registerTypes = [&](const auto& types, const std::string& prefix) {
            for (const auto& type : types) {
                const auto* sound = catalog.find(trimAsciiLocal(type.ambientSound));
                if (!sound || sound->samples.empty()) continue;
                audio.typeEvents[prefix + toLowerAsciiLocal(type.id)] = sound->eventId;
                if (audio.specs.contains(sound->eventId)) continue;
                RuntimeObjectAmbientSpec spec; spec.sound = *sound;
                const auto tokens = [&](const char* key) {
                    return splitRuntimeWhitespaceTokens(toLowerAsciiLocal(soundIni.getValue(sound->eventId, key,
                        soundIni.getValue("Defaults", key, ""))));
                };
                const auto controls = tokens("Control"), soundTypes = tokens("Type");
                const auto has = [](const auto& values, const char* word) {return std::find(values.begin(), values.end(), word) != values.end();};
                spec.all = has(controls, "all"); spec.attack = has(controls, "attack");
                spec.decay = has(controls, "decay"); spec.literalLoop = has(controls, "loop");
                spec.shroud = has(soundTypes, "shroud"); spec.global = has(soundTypes, "global");
                audio.specs.emplace(sound->eventId, std::move(spec));
            }
        };
        registerTypes(session.rules.getInfantryTypes(), "i:");
        registerTypes(session.rules.getVehicleTypes(), "u:");
        registerTypes(session.rules.getAircraftTypes(), "a:");
        registerTypes(session.rules.getBuildingTypes(), "b:");
        registerTypes(session.rules.getTerrainTypes(), "t:");
    }
    std::vector<RuntimeObjectAmbientSource> sources;
    if (!audio.typeEvents.empty()) {
        std::unordered_set<int> transported;
        for (const auto& object : session.world.getObjects())
            for (int id : object.transportPassengerIds) transported.insert(id);
        for (const auto& object : session.world.getObjects()) {
            if (!object.alive || object.health <= 0 || object.rubble || object.instanceId < 0
                || object.x < 0 || object.y < 0 || runtimeGateObjectIsLimbo(object)
                || object.garrisonHostObjectId >= 0 || transported.contains(object.instanceId)) continue;
            if (session.specialMechanismsInitialized && (session.specialMechanisms.isChronoInTransit(object.instanceId)
                || session.specialMechanisms.isCarrierChildDocked(object.instanceId))) continue;
            const std::string prefix = object.kind == WorldObjectKind::Structure ? "b:"
                : object.kind == WorldObjectKind::Infantry ? "i:" : object.authoredAircraft ? "a:" : "u:";
            auto found = audio.typeEvents.find(prefix + toLowerAsciiLocal(object.type));
            if (found == audio.typeEvents.end() && object.kind == WorldObjectKind::Unit && !object.authoredAircraft
                && !session.rules.findUnitType("UnitType", object.type)) found = audio.typeEvents.find("a:" + toLowerAsciiLocal(object.type));
            if (found == audio.typeEvents.end()) continue;
            const auto& spec = audio.specs.at(found->second);
            sources.push_back({"object:" + std::to_string(object.instanceId), object.type, found->second,
                {object.x, object.y, object.level}, spatialGain(spec.sound, runtimeSpatialAudioPositionForObject(object)), object.instanceId});
        }
        for (const auto& terrain : session.world.getTerrainObjects()) {
            if (!terrain.alive || terrain.health <= 0 || terrain.x < 0 || terrain.y < 0) continue;
            const auto found = audio.typeEvents.find("t:" + toLowerAsciiLocal(terrain.type));
            if (found == audio.typeEvents.end()) continue;
            const auto& spec = audio.specs.at(found->second);
            sources.push_back({"terrain:" + terrain.mapId + ":" + std::to_string(terrain.cell), terrain.type, found->second,
                {terrain.x, terrain.y, terrain.level}, spatialGain(spec.sound, {double(terrain.x), double(terrain.y), double(terrain.level)})});
        }
    }
    const auto duration = [&](const std::string& sample, int frequency) {
        const std::string key = sample + ":" + std::to_string(frequency);
        auto found = audio.sampleDurationMilliseconds.find(key);
        if (found == audio.sampleDurationMilliseconds.end()) {
            found = audio.sampleDurationMilliseconds.emplace(key, resolveDuration(sample, frequency)).first;
        }
        return found->second;
    };
    const auto actions = audio.advance(std::move(sources), nowMilliseconds, session.tick, duration);
    publishActions(actions);
    return !actions.empty();
}

bool advanceRuntimeObjectAmbientAudio(RuntimeProductionHostState& state, std::uint64_t nowMilliseconds) {
    if (!state.rulesLoaded || !state.worldAudioCatalogLoaded) return false;
    return advanceRuntimeObjectAmbientPlayback(
        state, state.objectAmbientAudio, state.worldAudioCatalog, state.soundIni, nowMilliseconds,
        [&](const RuntimeWorldAudioSoundSpec& sound, const RuntimeSpatialAudioPosition& position) {
            return runtimeSpatialAudioGainPermille(state, sound, position);
        },
        [&](const std::string& sample, int frequency) {
            const auto source = findRuntimeSoundSamplePath(state, sample);
            // The exact backend variant can have a rounded sample rate or
            // retain the original duration when the format cannot be pitched.
            const auto path = source ? runtimeAudioPitchVariantPath(*source,
                std::filesystem::path(state.config.localDataRootPath) / "runtime_audio_cache", frequency) : "";
            return path.empty() ? 1000 : runtimeWavDurationTicks(path, 1000);
        },
        [&](const std::vector<RuntimeWorldAudioAction>& actions) {
            applyRuntimeWorldAudioBackendActions(state, actions);
        }
    );
}
