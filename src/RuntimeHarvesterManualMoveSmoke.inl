// Exercise the real save writer and object loader, including old records.
bool checkRuntimeHarvesterManualMoveSave(RuntimeWindowState& state, int objectId,
    std::string& error) {
    const auto* object = findObjectById(state, objectId);
    if (object == nullptr) { error = "missing miner for save"; return false; }
    const WorldObjectInstance savedMiner = *object;
    const auto* slaveManagerRules = runtimeSlaveMinerParentBuilding(state, savedMiner);
    const bool slaveManager = slaveManagerRules != nullptr && slaveManagerRules->slavesNumber > 0;
    const auto body = [](const WorldObjectInstance& item) {
        return std::array<double, 3>{item.hasRenderPosition ? item.renderX : item.x,
            item.hasRenderPosition ? item.renderY : item.y,
            item.hasRenderPosition ? item.renderLevel : item.level};
    };
    const bool suspended = object->harvestAutoSearchSuspended;
    const auto commandHash = runtimePersistentTransientStateHash(state);
    findMutableObjectById(state, objectId)->harvestAutoSearchSuspended = !suspended;
    const auto changedHash = runtimePersistentTransientStateHash(state);
    findMutableObjectById(state, objectId)->harvestAutoSearchSuspended = suspended;
    if (changedHash == commandHash) { error = "miner command absent from saved state hash"; return false; }
    std::ostringstream saved;
    if (!writeRuntimeStateToStream(state, saved, error)) return false;
    IniFile ini;
    if (!ini.loadFromString(saved.str())) { error = "miner save INI"; return false; }
    auto loaded = std::make_unique<RuntimeWindowState>();
    seedRuntimeLoadScratch(*loaded, state, state.visionEnabled);
    if (!materializeRuntimeLoadStateFromIni(*loaded, ini, error)) {
        error = "miner full save materialization: " + error; return false;
    }
    const auto* loadedMiner = findObjectById(*loaded, objectId);
    if (loadedMiner == nullptr || loadedMiner->harvestAutoSearchSuspended != suspended) {
        error = "full save lost the miner command state"; return false;
    }
    if (slaveManager) {
        const auto savedBody = body(savedMiner), loadedBody = body(*loadedMiner);
        if (loadedMiner->instanceId != savedMiner.instanceId || loadedMiner->kind != savedMiner.kind
            || loadedMiner->type != savedMiner.type || loadedMiner->owner != savedMiner.owner
            || loadedMiner->alive != savedMiner.alive || loadedMiner->health != savedMiner.health
            || loadedMiner->x != savedMiner.x || loadedMiner->y != savedMiner.y
            || loadedMiner->cell != savedMiner.cell || loadedMiner->level != savedMiner.level
            || loadedMiner->onBridge != savedMiner.onBridge || loadedMiner->airborne != savedMiner.airborne
            || loadedMiner->renderMoveActive != savedMiner.renderMoveActive
            || std::abs(savedBody[0] - loadedBody[0]) > 1e-8
            || std::abs(savedBody[1] - loadedBody[1]) > 1e-8
            || std::abs(savedBody[2] - loadedBody[2]) > 1e-8) {
            const auto describe = [&](const WorldObjectInstance& item) {
                const auto position = body(item);
                std::ostringstream detail;
                detail << std::setprecision(17) << "id=" << item.instanceId
                    << " kind=" << static_cast<int>(item.kind) << " type=" << item.type
                    << " owner=" << item.owner << " alive=" << item.alive << " hp=" << item.health
                    << " logical=" << item.x << ',' << item.y << ',' << item.level << " cell=" << item.cell
                    << " bridge=" << item.onBridge << " airborne=" << item.airborne
                    << " render=" << item.hasRenderPosition << " active=" << item.renderMoveActive
                    << " body=" << position[0] << ',' << position[1] << ',' << position[2];
                return detail.str();
            };
            error = "full save changed the slave manager identity, health or body: "
                + describe(savedMiner) + " -> " + describe(*loadedMiner);
            const int objectCount = parseRuntimeIntOrDefault(ini.getValue("Objects", "Count", "0"), 0);
            for (int index = 0; index < objectCount; ++index) {
                const auto record = ini.getValue("Objects", std::to_string(index), "");
                const auto fields = splitRuntimeSaveFields(record);
                if (fields.size() >= 2 && parseRuntimeIntOrDefault(fields[1], -1) == objectId) {
                    error += " savedObject=" + record;
                    break;
                }
            }
            return false;
        }
        int originalTasks = 0, restoredTasks = 0;
        for (const auto& [id, task] : state.slaveMinerTasks) {
            if (task.refineryId != objectId) continue;
            ++originalTasks;
            const auto found = loaded->slaveMinerTasks.find(id);
            const auto* before = findRuntimeObjectByIdIncludingInactive(state, id);
            const auto* after = findRuntimeObjectByIdIncludingInactive(*loaded, id);
            if (found == loaded->slaveMinerTasks.end()
                || serializeRuntimeSlaveMinerTask(found->second) != serializeRuntimeSlaveMinerTask(task)
                || before == nullptr || after == nullptr || before->instanceId != after->instanceId
                || before->kind != after->kind || before->type != after->type || before->owner != after->owner
                || before->alive != after->alive || before->health != after->health
                || before->x != after->x || before->y != after->y || before->cell != after->cell
                || before->level != after->level || before->onBridge != after->onBridge
                || before->subCell != after->subCell || before->mission != after->mission
                || before->facing != after->facing || before->veterancy != after->veterancy
                || before->veterancyExperience != after->veterancyExperience
                || before->harvestAutoSearchSuspended != after->harvestAutoSearchSuspended
                || before->hasRenderPosition != after->hasRenderPosition
                || before->renderMoveActive != after->renderMoveActive) {
                error = "full save changed retained slave #" + std::to_string(id)
                    + " identity/task/cargo: " + serializeRuntimeSlaveMinerTask(task)
                    + " -> " + (found == loaded->slaveMinerTasks.end() ? "missing"
                        : serializeRuntimeSlaveMinerTask(found->second)); return false;
            }
            const auto beforeBody = body(*before), afterBody = body(*after);
            if (std::abs(beforeBody[0] - afterBody[0]) > 1e-8
                || std::abs(beforeBody[1] - afterBody[1]) > 1e-8
                || std::abs(beforeBody[2] - afterBody[2]) > 1e-8) {
                std::ostringstream detail;
                detail << std::setprecision(17) << "full save changed slave #" << id << " actual body "
                    << beforeBody[0] << ',' << beforeBody[1] << ',' << beforeBody[2] << " -> "
                    << afterBody[0] << ',' << afterBody[1] << ',' << afterBody[2];
                error = detail.str(); return false;
            }
        }
        for (const auto& [id, task] : loaded->slaveMinerTasks) {
            (void)id; restoredTasks += task.refineryId == objectId;
        }
        if (originalTasks != restoredTasks) {
            error = "full save changed the slave manager worker count"; return false;
        }
    }
    const auto savedTask = state.harvesterTasks.find(objectId);
    if (savedTask != state.harvesterTasks.end()) {
        const auto loadedTask = loaded->harvesterTasks.find(objectId);
        if (loadedTask == loaded->harvesterTasks.end()
            || loadedTask->second.cargoUnits != savedTask->second.cargoUnits
            || loadedTask->second.cargoValue != savedTask->second.cargoValue) {
            error = "full save changed retained miner cargo"; return false;
        }
    }
    if (suspended && !state.behaviors.contains(objectId)) {
        const auto parkedCell = runtimeCellPointForObject(*loadedMiner);
        const int parkedCredits = loaded->economy.credits;
        const WorldObjectInstance parkedMiner = *loadedMiner;
        const auto parkedBody = body(parkedMiner);
        const auto* parkedUnitRules = slaveManager ? findRuntimeUnitType(loaded->rules, parkedMiner) : nullptr;
        const int parkedHealthLimit = slaveManager ? std::max(1,
            parkedUnitRules ? parkedUnitRules->strength : slaveManagerRules->strength) : parkedMiner.health;
        int childDeposits = 0;
        for (int tick = 0; tick < 120; ++tick) {
            std::map<int, RuntimeSlaveMinerTask> beforeTasks;
            std::map<int, WorldObjectInstance> beforeWorkers;
            const int beforeCredits = loaded->economy.credits;
            const int beforeTrips = loaded->economy.resourceTrips;
            int purifierCount = 0;
            if (slaveManager) {
                for (const auto& [id, task] : loaded->slaveMinerTasks) {
                    if (task.refineryId != objectId) continue;
                    const auto* worker = findRuntimeObjectByIdIncludingInactive(*loaded, id);
                    if (!worker) { error = "saved slave manager has a missing worker"; return false; }
                    beforeTasks.emplace(id, task);
                    beforeWorkers.emplace(id, *worker);
                }
                if (!runtimeEconomyLowPower(*loaded)) for (const auto& item : loaded->world.getObjects()) {
                    const auto* rules = item.kind == WorldObjectKind::Structure
                        ? findRuntimeBuildingTypeById(loaded->rules, item.type) : nullptr;
                    if (item.alive && item.owner == loaded->economy.owner && rules && rules->orePurifier)
                        ++purifierCount;
                }
                // These local native fixtures have no computer virtual income.
                if (loaded->computerMiningScopeActive || loaded->economy.simplifiedHarvesterIncomeEnabled) {
                    error = "saved slave manager fixture has unrelated virtual income"; return false;
                }
            }
            ++loaded->tick;
            advanceRuntimeSimulation(*loaded);
            if (slaveManager) {
                loadedMiner = findObjectById(*loaded, objectId);
                if (!loadedMiner) { error = "loaded parked slave manager disappeared"; return false; }
                const auto actualBody = body(*loadedMiner);
                if (loadedMiner->instanceId != parkedMiner.instanceId || loadedMiner->kind != parkedMiner.kind
                    || loadedMiner->type != parkedMiner.type || loadedMiner->owner != parkedMiner.owner
                    || !loadedMiner->alive || loadedMiner->health < parkedMiner.health
                    || loadedMiner->health > parkedHealthLimit
                    || loadedMiner->harvestAutoSearchSuspended != suspended
                    || loadedMiner->x != parkedMiner.x || loadedMiner->y != parkedMiner.y
                    || loadedMiner->cell != parkedMiner.cell || loadedMiner->level != parkedMiner.level
                    || loadedMiner->onBridge != parkedMiner.onBridge || loadedMiner->airborne != parkedMiner.airborne
                    || loadedMiner->hasRenderPosition != parkedMiner.hasRenderPosition || loadedMiner->renderMoveActive
                    || std::abs(actualBody[0] - parkedBody[0]) > 1e-8
                    || std::abs(actualBody[1] - parkedBody[1]) > 1e-8
                    || std::abs(actualBody[2] - parkedBody[2]) > 1e-8 || loaded->behaviors.contains(objectId)) {
                    error = "loaded slave manager changed its parked body, health bounds or command"; return false;
                }
                long long deliveredCredits = 0;
                int deliveries = 0;
                for (const auto& [id, task] : beforeTasks) {
                    const auto found = loaded->slaveMinerTasks.find(id);
                    if (found == loaded->slaveMinerTasks.end() || found->second.refineryId != objectId) {
                        error = "loaded parked slave manager lost a retained task"; return false;
                    }
                    const auto& after = found->second;
                    if (after.cargoValue >= task.cargoValue) continue;
                    const auto& worker = beforeWorkers.at(id);
                    const auto actualWorkerBody = body(worker);
                    const RuntimeCellPoint workerBodyCell{static_cast<int>(std::lround(actualWorkerBody[0])),
                        static_cast<int>(std::lround(actualWorkerBody[1]))};
                    if (task.phase != RuntimeSlaveMinerPhase::Returning || task.cargoValue <= 0
                        || after.cargoValue != 0 || after.cargoUnits != 0 || after.cargoGemUnits != 0
                        || after.workTicksRemaining != 0 || !worker.alive || worker.renderMoveActive
                        || worker.level != parkedMiner.level || worker.onBridge != parkedMiner.onBridge
                        || runtimeCellDistanceToObjectFootprint(*loaded, runtimeCellPointForObject(worker), parkedMiner) != 0.0
                        || runtimeCellDistanceToObjectFootprint(*loaded, workerBodyCell, parkedMiner) != 0.0) {
                        error = "loaded slave #" + std::to_string(id)
                            + " cargo vanished without a settled native return: "
                            + serializeRuntimeSlaveMinerTask(task) + " -> "
                            + serializeRuntimeSlaveMinerTask(after); return false;
                    }
                    const int bonusPerPurifier = static_cast<int>(std::floor(
                        task.cargoValue * std::max(0.0, loaded->rules.getPurifierBonus())));
                    deliveredCredits += task.cargoValue + static_cast<long long>(purifierCount) * bonusPerPurifier;
                    ++deliveries;
                }
                if (loaded->economy.credits != beforeCredits + deliveredCredits
                    || loaded->economy.resourceTrips != beforeTrips + deliveries) {
                    error = "loaded slave deliveries do not exactly conserve credits/trips: parent#"
                        + std::to_string(objectId) + " tick=" + std::to_string(loaded->tick)
                        + " credits=" + std::to_string(beforeCredits) + "->"
                        + std::to_string(loaded->economy.credits) + " expectedDelta="
                        + std::to_string(deliveredCredits) + " trips=" + std::to_string(beforeTrips)
                        + "->" + std::to_string(loaded->economy.resourceTrips)
                        + " expectedDeliveries=" + std::to_string(deliveries); return false;
                }
                childDeposits += deliveries;
            }
        }
        loadedMiner = findObjectById(*loaded, objectId);
        if (loadedMiner == nullptr || !loadedMiner->alive
            || runtimeCellPointForObject(*loadedMiner) != parkedCell
            || loaded->behaviors.contains(objectId) || (!slaveManager && loaded->economy.credits != parkedCredits)) {
            error = "loaded miner resumed automatic mining"; return false;
        }
        if (slaveManager) std::cout << "Slave manager saved hold: " << parkedMiner.type << '#' << objectId
            << " parent/body/pause=exact naturalHP=" << parkedMiner.health << "->" << loadedMiner->health
            << " childDeposits=" << childDeposits << " credits=" << parkedCredits << "->"
            << loaded->economy.credits << " conserved=ok\n";
    }
    const int count = parseRuntimeIntOrDefault(ini.getValue("Objects", "Count", "0"), 0);
    const int version = parseRuntimeIntOrDefault(ini.getValue("RuntimeSave", "Version", "0"), 0);
    for (int index = 0; index < count; ++index) {
        auto fields = splitRuntimeSaveFields(ini.getValue("Objects", std::to_string(index), ""));
        if (fields.size() < 2 || parseRuntimeIntOrDefault(fields[1], -1) != objectId) continue;
        WorldObjectInstance restored;
        if (fields.size() < 55U || !parseRuntimeCompleteSavedObjectRecord(
                state, fields, index + 1, version, restored, error)
            || restored.harvestAutoSearchSuspended != suspended) {
            error = "miner manual move did not survive save: " + error; return false;
        }
        fields[54] = "invalid";
        WorldObjectInstance malformed;
        std::string malformedError;
        if (parseRuntimeCompleteSavedObjectRecord(state, fields, index + 1,
                version, malformed, malformedError)) {
            error = "malformed miner command accepted"; return false;
        }
        fields.resize(54);
        WorldObjectInstance legacy;
        if (!parseRuntimeCompleteSavedObjectRecord(state, fields, index + 1,
                version, legacy, error) || legacy.harvestAutoSearchSuspended) {
            error = "legacy miner did not default to automatic mining: " + error; return false;
        }
        return true;
    }
    error = "miner missing from saved object records"; return false;
}

bool runRuntimeHarvesterManualMoveSmokeProbe(RuntimeWindowState& live, std::string& error) {
    for (const char* type : {"CMIN", "HARV"}) {
        const auto fail = [&](const std::string& why) {
            error = std::string("harvester manual move ") + type + ": " + why; return false;
        };
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = live.config;
        probe.config.fixedTickHz = 30;
        probe.rules = live.rules; probe.rulesLoaded = true;
        probe.art = live.art; probe.artLoaded = true;
        probe.runtimeSuppressPlayerAudioFeedback = true;
        probe.economy.owner = "Americans";
        probe.economy.credits = 0;
        probe.economy.skirmishStartApplied = true;
        probe.triggers.bootstrapComplete = true;
        const RuntimeCellPoint ore{14, 14};
        for (int y = 1; y <= 28; ++y) for (int x = 1; x <= 28; ++x) {
            TerrainCell terrain;
            terrain.x = x; terrain.y = y; terrain.cellIndex512 = runtimeCellKey(x, y);
            terrain.tileIndex = 0; terrain.subTileIndex = 0;
            if (x >= ore.x && x < ore.x + 4 && y >= ore.y && y < ore.y + 4)
                setRuntimeResourceTerrainBails(terrain, "TIB", 12);
            probe.world.accessTerrainCells().push_back(terrain);
        }
        auto* miner = addRuntimeObjectInstance(probe, RuntimeBuildKind::Unit, type, {12, 12});
        if (miner == nullptr) return fail("missing miner");
        const int id = miner->instanceId;
        const std::string refineryType = std::string(type) == "CMIN" ? "GAREFN" : "NAREFN";
        auto* refinery = addRuntimeObjectInstance(probe, RuntimeBuildKind::Building, refineryType, {23, 20});
        if (refinery == nullptr) return fail("missing refinery");
        const int refineryId = refinery->instanceId;
        for (auto& object : probe.world.accessObjects()) {
            object.buildAnimTicksRemaining = object.buildAnimTicksTotal = 0;
            object.mission = "Guard";
        }
        initializeRuntimeNavigation(probe);
        rebuildRuntimeEconomyPower(probe);
        const auto step = [&]() { ++probe.tick; advanceRuntimeSimulation(probe); };
        const auto cargo = [&]() {
            const auto task = probe.harvesterTasks.find(id);
            return task == probe.harvesterTasks.end() ? 0 : task->second.cargoUnits;
        };
        for (int tick = 0; tick < 1600 && cargo() == 0; ++tick) step();
        if (cargo() == 0) return fail("new miner no longer starts automatic mining: "
            + probe.economy.status + " harvesters=" + std::to_string(probe.economy.harvesters));
        const auto moveAndHold = [&](RuntimeCellPoint target) {
            const int beforeCargo = cargo();
            const int beforeValue = probe.harvesterTasks.at(id).cargoValue;
            const int beforeCredits = probe.economy.credits;
            applySelection(probe, {id}, false, false);
            issueRuntimeMoveCommand(probe, target);
            const auto& task = probe.harvesterTasks.at(id);
            if (probe.lastCommand.acceptedCount != 1
                || !findObjectById(probe, id)->harvestAutoSearchSuspended
                || task.workTicksRemaining != 0 || task.targetRefineryId != -1
                || task.targetOre != RuntimeCellPoint{} || task.phase != RuntimeHarvesterPhase::Idle)
                return fail("ground command retained the old mining/dock task");
            bool arrived = false;
            for (int tick = 0; tick < 1600; ++tick) {
                step();
                const auto* current = findObjectById(probe, id);
                if (runtimeCellPointForObject(*current) == target
                    && !probe.behaviors.contains(id)) { arrived = true; break; }
            }
            if (!arrived) return fail("miner did not reach the ground destination");
            if (!checkRuntimeHarvesterManualMoveSave(probe, id, error)) return false;
            for (int tick = 0; tick < 240; ++tick) step();
            if (runtimeCellPointForObject(*findObjectById(probe, id)) != target
                || probe.behaviors.contains(id) || cargo() != beforeCargo
                || probe.harvesterTasks.at(id).cargoValue != beforeValue
                || probe.economy.credits != beforeCredits)
                return fail("miner resumed collection/return or changed cargo after a ground move");
            return true;
        };
        if (!moveAndHold({4, 4}) || !moveAndHold({4, 6})) return false;
        const int beforeResume = cargo();
        issueRuntimeMoveCommand(probe, ore);
        if (probe.lastCommand.acceptedCount != 1
            || findObjectById(probe, id)->harvestAutoSearchSuspended)
            return fail("ore click failed to resume mining");
        if (!checkRuntimeHarvesterManualMoveSave(probe, id, error)) return false;
        for (int tick = 0; tick < 2000 && cargo() <= beforeResume; ++tick) step();
        if (cargo() <= beforeResume) return fail("resumed miner did not gather ore");

        // Interrupt a full unloading task. The move must keep that cargo,
        // release the dock and prevent both delivery and a Chrono return.
        auto& task = probe.harvesterTasks.at(id);
        task.cargoUnits = task.cargoCapacity;
        task.cargoValue = task.cargoUnits * 25;
        task.cargoGemUnits = 0; task.cargoHasGems = false;
        task.phase = RuntimeHarvesterPhase::Unloading;
        task.targetRefineryId = refineryId;
        findMutableObjectById(probe, refineryId)->mission = "Unload";
        if (!moveAndHold({4, 8})) return false;
        if (findObjectById(probe, refineryId)->mission == "Unload")
            return fail("interrupted unloading left the refinery busy");
        const int beforeDelivery = probe.economy.credits;
        issueRuntimeMoveCommand(probe, ore);
        for (int tick = 0; tick < 3500 && probe.economy.credits <= beforeDelivery; ++tick) step();
        if (probe.economy.credits <= beforeDelivery || cargo() != 0)
            return fail("full miner failed to resume delivery after ore click");
        std::cout << "Harvester manual move: " << type
            << " defaultAuto=ok partial/fullCargo=ok groundHold=ok repeatedMove=ok"
            << " explicitResume=ok interruptedUnload=ok save/legacy/invalid=ok\n";
    }
    return true;
}
