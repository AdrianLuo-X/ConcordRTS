bool runRuntimeBehindMarkerSmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail = [&](const std::string& message) { error = "Behind marker: " + message; return false; };
    const char* requestedRoot = std::getenv("CONCORD_BEHIND_MARKER_OUTPUT");
    const std::string output = requestedRoot ? requestedRoot : "build/smoke/gameplay/08-marker-probe";
    std::filesystem::create_directories(output);
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& state = *storage;
    state.config = config;
    state.config.computerAiEnabled = false;
    state.config.playerSlots.clear();
    state.config.gameMode = "Battle";
    state.config.gameModeRulesOverride.clear();
    state.config.seeHiddenObjects = true;
    state.runtimePreferencePersistenceEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.runtimeSoundsEnabled = false;
    state.report = buildRuntimeStartupReport(state.config, &state.world,
        &state.theater, &state.art, &state.rules, &state.mapFacts);
    if (!state.report.success) return fail("original assets unavailable");
    state.theaterLoaded = state.report.theaterLoaded;
    state.artLoaded = state.report.artLoaded;
    state.rulesLoaded = state.report.rulesLoaded;
    state.mapFactsLoaded = false;
    state.world.accessObjects().clear();
    state.world.accessTerrainObjects().clear();
    state.world.accessTerrainCells().clear();
    state.world.accessSmudges().clear();
    for (int y = 64; y <= 96; ++y) for (int x = 64; x <= 96; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y;
        cell.cellIndex512 = runtimeCellKey(x, y); cell.tileIndex = 0;
        state.world.accessTerrainCells().push_back(cell);
    }
    state.economy.owner = "Americans";
    state.triggers.bootstrapComplete = true;
    WorldObjectInstance building;
    building.instanceId = 1; building.kind = WorldObjectKind::Structure;
    building.type = "GACNST"; building.owner = "Americans";
    building.alive = true; building.health = state.rules.findBuildingType(building.type)->strength;
    building.x = building.y = 80; building.cell = runtimeCellKey(80, 80); building.mission = "Guard";
    state.world.accessObjects().push_back(building);
    WorldObjectInstance unit;
    unit.instanceId = 2; unit.owner = "Americans"; unit.alive = true;
    unit.health = 100; unit.x = 79; unit.y = 80;
    unit.cell = runtimeCellKey(unit.x, unit.y); unit.mission = "Guard";
    state.world.accessObjects().push_back(unit);
    std::vector<std::uint32_t> enabledPixels, disabledPixels;
    const auto render = [&](const std::string& name, int wanted, bool vision = false,
        std::vector<std::uint32_t>* pixels = nullptr) -> bool {
        const auto result = drawRuntimeStateOffscreenAtCell(state, {80, 80}, 640, 480,
            error, output + "/" + name + ".bmp", false, false, -1, pixels,
            false, nullptr, false, vision);
        if (!result) return false;
        if (result->behindMarkerSprites != wanted || result->behindMarkerMissing != 0)
            return fail(name + " markers=" + std::to_string(result->behindMarkerSprites)
                + " missing=" + std::to_string(result->behindMarkerMissing));
        std::cout << "Behind marker frame: " << name << " markers=" << result->behindMarkerSprites
            << " animation=" << result->behindMarkerFrame << '\n';
        return true;
    };
    state.world.accessObjects()[1].type = "E1";
    state.world.accessObjects()[1].kind = WorldObjectKind::Infantry;
    if (!render("visible-above-construction-yard", 0)) return false;
    building.type = "NANRCT";
    building.health = state.rules.findBuildingType(building.type)->strength;
    state.world.accessObjects()[0] = building;
    if (!render("visible-above-reactor", 0)) return false;
    const char* fixtureBuilding = std::getenv("CONCORD_BEHIND_MARKER_BUILDING");
    building.type = fixtureBuilding ? fixtureBuilding : "CANEWY06";
    const auto* fixtureRules = state.rules.findBuildingType(building.type);
    if (!fixtureRules) return fail("missing original occluding building " + building.type);
    building.health = fixtureRules->strength;
    state.world.accessObjects()[0] = building;
    const char* fixtureX = std::getenv("CONCORD_BEHIND_MARKER_X");
    const char* fixtureY = std::getenv("CONCORD_BEHIND_MARKER_Y");
    const int behindX = fixtureX ? std::atoi(fixtureX) : 79;
    const int behindY = fixtureY ? std::atoi(fixtureY) : 80;
    for (const auto& entry : std::array<std::pair<const char*, WorldObjectKind>, 2>{{
            {"E1", WorldObjectKind::Infantry}, {"MTNK", WorldObjectKind::Unit}}}) {
        auto& actor = state.world.accessObjects()[1];
        actor.type = entry.first; actor.kind = entry.second;
        actor.x = behindX; actor.y = behindY; actor.cell = runtimeCellKey(actor.x, actor.y);
        if (!render(std::string(entry.first) + "-behind", 1, false, &enabledPixels)) return false;
        state.config.seeHiddenObjects = false;
        if (!render(std::string(entry.first) + "-off", 0, false, &disabledPixels)
            || enabledPixels == disabledPixels) return fail("option did not remove silver pixels");
        state.config.seeHiddenObjects = true;
        actor.x = actor.y = 84; actor.cell = runtimeCellKey(84, 84);
        if (!render(std::string(entry.first) + "-front", 0)) return false;
    }
    auto& actor = state.world.accessObjects()[1];
    actor.x = behindX; actor.y = behindY; actor.cell = runtimeCellKey(actor.x, actor.y);
    state.tick = 12;
    if (!render("animated", 1, false, &disabledPixels) || disabledPixels == enabledPixels)
        return fail("authored animation did not advance");
    state.world.accessObjects()[0].alive = false;
    state.world.accessObjects()[0].health = 0;
    if (!render("building-removed", 0)) return false;
    state.world.accessObjects()[0] = building;
    state.economy.owner = "Observer";
    actor.owner = "Russians";
    state.visionEnabled = true;
    updateRuntimeVisionState(state);
    if (!render("unexplored", 0, true)) return false;
    state.vision.revealMap(state.economy.owner);
    actor.cloaked = true;
    if (!render("enemy-cloaked", 0, true)) return false;
    actor.cloaked = false;
    actor.mission = "InsideTransport";
    if (!render("passenger", 0)) return false;
    actor.mission = "Guard";
    actor.tankBunkerHostId = building.instanceId;
    if (!render("bunker-occupant", 0)) return false;
    actor.tankBunkerHostId = -1;
    actor.authoredAircraft = true; actor.airborne = true;
    if (!render("airborne", 0)) return false;

    state.config.configPath = output + "/preferences.ini";
    state.config.seeHiddenObjects = true;
    state.runtimePreferencePersistenceEnabled = true;
    applyRuntimeConfigPreferences(state);
    state.runtimeMenuPage = RuntimeMenuPage::Settings;
    const RECT client{0, 0, 800, 600};
    const auto controls = runtimeMenuContentControls(state, client);
    const auto found = std::find_if(controls.begin(), controls.end(), [](const auto& item) {
        return item.action == RuntimeMenuAction::ToggleHiddenObjects;
    });
    if (found == controls.end() || !found->checked) return fail("menu checkbox unavailable");
    activateRuntimeMenuAction(nullptr, state, RuntimeMenuAction::ToggleHiddenObjects);
    RuntimeConfigOverrides overrides; overrides.configPath = state.config.configPath;
    if (state.config.seeHiddenObjects || loadRuntimeConfig(overrides).seeHiddenObjects)
        return fail("disabled option did not persist");
    activateRuntimeMenuAction(nullptr, state, RuntimeMenuAction::ToggleHiddenObjects);
    if (!state.config.seeHiddenObjects || !loadRuntimeConfig(overrides).seeHiddenObjects)
        return fail("enabled option did not persist");
    std::cout << "Behind marker: stock infantry/vehicle pixels, animated silver asset, foreground/removed building, "
        "shroud/cloak/passenger/airborne exclusions and menu persistence PASS\n";
    return true;
}
