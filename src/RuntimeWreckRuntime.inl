void spawnRuntimeWreckAnimation(RuntimeWindowState& state, const RuntimeWreckState& wreck,
    const WorldObjectInstance& pose, const std::string& animation, bool water, const char* phase);

bool beginRuntimeWreck(RuntimeWindowState& state, const WorldObjectInstance& destroyed,
    const UnitType& unit, const RuntimeProjectileEffect& cause, bool wasAliveBeforeDamage = false) {
    if (destroyed.kind == WorldObjectKind::Structure || (!destroyed.alive && !wasAliveBeforeDamage)
        || destroyed.garrisonHostObjectId >= 0 || destroyed.mission == "Passenger") return false;
    for (const auto& wreck : state.wrecks)
        if (wreck.object.instanceId == destroyed.instanceId) return true;
    const auto terrain = state.terrainByCell.find(runtimeCellKey(destroyed.x, destroyed.y));
    const TerrainCell* ground = terrain == state.terrainByCell.end() ? nullptr : terrain->second;
    const auto terrainInfo = ground ? runtimeTerrainInfoForCell(state, *ground) : RuntimeTerrainInfo{};
    const bool water = ground && terrainInfo.water && !terrainInfo.bridge && !destroyed.onBridge;
    const bool drownedVehicle = water && !destroyed.onBridge
        && runtimeVehicleCannotSurviveOpenWaterAtCell(state, destroyed, {destroyed.x, destroyed.y});
    const bool sinking = drownedVehicle || (water && unit.naval && !unit.underwater && !unit.organic
        && unit.weight >= unit.shipSinkingWeight && !destroyed.airborne);
    const bool aircraftType = state.rules.findUnitType("AircraftType", destroyed.type) != nullptr;
    const bool crashing = destroyed.airborne && !unit.missileSpawn && (aircraftType || unit.crashable);
    if (!sinking && !crashing) return false;
    RuntimeWreckState wreck;
    wreck.object = destroyed;
    wreck.object.transportPassengerIds.clear(); wreck.object.garrisonOccupantIds.clear();
    wreck.object.alive = false; wreck.object.health = 0;
    wreck.startTick = state.tick; wreck.sinking = sinking;
    wreck.jumpjet = unit.jumpJet || toLowerAsciiLocal(unit.locomotor).find("92612c46") != std::string::npos;
    wreck.tilt = !wreck.jumpjet || unit.tiltCrashJumpjet;
    wreck.x = runtimeObjectRenderX(destroyed); wreck.y = runtimeObjectRenderY(destroyed);
    wreck.groundLevel = ground ? runtimeEffectiveTerrainLevel(*ground) : 0.0;
    // Include the existing aircraft draw-origin lift so the first retained
    // frame joins the live sprite exactly, then let it reach the surface.
    wreck.startLevel = runtimeDrawLevelForObject(destroyed) + (crashing ? 34.0 / 15.0 : 0.0);
    wreck.fallSpeed = std::max(1.0, unit.jumpjetCrash);
    wreck.gravity = std::max(0.01, unit.crashGravity);
    if (crashing && !wreck.jumpjet) {
        const double heading = destroyed.facing * 6.283185307179586 / 256.0;
        const double speed = std::clamp(unit.speed / 256.0, 0.0, 0.25);
        wreck.driftX = (std::sin(heading) - std::cos(heading)) * speed / std::sqrt(2.0);
        wreck.driftY = (-std::sin(heading) - std::cos(heading)) * speed / std::sqrt(2.0);
    }
    wreck.impactAnim = runtimeUnitDestructionAnim(state, destroyed, unit, cause);
    wreck.warhead = cause.warheadId;
    state.wrecks.push_back(std::move(wreck));
    if (drownedVehicle) {
        const auto& retained = state.wrecks.back();
        spawnRuntimeWreckAnimation(state, retained,
            runtimeWreckObjectPose(retained, state.tick, state.config.fixedTickHz),
            "H2O_EXP3", true, "WreckWaterEntry");
    }
    const std::string sound = sinking ? unit.sinkingSound : unit.crashingSound;
    if (!trimAsciiLocal(sound).empty()) emitRuntimeSound(state, "sound:" + trimAsciiLocal(sound), runtimeSpatialAudioPositionForObject(destroyed), true);
    if (crashing) {
        const auto voices = splitRuntimeTokenList(unit.voiceCrashing);
        if (!voices.empty()) emitRuntimeSound(state, "sound:" + voices[static_cast<std::size_t>(destroyed.instanceId) % voices.size()], runtimeSpatialAudioPositionForObject(destroyed), true);
    }
    return true;
}

void spawnRuntimeWreckAnimation(RuntimeWindowState& state, const RuntimeWreckState& wreck,
    const WorldObjectInstance& pose, const std::string& animation, bool water, const char* phase) {
    if (animation.empty() || !state.art.findArtType(animation)) return;
    RuntimeProjectileTargetPosition position;
    position.x = pose.renderX; position.y = pose.renderY;
    position.cell = {pose.x, pose.y};
    position.level = static_cast<int>(std::lround(water ? wreck.groundLevel : pose.renderLevel));
    spawnRuntimeImpact(state, position.cell, position.level, !water, animation, wreck.warhead,
        0, 0.0, true, "", nullptr, &position);
    if (!state.impactEffects.empty()) {
        auto& effect = state.impactEffects.back();
        effect.owner = wreck.object.owner;
        effect.worldX = position.x; effect.worldY = position.y;
        effect.worldLevel = water ? wreck.groundLevel : pose.renderLevel;
        effect.renderIntent = water ? "waterWake" : "unitDestruction";
        effect.lifecycleState = phase;
        if (water) {
            // Retain the chosen authored water animation in the canonical
            // impact record and advance its complete native frame sequence.
            effect.warheadImpactIntentId = animation;
            initializeRuntimeImpactAnimationTimeline(state, effect, true);
        }
    }
}

bool advanceRuntimeWrecks(RuntimeWindowState& state) {
    if (state.wrecks.empty()) return false;
    std::size_t kept = 0;
    for (auto& wreck : state.wrecks) {
        const double frames = runtimeWreckAgeFrames(wreck, state.tick, state.config.fixedTickHz);
        auto pose = runtimeWreckObjectPose(wreck, state.tick, state.config.fixedTickHz);
        const auto terrain = state.terrainByCell.find(runtimeCellKey(pose.x, pose.y));
        const TerrainCell* ground = terrain == state.terrainByCell.end() ? nullptr : terrain->second;
        const auto info = ground ? runtimeTerrainInfoForCell(state, *ground) : RuntimeTerrainInfo{};
        const double groundLevel = ground ? runtimeEffectiveTerrainLevel(*ground) : wreck.groundLevel;
        const bool water = ground && info.water && !info.bridge;
        const bool finished = wreck.sinking ? runtimeWreckFallLeptons(wreck, frames) > 400.0
            : pose.renderLevel <= groundLevel || !ground;
        const auto* unit = findRuntimeUnitTypeById(state.rules, wreck.object.type);
        const int previousFrame = static_cast<int>(runtimeWreckAgeFrames(wreck, state.tick - 1, state.config.fixedTickHz));
        const int currentFrame = static_cast<int>(frames);
        if (!finished && currentFrame / 4 > previousFrame / 4) {
            if (wreck.sinking) {
                std::uint64_t seed = runtimeStableHashString("sinkingWake");
                runtimeStableHashMix(seed, wreck.object.instanceId); runtimeStableHashMix(seed, currentFrame / 4);
                pose.renderX += (static_cast<int>(seed % 341) - 170) / 256.0;
                pose.renderY += (static_cast<int>((seed >> 16) % 341) - 170) / 256.0;
                spawnRuntimeWreckAnimation(state, wreck, pose, state.rules.getWake(), true, "WreckWake");
            } else {
                spawnRuntimeWreckAnimation(state, wreck, pose, "SMOKEY", false, "WreckSmoke");
            }
        }
        if (finished) {
            if (!wreck.sinking && ground) {
                pose.renderLevel = groundLevel;
                pose.level = static_cast<int>(groundLevel); pose.airborne = false;
                pose.onBridge = info.bridge; pose.renderOnBridge = info.bridge;
                RuntimeProjectileEffect cause; cause.warheadId = wreck.warhead;
                const bool infantrySequence = !water && unit && pose.kind == WorldObjectKind::Infantry
                    && spawnRuntimeInfantryDeathSequence(state, pose, *unit, cause);
                if (!infantrySequence) {
                    // The landing surface can differ from the cell under the
                    // aircraft at destruction (slopes, bridges and shorelines).
                    auto landing = wreck; landing.groundLevel = groundLevel;
                    spawnRuntimeWreckAnimation(state, landing, pose, water ? "H2O_EXP3" : wreck.impactAnim,
                        water, water ? "WreckWaterImpact" : "WreckImpact");
                }
                const std::string sound = unit ? (water ? unit->impactWaterSound : unit->impactLandSound) : std::string();
                if (!sound.empty()) emitRuntimeSound(state, "sound:" + sound, runtimeSpatialAudioPositionForObject(pose));
            }
            continue;
        }
        if (&state.wrecks[kept] != &wreck) state.wrecks[kept] = std::move(wreck);
        ++kept;
    }
    state.wrecks.resize(kept);
    return true;
}
