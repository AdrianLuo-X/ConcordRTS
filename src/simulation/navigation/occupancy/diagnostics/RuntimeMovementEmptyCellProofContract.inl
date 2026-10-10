// Exercise the production activation boundary before any enclosing batch refresh.
bool runRuntimeWorldActivationOccupancyContract(std::string& error) {
    const auto fail = [&](const std::string& reason) {
        error = "world activation occupancy: " + reason;
        return false;
    };
    const auto makeState = [&]() {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& s = *storage;
        IniFile rules;
        if (!rules.loadFromString(
            "[InfantryTypes]\n0=INF\n[VehicleTypes]\n0=VEH\n1=TRAN\n"
            "[INF]\nStrength=100\nSpeed=5\nSpeedType=Foot\nSize=1\n"
            "[VEH]\nStrength=200\nSpeed=6\nSpeedType=Track\nSize=2\n"
            "[TRAN]\nStrength=500\nSpeed=6\nSpeedType=Track\nPassengers=8\nSizeLimit=10\n")
            || !s.rules.loadFromIni(rules)) return std::unique_ptr<RuntimeWindowState>{};
        s.rulesLoaded = true; s.tick = 10; s.config.fixedTickHz = 30;
        s.config.computerAiEnabled = false; s.economy.owner = "Player";
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y;
            cell.cellIndex512 = runtimeCellKey(x, y);
            s.world.accessTerrainCells().push_back(cell);
            RuntimeTerrainInfo info; info.passable = true;
            s.terrainInfoByCell.emplace(runtimeCellKey(x, y), info);
        }
        s.terrainByCell = buildRuntimeTerrainLookup(s.world);
        s.world.accessObjects().reserve(16);
        WorldObjectInstance host;
        host.instanceId = 100; host.kind = WorldObjectKind::Unit;
        host.type = "TRAN"; host.owner = "Player"; host.health = 500;
        host.alive = true; host.x = host.y = 6; host.cell = runtimeCellKey(6, 6);
        host.mission = "Guard"; host.subCell = -1;
        host.transportPassengerIds = {1, 2, 3, 4, 5, 6};
        s.world.accessObjects().push_back(host);
        for (int id = 1; id <= 6; ++id) {
            WorldObjectInstance passenger;
            passenger.instanceId = id;
            passenger.kind = id <= 2 ? WorldObjectKind::Unit : WorldObjectKind::Infantry;
            passenger.type = id <= 2 ? "VEH" : "INF";
            passenger.owner = "Player"; passenger.health = 80 + id;
            passenger.alive = false; passenger.x = passenger.y = 6;
            passenger.cell = runtimeCellKey(6, 6); passenger.subCell = -1;
            passenger.mission = "InsideTransport";
            s.world.accessObjects().push_back(passenger);
        }
        return storage;
    };
    int checks = 0;
    {
        auto storage = makeState();
        if (!storage) return fail("fixture rules did not load");
        auto& s = *storage; RuntimeSnapshotActiveStateScope scope(s);
        s.world.accessObjects().front().transportPassengerIds.clear();
        auto* object = findMutableRuntimeObjectByIdIncludingInactive(s, 1);
        object->x = object->y = 10; object->cell = runtimeCellKey(10, 10);
        (void)runtimeTickOccupancyLookup(s);
        if (!runtimeMovementTickProvesEmptyCell(s, {10, 10}))
            return fail("inactive object was not initially excluded");
        const auto* data = s.world.getObjects().data();
        const auto count = s.world.getObjects().size();
        const int health = object->health;
        const int cell = object->cell;
        activateRuntimeWorldObject(s, *object);
        // Check immediately: no subcell assignment, next query or batch-end
        // refresh may be needed to make the old proof unusable.
        if (!object->alive || object->health != health || object->cell != cell
            || object->owner != "Player" || object->type != "VEH"
            || s.world.getObjects().data() != data || s.world.getObjects().size() != count
            || s.tickOccupancyCacheTick == s.tick || s.movementEmptyCellProof.valid
            || runtimeMovementTickProvesEmptyCell(s, {10, 10})
            || !runtimeCellHasHardWholeCellBlocker(s, {10, 10}, 999, -1, {}, true)
            || !runtimeCellHasHardWholeCellBlocker(s, {10, 10}, 999, -1, {}, false))
            return fail("same-storage activation retained an empty-cell proof");
        ++checks;
    }
    // The cold run cannot take the empty-proof shortcut. The warm run must
    // produce exactly the same authoritative object and subcell state.
    for (bool restricted : {false, true}) {
        std::array<std::uint64_t, 2> worlds{}, subcells{};
        std::array<int, 2> unloaded{};
        for (int warm = 0; warm != 2; ++warm) {
            auto storage = makeState();
            if (!storage) return fail("unload fixture rules did not load");
            auto& s = *storage; RuntimeSnapshotActiveStateScope scope(s);
            if (restricted) {
                for (auto& [key, info] : s.terrainInfoByCell)
                    info.passable = key == runtimeCellKey(5, 5);
            }
            if (warm) {
                (void)runtimeTickOccupancyLookup(s);
                if (!runtimeMovementTickProvesEmptyCell(s, {5, 5}))
                    return fail("unload fixture did not start with a warm proof");
            } else invalidateRuntimeTickOccupancyCache(s);
            auto* host = findMutableObjectById(s, 100);
            unloaded[warm] = unloadRuntimeTransport(s, *host);
            const int expected = restricted ? 1 : 6;
            if (unloaded[warm] != expected
                || host->transportPassengerIds.size() != static_cast<std::size_t>(6 - expected))
                return fail("unexpected unload count " + std::to_string(unloaded[warm])
                    + " expected=" + std::to_string(expected));
            std::unordered_set<int> exits;
            for (int id = 1; id <= 6; ++id) {
                const auto* passenger = findRuntimeObjectByIdIncludingInactive(s, id);
                if (!passenger || passenger->health != 80 + id)
                    return fail("unload changed passenger identity/health");
                const bool retained = std::find(host->transportPassengerIds.begin(),
                    host->transportPassengerIds.end(), id) != host->transportPassengerIds.end();
                if (passenger->alive == retained) return fail("unload manifest/alive mismatch");
                if (passenger->alive && (!exits.insert(passenger->cell).second
                    || passenger->cell == host->cell
                    || (passenger->kind == WorldObjectKind::Infantry && passenger->subCell < 0)))
                    return fail("unload overlap or missing infantry subcell");
            }
            worlds[warm] = s.world.deterministicSimulationStateHash();
            subcells[warm] = s.subcells.stateHash(s.world.getObjects(), s.world.getTerrainObjects());
        }
        if (worlds[0] != worlds[1] || subcells[0] != subcells[1] || unloaded[0] != unloaded[1])
            return fail("warm/cold multi-passenger unload diverged");
        ++checks;
    }
    {
        std::array<std::uint64_t, 2> worlds{}, subcells{};
        for (int warm = 0; warm != 2; ++warm) {
            auto storage = makeState();
            if (!storage) return fail("parasite fixture rules did not load");
            auto& s = *storage; RuntimeSnapshotActiveStateScope scope(s);
            s.world.accessObjects().front().transportPassengerIds.clear();
            std::vector<SpecialMechanismAction> actions;
            for (int id : {1, 2}) {
                auto* source = findMutableRuntimeObjectByIdIncludingInactive(s, id);
                source->mission = "ParasiteLimbo";
                SpecialMechanismAction action;
                action.kind = id == 1 ? SpecialMechanismActionKind::ParasiteReturnFromLimbo
                    : SpecialMechanismActionKind::ParasiteEject;
                action.tick = s.tick; action.projectileId = 40 + id;
                action.objectId = id; action.targetObjectId = 100; action.owner = "Player";
                action.cell = {8, 8}; action.stunTicks = id == 2 ? 5 : 0;
                actions.push_back(action);
            }
            if (warm) {
                (void)runtimeTickOccupancyLookup(s);
                if (!runtimeMovementTickProvesEmptyCell(s, {8, 8}))
                    return fail("parasite fixture did not start with a warm proof");
            } else invalidateRuntimeTickOccupancyCache(s);
            const auto result = applyRuntimeParasiteActionsAtomically(s, actions, {}, -1);
            const auto* first = findObjectById(s, 1);
            const auto* second = findObjectById(s, 2);
            if (!result.accepted || !result.changed || !first || !second
                || !first->alive || !second->alive || first->cell == second->cell
                || first->health != 81 || second->health != 82
                || first->mission != "Guard" || second->mission != "ParasiteStunned"
                || !s.parasiteStunUntilTickByObject.contains(2)
                || s.parasiteStunUntilTickByObject.at(2) != s.tick + 5)
                return fail("return/eject did not restore both sources exactly");
            (void)runtimeTickOccupancyLookup(s);
            for (const auto* object : {first, second}) {
                const auto cell = runtimeCellPointForObject(*object);
                if (runtimeMovementTickProvesEmptyCell(s, cell)
                    || !runtimeCellHasHardWholeCellBlocker(s, cell, 999, -1, {}, true)
                    || !runtimeCellHasHardWholeCellBlocker(s, cell, 999, -1, {}, false))
                    return fail("restored parasite remained invisible to occupancy");
            }
            worlds[warm] = s.world.deterministicSimulationStateHash();
            subcells[warm] = s.subcells.stateHash(s.world.getObjects(), s.world.getTerrainObjects());
        }
        if (worlds[0] != worlds[1] || subcells[0] != subcells[1])
            return fail("warm/cold parasite activation diverged");
        ++checks;
    }
    std::cout << "World activation occupancy passed: checks=" << checks
        << " immediate/multi-passenger/blocked-unload/parasite-return/parasite-eject=yes\n";
    error.clear();
    return true;
}

// Include after the movement helpers, alongside the independent smoke probes.
bool runRuntimeMovementEmptyCellProofContract(std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "movement empty-cell proof: " + why;
        return false;
    };
    if (!runRuntimeWorldActivationOccupancyContract(error)) return false;
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& state = *storage;
    RuntimeSnapshotActiveStateScope scope(state);
    IniFile rules;
    if (!rules.loadFromString(
        "[InfantryTypes]\n0=INF\n[VehicleTypes]\n0=VEH\n1=AIR\n"
        "[BuildingTypes]\n0=DRIVE\n1=HIDDEN\n"
        "[INF]\nStrength=100\nSpeed=5\nSpeedType=Foot\n"
        "[VEH]\nStrength=100\nSpeed=6\nSpeedType=Track\n"
        "[AIR]\nStrength=100\nSpeed=6\nSpeedType=Winged\nMovementZone=Fly\n"
        "[DRIVE]\nStrength=500\nNumberImpassableRows=1\n"
        "[HIDDEN]\nStrength=500\nInvisibleInGame=yes\n")
        || !state.rules.loadFromIni(rules)) return fail("fixture rules did not load");
    state.rulesLoaded = true;
    state.config.fixedTickHz = 30;
    state.tick = 10;
    state.economy.owner = "Player";
    state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.structureFootprints["DRIVE"] = {3, 2, true};
    state.structureFootprints["HIDDEN"] = {2, 2, true};
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        TerrainCell cell;
        cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        state.world.accessTerrainCells().push_back(cell);
        RuntimeTerrainInfo info;
        info.passable = true;
        info.bridge = (x == 2 && y == 8) || (x == 7 && y == 7);
        state.terrainInfoByCell.emplace(runtimeCellKey(x, y), info);
    }
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    const auto add = [&](int id, WorldObjectKind kind, const char* type, int x, int y) {
        WorldObjectInstance object;
        object.instanceId = id; object.kind = kind; object.type = type;
        object.owner = "Player"; object.x = x; object.y = y;
        object.cell = runtimeCellKey(x, y); object.alive = true;
        object.health = 100; object.mission = "Guard"; object.subCell = 2;
        state.world.accessObjects().push_back(std::move(object));
    };
    add(1, WorldObjectKind::Infantry, "INF", 1, 1);
    add(2, WorldObjectKind::Unit, "VEH", 4, 4);
    add(3, WorldObjectKind::Unit, "VEH", 4, 4);
    add(4, WorldObjectKind::Structure, "DRIVE", 8, 8);
    add(5, WorldObjectKind::Structure, "HIDDEN", 12, 12);
    add(6, WorldObjectKind::Infantry, "INF", 6, 6);
    add(7, WorldObjectKind::Unit, "VEH", 7, 7);
    add(8, WorldObjectKind::Unit, "VEH", 3, 7);
    add(9, WorldObjectKind::Unit, "AIR", 4, 7);
    add(10, WorldObjectKind::Unit, "VEH", 5, 7);
    findMutableObjectById(state, 7)->onBridge = true;
    findMutableObjectById(state, 8)->alive = false;
    findMutableObjectById(state, 10)->health = 0;
    for (int index = 0; index < 4; ++index) {
        WorldTerrainObject terrain;
        terrain.alive = true; terrain.health = 100;
        terrain.x = 2 + index; terrain.y = 8;
        terrain.cell = runtimeCellKey(terrain.x, terrain.y);
        terrain.occupationBits = index == 1 ? 7 : index == 2 ? 8 : 0;
        terrain.occupiedSubCells.clear();
        if (index == 0) terrain.occupiedSubCells = {2};
        state.world.accessTerrainObjects().push_back(std::move(terrain));
    }
    const auto rebuild = [&]() {
        invalidateRuntimeTickOccupancyCache(state);
        (void)runtimeTickOccupancyLookup(state);
    };
    const auto empty = [&](RuntimeCellPoint cell) {
        return runtimeMovementTickProvesEmptyCell(state, cell);
    };
    int comparisons = 0, provenCells = 0;
    const auto compare = [&](const std::string& phase) {
        const auto worldHash = state.world.deterministicSimulationStateHash();
        const auto subcellHash = state.subcells.stateHash(
            state.world.getObjects(), state.world.getTerrainObjects());
        const auto magHash = state.magBeams.stateHash();
        for (int allowed : {-1, 2, 4, 5}) {
            for (const auto& ignored : {std::unordered_set<int>{},
                    std::unordered_set<int>{2, 3, 7}, std::unordered_set<int>{4, 5}}) {
                for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
                    const RuntimeCellPoint cell{x, y};
                    const bool scalar = runtimeCellHasHardWholeCellBlocker(
                        state, cell, 1, allowed, ignored, true);
                    const bool optimized = runtimeCellHasHardWholeCellBlocker(
                        state, cell, 1, allowed, ignored, false);
                    if (scalar != optimized || (empty(cell) && scalar))
                        return fail(phase + " mismatch at " + std::to_string(x)
                            + "," + std::to_string(y));
                    ++comparisons;
                    if (empty(cell)) ++provenCells;
                }
            }
        }
        if (worldHash != state.world.deterministicSimulationStateHash()
            || subcellHash != state.subcells.stateHash(state.world.getObjects(), state.world.getTerrainObjects())
            || magHash != state.magBeams.stateHash()) return fail(phase + " query mutated authoritative state");
        return true;
    };
    if (empty({2, 2})) return fail("unbuilt cache provided a proof");
    rebuild();
    for (const auto& [cell, expected] : std::vector<std::pair<RuntimeCellPoint, bool>>{
            {{2, 2}, true}, {{4, 4}, false}, {{9, 8}, false}, {{13, 13}, false},
            {{2, 8}, false}, {{3, 8}, false}, {{4, 8}, false}, {{5, 8}, true},
            {{7, 7}, false}, {{5, 7}, false}}) {
        if (empty(cell) != expected)
            return fail("initial fixture proof at " + std::to_string(cell.x) + ","
                + std::to_string(cell.y) + " expected=" + std::to_string(expected)
                + " actual=" + std::to_string(empty(cell)));
    }
    if (!state.tickOccupiedByCell.contains(runtimeCellKey(9, 8)))
        return fail("ordinary navigation omitted a building drive row");
    if (state.tickOccupiedByCell.contains(runtimeCellKey(13, 13)))
        return fail("hidden-foundation fixture unexpectedly present in navigation lookup");
    if (!compare("initial")) return false;

    // A proof means no actual whole-cell blocker. A reservation on the empty
    // cell must remain authoritative, including its specific subcell and layer.
    const RuntimeSubcellLocation reservedCell{2, 2, runtimeCellKey(2, 2), false};
    const auto reserved = state.subcells.reserveBatch(state.world.getObjects(),
        state.world.getTerrainObjects(), {{1, reservedCell}});
    if (reserved.size() != 1 || !reserved.front().reserved() || !empty({2, 2}))
        return fail("reservation fixture failed");
    const auto available = state.subcells.availableSubCells(state.world.getObjects(),
        state.world.getTerrainObjects(), reservedCell);
    if (std::find(available.begin(), available.end(), reserved.front().subCell) != available.end()
        || !compare("reservation")) return fail("empty proof bypassed a reservation");
    state.subcells.clearReservations();

    const auto move = [&](int id, RuntimeCellPoint to) {
        auto* object = findMutableObjectById(state, id);
        const auto from = runtimeCellPointForObject(*object);
        setRuntimeObjectCell(state, *object, to, false);
        updateRuntimeTickOccupancyForMove(state, *object, from, to);
    };
    move(2, {5, 4});
    if (empty({4, 4}) || empty({5, 4})) return fail("first overlap departure hid its peer");
    move(3, {5, 5});
    if (!empty({4, 4}) || empty({5, 5}) || !compare("same-tick moves"))
        return fail("same-tick movement left stale occupancy");
    // Preserve the count guard even when no representative is available.
    state.tickOccupiedByCell.erase(runtimeCellKey(3, 8));
    if (empty({3, 8}) || !compare("count-only cell")) return fail("count-only occupancy was ignored");
    rebuild();
    move(5, {11, 12});
    if (empty({2, 2}) || !compare("structure movement")) return fail("moved foundation retained a proof");
    rebuild();
    invalidateRuntimeTickOccupancyCache(state);
    if (empty({2, 2})) return fail("explicit invalidation retained a proof");
    rebuild();
    ++state.tick;
    if (empty({2, 2})) return fail("stale tick retained a proof");
    (void)runtimeTickOccupancyLookup(state);
    if (empty({-1, 2}) || empty({512, 2})
        || empty({std::numeric_limits<int>::max(), std::numeric_limits<int>::max()}))
        return fail("unusual coordinates supplied a proof");

    // Guard both insertion and reallocation with an unchanged population.
    auto& objects = state.world.accessObjects();
    objects.reserve(objects.capacity() + 1);
    if (empty({2, 2})) return fail("object storage relocation retained a proof");
    rebuild();
    add(-1, WorldObjectKind::Unit, "VEH", 2, 3);
    if (empty({2, 2})) return fail("object insertion retained a proof");
    rebuild();
    if (empty({2, 2}) || !compare("negative id")) return fail("synthetic id supplied a proof");
    objects.back().alive = false;
    rebuild();
    auto& terrainObjects = state.world.accessTerrainObjects();
    terrainObjects.reserve(terrainObjects.capacity() + 1);
    if (empty({2, 2})) return fail("terrain object storage relocation retained a proof");
    rebuild();
    auto& terrainCells = state.world.accessTerrainCells();
    terrainCells.reserve(terrainCells.capacity() + 1);
    if (empty({2, 2})) return fail("map terrain storage relocation retained a proof");
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    rebuild();
    state.mapFacts.cells.emplace_back();
    if (empty({2, 2})) return fail("map facts growth retained a proof");
    rebuild();
    state.structureFootprints["HIDDEN"] = {5000, 5000, true};
    rebuild();
    if (empty({2, 2}) || !compare("giant hidden foundation"))
        return fail("exceptional foundation failed conservative fallback");
    state.structureFootprints["HIDDEN"] = {2, 2, true};
    rebuild();

    RuntimeMagBeamAcquireRequest acquire;
    acquire.currentTick = state.tick;
    acquire.source = {2, "Player", "VEH", true, true};
    acquire.target.objectId = 3; acquire.target.owner = "Enemy"; acquire.target.typeId = "VEH";
    acquire.target.kind = RuntimeMagBeamTargetKind::Vehicle;
    acquire.target.present = acquire.target.alive = acquire.target.enemyToSource = true;
    acquire.target.onGround = acquire.target.liftable = true;
    acquire.target.weight = 1; acquire.target.currentStrength = acquire.target.maximumStrength = 100;
    acquire.target.originalLocomotorId = "{4A582741-9839-11d1-B709-00A024DDAFD1}";
    acquire.target.kinematics.location = {5, 5, runtimeCellKey(5, 5), 0, false, false};
    acquire.target.kinematics.worldX = acquire.target.kinematics.worldY = 5;
    acquire.weapon = {"MagneticBeam", "LocomotorBeam", "{92612C46-F71F-11d1-AC9F-006008055BB5}",
        true, true, true, true, 5000};
    const auto acquired = state.magBeams.acquire(acquire);
    if (!acquired.accepted || empty({2, 2})) return fail("new MagBeam binding reused a proof");
    RuntimeMagBeamAdvanceInput lift;
    lift.currentTick = ++state.tick;
    lift.sources.push_back(acquire.source); lift.targets.push_back(acquire.target);
    RuntimeMagBeamHostUpdate update;
    update.bindingId = acquired.bindingId; update.kind = RuntimeMagBeamHostUpdateKind::LiftStarted;
    update.kinematics = acquire.target.kinematics; update.kinematics.worldZ = 3;
    lift.hostUpdates.push_back(update);
    if (!state.magBeams.advance(lift).advanced || !runtimeObjectOffGroundByMagBeam(state, 3))
        return fail("MagBeam lift fixture failed");
    rebuild();
    if (empty({2, 2}) || !compare("MagBeam lift")) return fail("lifted object supplied a proof");
    if (provenCells == 0) return fail("no optimized empty cells were exercised");
    std::cout << "Movement empty-cell proof passed: comparisons=" << comparisons
        << " provenCells=" << provenCells
        << " foundations/bridge/terrain/reservations/moves/invalidation/storage/magbeam=yes\n";
    error.clear();
    return true;
}
