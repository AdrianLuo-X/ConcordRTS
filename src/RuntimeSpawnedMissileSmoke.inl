bool runRuntimeSpawnedMissileSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "spawned missiles: " + message;
        return false;
    };
    struct Scenario { const char* defense; int rank; bool sinkParent; };
    for (const auto scenario : {Scenario{"", 0, false}, {"", 2, false},
            {"", 0, true}, {"NASAM", 0, false}, {"NAFLAK", 0, false}, {"YAGGUN", 0, false}}) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = live.config; probe.report = live.report;
        probe.rules = live.rules; probe.rulesLoaded = true;
        probe.art = live.art; probe.artLoaded = true;
        probe.theater = live.theater; probe.theaterLoaded = live.theaterLoaded;
        probe.world = live.world;
        probe.world.accessTerrainCells().clear();
        probe.world.accessTerrainObjects().clear(); probe.world.accessSmudges().clear();
        probe.economy.owner = "Americans"; probe.economy.skirmishStartApplied = true;
        probe.runtimeSuppressPlayerAudioFeedback = true;
        for (int y = 1; y <= 28; ++y) for (int x = 1; x <= 32; ++x) {
            TerrainCell cell;
            cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x,y);
            cell.tileIndex = 0; cell.subTileIndex = 0;
            probe.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance ship;
        ship.instanceId = 1; ship.type = "DRED"; ship.owner = "Russians";
        ship.kind = WorldObjectKind::Unit; ship.health = 800; ship.alive = true;
        ship.x = 8; ship.y = 10; ship.cell = runtimeCellKey(8,10);
        ship.facing = ship.turretFacing = runtimeFacingFromDelta(1,0);
        ship.mission = "Attack"; ship.veterancy = scenario.rank;
        WorldObjectInstance target = ship;
        target.instanceId = 2; target.type = "CAEAST01"; target.owner = "Americans";
        target.kind = WorldObjectKind::Structure;
        constexpr int targetHealth = 3000; // Native Strength; do not exceed the health cap.
        target.health = targetHealth; target.x = 20; target.cell = runtimeCellKey(20,10);
        target.mission = "Guard"; target.veterancy = 0;
        probe.world.accessObjects() = {ship, target};
        const bool hasDefense = scenario.defense[0] != '\0';
        if (hasDefense) {
            WorldObjectInstance defense = target;
            defense.instanceId = 3; defense.kind = WorldObjectKind::Structure;
            defense.type = scenario.defense; defense.x = 13; defense.y = 12;
            defense.cell = runtimeCellKey(13,12); defense.poweredOperational = true;
            probe.world.accessObjects().push_back(defense);
        }
        initializeRuntimeNavigation(probe);
        RuntimeObjectBehavior attack;
        attack.kind = RuntimeBehaviorKind::Attack; attack.state = RuntimeBehaviorState::Queued;
        attack.targetObjectId = 2;
        probe.behaviors[1] = attack;
        advanceRuntimeSpecialMechanisms(probe);
        if (probe.specialMechanisms.carrierChildren().size() != 2
            || !probe.specialMechanisms.spawnedMissiles().empty())
            return fail("carrier tick launched missiles without the weapon fire boundary");
        for (const auto& [id, child] : probe.specialMechanisms.carrierChildren()) {
            const auto* entity = findObjectById(probe, id);
            if (child.phase != CarrierChildPhase::Docked || entity == nullptr
                || entity->health != 50 || !entity->authoredAircraft)
                return fail("docked DMISL was not a real 50-health aircraft object");
        }
        const bool capture = !hasDefense && scenario.rank == 0 && !scenario.sinkParent;
        if (capture) {
            auto lifecycle = std::make_unique<RuntimeWindowState>();
            lifecycle->config = probe.config; lifecycle->rules = probe.rules; lifecycle->rulesLoaded = true;
            lifecycle->art = probe.art; lifecycle->artLoaded = true;
            lifecycle->world = probe.world; lifecycle->economy.owner = probe.economy.owner;
            lifecycle->specialMechanisms = probe.specialMechanisms;
            lifecycle->specialMechanismsInitialized = true;
            lifecycle->behaviors[1] = attack;
            const auto* launcher = lifecycle->rules.findWeaponType("DredLauncher");
            if (!launcher || !launchRuntimeSpawnedMissileVolley(*lifecycle,
                    *findObjectById(*lifecycle, 1), *findObjectById(*lifecycle, 2), *launcher, 1.0))
                return fail("lifecycle fixture could not start preparing");
            eraseRuntimeBehavior(*lifecycle, 1);
            ++lifecycle->tick;
            advanceRuntimeSpecialMechanisms(*lifecycle);
            if (!lifecycle->specialMechanisms.spawnedMissiles().empty()
                || lifecycle->specialMechanisms.carrierChildren().size() != 2)
                return fail("stopping during preparation spent or fired the missiles");
            const int oldChild = lifecycle->specialMechanisms.carrierChildren().begin()->first;
            lifecycle->specialMechanisms.notifyCarrierChildFired(oldChild, lifecycle->tick, nullptr);
            ++lifecycle->tick;
            advanceRuntimeSpecialMechanisms(*lifecycle);
            if (!lifecycle->specialMechanisms.isCarrierChildDocked(oldChild))
                return fail("legacy missile aircraft mission did not recover its deck slot");
            lifecycle->behaviors[1] = attack;
            if (!launchRuntimeSpawnedMissileVolley(*lifecycle, *findObjectById(*lifecycle, 1),
                    *findObjectById(*lifecycle, 2), *launcher, 1.0))
                return fail("cancelled volley did not become available again");
            RuntimeObjectExitRequest exit;
            exit.objectId = 1; exit.kind = RuntimeObjectExitKind::Destroyed;
            exit.tick = lifecycle->tick; exit.mission = "Destroyed";
            exit.reason = "preparing missile cleanup smoke";
            queueRuntimeObjectExit(*lifecycle, std::move(exit));
            drainRuntimeObjectExitQueue(*lifecycle);
            if (!lifecycle->specialMechanisms.spawnedMissiles().empty()
                || !lifecycle->specialMechanisms.carrierChildren().empty())
                return fail("unreleased missiles survived their launcher's destruction");
        }
        const std::string output = capture && std::getenv("CONCORD_MISSILE_OUTPUT")
            ? std::getenv("CONCORD_MISSILE_OUTPUT") : "";
        const auto screenshot = [&](const std::string& label) {
            if (output.empty()) return true;
            refreshRuntimeSpecialMechanismRenderSnapshots(probe);
            const auto result = drawRuntimeStateOffscreenAtCell(probe, {12,10}, 680, 420, error,
                output + "/" + label + ".bmp", false);
            return result.has_value() && result->specialMechanismRenderActors >= 2;
        };
        if (!screenshot("docked")) return fail("docked missile voxel did not render: " + error);
        for (int i = 0; i < 40 && probe.specialMechanisms.spawnedMissiles().empty(); ++i) {
            auto* parent = findMutableObjectById(probe, 1);
            (void)advanceRuntimeAttackBehavior(probe, *parent, probe.behaviors.at(1));
            if (!probe.specialMechanisms.spawnedMissiles().empty()) break;
            ++probe.tick;
            advanceRuntimeEffects(probe);
        }
        if (probe.specialMechanisms.spawnedMissiles().size() != 2 || !probe.projectiles.empty())
            return fail("DredLauncher did not reserve exactly two real missiles: " + probe.simulationStatus);
        const auto initial = probe.specialMechanisms.spawnedMissiles();
        const int firstRelease = initial.begin()->second.releaseTick;
        int lastRelease = firstRelease;
        for (const auto& [id, m] : initial) lastRelease = std::max(lastRelease, m.releaseTick);
        if (lastRelease <= probe.tick + 30) return fail("native PauseFrames/TiltFrames were skipped");
        const auto roundTrip = [&]() {
            std::ostringstream save;
            probe.specialMechanisms.save(save);
            IniFile ini;
            RuntimeSpecialMechanismState restored;
            restored.reset(probe.rules.getSpecialMechanismRules());
            if (!ini.loadFromString(save.str()) || !restored.load(ini, error)
                || restored.stateHash() != probe.specialMechanisms.stateHash()
                || !validateRuntimeSavedCarrierState(probe, ini, 15, restored, probe.world.getObjects(), error))
                return false;
            // A corrupt flight number must fail transactionally, preserving the live graph.
            std::string corrupt = save.str();
            const auto section = corrupt.find("[SpecialSpawnedMissiles]");
            const auto row = corrupt.find("0=", section);
            const auto rowEnd = corrupt.find('\n', row);
            const auto lastField = corrupt.rfind('|', rowEnd);
            if (row != std::string::npos && lastField != std::string::npos) {
                corrupt.replace(lastField + 1, rowEnd - lastField - 1, "nan");
                IniFile bad;
                std::string rejection;
                const auto before = restored.stateHash();
                if (!bad.loadFromString(corrupt) || restored.load(bad, rejection)
                    || restored.stateHash() != before) return false;
            }
            probe.specialMechanisms = std::move(restored);
            if (capture) {
                std::ostringstream complete;
                if (!writeRuntimeStateToStream(probe, complete, error)) {
                    error = "full save write: " + error;
                    return false;
                }
                IniFile completeIni;
                auto loaded = std::make_unique<RuntimeWindowState>();
                seedRuntimeLoadScratch(*loaded, probe, false);
                if (!completeIni.loadFromString(complete.str())
                    || !materializeRuntimeLoadStateFromIni(*loaded, completeIni, error)) {
                    error = "full save load: " + error;
                    return false;
                }
                if (loaded->specialMechanisms.stateHash() != probe.specialMechanisms.stateHash()) {
                    error = "full save changed missile/carrier authority";
                    return false;
                }
                for (const auto& [id, missile] : loaded->specialMechanisms.spawnedMissiles()) {
                    const auto* entity = findObjectById(*loaded, id);
                    if (entity == nullptr || runtimeObjectRenderX(*entity) != missile.x
                        || runtimeObjectRenderY(*entity) != missile.y
                        || runtimeDrawLevelForObject(*entity) != missile.z) {
                        error = "full save lost live missile pose #" + std::to_string(id);
                        return false;
                    }
                }
            }
            return true;
        };
        bool tilted = false, flew = false, parentSunk = false, acquired = false, fired = false;
        bool savedTilt = false, savedFlight = false;
        int finishedTick = -1;
        for (int step = 0; step < probe.config.fixedTickHz * 85; ++step) {
            ++probe.tick;
            if (hasDefense) {
                advanceRuntimeWeaponCooldowns(probe);
                advanceRuntimeCombatBursts(probe);
                advanceRuntimeGattlingWeaponTimers(probe);
                const auto completed = probe.behaviors.find(3);
                if (completed != probe.behaviors.end() && completed->second.state == RuntimeBehaviorState::Complete)
                    probe.behaviors.erase(completed);
                advanceRuntimeGuardBehavior(probe);
                const auto behavior = probe.behaviors.find(3);
                if (behavior != probe.behaviors.end() && behavior->second.kind == RuntimeBehaviorKind::Attack) {
                    const bool targetingMissile = initial.contains(behavior->second.targetObjectId);
                    if (targetingMissile) acquired = true;
                    auto* defense = findMutableObjectById(probe, 3);
                    if (defense && defense->alive)
                        (void)advanceRuntimeAttackBehavior(probe, *defense, behavior->second);
                    // Scattered flak keeps a fixed world endpoint instead of
                    // a homing target ID. Observe the actual launch against
                    // the acquired missile without requiring homing metadata.
                    for (const auto& projectile : probe.projectiles)
                        if (targetingMissile && projectile.attackerObjectId == 3 && projectile.aa
                            && projectile.spawnTick == probe.tick) fired = true;
                }
            }
            advanceRuntimeEffects(probe);
            if (probe.tick < firstRelease && findObjectById(probe, 2)->health != targetHealth)
                return fail("target damaged before the launch sequence completed");
            for (const auto& [id, m] : probe.specialMechanisms.spawnedMissiles()) {
                const auto* entity = findObjectById(probe, id);
                if (entity == nullptr || !entity->alive) return fail("flight authority outlived its world object");
                if (!m.released) {
                    if (entity->airborne || !probe.specialMechanisms.isCarrierChildDocked(id)
                        || std::abs(m.x - initial.at(id).x) > 0.00001
                        || std::abs(m.y - initial.at(id).y) > 0.00001)
                        return fail("missile translated across the deck before completing tilt");
                    tilted = tilted || m.pitch > 0.2;
                } else {
                    flew = true;
                    if (!entity->airborne || probe.specialMechanisms.isCarrierChild(id)
                        || entity->dockedToObjectId >= 0 || probe.behaviors.contains(id))
                        return fail("released missile still used a carrier aircraft mission");
                }
            }
            if (!savedTilt && tilted && probe.tick < firstRelease) {
                if (!roundTrip() || !screenshot("tilting")) return fail("tilt save/render: " + error);
                savedTilt = true;
            }
            if (!savedFlight && probe.tick == lastRelease + 1) {
                if (!roundTrip() || !screenshot("released")) return fail("flight save/render: " + error);
                savedFlight = true;
            }
            if (capture && probe.tick == lastRelease + 50 && !screenshot("in-flight"))
                return fail("flight model did not render");
            if (scenario.sinkParent && !parentSunk && probe.tick > lastRelease) {
                RuntimeObjectExitRequest exit;
                exit.objectId = 1; exit.kind = RuntimeObjectExitKind::Destroyed;
                exit.tick = probe.tick; exit.reason = "missile parent independence smoke";
                exit.mission = "Destroyed"; exit.sourceOwner = "Americans";
                queueRuntimeObjectExit(probe, std::move(exit));
                drainRuntimeObjectExitQueue(probe);
                if (probe.specialMechanisms.spawnedMissiles().size() != 2)
                    return fail("sinking the launcher erased its airborne missiles");
                parentSunk = true;
            }
            if (probe.specialMechanisms.spawnedMissiles().empty()) {
                finishedTick = probe.tick;
                break;
            }
        }
        if (finishedTick < 0 || !tilted || !flew || !savedTilt || !savedFlight)
            return fail("missile launch/flight did not complete for " + std::string(scenario.defense));
        const int remainingHealth = findObjectById(probe, 2)->health;
        if (hasDefense) {
            if (!acquired || !fired || remainingHealth != targetHealth)
                return fail(std::string(scenario.defense) + " interception failed: acquired="
                    + std::to_string(acquired) + " fired=" + std::to_string(fired)
                    + " targetHealth=" + std::to_string(remainingHealth));
            // Advance past the old fake projectile's would-be impact: no ghost damage.
            for (int i = 0; i < 120; ++i) { ++probe.tick; advanceRuntimeEffects(probe); }
            if (findObjectById(probe, 2)->health != targetHealth) return fail("intercepted missile applied ghost impact");
        } else {
            const int nativeDamage = scenario.rank >= 2 ? 600 : 300;
            const double multiplier = initial.begin()->second.damageMultiplier;
            const int expected = runtimeAuthoredDamageAgainst(probe, *findObjectById(probe, 2),
                nativeDamage, initial.begin()->second.warheadId, 0.0, multiplier) * 2;
            if (remainingHealth != targetHealth - expected)
                return fail("native missile impact damage/volley wrong: actual="
                    + std::to_string(targetHealth - remainingHealth) + " expected=" + std::to_string(expected));
        }
        std::cout << "Dreadnought missiles: defense=" << (hasDefense ? scenario.defense : "none")
            << " rank=" << scenario.rank << " parentSunk=" << parentSunk
            << " tilt=ok realEntities=ok saveLoad=ok finishTick=" << finishedTick
            << " targetHealth=" << remainingHealth << '\n';
    }
    return true;
}
