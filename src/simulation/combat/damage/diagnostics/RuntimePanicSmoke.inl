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
    {
        auto s = world(30);
        auto* object = findMutableObjectById(*s, 1);
        auto& type = *const_cast<UnitType*>(findRuntimeUnitType(s->rules, *object));
        type.veterancy.veteranAbilities = {VeteranAbility::Faster};
        type.veterancy.eliteAbilities = {VeteranAbility::Faster, VeteranAbility::Faster};
        const_cast<VeterancyRules&>(s->rules.getVeterancyRules()).veteranSpeed = 1.5;
        const double walking = runtimeMovementBaseIntervalTicks(*s, *object);
        const auto intervalMatches = [&](double expected) {
            return std::abs(runtimeMovementBaseIntervalTicks(*s, *object) - expected) < 1.0e-9;
        };
        object->prone = true;
        if (!intervalMatches(walking * 2.0)) return fail("prone does not halve walking speed");
        object->veterancy = 1;
        if (!intervalMatches(walking * 2.0 / 1.5)) return fail("prone lost veteran speed");
        if (!beginRuntimePanic(*s, *object, false) || object->prone
            || !intervalMatches(walking / 3.0)) return fail("panic lost veteran speed");
        object->veterancy = 2;
        if (!intervalMatches(walking / 3.0)) return fail("elite speed multiplied twice");
        object->prone = true;
        if (!intervalMatches(walking / 1.5)) return fail("posture and panic factors did not compose");
        advanceRuntimePanics(*s);
        if (object->prone || !intervalMatches(walking / 3.0)) return fail("panic did not restore running posture");

        auto& reaction = s->combatDetails.panics.at(1);
        reaction.choiceSerial = 9;
        const bool pending = reaction.pendingStart;
        const int chooseTick = reaction.nextChooseTick;
        if (!beginRuntimePanic(*s, *object, true)
            || !beginRuntimePanic(*s, *object, false)
            || !reaction.permanent || reaction.choiceSerial != 9
            || reaction.pendingStart != pending || reaction.nextChooseTick != chooseTick)
            return fail("refresh downgraded permanent panic or reset its walk state");
        object->kind = WorldObjectKind::Unit;object->type = "GUN";object->prone = true;object->veterancy = 0;
        if (!intervalMatches(walking / 2.0)) return fail("script vehicle panic or infantry-only prone scale changed");
    }
    {
        RuntimePanicState reaction;
        reaction.ticksRemaining = 150;reaction.lastTick = 10;
        if (advanceRuntimePanicClock(reaction, 10, false) != RuntimePanicClockStep::Active
            || reaction.ticksRemaining != 150) return fail("same-tick panic clock consumed time");
        if (advanceRuntimePanicClock(reaction, 80, true) != RuntimePanicClockStep::Suspended
            || reaction.ticksRemaining != 150 || reaction.lastTick != 80)
            return fail("suspended panic clock consumed time");
        if (advanceRuntimePanicClock(reaction, 229, false) != RuntimePanicClockStep::Active
            || reaction.ticksRemaining != 1
            || advanceRuntimePanicClock(reaction, 230, false) != RuntimePanicClockStep::Expired)
            return fail("panic clock expiration boundary");
        reaction.permanent = true;
        if (advanceRuntimePanicClock(reaction, std::numeric_limits<int>::max(), false)
            != RuntimePanicClockStep::Active) return fail("permanent panic clock expired");
    }
    std::cout << "Panic: posture/veteran/elite/vehicle speed composition, permanent refresh and clock boundaries passed\n";
    for (int hz : {15,30,60}) {
        auto s = world(hz);
        auto* object = findMutableObjectById(*s, 1);
        const double walkingInterval = runtimeMovementBaseIntervalTicks(*s, *object);
        const auto walkingQ16 = runtimeMovementSegmentIntervalQ16(*s, *object, {12,12}, {13,12});
        if (!beginRuntimePanic(*s, *object, false, 5,12)) return fail("damage panic start");
        const double panicInterval = runtimeMovementBaseIntervalTicks(*s, *object);
        const auto panicQ16 = runtimeMovementSegmentIntervalQ16(*s, *object, {12,12}, {13,12});
        // The speed contract precedes segment quantization. Rounding each Q16
        // interval separately can leave their doubled comparison one unit apart.
        if (!std::isfinite(walkingInterval) || !std::isfinite(panicInterval)
            || std::abs(2.0 * panicInterval - walkingInterval) > 1.0e-9
            || std::abs(2 * panicQ16 - walkingQ16) > 1)
            return fail("panic speed or Q16 quantization: hz=" + std::to_string(hz)
                + " walking=" + std::to_string(walkingInterval)
                + " panic=" + std::to_string(panicInterval)
                + " walkingQ16=" + std::to_string(walkingQ16)
                + " panicQ16=" + std::to_string(panicQ16));
        std::cout << "Panic: speed hz=" << hz << " walking=" << walkingInterval
            << " panic=" << panicInterval << " walkingQ16=" << walkingQ16
            << " panicQ16=" << panicQ16 << '\n';
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
    const auto advanceSnapshotPanic = [&](RuntimeWindowState& state) {
        RuntimeSnapshotActiveStateScope active(state);
        tick(state);
    };
    const auto snapshotHash = [&](const RuntimeWindowState& state) {
        RuntimeSnapshotActiveStateScope active(state);
        return runtimeMultiplayerAuthoritativeStateHash(state,error);
    };
    const auto makeSnapshotPanicState = [&]() -> std::unique_ptr<RuntimeWindowState> {
        const auto fixtureFailure = [&](const std::string& reason) -> std::unique_ptr<RuntimeWindowState> {
            error = reason;
            return {};
        };
        auto state = std::make_unique<RuntimeWindowState>();
        RuntimeSnapshotActiveStateScope fixtureScope(*state);
        if (!configureRuntimeMultiplayerSnapshotSmokeState(*state,false,false,false,0.0))
            return fixtureFailure("snapshot fixture");
        state->config.computerAiEnabled = false;
        state->runtimeSoundsEnabled = false;
        state->config.shortGame = false; // This movement fixture deliberately has no bases.
        for (int y = 0; y < 40; ++y) for (int x = 0; x < 40; ++x) {
            TerrainCell cell;
            cell.x=x;cell.y=y;cell.level=2;cell.tileIndex=0;cell.cellIndex512=y*512+x;
            state->world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance actor;
        actor.instanceId=101;actor.type="SNAPSHOT_TANK";actor.owner="SkirmishPlayer1";
        actor.kind=WorldObjectKind::Unit;actor.x=10;actor.y=10;actor.level=2;actor.cell=runtimeCellKey(10,10);
        actor.alive=true;actor.health=200;actor.mission="Guard";
        state->world.accessObjects().push_back(actor);
        auto opponent=actor;
        opponent.instanceId=102;opponent.owner="SkirmishPlayer2";
        opponent.x=30;opponent.cell=runtimeCellKey(30,10);
        state->world.accessObjects().push_back(opponent);
        state->playerCommands.alliances[{"skirmishplayer1","skirmishplayer2"}]=false;
        state->playerCommands.alliances[{"skirmishplayer2","skirmishplayer1"}]=false;
        initializeRuntimeNavigation(*state);
        completeRuntimeWorldRefresh(*state);
        updateRuntimeVisionState(*state);
        if (!beginRuntimePanic(*state,*findMutableObjectById(*state,101),true))
            return fixtureFailure("snapshot panic start");
        for (int n=0;n<120 && !findObjectById(*state,101)->renderMoveActive;++n) advanceSnapshotPanic(*state);
        if (!state->behaviors.contains(101) || !findObjectById(*state,101)->renderMoveActive)
            return fixtureFailure("snapshot was not taken during a moving panic segment: "
                + state->simulationStatus + " result=" + state->skirmishResult
                + " mission=" + findObjectById(*state,101)->mission);
        // Both independent fixtures align the same empty subsystem clocks at
        // the first active movement segment before either trajectory advances.
        state->particles.reset(state->config.fixedTickHz);state->particlesInitialized=true;
        state->superWeapons.reset(std::vector<SuperWeaponRule>{});state->superWeaponsInitialized=true;
        if (!state->particles.alignEmptyStateTick(state->tick)
            || !state->superWeapons.alignEmptyStateTick(state->tick))
            return fixtureFailure("snapshot empty subsystem clock alignment");
        return state;
    };
    auto referenceState = makeSnapshotPanicState();
    if (!referenceState) return fail(error);
    auto s = makeSnapshotPanicState();
    if (!s) return fail(error);
    RuntimeSnapshotActiveStateScope restoredScope(*s);
    const auto hash=snapshotHash(*s);
    if (!hash) return fail("snapshot source: "+error);
    const auto referenceHash=snapshotHash(*referenceState);
    if (referenceHash!=hash) return fail("independent panic fixtures differ at snapshot barrier: reference="
        + std::to_string(referenceHash) + " restored=" + std::to_string(hash));
    ++s->combatDetails.panics.at(101).choiceSerial;
    if (hash==snapshotHash(*s)) return fail("panic cursor absent from authority hash");
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
    // Commit at the unchanged snapshot barrier. The unrestored reference is
    // an independently initialized world, never a later state rolled backward.
    commitRuntimeMultiplayerSnapshotAuthority(*s,std::move(staged));
    if (snapshotHash(*s)!=hash || s->combatDetails.deterministicHash()!=panicHash)
        return fail("snapshot commit changed panic authority");

    struct PanicContinuationSample {
        int tick = 0;
        bool objectPresent = false, hasPanic = false, hasBehavior = false;
        bool hasRenderPosition = false, renderMoveActive = false;
        RuntimeCellPoint cell{}, from{}, to{};
        double renderX = 0.0, renderY = 0.0, renderLevel = 0.0;
        double startupRamp = 0.0, arrivalAfter = -1.0;
        int cooldown = 0, total = 0, remaining = 0;
        std::int32_t remainderQ16 = 0;
        std::int64_t startupDistanceQ16 = 0;
        std::uint64_t panicHash = 0, choiceSerial = 0;
    };
    const auto observe = [](const RuntimeWindowState& observed) {
        RuntimeSnapshotActiveStateScope active(observed);
        PanicContinuationSample sample;
        sample.tick = observed.tick;
        sample.panicHash = observed.combatDetails.deterministicHash();
        if (const auto* actor = findObjectById(observed,101)) {
            sample.objectPresent = true;
            sample.cell = runtimeCellPointForObject(*actor);
            sample.hasRenderPosition = actor->hasRenderPosition;
            sample.renderMoveActive = actor->renderMoveActive;
            sample.renderX = actor->renderX;
            sample.renderY = actor->renderY;
            sample.renderLevel = actor->renderLevel;
        }
        if (const auto reaction = observed.combatDetails.panics.find(101);
            reaction != observed.combatDetails.panics.end()) {
            sample.hasPanic = true;
            sample.choiceSerial = reaction->second.choiceSerial;
        }
        if (const auto move = observed.behaviors.find(101); move != observed.behaviors.end()) {
            sample.hasBehavior = true;
            sample.from = move->second.renderFrom;
            sample.to = move->second.renderTo;
            sample.cooldown = move->second.moveCooldownTicks;
            sample.total = move->second.renderMoveTicksTotal;
            sample.remaining = move->second.renderMoveTicksRemaining;
            sample.remainderQ16 = move->second.moveIntervalRemainderQ16;
            sample.startupDistanceQ16 = move->second.moveStartupDistanceQ16;
            sample.startupRamp = move->second.renderStartupAccelerationDistance;
            sample.arrivalAfter = move->second.renderArrivalDistanceAfter;
        }
        return sample;
    };
    const auto describe = [](const PanicContinuationSample& sample) {
        std::ostringstream text;
        text << std::setprecision(17) << "tick=" << sample.tick
            << " object/panic/behavior=" << sample.objectPresent << '/' << sample.hasPanic << '/' << sample.hasBehavior
            << " cell=" << sample.cell.x << ',' << sample.cell.y
            << " render=" << sample.hasRenderPosition << '/' << sample.renderMoveActive
            << ':' << sample.renderX << ',' << sample.renderY << ',' << sample.renderLevel
            << " segment=" << sample.from.x << ',' << sample.from.y << "->" << sample.to.x << ',' << sample.to.y
            << " cooldown/total/remaining=" << sample.cooldown << '/' << sample.total << '/' << sample.remaining
            << " remainder/startupQ16=" << sample.remainderQ16 << '/' << sample.startupDistanceQ16
            << " ramp/arrival=" << sample.startupRamp << '/' << sample.arrivalAfter
            << " panicHash/choice=" << sample.panicHash << '/' << sample.choiceSerial;
        return text.str();
    };
    const auto sameSample = [](const PanicContinuationSample& a, const PanicContinuationSample& b) {
        return a.tick == b.tick && a.objectPresent == b.objectPresent
            && a.hasPanic == b.hasPanic && a.hasBehavior == b.hasBehavior
            && a.cell == b.cell && a.from == b.from && a.to == b.to
            && a.hasRenderPosition == b.hasRenderPosition && a.renderMoveActive == b.renderMoveActive
            && a.renderX == b.renderX && a.renderY == b.renderY && a.renderLevel == b.renderLevel
            && a.cooldown == b.cooldown && a.total == b.total && a.remaining == b.remaining
            && a.remainderQ16 == b.remainderQ16 && a.startupDistanceQ16 == b.startupDistanceQ16
            && a.startupRamp == b.startupRamp && a.arrivalAfter == b.arrivalAfter
            && a.panicHash == b.panicHash && a.choiceSerial == b.choiceSerial;
    };
    const auto initial = observe(*referenceState);
    const int hz = std::clamp(s->config.fixedTickHz,1,1000);
    const int segmentRemaining = std::max(initial.cooldown,initial.remaining);
    if (!initial.objectPresent || !initial.hasPanic || !initial.hasBehavior
        || !initial.renderMoveActive || segmentRemaining <= 0 || segmentRemaining > 10 * hz)
        return fail("snapshot continuation has no bounded active segment: " + describe(initial));
    // The saved segment includes vehicle acceleration and arrival braking.
    // Give its remaining clock one additional simulation second for the next
    // route step to be committed; a fixed 30-tick cell test can end mid-segment.
    const int continuationTicks = segmentRemaining + hz;
    std::cout << "Panic: snapshot initial " << describe(initial)
        << " continuationTicks=" << continuationTicks << '\n';
    const auto progressedFromInitial = [&](const PanicContinuationSample& sample) {
        return sample.objectPresent && sample.hasRenderPosition
            && (sample.renderX != initial.renderX || sample.renderY != initial.renderY);
    };
    const auto restoredInitial = observe(*s);
    if (!sameSample(initial,restoredInitial))
        return fail("snapshot initial segment changed: expected={" + describe(initial)
            + "} actual={" + describe(restoredInitial) + "}");
    bool referenceProgress = false, referenceCellCommit = false;
    bool restoredProgress = false, restoredCellCommit = false;
    for (int n=0;n<continuationTicks;++n) {
        advanceSnapshotPanic(*referenceState);
        advanceSnapshotPanic(*s);
        const auto expected = observe(*referenceState);
        const auto sample = observe(*s);
        if (!expected.objectPresent || !expected.hasPanic)
            return fail("unrestored panic lost its actor or reaction: " + describe(expected));
        if (!sameSample(expected,sample))
            return fail("snapshot continuation diverged at step " + std::to_string(n+1)
                + ": expected={" + describe(expected) + "} actual={" + describe(sample)
                + "} referenceSimulation=" + referenceState->simulationStatus
                + " restoredSimulation=" + s->simulationStatus + " result=" + s->skirmishResult);
        referenceProgress |= progressedFromInitial(expected);
        referenceCellCommit |= expected.cell != initial.cell;
        restoredProgress |= progressedFromInitial(sample);
        restoredCellCommit |= sample.cell != initial.cell;
        if (n+1 == std::min(30,continuationTicks))
            std::cout << "Panic: snapshot replay intermediate " << describe(sample) << '\n';
    }
    if (!referenceProgress || !referenceCellCommit || !restoredProgress || !restoredCellCommit)
        return fail("panic continuation lacked progress or a subsequent cell commit: initial={"
            + describe(initial) + "} reference={" + describe(observe(*referenceState))
            + "} restored={" + describe(observe(*s)) + "}");
    std::cout << "Panic: snapshot continuation matched unrestored trajectory for "
        << continuationTicks << " ticks; continuous progress and subsequent cell commit passed\n";
    RuntimeTeamMissionState mission;mission.objectIds={101};issueRuntimeTeamScriptStep(*s,mission,{41,0});
    if (s->combatDetails.panics.contains(101)) return fail("restored panic did not unpanic");
    std::cout << "Panic: passed; full moving snapshot/save, malformed atomic rejection, authoritative cursor and restored continuation verified\n";
    return true;
}
