bool runRuntimeSlaveMinerDeliverySmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "slave delivery: " + message;
        return false;
    };
    for (const std::string mode : {"adjacent", "partial", "blocked"}) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = live.config;
        probe.config.fixedTickHz = 30;
        probe.rules = live.rules;
        probe.rulesLoaded = true;
        probe.art = live.art;
        probe.artLoaded = true;
        probe.economy.owner = "YuriCountry";
        probe.economy.credits = 0;
        probe.economy.skirmishStartApplied = true;
        probe.triggers.bootstrapComplete = true;
        probe.runtimeSuppressPlayerAudioFeedback = true;
        const RuntimeCellPoint ore{mode == "blocked" ? 6 : 7, 8};
        for (int y = 1; y <= 16; ++y) for (int x = 1; x <= 16; ++x) {
            TerrainCell cell;
            cell.x = x;
            cell.y = y;
            cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0;
            if (RuntimeCellPoint{x, y} == ore && mode != "partial")
                setRuntimeResourceTerrainBails(cell, "TIB", 12);
            probe.world.accessTerrainCells().push_back(cell);
        }
        auto* refinery = addRuntimeObjectInstance(probe, RuntimeBuildKind::Building, "YAREFN", {8, 8});
        if (refinery == nullptr) return fail("refinery fixture missing");
        const int refineryId = refinery->instanceId;
        refinery->buildAnimTicksTotal = refinery->buildAnimTicksRemaining = 0;
        auto* worker = addRuntimeObjectInstance(probe, RuntimeBuildKind::Infantry, "SLAV", ore);
        if (worker == nullptr) return fail("worker fixture missing");
        const int workerId = worker->instanceId;
        worker->mission = "HarvestLoad";
        RuntimeSlaveMinerTask task;
        task.refineryId = refineryId;
        task.targetOre = ore;
        task.cargoUnits = mode == "partial" ? 1 : 4;
        task.cargoValue = task.cargoUnits * 25;
        task.phase = mode == "partial" ? RuntimeSlaveMinerPhase::Harvesting
            : RuntimeSlaveMinerPhase::Returning;
        const int expectedValue = task.cargoValue;
        probe.slaveMinerTasks[workerId] = task;
        if (mode == "blocked") {
            for (int y = 7; y <= 10; ++y) for (int x = 7; x <= 10; ++x) {
                if (x != 7 && x != 10 && y != 7 && y != 10) continue;
                auto* wall = addRuntimeObjectInstance(probe, RuntimeBuildKind::Building, "GAWALL", {x, y});
                if (wall == nullptr) return fail("blocking wall fixture missing");
                wall->buildAnimTicksTotal = wall->buildAnimTicksRemaining = 0;
            }
        }
        initializeRuntimeNavigation(probe);
        worker = findMutableObjectById(probe, workerId);
        spawnRuntimeHarvesterGatherEffect(probe, *worker, ore);
        if (!probe.impactEffects.empty()) return fail("infantry generated the vehicle intake effect");
        advanceRuntimeSlaveMinerWorkerTask(probe, *worker, probe.slaveMinerTasks.at(workerId));
        if (probe.economy.credits != 0 || probe.slaveMinerTasks.at(workerId).cargoValue != expectedValue
            || worker->mission != "Return") return fail(mode + " credited ore before a return trip");
        if (mode == "blocked") {
            if (probe.behaviors.contains(workerId)) return fail("blocked entrance received a fallback delivery path");
            for (int tick = 0; tick < 20; ++tick)
                advanceRuntimeSlaveMinerWorkerTask(probe, *worker, probe.slaveMinerTasks.at(workerId));
            if (probe.economy.credits != 0 || probe.slaveMinerTasks.at(workerId).cargoValue != expectedValue)
                return fail("blocked entrance discarded or remotely delivered cargo");
            continue;
        }
        bool walkedBack = false;
        for (int tick = 0; tick < 500 && probe.slaveMinerTasks.at(workerId).cargoUnits > 0; ++tick) {
            ++probe.tick;
            advanceRuntimeSimulation(probe);
            worker = findMutableObjectById(probe, workerId);
            walkedBack = walkedBack || (worker->mission == "Return" && worker->renderMoveActive);
            if (probe.economy.credits > 0 && (worker->x < 8 || worker->x > 9 || worker->y < 8 || worker->y > 9))
                return fail(mode + " delivered without reaching the building");
        }
        if (!walkedBack || probe.slaveMinerTasks.at(workerId).cargoUnits != 0
            || probe.economy.credits != expectedValue) {
            const auto movement = probe.behaviors.find(workerId);
            return fail(mode + " did not carry and deliver the exact load: walked="
                + std::to_string(walkedBack) + " cargo="
                + std::to_string(probe.slaveMinerTasks.at(workerId).cargoUnits)
                + " credits=" + std::to_string(probe.economy.credits)
                + " cell=" + std::to_string(worker->x) + "," + std::to_string(worker->y)
                + " mission=" + worker->mission + " behavior="
                + (movement == probe.behaviors.end() ? "none"
                    : runtimeMultiplayerSnapshotBehaviorRecord(workerId, movement->second))
                + " simulation=" + probe.simulationStatus);
        }
        if (probe.economy.lastRefinerySmokeEffects != 4
            || probe.economy.lastRefinerySmokeImage != "SGRYSMK1")
            return fail("delivered slave ore did not produce smoke at all four chimneys");
        if (mode == "adjacent") {
            worker = findMutableObjectById(probe, workerId);
            advanceRuntimeSlaveMinerWorkerTask(probe, *worker, probe.slaveMinerTasks.at(workerId));
            const auto moving = probe.behaviors.find(workerId);
            if (worker->mission == "HarvestLoad" || moving == probe.behaviors.end()
                || moving->second.moveTarget != ore) return fail("worker dug again from inside the refinery");
        } else {
            for (int tick = 0; tick < 250; ++tick) {
                ++probe.tick;
                advanceRuntimeSimulation(probe);
            }
            worker = findMutableObjectById(probe, workerId);
            refinery = findMutableObjectById(probe, refineryId);
            if (runtimeSlaveMinerWorkerAtRefinery(probe, *worker, *refinery)
                || probe.economy.credits != expectedValue)
                return fail("empty worker did not leave the depleted refinery after unloading");
        }
        WorldObjectInstance vehicle = *worker;
        vehicle.kind = WorldObjectKind::Unit;
        vehicle.type = "CMIN";
        spawnRuntimeHarvesterGatherEffect(probe, vehicle, ore);
        if (probe.impactEffects.size() != 1U || probe.impactEffects.front().animId != "OREGATH")
            return fail("normal mining vehicles lost their intake animation");
    }
    std::cout << "Slave delivery: adjacent ore return=ok partial load=ok blocked entry=ok no intake=ok vehicle intake=ok\n";
    return true;
}

bool runRuntimeSlaveMinerEmergenceSmokeProbe(RuntimeWindowState& live, std::string& error) {
    for (const std::string mode : {"ore", "no ore", "blocked"}) {
        const auto fail = [&](const std::string& message) {
            error = "slave emergence (" + mode + "): " + message;
            return false;
        };
        auto fixture = std::make_unique<RuntimeWindowState>();
        fixture->config = live.config;
        fixture->config.fixedTickHz = 30;
        fixture->config.computerAiEnabled = false;
        fixture->rules = live.rules;
        fixture->rulesLoaded = true;
        fixture->art = live.art;
        fixture->artLoaded = true;
        fixture->economy.owner = "YuriCountry";
        fixture->economy.skirmishStartApplied = true;
        fixture->triggers.bootstrapComplete = true;
        fixture->runtimeSuppressPlayerAudioFeedback = true;
        for (int y = 1; y <= 20; ++y) for (int x = 1; x <= 20; ++x) {
            TerrainCell cell;
            cell.x = x;
            cell.y = y;
            cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0;
            if (mode != "no ore" && x >= 12 && x <= 14 && y >= 8 && y <= 10)
                setRuntimeResourceTerrainBails(cell, "TIB", 12);
            fixture->world.accessTerrainCells().push_back(cell);
        }
        auto* refinery = addRuntimeObjectInstance(*fixture, RuntimeBuildKind::Building, "YAREFN", {8, 8});
        if (refinery == nullptr) return fail("refinery fixture missing");
        const int refineryId = refinery->instanceId;
        refinery->buildAnimTicksTotal = refinery->buildAnimTicksRemaining = 0;
        int openingWallId = -1;
        if (mode == "blocked") {
            for (int y = 7; y <= 10; ++y) for (int x = 7; x <= 10; ++x) {
                if (x != 7 && x != 10 && y != 7 && y != 10) continue;
                auto* wall = addRuntimeObjectInstance(*fixture, RuntimeBuildKind::Building, "GAWALL", {x, y});
                if (wall == nullptr) return fail("wall fixture missing");
                wall->buildAnimTicksTotal = wall->buildAnimTicksRemaining = 0;
                if (x == 10 && y == 8) openingWallId = wall->instanceId;
            }
        }
        initializeRuntimeNavigation(*fixture);
        const auto* building = findRuntimeBuildingTypeById(fixture->rules, "YAREFN");
        if (building == nullptr || !ensureRuntimeSlaveMinerWorkers(
                *fixture, *findObjectById(*fixture, refineryId), *building))
            return fail("no initial workers");
        const int wanted = building->slavesNumber;
        std::map<int, RuntimeCellPoint> lastCells;
        for (const auto& [id, task] : fixture->slaveMinerTasks) {
            const auto* worker = findObjectById(*fixture, id);
            if (worker == nullptr || task.refineryId != refineryId
                || !runtimeSlaveMinerWorkerAtRefinery(*fixture, *worker, *findObjectById(*fixture, refineryId)))
                return fail("initial worker appeared outside the building");
            lastCells[id] = runtimeCellPointForObject(*worker);
        }
        // Loading a worker before its first movement must not invoke generic
        // infantry overlap relocation and put it on the surrounding ground.
        std::ostringstream saved;
        IniFile ini;
        auto storage = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*storage, *fixture, false);
        if (!writeRuntimeStateToStream(*fixture, saved, error)
            || !ini.loadFromString(saved.str())
            || !materializeRuntimeLoadStateFromIni(*storage, ini, error))
            return fail("interior save/load: " + error);
        auto& probe = *storage;
        RuntimeSnapshotActiveStateScope active(probe);
        probe.runtimeSuppressPlayerAudioFeedback = true;
        if (probe.slaveMinerTasks.size() != lastCells.size()) return fail("load changed worker count");
        for (const auto& [id, cell] : lastCells) {
            const auto* worker = findObjectById(probe, id);
            if (worker == nullptr || runtimeCellPointForObject(*worker) != cell)
                return fail("load relocated an interior worker");
        }
        std::unordered_set<int> emerged;
        const auto inspect = [&](bool mustStayInside) {
            const auto* host = findObjectById(probe, refineryId);
            if (host == nullptr || !host->alive) return fail("refinery disappeared");
            for (const auto& [id, task] : probe.slaveMinerTasks) {
                const auto* worker = findObjectById(probe, id);
                if (worker == nullptr || !worker->alive || task.refineryId != refineryId)
                    return fail("worker identity or parent lost");
                const auto cell = runtimeCellPointForObject(*worker);
                const bool inside = runtimeSlaveMinerWorkerAtRefinery(probe, *worker, *host);
                const auto previous = lastCells.find(id);
                if (previous == lastCells.end()) {
                    const bool emerging = worker->renderMoveActive && worker->hasRenderPosition
                        && worker->renderX >= 8.0 && worker->renderX <= 9.0
                        && worker->renderY >= 8.0 && worker->renderY <= 9.0;
                    if (!inside && !emerging) return fail("new worker appeared on surrounding ground");
                } else {
                    if (std::abs(cell.x - previous->second.x) > 1
                        || std::abs(cell.y - previous->second.y) > 1)
                        return fail("worker teleported instead of walking");
                    if (!inside && runtimeCellDistanceToObjectFootprint(probe, previous->second, *host) == 0.0
                        && (!worker->renderMoveActive || !worker->hasRenderPosition))
                        return fail("worker left without interpolated walk animation");
                }
                if (mustStayInside && !inside) return fail("worker escaped through a closed wall ring");
                if (!inside) emerged.insert(id);
                lastCells[id] = cell;
            }
            return true;
        };
        const auto step = [&]() { ++probe.tick; advanceRuntimeSimulation(probe); };
        if (mode == "blocked") {
            for (int tick = 0; tick < 120; ++tick) {
                step();
                if (!inspect(true)) return false;
            }
            RuntimeObjectExitRequest request;
            request.objectId = openingWallId;
            request.kind = RuntimeObjectExitKind::Destroyed;
            request.tick = ++probe.tick;
            request.suppressCrew = request.suppressDeathWeapon = true;
            if (!finalizeRuntimeObjectExit(probe, request).accepted) return fail("wall opening failed");
        }
        for (int tick = 0; tick < 1500 && emerged.size() < static_cast<std::size_t>(wanted); ++tick) {
            step();
            if (!inspect(false)) return false;
        }
        if (emerged.size() != static_cast<std::size_t>(wanted)
            || probe.slaveMinerTasks.size() != static_cast<std::size_t>(wanted))
            return fail("not all workers walked out: " + std::to_string(emerged.size()));
        if (mode == "ore") {
            const int lostId = *emerged.begin();
            RuntimeObjectExitRequest request;
            request.objectId = lostId;
            request.kind = RuntimeObjectExitKind::Destroyed;
            request.tick = ++probe.tick;
            request.suppressCrew = request.suppressDeathWeapon = true;
            if (!finalizeRuntimeObjectExit(probe, request).accepted) return fail("worker loss fixture failed");
            cleanRuntimeSlaveMinerTasks(probe);
            if (probe.slaveMinerTasks.contains(lostId)
                || probe.slaveMinerTasks.size() != static_cast<std::size_t>(wanted - 1))
                return fail("lost worker remained in the active group");
            const auto initialIds = lastCells;
            int replacementId = -1;
            for (int tick = 0; tick < 2000; ++tick) {
                step();
                if (!inspect(false)) return false;
                for (const auto& [id, task] : probe.slaveMinerTasks)
                    if (!initialIds.contains(id)) replacementId = id;
                if (replacementId >= 0 && emerged.contains(replacementId)) break;
            }
            if (replacementId < 0 || !emerged.contains(replacementId)
                || probe.slaveMinerTasks.size() != static_cast<std::size_t>(wanted))
                return fail("replacement worker did not emerge normally");
        }
        std::cout << "Slave emergence: " << mode
            << " interior spawn/save/load=ok continuous walk=ok workers=" << wanted
            << (mode == "ore" ? " replacement=ok" : mode == "blocked" ? " closed/reopened exit=ok" : "") << '\n';
    }
    return true;
}

bool runRuntimeSlaveMinerSmokeProbe(RuntimeWindowState& live, std::string& error) {
    if (!runRuntimeSlaveMinerEmergenceSmokeProbe(live, error)) return false;
    if (!runRuntimeSlaveMinerDeliverySmokeProbe(live, error)) return false;
    const auto fail = [&](const std::string& message) {
        error = "slave miner: " + message;
        return false;
    };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& probe = *storage;
    probe.config = live.config;
    probe.config.fixedTickHz = 30;
    probe.config.mcvRepacks = false; // This option only limits construction yards.
    probe.rules = live.rules;
    probe.rulesLoaded = true;
    probe.art = live.art;
    probe.artLoaded = true;
    probe.economy.owner = "YuriCountry";
    probe.economy.credits = 0;
    probe.economy.skirmishStartApplied = true;
    probe.triggers.bootstrapComplete = true;
    probe.runtimeSuppressPlayerAudioFeedback = true;
    const RuntimeCellPoint firstOre{24, 12}, secondOre{5, 21}, start{4, 4};
    for (int y = 1; y <= 28; ++y) for (int x = 1; x <= 32; ++x) {
        TerrainCell cell;
        cell.x = x;
        cell.y = y;
        cell.cellIndex512 = runtimeCellKey(x, y);
        cell.tileIndex = 0;
        cell.subTileIndex = 0;
        if (x >= firstOre.x && x < firstOre.x + 3 && y >= firstOre.y && y < firstOre.y + 3)
            setRuntimeResourceTerrainBails(cell, "TIB", 12);
        probe.world.accessTerrainCells().push_back(cell);
    }
    auto* miner = addRuntimeObjectInstance(probe, RuntimeBuildKind::Unit, "SMIN", start);
    if (miner == nullptr) return fail("SMIN missing");
    const int mobileId = miner->instanceId;
    miner->mission = "Guard";
    miner->buildAnimTicksTotal = miner->buildAnimTicksRemaining = 0;
    initializeRuntimeNavigation(probe);
    applySelection(probe, {mobileId}, false, false);
    const RuntimeCellPoint groundMove{8, 4};
    issueRuntimeMoveCommand(probe, groundMove);
    if (probe.lastCommand.acceptedCount != 1) return fail("mobile miner rejected a normal ground move");
    bool reachedGroundMove = false;
    for (int tick = 0; tick < 500; ++tick) {
        ++probe.tick;
        advanceRuntimeSimulation(probe);
        const auto* moving = findObjectById(probe, mobileId);
        if (moving != nullptr && runtimeCellPointForObject(*moving) == groundMove) {
            reachedGroundMove = true;
            break;
        }
    }
    if (!reachedGroundMove) return fail("mobile miner did not drive to the commanded ground cell");
    const auto findRefinery = [&]() -> const WorldObjectInstance* {
        for (const auto& object : probe.world.getObjects())
            if (object.alive && object.kind == WorldObjectKind::Structure && object.type == "YAREFN")
                return &object;
        return nullptr;
    };
    const auto step = [&]() { ++probe.tick; advanceRuntimeSimulation(probe); };
    for (int tick = 0; tick < 240; ++tick) step();
    if (findRefinery() != nullptr || findObjectById(probe, mobileId) == nullptr
        || runtimeCellPointForObject(*findObjectById(probe, mobileId)) != groundMove
        || !findObjectById(probe, mobileId)->harvestAutoSearchSuspended)
        return fail("mobile miner automatically left the commanded ground cell");
    if (!checkRuntimeHarvesterManualMoveSave(probe, mobileId, error)) return false;
    issueRuntimeMoveCommand(probe, firstOre);
    if (probe.lastCommand.acceptedCount != 1
        || findObjectById(probe, mobileId)->harvestAutoSearchSuspended)
        return fail("mobile miner did not resume after an explicit ore command");
    bool moved = false;
    RuntimeCellPoint lastMobileCell = start;
    for (int tick = 0; tick < 1800 && findRefinery() == nullptr; ++tick) {
        if (const auto* mobile = findObjectById(probe, mobileId)) {
            lastMobileCell = runtimeCellPointForObject(*mobile);
            moved = moved || lastMobileCell != start;
        }
        step();
    }
    const auto* refinery = findRefinery();
    if (!moved || refinery == nullptr || runtimeCellPointForObject(*refinery) != lastMobileCell)
        return fail("automatic mining did not drive to the deployment cell before transforming: "
            + probe.economy.status);
    const int firstRefineryId = refinery->instanceId;
    if (runtimeCellDistanceToObjectFootprint(probe, firstOre, *refinery) > 1.5
        || refinery->owner != probe.economy.owner)
        return fail("deployed refinery is not beside the ore or lost its owner");
    bool digging = false, carrying = false;
    int maxCargo = 0;
    for (int tick = 0; tick < 3500 && (!digging || !carrying || probe.economy.credits <= 0); ++tick) {
        step();
        for (const auto& effect : probe.impactEffects) {
            if (effect.animId == "OREGATH" && probe.slaveMinerTasks.contains(effect.attackerObjectId))
                return fail("digging worker emitted the vehicle suction animation");
        }
        for (const auto& [id, task] : probe.slaveMinerTasks) {
            const auto* worker = findRuntimeObjectByIdIncludingInactive(probe, id);
            if (worker == nullptr) return fail("worker task has no worker");
            if (task.phase == RuntimeSlaveMinerPhase::Stored) {
                if (worker->alive || worker->health <= 0 || worker->mission != "InsideSlaveMiner")
                    return fail("stored worker lost its living containment state");
                continue;
            }
            if (!worker->alive) return fail("active worker task has no live worker");
            digging = digging || (worker->mission == "HarvestLoad" && !worker->renderMoveActive);
            carrying = carrying || (worker->mission == "Return" && worker->renderMoveActive && task.cargoUnits > 0);
            maxCargo = std::max(maxCargo, task.cargoUnits);
        }
    }
    const auto* building = findRuntimeBuildingTypeById(probe.rules, "YAREFN");
    if (!digging || !carrying || maxCargo != 4 || probe.economy.credits <= 0
        || probe.slaveMinerTasks.size() != static_cast<std::size_t>(building->slavesNumber))
        return fail("workers did not dig, carry full loads and deposit credits: digging="
            + std::to_string(digging) + " carrying=" + std::to_string(carrying)
            + " cargo=" + std::to_string(maxCargo) + " credits=" + std::to_string(probe.economy.credits));

    const int secondOreFixtureTick = probe.tick;
    for (auto& cell : probe.world.accessTerrainCells())
        if (cell.x >= secondOre.x && cell.x < secondOre.x + 3
            && cell.y >= secondOre.y && cell.y < secondOre.y + 3)
            setRuntimeResourceTerrainBails(cell, "TIB", 12);
    invalidateRuntimeResourceCaches(probe);
    std::map<int, int> workerCargo;
    for (const auto& [id, task] : probe.slaveMinerTasks) workerCargo[id] = task.cargoUnits;
    auto* damagedRefinery = findMutableObjectById(probe, firstRefineryId);
    damagedRefinery->health = 700;
    damagedRefinery->veterancy = 1;
    damagedRefinery->veterancyExperience = 55;
    applySelection(probe, {firstRefineryId}, false, false);
    const RuntimeCellPoint relocationGround{5, 17};
    issueRuntimeMoveCommand(probe, relocationGround);
    if (probe.lastCommand.acceptedCount != 1 || probe.lastCommand.rejectedCount != 0
        || findRefinery() != nullptr || probe.selectedObjectIds.size() != 1)
        return fail("building move command did not repack into a selected mobile miner");
    const int relocatedId = probe.selectedObjectIds.front();
    const auto* relocated = findObjectById(probe, relocatedId);
    if (relocated == nullptr || relocated->type != "SMIN" || relocated->health != 700
        || !relocated->harvestAutoSearchSuspended
        || relocated->veterancy != 1 || relocated->veterancyExperience != 55)
        return fail("repacking lost health/veterancy");
    cleanRuntimeSlaveMinerTasks(probe);
    for (const auto& [id, cargo] : workerCargo) {
        const auto task = probe.slaveMinerTasks.find(id);
        if (task == probe.slaveMinerTasks.end() || task->second.refineryId != relocatedId
            || task->second.cargoUnits != cargo) return fail("repacking released or emptied a worker");
        const auto restored = parseRuntimeSlaveMinerTask(serializeRuntimeSlaveMinerTask(task->second));
        if (!restored || restored->refineryId != relocatedId || restored->cargoUnits != cargo)
            return fail("mobile parent/cargo did not survive task save round trip");
    }
    const int mobileCreditsBefore = probe.economy.credits;
    const int relocationFixtureTick = probe.tick;
    for (int tick = 0; tick < 2000 && probe.behaviors.contains(relocatedId); ++tick) step();
    for (int tick = 0; tick < 240; ++tick) step();
    const auto allStored = [&]() {
        if (probe.slaveMinerTasks.size() != workerCargo.size()) return false;
        for (const auto& [id, cargo] : workerCargo) {
            (void)cargo;
            const auto task = probe.slaveMinerTasks.find(id);
            if (task == probe.slaveMinerTasks.end() || task->second.refineryId != relocatedId
                || task->second.phase != RuntimeSlaveMinerPhase::Stored) return false;
        }
        return true;
    };
    for (int tick = 0; tick < 3500 && !allStored(); ++tick) step();
    const auto* parked = findObjectById(probe, relocatedId);
    if (findRefinery() != nullptr || parked == nullptr
        || runtimeCellPointForObject(*parked) != relocationGround
        || !parked->harvestAutoSearchSuspended)
        return fail("repacked miner did not stay at its ground destination");
    if (!allStored() || probe.economy.credits <= mobileCreditsBefore)
        return fail("outside workers did not finish mining, deliver and enter the mobile miner");
    const int storedFixtureTick = probe.tick;
    if (!validateRuntimeStoredSlaveMinerState(probe, &error)) return false;
    if (!checkRuntimeHarvesterManualMoveSave(probe, relocatedId, error)) return false;
    // Pack/deploy and save/load must not silently override the ground order.
    if (!executeRuntimeSecondaryAction(probe, relocatedId) || findRefinery() == nullptr
        || !findRefinery()->harvestAutoSearchSuspended)
        return fail("explicit deployment lost the suspended mining state");
    const int parkedRefineryId = findRefinery()->instanceId;
    const int parkedCredits = probe.economy.credits;
    const int redeployFixtureTick = probe.tick;
    for (int tick = 0; tick < 3500 && probe.economy.credits <= parkedCredits; ++tick) step();
    if (probe.economy.credits <= parkedCredits)
        return fail("deployment did not naturally resume the stored workers' collection/delivery");
    const int resumedDeliveryFixtureTick = probe.tick;
    if (!checkRuntimeHarvesterManualMoveSave(probe, parkedRefineryId, error)) return false;
    for (const auto& [id, cargo] : workerCargo) {
        (void)cargo;
        const auto task = probe.slaveMinerTasks.find(id);
        const auto* worker = findObjectById(probe, id);
        if (task == probe.slaveMinerTasks.end() || task->second.refineryId != parkedRefineryId
            || worker == nullptr || worker->owner != probe.economy.owner
            || task->second.cargoUnits < 0 || task->second.cargoUnits > 4)
            return fail("redeployment lost worker identity, owner or cargo capacity");
    }
    // The preceding real return/redeploy collection can consume the twelve
    // bails at this one clicked cell while neighboring fixture cells still
    // contain ore. This next independent ore-command phase needs an actual
    // resource under its cursor, rather than a now empty ground-move target.
    auto* commandOre = findMutableRuntimeTerrainCellAt(probe, secondOre);
    if (commandOre == nullptr) return fail("second ore command terrain fixture missing");
    const int commandOreBailsBefore = runtimeResourceBailCountForTerrain(*commandOre);
    int commandRegionBailsBefore = 0;
    for (const auto& cell : probe.world.getTerrainCells())
        if (cell.x >= secondOre.x && cell.x < secondOre.x + 3
            && cell.y >= secondOre.y && cell.y < secondOre.y + 3)
            commandRegionBailsBefore += runtimeResourceBailCountForTerrain(cell);
    if (commandOreBailsBefore <= 0) {
        setRuntimeResourceTerrainBails(*commandOre, "TIB", 12);
        invalidateRuntimeResourceCaches(probe);
    }
    std::cout << "Slave miner ore-command fixture: tick=" << probe.tick
        << " clickedBailsBefore=" << commandOreBailsBefore
        << " regionBailsBefore=" << commandRegionBailsBefore
        << " clickedBails=" << runtimeResourceBailCountForTerrain(*commandOre)
        << " timeline=" << secondOreFixtureTick << ',' << relocationFixtureTick
        << ',' << storedFixtureTick << ',' << redeployFixtureTick << ','
        << resumedDeliveryFixtureTick << '\n';
    applySelection(probe, {parkedRefineryId}, false, false);
    issueRuntimeMoveCommand(probe, secondOre);
    const auto* commandedMiner = probe.selectedObjectIds.empty() ? nullptr
        : findObjectById(probe, probe.selectedObjectIds.front());
    if (probe.lastCommand.acceptedCount != 1 || commandedMiner == nullptr
        || commandedMiner->harvestAutoSearchSuspended) {
        std::ostringstream detail;
        detail << "building ore command failed to resume the parked miner: accepted="
            << probe.lastCommand.acceptedCount << " rejected=" << probe.lastCommand.rejectedCount
            << " tick=" << probe.tick << " target=" << secondOre.x << ',' << secondOre.y
            << " bails=" << runtimeResourceBailCountForTerrain(*commandOre)
            << " before=" << commandOreBailsBefore << " regionBefore=" << commandRegionBailsBefore
            << " selected=" << probe.selectedObjectIds.size() << " homeBefore=" << parkedRefineryId;
        if (commandedMiner) detail << " miner=" << commandedMiner->type << '#' << commandedMiner->instanceId
            << " alive=" << commandedMiner->alive << " hp=" << commandedMiner->health
            << " paused=" << commandedMiner->harvestAutoSearchSuspended
            << " mission=" << commandedMiner->mission << " cell=" << commandedMiner->x << ',' << commandedMiner->y
            << " moving=" << commandedMiner->renderMoveActive;
        for (const auto& [id, task] : probe.slaveMinerTasks)
            detail << " worker#" << id << '=' << serializeRuntimeSlaveMinerTask(task);
        detail << " timeline=" << secondOreFixtureTick << ',' << relocationFixtureTick
            << ',' << storedFixtureTick << ',' << redeployFixtureTick << ',' << resumedDeliveryFixtureTick;
        return fail(detail.str());
    }
    for (int tick = 0; tick < 2000 && findRefinery() == nullptr; ++tick) step();
    refinery = findRefinery();
    if (refinery == nullptr || runtimeCellDistanceToObjectFootprint(probe, secondOre, *refinery) > 1.5)
        return fail("repacked miner did not drive to the commanded deposit and redeploy: " + probe.economy.status);
    const int secondRefineryId = refinery->instanceId;
    // A direct transform must preserve damage; normal self-healing while moving is allowed.
    auto* completedRefinery = findMutableObjectById(probe, secondRefineryId);
    completedRefinery->buildAnimTicksTotal = completedRefinery->buildAnimTicksRemaining = 0;
    completedRefinery->health = 700;
    applySelection(probe, {secondRefineryId}, false, false);
    if (selectedMovableObjectCount(probe) != 1
        || runtimeSecondaryAbilityForObject(probe, *completedRefinery) != RuntimeSecondaryAbility::Undeploy
        || !executeRuntimeSecondaryAction(probe, secondRefineryId) || findRefinery() != nullptr
        || probe.selectedObjectIds.size() != 1U)
        return fail("building form did not expose its move / pack-up actions");
    const int roundTripId = probe.selectedObjectIds.front();
    const auto* packed = findObjectById(probe, roundTripId);
    if (packed == nullptr || packed->type != "SMIN"
        || runtimeSecondaryAbilityForObject(probe, *packed) != RuntimeSecondaryAbility::Deploy
        || !executeRuntimeSecondaryAction(probe, roundTripId))
        return fail("mobile form did not expose its deploy action");
    refinery = findRefinery();
    if (refinery == nullptr || refinery->health != 700) return fail("deployment healed the miner");
    const int finalRefineryId = refinery->instanceId;
    const int creditsBefore = probe.economy.credits;
    for (int tick = 0; tick < 3500 && probe.economy.credits <= creditsBefore; ++tick) step();
    if (probe.economy.credits <= creditsBefore || probe.slaveMinerTasks.size() != workerCargo.size())
        return fail("workers failed to resume delivery after relocation");
    int liveWorkers = 0;
    for (const auto& object : probe.world.getObjects())
        liveWorkers += object.alive && object.kind == WorldObjectKind::Infantry && object.type == "SLAV";
    if (liveWorkers != building->slavesNumber) return fail("redeployment duplicated workers");
    for (const auto& [id, cargo] : workerCargo) {
        (void)cargo;
        const auto task = probe.slaveMinerTasks.find(id);
        if (task == probe.slaveMinerTasks.end() || task->second.refineryId != finalRefineryId)
            return fail("redeployment replaced the original worker identities");
    }

    // A nearby but inaccessible deposit must never trigger a remote placement.
    auto* blockedProbe = addRuntimeObjectInstance(probe, RuntimeBuildKind::Unit, "SMIN", {30, 3});
    if (blockedProbe == nullptr) return fail("blocked-site probe could not be created");
    const RuntimeCellPoint cliffOre{30, 7};
    auto* cliff = findMutableRuntimeTerrainCellAt(probe, cliffOre);
    cliff->level = 4;
    setRuntimeResourceTerrainBails(*cliff, "TIB", 12);
    invalidateRuntimeResourceCaches(probe);
    if (findRuntimeSlaveMinerDeployCellNearOre(probe, *blockedProbe, cliffOre).has_value())
        return fail("accepted a deployment below inaccessible cliff ore");

    RuntimeMapRenderContext render;
    render.rules = &live.rules;
    render.art = &live.art;
    render.assetRootPath = runtimeAssetRootPath(live.config);
    render.palettePath = runtimePalettePath(live.config, live.report.map);
    render.structurePalettePath = runtimeStructurePalettePath(live.config, live.report.map);
    if (!runRuntimeSlaveMinerRenderContractCheck(render, error)) return false;
    std::cout << "Slave miner: explicitOreDrive=ok exactDeploy=ok buildingMove=ok "
        "groundHold=ok mobileDelivery/stored=ok naturalWorkerResume=ok suspendedParent/save=ok "
        "dig/carry/deliver=ok workers=" << liveWorkers
        << " cargo/owner/health/veterancy=ok taskSave=ok blockedOre=ok\n";
    return true;
}
