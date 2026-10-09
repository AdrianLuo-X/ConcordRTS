#include "RuntimeVehicleBlastSmoke.inl"

bool runRuntimeVehicleParasiteBiteTimingProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& text) {
        error = "parasite bite rocking: " + text;
        return false;
    };
    for (int hz : {30, 60}) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = live.config; probe.config.fixedTickHz = hz;
        probe.config.noCooldown = false;
        probe.rules = live.rules; probe.rulesLoaded = true;
        probe.art = live.art; probe.artLoaded = true;
        probe.economy.owner = "Americans";
        probe.economy.skirmishStartApplied = true;
        probe.runtimeSuppressPlayerAudioFeedback = true;
        probe.runtimePreferencePersistenceEnabled = false;
        probe.tick = hz;
        for (int y = 1; y <= 16; ++y) for (int x = 1; x <= 16; ++x) {
            TerrainCell cell;
            cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0;
            probe.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(probe);
        auto* created = addRuntimeObjectInstance(probe, RuntimeBuildKind::Unit, "DRON", {7, 7});
        if (created == nullptr) return fail("stock drone fixture missing");
        const int droneId = created->instanceId;
        created->owner = "Russians";
        created = addRuntimeObjectInstance(probe, RuntimeBuildKind::Unit, "APOC", {8, 7});
        if (created == nullptr) return fail("stock vehicle fixture missing");
        const int tankId = created->instanceId;
        const auto* drone = findObjectById(probe, droneId);
        const auto* tank = findObjectById(probe, tankId);
        const auto profile = runtimeWeaponProfileForObject(probe, *drone, tank);
        if (profile.weapon == nullptr
            || !launchRuntimeParasiteProjectile(probe, *drone, *tank, *profile.weapon, profile)
            || probe.projectiles.empty()) return fail("stock drone launch failed");
        const auto impact = probe.projectiles.back();
        probe.projectiles.clear();
        if (!applyRuntimeParasiteProjectileImpactExclusive(probe, impact)
            || !probe.specialMechanisms.parasiteLinks().contains(tankId))
            return fail("stock drone did not attach");
        const int attachedTick = probe.tick;
        const int interval = probe.specialMechanisms.parasiteLinks().at(tankId).damageIntervalTicks;
        if (interval != 4 * hz) return fail("stock DroneJump ROF cadence changed");
        const auto bitePose = [&](const RuntimeWindowState& state) {
            for (const auto& snapshot : state.specialMechanisms.renderSnapshots(state.tick, hz)) {
                if (snapshot.kind != "parasite" || snapshot.targetObjectId != tankId) continue;
                const int age = runtimeVehicleParasiteBiteAgeTicks(snapshot.progress,
                    snapshot.requiredProgress, snapshot.expireTick, state.tick);
                return runtimeVehicleParasiteMotion(static_cast<double>(age) / hz, 3.5);
            }
            return RuntimeVehicleBodyPose{};
        };
        if (bitePose(probe).active()) return fail("attachment shook before its first damage tick");
        int damageEvents = 0, joltEvents = 0;
        bool wasActive = false;
        for (int step = 0; step <= 2 * interval + hz; ++step) {
            probe.tick = attachedTick + step;
            const int healthBefore = findObjectById(probe, tankId)->health;
            const auto actions = probe.specialMechanisms.tickParasites(
                probe.world.getObjects(), probe.rules, probe.tick, SpecialEventSink{});
            const auto prepared = prepareRuntimeParasiteActions(probe, actions);
            if (!applyRuntimeParasiteActionsAtomically(probe, prepared, {}, droneId).accepted)
                return fail("authoritative damage batch was rejected");
            const int healthAfter = findObjectById(probe, tankId)->health;
            const bool damaged = healthAfter < healthBefore;
            const auto pose = bitePose(probe);
            if (damaged) {
                ++damageEvents;
                if (step % interval != 0 || !pose.active())
                    return fail("damage and jolt did not occur on the same tick");
            }
            if (pose.active() && !wasActive) {
                ++joltEvents;
                if (!damaged) return fail("vehicle started another jolt without losing health");
            }
            if (step % interval >= static_cast<int>(std::ceil(0.72 * hz)) && pose.active())
                return fail("vehicle did not rest between bites");
            wasActive = pose.active();
            if (step == hz / 3) {
                const auto hash = probe.specialMechanisms.stateHash();
                std::ostringstream saved;
                probe.specialMechanisms.save(saved);
                IniFile ini;
                RuntimeSpecialMechanismState restored;
                restored.reset(probe.rules.getSpecialMechanismRules());
                if (!ini.loadFromString(saved.str()) || !restored.load(ini, error)
                    || restored.stateHash() != hash) return fail("mid-jolt save did not round trip");
                probe.specialMechanisms = std::move(restored);
                const auto resumed = bitePose(probe);
                if (pose.pitch != resumed.pitch || pose.roll != resumed.roll || pose.height != resumed.height)
                    return fail("loading restarted the jolt or shifted the next bite");
            }
        }
        if (damageEvents != 3 || joltEvents != damageEvents)
            return fail("three stock bites did not produce exactly three jolts");
        std::cout << "Parasite bite timing hz=" << hz
            << " interval=4s damageEvents=" << damageEvents << " joltEvents=" << joltEvents
            << " same-tick/rest-between-bites/save-resume=ok\n";
    }
    return true;
}

bool runRuntimeVehicleBodySmokeProbe(RuntimeWindowState& live, std::string& error) {
    if (!runRuntimeVehicleBlastSmokeProbe(live, error)) return false;
    // Exercise actual host attachment/ejection and authoritative save/load first.
    if (!runRuntimeParasiteHostSmokeProbe(error)) return false;
    if (!runRuntimeVehicleParasiteBiteTimingProbe(live, error)) return false;
    RuntimeMapRenderContext context;
    context.rules=&live.rules; context.art=&live.art;
    context.assetRootPath=runtimeAssetRootPath(live.config);
    context.palettePath=runtimePalettePath(live.config,live.report.map);
    context.structurePalettePath=runtimeStructurePalettePath(live.config,live.report.map);
    if (!runRuntimeVehicleBodyRenderContractCheck(context,error)) return false;
    std::cout<<"Vehicle body dynamics: original Rocker/Weight, 4 VXL vehicle bodies, independent turret yaw, "
        "parasite damage-synced jolts/attachment/ejection/save, gravity, directional blast, settling, 30/60Hz and hidden hosts=ok\n";
    return true;
}
