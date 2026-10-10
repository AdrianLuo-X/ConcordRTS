bool runRuntimeVehicleBlastSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "real missile blast: " + why; return false; };
    const char* output = std::getenv("CONCORD_VEHICLE_BODY_OUTPUT");
    const std::string root = output ? output : "";
    int volleys = 0, transferredFrames = 0;
    for (const char* type : {"V3", "DRED", "BSUB"}) for (int hz : {15, 30, 60}) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        s.config=live.config; s.report=live.report;
        s.rules=live.rules; s.rulesLoaded=true; s.art=live.art; s.artLoaded=true;
        s.theater=live.theater; s.theaterLoaded=live.theaterLoaded;
        s.config.fixedTickHz = hz; s.config.multiplayerActive = false; s.config.computerAiEnabled = false;
        IniFile rules;
        if (!rules.loadFromFile(live.report.rulesPath) || !s.rules.loadFromIni(rules)) return fail("stock rules");
        s.world = live.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts = {}; s.mapFactsLoaded = false; s.terrainByCell.clear(); s.terrainInfoByCell.clear();
        s.economy.owner = "Russians"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        const int level = std::string(type) == "BSUB" ? 2 : 0;
        for (int y = 1; y <= 28; ++y) for (int x = 1; x <= 32; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.level = level;
            cell.cellIndex512 = runtimeCellKey(x,y); cell.tileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance source; source.instanceId = 1; source.kind = WorldObjectKind::Unit;
        source.type = type; source.owner = "Russians"; source.alive = true;
        source.health = s.rules.findUnitType("Vehicle",type)->strength;
        source.x = 8; source.y = 10; source.level = level; source.cell = runtimeCellKey(8,10);
        source.mission = "Attack"; source.facing = source.turretFacing = runtimeFacingFromDelta(1,0);
        auto target = source; target.instanceId = 2; target.type = "CAEAST01";
        target.kind = WorldObjectKind::Structure; target.owner = "Americans";
        target.health = 3000; target.x = 20; target.cell = runtimeCellKey(20,10); target.mission = "Guard";
        s.world.accessObjects() = {source,target};
        const char* tanks[] = {"MTNK","HTNK","APOC","FV"};
        const int xs[] = {21,19,20,20}, ys[] = {10,10,11,9};
        std::vector<WorldObjectInstance> vehicles;
        for (int i = 0; i < 4; ++i) {
            auto tank = source; tank.instanceId = i+3; tank.type = tanks[i]; tank.owner = "Americans";
            tank.health = s.rules.findUnitType("Vehicle",tank.type)->strength;
            tank.x = xs[i]; tank.y = ys[i]; tank.cell = runtimeCellKey(tank.x,tank.y);
            tank.mission = "Guard"; tank.facing = 96; tank.turretFacing = 160;
            vehicles.push_back(tank); s.world.accessObjects().push_back(tank);
        }
        RuntimeSnapshotActiveStateScope active(s); initializeRuntimeNavigation(s);
        RuntimeObjectBehavior attack; attack.kind = RuntimeBehaviorKind::Attack;
        attack.state = RuntimeBehaviorState::Queued; attack.targetObjectId = 2; s.behaviors[1] = attack;
        advanceRuntimeSpecialMechanisms(s);
        const auto* weapon = s.rules.findWeaponType(std::string(type) == "V3" ? "V3Launcher"
            : std::string(type) == "DRED" ? "DredLauncher" : "CruiseLauncher");
        if (!weapon || !launchRuntimeSpawnedMissileVolley(s,*findObjectById(s,1),*findObjectById(s,2),*weapon,1.0))
            return fail(std::string(type)+" launch");
        std::vector<std::vector<RuntimeImpactRenderActor>> frames(1), restoredFrames(1);
        int firstImpact = -1, lastImpact = -1, impactCount = 0;
        std::set<int> seenImpacts;
        const std::string label = std::string(type)+"-"+std::to_string(hz);
        for (int step = 0; step < hz*90; ++step) {
            ++s.tick; (void)advanceRuntimeEffects(s);
            std::vector<RuntimeImpactRenderActor> current, restored;
            for (const auto& effect : s.impactEffects) {
                if (effect.renderIntent != "warheadImpact" || effect.damage <= 0
                    || effect.weaponId != weapon->id || effect.attackerObjectId != 1) continue;
                if (seenImpacts.insert(effect.projectileActorId).second) {
                    std::cout << "Live missile " << label << " actor=" << effect.projectileActorId
                        << " warhead=" << effect.warheadId << " damage=" << effect.damage
                        << " x=" << effect.worldX << " y=" << effect.worldY << " z=" << effect.worldLevel << '\n';
                    ++impactCount; lastImpact = s.tick;
                    if (firstImpact < 0) firstImpact = s.tick;
                }
                current.push_back(makeRuntimeImpactRenderActor(effect));
                RuntimeImpactEffect transferred;
                if (!parseRuntimeMultiplayerSnapshotImpactRecord(runtimeMultiplayerSnapshotImpactRecord(effect),transferred))
                    return fail(label+" snapshot impact decode");
                if (transferred.renderIntent != "impact" || transferred.damage != effect.damage
                    || transferred.remainingTicks != effect.remainingTicks || transferred.totalTicks != effect.totalTicks)
                    return fail(label+" saved blast age/strength");
                restored.push_back(makeRuntimeImpactRenderActor(transferred)); ++transferredFrames;
            }
            if (firstImpact >= 0) {
                frames.push_back(std::move(current)); restoredFrames.push_back(std::move(restored));
                const int age = s.tick-firstImpact;
                if (hz == 30 && !root.empty() && (age == 0 || age == 3 || age == 15 || age == 30)) {
                    const auto stats = drawRuntimeStateOffscreenAtCell(s,{20,10},800,600,error,
                        root+"/"+label+"-world-"+std::to_string(age)+".bmp",false);
                    if (!stats) return fail(label+" scene render: "+error);
                }
                if (s.specialMechanisms.spawnedMissiles().empty() && s.tick-lastImpact > hz*2) break;
            }
        }
        if (impactCount != (std::string(type)=="V3" ? 1 : 2) || !s.specialMechanisms.spawnedMissiles().empty()
            || findObjectById(s,2)->health >= 3000) return fail(label+" missing live volley impacts: count="
                +std::to_string(impactCount)+" active="+std::to_string(s.specialMechanisms.spawnedMissiles().size())
                +" targetHealth="+std::to_string(findObjectById(s,2)->health)+" tick="+std::to_string(s.tick));
        RuntimeMapRenderContext context; context.rules = &s.rules; context.art = &s.art;
        context.assetRootPath = runtimeAssetRootPath(s.config);
        context.palettePath = runtimePalettePath(s.config,s.report.map);
        context.structurePalettePath = runtimeStructurePalettePath(s.config,s.report.map);
        context.fixedTickHz = hz;
        if (!runRuntimeVehicleBlastRenderContractCheck(context,vehicles,frames,restoredFrames,label,error)) return false;
        ++volleys;
    }
    std::cout << "Real missile blasts: " << volleys << " V3/DRED/BSUB volleys, 36 adjacent vehicles, "
        << transferredFrames << " snapshot impact frames, 15/30/60 Hz and raised terrain passed\n";
    return true;
}
