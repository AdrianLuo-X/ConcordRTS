bool runRuntimeGarrisonInteractionSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) { error = "garrison interaction: " + message; return false; };
    std::set<int> capacities;
    int buildingCount = 0;
    for (const auto& rule : live.rules.getBuildingTypes()) {
        if (!rule.canBeOccupied || rule.maxNumberOccupants <= 0) continue;
        WorldObjectInstance host;
        host.kind = WorldObjectKind::Structure; host.type = rule.id;
        host.alive = true; host.health = rule.strength;
        const auto frames = runtimeGarrisonPipFrames(live, host);
        if (frames.size() != static_cast<std::size_t>(rule.maxNumberOccupants)
            || std::any_of(frames.begin(), frames.end(), [](int f) { return f != 6; }))
            return fail(rule.id + " capacity/empty pips mismatch");
        capacities.insert(rule.maxNumberOccupants);
        ++buildingCount;
    }
    if (!capacities.contains(5) || !capacities.contains(6) || !capacities.contains(10))
        return fail("stock capacity variations missing");
    for (int frame : {6, 7, 8, 9, 10, 11, 12}) {
        const auto* sprite = loadRuntimeUiShpFrame(live, "sidebar", "pips", frame);
        if (sprite == nullptr || sprite->image.height < 12
            || runtimeUiPaletteForEntry(live, sprite, "palette") == nullptr)
            return fail("original person frame " + std::to_string(frame) + " unavailable/wrong asset: "
                + (sprite ? sprite->sourcePath : "missing"));
    }
    const auto step = [](RuntimeWindowState& s) { ++s.tick; advanceRuntimeSimulation(s); };
    const auto savePath = pathString(std::filesystem::current_path() / "runtime_garrison_walk_smoke.ini");
    for (int hz : {30, 60}) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& s = *storage;
        s.config = live.config; s.config.fixedTickHz = hz; s.config.computerAiEnabled = false;
        s.config.defaultMapPath = "garrison-walk-probe";
        s.rules = live.rules; s.rulesLoaded = true; s.art = live.art; s.artLoaded = true;
        s.economy.owner = "Americans"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        for (int y = 1; y <= 28; ++y) for (int x = 1; x <= 28; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        auto* host = addRuntimeObjectInstance(s, RuntimeBuildKind::Building, "NABNKR", {12, 12});
        if (host == nullptr) return fail("stock bunker missing");
        const int hostId = host->instanceId;
        host->mission = "Guard"; host->buildAnimTicksTotal = host->buildAnimTicksRemaining = 0;
        std::vector<int> infantryIds;
        for (const char* type : {"E1", "E2", "INIT"}) {
            auto* unit = addRuntimeObjectInstance(s, RuntimeBuildKind::Infantry, type,
                {11, 12 + static_cast<int>(infantryIds.size())});
            if (unit == nullptr) return fail("stock infantry missing");
            unit->mission = "Guard"; clearRuntimeRenderPosition(*unit);
            infantryIds.push_back(unit->instanceId);
        }
        initializeRuntimeNavigation(s); ensureRuntimeGarrisonsInitialized(s);
        RuntimeSnapshotActiveStateScope scope(s);
        host = findMutableObjectById(s, hostId);
        const auto footprint = runtimeStructureFootprintCellsForObject(s, *host);
        const auto inside = [&](const RuntimeCellPoint& c) {
            return std::find(footprint.begin(), footprint.end(), c) != footprint.end();
        };
        applySelection(s, infantryIds, false, false);
        if (!queueRuntimeOccupyTarget(s, *host) || s.pendingOccupyTargets.size() != 3
            || completeRuntimeOccupyEntry(s, infantryIds.front()))
            return fail("adjacent infantry vanished before entering");
        std::set<int> walkedInside;
        bool savedEntry = false;
        for (int t = 0; t < hz * 20; ++t) {
            step(s);
            for (const auto& [id, target] : s.pendingOccupyTargets) {
                const auto* entrant = findObjectById(s, id);
                if (entrant == nullptr || !runtimeMissionAllowsPendingOccupyIntent(*entrant))
                    return fail("queued/blocked entrant lost its entry mission");
            }
            if (t == 0 && (!saveRuntimeState(s, savePath, error) || !loadRuntimeState(s, savePath, error)))
                return false;
            for (int id : infantryIds) {
                const auto* unit = findRuntimeObjectByIdIncludingInactive(s, id);
                if (unit == nullptr) return fail("entrant lost");
                if (unit->alive && unit->renderMoveActive && inside(runtimeCellPointForObject(*unit))) {
                    walkedInside.insert(id);
                    if (!s.pendingOccupyTargets.contains(id) || unit->garrisonHostObjectId >= 0)
                        return fail("entry committed before visible movement finished");
                    if (!savedEntry) {
                        const double x = unit->renderX, y = unit->renderY;
                        if (!saveRuntimeState(s, savePath, error) || !loadRuntimeState(s, savePath, error)) return false;
                        unit = findObjectById(s, id);
                        if (unit == nullptr || !unit->renderMoveActive
                            || std::abs(unit->renderX - x) > 0.0001 || std::abs(unit->renderY - y) > 0.0001)
                            return fail("saving during entry moved the visible soldier");
                        savedEntry = true;
                    }
                }
            }
            host = findMutableObjectById(s, hostId);
            if (host->garrisonOccupantIds.size() == 3) break;
        }
        host = findMutableObjectById(s, hostId);
        if (!savedEntry || walkedInside.size() != 3 || host->garrisonOccupantIds.size() != 3)
            return fail("group failed to walk inside: " + s.simulationStatus);
        const auto occupiedFrames = runtimeGarrisonPipFrames(s, *host);
        std::multiset<int> colors(occupiedFrames.begin(), occupiedFrames.end());
        if (colors != std::multiset<int>{6, 6, 10, 11, 12}) return fail("mixed occupancy pip colors/count wrong");

        // Use the same target order dispatched by Ctrl-click in the live UI.
        applySelection(s, {hostId}, false, false);
        auto* victim = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, "MTNK", {10, 10});
        if (victim == nullptr) return fail("force attack target missing");
        const int targetId = victim->instanceId;
        for (const char* owner : {"Americans", "Neutral", "Russians"}) {
            victim = findMutableObjectById(s, targetId);
            victim->owner = owner; victim->health = 10000; victim->mission = "Sleep";
            victim->weaponCooldownTicksRemaining = 100000;
            if (std::string(owner) == "Americans") {
                issueRuntimeAttackCommand(s, *victim);
                if (s.lastCommand.acceptedCount != 0) return fail("ordinary order fired on allied unit");
            }
            if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Attack,
                    {10, 10}, victim) || s.lastCommand.acceptedCount != 1)
                return fail(std::string("Ctrl target rejected: ") + owner);
            for (int t = 0; t < hz * 3; ++t) step(s);
            victim = findMutableObjectById(s, targetId);
            if (victim == nullptr || victim->health >= 10000) return fail(std::string("Ctrl target not damaged: ") + owner);
        }
        victim->owner = "Americans";
        const RuntimeCellPoint ground{10, 9};
        if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell, ground)
            || s.lastCommand.acceptedCount != 1 || !s.behaviors.at(hostId).attackGround)
            return fail("Ctrl ground order rejected");
        if (!saveRuntimeState(s, savePath, error) || !loadRuntimeState(s, savePath, error)) return false;
        if (!s.behaviors.contains(hostId) || !s.behaviors.at(hostId).forceFire
            || !s.behaviors.at(hostId).attackGround) return fail("save lost force fire");
        int before = s.nextProjectileActorId;
        for (int t = 0; t < hz * 3; ++t) step(s);
        if (s.nextProjectileActorId < before + 3) return fail("ground force fire did not repeat: " + s.simulationStatus);
        applySelection(s, {hostId}, false, false); // Loading clears the live UI selection.
        issueRuntimeStopCommand(s);
        if (s.lastCommand.acceptedCount != 1) return fail("selected garrison rejected Stop");
        before = s.nextProjectileActorId;
        for (int t = 0; t < hz; ++t) step(s);
        if (s.nextProjectileActorId != before) return fail("Stop did not cancel garrison force fire");

        const auto exits = evacuateRuntimeGarrisonHost(s, hostId, RuntimeGarrisonEvacuationReason::Manual, false);
        if (exits.size() != 3) return fail("manual exit count wrong");
        for (int id : infantryIds) {
            const auto* unit = findObjectById(s, id);
            if (unit == nullptr || !unit->renderMoveActive || unit->garrisonHostObjectId >= 0
                || !s.behaviors.contains(id) || inside(runtimeCellPointForObject(*unit))
                || !inside(s.behaviors.at(id).renderFrom)
                || runtimeCellDistance(s.behaviors.at(id).renderFrom, s.behaviors.at(id).renderTo) != 1.0)
                return fail("exit did not start a one-cell walk from the building");
        }
        step(s);
        const auto* exiting = findObjectById(s, infantryIds.front());
        const double exitX = exiting->renderX, exitY = exiting->renderY;
        if (!saveRuntimeState(s, savePath, error) || !loadRuntimeState(s, savePath, error)) return false;
        exiting = findObjectById(s, infantryIds.front());
        if (exiting == nullptr || !exiting->renderMoveActive
            || std::abs(exiting->renderX - exitX) > 0.0001 || std::abs(exiting->renderY - exitY) > 0.0001)
            return fail("save interrupted the exit walk");
        for (int t = 0; t < hz * 2; ++t) step(s);
        for (int id : infantryIds) {
            const auto* unit = findObjectById(s, id);
            if (unit == nullptr || unit->renderMoveActive || unit->hasRenderPosition
                || unit->movementAnimationDistance <= 0.0 || inside(runtimeCellPointForObject(*unit)))
                return fail("exit did not finish outside with walking animation");
        }
        host = findMutableObjectById(s, hostId);
        if (runtimeGarrisonPipFrames(s, *host) != std::vector<int>(5, 6)) return fail("exit kept filled pips");
        if (!s.garrisons.tryEnter(s.rules, s.world.accessObjects(), infantryIds.front(), hostId).entered())
            return fail("blocked-exit fixture entry failed");
        // A ring of tanks blocks every doorway while cells beyond it stay free.
        std::set<std::pair<int, int>> perimeter;
        for (const auto& c : footprint) for (const auto& d : std::vector<RuntimeCellPoint>{{1,0},{-1,0},{0,1},{0,-1}}) {
            RuntimeCellPoint p{c.x + d.x, c.y + d.y};
            if (!inside(p)) perimeter.emplace(p.x, p.y);
        }
        for (const auto& [x,y] : perimeter) addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, "MTNK", {x,y});
        refreshRuntimeObjectOccupancyAfterLocalChange(s);
        evacuateRuntimeGarrisonHost(s, hostId, RuntimeGarrisonEvacuationReason::Manual, false);
        if (findRuntimeObjectByIdIncludingInactive(s, infantryIds.front())->garrisonHostObjectId != hostId)
            return fail("blocked occupant teleported through the ring");

        // This stock 1x1 house has ten slots but only four cardinal exit cells.
        auto* smallHost = addRuntimeObjectInstance(s, RuntimeBuildKind::Building, "CARUS07", {22, 22});
        if (smallHost == nullptr) return fail("small stock garrison missing");
        const int smallId = smallHost->instanceId;
        smallHost->mission = "Guard";
        smallHost->buildAnimTicksTotal = smallHost->buildAnimTicksRemaining = 0;
        const auto smallFootprint = runtimeStructureFootprintCellsForObject(s, *smallHost);
        if (smallFootprint.size() != 1 || runtimeGarrisonPipFrames(s, *smallHost).size() != 10)
            return fail("small stock garrison fixture has wrong footprint/capacity");
        std::vector<int> smallOccupants;
        for (int i = 0; i < 10; ++i) {
            auto* unit = addRuntimeObjectInstance(s, RuntimeBuildKind::Infantry, "E1", {18 + i % 5, 18 + i / 5});
            if (unit == nullptr) return fail("small stock garrison entrant missing");
            smallOccupants.push_back(unit->instanceId);
            if (!s.garrisons.tryEnter(s.rules, s.world.accessObjects(), unit->instanceId, smallId).entered())
                return fail("small stock garrison fixture entry failed");
        }
        refreshRuntimeObjectOccupancyAfterLocalChange(s);
        const auto smallExits = evacuateRuntimeGarrisonHost(s, smallId, RuntimeGarrisonEvacuationReason::Manual, false);
        std::set<std::tuple<int, int, int>> occupiedExitSlots;
        for (int id : smallOccupants) {
            const auto* unit = findObjectById(s, id);
            if (unit == nullptr || !unit->alive || !unit->renderMoveActive || unit->garrisonHostObjectId >= 0
                || !RuntimeSubcellSystemState::isActiveSubCell(unit->subCell)
                || runtimeCellDistance(runtimeCellPointForObject(*unit), smallFootprint.front()) != 1.0
                || !occupiedExitSlots.emplace(unit->x, unit->y, unit->subCell).second)
                return fail("small house failed to share its exit cells without overlaps");
        }
        if (smallExits.size() != 10 || !findObjectById(s, smallId)->garrisonOccupantIds.empty())
            return fail("small house did not evacuate all ten occupants");
        for (int t = 0; t < hz * 2; ++t) step(s);
        occupiedExitSlots.clear();
        for (int id : smallOccupants) {
            const auto* unit = findObjectById(s, id);
            if (unit == nullptr || !unit->alive || unit->renderMoveActive || unit->hasRenderPosition
                || !occupiedExitSlots.emplace(unit->x, unit->y, unit->subCell).second)
                return fail("small house exits did not finish in distinct subcells");
        }
    }
    std::cout << "Garrison interaction: " << buildingCount << " stock capacities, original person pips; "
        << "30/60Hz entry/exit walks, saves, blocked doors, 1x1 ten-person exits, Ctrl targets/ground and Stop=ok\n";
    return true;
}
