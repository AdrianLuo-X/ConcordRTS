bool runRuntimeComputerMiningSmoke(const RuntimeConfig& config, std::string& error) try {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) {
        error = "computer mining: " + why; return false;
    };
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config;
    stock->config.gameMode = "Battle";
    stock->config.gameModeRulesOverride.clear();
    stock->report = buildRuntimeStartupReport(stock->config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("stock rules/art unavailable");
    const auto makeState = [&](const std::string& country, const std::string& difficulty,
                               bool ore = true) {
        auto result = std::make_unique<RuntimeWindowState>();
        auto& s = *result;
        s.config = stock->config; s.config.fixedTickHz = 30;
        s.config.computerAiEnabled = true; s.config.multiplayerActive = false;
        s.config.crates = false; s.config.shortGame = false; s.config.superweapons = false;
        s.config.startingCredits = 0; s.config.playerSlots.clear();
        s.config.playerSlots.resize(2);
        for (int i = 0; i < 2; ++i) {
            auto& slot = s.config.playerSlots[i];
            slot.enabled = true; slot.human = i == 0; slot.local = i == 0;
            slot.country = i == 0 ? "Americans" : country;
            slot.difficulty = difficulty; slot.team = "-"; slot.startPosition = i + 1;
        }
        s.rules = stock->rules; s.rulesLoaded = true;
        s.art = stock->art; s.artLoaded = true;
        s.report = stock->report;
        s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        s.visionEnabled = false;
        s.economy.owner = runtimeSkirmishOwnerForSlot(0);
        s.economy.credits = 12345; s.economy.skirmishStartApplied = true;
        s.economy.status = "local building ready";
        s.interactionStatus = "local selection";
        s.triggers.bootstrapComplete = true;
        s.skirmishCountryByOwner[toLowerAsciiLocal(runtimeSkirmishOwnerForSlot(0))] = "Americans";
        s.skirmishCountryByOwner[toLowerAsciiLocal(runtimeSkirmishOwnerForSlot(1))] = country;
        s.skirmishDifficultyByOwner[toLowerAsciiLocal(runtimeSkirmishOwnerForSlot(1))] = difficulty;
        for (int y = 1; y <= 28; ++y) for (int x = 1; x <= 36; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y;
            cell.cellIndex512 = runtimeCellKey(x, y); cell.tileIndex = cell.subTileIndex = 0;
            if (ore && x >= 23 && x <= 25 && y >= 14 && y <= 16)
                setRuntimeResourceTerrainBails(cell, "TIB", 12);
            s.world.accessTerrainCells().push_back(cell);
        }
        auto* humanBase = addRuntimeObjectInstanceForOwner(s, s.economy.owner,
            RuntimeBuildKind::Building, "GACNST", {30, 2});
        if (humanBase) humanBase->buildAnimTicksTotal = humanBase->buildAnimTicksRemaining = 0;
        ensureRuntimeComputerAiStates(s);
        for (auto& [key, ai] : s.computerAiByOwner) {
            ai.nextThinkTick = 100000000; // Isolate mining from item 5's tactics.
            ai.credits = 0;
        }
        return result;
    };
    const std::string aiOwner = runtimeSkirmishOwnerForSlot(1);
    const std::string aiKey = toLowerAsciiLocal(aiOwner);
    const auto add = [&](RuntimeWindowState& s, RuntimeBuildKind kind, const char* type,
                         RuntimeCellPoint cell) {
        auto* object = addRuntimeObjectInstanceForOwner(s, aiOwner, kind, type, cell);
        if (object == nullptr) return -1;
        object->buildAnimTicksTotal = object->buildAnimTicksRemaining = 0;
        object->mission = "Guard";
        object->harvestAutoSearchSuspended = false;
        return object->instanceId;
    };
    const auto step = [&](RuntimeWindowState& s) { ++s.tick; advanceRuntimeSimulation(s); };
    const auto money = [&](const RuntimeWindowState& s) { return s.computerAiByOwner.at(aiKey).credits; };
    const auto oreBails = [&](const RuntimeWindowState& s) {
        int total = 0;
        for (const auto& cell : s.world.getTerrainCells())
            if (cell.overlayIsOre || cell.overlayIsGem)
                total += runtimeResourceDensityAtCell(s, {cell.x, cell.y});
        return total;
    };
    const auto localUnchanged = [&](const RuntimeWindowState& s) {
        return s.economy.owner == runtimeSkirmishOwnerForSlot(0)
            && s.economy.credits == 12345 && s.economy.resourceTrips == 0
            && !s.computerMiningScopeActive && !s.humanHouseScopeActive;
    };
    const auto& settings = stock->rules.getComputerMiningRules();
    if (settings.virtualPurifiers != std::array<int, 3>{4, 2, 0}
        || settings.harvestersPerRefinery != std::array<int, 3>{2, 2, 1}
        || settings.slaveMinerNumber != std::array<int, 3>{4, 3, 2})
        return fail("retail mining settings not loaded");

    for (const std::string type : {"CMIN", "HARV"}) {
        for (const std::string difficulty : {"Easy", "Medium", "Hard"}) {
            auto state = makeState(type == "CMIN" ? "Americans" : "Russians", difficulty);
            auto& s = *state;
            const int refinery = add(s, RuntimeBuildKind::Building,
                type == "CMIN" ? "GAREFN" : "NAREFN", {5, 5});
            const int miner = add(s, RuntimeBuildKind::Unit, type.c_str(), {10, 10});
            if (refinery < 0 || miner < 0) return fail("stock harvester fixture");
            initializeRuntimeNavigation(s);
            if (difficulty == "Hard") {
                // A stock-map refinery can cover impassable ground. Its dock
                // remains a vehicle passage for both arrival and departure.
                const auto dock = runtimeRefineryDefaultDockCell(s, *findObjectById(s, refinery));
                s.terrainInfoByCell[runtimeCellKey(dock.x, dock.y)].passable = false;
                invalidateRuntimeResourceCaches(s);
            }
            const int beforeBails = oreBails(s);
            bool moved = false, loaded = false, saved = false;
            for (int tick = 0; tick < 7000 && money(s) == 0; ++tick) {
                step(s);
                const auto* object = findObjectById(s, miner);
                moved = moved || runtimeCellPointForObject(*object) != RuntimeCellPoint{10, 10};
                const auto task = s.harvesterTasks.find(miner);
                if (task != s.harvesterTasks.end() && task->second.cargoUnits > 0) {
                    loaded = true;
                    if (!saved && type == "HARV" && difficulty == "Medium") {
                        std::ostringstream output;
                        if (!writeRuntimeStateToStream(s, output, error)) return false;
                        IniFile ini; if (!ini.loadFromString(output.str())) return fail("save INI");
                        auto restored = std::make_unique<RuntimeWindowState>();
                        seedRuntimeLoadScratch(*restored, s, s.visionEnabled);
                        if (!materializeRuntimeLoadStateFromIni(*restored, ini, error)) return false;
                        if (restored->harvesterTasks.at(miner).cargoValue != task->second.cargoValue
                            || money(*restored) != 0 || !localUnchanged(*restored)
                            || !restored->computerMiningByOwner.contains(aiKey))
                            return fail("saved trip changed its cargo or account");
                        for (int t = 0; t < 7000 && money(*restored) == 0; ++t) step(*restored);
                        if (money(*restored) <= 0 || restored->computerMiningByOwner.at(aiKey).resourceTrips != 1)
                            return fail("loaded AI could not finish the retained trip");
                        saved = true;
                    }
                }
                if (!localUnchanged(s)) return fail("computer mining altered local player money");
            }
            const int capacity = runtimeHarvesterCargoCapacity(s, *findObjectById(s, miner));
            const int base = capacity * 25;
            const int multiplier = settings.virtualPurifiers[runtimeComputerMiningDifficultyIndex(difficulty)];
            const int expected = base + static_cast<int>(std::floor(base * stock->rules.getPurifierBonus())) * multiplier;
            if (!moved || !loaded || money(s) != expected || oreBails(s) >= beforeBails
                || s.computerMiningByOwner.at(aiKey).resourceTrips != 1
                || s.computerAiByOwner.at(aiKey).incomeEarned != expected)
                return fail(type + " " + difficulty + " actual trip: moved=" + std::to_string(moved)
                    + " loaded=" + std::to_string(loaded) + " credits=" + std::to_string(money(s))
                    + " expected=" + std::to_string(expected) + " tick=" + std::to_string(s.tick)
                    + " task=" + (s.harvesterTasks.contains(miner)
                        ? serializeRuntimeHarvesterTask(s.harvesterTasks.at(miner)) : "missing"));
            if (type == "CMIN" && s.computerMiningByOwner.at(aiKey).harvesterTeleports == 0)
                return fail("chrono miner never teleported its load home");
            const int afterFirstTripBails = oreBails(s);
            for (int tick = 0; tick < 7000
                && s.computerMiningByOwner.at(aiKey).resourceTrips < 2; ++tick) step(s);
            if (s.computerMiningByOwner.at(aiKey).resourceTrips != 2
                || money(s) != 2 * expected
                || s.computerAiByOwner.at(aiKey).incomeEarned != 2 * expected
                || oreBails(s) >= afterFirstTripBails || !localUnchanged(s))
                return fail(type + " " + difficulty + " could not leave its dock and complete a second trip: "
                    + serializeRuntimeHarvesterTask(s.harvesterTasks.at(miner)));
            const int oreValue = s.computerMiningByOwner.at(aiKey).storedResourceValue;
            debitRuntimeRepairDepotCredits(s, aiOwner, 100);
            if (money(s) != 2 * expected - 100 || s.computerMiningByOwner.at(aiKey).storedResourceValue != oreValue - 100)
                return fail("AI purchase did not consume stored ore");
            std::cout << "Computer mining trip: " << type << ' ' << difficulty
                      << " real ore/delivery=yes trips=2 credits=" << 2 * expected << '\n';
        }
    }

    // A stationary dock phase must not complete against a captured refinery.
    for (const std::string loss : {"destroyed", "captured"}) {
        auto state = makeState("Russians", "Hard", false);
        auto& s = *state;
        const int refinery = add(s, RuntimeBuildKind::Building, "NAREFN", {5, 5});
        const int miner = add(s, RuntimeBuildKind::Unit, "HARV", {8, 9});
        add(s, RuntimeBuildKind::Building, "NACNST", {2, 18});
        initializeRuntimeNavigation(s);
        auto& task = s.harvesterTasks[miner];
        task.phase = RuntimeHarvesterPhase::Unloading;
        task.targetRefineryId = refinery; task.cargoUnits = 10; task.cargoValue = 250;
        task.dockCell = runtimeCellPointForObject(*findObjectById(s, miner));
        if (loss == "destroyed") findMutableObjectById(s, refinery)->alive = false;
        else findMutableObjectById(s, refinery)->owner = s.economy.owner;
        for (int t = 0; t < 100; ++t) step(s);
        if (money(s) != 0 || s.harvesterTasks.at(miner).cargoValue != 250)
            return fail(loss + " refinery paid for undelivered cargo");
        findMutableObjectById(s, miner)->health = 0;
        findMutableObjectById(s, miner)->alive = false;
        step(s);
        if (money(s) != 0 || s.harvesterTasks.contains(miner))
            return fail("destroyed loaded miner generated money or retained cargo");
    }
    for (const std::string absent : {"ore", "miner", "refinery", "path"}) {
        auto state = makeState("Russians", "Hard", absent != "ore");
        auto& s = *state;
        add(s, RuntimeBuildKind::Building, "NACNST", {2, 2});
        if (absent != "refinery") add(s, RuntimeBuildKind::Building, "NAREFN", {5, 5});
        if (absent != "miner") add(s, RuntimeBuildKind::Unit, "HARV", {10, 10});
        initializeRuntimeNavigation(s);
        if (absent == "path") {
            for (int y = 13; y <= 17; ++y) for (int x = 22; x <= 26; ++x)
                if (x == 22 || x == 26 || y == 13 || y == 17)
                    s.terrainInfoByCell[runtimeCellKey(x, y)].passable = false;
            invalidateRuntimeResourceCaches(s);
        }
        auto& ai = s.computerAiByOwner.at(aiKey);
        // Run the ordinary AI cadence with an empty account: no base grant or
        // virtual refinery/miner pair may fund its next building.
        ai.nextThinkTick = 0; ai.nextBuildTick = 0;
        for (int t = 0; t < 2000; ++t) step(s);
        if (money(s) != 0 || ai.incomeEarned != 0 || !localUnchanged(s))
            return fail("missing " + absent + " still earned money");
    }

    // Exercise the real production choice and purchase after losing a miner.
    // A refinery's free miner and Yuri's deployed mine both count toward the
    // authored limits, so replacement cannot grow an unbounded mining fleet.
    for (const std::string country : {"Americans", "Russians", "YuriCountry"}) {
        for (const std::string difficulty : {"Easy", "Medium", "Hard"}) {
            auto state = makeState(country, difficulty, false);
            auto& s = *state;
            const bool yuri = country == "YuriCountry", soviet = country == "Russians";
            const char* mine = yuri ? "YAREFN" : soviet ? "NAREFN" : "GAREFN";
            const char* vehicle = yuri ? "SMIN" : soviet ? "HARV" : "CMIN";
            add(s, RuntimeBuildKind::Building, yuri ? "YACNST" : soviet ? "NACNST" : "GACNST", {2, 2});
            add(s, RuntimeBuildKind::Building, yuri ? "YAPOWR" : soviet ? "NAPOWR" : "GAPOWR", {9, 2});
            add(s, RuntimeBuildKind::Building, yuri ? "YABRCK" : soviet ? "NAHAND" : "GAPILE", {15, 2});
            add(s, RuntimeBuildKind::Building, mine, {2, 9});
            if (!yuri) add(s, RuntimeBuildKind::Building, mine, {2, 16});
            add(s, RuntimeBuildKind::Building, yuri ? "YAWEAP" : soviet ? "NAWEAP" : "GAWEAP", {10, 9});
            initializeRuntimeNavigation(s);
            auto& ai = s.computerAiByOwner.at(aiKey);
            ai.credits = 10000; ai.nextBuildTick = 0;
            const auto choice = runtimeComputerAiSelectNeededHarvester(s, ai, runtimeComputerAiMetrics(s, ai));
            if (!choice || choice->typeId != vehicle)
                return fail(country + " " + difficulty + " did not select its replacement miner");
            const int beforeObjects = static_cast<int>(s.world.getObjects().size());
            if (!advanceRuntimeComputerAiProduction(s, ai, runtimeComputerAiProfile(s, ai), runtimeComputerAiMetrics(s, ai))
                || ai.lastBuildType != vehicle || ai.credits != 10000 - choice->cost
                || static_cast<int>(s.world.getObjects().size()) <= beforeObjects)
                return fail("lost miner replacement did not build/pay: " + ai.status);
            const int index = runtimeComputerMiningDifficultyIndex(difficulty);
            const int quota = yuri ? settings.slaveMinerNumber[index] : 2 * settings.harvestersPerRefinery[index];
            int lastMiner = -1;
            for (int i = runtimeComputerAiMetrics(s, ai).harvesters; i < quota; ++i)
                lastMiner = add(s, RuntimeBuildKind::Unit, vehicle, {22 + i, 22});
            if (runtimeComputerAiMetrics(s, ai).harvesters != quota
                || runtimeComputerAiSelectNeededHarvester(s, ai, runtimeComputerAiMetrics(s, ai)))
                return fail(country + " exceeded its retail miner quota");
            if (lastMiner >= 0) {
                findMutableObjectById(s, lastMiner)->alive = false;
                if (!runtimeComputerAiSelectNeededHarvester(s, ai, runtimeComputerAiMetrics(s, ai)))
                    return fail("a destroyed miner still filled the quota");
            }
        }
    }

    // Yuri starts in vehicle form, drives to real ore, deploys, creates slaves,
    // and earns only after workers carry their loads inside the refinery.
    {
        auto state = makeState("YuriCountry", "Hard");
        auto& s = *state;
        const int mobile = add(s, RuntimeBuildKind::Unit, "SMIN", {8, 8});
        initializeRuntimeNavigation(s);
        bool deployed = false, carried = false;
        for (int t = 0; t < 7000 && money(s) == 0; ++t) {
            step(s);
            for (const auto& object : s.world.getObjects())
                deployed = deployed || (object.alive && object.owner == aiOwner && object.type == "YAREFN");
            for (const auto& [id, task] : s.slaveMinerTasks) carried = carried || task.cargoValue > 0;
            if (!localUnchanged(s)) return fail("AI slave miner changed human account");
        }
        if (!deployed || !carried || money(s) <= 0 || findObjectById(s, mobile) != nullptr
            || s.computerMiningByOwner.at(aiKey).resourceTrips <= 0)
            return fail("Yuri did not deploy and deliver: deployed=" + std::to_string(deployed)
                + " carried=" + std::to_string(carried) + " money=" + std::to_string(money(s)));
        const auto& ai = s.computerAiByOwner.at(aiKey);
        auto metrics = runtimeComputerAiMetrics(s, ai);
        if (metrics.harvesters != 1) return fail("deployed mine was not counted for replacement quota");
        std::cout << "Computer mining: Yuri drive/deploy/slaves/return=yes\n";
    }
    {
        auto state = makeState("Americans", "Medium");
        auto& s = *state;
        const std::string remoteOwner = runtimeSkirmishOwnerForSlot(2);
        auto remoteSlot = s.config.playerSlots.front();
        remoteSlot.local = false; remoteSlot.country = "Russians";
        s.config.playerSlots.push_back(remoteSlot);
        s.skirmishCountryByOwner[toLowerAsciiLocal(remoteOwner)] = "Russians";
        RuntimeHumanHouseAuthority local;
        local.owner = s.economy.owner; local.canonicalOwner = toLowerAsciiLocal(local.owner);
        local.country = "Americans"; local.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(local));
        RuntimeHumanHouseAuthority remote;
        remote.slotIndex = 2; remote.owner = remoteOwner;
        remote.canonicalOwner = toLowerAsciiLocal(remoteOwner); remote.country = "Russians";
        remote.remoteEconomy.emplace(); remote.remoteEconomy->owner = remoteOwner;
        remote.remoteEconomy->credits = 2468; remote.remoteTechTree.emplace();
        s.humanHouseAuthorities.push_back(std::move(remote));
        add(s, RuntimeBuildKind::Building, "GAREFN", {5, 5});
        add(s, RuntimeBuildKind::Unit, "CMIN", {10, 10});
        initializeRuntimeNavigation(s);
        for (int t = 0; t < 7000 && money(s) == 0; ++t) step(s);
        if (money(s) != 750 || !localUnchanged(s)
            || s.humanHouseAuthorities.at(1).remoteEconomy->credits != 2468)
            return fail("two human houses changed the AI mining cadence or payment owner");
    }
    {
        auto state = makeState("Russians", "Hard");
        auto& s = *state;
        add(s, RuntimeBuildKind::Building, "NAREFN", {5, 5});
        add(s, RuntimeBuildKind::Unit, "HARV", {10, 10});
        s.campaignMode = true; s.config.campaignDifficulty = "Hard";
        RuntimeCampaignHouseProductionState house;
        house.owner = aiOwner; house.country = "Russians"; house.side = 2;
        s.triggers.campaignHouseProduction[aiKey] = house;
        initializeRuntimeNavigation(s);
        for (int t = 0; t < 7000 && s.triggers.campaignHouseProduction.at(aiKey).credits == 0; ++t) step(s);
        if (s.triggers.campaignHouseProduction.at(aiKey).credits != 3000 || money(s) != 0
            || !localUnchanged(s)) return fail("campaign harvest did not reach the campaign house account");
    }
    std::cout << "Computer mining: loss/no ore/no miner/no refinery=no free income campaign=yes save/resume=yes local account=yes\n";
    return true;
} catch (const std::exception& exception) {
    error = std::string("computer mining fixture: ") + exception.what();
    return false;
}
