template<class SpatialGain, class ResolveDuration, class PublishActions>
bool advanceRuntimeTurretSoundPlayback(
    RuntimeGameSessionState& session,
    RuntimeTurretSoundAudioState& audio,
    const RuntimeWorldAudioCatalog& catalog,
    const IniFile& soundIni,
    const bool& soundEffectsEnabled,
    const int& soundVolume,
    std::uint64_t now,
    SpatialGain&& spatialGain,
    ResolveDuration&& resolveDuration,
    PublishActions&& publishActions
) {
    if (!audio.prepared || audio.preparedRulesHash != session.rules.getDetailRulesHash()) {
        publishActions(audio.advance({}, {}, now, session.tick, [](const auto&, int) {return 1;}, false));
        audio = {}; audio.prepared = true; audio.preparedRulesHash = session.rules.getDetailRulesHash();
        const auto registerTypes = [&](const auto& types) {
            for (const auto& type : types) {
                const auto* sound = catalog.find(trimAsciiLocal(type.turretRotateSound));
                if (!type.turret || !sound || sound->samples.empty() || audio.specs.contains(sound->eventId)) continue;
                RuntimeObjectAmbientSpec spec; spec.sound = *sound;
                const auto tokens = [&](const char* key) {
                    return splitRuntimeWhitespaceTokens(toLowerAsciiLocal(soundIni.getValue(sound->eventId, key,
                        soundIni.getValue("Defaults", key, ""))));
                };
                const auto controls = tokens("Control"), types = tokens("Type");
                const auto has = [](const auto& words, const char* name) {return std::find(words.begin(), words.end(), name) != words.end();};
                spec.attack = has(controls, "attack"); spec.decay = has(controls, "decay");
                spec.all = has(controls, "all"); spec.literalLoop = has(controls, "loop");
                spec.global = has(types, "global"); spec.shroud = has(types, "shroud");
                audio.specs.emplace(sound->eventId, std::move(spec));
            }
        };
        registerTypes(session.rules.getInfantryTypes()); registerTypes(session.rules.getVehicleTypes());
        registerTypes(session.rules.getAircraftTypes()); registerTypes(session.rules.getBuildingTypes());
    }
    const auto source = [&](const WorldObjectInstance& object, const std::string& event, int x, int y, int level) {
        RuntimeTurretSoundSource result;
        result.objectId = object.instanceId;
        result.source = {"object:" + std::to_string(object.instanceId), object.type, event, {x, y, level}, 0};
        const auto found = audio.specs.find(event); if (found == audio.specs.end()) return result;
        const auto& spec = found->second;
        result.source.gainPermille = spatialGain(spec.sound, x, y, level);
        return result;
    };
    std::vector<RuntimeTurretSoundSource> sources;
    std::vector<RuntimeTurretSoundMotion> motions;
    std::unordered_set<int> transported;
    for (const auto& object : session.world.getObjects()) for (int id : object.transportPassengerIds) transported.insert(id);
    for (auto& object : session.world.accessObjects()) {
        for (const auto& event : object.pendingTurretSounds)
            motions.push_back({source(object, event.event, event.x, event.y, event.level), event.rotating});
        object.pendingTurretSounds.clear();
        if (!object.activeTurretSoundEvent.empty() && runtimeTurretSoundObjectPresent(session, object)
            && !transported.contains(object.instanceId)
            && runtimeTurretSoundForObject(session, object) == object.activeTurretSoundEvent)
            sources.push_back(source(object, object.activeTurretSoundEvent, object.x, object.y, object.level));
    }
    const auto duration = [&](const std::string& sample, int frequency) {
        const std::string key = sample + ":" + std::to_string(frequency);
        auto found = audio.durations.find(key);
        if (found == audio.durations.end()) {
            found = audio.durations.emplace(key, resolveDuration(sample, frequency)).first;
        }
        return found->second;
    };
    const auto actions = audio.advance(sources, motions, now, session.tick, duration,
        soundEffectsEnabled && soundVolume > 0);
    publishActions(actions);
    return !actions.empty();
}

bool advanceRuntimeTurretSoundAudio(RuntimeProductionHostState& state, std::uint64_t now) {
    if (!state.rulesLoaded || !state.worldAudioCatalogLoaded) return false;
    return advanceRuntimeTurretSoundPlayback(
        state, state.turretSoundAudio, state.worldAudioCatalog, state.soundIni,
        state.runtimeSoundEffectsEnabled, state.runtimeSoundVolume, now,
        [&](const RuntimeWorldAudioSoundSpec& event, int x, int y, int level) {
            return runtimeSpatialAudioGainPermille(state, event,
                {double(x), double(y), double(level)});
        },
        [&](const std::string& sample, int frequency) {
            const auto original = findRuntimeSoundSamplePath(state, sample);
            const auto path = original ? runtimeAudioPitchVariantPath(*original,
                std::filesystem::path(state.config.localDataRootPath) / "runtime_audio_cache", frequency) : "";
            return path.empty() ? 1 : runtimeWavDurationTicks(path, 1000);
        },
        [&](const std::vector<RuntimeWorldAudioAction>& actions) {
            applyRuntimeWorldAudioBackendActions(state, actions);
        }
    );
}
