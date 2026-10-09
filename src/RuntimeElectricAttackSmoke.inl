bool runRuntimeElectricAttackSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "electric attack: " + message;
        return false;
    };
    RuntimeMapRenderContext renderContext;
    renderContext.rules = &live.rules;
    renderContext.art = &live.art;
    renderContext.theater = &live.theater;
    renderContext.assetRootPath = runtimeAssetRootPath(live.config);
    renderContext.structureRootPath = runtimeStructureRootPath(live.config, live.report.map);
    renderContext.palettePath = runtimePalettePath(live.config, live.report.map);
    renderContext.structurePalettePath = runtimeStructurePalettePath(live.config, live.report.map);
    if (!runRuntimeElectricTargetRenderContractCheck(renderContext, error)) return false;
    struct AttackCase { const char* type; int rank; bool charged; };
    for (const auto scenario : {AttackCase{"TESLA", 0, false}, {"TESLA", 0, true},
            {"TTNK", 0, false}, {"TTNK", 2, false},
            {"SHK", 0, false}, {"SHK", 2, false}}) {
        const char* type = scenario.type;
        const std::string label = std::string(type) + (scenario.charged ? "-charged" : "")
            + (scenario.rank == 2 ? "-elite" : "");
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = live.config;
        probe.report = live.report;
        probe.rules = live.rules;
        probe.rulesLoaded = true;
        probe.art = live.art;
        probe.artLoaded = true;
        probe.theater = live.theater;
        probe.theaterLoaded = live.theaterLoaded;
        probe.world = live.world;
        probe.world.accessTerrainCells().clear();
        probe.world.accessTerrainObjects().clear();
        probe.world.accessSmudges().clear();
        probe.economy.owner = "Russians";
        probe.runtimeSuppressPlayerAudioFeedback = true;
        for (int y = 1; y <= 20; ++y) for (int x = 1; x <= 20; ++x) {
            TerrainCell cell;
            cell.x = x;
            cell.y = y;
            cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = 0;
            cell.subTileIndex = 0;
            cell.level = scenario.charged && x == 6 && y == 14 ? 5 : 3;
            probe.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance attacker;
        attacker.instanceId = 1;
        attacker.type = type;
        attacker.kind = std::string(type) == "TESLA" ? WorldObjectKind::Structure
            : std::string(type) == "SHK" ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        attacker.subCell = attacker.kind == WorldObjectKind::Infantry ? 3 : -1;
        attacker.owner = "Russians";
        attacker.health = 600;
        attacker.x = 10;
        attacker.y = 10;
        attacker.level = 3;
        attacker.cell = runtimeCellKey(10, 10);
        attacker.alive = true;
        attacker.facing = runtimeFacingFromDelta(-1, 1);
        attacker.turretFacing = attacker.facing;
        attacker.mission = "Attack";
        attacker.veterancy = scenario.rank;
        WorldObjectInstance target = attacker;
        target.instanceId = 2;
        target.type = "MTNK";
        target.kind = WorldObjectKind::Unit;
        target.owner = "Americans";
        target.x = 6;
        target.y = 14;
        target.cell = runtimeCellKey(6, 14);
        target.mission = "Guard";
        target.level = scenario.charged ? 5 : 3;
        probe.world.accessObjects() = {attacker, target};
        initializeRuntimeNavigation(probe);
        auto profile = runtimeWeaponProfileForObject(probe, attacker, &target);
        if (scenario.charged) {
            const auto* charged = probe.rules.findWeaponType("OPCoilBolt");
            if (charged == nullptr) return fail("stock charged coil weapon missing");
            finalizeRuntimeWeaponProfile(profile, probe, RuntimeWeaponSlot::Secondary,
                *charged, "charged coil smoke");
        }
        if (profile.weapon == nullptr || !profile.weapon->electricBolt) return fail("stock electric weapon missing");
        spawnRuntimeProjectile(probe, attacker, target, *profile.weapon, profile, 1);
        if (probe.projectiles.empty()) return fail("no electric projectile");
        const auto shot = probe.projectiles.front();
        const double muzzleRisePixels = (20.0 - shot.sourceX - shot.sourceY
            + shot.sourceZ - attacker.level) * 15.0;
        if (attacker.kind == WorldObjectKind::Structure) {
            const auto oppositeMuzzle = runtimeProjectileSourcePosition(probe, attacker, profile, 64);
            if (muzzleRisePixels < 45.0 || muzzleRisePixels > 50.0
                || std::abs(shot.sourceX - shot.sourceY) * 30.0 > 0.5
                || oppositeMuzzle.x != shot.sourceX || oppositeMuzzle.y != shot.sourceY)
                return fail("fixed tower muzzle missed its top or rotated toward the target");
        } else if (attacker.kind == WorldObjectKind::Unit
            && (muzzleRisePixels < 13.0 || muzzleRisePixels > 16.0)) {
            return fail("tank muzzle was below its rendered discharge coils");
        }
        const std::string expectedColor = scenario.charged ? "255,200,255" : "80,80,255";
        if (shot.electricColor != expectedColor || shot.alternateElectricColor != scenario.charged)
            return fail("electric weapon used another beam's palette");
        const std::string outputRoot = std::getenv("CONCORD_ELECTRIC_ATTACK_OUTPUT")
            ? std::getenv("CONCORD_ELECTRIC_ATTACK_OUTPUT") : "";
        if (!outputRoot.empty()) {
            const auto stats = drawRuntimeStateOffscreenAtCell(probe, {8, 12}, 480, 280, error,
                outputRoot + "/" + label + "-discharge.bmp", false);
            if (!stats) return false;
        }
        for (int tick = 0; tick < shot.totalTicks && !probe.projectiles.empty(); ++tick) {
            ++probe.tick;
            advanceRuntimeEffects(probe);
        }
        refreshRuntimeSpecialMechanismRenderSnapshots(probe);
        auto arcs = probe.specialMechanismRenderSnapshots;
        const auto arc = std::find_if(arcs.begin(), arcs.end(), [](const auto& snapshot) {
            return snapshot.kind == "electricArc";
        });
        if (arc == arcs.end()) return fail("no post-impact electric arc");
        if (!outputRoot.empty()) {
            const auto stats = drawRuntimeStateOffscreenAtCell(probe, {8, 12}, 480, 280, error,
                outputRoot + "/" + label + "-afterglow.bmp", false);
            if (!stats) return false;
        }
        std::cout << "Electric attack " << label << ": muzzleRisePixels=" << muzzleRisePixels
            << " shot=" << shot.sourceX << ',' << shot.sourceY
            << ',' << shot.sourceZ << " arc=" << arc->sourceX << ',' << arc->sourceY
            << ',' << arc->sourceLevel << " targetLevel=" << arc->targetLevel << '\n';
        if (!arc->hasBeamEndpoints || std::abs(arc->sourceLevel - shot.sourceZ) > 0.000001
            || std::abs(arc->targetLevel - shot.targetZ) > 0.000001
            || arc->sourceX != shot.sourceX || arc->sourceY != shot.sourceY
            || arc->targetX != shot.targetX || arc->targetY != shot.targetY
            || arc->color != expectedColor || arc->instanceId <= 0
            || arc->phase != (scenario.charged ? "Alternate" : "Normal"))
            return fail(std::string(type) + " afterglow discarded the discharge endpoint heights");
        std::ostringstream saved;
        probe.specialMechanisms.save(saved);
        IniFile savedIni;
        RuntimeSpecialMechanismState restored;
        if (!savedIni.loadFromString(saved.str()) || !restored.load(savedIni, error)
            || restored.stateHash() != probe.specialMechanisms.stateHash())
            return fail("electric afterglow did not survive a save/load round trip");
        const auto restoredArcs = restored.renderSnapshots(probe.tick, probe.config.fixedTickHz);
        const auto restoredArc = std::find_if(restoredArcs.begin(), restoredArcs.end(), [&](const auto& snapshot) {
            return snapshot.kind == "electricArc" && snapshot.instanceId == arc->instanceId;
        });
        // Existing special-mechanism saves use decimal display precision.
        // Permit subpixel rounding while rejecting a changed world endpoint.
        if (restoredArc == restoredArcs.end() || !restoredArc->hasBeamEndpoints
            || std::abs(restoredArc->sourceLevel - arc->sourceLevel) > 0.001
            || std::abs(restoredArc->targetLevel - arc->targetLevel) > 0.001
            || std::abs(restoredArc->sourceX - arc->sourceX) > 0.001
            || std::abs(restoredArc->sourceY - arc->sourceY) > 0.001
            || std::abs(restoredArc->targetX - arc->targetX) > 0.001
            || std::abs(restoredArc->targetY - arc->targetY) > 0.001
            || restoredArc->color != arc->color
            || restoredArc->phase != arc->phase || restoredArc->progress != arc->progress)
            return fail("restored electric arc changed its height, color, or animation phase");
    }
    return true;
}
