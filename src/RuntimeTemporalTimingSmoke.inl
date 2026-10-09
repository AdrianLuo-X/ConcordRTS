bool runRuntimeTemporalTimingSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& reason) {
        error = "temporal timing: " + reason;
        return false;
    };
    IniFile stockIni;
    RulesDatabase stockRules;
    if (!stockIni.loadFromFile(live.report.rulesPath) || !stockRules.loadFromIni(stockIni))
        return fail("original rules could not be loaded");
    const auto fixture = [&](int hz, int rank, const std::string& targetType,
                             int attackers, bool wounded = false) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& s = *storage;
        s.config = live.config;
        s.config.fixedTickHz = hz;
        s.config.gameSpeed = 4;
        s.config.computerAiEnabled = s.config.noCooldown = s.config.multiplayerActive = false;
        s.config.crates = s.config.superweapons = s.config.shortGame = false;
        s.config.playerSlots.clear();
        s.report = live.report;
        s.rules = stockRules;
        s.art = live.art;
        s.theater = live.theater;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        s.economy.owner = "Americans";
        s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true;
        s.world = live.world;
        s.world.accessObjects().clear();
        s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear();
        for (int y = 1; y <= 18; ++y) for (int x = 1; x <= 18; ++x) {
            TerrainCell cell;
            cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = cell.subTileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance source;
        source.instanceId = 1; source.kind = WorldObjectKind::Infantry;
        source.type = "CLEG"; source.owner = "Americans";
        source.x = 8; source.y = 8; source.cell = runtimeCellKey(8, 8);
        source.health = 125; source.facing = 64; source.veterancy = rank;
        source.mission = "Guard";
        WorldObjectInstance target;
        target.instanceId = 2;
        target.kind = targetType == "E1" ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        target.type = targetType; target.owner = "Russians";
        target.x = 10; target.y = 8; target.cell = runtimeCellKey(10, 8);
        target.health = wounded ? 1 : targetType == "E1" ? 125 : 300;
        target.mission = "Sleep";
        s.world.accessObjects() = {source, target};
        s.selectedObjectIds = {1};
        if (attackers == 2) {
            source.instanceId = 3; source.y = 9; source.cell = runtimeCellKey(8, 9);
            s.world.accessObjects().push_back(source);
            s.selectedObjectIds.push_back(3);
        }
        initializeRuntimeLocalProductionLanes(s.economy);
        initializeRuntimeNavigation(s);
        RuntimeHumanHouseAuthority authority;
        authority.owner = "Americans"; authority.canonicalOwner = "americans";
        authority.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(authority));
        issueRuntimeAttackCommand(s, *findObjectById(s, 2));
        return storage;
    };
    const auto targetStatus = [](const RuntimeWindowState& s) {
        SpecialMechanismRenderSnapshot result;
        for (const auto& snapshot : s.specialMechanisms.renderSnapshots(s.tick, s.config.fixedTickHz))
            if (snapshot.kind == "temporal" && snapshot.targetObjectId == 2) return snapshot;
        return result;
    };
    const auto step = [](RuntimeWindowState& s) {
        ++s.tick;
        advanceRuntimeSimulation(s);
    };
    const auto establish = [&](RuntimeWindowState& s, int attackers) {
        if (s.lastCommand.acceptedCount != attackers) return false;
        for (int wait = 0; wait < s.config.fixedTickHz * 10; ++wait) {
            if (s.specialMechanisms.hasTemporalAttacker(1, 2)
                && (attackers == 1 || s.specialMechanisms.hasTemporalAttacker(3, 2))) return true;
            step(s);
        }
        return false;
    };
    int timingCases = 0;
    int reloadCases = 0;
    for (const int hz : {15, 30, 60}) for (const int rank : {0, 2})
        for (const std::string targetType : {std::string("E1"), std::string("MTNK")}) {
            auto storage = fixture(hz, rank, targetType, 1);
            auto& s = *storage;
            const std::string label = targetType + "/rank=" + std::to_string(rank)
                + "/hz=" + std::to_string(hz);
            if (!establish(s, 1)) return fail(label + " real attack did not connect");
            const int damage = rank == 0 ? 8 : 16;
            const int required = targetType == "E1" ? 1250 : 3000;
            const auto* attacker = findObjectById(s, 1);
            const auto profile = runtimeWeaponProfileForObject(s, *attacker, findObjectById(s, 2));
            if (!profile.weapon || profile.weapon->damage != damage
                || profile.weapon->id != (rank == 0 ? "NeutronRifle" : "NeutronRifleE"))
                return fail(label + " stock weapon fixture changed");
            auto status = targetStatus(s);
            if (status.requiredProgress != required || status.progress != 0)
                return fail(label + " initial hit must establish full-health budget without progress");
            const int startTick = s.tick;
            int expectedProgress = 0;
            std::unique_ptr<RuntimeWindowState> restored;
            bool completed = false;
            for (int elapsed = 0; elapsed < hz * 40; ++elapsed) {
                const int beforeTick = s.tick;
                step(s);
                // Independent retail rule-frame calculation: a rule minute has
                // 900 frames, and each connected firer contributes Damage/frame.
                const int frames = s.tick * 15 / hz - beforeTick * 15 / hz;
                expectedProgress += frames * damage;
                if (restored) {
                    step(*restored);
                    if (restored->tick != s.tick
                        || restored->specialMechanisms.stateHash() != s.specialMechanisms.stateHash()
                        || (findObjectById(*restored, 2) != nullptr) != (findObjectById(s, 2) != nullptr))
                        return fail(label + " resumed attack diverged");
                }
                if (expectedProgress >= required) {
                    if (findObjectById(s, 2) || s.specialMechanisms.isTemporalFrozen(2))
                        return fail(label + " did not erase at expected rule frame");
                    completed = true;
                    break;
                }
                status = targetStatus(s);
                if (!findObjectById(s, 2) || status.progress != expectedProgress
                    || status.requiredProgress != required)
                    return fail(label + " continuous progress expected=" + std::to_string(expectedProgress)
                        + " actual=" + std::to_string(status.progress) + " tick=" + std::to_string(s.tick));
                if (findObjectById(s, 2)->health != (targetType == "E1" ? 125 : 300))
                    return fail(label + " erasure damaged health before completion");
                const auto beforeRepeat = s.specialMechanisms.stateHash();
                advanceRuntimeSpecialMechanisms(s);
                if (s.specialMechanisms.stateHash() != beforeRepeat)
                    return fail(label + " repeated same-tick update advanced erasure");
                if (!restored && expectedProgress >= damage * 20) {
                    std::ostringstream saved;
                    std::string loadError;
                    if (!writeRuntimeStateToStream(s, saved, loadError))
                        return fail(label + " save failed: " + loadError);
                    IniFile savedIni;
                    if (!savedIni.loadFromString(saved.str())) return fail(label + " saved INI rejected");
                    restored = std::make_unique<RuntimeWindowState>();
                    seedRuntimeLoadScratch(*restored, s, false);
                    if (!materializeRuntimeLoadStateFromIni(*restored, savedIni, loadError)
                        || restored->specialMechanisms.stateHash() != s.specialMechanisms.stateHash())
                        return fail(label + " load changed progress/clock: " + loadError);
                    ++reloadCases;
                }
            }
            if (!completed || !restored) return fail(label + " completion/reload missing");
            const int frames = s.tick * 15 / hz - startTick * 15 / hz;
            if (frames != (required + damage - 1) / damage)
                return fail(label + " wrong total erasure frame budget");
            ++timingCases;
            std::cout << "Temporal timing: target=" << targetType << " rank=" << rank << " hz=" << hz
                << " frames=" << frames << " ticks=" << s.tick - startTick
                << " nominalSeconds=" << static_cast<double>(frames) / 15.0 << " reload=ok\n";
        }

    // Two real attack orders contribute simultaneously; damage to the target
    // before connecting must not reduce its authored temporal budget.
    for (const bool wounded : {false, true}) {
        auto storage = fixture(30, 0, "MTNK", 2, wounded);
        auto& s = *storage;
        if (!establish(s, 2)) return fail("two attackers did not connect");
        auto status = targetStatus(s);
        if (status.requiredProgress != 3000) return fail("wounded target changed maximum-health budget");
        int expectedProgress = status.progress;
        bool completed = false;
        const int startTick = s.tick;
        while (s.tick - startTick < 1200) {
            const int beforeTick = s.tick;
            step(s);
            expectedProgress += (s.tick * 15 / 30 - beforeTick * 15 / 30) * 16;
            if (expectedProgress >= 3000) {
                if (findObjectById(s, 2)) return fail("cooperative erasure missed completion");
                completed = true;
                break;
            }
            status = targetStatus(s);
            if (status.progress != expectedProgress || !s.specialMechanisms.hasTemporalAttacker(1, 2)
                || !s.specialMechanisms.hasTemporalAttacker(3, 2))
                return fail("cooperative erasure lost combined per-frame contributions");
        }
        if (!completed) return fail("cooperative erasure timed out");
        std::cout << "Temporal cooperation: wounded=" << wounded << " ticks=" << s.tick - startTick << " passed\n";
    }

    // Cancel one of two established attacks through the real command/death
    // paths. Advance only special mechanisms afterwards so a stopped guard
    // cannot independently reacquire the adjacent enemy during this check.
    for (const bool attackerDies : {false, true}) {
        auto storage = fixture(30, 0, "MTNK", 2);
        auto& s = *storage;
        if (!establish(s, 2)) return fail("interruption fixture did not connect");
        for (int tick = 0; tick < 40; ++tick) step(s);
        const auto before = targetStatus(s);
        const auto beforeLinks = s.specialMechanisms.temporalTargets().find(2);
        if (beforeLinks == s.specialMechanisms.temporalTargets().end()
            || !beforeLinks->second.progressByAttacker.contains(1)
            || !beforeLinks->second.progressByAttacker.contains(3)
            || beforeLinks->second.progressByAttacker.at(1) <= 0
            || beforeLinks->second.progressByAttacker.at(3) <= 0)
            return fail("interruption fixture did not accumulate both contributions");
        if (attackerDies) {
            RuntimeObjectExitRequest death;
            death.objectId = 1;
            death.kind = RuntimeObjectExitKind::Destroyed;
            death.tick = s.tick;
            death.reason = "temporal timing contributor death";
            finalizeRuntimeObjectExit(s, death);
            if (findObjectById(s, 1)) return fail("contributor death did not remove attacker");
        } else {
            s.selectedObjectIds = {1};
            issueRuntimeStopCommand(s);
        }
        if (targetStatus(s).progress != before.progress
            || !s.specialMechanisms.isTemporalFrozen(2)
            || s.specialMechanisms.hasTemporalAttacker(1, 2)
            || !s.specialMechanisms.hasTemporalAttacker(3, 2))
            return fail(attackerDies ? "death lost shared temporal progress" : "stop lost shared temporal progress");
        int expected = before.progress;
        for (int tick = 0; tick < 30; ++tick) {
            const int previousTick = s.tick;
            ++s.tick;
            advanceRuntimeSpecialMechanisms(s);
            expected += (s.tick * 15 / 30 - previousTick * 15 / 30) * 8;
            if (targetStatus(s).progress != expected || !s.specialMechanisms.isTemporalFrozen(2))
                return fail("remaining contributor did not continue at eight progress per frame");
        }
        s.selectedObjectIds = {3};
        issueRuntimeStopCommand(s);
        const auto* target = findObjectById(s, 2);
        if (!target || target->health != 300 || target->mission == "Temporal"
            || s.specialMechanisms.isTemporalFrozen(2)
            || s.specialMechanisms.hasTemporalAttacker(3, 2)
            || s.specialMechanisms.temporalTargets().contains(2))
            return fail("last contributor stop did not release intact target");
        std::cout << "Temporal interruption: first=" << (attackerDies ? "death" : "stop")
            << " preservedProgress=" << before.progress << " continuation=" << expected
            << " lastStop=released\n";
    }

    // Build a historical four-field record inside an otherwise real core save.
    // The old file had neither ProgressScale nor lastAdvancedTick. Check fixed
    // historical numbers and then ensure a modern round trip does not scale twice.
    {
        auto storage = fixture(30, 0, "MTNK", 2);
        auto& s = *storage;
        if (!establish(s, 2)) return fail("legacy save fixture did not connect");
        for (int tick = 0; tick < 12; ++tick) step(s);
        std::ostringstream saved;
        s.specialMechanisms.save(saved);
        std::string historical = saved.str();
        const auto section = historical.find("[SpecialTemporal]");
        if (section == std::string::npos) return fail("core save omitted temporal section");
        const auto nextSection = historical.find("\n[", section + 1);
        historical.replace(section,
            nextSection == std::string::npos ? std::string::npos : nextSection - section,
            "[SpecialTemporal]\nCount=1\n0=2|300|yes|1,24~3,32\n");
        IniFile legacyIni;
        RuntimeSpecialMechanismState migrated;
        migrated.reset(s.rules.getSpecialMechanismRules());
        std::string loadError;
        if (!legacyIni.loadFromString(historical) || !migrated.load(legacyIni, loadError))
            return fail("legacy temporal save rejected: " + loadError);
        const auto legacyTarget = migrated.temporalTargets().find(2);
        if (legacyTarget == migrated.temporalTargets().end()) return fail("legacy target missing");
        const auto& loaded = legacyTarget->second;
        if (loaded.requiredProgress != 3000 || !loaded.frozen || loaded.lastAdvancedTick != -1
            || loaded.progressByAttacker != std::map<int, int>{{1, 240}, {3, 320}})
            return fail("legacy temporal migration did not multiply budget and all contributions by ten");
        std::ostringstream modernText;
        migrated.save(modernText);
        IniFile modernIni;
        RuntimeSpecialMechanismState roundTrip;
        roundTrip.reset(s.rules.getSpecialMechanismRules());
        if (!modernIni.loadFromString(modernText.str())
            || modernIni.getValue("SpecialTemporal", "ProgressScale", "") != "10"
            || !roundTrip.load(modernIni, loadError) || roundTrip.stateHash() != migrated.stateHash())
            return fail("migrated temporal save rescaled on second load: " + loadError);
        std::cout << "Temporal legacy save: fourFields=ok scale=1->10 budget=300->3000"
            << " progress=56->560 percentage=preserved modernRoundTrip=stable\n";
    }

    // Consume exactly the tick budget scheduled by each finite speed for one second.
    // This proves pacing integration, not achieved wall-clock performance.
    int previousProgress = -1;
    int previousScheduledTicks = -1;
    for (int speed = 0; speed < 6; ++speed) {
        auto storage = fixture(30, 0, "MTNK", 1);
        auto& s = *storage;
        if (!establish(s, 1)) return fail("speed fixture did not connect");
        const auto initial = targetStatus(s);
        s.config.gameSpeed = speed;
        if (targetStatus(s).progress != initial.progress) return fail("speed change jumped progress");
        const int startTick = s.tick;
        const int scheduledTicks = runtimeSimulationPacingTickRate(s.config);
        for (int tick = 0; tick < scheduledTicks; ++tick) step(s);
        const int expected = initial.progress + (s.tick * 15 / 30 - startTick * 15 / 30) * 8;
        const int actual = targetStatus(s).progress;
        // Adjacent slow settings can finish on the same retail frame after
        // exactly one second, despite scheduling different simulation ticks.
        if (actual != expected || actual < previousProgress || scheduledTicks <= previousScheduledTicks)
            return fail("speed=" + std::to_string(speed) + " did not accelerate actual temporal progress");
        previousProgress = actual;
        previousScheduledTicks = scheduledTicks;
        std::cout << "Temporal speed: speed=" << speed << " scheduledTicks=" << scheduledTicks
            << " progress=" << actual << " budgetSeconds=1\n";
    }

    // The retail maximum is an unlimited/no-wait sentinel, not a fixed rate.
    // A zero-elapsed scheduler batch still advances real temporal simulation,
    // while the same input-service caps used by the loop remain authoritative.
    for (const int budget : {8, 16}) {
        auto storage = fixture(30, 0, "MTNK", 1);
        auto& s = *storage;
        if (!establish(s, 1)) return fail("unlimited speed fixture did not connect");
        const auto initial = targetStatus(s);
        s.config.gameSpeed = 6;
        const auto timing = runtimeGameSpeedTiming(s.config.gameSpeed, s.config.fixedTickHz);
        if (targetStatus(s).progress != initial.progress
            || !timing.unlimited() || timing.nativeWaitQuanta != 0
            || timing.secondsPerTick != 0.0 || timing.roundedTickRate() != 0
            || runtimeSimulationPacingTickRate(s.config) != 0)
            return fail("speed=6 must retain progress and use the unlimited no-wait sentinel");
        const int startTick = s.tick;
        double accumulated = timing.accumulate(0.0, 0.0, budget);
        int advanced = 0;
        while (advanced <= budget && timing.ready(accumulated, advanced, budget)) {
            step(s);
            ++advanced;
            accumulated = timing.consume(accumulated);
            const int expected = initial.progress + (s.tick * 15 / 30 - startTick * 15 / 30) * 8;
            const auto status = targetStatus(s);
            const auto* target = findObjectById(s, 2);
            if (!target || target->health != 300 || status.progress != expected
                || status.requiredProgress != 3000 || !s.specialMechanisms.isTemporalFrozen(2)
                || !s.specialMechanisms.hasTemporalAttacker(1, 2))
                return fail("speed=6 zero-wait batch changed exact temporal progress or intact target");
        }
        if (advanced != budget || s.tick - startTick != budget || accumulated != 0.0
            || timing.ready(accumulated, advanced, budget)
            || !timing.ready(accumulated, 0, budget)
            || targetStatus(s).progress <= initial.progress)
            return fail("speed=6 zero-wait simulation did not honor the complete finite input batch cap");
        std::cout << "Temporal speed: speed=6 unlimited=yes elapsedSeconds=0 batchCap=" << budget
            << " advancedTicks=" << advanced << " progress=" << targetStatus(s).progress << '\n';
    }

    std::cout << "Temporal timing: passed cases=" << timingCases << " reloads=" << reloadCases
        << " cooperation=2 interruptions=2 legacyMigration=ok speeds=7 sameTick=stable HP=unchanged\n";
    return true;
}
