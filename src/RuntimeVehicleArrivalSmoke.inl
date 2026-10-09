bool runRuntimeVehicleArrivalSmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail = [&](const std::string& why) { error = why; return false; };
    auto live = std::make_unique<RuntimeWindowState>();
    live->config = config;
    live->report = buildRuntimeStartupReport(config, &live->world, &live->theater,
        &live->art, &live->rules, &live->mapFacts);
    if (!live->report.success) return fail("original rules/map did not load");
    const auto makeProbe = [&](const char* type, int veteran = 0) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& state = *storage;
        state.config = config;
        state.config.fixedTickHz = 30;
        state.config.computerAiEnabled = false;
        state.config.multiplayerActive = false;
        state.config.crates = false;
        state.rules = live->rules; state.rulesLoaded = true;
        state.art = live->art; state.artLoaded = true;
        state.runtimeSuppressPlayerAudioFeedback = true;
        state.runtimePreferencePersistenceEnabled = false;
        state.economy.owner = "Americans";
        state.economy.skirmishStartApplied = true;
        state.triggers.bootstrapComplete = true;
        for (int y = 1; y <= 24; ++y) for (int x = 1; x <= 24; ++x) {
            TerrainCell cell;
            cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = 0; cell.subTileIndex = 0;
            state.world.accessTerrainCells().push_back(cell);
        }
        auto* object = addRuntimeObjectInstance(state, RuntimeBuildKind::Unit, type, {4, 4});
        if (object != nullptr) {
            object->facing = runtimeFacingFromDelta(1, 0);
            object->veterancy = veteran;
            object->harvestAutoSearchSuspended = true;
        }
        initializeRuntimeNavigation(state);
        return storage;
    };
    const auto step = [](RuntimeWindowState& state) { ++state.tick; advanceRuntimeSimulation(state); };
    const auto position = [](const WorldObjectInstance& object) {
        return std::pair<double, double>{object.hasRenderPosition ? object.renderX : double(object.x),
            object.hasRenderPosition ? object.renderY : double(object.y)};
    };
    const auto beginRoute = [&](RuntimeWindowState& state, const std::vector<RuntimeCellPoint>& path) {
        if (state.world.getObjects().empty()) return false;
        auto& object = state.world.accessObjects().front();
        applySelection(state, {object.instanceId}, false, false);
        issueRuntimeMoveCommand(state, path.back());
        if (state.lastCommand.acceptedCount != 1) return false;
        auto& move = state.behaviors.at(object.instanceId);
        replaceRuntimeBehaviorPath(move, path);
        move.pathRequestPending = false;
        return true;
    };
    const auto checkRoute = [&](const char* type, int veteran,
        const std::vector<RuntimeCellPoint>& path) {
        auto storage = makeProbe(type, veteran);
        auto& state = *storage;
        if (!beginRoute(state, path)) return fail(std::string(type) + " move rejected");
        const int id = state.world.getObjects().front().instanceId;
        std::vector<double> terminalSpeeds;
        int cruiseSegments = 0, brakeSegments = 0;
        for (int tick = 0; tick < 1000 && state.behaviors.contains(id); ++tick) {
            const auto& before = state.behaviors.at(id);
            const bool finalStep = before.moveCooldownTicks > 0 && before.renderArrivalDistanceAfter == 0.0;
            const auto previous = position(*findObjectById(state, id));
            step(state);
            const auto* object = findObjectById(state, id);
            const auto current = position(*object);
            if (finalStep) terminalSpeeds.push_back(std::hypot(current.first - previous.first,
                current.second - previous.second));
            const auto found = state.behaviors.find(id);
            if (found != state.behaviors.end()) {
                const auto& move = found->second;
                if (move.renderMoveTicksTotal > 0 && move.renderMoveTicksRemaining == move.renderMoveTicksTotal) {
                    if (move.renderArrivalDistanceAfter >= 0.0) {
                        ++brakeSegments;
                        // A segment that barely enters the braking zone can
                        // round to the original integer tick count. The final
                        // segment must always allow extra time to stop.
                        if (move.renderArrivalDistanceAfter == 0.0 && veteran == 0
                            && move.moveCooldownTicks <= runtimeMovementSegmentIntervalTicks(
                                state, *object, move.renderFrom, move.renderTo))
                            return fail(std::string(type) + " brakes visually but still arrives at cruise time");
                    } else if (move.renderStartupAccelerationDistance == 0.0) {
                        // Startup segments have their own physical velocity/time
                        // contract; retain this unchanged bound for cruise only.
                        ++cruiseSegments;
                        // Q16 carry can place either neighboring integer
                        // duration on a segment; cumulative cruise is checked
                        // independently by the actual long-route time test.
                        const double cruise = runtimeMovementSegmentIntervalQ16(
                            state, *object, move.renderFrom, move.renderTo) / 65536.0;
                        if (std::abs(move.moveCooldownTicks - cruise) >= 1.0)
                            return fail(std::string(type) + " intermediate cruise speed changed");
                    }
                }
                if (move.renderMoveTicksRemaining > 0 && move.renderMoveTicksRemaining != move.moveCooldownTicks)
                    return fail(std::string(type) + " render/gameplay clocks differ");
            }
        }
        const auto* object = findObjectById(state, id);
        if (state.behaviors.contains(id) || runtimeCellPointForObject(*object) != path.back()
            || object->renderMoveActive || object->hasRenderPosition || terminalSpeeds.size() < 3
            || brakeSegments == 0 || (path.size() > 3 && cruiseSegments == 0))
            return fail(std::string(type) + " did not finish route with a braking segment");
        for (std::size_t i = 1; i < terminalSpeeds.size(); ++i)
            if (terminalSpeeds[i] <= 0.0 || terminalSpeeds[i] > terminalSpeeds[i - 1] + 1e-8)
                return fail(std::string(type) + " terminal displacement does not decrease");
        if (terminalSpeeds.back() >= terminalSpeeds.front() * 0.2)
            return fail(std::string(type) + " still stops at substantial speed");
        std::cout << "Arrival " << type << " veteran=" << veteran << " cells=" << path.size()
            << " final displacement=" << terminalSpeeds.front() << " -> " << terminalSpeeds.back() << '\n';
        return true;
    };
    for (const char* type : {"MTNK", "HTNK", "LTNK", "FV", "HARV", "CMIN", "AMCV", "SMCV", "PCV", "LCRF"})
        if (!checkRoute(type, 0, {{5,4}, {6,4}, {7,4}, {8,4}, {9,4}, {10,4}})) return false;
    if (!checkRoute("HTNK", 0, {{5,4}})
        || !checkRoute("MTNK", 0, {{5,5}})
        || !checkRoute("HTNK", 0, {{5,5}, {6,6}, {7,7}, {8,8}})
        || !checkRoute("FV", 0, {{5,4}, {6,4}, {6,5}, {6,6}})
        || !checkRoute("MTNK", 1, {{5,4}, {6,4}, {7,4}, {8,4}})) return false;

    // A temporary route end beside the destination must not trigger braking;
    // attack pursuit, foot units, biological units, ships and aircraft retain
    // their own movement rules.
    auto eligibility = makeProbe("HTNK");
    auto& eligibleObject = eligibility->world.accessObjects().front();
    RuntimeObjectBehavior candidate;
    candidate.kind = RuntimeBehaviorKind::Move;
    candidate.moveTarget = {6,4};
    const int cruise = runtimeMovementSegmentIntervalTicks(*eligibility, eligibleObject, {4,4}, {5,4});
    if (consumeRuntimeMovementSegmentIntervalTicks(*eligibility, eligibleObject, candidate, {4,4}, {5,4}) != cruise
        || candidate.renderArrivalDistanceAfter >= 0.0) return fail("temporary route end brakes");
    candidate.moveTarget = {5,4}; candidate.kind = RuntimeBehaviorKind::Attack;
    const double pursuitCruise = runtimeMovementSegmentIntervalQ16(*eligibility, eligibleObject, {4,4}, {5,4}) / 65536.0;
    if (std::abs(consumeRuntimeMovementSegmentIntervalTicks(*eligibility, eligibleObject, candidate, {4,4}, {5,4}) - pursuitCruise) >= 1.0
        || candidate.renderArrivalDistanceAfter >= 0.0)
        return fail("attack pursuit brakes at each route end");
    for (const auto& sample : std::vector<std::pair<const char*, WorldObjectKind>>{
            {"E1", WorldObjectKind::Infantry}, {"SHAD", WorldObjectKind::Unit},
            {"DTRUCK", WorldObjectKind::Unit}, {"DLPH", WorldObjectKind::Unit}, {"DEST", WorldObjectKind::Unit}}) {
        WorldObjectInstance object = eligibleObject;
        object.type = sample.first; object.kind = sample.second;
        if (findRuntimeUnitType(eligibility->rules, object) == nullptr)
            return fail("missing class fixture: " + object.type);
        // DTRUCK is an ordinary wheeled vehicle and must participate.
        if (runtimeObjectUsesVehicleArrival(*eligibility, object) != (object.type == "DTRUCK"))
            return fail("wrong eligible class: " + object.type);
    }

    auto active = makeProbe("HTNK");
    if (!beginRoute(*active, {{5,4}, {6,4}, {7,4}})) return fail("save/retarget setup");
    const int id = active->world.getObjects().front().instanceId;
    for (int tick = 0; tick < 200; ++tick) {
        step(*active);
        const auto found = active->behaviors.find(id);
        if (found != active->behaviors.end() && found->second.renderArrivalDistanceAfter == 0.0
            && found->second.renderMoveTicksRemaining < found->second.renderMoveTicksTotal - 2) break;
    }
    if (!active->behaviors.contains(id) || active->behaviors.at(id).renderArrivalDistanceAfter != 0.0)
        return fail("could not capture mid-braking pose");
    std::ostringstream saved;
    if (!writeRuntimeStateToStream(*active, saved, error)) return false;
    IniFile ini;
    if (!ini.loadFromString(saved.str())) return fail("save INI");
    auto loaded = std::make_unique<RuntimeWindowState>();
    seedRuntimeLoadScratch(*loaded, *active, active->visionEnabled);
    if (!materializeRuntimeLoadStateFromIni(*loaded, ini, error)) {
        error = "full braking save load: " + error; return false;
    }
    if (position(*findObjectById(*active, id)) != position(*findObjectById(*loaded, id))
        || active->behaviors.at(id).moveCooldownTicks != loaded->behaviors.at(id).moveCooldownTicks)
        return fail("load lost braking position/timing");
    for (int tick = 0; tick < 1000 && (active->behaviors.contains(id) || loaded->behaviors.contains(id)); ++tick) {
        step(*active); step(*loaded);
        if (active->behaviors.contains(id) != loaded->behaviors.contains(id)
            || position(*findObjectById(*active, id)) != position(*findObjectById(*loaded, id)))
            return fail("loaded braking diverged from uninterrupted arrival");
    }
    if (active->behaviors.contains(id) || loaded->behaviors.contains(id)) return fail("loaded arrival stalled");
    IniFile legacy;
    if (!legacy.loadFromString("[RuntimeSave]\nVersion=1\n")
        || !loadRuntimeVehicleArrivals(*loaded, legacy, error)) return fail("optional section breaks older saves");
    IniFile invalid;
    invalid.loadFromString("[VehicleMovePresentation]\nSchema=1\nCount=1\n0=bad\n");
    std::string invalidError;
    if (loadRuntimeVehicleArrivals(*loaded, invalid, invalidError)) return fail("malformed presentation accepted");

    if (!beginRoute(*active, {{8,4}})) return fail("retarget setup");
    step(*active); step(*active); step(*active);
    for (const RuntimeCellPoint target : {RuntimeCellPoint{14,4}, RuntimeCellPoint{4,12}}) {
        const auto before = position(*findObjectById(*active, id));
        const int cooldown = active->behaviors.at(id).moveCooldownTicks;
        issueRuntimeMoveCommand(*active, target);
        if (active->lastCommand.acceptedCount != 1 || !active->behaviors.contains(id)
            || position(*findObjectById(*active, id)) != before
            || active->behaviors.at(id).moveCooldownTicks != cooldown)
            return fail("new move order snaps or discards the active segment");
        step(*active);
    }
    for (int tick = 0; tick < 1000 && active->behaviors.contains(id); ++tick) step(*active);
    if (active->behaviors.contains(id) || runtimeCellPointForObject(*findObjectById(*active, id)) != RuntimeCellPoint{4,12})
        return fail("retargeted vehicle did not reach its new destination");
    std::cout << "Vehicle arrival: 15 routes, temporary paths, unit classes, full save/load, repeated retarget passed\n";
    return true;
}
