// Independent goldens from gamemd.exe 006673C7 / 0073E96F (9 stages * 2
// frames), and 0073E361 (integer stage >= 0.016 * 900, hence 15 frames).
// This executes real collection/docking/deposit logic and full save loading.
bool runRuntimeStockHarvesterCadenceSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "stock harvester cadence: " + message;
        return false;
    };
    std::string rulesText;
    if (!readRuntimeIniSource(live.config, RuntimeCampaignIniKind::Rules, rulesText))
        return fail("original rules unavailable");
    IniFile overrideIni;
    RulesDatabase overridden;
    if (!overrideIni.loadFromString(rulesText
        + "\n[General]\nHarvesterLoadRate=3\nHarvesterDumpRate=.017\n")
        || !overridden.loadFromIni(overrideIni)) return fail("global override fixture");
    if (live.rules.getHarvesterCadenceRules().loadRate != 2
        || live.rules.getHarvesterCadenceRules().dumpRateMinutes != .016
        || overridden.getHarvesterCadenceRules().loadRate != 3
        || overridden.getHarvesterCadenceRules().dumpRateMinutes != .017
        || live.rules.getDetailRulesHash() == overridden.getDetailRulesHash())
        return fail("native defaults, parsed overrides or authoritative rules hash");
    // Each field independently affects the hash, including an otherwise equal
    // rules load. Merely hashing the source INI is not sufficient here.
    for (const char* entry : {"HarvesterLoadRate=3", "HarvesterDumpRate=.017"}) {
        IniFile changedIni;
        RulesDatabase changed;
        if (!changedIni.loadFromString(rulesText + "\n[General]\n" + entry + "\n")
            || !changed.loadFromIni(changedIni)
            || changed.getDetailRulesHash() == live.rules.getDetailRulesHash())
            return fail(std::string("missing cadence field in rules hash: ") + entry);
    }
    const auto makeState = [&](int hz, const RulesDatabase& rules) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = live.config;
        probe.config.fixedTickHz = hz;
        probe.config.computerAiEnabled = probe.config.multiplayerActive = false;
        probe.rules = rules;
        probe.art = live.art;
        probe.rulesLoaded = probe.artLoaded = true;
        probe.runtimeSoundsEnabled = probe.runtimePreferencePersistenceEnabled = false;
        probe.runtimeSuppressPlayerAudioFeedback = true;
        probe.economy.owner = "Americans";
        probe.economy.credits = 0;
        probe.economy.harvesters = 1;
        probe.economy.skirmishStartApplied = true;
        probe.triggers.bootstrapComplete = true;
        for (int y = 1; y <= 18; ++y) for (int x = 1; x <= 18; ++x) {
            TerrainCell cell;
            cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0;
            probe.world.accessTerrainCells().push_back(cell);
        }
        probe.world.accessObjects().reserve(4);
        return storage;
    };
    int loads = 0, pickups = 0, dumps = 0, depositBatches = 0, saves = 0, continuityChecks = 0;
    const auto restore = [&](RuntimeWindowState& source, int id,
        std::unique_ptr<RuntimeWindowState>& destination) {
        std::ostringstream saved;
        if (!writeRuntimeStateToStream(source, saved, error)) return false;
        IniFile ini;
        if (!ini.loadFromString(saved.str())) return false;
        auto loaded = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*loaded, source, source.visionEnabled);
        if (!materializeRuntimeLoadStateFromIni(*loaded, ini, error)) return false;
        if (!loaded->harvesterTasks.contains(id)
            || serializeRuntimeHarvesterTask(loaded->harvesterTasks.at(id))
                != serializeRuntimeHarvesterTask(source.harvesterTasks.at(id))
            || loaded->economy.credits != source.economy.credits
            || loaded->tick != source.tick) return false;
        destination = std::move(loaded);
        ++saves;
        return true;
    };
    const auto step = [&](RuntimeWindowState& probe) {
        ++probe.tick;
        advanceRuntimeHarvesterEconomy(probe);
    };
    const auto consistent = [&](const RuntimeWindowState& actual,
        const RuntimeWindowState& loaded, int id) {
        ++continuityChecks;
        return actual.tick == loaded.tick
            && actual.economy.credits == loaded.economy.credits
            && actual.economy.resourceTrips == loaded.economy.resourceTrips
            && serializeRuntimeHarvesterTask(actual.harvesterTasks.at(id))
                == serializeRuntimeHarvesterTask(loaded.harvesterTasks.at(id));
    };
    for (int variant = 0; variant < 2; ++variant)
    for (const char* type : {"CMIN", "HARV"}) for (int hz : {15, 30, 60}) {
        const auto& rules = variant == 0 ? live.rules : overridden;
        const int expectedLoad = (variant == 0 ? 18 : 27) * (hz / 15);
        const int expectedDump = (variant == 0 ? 15 : 16) * (hz / 15);
        auto storage = makeState(hz, rules);
        auto& probe = *storage;
        const RuntimeCellPoint origin{5, 5}, ore{6, 5};
        auto* terrain = findMutableRuntimeTerrainCellAt(probe, ore);
        if (terrain == nullptr) return fail("ore terrain fixture");
        setRuntimeResourceTerrainBails(*terrain, "TIB", 12);
        auto* miner = addRuntimeObjectInstance(probe, RuntimeBuildKind::Unit, type, origin);
        if (miner == nullptr) return fail("miner fixture");
        const int id = miner->instanceId;
        miner->facing = runtimeFacingFromDelta(1, 0);
        miner->mission = "Harvest";
        RuntimeHarvesterTask task;
        task.phase = RuntimeHarvesterPhase::SeekingOre;
        task.targetOre = ore;
        probe.harvesterTasks[id] = task;
        initializeRuntimeNavigation(probe);
        step(probe);
        if (probe.harvesterTasks.at(id).workTicksRemaining != expectedLoad)
            return fail(std::string(type) + " initial pickup timer");
        const int start = probe.tick;
        std::unique_ptr<RuntimeWindowState> restored;
        int previousCargo = 0;
        for (int elapsed = 1; elapsed <= 3 * expectedLoad; ++elapsed) {
            step(probe);
            if (restored) {
                step(*restored);
                if (!consistent(probe, *restored, id)) return fail("pickup save continuation diverged");
            }
            const auto& current = probe.harvesterTasks.at(id);
            const int expectedCargo = elapsed / expectedLoad;
            const auto* cell = findRuntimeTerrainCellAt(probe, ore);
            if (current.cargoUnits != expectedCargo || current.cargoGemUnits != 0
                || current.cargoValue != expectedCargo * 25 || probe.economy.credits != 0
                || cell == nullptr || runtimeResourceBailCountForTerrain(*cell) + current.cargoUnits != 12)
                return fail(std::string(type) + " pickup cadence or conservation at " + std::to_string(elapsed));
            if (current.cargoUnits != previousCargo) { ++pickups; previousCargo = current.cargoUnits; }
            if (elapsed == expectedLoad / 2 && !restore(probe, id, restored))
                return fail("pickup full save roundtrip: " + error);
        }
        ++loads;
        std::cout << "Stock harvester cadence: type=" << type << " hz=" << hz
            << " globalOverride=" << variant << " pickupTicks=" << expectedLoad
            << " elapsed=" << probe.tick - start << " bails=3 saved=1 result=PASS\n";

        // A same-tick turn call cannot advance the rate-limited hull. This also
        // catches the old render-smoke fixture's repeated, unclocked calls.
        WorldObjectInstance facingMiner = *findObjectById(probe, id);
        facingMiner.facing = 0;
        bool aligned = alignRuntimeHarvesterToResource(probe, facingMiner, ore);
        if (aligned) return fail("turn fixture unexpectedly started aligned");
        for (int ticks = 0; ticks < 8 * hz && !aligned; ++ticks) {
            ++probe.tick;
            aligned = alignRuntimeHarvesterToResource(probe, facingMiner, ore);
        }
        if (!aligned || facingMiner.facing != runtimeFacingFromDelta(1, 0))
            return fail("clocked intake alignment did not complete");

        const auto* unit = findRuntimeUnitTypeById(rules, type);
        if (unit == nullptr || (unit->storage != 20 && unit->storage != 40))
            return fail("stock Storage fixture");
        for (int cargoKind = 0; cargoKind < 4; ++cargoKind) {
            auto unloadStorage = makeState(hz, rules);
            auto& unloading = *unloadStorage;
            unloading.economy.refineries = 1;
            auto* refinery = addRuntimeObjectInstance(unloading, RuntimeBuildKind::Building,
                std::string(type) == "HARV" ? "NAREFN" : "GAREFN", {10, 10});
            if (refinery == nullptr) return fail("refinery fixture");
            const int refineryId = refinery->instanceId;
            refinery->buildAnimTicksTotal = refinery->buildAnimTicksRemaining = 0;
            const auto dock = runtimeRefineryDefaultDockCell(unloading, *refinery);
            auto* vehicle = addRuntimeObjectInstance(unloading, RuntimeBuildKind::Unit, type, dock);
            if (vehicle == nullptr) return fail("dock miner fixture");
            const int minerId = vehicle->instanceId;
            vehicle->facing = runtimeHarvesterRefineryDockFacing(unloading, *refinery, dock);
            vehicle->mission = "Dock";
            const int oreCount = cargoKind == 0 ? unit->storage
                : cargoKind == 1 ? 0 : cargoKind == 2 ? 7 : 1;
            const int gemCount = cargoKind == 1 ? unit->storage : cargoKind == 2 ? 3 : 0;
            const int originalValue = oreCount * 25 + gemCount * 50;
            RuntimeHarvesterTask unloadingTask;
            unloadingTask.phase = RuntimeHarvesterPhase::Docking;
            unloadingTask.targetRefineryId = refineryId;
            unloadingTask.dockCell = dock;
            unloadingTask.cargoUnits = oreCount + gemCount;
            unloadingTask.cargoGemUnits = gemCount;
            unloadingTask.cargoHasGems = gemCount > 0;
            unloadingTask.cargoValue = originalValue;
            unloading.harvesterTasks[minerId] = unloadingTask;
            initializeRuntimeNavigation(unloading);
            step(unloading);
            if (unloading.harvesterTasks.at(minerId).phase != RuntimeHarvesterPhase::PreparingToUnload
                || unloading.harvesterTasks.at(minerId).workTicksRemaining != expectedDump)
                return fail("real dock transaction did not initialize dump timer");
            const int batches = oreCount > 0 && gemCount > 0 ? 2 : 1;
            std::unique_ptr<RuntimeWindowState> restoredUnload;
            int previousCredits = 0;
            for (int elapsed = 1; elapsed <= batches * expectedDump; ++elapsed) {
                step(unloading);
                if (restoredUnload) {
                    step(*restoredUnload);
                    if (!consistent(unloading, *restoredUnload, minerId))
                        return fail("unload full save continuation diverged");
                }
                const auto& current = unloading.harvesterTasks.at(minerId);
                const bool firstComplete = elapsed >= expectedDump;
                const bool done = elapsed == batches * expectedDump;
                const int expectedCredits = done ? originalValue
                    : firstComplete ? oreCount * 25 : 0;
                const int expectedUnits = done ? 0 : firstComplete ? gemCount : oreCount + gemCount;
                if (unloading.economy.credits != expectedCredits || current.cargoUnits != expectedUnits
                    || current.cargoValue + unloading.economy.credits != originalValue
                    || current.cargoGemUnits != (done ? 0 : gemCount)
                    || unloading.economy.resourceTrips != (done ? 1 : 0))
                    return fail(std::string(type) + " dump timing/accounting cargo=" + std::to_string(cargoKind)
                        + " at=" + std::to_string(elapsed) + " credits=" + std::to_string(unloading.economy.credits));
                if (unloading.economy.credits != previousCredits) {
                    ++depositBatches; previousCredits = unloading.economy.credits;
                }
                if ((elapsed == expectedDump / 2 || (batches == 2 && elapsed == expectedDump))
                    && !restore(unloading, minerId, restoredUnload))
                    return fail("unload full save roundtrip: " + error);
            }
            ++dumps;
            std::cout << "Stock harvester cadence: type=" << type << " hz=" << hz
                << " globalOverride=" << variant << " cargoKind=" << cargoKind
                << " dumpTicks=" << expectedDump << " batches=" << batches
                << " credits=" << unloading.economy.credits << " result=PASS\n";
        }
    }
    std::cout << "Stock harvester cadence: loads=" << loads << " pickups=" << pickups
        << " unloads=" << dumps << " batches=" << depositBatches
        << " fullSaves=" << saves << " continuationChecks=" << continuityChecks << " PASS\n";
    return loads == 12 && pickups == 36 && dumps == 48 && depositBatches == 60 && saves == 72;
}
