bool runRuntimeMapLightSmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "Map lights: " + why; return false; };
    auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
    state.config = config; state.config.computerAiEnabled = false;
    state.config.playerSlots.clear(); state.config.gameMode = "Battle";
    state.config.gameModeRulesOverride.clear(); state.config.crates = false;
    state.runtimePreferencePersistenceEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true; state.runtimeSoundsEnabled = false;
    state.report = buildRuntimeStartupReport(state.config, &state.world,
        &state.theater, &state.art, &state.rules, &state.mapFacts);
    if (!state.report.success) return fail("original data unavailable");
    state.rulesLoaded = state.report.rulesLoaded; state.artLoaded = state.report.artLoaded;
    state.theaterLoaded = state.report.theaterLoaded; state.mapFactsLoaded = true;
    state.mapFacts.lighting.base = {0.35, 1.0, 1.0, 1.0, 0, 0};
    state.lighting.reset(state.mapFacts.lighting); state.lightingInitialized = true;
    state.visionEnabled = false; state.triggers.bootstrapComplete = true;
    state.economy.owner = "Americans"; state.economy.skirmishStartApplied = true;
    state.world.accessObjects().clear(); state.world.accessTerrainObjects().clear();
    state.world.accessTerrainCells().clear(); state.world.accessSmudges().clear();
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        cell.tileIndex = 0; cell.subTileIndex = 0; state.world.accessTerrainCells().push_back(cell);
    }
    initializeRuntimeNavigation(state);
    const auto add = [&](const std::string& type, int id, int x, int y, WorldObjectKind kind) {
        WorldObjectInstance object; object.instanceId = id; object.mapId = "Light" + std::to_string(id);
        object.type = type; object.kind = kind; object.owner = kind == WorldObjectKind::Structure ? "Neutral" : "Americans";
        object.x = x; object.y = y; object.cell = runtimeCellKey(x, y); object.health = 600;
        object.alive = true; object.mission = "Guard";
        state.world.accessObjects().push_back(object); invalidateRuntimeObjectProjectionCache(state);
    };
    add("MTNK", 1, 81, 80, WorldObjectKind::Unit);
    add("E1", 2, 82, 81, WorldObjectKind::Infantry);
    add("GAPOWR", 3, 77, 78, WorldObjectKind::Structure);
    add("INGALITE", 4, 80, 80, WorldObjectKind::Structure);
    ensureRuntimeSubcellsInitialized(state);
    const auto tint = [&](double x, double y) {
        return runtimeMapLightTintAt(runtimeMapLightSources(state.world, &state.rules),
            state.lighting.renderTint(), &state.lighting.currentProfile(), x, y);
    };
    const auto equal = [](const RuntimeLightingTint& a, const RuntimeLightingTint& b) {
        return std::abs(a.red - b.red) < 1e-9 && std::abs(a.green - b.green) < 1e-9 && std::abs(a.blue - b.blue) < 1e-9;
    };
    int count = 0;
    for (const auto& type : state.rules.getBuildingTypes()) {
        if (!type.lightIntensity) continue;
        auto* lamp = findMutableObjectById(state, 4); lamp->type = type.id;
        auto sources = runtimeMapLightSources(state.world, &state.rules);
        if (sources.size() != 1 || sources.front().visibility != type.lightVisibility)
            return fail(type.id + " source/radius missing");
        if (equal(tint(80, 80), state.lighting.renderTint())) return fail(type.id + " has no illumination");
        const double radius = type.lightVisibility / 256.0;
        if (!equal(tint(80 + radius, 80), state.lighting.renderTint())
            || !equal(tint(80 + radius + 1, 80), state.lighting.renderTint())) return fail("light escaped its authored radius");
        if (type.invisibleInGame && (runtimeObjectVisibleToOwner(state, "Americans", *lamp)
            || !type.immune || !runtimeStructureFootprintCellsForObject(state, *lamp).empty()
            || runtimeStructureCellBlocksVehicleNavigation(state, *lamp, {80, 80})))
            return fail(type.id + " invisible source selectable, vulnerable or blocking navigation");
        const auto room = state.subcells.availableSubCells(state.world.getObjects(), state.world.getTerrainObjects(),
            {80, 80, runtimeCellKey(80, 80), false}, {}, runtimeGateSubcellContext(state));
        if (room.empty() == type.invisibleInGame) return fail(type.id + " infantry passage differs from lamp visibility");
        lamp->poweredOperational = false;
        if (runtimeMapLightSources(state.world, &state.rules).empty() != type.powered) return fail("Powered rule ignored");
        lamp->poweredOperational = true; lamp->buildAnimTicksRemaining = 1;
        if (!runtimeMapLightSources(state.world, &state.rules).empty()) return fail("unfinished source emitted light");
        lamp->buildAnimTicksRemaining = 0;
        ++count;
    }
    if (count != 23) return fail("expected 23 original lamp types, got " + std::to_string(count));
    const char* output = std::getenv("CONCORD_MAP_LIGHT_OUTPUT");
    const std::string root = output ? output : "build/smoke/map-light-probe";
    std::filesystem::create_directories(root);
    const auto capture = [&](const std::string& name, int sources) {
        const auto stats = drawRuntimeStateOffscreenAtCell(state, {80, 80}, 960, 800, error,
            output ? root + "/" + name + ".bmp" : "", false, false, -1, nullptr, false, nullptr, false, true);
        if (!stats) return false;
        std::cout << name << " sources=" << stats->mapLightSources << " receivers=" << stats->mapLightReceivers << '\n';
        if (stats->mapLightSources != sources || (sources && stats->mapLightReceivers < 10))
            return fail(name + " did not illuminate actual terrain and objects");
        return true;
    };
    findMutableObjectById(state, 4)->alive = false;
    if (!capture("01-dark-baseline", 0)) return false;
    state.world.accessObjects().back().alive = true;
    for (const auto* id : {"GALITE", "INREDLMP", "INGRNLMP", "INBLULMP", "NEGLAMP", "NEGRED"}) {
        findMutableObjectById(state, 4)->type = id;
        const auto value = tint(80, 80);
        if (std::string(id) == "INREDLMP" && !(value.red > value.green && value.red > value.blue)) return fail("red tint missing");
        if (std::string(id) == "INGRNLMP" && !(value.green > value.red && value.green > value.blue)) return fail("green tint missing");
        if (std::string(id) == "INBLULMP" && !(value.blue > value.red && value.blue > value.green)) return fail("blue tint missing");
        if (std::string(id) == "NEGLAMP" && !(value.red < 0.35 && value.green < 0.35 && value.blue < 0.35)) return fail("negative light not darkening");
        if (std::string(id) == "NEGRED" && !(value.red < value.green && value.red < value.blue)) return fail("negative red tint missing");
        if (!capture(id, 1)) return false;
    }
    findMutableObjectById(state, 4)->type = "INGALITE";
    findMutableObjectById(state, 4)->poweredOperational = false;
    if (!capture("08-power-off", 0)) return false;
    findMutableObjectById(state, 4)->poweredOperational = true;
    // Illumination remains additive while the existing map/trigger profile changes.
    state.lighting.activateProfile(RuntimeLightingMode::Dominator, state.tick, false);
    if (!capture("09-dominator-with-lamp", 1)) return false;
    state.lighting.restoreBase(state.tick, false);
    const auto passage = runtimeGateSubcellContext(state);
    const auto reservation = state.subcells.reserveForDestination(state.world.getObjects(), state.world.getTerrainObjects(),
        2, {80, 80, runtimeCellKey(80, 80), false}, passage);
    if (!reservation.reserved() || !state.subcells.commit(state.world.accessObjects(), state.world.getTerrainObjects(), 2, passage).committed())
        return fail("infantry cannot enter the invisible light cell");
    const auto before = tint(80, 80);
    if (!saveRuntimeState(state, root + "/map-lights-save.ini", error)) return fail("save: " + error);
    findMutableObjectById(state, 4)->alive = false;
    if (!loadRuntimeState(state, root + "/map-lights-save.ini", error)) return fail("load: " + error);
    if (runtimeMapLightSources(state.world, &state.rules).size() != 1 || !equal(tint(80, 80), before))
        return fail("restored world lost lamp or changed its lighting");
    if (!capture("10-restored-light", 1)) return false;
    findMutableObjectById(state, 4)->type = "GALITE";
    RuntimeObjectExitRequest request; request.objectId = 4; request.tick = state.tick;
    request.kind = RuntimeObjectExitKind::Destroyed; request.suppressCrew = true; request.suppressDeathWeapon = true;
    if (!finalizeRuntimeObjectExit(state, request).accepted || !runtimeMapLightSources(state.world, &state.rules).empty())
        return fail("destroyed light is still active");
    state.world.accessObjects().clear(); state.impactEffects.clear();
    add("INGALITE", 11, 80, 80, WorldObjectKind::Structure);
    add("E1", 12, 8, 8, WorldObjectKind::Infantry);
    state.visionEnabled = true; state.vision.reset(); updateRuntimeVisionState(state);
    if (!capture("11-unexplored-hidden", 1)) return false;
    // Real map overrides pass through the same merged rules as other buildings.
    RuntimeConfig overrideConfig = config;
    overrideConfig.defaultMapPath = (std::filesystem::path(config.localDataRootPath) / "resources/mix_exports/multimd/saharami.map").string();
    auto mapStorage = std::make_unique<RuntimeWindowState>(); auto& actualMap = *mapStorage;
    actualMap.config = overrideConfig; actualMap.runtimePreferencePersistenceEnabled = false;
    actualMap.runtimeSuppressPlayerAudioFeedback = true; actualMap.runtimeSoundsEnabled = false;
    actualMap.report = buildRuntimeStartupReport(overrideConfig, &actualMap.world, &actualMap.theater,
        &actualMap.art, &actualMap.rules, &actualMap.mapFacts);
    actualMap.rulesLoaded = actualMap.report.rulesLoaded; actualMap.artLoaded = actualMap.report.artLoaded;
    actualMap.theaterLoaded = actualMap.report.theaterLoaded; actualMap.mapFactsLoaded = true;
    actualMap.economy.owner = "Americans"; actualMap.visionEnabled = false;
    actualMap.lighting.reset(actualMap.mapFacts.lighting); actualMap.lightingInitialized = true;
    const auto* authored = actualMap.rules.findBuildingType("TEMDUSLAMP");
    if (!actualMap.report.success || !authored || authored->lightIntensity != 5 || authored->lightRedTint != 600
        || authored->lightGreenTint != 400 || authored->lightBlueTint != 400 || authored->lightVisibility != 3000)
        return fail("Sahara Mirage map lamp overrides not applied");
    const auto originalSources = runtimeMapLightSources(actualMap.world, &actualMap.rules);
    if (originalSources.empty()) return fail("original Sahara Mirage lamps missing from world");
    const auto& firstSource = originalSources.front();
    const auto mapView = drawRuntimeStateOffscreenAtCell(actualMap,
        {static_cast<int>(firstSource.x), static_cast<int>(firstSource.y)}, 960, 800, error,
        output ? root + "/12-original-saharami-map.bmp" : "", false);
    if (!mapView || mapView->mapLightSources != originalSources.size()) return fail("original map did not draw its lights");
    std::cout << "Map lights: all 23 stock sources, colors, negative intensity/tint, radius, power, destruction, "
        << "InvisibleInGame, actual SHP/VXL/TMP, changing ambient, saves, shroud and Sahara Mirage overrides passed\n";
    return true;
}
