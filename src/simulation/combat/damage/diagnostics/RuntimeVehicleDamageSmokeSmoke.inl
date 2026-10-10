bool runRuntimeVehicleDamageSmokeSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& text) { error = "vehicle damage smoke: " + text; return false; };
    const auto* system = live.rules.findParticleSystemType("SmallGreySSys");
    const auto* particle = live.rules.findParticleType("SmallGreySmoke");
    if (system == nullptr || particle == nullptr || system->holdsWhat != "SmallGreySmoke"
        || system->spawnFrames != 10 || system->particleCap != 7
        || particle->image != "SGRYSMK1" || particle->translucency != 50
        || particle->endStateAI != 20 || particle->stateAIAdvance != 4)
        return fail("original SmallGreySSys / SmallGreySmoke / SGRYSMK1 chain missing");
    const auto makeProbe = [&](int hz) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = live.config; probe.config.fixedTickHz = hz;
        probe.rules = live.rules; probe.rulesLoaded = true;
        probe.art = live.art; probe.artLoaded = true;
        probe.economy.owner = "Americans";
        probe.economy.skirmishStartApplied = true;
        probe.triggers.bootstrapComplete = true;
        probe.runtimeSuppressPlayerAudioFeedback = true;
        probe.runtimePreferencePersistenceEnabled = false;
        for (int y = 1; y <= 20; ++y) for (int x = 1; x <= 20; ++x) {
            TerrainCell cell;
            cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0;
            probe.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(probe);
        ensureRuntimeParticlesInitialized(probe);
        return storage;
    };
    const auto advance = [](RuntimeWindowState& probe, int ticks) {
        for (int i = 0; i < ticks; ++i) { ++probe.tick; advanceRuntimeParticles(probe); }
    };
    const auto countEmitting = [](const RuntimeWindowState& probe) {
        return std::count_if(probe.particles.systems().begin(), probe.particles.systems().end(),
            [](const auto& value) { return value.emitting && value.sourceTag == "vehicle-damage-smoke"; });
    };
    auto coverageStorage = makeProbe(30);
    auto& coverage = *coverageStorage;
    int eligibleTypes = 0;
    for (const auto& unit : live.rules.getVehicleTypes()) {
        bool authoredSmoke = false;
        for (const auto& id : unit.damageParticleSystems) {
            const auto* entry = live.rules.findParticleSystemType(id);
            authoredSmoke = authoredSmoke || (entry != nullptr && entry->behavior == ParticleBehavior::Smoke
                && live.rules.findParticleType(entry->holdsWhat) != nullptr);
        }
        if (unit.strength <= 0) continue;
        WorldObjectInstance object;
        object.kind = WorldObjectKind::Unit; object.type = unit.id; object.instanceId = 1;
        object.alive = true; object.health = std::max(1, unit.strength / 4);
        if ((runtimeVehicleDamageSmokeType(coverage, object) != nullptr) != authoredSmoke)
            return fail("authored smoke eligibility ignored: " + unit.id);
        if (authoredSmoke) ++eligibleTypes;
        object.health = unit.strength;
        if (runtimeVehicleDamageSmokeType(coverage, object) != nullptr)
            return fail("healthy vehicle smoked: " + unit.id);
    }
    if (eligibleTypes < 20) return fail("too few stock vehicles covered");
    std::vector<WorldObjectInstance> poses;
    std::vector<std::vector<RuntimeParticleRenderSnapshot>> previews;
    for (int hz : {30, 60}) for (const char* id : {"MTNK", "HTNK", "APOC", "MIND"}) {
        auto storage = makeProbe(hz);
        auto& probe = *storage;
        auto* created = addRuntimeObjectInstance(probe, RuntimeBuildKind::Unit, id, {7, 7});
        if (created == nullptr) return fail("vehicle fixture missing");
        auto& tank = *created;
        tank.facing = tank.turretFacing = 96;
        const int maximum = runtimeMaxHealthForObject(probe, tank);
        const int threshold = static_cast<int>(maximum * live.rules.getSpecialMechanismRules().conditionYellow);
        tank.health = threshold + 1;
        advance(probe, 1);
        if (!probe.particles.systems().empty()) return fail("smoke above yellow health");
        tank.health = threshold;
        advance(probe, 1);
        if (countEmitting(probe) != 1 || probe.particles.particles().size() != 1U)
            return fail("yellow boundary did not start exactly one original smoke producer");
        const auto origin = runtimeVehicleDamageSmokeOrigin(probe, tank);
        const auto producerId = probe.particles.systems().front().id;
        const auto firstParticleId = probe.particles.particles().front().id;
        const auto firstPosition = probe.particles.particles().front().position;
        advanceRuntimeParticles(probe);
        if (probe.particles.systems().front().id != producerId || probe.particles.particles().size() != 1U)
            return fail("duplicate paint/update spawned another producer");
        tank.hasRenderPosition = tank.renderMoveActive = true;
        tank.renderX = 10.25; tank.renderY = 8.5; tank.renderLevel = 2.0;
        tank.owner = "Russians";
        advance(probe, hz / 15 * 10);
        const auto movedOrigin = runtimeVehicleDamageSmokeOrigin(probe, tank);
        const auto& attached = probe.particles.systems().front();
        const auto& oldSmoke = probe.particles.particles().front();
        if (attached.id != producerId || attached.owner != tank.owner
            || attached.origin.x != movedOrigin.x || attached.origin.y != movedOrigin.y
            || attached.origin.z != movedOrigin.z || oldSmoke.id != firstParticleId
            || oldSmoke.position.x != firstPosition.x || oldSmoke.position.y != firstPosition.y
            || oldSmoke.position.z <= firstPosition.z || movedOrigin.x == origin.x)
            return fail("moving/captured tank dragged old smoke or left the emitter behind");
        std::ostringstream saved;
        probe.particles.save(saved);
        IniFile ini;
        ini.loadFromString(saved.str());
        RuntimeParticleSystemState restored(hz);
        if (!restored.load(ini, probe.rules, &error) || restored.stateHash() != probe.particles.stateHash())
            return fail("attached smoke failed to round trip: " + error);
        auto expected = restored;
        probe.particles = std::move(restored);
        ++probe.tick;
        advanceRuntimeParticles(probe);
        (void)expected.tick(probe.rules);
        if (probe.particles.stateHash() != expected.stateHash())
            return fail("loading restarted or duplicated the smoke producer");
        tank.hasRenderPosition = tank.renderMoveActive = false;
        tank.owner = "Americans";
        advance(probe, hz * 6);
        const auto snapshots = probe.particles.renderSnapshots(probe.rules, &probe.art);
        std::set<int> frames;
        for (const auto& snapshot : snapshots) {
            if (snapshot.imageId != "SGRYSMK1" || snapshot.renderKind != RuntimeParticleRenderKind::Shp)
                return fail("damaged vehicle used a substitute smoke image");
            frames.insert(snapshot.frameIndex);
        }
        if (frames.size() < 3U || snapshots.size() > 7U || countEmitting(probe) != 1)
            return fail("smoke animation was frozen, unbounded or restarted each tick");
        if (hz == 30) { poses.push_back(tank); previews.push_back(snapshots); }
        tank.health = maximum;
        advance(probe, 1);
        if (countEmitting(probe) != 0 || probe.particles.particles().empty())
            return fail("repair did not stop new smoke and retain fading puffs");
        advance(probe, hz * 6);
        if (!probe.particles.systems().empty() || !probe.particles.particles().empty())
            return fail("repaired vehicle smoke never dissipated");
        tank.health = threshold / 2;
        advance(probe, 1);
        if (countEmitting(probe) != 1) return fail("new damage did not restart smoke");
        tank.cloaked = true;
        advance(probe, 1);
        if (countEmitting(probe) != 0) return fail("cloaked vehicle continued emitting");
        tank.cloaked = false; tank.dockedToObjectId = 123;
        advance(probe, 1);
        if (countEmitting(probe) != 0) return fail("passenger emitted outside its transport");
        tank.dockedToObjectId = -1; tank.submerged = true;
        advance(probe, 1);
        if (countEmitting(probe) != 0) return fail("submerged vehicle emitted above water");
        tank.submerged = false;
        advance(probe, 1);
        RuntimeObjectExitRequest exit;
        exit.objectId = tank.instanceId;
        exit.kind = RuntimeObjectExitKind::Destroyed;
        exit.tick = probe.tick;
        exit.mission = "Destroyed";
        exit.reason = "vehicle damage smoke probe";
        exit.destructionEffectsSpawned = true;
        exit.suppressDeathWeapon = true;
        (void)queueRuntimeObjectExit(probe, std::move(exit));
        (void)drainRuntimeObjectExitQueue(probe);
        if (countEmitting(probe) != 0 || probe.particles.particles().empty())
            return fail("object exit did not immediately stop smoke while retaining fading puffs");
        advance(probe, hz * 6);
        if (!probe.particles.systems().empty() || !probe.particles.particles().empty())
            return fail("destroyed source left a permanent emitter");
    }
    RuntimeMapRenderContext context;
    context.rules = &live.rules; context.art = &live.art;
    context.assetRootPath = runtimeAssetRootPath(live.config);
    context.palettePath = runtimePalettePath(live.config, live.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(live.config, live.report.map);
    if (!runRuntimeVehicleDamageSmokeRenderContractCheck(context, poses, previews, error)) return false;
    // Check the real moving emitter, including a turret facing away from its
    // hull. All eight hull directions must emit over the turret/body centre.
    for (int facing = 0; facing < 256; facing += 32) {
        std::vector<WorldObjectInstance> facingPoses;
        std::vector<std::vector<RuntimeParticleRenderSnapshot>> facingSmoke;
        for (const char* id : {"MTNK", "HTNK", "APOC", "MIND"}) {
            auto storage = makeProbe(30); auto& probe = *storage;
            auto* tank = addRuntimeObjectInstance(probe, RuntimeBuildKind::Unit, id, {7, 7});
            if (tank == nullptr) return fail("turret smoke fixture missing");
            tank->facing = facing; tank->turretFacing = (facing + 64) % 256;
            tank->health = runtimeMaxHealthForObject(probe, *tank) / 3;
            tank->hasRenderPosition = tank->renderMoveActive = true;
            tank->renderX = 7.25; tank->renderY = 7.5; tank->renderLevel = 2.0;
            advance(probe, 90);
            if (countEmitting(probe) != 1) return fail("turret smoke source did not emit");
            const auto origin = probe.particles.systems().front().origin;
            const double dx = origin.x / 256.0 - tank->renderX;
            const double dy = origin.y / 256.0 - tank->renderY;
            const double height = origin.z / 256.0 - tank->renderLevel;
            if (std::hypot(dx, dy) > 0.15 || height < 0.75 || height > 1.5)
                return fail("smoke source was not above the turret/body centre: " + std::string(id));
            tank->turretFacing = (facing + 192) % 256;
            advance(probe, 1);
            const auto afterTurret = probe.particles.systems().front().origin;
            if (origin.x != afterTurret.x || origin.y != afterTurret.y || origin.z != afterTurret.z)
                return fail("turret rotation moved smoke away from the turret centre");
            facingPoses.push_back(*tank);
            facingSmoke.push_back(probe.particles.renderSnapshots(probe.rules, &probe.art));
        }
        if (!runRuntimeVehicleDamageSmokeRenderContractCheck(context, facingPoses, facingSmoke, error)) return false;
    }
    std::cout << "Vehicle damage smoke: " << eligibleTypes
        << " stock vehicle types; yellow boundary, moving plume, original SHP frames, 30/60Hz, save, repair, concealment, death=ok; 4 tanks x 8 turret smoke positions and turret rotation=ok\n";
    error.clear();
    return true;
}
