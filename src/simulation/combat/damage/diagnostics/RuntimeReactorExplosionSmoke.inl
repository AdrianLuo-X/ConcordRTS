bool runRuntimeReactorExplosionSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    std::string scene;
    const auto fail = [&](const std::string& why) {
        error = "Reactor immediate explosion: " + scene + ": " + why; return false;
    };
    const char* output = std::getenv("CONCORD_REACTOR_EXPLOSION_OUTPUT");
    const std::string root = output ? output : "";
    IniFile stockIni; RulesDatabase stockRules;
    if (!stockIni.loadFromFile(live.report.rulesPath) || !stockRules.loadFromIni(stockIni))
        return fail("stock rules unavailable");
    const auto* reactorRule = stockRules.findBuildingType("NANRCT");
    const auto* payloadRule = stockRules.findWeaponType("NukePayload");
    const auto* nukeRule = stockRules.findWarheadType("NUKE");
    if (!reactorRule || !payloadRule || !nukeRule || reactorRule->deathWeapon != "NukePayload"
        || reactorRule->deathWeaponDamageModifier != 0.5 || payloadRule->damage != 600
        || payloadRule->radLevel != 500 || nukeRule->cellSpread != 10
        || nukeRule->percentAtMax != 0.02 || nukeRule->animList != "NUKEANIM")
        return fail("retail blast declaration changed");
    int cases = 0, saves = 0, captures = 0;
    const auto health = [](const RuntimeWindowState& s, int id) {
        const auto* o = findRuntimeObjectByIdIncludingInactive(s, id);
        return o ? o->health : -1;
    };
    const auto fixture = [&](int hz) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        seedRuntimeLoadScratch(s, live, false);
        s.rules = stockRules; s.rulesLoaded = true; s.tick = 100;
        s.config.fixedTickHz = hz; s.config.multiplayerActive = false;
        s.config.computerAiEnabled = false; s.config.gameMode = "Battle"; s.campaignMode = false;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false; s.visionEnabled = false;
        s.economy.owner = "Russians"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true;
        s.skirmishModeRulesLoaded = true; s.skirmishModeAlliesAllowed = true;
        s.skirmishTeamByOwner = {{"russians", "1"}, {"confederation", "1"}, {"americans", "2"}};
        s.behaviors.clear(); s.projectiles.clear(); s.recentExpiredProjectiles.clear();
        s.impactEffects.clear(); s.deathWeapons.reset();
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear(); s.world.accessTerrainCells().clear();
        s.terrainInfoByCell.clear(); s.terrainByCell.clear(); s.mapFacts = {}; s.mapFactsLoaded = false;
        for (int y = 44; y <= 94; ++y) for (int x = 44; x <= 94; ++x) {
            TerrainCell c; c.x = x; c.y = y; c.cellIndex512 = runtimeCellKey(x, y);
            c.tileIndex = 0; c.subTileIndex = 0; s.world.accessTerrainCells().push_back(c);
        }
        initializeRuntimeNavigation(s);
        return box;
    };
    const auto add = [&](RuntimeWindowState& s, const char* type, RuntimeBuildKind kind,
                         RuntimeCellPoint cell, const char* owner) {
        auto* o = addRuntimeObjectInstance(s, kind, type, cell);
        if (!o) return -1;
        o->owner = owner; o->mission = "Guard";
        o->buildAnimTicksRemaining = o->buildAnimTicksTotal = 0;
        return o->instanceId;
    };
    const auto cloneSave = [&](RuntimeWindowState& s) -> std::unique_ptr<RuntimeWindowState> {
        std::ostringstream stream;
        if (!writeRuntimeStateToStream(s, stream, error)) return nullptr;
        IniFile ini; if (!ini.loadFromString(stream.str())) return nullptr;
        auto loaded = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*loaded, s, false);
        if (!materializeRuntimeLoadStateFromIni(*loaded, ini, error)) return nullptr;
        ++saves; return loaded;
    };
    const auto blastCount = [](const RuntimeWindowState& s) {
        return std::count_if(s.impactEffects.begin(), s.impactEffects.end(), [](const auto& fx) {
            // Destroyed buildings can reuse the incoming animation for a
            // cosmetic death effect. Count authoritative payload impacts.
            return fx.animId == "NUKEANIM" && !fx.death && fx.weaponId == "NukePayload";
        });
    };
    const auto noMissile = [](RuntimeWindowState& s) {
        if (std::any_of(s.projectiles.begin(), s.projectiles.end(), [](const auto& p) {
                return runtimeProjectileIsNuclearDeathDetonation(p);
            })) return false;
        const auto* actors = runtimeProjectileRenderActorsForRender(s);
        if (actors && std::any_of(actors->begin(), actors->end(), [](const auto& a) {
                return toLowerAsciiLocal(a.projectileImageId) == "nkmsldn";
            })) return false;
        return std::none_of(s.impactEffects.begin(), s.impactEffects.end(), [](const auto& fx) {
            return fx.animId == "NUKEBALL" || fx.renderIntent == "missileExhaust";
        });
    };
    const auto kill = [&](RuntimeWindowState& s, int reactor, int mode) {
        if (mode == 0) {
            // Enter through an actual lethal weapon impact, including the
            // ordinary building destruction path and its exit queue.
            RuntimeProjectileEffect hit;
            hit.actorId = nextRuntimeProjectileActorId(s);
            hit.targetObjectId = reactor; hit.owner = "Americans";
            hit.toCell = {66, 66}; hit.currentCell = hit.toCell;
            hit.damage = 20000; hit.warheadId = "AP"; hit.impactAnimId = "PIFF";
            hit.ag = true; hit.affectsEnemies = hit.affectsAllies = true;
            return applyRuntimeProjectileImpact(s, hit);
        }
        RuntimeObjectExitRequest exit;
        exit.objectId = reactor; exit.tick = s.tick; exit.suppressCrew = true;
        exit.kind = mode == 2 ? RuntimeObjectExitKind::Sold
            : mode == 4 ? RuntimeObjectExitKind::TemporalDestroyed : RuntimeObjectExitKind::Destroyed;
        exit.suppressDeathWeapon = mode == 3;
        exit.reason = "reactor immediate explosion regression";
        if (queueRuntimeObjectExit(s, exit) != RuntimeObjectExitQueueResult::Queued) return false;
        return drainRuntimeObjectExitQueue(s);
    };
    for (int hz : {15, 30, 60}) for (int mode = 0; mode != 5; ++mode) {
        scene = "hz=" + std::to_string(hz) + " mode=" + std::to_string(mode);
        auto box = fixture(hz); auto& s = *box;
        const int reactor = add(s, "NANRCT", RuntimeBuildKind::Building, {64, 64}, "Russians");
        std::vector<int> tanks, buildings;
        for (const char* owner : {"Russians", "Confederation", "Americans"}) {
            // Coincident, identical victims isolate ownership from range and armor.
            tanks.push_back(add(s, "MTNK", RuntimeBuildKind::Unit, {70, 66}, owner));
            buildings.push_back(add(s, "GAPOWR", RuntimeBuildKind::Building, {72, 69}, owner));
        }
        const int outside = add(s, "MTNK", RuntimeBuildKind::Unit, {80, 66}, "Confederation");
        if (reactor < 0 || outside < 0 || std::find(tanks.begin(), tanks.end(), -1) != tanks.end()
            || std::find(buildings.begin(), buildings.end(), -1) != buildings.end())
            return fail("stock scene objects unavailable");
        if (!runtimeOwnersAreAllied(s, "Russians", "Confederation")
            || runtimeOwnersAreAllied(s, "Russians", "Americans")) return fail("alliance fixture");
        const int tankBefore = health(s, tanks.front()), buildingBefore = health(s, buildings.front());
        std::unique_ptr<RuntimeWindowState> beforeSave;
        if (mode < 2) { beforeSave = cloneSave(s); if (!beforeSave) return fail(error); }
        const int deathTick = s.tick;
        if (!kill(s, reactor, mode)) return fail("death entry rejected");
        const bool explodes = mode < 2;
        if (s.tick != deathTick || !s.deathWeapons.hasTriggered(reactor)
            || findRuntimeObjectByIdIncludingInactive(s, reactor)->alive || !noMissile(s))
            return fail("death did not resolve atomically without a missile");
        if (blastCount(s) != (explodes ? 1 : 0)
            || s.specialMechanisms.radiationAreas().size() != (explodes ? 1U : 0U))
            return fail("same-tick NUKEANIM/radiation missing or suppressed death exploded");
        int nuclearTicks = 0, nuclearActor = -1, nuclearLastFrame = -1;
        if (explodes) {
            const auto blast = std::find_if(s.impactEffects.begin(), s.impactEffects.end(), [&](const auto& fx) {
                return !fx.death && fx.animId == "NUKEANIM" && fx.attackerObjectId == reactor;
            });
            if (blast == s.impactEffects.end() || blast->animationFrameCount <= 1
                || blast->animationFrameDelayTicks != hz * 2 / 15
                || blast->animationSequenceSteps != blast->animationFrameCount
                || blast->totalTicks != blast->animationFrameCount * blast->animationFrameDelayTicks)
                return fail("NUKEANIM was truncated or did not use the retail Rate=400 clock");
            nuclearTicks = blast->totalTicks; nuclearActor = blast->projectileActorId;
            nuclearLastFrame = blast->animationFrameCount - 1;
        }
        // Stock NUKE: 600 * 0.5, heavy armor 100%, distance 4 / radius 10,
        // PercentAtMax .02 => round(300 * .608) = 182. No radiation tick yet.
        const int expectedTank = tankBefore - (explodes ? 182 : 0);
        for (int id : tanks) if (health(s, id) != expectedTank)
            return fail("same-tick tank damage differs: id=" + std::to_string(id)
                + " health=" + std::to_string(health(s, id)) + " expected=" + std::to_string(expectedTank));
        for (int id : buildings) if (health(s, id) != health(s, buildings.front())
                || (explodes ? health(s, id) >= buildingBefore : health(s, id) != buildingBefore))
            return fail("building blast filtered by alliance");
        if (health(s, outside) != tankBefore) return fail("blast escaped radius");
        const auto sequence = s.deathWeapons.nextRequestId();
        RuntimeObjectExitRequest repeat; repeat.objectId = reactor; repeat.tick = s.tick;
        if (queueRuntimeObjectExit(s, repeat) != RuntimeObjectExitQueueResult::AlreadyFinalized
            || s.deathWeapons.nextRequestId() != sequence || health(s, tanks.front()) != expectedTank)
            return fail("duplicate death repeated the explosion");
        if (beforeSave) {
            if (!kill(*beforeSave, reactor, mode) || blastCount(*beforeSave) != 1 || !noMissile(*beforeSave))
                return fail("death after pre-blast load did not detonate immediately");
            for (int id : tanks) if (health(*beforeSave, id) != health(s, id))
                return fail("pre-blast load changed friendly damage");
            auto afterSave = cloneSave(s);
            if (!afterSave || !noMissile(*afterSave)
                || afterSave->specialMechanisms.radiationAreas().size() != 1)
                return fail("post-blast load lost radiation or resurrected missile: " + error);
            if (queueRuntimeObjectExit(*afterSave, repeat) != RuntimeObjectExitQueueResult::AlreadyFinalized)
                return fail("post-blast load forgot once-only death");
            for (int id : tanks) if (health(*afterSave, id) != health(s, id))
                return fail("post-blast load repeated damage");
        }
        if (hz == 30 && mode == 0 && !root.empty()) {
            auto stats = drawRuntimeStateOffscreenAtCell(s, {66, 66}, 960, 640, error,
                root + "/reactor-same-tick.bmp", false, false);
            if (!stats || !stats->rendered || stats->impactActorDrawn == 0 || stats->missileExhaustDrawn != 0)
                return fail("same-tick original explosion did not render: " + error);
            ++captures;
        }
        std::set<int> initialBlastActors;
        for (const auto& fx : s.impactEffects) if (fx.animId == "NUKEANIM")
            initialBlastActors.insert(fx.projectileActorId);
        // No delayed second nuclear detonation may appear after the first.
        for (int step = 0; step < std::max(hz * 3, nuclearTicks + 1); ++step) {
            ++s.tick; advanceRuntimeEffects(s);
            if (!noMissile(s) || s.deathWeapons.nextRequestId() < sequence)
                return fail("missile/windup appeared after death");
            for (const auto& fx : s.impactEffects) if (fx.animId == "NUKEANIM"
                    && !initialBlastActors.contains(fx.projectileActorId))
                return fail("delayed second nuclear explosion");
            if (explodes && step + 1 == nuclearTicks - 1) {
                const auto blast = std::find_if(s.impactEffects.begin(), s.impactEffects.end(), [&](const auto& fx) {
                    return !fx.death && fx.animId == "NUKEANIM" && fx.projectileActorId == nuclearActor;
                });
                if (blast == s.impactEffects.end()
                    || runtimeImpactAnimationFrameForStep(*blast,
                        (blast->totalTicks - blast->remainingTicks) / blast->animationFrameDelayTicks) != nuclearLastFrame)
                    return fail("the final original nuclear animation frame never played");
            }
            if (explodes && step + 1 >= nuclearTicks && std::any_of(s.impactEffects.begin(), s.impactEffects.end(), [&](const auto& fx) {
                    return !fx.death && fx.animId == "NUKEANIM" && fx.projectileActorId == nuclearActor;
                })) return fail("nuclear animation exceeded its authored sequence");
            if (hz == 30 && mode == 0 && step == hz / 2 && !root.empty()) {
                auto stats = drawRuntimeStateOffscreenAtCell(s, {66, 66}, 960, 640, error,
                    root + "/reactor-mushroom-cloud.bmp", false, false);
                if (!stats || !stats->rendered || stats->impactActorDrawn == 0 || stats->missileExhaustDrawn != 0)
                    return fail("original mushroom cloud did not render: " + error);
                ++captures;
            }
        }
        std::cout << "Reactor immediate: " << scene << " blastTick=" << (explodes ? deathTick : -1)
            << " self/ally/enemyInitialDamage=" << (tankBefore - expectedTank)
            << " nuclearAnimationTicks=" << nuclearTicks
            << " buildings=" << (explodes ? "all-damaged" : "unaffected") << " outside/duplicate/save PASS\n";
        ++cases;
    }
    for (int hz : {15, 30, 60}) {
        scene = "same-tick cascade hz=" + std::to_string(hz);
        auto box = fixture(hz); auto& s = *box;
        const int first = add(s, "NANRCT", RuntimeBuildKind::Building, {64, 64}, "Russians");
        const int second = add(s, "NANRCT", RuntimeBuildKind::Building, {70, 64}, "Russians");
        if (first < 0 || second < 0) return fail("cascade reactor missing");
        findMutableObjectById(s, second)->health = 1;
        const int tick = s.tick;
        if (!kill(s, first, 1) || s.tick != tick || health(s, second) != 0
            || !s.deathWeapons.hasTriggered(second) || blastCount(s) != 2 || !noMissile(s)
            || !s.pendingObjectExits.empty()) return fail("friendly cascade did not drain in one tick: tick="
                + std::to_string(s.tick) + " health=" + std::to_string(health(s, second))
                + " finalized=" + std::to_string(s.deathWeapons.hasTriggered(second))
                + " payloadImpacts=" + std::to_string(blastCount(s))
                + " noMissile=" + std::to_string(noMissile(s))
                + " pending=" + std::to_string(s.pendingObjectExits.size()));
        for (const auto& death : s.deathWeapons.triggeredObjects())
            if ((death.objectId == first || death.objectId == second) && death.deathTick != tick)
                return fail("cascade explosion was delayed");
        auto loaded = cloneSave(s);
        if (!loaded || health(*loaded, second) != 0 || !loaded->deathWeapons.hasTriggered(second)
            || !noMissile(*loaded)) return fail("cascade save did not preserve finalized deaths: " + error);
        std::cout << "Reactor cascade: " << hz << " Hz two same-owner reactors, same tick, save PASS\n";
        ++cases;
    }
    std::cout << "Reactor immediate explosion: passed cases=" << cases << " saves=" << saves
        << " captures=" << captures << " instant NUKEANIM/300-base-damage/radiation; self/ally/enemy,"
        " range, sold/suppressed/temporal, duplicate and cascade PASS\n";
    return true;
}
