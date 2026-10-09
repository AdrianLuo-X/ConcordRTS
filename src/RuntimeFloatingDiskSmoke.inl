bool runRuntimeFloatingDiskSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) { error = "floating disk: " + message; return false; };
    const auto* rules = live.rules.findUnitType("Vehicle", "DISK");
    const auto* weapon = live.rules.findWeaponType("DiskLaser");
    if (rules == nullptr || !rules->turretSpins || weapon == nullptr || !weapon->diskLaser
        || weapon->laser || weapon->laserInnerColor != "216,0,184"
        || weapon->laserOuterColor != "80,0,88" || weapon->laserDuration != 15)
        return fail("stock spin / dedicated purple ring rules were not read");
    const auto makeProbe = [&](int hz) {
        auto probe = std::make_unique<RuntimeWindowState>();
        probe->config = live.config;
        probe->config.fixedTickHz = hz;
        probe->rules = live.rules; probe->rulesLoaded = true;
        probe->art = live.art; probe->artLoaded = true;
        probe->economy.owner = "YuriCountry";
        probe->economy.skirmishStartApplied = true;
        probe->triggers.bootstrapComplete = true;
        probe->runtimeSuppressPlayerAudioFeedback = true;
        for (int y = 1; y <= 16; ++y) for (int x = 1; x <= 16; ++x) {
            TerrainCell cell;
            cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0;
            probe->world.accessTerrainCells().push_back(cell);
        }
        addRuntimeObjectInstance(*probe, RuntimeBuildKind::Unit, "DISK", {6, 6});
        initializeRuntimeNavigation(*probe);
        return probe;
    };
    std::vector<WorldObjectInstance> poses;
    std::vector<RuntimeProjectileRenderActor> previews;
    for (int hz : {30, 60}) {
        auto storage = makeProbe(hz);
        auto& probe = *storage;
        auto& disk = probe.world.accessObjects().front();
        disk.facing = disk.turretFacing = 0;
        if (hz == 30) poses.push_back(disk);
        std::set<int> facings{0};
        for (int tick = 1; tick <= hz * 32 / 15; ++tick) {
            probe.tick = tick;
            advanceRuntimeIdleWeaponFacings(probe);
            facings.insert(disk.turretFacing);
            if (hz == 30 && (tick == 2 || tick == 4 || tick == 6)) poses.push_back(disk);
        }
        if (facings.size() != 32U || disk.turretFacing != 0 || disk.facing != 0
            || disk.x != 6 || disk.y != 6 || !disk.airborne)
            return fail("hovering did not spin once without turning or moving the hull");
    }
    for (const std::string mode : {"normal", "elite", "60hz", "owner-lost", "target-lost", "out-of-range"}) {
        const int hz = mode == "60hz" ? 60 : 30;
        auto storage = makeProbe(hz);
        auto& probe = *storage;
        const int diskId = probe.world.getObjects().front().instanceId;
        auto* target = addRuntimeObjectInstanceForOwner(probe, "Americans", RuntimeBuildKind::Unit, "MTNK", {10, 6});
        if (target == nullptr) return fail("target fixture missing");
        const int targetId = target->instanceId;
        target->health = 2000; target->mission = "Sleep";
        auto* disk = findMutableObjectById(probe, diskId);
        disk->veterancy = mode == "elite" ? 2 : 0;
        applySelection(probe, {diskId}, false, false);
        issueRuntimeAttackCommand(probe, *target);
        for (int tick = 0; tick < 80 && probe.projectiles.empty(); ++tick) {
            ++probe.tick;
            advanceRuntimeSimulation(probe);
        }
        if (probe.projectiles.size() != 1U || probe.projectiles.front().renderIntent != "diskLaserCharge")
            return fail(mode + " attack did not launch the ring charge");
        const auto charge = probe.projectiles.front();
        if (charge.weaponId != (mode == "elite" ? "DiskLaserE" : "DiskLaser")
            || charge.totalTicks != hz * 16 / 15)
            return fail(mode + " charge weapon or timing changed");
        RuntimeProjectileEffect restored;
        if (!parseRuntimeProjectileEffect(serializeRuntimeProjectileEffect(charge), restored)
            || serializeRuntimeProjectileEffect(restored) != serializeRuntimeProjectileEffect(charge))
            return fail("charge save round trip lost its phase or source");
        if (mode == "normal") {
            previews.push_back(makeRuntimeProjectileRenderActor(charge, &probe.rules, &probe.art));
            poses.push_back(*findObjectById(probe, diskId));
        }
        if (mode != "normal") eraseRuntimeBehavior(probe, diskId);
        if (mode == "owner-lost") findMutableObjectById(probe, diskId)->alive = false;
        if (mode == "target-lost") findMutableObjectById(probe, targetId)->alive = false;
        if (mode == "out-of-range") setRuntimeObjectCell(probe, *findMutableObjectById(probe, targetId), {15, 6});
        const bool cancelled = mode == "owner-lost" || mode == "target-lost" || mode == "out-of-range";
        int hitHealth = 2000;
        bool beamSeen = false;
        for (int elapsed = 1; elapsed <= hz * 32 / 15; ++elapsed) {
            const int facingBefore = findMutableRuntimeObjectByIdIncludingInactive(probe, diskId)->turretFacing;
            ++probe.tick;
            if (mode == "normal") {
                advanceRuntimeSimulation(probe);
                const int frames = runtimeDiskLaserAuthoredFrame(probe.tick, hz)
                    - runtimeDiskLaserAuthoredFrame(probe.tick - 1, hz);
                if (findMutableObjectById(probe, diskId)->turretFacing
                    != normalizeRuntimeFacing(facingBefore + 8 * frames))
                    return fail("attack aiming interrupted the continuous turret spin");
            } else advanceRuntimeEffects(probe);
            const int health = findMutableRuntimeObjectByIdIncludingInactive(probe, targetId)->health;
            if ((cancelled || elapsed < charge.totalTicks) && health != 2000)
                return fail(mode + " damaged the target before charge completion");
            for (const auto& shot : probe.projectiles) {
                if (mode == "normal" && shot.renderIntent == "diskLaserCharge"
                    && (shot.elapsedTicks == 8 || shot.elapsedTicks == 24)) {
                    previews.push_back(makeRuntimeProjectileRenderActor(shot, &probe.rules, &probe.art));
                    poses.push_back(*findObjectById(probe, diskId));
                }
                if (shot.renderIntent != "diskLaserBeam") continue;
                if (cancelled) return fail("cancelled charge emitted a beam");
                if (!beamSeen) {
                    beamSeen = true; hitHealth = health;
                    if (health >= 2000 || shot.damage != 0 || shot.totalTicks != hz
                        || shot.targetObjectId >= 0) return fail("beam did not apply one hit and retain fixed endpoints");
                    if (!parseRuntimeProjectileEffect(serializeRuntimeProjectileEffect(shot), restored)
                        || serializeRuntimeProjectileEffect(restored) != serializeRuntimeProjectileEffect(shot))
                        return fail("beam save round trip lost its endpoint or fade phase");
                    if (mode == "normal") {
                        previews.push_back(makeRuntimeProjectileRenderActor(shot, &probe.rules, &probe.art));
                        poses.push_back(*findObjectById(probe, diskId));
                    }
                }
            }
            if (beamSeen && health != hitHealth) return fail("beam fade applied extra damage");
        }
        if ((!cancelled && !beamSeen) || !probe.projectiles.empty()) return fail(mode + " beam lifecycle did not finish");
    }
    if (poses.size() != 8U || previews.size() != 4U) return fail("animation preview phases missing");
    RuntimeMapRenderContext context;
    context.rules = &live.rules; context.art = &live.art;
    context.assetRootPath = runtimeAssetRootPath(live.config);
    context.palettePath = runtimePalettePath(live.config, live.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(live.config, live.report.map);
    if (!runRuntimeFloatingDiskRenderContractCheck(context, poses, previews, error)) return false;
    std::cout << "Floating disk: 32 hover facings, stock ring geometry/colors, staged hit/fade, elite, 30/60Hz, cancellation, save, original voxel pixels=ok\n";
    return true;
}
