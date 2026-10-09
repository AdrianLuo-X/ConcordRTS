#include "RuntimeFlakPresentationSmoke.inl"

bool runRuntimeTankGroundImpactSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& text) { error = "tank ground impact: " + text; return false; };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& probe = *storage;
    probe.config = live.config; probe.rules = live.rules; probe.rulesLoaded = true;
    probe.art = live.art; probe.artLoaded = true;
    probe.runtimeSuppressPlayerAudioFeedback = true;
    probe.runtimePreferencePersistenceEnabled = false;
    for (int y = 6; y <= 14; ++y) for (int x = 6; x <= 16; ++x) {
        TerrainCell cell;
        cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
        cell.tileIndex = cell.subTileIndex = 0;
        cell.level = y >= 11 ? 2 : 0;
        probe.world.accessTerrainCells().push_back(cell);
        probe.terrainInfoByCell.emplace(runtimeCellKey(x, y), RuntimeTerrainInfo{});
    }
    probe.terrainByCell = buildRuntimeTerrainLookup(probe.world);
    std::vector<RuntimeProjectileRenderActor> shots;
    std::vector<RuntimeProjectileRenderActor> flightFrames;
    std::vector<RuntimeImpactRenderActor> impacts;
    std::set<std::string> types;
    for (const auto& type : probe.rules.getVehicleTypes()) {
        if (toLowerAsciiLocal(type.category) != "afv" || type.naval || type.jumpJet || type.balloonHover) continue;
        for (int rank : {0, 2}) for (int targetY : {9, 12}) {
            const auto ground = runtimeGroundAttackTargetObject(probe, {12, targetY});
            if (!ground) return fail("ground target fixture missing");
            auto target = *ground;
            WorldObjectInstance source;
            source.instanceId = static_cast<int>(shots.size()) + 1;
            source.kind = WorldObjectKind::Unit; source.type = type.id; source.owner = "Russians";
            source.alive = true; source.health = std::max(1, type.strength); source.veterancy = rank;
            source.x = 9; source.y = targetY; source.level = target.level;
            source.cell = runtimeCellKey(source.x, source.y); source.mission = "Fire";
            source.facing = source.turretFacing = (source.instanceId * 32) % 256;
            const auto profile = runtimeWeaponProfileForObject(probe, source, &target);
            if (profile.weapon == nullptr || profile.projectile == nullptr
                || toLowerAsciiLocal(profile.projectile->image) != "120mm") continue;
            probe.projectiles.clear(); probe.impactEffects.clear();
            spawnRuntimeProjectile(probe, source, target, *profile.weapon, profile, profile.weapon->damage);
            if (probe.projectiles.empty()) return fail("cannon did not fire: " + type.id);
            auto shot = probe.projectiles.front();
            if (std::getenv("CONCORD_COMMAND_SMOKE_TANK_ARC_ONLY") != nullptr) {
                for (int tick=0; tick<=shot.totalTicks; ++tick) {
                    auto flight=shot; flight.elapsedTicks=tick; flight.remainingTicks=shot.totalTicks-tick;
                    updateRuntimeProjectileTravelPosition(flight);
                    flightFrames.push_back(makeRuntimeProjectileRenderActor(flight));
                }
            }
            shot.elapsedTicks = shot.totalTicks; shot.remainingTicks = 0;
            updateRuntimeProjectileTravelPosition(shot);
            const auto close = [](double a, double b) { return std::abs(a - b) < 1e-8; };
            if (!profile.projectile->inaccurate && !profile.projectile->flakScatter
                && (!close(shot.currentX, target.x) || !close(shot.currentY, target.y)
                    || !close(shot.currentZ, target.level))) return fail("accurate shell missed selected cell: " + type.id);
            probe.impactEffects.clear();
            if (!applyRuntimeProjectileImpact(probe, shot)) return fail("impact was not applied: " + type.id);
            const auto hit = std::find_if(probe.impactEffects.begin(), probe.impactEffects.end(),
                [&](const auto& effect) { return effect.projectileActorId == shot.actorId
                    && effect.renderIntent == "warheadImpact"; });
            if (hit == probe.impactEffects.end() || !hit->hasWorldPosition
                || !close(hit->worldX, shot.currentX) || !close(hit->worldY, shot.currentY)
                || !close(hit->worldLevel, shot.currentZ)) return fail("flash position differs from shell: " + type.id);
            auto actor = makeRuntimeImpactRenderActor(*hit);
            if (!actor.centerOnAnchor || actor.screenOffsetX != 0 || actor.screenOffsetY != 0)
                return fail("ground burst has an extra sprite offset: " + type.id);
            shots.push_back(makeRuntimeProjectileRenderActor(shot));
            impacts.push_back(std::move(actor));
            types.insert(type.id);
        }
    }
    if (types.size() < 5) return fail("too few stock cannon tanks covered");
    RuntimeMapRenderContext context;
    context.rules = &probe.rules; context.art = &probe.art;
    context.assetRootPath = runtimeAssetRootPath(live.config);
    context.palettePath = runtimePalettePath(live.config, live.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(live.config, live.report.map);
    context.fixedTickHz = probe.config.fixedTickHz;
    if (!runRuntimeGroundImpactRenderContractCheck(context, shots, impacts, error)) return false;
    if (!flightFrames.empty() && !runRuntimeTankArcRenderContractCheck(context, flightFrames, error)) return false;
    std::cout << "Tank ground impacts: stockTypes=" << types.size()
        << " normal/elite elevated-ground projectile/impact coordinates=aligned\n";
    return true;
}

bool runRuntimeProjectileMuzzleSmokeProbe(RuntimeWindowState& live, std::string& error) {
    if (!runRuntimeTankGroundImpactSmokeProbe(live, error)) return false;
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& probe = *storage;
    probe.config = live.config; probe.report = live.report;
    probe.rules = live.rules; probe.rulesLoaded = true;
    probe.art = live.art; probe.artLoaded = true;
    probe.theater = live.theater; probe.theaterLoaded = live.theaterLoaded;
    probe.runtimeSuppressPlayerAudioFeedback = true;
    std::vector<RuntimeProjectileRenderActor> actors;
    for (const std::string type : {"HTNK", "MTNK", "FV", "E1", "E2", "CLEG"}) {
        for (int facing = 0; facing < 256; facing += 32) {
            WorldObjectInstance source;
            source.instanceId = static_cast<int>(actors.size()) + 1;
            source.kind = type == "E1" || type == "E2" || type == "CLEG"
                ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
            source.type = type; source.owner = "Russians";
            source.alive = true; source.health = 200; source.mission = "Fire";
            source.facing = facing;
            // Aim independently of the hull to exercise the turret transform.
            source.turretFacing = (facing + 64) % 256;
            source.x = 10; source.y = 10; source.level = 3;
            source.cell = runtimeCellKey(10, 10);
            source.hasRenderPosition = true; source.renderMoveActive = true;
            source.renderX = 10.25; source.renderY = 10.5; source.renderLevel = 3.0;
            auto target = source;
            target.instanceId = 1000; target.type = "HTNK";
            target.kind = WorldObjectKind::Unit; target.owner = "Americans";
            target.x = 16; target.y = 10; target.hasRenderPosition = false;
            target.cell = runtimeCellKey(target.x, target.y);
            const auto profile = runtimeWeaponProfileForObject(probe, source, &target);
            if (profile.weapon == nullptr) { error = type + " has no test weapon"; return false; }
            probe.projectiles.clear(); probe.impactEffects.clear();
            spawnRuntimeProjectile(probe, source, target, *profile.weapon, profile, 1);
            if (probe.projectiles.empty()) { error = type + " did not fire"; return false; }
            const auto& shot = probe.projectiles.front();
            if (shot.currentX != shot.sourceX || shot.currentY != shot.sourceY
                || shot.currentZ != shot.sourceZ || (source.kind == WorldObjectKind::Infantry
                    && shot.sourceScreenOffsetY >= 0)) {
                error = type + " did not capture its rendered muzzle on discharge";
                return false;
            }
            for (const auto& muzzle : probe.impactEffects) {
                if (muzzle.lifecycleState == "Muzzle" && (muzzle.worldX != shot.sourceX
                    || muzzle.worldY != shot.sourceY || muzzle.worldLevel != shot.sourceZ
                    || muzzle.screenOffsetY != shot.sourceScreenOffsetY)) {
                    error = type + " flash and projectile origins differ";
                    return false;
                }
            }
            probe.world.accessObjects().push_back(source);
            auto actor = makeRuntimeProjectileRenderActor(shot);
            actor.trailerCadenceManaged = true;
            actors.push_back(actor);
        }
    }
    RuntimeMapRenderContext context;
    context.theater = probe.theaterLoaded ? &probe.theater : nullptr;
    context.art = &probe.art; context.rules = &probe.rules;
    context.assetRootPath = runtimeAssetRootPath(probe.config);
    context.tileRootPath = runtimeTileRootPath(probe.config, probe.report.map);
    context.palettePath = runtimePalettePath(probe.config, probe.report.map);
    context.structureRootPath = runtimeStructureRootPath(probe.config, probe.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(probe.config, probe.report.map);
    context.fixedTickHz = probe.config.fixedTickHz;
    if (!runRuntimeProjectileOriginRenderContractCheck(context, probe.world, actors, error)) return false;
    std::cout << "Projectile muzzle: tanks+IFV+infantry=48 poses flash=aligned SHP=centered "
        "VXL=aligned missile-owner-colors=ok\n";
    return true;
}
