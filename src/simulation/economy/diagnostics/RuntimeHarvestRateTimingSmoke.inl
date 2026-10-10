// HarvestRate is authored in retail rule frames (15 per nominal second).
// These goldens deliberately do not call the production conversion helper.
// HARV/CMIN's absent-field default and the separate unloading approximation
// are not claims of retail parity made by this check.
bool runRuntimeHarvestRateTimingSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "harvest rate timing: " + message;
        return false;
    };
    const UnitType* originalSlave = findRuntimeUnitTypeById(live.rules, "SLAV");
    if (originalSlave == nullptr || originalSlave->harvestRate != 150
        || originalSlave->storage != 4)
        return fail("original SLAV HarvestRate=150 / Storage=4 fixture missing");

    std::string rulesText;
    if (!readRuntimeIniSource(live.config, RuntimeCampaignIniKind::Rules, rulesText)
        || rulesText.empty()) return fail("original rules source unavailable");
    RulesDatabase buildingRateRules, vehicleRateRules;
    IniFile buildingIni, vehicleIni;
    if (!buildingIni.loadFromString(rulesText
            + "\n\n[SLAV]\nHarvestRate=0\n[YAREFN]\nHarvestRate=41\n")
        || !buildingRateRules.loadFromIni(buildingIni)
        || !vehicleIni.loadFromString(rulesText + "\n\n[CMIN]\nHarvestRate=41\n")
        || !vehicleRateRules.loadFromIni(vehicleIni))
        return fail("isolated explicit-rate fixture rules failed to load");

    struct HarvestTimingCase {
        const char* name;
        const char* type;
        bool slave;
        const RulesDatabase* rules;
        int authoredFrames;
        std::array<int, 3> expectedTicks;
    };
    const std::array<HarvestTimingCase, 3> cases{{
        {"original-worker", "SLAV", true, &live.rules, 150, {150, 300, 600}},
        {"building-fallback", "SLAV", true, &buildingRateRules, 41, {41, 82, 164}},
        {"explicit-vehicle", "CMIN", false, &vehicleRateRules, 41, {41, 82, 164}}
    }};
    const std::array<int, 3> frequencies{{15, 30, 60}};
    std::vector<std::string> failures;
    int checks = 0, bailEvents = 0, saveChecks = 0;
    for (const auto& test : cases) for (std::size_t frequency = 0; frequency < frequencies.size(); ++frequency) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        const int hz = frequencies[frequency], expectedTicks = test.expectedTicks[frequency];
        probe.config = live.config;
        probe.config.fixedTickHz = hz;
        probe.config.computerAiEnabled = probe.config.multiplayerActive = false;
        probe.rules = *test.rules;
        probe.art = live.art;
        probe.rulesLoaded = probe.artLoaded = true;
        probe.runtimeSoundsEnabled = probe.runtimePreferencePersistenceEnabled = false;
        probe.runtimeSuppressPlayerAudioFeedback = true;
        probe.economy.owner = test.slave ? "YuriCountry" : "Americans";
        probe.economy.credits = 0;
        probe.economy.harvesters = test.slave ? 0 : 1;
        probe.economy.skirmishStartApplied = true;
        probe.triggers.bootstrapComplete = true;
        const RuntimeCellPoint origin{5, 5}, ore{6, 5};
        for (int y = 1; y <= 16; ++y) for (int x = 1; x <= 16; ++x) {
            TerrainCell cell;
            cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0;
            if (RuntimeCellPoint{x, y} == ore) setRuntimeResourceTerrainBails(cell, "TIB", 12);
            probe.world.accessTerrainCells().push_back(cell);
        }
        probe.world.accessObjects().reserve(4);
        int refineryId = -1;
        if (test.slave) {
            auto* refinery = addRuntimeObjectInstance(probe, RuntimeBuildKind::Building, "YAREFN", {10, 10});
            if (refinery == nullptr) return fail("YAREFN fixture missing");
            refineryId = refinery->instanceId;
            refinery->buildAnimTicksTotal = refinery->buildAnimTicksRemaining = 0;
        }
        auto* object = addRuntimeObjectInstance(probe,
            test.slave ? RuntimeBuildKind::Infantry : RuntimeBuildKind::Unit, test.type, origin);
        if (object == nullptr) return fail(std::string(test.type) + " object fixture missing");
        const int id = object->instanceId;
        object->facing = runtimeFacingFromDelta(1, 0);
        object->mission = "Harvest";
        if (test.slave) {
            RuntimeSlaveMinerTask task;
            task.refineryId = refineryId; task.targetOre = ore;
            task.phase = RuntimeSlaveMinerPhase::SeekingOre;
            probe.slaveMinerTasks[id] = task;
        } else {
            RuntimeHarvesterTask task;
            task.targetOre = ore; task.phase = RuntimeHarvesterPhase::SeekingOre;
            probe.harvesterTasks[id] = task;
        }
        initializeRuntimeNavigation(probe);
        const auto cargo = [&]() {
            return test.slave ? probe.slaveMinerTasks.at(id).cargoUnits : probe.harvesterTasks.at(id).cargoUnits;
        };
        const auto remaining = [&]() {
            return test.slave ? probe.slaveMinerTasks.at(id).workTicksRemaining
                              : probe.harvesterTasks.at(id).workTicksRemaining;
        };
        const auto step = [&]() {
            ++probe.tick;
            // Execute the real collector, including resource access, facing,
            // timer initialization, ore consumption and cargo accounting. Avoid
            // the unrelated automatic creation of four additional SLAV workers.
            if (test.slave) advanceRuntimeSlaveMinerWorkerTask(probe,
                *findMutableObjectById(probe, id), probe.slaveMinerTasks.at(id));
            else advanceRuntimeHarvesterEconomy(probe);
        };
        step();
        // Infantry consumes its first waiting tick when it acquires the target;
        // vehicles enter Loading first. Both must initialize the same rule time.
        const int initialTimer = remaining() + (test.slave ? 1 : 0);
        int first = -1, second = -1, previousCargo = cargo();
        bool accounting = previousCargo == 0, saved = false;
        for (int tick = 0; tick < 2 * expectedTicks + 12 && second < 0; ++tick) {
            step();
            const int loaded = cargo();
            if (loaded != previousCargo) {
                accounting = accounting && loaded == previousCargo + 1;
                if (first < 0) first = probe.tick;
                else second = probe.tick;
                ++bailEvents;
            }
            previousCargo = loaded;
            const auto* terrain = findRuntimeTerrainCellAt(probe, ore);
            accounting = accounting && terrain != nullptr
                && runtimeResourceBailCountForTerrain(*terrain) + loaded == 12
                && probe.economy.credits == 0;
            if (!saved && probe.tick >= std::max(2, initialTimer / 2) && loaded == 0) {
                const int before = remaining();
                if (test.slave) {
                    const auto restored = parseRuntimeSlaveMinerTask(
                        serializeRuntimeSlaveMinerTask(probe.slaveMinerTasks.at(id)));
                    if (!restored) return fail("worker countdown save parse failed");
                    probe.slaveMinerTasks.at(id) = *restored;
                } else {
                    const auto restored = parseRuntimeHarvesterTask(
                        serializeRuntimeHarvesterTask(probe.harvesterTasks.at(id)));
                    if (!restored) return fail("vehicle countdown save parse failed");
                    probe.harvesterTasks.at(id) = *restored;
                }
                saved = remaining() == before && cargo() == 0;
                if (saved) ++saveChecks;
                else return fail("countdown save changed active pickup state");
            }
        }
        // Allow the existing one/two task-transition ticks; the asserted rule
        // wait itself is exact. This does not claim retail mission scheduling.
        const int interval = second - first;
        const bool timing = initialTimer == expectedTicks
            && first >= expectedTicks && first <= expectedTicks + 2
            && second > first && interval >= expectedTicks && interval <= expectedTicks + 2;
        const bool passed = timing && accounting && saved && cargo() == 2;
        std::cout << "Harvest rate timing: branch=" << test.name << " type=" << test.type
            << " hz=" << hz << " frames=" << test.authoredFrames
            << " expectedTicks=" << expectedTicks << " initialTimer=" << initialTimer
            << " firstBailTick=" << first << " nextBailInterval=" << interval
            << " nominalIntervalSeconds=" << static_cast<double>(interval) / hz
            << " cargo=" << cargo() << " conserved=" << accounting
            << " saved=" << saved << " result=" << (passed ? "PASS" : "FAIL") << '\n';
        ++checks;
        if (!passed) failures.push_back(std::string(test.name) + "@" + std::to_string(hz)
            + "Hz expected=" + std::to_string(expectedTicks) + " actual=" + std::to_string(initialTimer)
            + " first=" + std::to_string(first) + " interval=" + std::to_string(interval));
    }
    std::cout << "Harvest rate timing: cases=" << checks << " bailEvents=" << bailEvents
        << " countdownSaves=" << saveChecks << " failures=" << failures.size() << '\n';
    if (!failures.empty()) {
        std::string summary;
        for (const auto& failure : failures) summary += (summary.empty() ? "" : "; ") + failure;
        return fail(summary);
    }
    return true;
}
