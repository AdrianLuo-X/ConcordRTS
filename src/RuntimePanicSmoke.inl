bool runRuntimePanicSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& text) { error = "Panic: " + text; return false; };
    auto stock = std::make_unique<RuntimeWindowState>(); stock->config = config;
    stock->report = buildRuntimeStartupReport(config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original resources unavailable");
    const auto* civ = stock->rules.findUnitType("InfantryType", "CIV1");
    const auto* gi = stock->rules.findUnitType("InfantryType", "E1");
    if (!civ || !civ->fraidycat || !gi || gi->fraidycat) return fail("native Fraidycat/default");
    const auto rulesHash = runtimeDetailRulesHash(stock->rules);
    const_cast<UnitType*>(civ)->fraidycat = false;
    if (rulesHash == runtimeDetailRulesHash(stock->rules)) return fail("missing rule fingerprint");
    const_cast<UnitType*>(civ)->fraidycat = true;
    RuntimeMapRenderContext context;
    context.rules = &stock->rules; context.art = &stock->art;
    context.assetRootPath = runtimeAssetRootPath(config);
    context.palettePath = runtimePalettePath(config, stock->report.map);
    context.structurePalettePath = runtimeStructurePalettePath(config, stock->report.map);
    if (!runRuntimePanicRenderContractCheck(context, error)) return false;
    std::cout << "Panic: native Fraidycat, rules fingerprint and 48 civilian animation frames passed\n";

    IniFile rules;
    if (!rules.loadFromString(R"PANIC(
[InfantryTypes]
0=CIV
1=FEARLESS
2=SOLDIER
[VehicleTypes]
0=GUN
[CIV]
Strength=1000
Speed=4
MovementZone=Normal
SpeedType=Foot
Fraidycat=yes
[FEARLESS]
Strength=1000
Speed=4
MovementZone=Normal
SpeedType=Foot
Fraidycat=yes
Fearless=yes
[SOLDIER]
Strength=1000
Speed=4
MovementZone=Normal
SpeedType=Foot
[GUN]
Strength=1000
Speed=4
Primary=HIT
[HIT]
Damage=10
Range=30
ROF=30
Projectile=PLAIN
Warhead=WH
Speed=100
[PLAIN]
Inviso=yes
[WH]
Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%
)PANIC")) return fail("fixture rules");
    const auto world = [&](int hz, bool enclosed = false) {
        auto s = std::make_unique<RuntimeWindowState>();
        s->config.fixedTickHz = hz; s->config.computerAiEnabled = false;
        s->config.defaultMapPath = "panic-smoke.map";
        s->config.playerSlots.clear(); s->economy.owner = "Alpha";
        s->runtimeSoundsEnabled = false; s->runtimeSuppressPlayerAudioFeedback = true;
        s->runtimePreferencePersistenceEnabled = false;
        s->rules.loadFromIni(rules); s->rulesLoaded = true;
        for (int y = 0; y < 25; ++y) for (int x = 0; x < 25; ++x) {
            if (enclosed && (x != 12 || y != 12)) continue;
            // Missing columns make a sealed island with real map boundaries.
            if (!enclosed && (x == 3 || x == 21)) continue;
            TerrainCell cell; cell.x = x; cell.y = y; cell.tileIndex = 0; cell.cellIndex512 = y * 512 + x;
            s->world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance object; object.instanceId = 1; object.kind = WorldObjectKind::Infantry;
        object.type = "CIV"; object.owner = "Alpha"; object.x = object.y = 12;
        object.cell = runtimeCellKey(12,12); object.alive = true; object.health = 1000; object.mission = "Guard";
        s->world.accessObjects().push_back(object);
        initializeRuntimeNavigation(*s);
        return s;
    };
    const auto tick = [](RuntimeWindowState& s) { ++s.tick; (void)advanceRuntimeSimulation(s); };
    for (int hz : {15,30,60}) {
        auto s = world(hz);
        auto* object = findMutableObjectById(*s, 1);
        const auto normalSpeed = runtimeMovementSegmentIntervalQ16(*s, *object, {12,12}, {13,12});
        if (!beginRuntimePanic(*s, *object, false, 5,12)) return fail("damage panic start");
        if (runtimeMovementSegmentIntervalQ16(*s, *object, {12,12}, {13,12}) * 2 != normalSpeed)
            return fail("panic does not run at twice walking speed");
        advanceRuntimePanics(*s);
        const auto authority = s->combatDetails.deterministicHash();
        for (int n = 0; n < 5; ++n) advanceRuntimePanics(*s);
        if (s->combatDetails.deterministicHash() != authority) return fail("same-tick update consumed timer or random choices");
        int moved = 0;
        for (int n = 0; n < 5 * hz; ++n) {
            const auto before = runtimeCellPointForObject(*findObjectById(*s,1));
            tick(*s);
            object = findMutableObjectById(*s,1);
            const auto after = runtimeCellPointForObject(*object);
            moved += before != after;
            if (after.x <= 3 || after.x >= 21 || after.y < 0 || after.y >= 25
                || !s->terrainByCell.contains(runtimeCellKey(after.x,after.y))) return fail("panic crossed impassable cells");
            if (n + 1 < 5 * hz && !s->combatDetails.panics.contains(1)) return fail("panic ended early");
        }
        if (moved < 2 || s->combatDetails.panics.contains(1) || s->behaviors.contains(1)
            || object->renderMoveActive || object->mission == "Panic") return fail("finite panic did not move and settle: hz="
                + std::to_string(hz) + " moved=" + std::to_string(moved)
                + " panics=" + std::to_string(s->combatDetails.panics.size())
                + " behaviors=" + std::to_string(s->behaviors.size())
                + " render=" + std::to_string(object->renderMoveActive)
                + " mission=" + object->mission + " simulation=" + s->simulationStatus);
    }
    {
        auto s = world(30, true); auto* object = findMutableObjectById(*s,1);
        beginRuntimePanic(*s,*object,false);
        for (int n = 0; n < 151; ++n) tick(*s);
        if (object->x != 12 || object->y != 12 || object->renderMoveActive
            || !s->combatDetails.panics.empty() || !s->behaviors.empty()) return fail("enclosed unit teleported or failed to finish");
    }
    {
        auto s = world(30); RuntimeTeamMissionState mission; mission.objectIds = {1};
        if (issueRuntimeTeamScriptStep(*s,mission,{19,0}) != RuntimeTeamStepDisposition::Complete)
            return fail("script 19 did not allow continuation");
        for (int n = 0; n < 200; ++n) tick(*s);
        if (!s->combatDetails.panics.contains(1) || !s->combatDetails.panics.at(1).permanent)
            return fail("script panic expired");
        issueRuntimeTeamScriptStep(*s,mission,{41,0});
        if (!s->combatDetails.panics.empty() || s->behaviors.contains(1)) return fail("script 41 failed");
        for (bool move : {false,true}) {
            issueRuntimeTeamScriptStep(*s,mission,{19,0}); tick(*s);
            s->selectedObjectIds = {1};
            if (move) issueRuntimeMoveCommand(*s,{8,8}); else issueRuntimeStopCommand(*s);
            if (!s->combatDetails.panics.empty()) return fail("replacing player command did not cancel panic");
        }
        issueRuntimeStopCommand(*s);
        s->triggers.loaded = true;
        RuntimeScriptType script; script.id = "PANIC_SCRIPT";
        script.steps = {{19,0},{5,1},{41,0},{5,1}};
        s->triggers.scripts[script.id] = script;
        mission.scriptId = script.id; mission.stepIndex = 0; mission.stepIssued = false;
        s->triggers.teamMissions[1] = mission;
        advanceRuntimeTeamMissions(*s);
        if (s->triggers.teamMissions.at(1).stepIndex != 1 || !s->combatDetails.panics.contains(1))
            return fail("script did not advance from Panic to wait");
        for (int n = 0; n < 120 && s->triggers.teamMissions.at(1).stepIndex < 3; ++n) tick(*s);
        if (s->triggers.teamMissions.at(1).stepIndex != 3 || !s->combatDetails.panics.empty())
            return fail("script wait/Unpanic continuation");
    }
    {
        auto s = world(30);
        auto shooter = *findObjectById(*s,1); shooter.instanceId = 2; shooter.kind = WorldObjectKind::Unit;
        shooter.type = "GUN"; shooter.owner = "Beta"; shooter.x = 8; shooter.cell = runtimeCellKey(8,12);
        s->world.accessObjects().push_back(shooter); initializeRuntimeNavigation(*s);
        const auto hit = [&]() {
            const auto& source = *findObjectById(*s,2); const auto& target = *findObjectById(*s,1);
            const auto profile = runtimeWeaponProfileForObject(*s,source,&target);
            if (!profile.weapon) return false;
            spawnRuntimeProjectile(*s,source,target,*profile.weapon,profile,10);
            const auto projectile = s->projectiles.back(); s->projectiles.clear();
            return applyRuntimeProjectileImpact(*s,projectile);
        };
        if (!hit() || !s->combatDetails.panics.contains(1) || findObjectById(*s,1)->prone)
            return fail("actual damage did not trigger Fraidycat panic");
        tick(*s); s->tick += 50; advanceRuntimePanics(*s);
        if (!hit() || s->combatDetails.panics.at(1).ticksRemaining != 150)
            return fail("further damage did not refresh finite panic");
        eraseRuntimeBehavior(*s,1);
        auto* victim = findMutableObjectById(*s,1); victim->type = "FEARLESS";
        if (!hit() || s->combatDetails.panics.contains(1) || victim->prone)
            return fail("Fearless did not suppress automatic panic");
        victim->type = "SOLDIER";
        if (!hit() || s->combatDetails.panics.contains(1) || !victim->prone)
            return fail("ordinary infantry prone control changed");
    }
    std::cout << "Panic: actual movement, speed, impassable/enclosed ground, finite timer, damage refresh, Fearless, scripts and cancellation passed\n";

    {
        auto original = world(30);
        beginRuntimePanic(*original,*findMutableObjectById(*original,1),false,5,12);
        for (int n=0;n<9;++n) tick(*original);
        std::ostringstream saved;
        if (!writeRuntimeStateToStream(*original,saved,error)) return fail("finite full save: "+error);
        IniFile savedIni;
        if (!savedIni.loadFromString(saved.str())) return fail("finite full save INI");
        auto restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,*original,false);
        if (!materializeRuntimeLoadStateFromIni(*restored,savedIni,error)) return fail("finite full restore: "+error);
        initializeRuntimeNavigation(*restored);
        for (int n=0;n<150;++n) {
            const auto* a=findObjectById(*original,1);const auto* b=findObjectById(*restored,1);
            if (original->combatDetails.deterministicHash()!=restored->combatDetails.deterministicHash()
                || a->x!=b->x || a->y!=b->y || a->renderMoveActive!=b->renderMoveActive
                || (a->hasRenderPosition && (a->renderX!=b->renderX || a->renderY!=b->renderY)))
                return fail("finite save continuation diverged at "+std::to_string(n));
            tick(*original);tick(*restored);
        }
        if (restored->combatDetails.panics.contains(1)) return fail("finite panic persisted past restored deadline");
    }
    std::cout << "Panic: finite full save and deterministic movement through expiration passed\n";

    // The complete multiplayer save path includes normal Move segments as
    // well as the independent panic timer and deterministic destination cursor.
    auto s = std::make_unique<RuntimeWindowState>();
    if (!configureRuntimeMultiplayerSnapshotSmokeState(*s,false,false,false,0.0)) return fail("snapshot fixture");
    s->config.computerAiEnabled = false; s->runtimeSoundsEnabled = false;
    s->config.shortGame = false; // This movement fixture deliberately has no bases.
    for (int y = 0; y < 40; ++y) for (int x = 0; x < 40; ++x) {
        TerrainCell t; t.x=x; t.y=y; t.level=2; t.tileIndex=0; t.cellIndex512=y*512+x;
        s->world.accessTerrainCells().push_back(t);
    }
    WorldObjectInstance object; object.instanceId=101;object.type="SNAPSHOT_TANK";object.owner="SkirmishPlayer1";
    object.kind=WorldObjectKind::Unit;object.x=10;object.y=10;object.level=2;object.cell=runtimeCellKey(10,10);
    object.alive=true;object.health=200;object.mission="Guard";
    s->world.accessObjects().push_back(object);
    auto opponent=object;opponent.instanceId=102;opponent.owner="SkirmishPlayer2";
    opponent.x=30;opponent.cell=runtimeCellKey(30,10);s->world.accessObjects().push_back(opponent);
    s->playerCommands.alliances[{"skirmishplayer1","skirmishplayer2"}]=false;
    s->playerCommands.alliances[{"skirmishplayer2","skirmishplayer1"}]=false;
    initializeRuntimeNavigation(*s); completeRuntimeWorldRefresh(*s); updateRuntimeVisionState(*s);
    if (!beginRuntimePanic(*s,*findMutableObjectById(*s,101),true)) return fail("snapshot panic start");
    for (int n=0;n<120 && !findObjectById(*s,101)->renderMoveActive;++n) tick(*s);
    if (!s->behaviors.contains(101) || !findObjectById(*s,101)->renderMoveActive)
        return fail("snapshot was not taken during a moving panic segment: "
            + s->simulationStatus + " result=" + s->skirmishResult
            + " mission=" + findObjectById(*s,101)->mission);
    // The generic snapshot fixture initializes empty effect systems at tick
    // seven. This probe advances real movement, so align those empty clocks
    // with the snapshot boundary just as the live-flight snapshot probe does.
    s->particles.reset(s->config.fixedTickHz);s->particlesInitialized=true;
    s->superWeapons.reset(std::vector<SuperWeaponRule>{});s->superWeaponsInitialized=true;
    if (!s->particles.alignEmptyStateTick(s->tick) || !s->superWeapons.alignEmptyStateTick(s->tick))
        return fail("snapshot empty subsystem clock alignment");
    const auto hash=runtimeMultiplayerAuthoritativeStateHash(*s,error);
    if (!hash) return fail("snapshot source: "+error);
    ++s->combatDetails.panics.at(101).choiceSerial;
    if (hash==runtimeMultiplayerAuthoritativeStateHash(*s,error)) return fail("panic cursor absent from authority hash");
    --s->combatDetails.panics.at(101).choiceSerial;
    std::ostringstream serialized; s->combatDetails.save(serialized);
    const auto panicHash=s->combatDetails.deterministicHash();
    IniFile malformed;
    malformed.loadFromString(serialized.str()+"\n[CombatDetails]\nPanic0=101 0 1 0 0 0 0 -1 -1 41 42\n");
    if (s->combatDetails.load(malformed,error) || s->combatDetails.deterministicHash()!=panicHash)
        return fail("invalid panic load was not rejected atomically");
    RuntimeMultiplayerSnapshotIdentity identity; identity.proposalId=1;
    identity.sourceTimelineEpoch=s->config.multiplayerTimelineEpoch;
    identity.snapshotTick=identity.barrierTick=s->tick;identity.snapshotToken="panic";
    identity.authoritativeStateHash=hash;identity.gameplaySetupFingerprint=s->config.multiplayerSetupHash;
    RuntimeMultiplayerSnapshotMemoryImage image;RuntimeMultiplayerSnapshotStagedState staged;
    if (!exportRuntimeMultiplayerSnapshotV15(*s,identity,image,error)
        || !stageRuntimeMultiplayerSnapshot(*s,image.bytes,image.descriptor,staged,error)) return fail("complete snapshot: "+error);
    commitRuntimeMultiplayerSnapshotAuthority(*s,std::move(staged));
    if (runtimeMultiplayerAuthoritativeStateHash(*s,error)!=hash || s->combatDetails.deterministicHash()!=panicHash)
        return fail("snapshot commit changed panic authority");
    const auto before=runtimeCellPointForObject(*findObjectById(*s,101));
    for (int n=0;n<30;++n) tick(*s);
    if (!s->combatDetails.panics.contains(101)
        || runtimeCellPointForObject(*findObjectById(*s,101))==before) return fail("restored panic did not continue");
    RuntimeTeamMissionState mission;mission.objectIds={101};issueRuntimeTeamScriptStep(*s,mission,{41,0});
    if (s->combatDetails.panics.contains(101)) return fail("restored panic did not unpanic");
    std::cout << "Panic: passed; full moving snapshot/save, malformed atomic rejection, authoritative cursor and restored continuation verified\n";
    return true;
}
