bool runRuntimeFactoryExitClearanceSmokeProbe(
    const RuntimeWindowState& live, std::string& error
) {
    const auto fail = [&](const std::string& message) {
        error = "factory exit clearance: " + message;
        return false;
    };
    struct FactoryCase {
        const char* factory;
        const char* unit;
        RuntimeBuildKind kind;
        RuntimeProductionLane lane;
    };
    const FactoryCase cases[] = {
        {"GAWEAP", "MTNK", RuntimeBuildKind::Unit, RuntimeProductionLane::Vehicles},
        {"NAWEAP", "HTNK", RuntimeBuildKind::Unit, RuntimeProductionLane::Vehicles},
        {"YAWEAP", "LTNK", RuntimeBuildKind::Unit, RuntimeProductionLane::Vehicles},
        {"GAPILE", "E1", RuntimeBuildKind::Infantry, RuntimeProductionLane::Infantry},
        {"NAHAND", "E2", RuntimeBuildKind::Infantry, RuntimeProductionLane::Infantry},
        {"YABRCK", "INIT", RuntimeBuildKind::Infantry, RuntimeProductionLane::Infantry},
        {"GAYARD", "DEST", RuntimeBuildKind::Unit, RuntimeProductionLane::Ships},
        {"NAYARD", "SUB", RuntimeBuildKind::Unit, RuntimeProductionLane::Ships},
        {"YAYARD", "BSUB", RuntimeBuildKind::Unit, RuntimeProductionLane::Ships}
    };
    const auto itemFor = [](const FactoryCase& scenario) {
        RuntimeBuildCatalogItem item;
        item.kind = scenario.kind;
        item.typeId = scenario.unit;
        item.requiredFactory = scenario.kind == RuntimeBuildKind::Infantry
            ? "InfantryType" : "UnitType";
        return item;
    };
    const auto add = [](RuntimeWindowState& state, int id, WorldObjectKind kind,
                        const std::string& type, RuntimeCellPoint cell) {
        WorldObjectInstance object;
        object.instanceId = id;
        object.kind = kind;
        object.type = type;
        object.owner = state.economy.owner;
        object.health = 1000;
        object.alive = true;
        object.poweredOperational = true;
        object.x = cell.x;
        object.y = cell.y;
        object.cell = runtimeCellKey(cell.x, cell.y);
        object.mission = "Guard";
        state.world.accessObjects().push_back(object);
        auto& added = state.world.accessObjects().back();
        if (runtimeInfantryUsesGroundSubcells(added))
            assignRuntimeInfantrySubcellAtCurrentLocation(state, added);
        invalidateRuntimeObjectProjectionCache(state);
    };
    const auto setup = [&](const FactoryCase& scenario) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& state = *storage;
        state.config.fixedTickHz = 30;
        state.config.gameMode = "Battle";
        state.config.multiplayerActive = state.config.computerAiEnabled = false;
        state.config.crates = state.config.superweapons = state.config.shortGame = false;
        state.config.startingUnits = 0;
        state.config.playerSlots.clear();
        state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
        state.visionEnabled = false;
        state.initialOreFieldsSeeded = true;
        state.triggers.bootstrapComplete = true;
        state.economy.skirmishStartApplied = true;
        state.rules = live.rules;
        state.art = live.art;
        state.rulesLoaded = true;
        state.artLoaded = live.artLoaded;
        state.runtimeSuppressPlayerAudioFeedback = true;
        state.economy.owner = "ProbeA";
        state.economy.credits = 12345;
        initializeRuntimeLocalProductionLanes(state.economy);
        state.economy.localProductionLanes.nextSerial = 3;
        for (int y = 1; y <= 25; ++y) {
            for (int x = 1; x <= 25; ++x) {
                TerrainCell terrain;
                terrain.x = x;
                terrain.y = y;
                terrain.cellIndex512 = runtimeCellKey(x, y);
                terrain.tileIndex = 0;
                state.world.accessTerrainCells().push_back(terrain);
                RuntimeTerrainInfo info;
                info.water = scenario.lane == RuntimeProductionLane::Ships;
                state.terrainInfoByCell.emplace(runtimeCellKey(x, y), info);
            }
        }
        state.terrainByCell = buildRuntimeTerrainLookup(state.world);
        add(state, 1, WorldObjectKind::Structure, scenario.factory, {8, 8});
        state.structureFootprints = buildRuntimeStructureFootprints(state);
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        return storage;
    };
    bool sawAnimation = false;
    const auto step = [&](RuntimeWindowState& state) {
        std::vector<std::pair<int, RuntimeCellPoint>> before;
        for (const auto& object : state.world.getObjects()) {
            if (object.kind != WorldObjectKind::Structure)
                before.emplace_back(object.instanceId, runtimeCellPointForObject(object));
        }
        RuntimeSnapshotActiveStateScope active(state);
        ++state.tick;
        advanceRuntimeSimulation(state, false);
        completeRuntimeWorldRefresh(state);
        for (const auto& [id, cell] : before) {
            const auto* object = findObjectById(state, id);
            if (object == nullptr) continue;
            sawAnimation = sawAnimation || object->renderMoveActive;
            if (std::abs(object->x - cell.x) > 1
                || std::abs(object->y - cell.y) > 1) return false;
        }
        return true;
    };
    for (const auto& scenario : cases) {
        auto storage = setup(scenario);
        auto& state = *storage;
        const auto item = itemFor(scenario);
        const auto spawn = findRuntimeProductionSpawnForOwner(state, item, "ProbeA", false);
        if (!spawn || spawn->anchor == nullptr) return fail(std::string(scenario.factory) + " has no baseline exit");
        const auto door = spawn->cell;
        const auto outward = runtimeProductionOutwardCell(state, *spawn->anchor, door);
        if (!outward) return fail("baseline exit has no outward step");
        const int stacked = scenario.kind == RuntimeBuildKind::Infantry ? 3 : 1;
        for (int i = 0; i < stacked; ++i) {
            add(state, 10 + i, runtimeKindForBuildItem(scenario.kind), scenario.unit, door);
        }
        add(state, 20, WorldObjectKind::Unit,
            scenario.lane == RuntimeProductionLane::Ships ? scenario.unit : "MTNK", *outward);
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        const auto originalSize = state.world.getObjects().size();
        if (completeRuntimeProductionOrder(state, item, false, scenario.lane, 1)
            || state.world.getObjects().size() != originalSize
            || state.behaviors.empty() || state.economy.credits != 12345) {
            return fail(std::string(scenario.factory) + " failed to request clearance without early spawn/payment");
        }
        for (std::uint64_t serial = 1; serial <= 2; ++serial) {
            bool completed = false;
            for (int tick = 0; tick < 600; ++tick) {
                if (!step(state)) return fail("a yielding unit teleported");
                if (!completed) {
                    completed = completeRuntimeProductionOrder(state, item, false, scenario.lane, serial);
                }
                if (completed && state.factoryDeliveries.leases().empty()) {
                    bool moving = false;
                    for (const auto& [id, behavior] : state.behaviors) {
                        moving = moving || behavior.state != RuntimeBehaviorState::Complete;
                    }
                    if (!moving) break;
                }
            }
            if (!completed || !state.factoryDeliveries.leases().empty()
                || state.world.getObjects().size() != originalSize + serial
                || state.economy.credits != 12345) {
                return fail(std::string(scenario.factory) + " did not finish consecutive deliveries: " + state.economy.status);
            }
            const auto& produced = state.world.getObjects().back();
            if (runtimeCellPointForObject(produced) == door || produced.renderMoveActive) {
                std::ostringstream detail;
                detail << scenario.factory << " produced unit did not walk out"
                       << " serial=" << serial << " tick=" << state.tick
                       << " door=" << door.x << ',' << door.y
                       << " produced=" << produced.instanceId;
                for (const auto& object : state.world.getObjects()) {
                    if (object.kind == WorldObjectKind::Structure) continue;
                    const auto offset = worldObjectSubCellOffset(object);
                    detail << " [id=" << object.instanceId << " type=" << object.type
                           << " cell=" << object.x << ',' << object.y
                           << " slot=" << object.subCell
                           << " render=" << object.renderMoveActive
                           << " hasPose=" << object.hasRenderPosition
                           << " body=" << runtimeObjectRenderX(object) + offset.x
                           << ',' << runtimeObjectRenderY(object) + offset.y
                           << " factoryHost=" << object.factoryExitHostId;
                    const auto behavior = state.behaviors.find(object.instanceId);
                    if (behavior != state.behaviors.end()) {
                        const auto& move = behavior->second;
                        detail << " state=" << static_cast<int>(move.state)
                               << " cooldown=" << move.moveCooldownTicks
                               << " renderTicks=" << move.renderMoveTicksRemaining
                               << '/' << move.renderMoveTicksTotal
                               << " pathPending=" << move.pathRequestPending
                               << " path=" << move.path.size()
                               << " goal=" << move.moveTarget.x << ',' << move.moveTarget.y
                               << " allowedHost=" << move.pendingAllowedOccupiedObjectId
                               << " clearanceHost=" << move.factoryExitClearanceHostId
                               << " source=" << move.renderFrom.x << ',' << move.renderFrom.y
                               << '+' << move.renderFromOffsetX << ',' << move.renderFromOffsetY
                               << " target=" << move.renderTo.x << ',' << move.renderTo.y
                               << '+' << move.renderToOffsetX << ',' << move.renderToOffsetY;
                        if (!move.path.empty()) detail << " next=" << move.path.front().x << ',' << move.path.front().y;
                    } else detail << " behavior=none";
                    detail << ']';
                }
                return fail(detail.str());
            }
        }
    }
    if (!sawAnimation) return fail("clearance had no movement animation");

    // A crowd cannot be cleared by moving every blocker into the same cell.
    auto crowded = setup(cases[0]);
    const auto item = itemFor(cases[0]);
    const auto spawn = findRuntimeProductionSpawnForOwner(*crowded, item, "ProbeA", false);
    if (!spawn) return fail("crowded factory setup failed");
    const auto door = spawn->cell;
    int id = 10;
    for (int y = door.y - 2; y <= door.y + 2; ++y) {
        for (int x = door.x; x <= door.x + 3; ++x) {
            add(*crowded, id++, WorldObjectKind::Unit, "MTNK", {x, y});
        }
    }
    refreshRuntimeObjectOccupancyAfterLocalChange(*crowded);
    bool completed = false;
    for (int tick = 0; tick < 900; ++tick) {
        if (!completed) completed = completeRuntimeProductionOrder(*crowded, item, false, RuntimeProductionLane::Vehicles, 1);
        if (!step(*crowded)) return fail("crowded clearance teleported");
        std::set<int> cells;
        for (const auto& object : crowded->world.getObjects()) {
            if (object.kind != WorldObjectKind::Structure && !cells.insert(object.cell).second)
                return fail("crowded clearance overlapped units");
        }
        if (completed && crowded->factoryDeliveries.leases().empty()) break;
    }
    if (!completed || !crowded->factoryDeliveries.leases().empty()) return fail("crowd never cleared the factory");

    // Units entering the rally cell after production also have to yield.
    auto late = setup(cases[0]);
    if (!completeRuntimeProductionOrder(*late, item, false, RuntimeProductionLane::Vehicles, 1))
        return fail("late blocker setup failed");
    const auto lease = late->factoryDeliveries.leases().front();
    add(*late, 100, WorldObjectKind::Unit, "MTNK", {lease.rallyCell.x, lease.rallyCell.y});
    refreshRuntimeObjectOccupancyAfterLocalChange(*late);
    for (int tick = 0; tick < 600 && !late->factoryDeliveries.leases().empty(); ++tick) {
        advanceRuntimeFactoryDeliveries(*late);
        if (!step(*late)) return fail("late clearance teleported");
        const auto behavior = late->behaviors.find(lease.producedObjectId);
        if (behavior != late->behaviors.end()
            && behavior->second.moveTarget != RuntimeCellPoint{lease.rallyCell.x, lease.rallyCell.y})
            return fail("delivery retry replaced its exit target with a nearby cell");
    }
    if (!late->factoryDeliveries.leases().empty()) {
        std::ostringstream detail;
        detail << "new arrival blocked a live delivery indefinitely: " << late->simulationStatus;
        for (const auto& object : late->world.getObjects()) {
            detail << " id=" << object.instanceId << " cell=" << object.x << ',' << object.y
                   << " mission=" << object.mission << " render=" << object.renderMoveActive;
            const auto behavior = late->behaviors.find(object.instanceId);
            if (behavior != late->behaviors.end()) {
                detail << " state=" << static_cast<int>(behavior->second.state)
                       << " pending=" << behavior->second.pathRequestPending
                       << " path=" << behavior->second.path.size()
                       << " goal=" << behavior->second.moveTarget.x << ',' << behavior->second.moveTarget.y;
            }
        }
        return fail(detail.str());
    }

    auto blocked = setup(cases[0]);
    add(*blocked, 10, WorldObjectKind::Unit, "MTNK", door);
    refreshRuntimeObjectOccupancyAfterLocalChange(*blocked);
    findMutableObjectById(*blocked, 10)->owner = "Enemy";
    if (requestRuntimeFactoryExitClearance(*blocked, "ProbeA", {door}) || !blocked->behaviors.empty())
        return fail("factory controlled an enemy blocker");
    findMutableObjectById(*blocked, 10)->owner = "ProbeA";
    for (auto& [key, info] : blocked->terrainInfoByCell) {
        if (key != runtimeCellKey(door.x, door.y)) info.passable = false;
    }
    const auto productionHash = runtimeProductionLanesStateHash(blocked->economy.localProductionLanes);
    for (int tick = 0; tick < 4; ++tick) {
        if (completeRuntimeProductionOrder(*blocked, item, false, RuntimeProductionLane::Vehicles, 1)
            || blocked->world.getObjects().size() != 2 || !blocked->behaviors.empty()
            || blocked->economy.credits != 12345
            || runtimeProductionLanesStateHash(blocked->economy.localProductionLanes) != productionHash)
            return fail("sealed exit changed production progress or overlapped units");
    }

    // Clearing a second factory must not delay production from an open first one.
    auto multiple = setup(cases[0]);
    add(*multiple, 2, WorldObjectKind::Structure, "GAWEAP", {8, 18});
    add(*multiple, 10, WorldObjectKind::Unit, "MTNK", {door.x, door.y + 10});
    refreshRuntimeObjectOccupancyAfterLocalChange(*multiple);
    if (!completeRuntimeProductionOrder(*multiple, item, false, RuntimeProductionLane::Vehicles, 1)
        || multiple->behaviors.contains(10)) return fail("a different blocked factory delayed the open factory");

    // Airborne production does not need ground occupants to leave first.
    auto airborne = setup(cases[0]);
    add(*airborne, 10, WorldObjectKind::Unit, "MTNK", door);
    refreshRuntimeObjectOccupancyAfterLocalChange(*airborne);
    auto airItem = item;
    airItem.typeId = "ZEP";
    if (!completeRuntimeProductionOrder(*airborne, airItem, false)
        || airborne->behaviors.contains(10)) return fail("airborne production displaced a ground unit");

    for (bool campaign : {false, true}) {
        auto ai = setup(cases[0]);
        add(*ai, 10, WorldObjectKind::Unit, "MTNK", door);
        refreshRuntimeObjectOccupancyAfterLocalChange(*ai);
        RuntimeComputerAiState computer;
        computer.owner = "ProbeA";
        RuntimeCampaignHouseProductionState house;
        house.owner = "ProbeA";
        RuntimeCampaignTeamBuild team;
        bool built = false;
        for (int tick = 0; tick < 300 && !built; ++tick) {
            built = campaign ? completeRuntimeCampaignAiUnit(*ai, house, item, team).has_value()
                : runtimeComputerAiBuildUnit(*ai, computer, item);
            if (tick == 0 && (built || !ai->behaviors.contains(10)))
                return fail("AI production did not request doorway clearance");
            if (!step(*ai)) return fail("AI clearance teleported");
        }
        if (!built) return fail("AI production never resumed after clearance");
    }
    std::cout << "Factory exit clearance: factories=9 consecutive=18 crowd=yes lateBlocker=yes "
              << "infantrySubcells=yes animation=yes enemyUntouched=yes sealedExitRetained=yes "
              << "multipleFactories=yes airUnchanged=yes skirmishAi=yes campaignAi=yes\n";
    return true;
}
