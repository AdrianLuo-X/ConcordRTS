// Exercise real nonlethal PsychicJab impacts, including the garrison variants.
bool runRuntimeInitiateFlameSmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail = [&](const std::string& reason) {
        error = "Initiate flame: " + reason;
        return false;
    };
    auto live = std::make_unique<RuntimeWindowState>();
    live->config = config;
    live->report = buildRuntimeStartupReport(config, &live->world, &live->theater,
        &live->art, &live->rules, &live->mapFacts);
    if (!live->report.success) return fail("stock startup failed");
    const auto* flameArt = live->art.findArtType("INITFIRE");
    const auto* fireArt = live->art.findArtType("FIRE01");
    if (!flameArt || toUpperAsciiLocal(flameArt->image) != "FIRE01"
        || flameArt->rate != 450 || !fireArt || fireArt->animationLoopCount != -1)
        return fail("original INITFIRE / FIRE01 alias missing");

    int checked = 0;
    for (int hz : {30, 60}) for (bool garrison : {false, true})
        for (int rank : {0, 2}) for (const std::string targetType : {"E1", "HTNK", "GAPOWR", "ground"}) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = config; probe.config.fixedTickHz = hz;
        probe.report = live->report;
        probe.rules = live->rules; probe.rulesLoaded = true;
        probe.art = live->art; probe.artLoaded = true;
        probe.theater = live->theater; probe.theaterLoaded = true;
        probe.visionEnabled = false;
        probe.runtimeSoundsEnabled = false;
        probe.runtimeSuppressPlayerAudioFeedback = true;
        probe.runtimePreferencePersistenceEnabled = false;
        for (int y = 5; y <= 16; ++y) for (int x = 5; x <= 16; ++x) {
            TerrainCell cell;
            cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0;
            probe.world.accessTerrainCells().push_back(cell);
            probe.terrainInfoByCell.emplace(cell.cellIndex512, RuntimeTerrainInfo{});
        }
        WorldObjectInstance source;
        source.instanceId = 1; source.kind = WorldObjectKind::Infantry;
        source.type = "INIT"; source.owner = "YuriCountry";
        source.health = 100; source.alive = true; source.veterancy = rank;
        source.x = 9; source.y = 10; source.cell = runtimeCellKey(9, 10);
        source.facing = 64; source.mission = "Guard";
        auto target = source;
        target.instanceId = 2; target.type = targetType; target.owner = "Americans";
        target.veterancy = 0; target.health = 1000;
        target.x = 12; target.cell = runtimeCellKey(12, 10);
        target.kind = targetType == "E1" ? WorldObjectKind::Infantry
            : (targetType == "HTNK" ? WorldObjectKind::Unit : WorldObjectKind::Structure);
        target.subCell = targetType == "E1" ? 2 : 0;
        probe.world.accessObjects().push_back(source);
        const bool ground = targetType == "ground";
        if (!ground) probe.world.accessObjects().push_back(target);
        probe.terrainByCell = buildRuntimeTerrainLookup(probe.world);
        initializeRuntimeNavigation(probe);
        if (ground) {
            const auto point = runtimeGroundAttackTargetObject(probe, {12, 10});
            if (!point) return fail("ground fixture missing");
            target = *point;
        }
        const auto profile = garrison
            ? runtimeGarrisonWeaponProfileForOccupant(probe, source, &target)
            : runtimeWeaponProfileForObject(probe, source, &target);
        const std::string expectedWeapon = garrison
            ? (rank == 2 ? "UCElitePsychicJab" : "UCPsychicJab")
            : (rank == 2 ? "PsychicJabE" : "PsychicJab");
        if (!profile.weapon || profile.weapon->id != expectedWeapon
            || !profile.projectile || !profile.projectile->inviso
            || !profile.warhead || chooseRuntimeImpactAnim(profile.warhead, *profile.weapon) != "INITFIRE")
            return fail("native weapon selection: " + expectedWeapon);
        spawnRuntimeProjectile(probe, source, target, *profile.weapon, profile, profile.weapon->damage);
        if (probe.projectiles.size() != 1) return fail("one shot was not spawned");
        const auto shot = probe.projectiles.front();
        probe.projectiles.clear();
        if (!applyRuntimeProjectileImpact(probe, shot)) return fail("impact rejected");
        const auto findFlame = [&]() {
            return std::find_if(probe.impactEffects.begin(), probe.impactEffects.end(),
                [&](const auto& effect) { return effect.projectileActorId == shot.actorId
                    && effect.renderIntent == "warheadImpact" && effect.animId == "INITFIRE"; });
        };
        auto flame = findFlame();
        if (flame == probe.impactEffects.end() || flame->animationFrameCount != 30
            || flame->animationSequenceSteps != 30 || flame->animationLoopCount != 1
            || flame->totalTicks != 2 * hz || flame->animationFrameDelayTicks != hz / 15)
            return fail("flame clipped or inherited FIRE01's infinite loop: " + expectedWeapon);
        const int healthAfter = ground ? 0 : findObjectById(probe, 2)->health;
        if (!ground && (healthAfter <= 0 || healthAfter >= target.health))
            return fail("nonlethal damage missing: " + expectedWeapon + " -> " + targetType);
        const auto first = *flame;
        std::vector<RuntimeImpactRenderActor> phases;
        int previousFrame = -1;
        for (int age = 0; age < first.totalTicks; ++age) {
            flame = findFlame();
            if (flame == probe.impactEffects.end() || flame->frameIndex < previousFrame
                || flame->frameIndex > 29 || !flame->hasWorldPosition
                || flame->worldX != first.worldX || flame->worldY != first.worldY)
                return fail("flame vanished, looped or left its hit point");
            if (flame->frameIndex != previousFrame) phases.push_back(makeRuntimeImpactRenderActor(*flame));
            previousFrame = flame->frameIndex;
            if (age == first.totalTicks / 2) {
                RuntimeImpactEffect restored;
                if (!parseRuntimeMultiplayerSnapshotImpactRecord(
                        runtimeMultiplayerSnapshotImpactRecord(*flame), restored)
                    || restored.remainingTicks != flame->remainingTicks
                    || restored.animationSequenceSteps != flame->animationSequenceSteps
                    || restored.animationFrameDelayTicks != flame->animationFrameDelayTicks)
                    return fail("snapshot lost flame timing");
            }
            ++probe.tick;
            advanceRuntimeEffects(probe);
        }
        if (findFlame() != probe.impactEffects.end() || phases.size() != 30
            || (!ground && findObjectById(probe, 2)->health != healthAfter))
            return fail("flame did not finish once or applied extra damage");
        RuntimeMapRenderContext context;
        context.rules = &probe.rules; context.art = &probe.art; context.theater = &probe.theater;
        context.assetRootPath = runtimeAssetRootPath(config);
        context.structureRootPath = runtimeStructureRootPath(config, probe.report.map);
        context.palettePath = runtimePalettePath(config, probe.report.map);
        context.structurePalettePath = runtimeStructurePalettePath(config, probe.report.map);
        context.fixedTickHz = hz;
        std::string previewPath;
        if (const char* folder = std::getenv("CONCORD_INITIATE_FLAME_DIR")) {
            if (*folder && hz == 30 && !garrison && rank == 0)
                previewPath = std::string(folder) + "/initiate-" + targetType + ".bmp";
        }
        if (!runRuntimeInitiateFlameRenderContractCheck(context, probe.world, phases, previewPath, error))
            return false;
        ++checked;
    }
    std::cout << "Initiate flame passed: " << checked
        << " normal/elite/garrison infantry/vehicle/building/ground cases; 30 frames,"
        << " 30/60 Hz duration, nonlethal hit, no extra damage, snapshot timing, one-shot expiry\n";
    return true;
}
