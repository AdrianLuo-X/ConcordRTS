void syncRuntimeVoxelDebrisPosition(RuntimeProjectileEffect& debris) {
    const auto& p = debris.voxelDebris.position;
    debris.currentX = p[0] / 256.0; debris.currentY = p[1] / 256.0;
    debris.currentZ = p[2] * runtimeDebrisHeightPerLepton;
    debris.currentCell = {static_cast<int>(std::lround(debris.currentX)), static_cast<int>(std::lround(debris.currentY))};
    debris.currentLevel = static_cast<int>(std::lround(debris.currentZ));
}

int spawnRuntimeUnitDebris(RuntimeWindowState& state, const WorldObjectInstance& destroyed,
    const UnitType& unit, const RuntimeProjectileEffect& cause) {
    if (unit.maxDebris <= 0) return 0;
    auto voxelIds = splitRuntimeTokenList(unit.debrisTypes);
    voxelIds.erase(std::remove_if(voxelIds.begin(), voxelIds.end(), [](const auto& id) {
        const auto key = toLowerAsciiLocal(id); return key == "none" || key == "<none>";
    }), voxelIds.end());
    if (voxelIds.empty()) {
        // Shared SHP list/count path, including repeated entries and the global
        // MetallicDebris fallback. Keep the accepted building implementation.
        BuildingType definition; definition.minDebris = unit.minDebris; definition.maxDebris = unit.maxDebris;
        definition.debrisAnims = unit.debrisAnims;
        return spawnRuntimeBuildingDebris(state, destroyed, definition, &cause, true);
    }
    std::uint64_t seed = runtimeStableHashString("unitVoxelDebris");
    runtimeStableHashMixString(seed, destroyed.type); runtimeStableHashMix(seed, destroyed.instanceId);
    runtimeStableHashMix(seed, state.tick);
    const int lower = std::min(unit.minDebris, unit.maxDebris - 1);
    const int upper = std::max(unit.minDebris, unit.maxDebris - 1);
    int budget = lower + static_cast<int>(runtimeDebrisRandom(seed) % (static_cast<std::uint64_t>(upper - lower) + 1));
    if (budget <= 0) return 0;
    const auto authoredMaximums = splitRuntimeTokenList(unit.debrisMaximums);
    std::vector<int> maximums(voxelIds.size(), 0);
    bool canEmit = false;
    for (std::size_t i = 0; i < voxelIds.size(); ++i) {
        if (i < authoredMaximums.size()) (void)parseRuntimeInt(authoredMaximums[i], maximums[i]);
        maximums[i] = std::max(0, maximums[i]);
        canEmit = canEmit || (maximums[i] > 0 && state.rules.findVoxelAnimType(voxelIds[i]));
    }
    // Missing types/maxima and all-zero lists would otherwise stall retail's
    // retry loop. Invalid mod data produces no substitute animation.
    if (!canEmit) return 0;
    const int hz = std::max(1, state.config.fixedTickHz);
    const auto origin = runtimeProjectileTargetPosition(state, destroyed);
    int spawned = 0;
    const std::uint64_t attemptLimit = (static_cast<std::uint64_t>(budget) + 1) * voxelIds.size() * 128;
    for (std::uint64_t attempt = 0; budget > 0 && attempt < attemptLimit; ++attempt) {
        const auto index = static_cast<std::size_t>(attempt % voxelIds.size());
        const auto* type = state.rules.findVoxelAnimType(voxelIds[index]);
        if (!type || maximums[index] <= 0) continue;
        const int count = std::min(budget, static_cast<int>(runtimeDebrisRandom(seed)
            % (static_cast<std::uint64_t>(maximums[index]) + 1)));
        for (int i = 0; i < count; ++i) {
            RuntimeProjectileEffect debris;
            debris.actorId = nextRuntimeProjectileActorId(state);
            debris.attackerObjectId = destroyed.instanceId; debris.sourceAnchorObjectId = destroyed.instanceId;
            debris.owner = destroyed.owner; debris.launchSourceKind = static_cast<int>(destroyed.kind);
            debris.launchSourceType = destroyed.type; debris.launchSourceAuthoredAircraft = destroyed.authoredAircraft;
            debris.launchSourceValidated = destroyed.instanceId >= 0 && !destroyed.owner.empty() && !destroyed.type.empty();
            debris.voxelDebris = runtimeMakeVoxelDebris(origin.x, origin.y, runtimeDrawLevelForObject(destroyed),
                type->duration, type->elasticity, type->minZVelocity, type->maxZVelocity, type->maxXYVelocity,
                type->minAngularVelocity, type->maxAngularVelocity, seed);
            syncRuntimeVoxelDebrisPosition(debris);
            debris.hasSourcePosition = debris.hasTargetPosition = true;
            debris.sourceX = debris.targetX = debris.currentX; debris.sourceY = debris.targetY = debris.currentY;
            debris.sourceZ = debris.targetZ = debris.currentZ;
            debris.fromCell = debris.toCell = debris.currentCell;
            debris.fromLevel = debris.toLevel = debris.currentLevel;
            debris.spawnTick = state.tick;
            debris.totalTicks = debris.remainingTicks = std::max(1, static_cast<int>((static_cast<long long>(type->duration) * hz + 14) / 15));
            debris.impactTick = state.tick + debris.totalTicks;
            debris.renderIntent = "unitVoxelDebris"; debris.weaponId = "UnitDebris"; debris.weaponSlot = "debris";
            debris.projectileId = type->id; debris.projectileImageId = type->image.empty() ? type->id : type->image;
            debris.impactAnimId = type->expireAnim; debris.trailerAnimId = type->trailerAnim;
            debris.damage = type->damage; debris.warheadId = type->warhead;
            debris.cellSpread = type->damageRadius / 256.0;
            debris.affectsAllies = debris.affectsEnemies = true;
            recordRuntimeProjectileReplayEvent(state, debris, "ProjectileSpawned");
            state.projectiles.push_back(std::move(debris)); ++spawned;
        }
        budget -= count;
    }
    state.actionSummary.projectilesInFlight = static_cast<int>(state.projectiles.size());
    return spawned;
}

RuntimeDebrisSurface runtimeVoxelDebrisSurface(const RuntimeWindowState& state,
    const std::array<float,3>& previous, const std::array<float,3>& next) {
    const int x = static_cast<int>(std::lround(next[0] / 256.0));
    const int y = static_cast<int>(std::lround(next[1] / 256.0));
    const auto found = state.terrainByCell.find(runtimeCellKey(x,y));
    RuntimeDebrisSurface surface;
    if (found == state.terrainByCell.end() || !found->second) { surface.exists = false; return surface; }
    const auto& terrain = *found->second;
    surface.height = terrain.level / runtimeDebrisHeightPerLepton;
    const auto info = runtimeCachedTerrainInfoForCell(state, terrain);
    surface.water = info.water && !info.bridge;
    if (runtimeOverlayProvidesBridgeDeck(terrain)) {
        const double deck = runtimeEffectiveTerrainLevel(terrain) / runtimeDebrisHeightPerLepton;
        if (previous[2] >= deck) { surface.height = deck; surface.water = false; }
        else if (next[2] >= deck && runtimeOverlayLooksHighBridge(terrain)) {
            surface.height = deck - 20.0; surface.ceiling = true; surface.water = false;
        }
    }
    return surface;
}

void spawnRuntimeVoxelDebrisAnimation(RuntimeWindowState& state, const RuntimeProjectileEffect& debris,
    const std::string& id) {
    if (id.empty() || !state.art.findArtType(id)) return;
    RuntimeProjectileTargetPosition position;
    position.cell = debris.currentCell; position.x = debris.currentX; position.y = debris.currentY;
    position.level = debris.currentLevel;
    spawnRuntimeImpact(state, position.cell, position.level, false, id, "", 0, 0.0, false, "", &debris, &position);
    if (!state.impactEffects.empty()) state.impactEffects.back().worldLevel = debris.currentZ;
}

bool advanceRuntimeVoxelDebris(RuntimeWindowState& state, RuntimeProjectileEffect& debris) {
    auto& motion = debris.voxelDebris;
    const auto* type = state.rules.findVoxelAnimType(debris.projectileId);
    if (!type) { debris.remainingTicks = 0; return false; }
    const int hz = std::max(1, state.config.fixedTickHz);
    ++debris.elapsedTicks; debris.remainingTicks = std::max(0, debris.totalTicks - debris.elapsedTicks);
    debris.progress = std::clamp(static_cast<double>(debris.elapsedTicks) / debris.totalTicks, 0.0, 1.0);
    debris.lifecycle = RuntimeProjectileLifecycleState::Traveling;
    debris.lifecycleState = runtimeProjectileLifecycleName(debris.lifecycle);
    motion.frameRemainder += 15;
    while (motion.frameRemainder >= hz) {
        motion.frameRemainder -= hz;
        // Retail decrements Duration first; zero duration expires before motion.
        if (motion.framesLeft > 0) --motion.framesLeft;
        if (motion.framesLeft <= 0) {
            const auto surface = runtimeVoxelDebrisSurface(state, motion.position, motion.position);
            if (surface.water && motion.position[2] <= surface.height + 10.0) {
                spawnRuntimeVoxelDebrisAnimation(state, debris, state.rules.getWake());
                const auto splashes = splitRuntimeTokenList(state.rules.getAudioVisualValue("SplashList"));
                if (!splashes.empty()) spawnRuntimeVoxelDebrisAnimation(state, debris,
                    splashes[static_cast<std::size_t>(debris.actorId) % splashes.size()]);
            } else spawnRuntimeVoxelDebrisAnimation(state, debris, type->expireAnim);
            debris.remainingTicks = 0; return false;
        }
        ++motion.frameCount;
        const auto contact = runtimeStepVoxelDebris(motion, [&](const auto& previous, const auto& next) {
            return runtimeVoxelDebrisSurface(state, previous, next);
        });
        syncRuntimeVoxelDebrisPosition(debris);
        if (contact == RuntimeDebrisContact::Outside) { debris.remainingTicks = 0; return false; }
        if (contact == RuntimeDebrisContact::Bounce) {
            spawnRuntimeVoxelDebrisAnimation(state, debris, type->bounceAnim);
            if (debris.damage > 0) {
                auto hit = debris; hit.voxelDebris = {}; hit.renderIntent = "unitDebrisImpact";
                hit.toCell = hit.currentCell; hit.targetX = hit.currentX; hit.targetY = hit.currentY;
                hit.targetZ = hit.currentZ; hit.toLevel = hit.currentLevel; hit.impactAnimId.clear();
                (void)applyRuntimeProjectileImpact(state, hit);
            }
        }
        if ((motion.frameCount % 2) == 0) spawnRuntimeVoxelDebrisAnimation(state, debris, type->trailerAnim);
        if (contact == RuntimeDebrisContact::Stopped || contact == RuntimeDebrisContact::Water) motion.framesLeft = 0;
    }
    return true;
}
