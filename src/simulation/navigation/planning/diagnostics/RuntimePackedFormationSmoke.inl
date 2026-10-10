// Exercise the actual command allocator and simulation, including mixed teams.
bool runRuntimePackedFormationSmokeProbe(std::string& error) {
    struct Scenario { int infantry; int vehicles; bool vehiclesFirst; int residents; };
    const std::array<Scenario, 9> scenarios{{
        {3, 0, false, 0}, {4, 0, false, 0}, {12, 0, false, 0},
        {6, 3, false, 0}, {6, 3, true, 0}, {7, 2, false, 0},
        {3, 0, false, 1}, {3, 0, false, 2}, {3, 0, false, -1}
    }};
    int cases = 0;
    for (const int hz : {15, 30, 60}) for (const auto& scenario : scenarios) {
        auto owned = std::make_unique<RuntimeWindowState>();
        auto& state = *owned;
        RuntimeSnapshotActiveStateScope snapshotScope(state);
        state.config.fixedTickHz = hz;
        state.config.defaultMapPath.clear();
        state.config.playerCountry = state.economy.owner = "Americans";
        state.runtimeSuppressPlayerAudioFeedback = true;
        state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
        state.config.computerAiEnabled = state.config.crates = state.config.superweapons = false;
        state.economy.skirmishStartApplied = state.triggers.bootstrapComplete = true;
        state.visionEnabled = false;
        IniFile rules;
        if (!rules.loadFromString(
                "[InfantryTypes]\n0=E1\n1=E2\n2=INIT\n"
                "[VehicleTypes]\n0=MTNK\n"
                "[E1]\nStrength=100\nSpeed=5\n"
                "[E2]\nStrength=100\nSpeed=4\n"
                "[INIT]\nStrength=100\nSpeed=4\n"
                "[MTNK]\nStrength=400\nSpeed=6\nROT=5\nCrusher=yes\n")
            || !state.rules.loadFromIni(rules)) { error = "packing rules"; return false; }
        state.rulesLoaded = true;
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            TerrainCell cell;
            cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            state.world.accessTerrainCells().push_back(cell);
            state.terrainInfoByCell[runtimeCellKey(x, y)] = RuntimeTerrainInfo{};
        }
        state.terrainByCell = buildRuntimeTerrainLookup(state.world);
        const RuntimeCellPoint target{16, 16};
        for (int i = 0; i < std::abs(scenario.residents); ++i) {
            if (!addRuntimeObjectInstance(state,
                    scenario.residents < 0 ? RuntimeBuildKind::Unit : RuntimeBuildKind::Infantry,
                    scenario.residents < 0 ? "MTNK" : "E1", target)) {
                error = "resident spawn"; return false;
            }
        }
        std::vector<int> ids;
        const auto spawn = [&](int count, bool infantry) {
            for (int i = 0; i < count; ++i) {
                const int index = static_cast<int>(ids.size());
                const char* type = infantry ? std::array<const char*,3>{{"E1","E2","INIT"}}[i % 3] : "MTNK";
                const auto* object = addRuntimeObjectInstance(state,
                    infantry ? RuntimeBuildKind::Infantry : RuntimeBuildKind::Unit, type,
                    {3 + index % 4, 3 + index / 4});
                if (!object) return false;
                ids.push_back(object->instanceId);
            }
            return true;
        };
        if (!(scenario.vehiclesFirst
                ? spawn(scenario.vehicles, false) && spawn(scenario.infantry, true)
                : spawn(scenario.infantry, true) && spawn(scenario.vehicles, false))) {
            error = "moving group spawn"; return false;
        }
        initializeRuntimeNavigation(state);
        for (auto& [key, info] : state.terrainInfoByCell) {
            (void)key;
            info = RuntimeTerrainInfo{};
            info.authoredLandType = info.movementLandType = RuntimeTerrainLandType::Clear;
        }
        state.selectedObjectIds = ids;
        std::map<int, RuntimeCellPoint> destinations;
        issueRuntimeMoveCommand(state, target, &destinations);
        const std::string label = std::to_string(hz) + "Hz infantry="
            + std::to_string(scenario.infantry) + " vehicles=" + std::to_string(scenario.vehicles)
            + " first=" + std::to_string(scenario.vehiclesFirst)
            + " residents=" + std::to_string(scenario.residents);
        const auto fail = [&](const std::string& why) { error = label + ": " + why; return false; };
        if (destinations.size() != ids.size()) return fail("command rejected a mover");
        std::map<int, std::pair<int,int>> allocation;
        for (const auto& object : state.world.getObjects()) {
            const auto assigned = destinations.find(object.instanceId);
            const auto cell = assigned == destinations.end() ? runtimeCellPointForObject(object) : assigned->second;
            auto& count = allocation[runtimeCellKey(cell.x, cell.y)];
            ++(object.kind == WorldObjectKind::Infantry ? count.first : count.second);
        }
        for (const auto& [key, count] : allocation) {
            if (count.first > 3 || count.second > 1 || (count.first && count.second))
                return fail("overbooked destination " + std::to_string(key));
        }
        if (!scenario.vehicles && !scenario.residents
            && allocation.size() != static_cast<std::size_t>((scenario.infantry + 2) / 3))
            return fail("infantry did not gather three per cell; cells=" + std::to_string(allocation.size()));
        std::reverse(state.selectedObjectIds.begin(), state.selectedObjectIds.end());
        std::map<int, RuntimeCellPoint> reordered;
        issueRuntimeMoveCommand(state, target, &reordered);
        if (destinations != reordered) return fail("selection order changes formation");
        bool arrived = false;
        for (int tick = 0; tick < hz * 80; ++tick) {
            ++state.tick;
            advanceRuntimeSimulation(state);
            std::map<std::tuple<int,int,bool>, std::set<int>> occupancy;
            for (const auto& object : state.world.getObjects()) {
                if (!object.alive || object.health <= 0) return fail("friendly unit died while moving");
                auto& slots = occupancy[{object.x, object.y, object.onBridge}];
                if (object.kind == WorldObjectKind::Infantry) {
                    if (!RuntimeSubcellSystemState::isActiveSubCell(object.subCell)
                        || slots.count(-1) || !slots.insert(object.subCell).second)
                        return fail("infantry overlaps at tick " + std::to_string(state.tick));
                } else {
                    if (!slots.empty()) return fail("tank overlaps at tick " + std::to_string(state.tick));
                    slots.insert(-1);
                }
            }
            arrived = true;
            for (const int id : ids) if (state.behaviors.count(id)) arrived = false;
            if (arrived) break;
        }
        if (!arrived) {
            std::ostringstream details;
            for (const int id : ids) {
                const auto* object = findObjectById(state, id);
                details << " #" << id << "@" << object->x << ',' << object->y;
                const auto motion = state.behaviors.find(id);
                if (motion != state.behaviors.end()) {
                    const auto& b = motion->second;
                    details << " goal=" << b.moveTarget.x << ',' << b.moveTarget.y
                        << " path=" << b.path.size() << " pending=" << b.pathRequestPending
                        << " block=" << b.blockedTicks << " replans=" << b.pathReplans;
                    if (!b.path.empty()) details << " next=" << b.path.front().x << ',' << b.path.front().y;
                }
            }
            return fail("formation did not finish:" + details.str());
        }
        for (const auto& [id, cell] : destinations) {
            (void)cell;
            const auto* object = findObjectById(state, id);
            // Crowded formations may settle in an adjacent free slot; the
            // shared goal also permits units to exchange formation slots.
            if (!object || runtimeCellDistance(runtimeCellPointForObject(*object), target) > 6.25)
                return fail("unit stopped outside the destination formation");
            if (object->kind == WorldObjectKind::Infantry) {
                const auto offset = worldObjectSubCellOffset(*object);
                if (std::abs(offset.x) < 0.2 || std::abs(offset.y) < 0.2)
                    return fail("infantry rendered at tile center");
            }
        }
        if (!scenario.vehicles && !scenario.residents) {
            std::map<int, int> finalCounts;
            for (const int id : ids) {
                const auto* object = findObjectById(state, id);
                ++finalCounts[runtimeCellKey(object->x, object->y)];
            }
            if (std::none_of(finalCounts.begin(), finalCounts.end(),
                    [](const auto& cell) { return cell.second == 3; }))
                return fail("arrival lost all three-infantry groups");
        }
        ++cases;
        std::cout << "Packed formation: " << label << " passed\n";
    }
    std::cout << "Packed formation: " << cases
              << " command, capacity, ordering, movement, arrival and render-offset cases passed\n";
    return true;
}
