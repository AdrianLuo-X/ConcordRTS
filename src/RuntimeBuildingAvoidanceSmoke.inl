#include "RuntimeFoundationCollisionSmoke.inl"

// Verify ordinary routes and interpolated poses against the entire foundation,
// including drive rows reserved for explicit building service/exit orders.
bool runRuntimeBuildingAvoidanceSmoke(std::string& error) {
    if (std::getenv("CONCORD_BUILDING_CORNER_ONLY"))
        return runRuntimeStoppedInfantryFoundationCornerSmoke(error);
    if (!runRuntimeStoppedInfantryFoundationCornerSmoke(error)
        || !runRuntimeFoundationCollisionSmoke(error)) return false;
    int cases = 0;
    for (int hz : {15, 30, 60}) for (const char* type : {"BATANK", "BAFOOT", "BAHOVER", "BASHIP"})
        for (const char* owner : {"Player", "Enemy", "Neutral"}) for (int route = 0; route < 4; ++route) {
        if (route == 3 && std::string(type) == "BAFOOT") continue;
        auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
        RuntimeSnapshotActiveStateScope scope(state);
        const std::string label = std::to_string(hz) + "Hz " + type + " " + owner + " route=" + std::to_string(route);
        const auto fail = [&](const std::string& why) { error = "building avoidance " + label + ": " + why; return false; };
        IniFile ini;
        if (!ini.loadFromString(
            "[VehicleTypes]\n0=BATANK\n1=BAHOVER\n2=BASHIP\n[InfantryTypes]\n0=BAFOOT\n"
            "[BuildingTypes]\n0=BADRIVE\n[BADRIVE]\nStrength=1000\nNumberImpassableRows=1\n"
            "[BATANK]\nStrength=200\nSpeed=8\nROT=10\nCrusher=yes\nSpeedType=Track\nMovementZone=Normal\n"
            "[BAFOOT]\nStrength=100\nSpeed=6\nSpeedType=Foot\nMovementZone=Normal\n"
            "[BAHOVER]\nStrength=200\nSpeed=8\nROT=10\nSpeedType=Hover\nMovementZone=Amphibious\n"
            "[BASHIP]\nStrength=200\nSpeed=8\nROT=10\nNaval=yes\nSpeedType=Float\nMovementZone=Water\n")
            || !state.rules.loadFromIni(ini)) return fail("fixture rules");
        state.rulesLoaded = true; state.config.fixedTickHz = hz;
        state.economy.owner = state.config.playerCountry = "Player";
        state.config.computerAiEnabled = state.config.crates = state.config.superweapons = false;
        state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true; state.visionEnabled = false;
        state.economy.skirmishStartApplied = state.triggers.bootstrapComplete = true;
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            state.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance building;
        building.instanceId = 1; building.kind = WorldObjectKind::Structure; building.type = "BADRIVE";
        building.owner = owner; building.x = building.y = 10; building.cell = runtimeCellKey(10, 10);
        building.alive = true; building.health = 1000; building.mission = "Guard";
        state.world.accessObjects().push_back(building);
        const RuntimeCellPoint start = route == 3 ? RuntimeCellPoint{12, 11}
            : route == 1 ? RuntimeCellPoint{8, 8} : RuntimeCellPoint{12, 7};
        const RuntimeCellPoint goal = route == 2 ? RuntimeCellPoint{12, 11} : RuntimeCellPoint{12, 16};
        WorldObjectInstance mover;
        mover.instanceId = 2; mover.type = type; mover.owner = "Player";
        mover.kind = std::string(type) == "BAFOOT" ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        mover.x = start.x; mover.y = start.y; mover.cell = runtimeCellKey(start.x, start.y);
        mover.subCell = 2; mover.alive = true; mover.health = 100; mover.mission = "Guard";
        mover.facing = runtimeFacingFromDelta(goal.x - start.x, goal.y - start.y);
        state.world.accessObjects().push_back(mover);
        initializeRuntimeNavigation(state);
        state.structureFootprints["BADRIVE"] = {4, 3, true};
        const bool water = std::string(type) == "BASHIP";
        for (auto& [key, info] : state.terrainInfoByCell) {
            (void)key; info = RuntimeTerrainInfo{}; info.water = water;
            info.kind = water ? RuntimeTerrainKind::Water : RuntimeTerrainKind::Land;
            info.authoredLandType = info.movementLandType = water ? RuntimeTerrainLandType::Water : RuntimeTerrainLandType::Clear;
        }
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        const auto inside = [](double x, double y) { return x > 9.5 && x < 13.5 && y > 9.5 && y < 12.5; };
        if (route != 2) {
            const auto path = findRuntimePath(state, *findObjectById(state, 2), start, goal);
            if (path.empty() || path.back() != goal) return fail("no route around the foundation");
            bool exited = route != 3;
            for (const auto& cell : path) {
                if (inside(cell.x, cell.y) && exited)
                    return fail("planned route enters the building at " + std::to_string(cell.x) + "," + std::to_string(cell.y));
                exited = exited || !inside(cell.x, cell.y);
            }
        }
        state.selectedObjectIds = {2};
        const auto* initialActor = findObjectById(state,2);
        const auto initialOffset = worldObjectSubCellOffset(*initialActor);
        std::array<double,2> previousCenter{runtimeObjectRenderX(*initialActor)+initialOffset.x,
            runtimeObjectRenderY(*initialActor)+initialOffset.y};
        issueRuntimeMoveCommand(state, goal);
        if (state.lastCommand.acceptedCount != 1) return fail("ordinary move rejected");
        bool complete = false;
        bool cellExited = route != 3, poseExited = route != 3;
        for (int tick = 0; tick < hz * 50; ++tick) {
            ++state.tick; advanceRuntimeSimulation(state);
            const auto* actor = findObjectById(state, 2);
            if (!actor || !actor->alive || (cellExited && inside(actor->x, actor->y))) return fail("movement entered the foundation");
            cellExited = cellExited || !inside(actor->x, actor->y);
            const auto offset = worldObjectSubCellOffset(*actor);
            const std::array<double,2> actual{runtimeObjectRenderX(*actor)+offset.x, runtimeObjectRenderY(*actor)+offset.y};
            const bool poseInside = inside(actual[0],actual[1]);
            // An authorized occupant may finish its departure. Once its real
            // body is outside, an ordinary order cannot sweep back into the host.
            if (poseExited && runtimeFoundationTraceEntersRectangle(previousCenter[0],previousCenter[1],
                    actual[0],actual[1],9.5,9.5,13.5,12.5))
                return fail("swept interpolated body crossed the foundation");
            poseExited = poseExited || !poseInside; previousCenter = actual;
            if (!state.behaviors.contains(2)) { complete = true; break; }
        }
        const auto* actor = findObjectById(state, 2);
        if (!complete || (route != 2 && runtimeCellPointForObject(*actor) != goal)) return fail("did not finish the detour");
        if (findObjectById(state, 1)->health != 1000) return fail("normal movement damaged the building");
        if (route == 3) {
            if (runtimeStructureDepartureHostId(state, *actor) >= 0) return fail("departure permission survived leaving");
            const auto occupancy = buildRuntimeOccupancyLookup(state, actor->instanceId);
            if (runtimeCellIsPassable(state, state.terrainByCell, occupancy, {12, 11}, *actor))
                return fail("departed unit could re-enter with an ordinary order");
        }
        ++cases;
    }
    std::cout << "Building avoidance: " << cases << " routes across three tick rates, infantry/tank/hover/naval, "
        "own/enemy/neutral foundations, inside clicks, departure and interpolated poses passed\n";
    return true;
}
