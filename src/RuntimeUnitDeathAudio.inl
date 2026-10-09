// Ordinary hits and super-weapon removals share the same authored death cues.
// Wrecks own their sinking/crashing audio and must not play these cues twice.
void emitRuntimeUnitDeathSound(
    RuntimeWindowState& state,
    const WorldObjectInstance& object,
    std::optional<RuntimeSpatialAudioPosition> source = std::nullopt,
    bool includeCrashingSound = false
) {
    if (object.kind == WorldObjectKind::Structure
        || std::any_of(state.wrecks.begin(), state.wrecks.end(),
            [&](const auto& wreck) {
                return wreck.object.instanceId == object.instanceId;
            })) {
        return;
    }
    const UnitType* unit = findRuntimeUnitTypeById(state.rules, object.type);
    const auto position = source.value_or(runtimeSpatialAudioPositionForObject(object));
    // Some damage modules mark victims inactive before notifying their exit.
    // Preserve the native surface-ship replacement cue even when no wreck was
    // retained; existing wrecks above already own that sound exactly once.
    const auto terrain = state.terrainByCell.find(runtimeCellKey(object.x, object.y));
    const TerrainCell* ground = terrain == state.terrainByCell.end() ? nullptr : terrain->second;
    const auto terrainInfo = ground ? runtimeTerrainInfoForCell(state, *ground) : RuntimeTerrainInfo{};
    const bool sinkingWithoutWreck = includeCrashingSound && unit != nullptr
        && object.garrisonHostObjectId < 0 && object.mission != "Passenger"
        && ground != nullptr && terrainInfo.water && !terrainInfo.bridge && !object.onBridge
        && unit->naval && !unit->underwater && !unit->organic
        && unit->weight >= unit->shipSinkingWeight && !object.airborne;
    if (sinkingWithoutWreck) {
        if (!trimAsciiLocal(unit->sinkingSound).empty()) {
            emitRuntimeSound(state, "sound:" + trimAsciiLocal(unit->sinkingSound), position, true);
        }
        return;
    }
    const bool crashingWithoutWreck = includeCrashingSound && unit != nullptr
        && object.airborne && !unit->missileSpawn
        && (unit->crashable
            || state.rules.findUnitType("AircraftType", object.type) != nullptr);
    if (crashingWithoutWreck && !trimAsciiLocal(unit->crashingSound).empty()) {
        emitRuntimeSound(state, "sound:" + trimAsciiLocal(unit->crashingSound), position, true);
    }
    if (unit != nullptr
        && runtimeObjectMovementMode(state, object) == PathMovementMode::Air) {
        const auto voices = splitRuntimeTokenList(unit->voiceCrashing);
        if (!voices.empty()) {
            const std::size_t seed =
                static_cast<std::size_t>(std::max(0, object.instanceId)) * 131U
                + static_cast<std::size_t>(std::max(0, state.tick)) * 17U
                + static_cast<std::size_t>(state.runtimeSoundEvents);
            emitRuntimeSound(state, voices[seed % voices.size()], position, true);
        }
    }
    if (crashingWithoutWreck) return;
    const std::string authored = unit == nullptr
        ? std::string{} : trimAsciiLocal(unit->dieSound);
    if (!authored.empty()) {
        auto sounds = splitRuntimeTokenList(authored);
        sounds.erase(std::remove_if(sounds.begin(), sounds.end(),
            [](const std::string& sound) {
                const std::string key = toLowerAsciiLocal(trimAsciiLocal(sound));
                return key == "none" || key == "<none>" || key == "-1";
            }), sounds.end());
        if (!sounds.empty()) {
            const std::size_t seed =
                static_cast<std::size_t>(std::max(0, object.instanceId)) * 131U
                + static_cast<std::size_t>(std::max(0, state.tick)) * 17U
                + static_cast<std::size_t>(state.runtimeSoundEvents) * 29U;
            emitRuntimeSound(state, "sound:" + sounds[seed % sounds.size()], position);
        }
    } else if (object.kind == WorldObjectKind::Unit
        && (unit == nullptr || !unit->missileSpawn)) {
        emitRuntimeSound(state, "sound:GenVehicleDie", position);
    }
}
