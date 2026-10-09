bool runRuntimeBuildingRevealSmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "Building reveal: " + why; return false; };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& state = *storage;
    state.config = config;
    state.config.computerAiEnabled = false;
    state.config.playerSlots.clear();
    state.config.gameMode = "Battle";
    state.config.gameModeRulesOverride.clear();
    state.runtimePreferencePersistenceEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.runtimeSoundsEnabled = false;
    state.report = buildRuntimeStartupReport(state.config, &state.world,
        &state.theater, &state.art, &state.rules, &state.mapFacts);
    if (!state.report.success) return fail("original assets unavailable");
    state.rulesLoaded = state.report.rulesLoaded;
    state.artLoaded = state.report.artLoaded;
    state.theaterLoaded = state.report.theaterLoaded;
    state.mapFacts = {}; state.mapFactsLoaded = false;
    state.visionEnabled = true;
    state.economy.owner = "Americans";
    state.triggers.bootstrapComplete = true;
    state.world.accessObjects().clear();
    state.world.accessTerrainObjects().clear();
    state.world.accessTerrainCells().clear();
    state.world.accessSmudges().clear();
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x; cell.tileIndex = 0;
        state.world.accessTerrainCells().push_back(cell);
    }
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    const auto add = [&](const std::string& type, const std::string& owner, int x, int y, bool structure) {
        WorldObjectInstance object;
        object.instanceId = static_cast<int>(state.world.getObjects().size()) + 1;
        object.mapId = "Reveal" + std::to_string(object.instanceId);
        object.type = type; object.owner = owner; object.x = x; object.y = y;
        object.cell = runtimeCellKey(x, y); object.health = 1000; object.alive = true;
        object.kind = structure ? WorldObjectKind::Structure : WorldObjectKind::Infantry;
        object.mission = "Guard";
        state.world.accessObjects().push_back(object);
    };
    add("E1", "Americans", 8, 8, false);
    add("INIT", "YuriCountry", 115, 115, false);
    add("NAMISL", "Russians", 80, 80, true);
    const auto advance = [&] { ++state.tick; updateRuntimeVisionState(state); };
    const char* outputRoot = std::getenv("CONCORD_BUILDING_REVEAL_OUTPUT");
    std::vector<std::uint32_t> hiddenPixels, revealedPixels;
    const auto capture = [&](const char* name, std::vector<std::uint32_t>& pixels) {
        if (!outputRoot) return true;
        std::filesystem::create_directories(outputRoot);
        return drawRuntimeStateOffscreenAtCell(state, {80, 80}, 640, 480, error,
            (std::filesystem::path(outputRoot) / name).string(), false, false, -1, &pixels,
            false, nullptr, false, true).has_value();
    };
    int stockBuildings = 0;
    for (const auto& rule : state.rules.getBuildingTypes()) {
        if (!rule.revealToAll) continue;
        auto& building = state.world.accessObjects()[2];
        building.type = rule.id; building.owner = "Russians";
        building.buildAnimTicksRemaining = 2; building.poweredOperational = false;
        state.vision.reset(); state.triggers.publicBuildingLocations.clear(); state.playerLastRadarCell.reset();
        advance();
        if (state.vision.isCellExploredBy("Americans", 80, 80)
            || state.vision.isCellExploredBy("YuriCountry", 80, 80)
            || !state.triggers.publicBuildingLocations.empty())
            return fail(rule.id + " revealed before construction finished");
        if (rule.id == "NAMISL" && !capture("under-construction-hidden.bmp", hiddenPixels)) return false;
        building.buildAnimTicksRemaining = 0;
        advance();
        const int radius = rule.sight + 3;
        if (!state.vision.isCellExploredBy("Americans", 80 + radius, 80)
            || !state.vision.isCellExploredBy("YuriCountry", 80, 80)
            || state.vision.isCellExploredBy("Americans", 81 + radius, 80)
            || state.vision.isCellVisibleTo("Americans", 80, 80)
            || state.triggers.publicBuildingLocations.size() != 1 || !state.playerLastRadarCell)
            return fail(rule.id + " did not reveal its original radius to other houses while unpowered");
        if (rule.id == "NAMISL" && !capture("completed-revealed.bmp", revealedPixels)) return false;
        const auto noticeTick = state.triggers.publicBuildingLocations.at(3).noticeTick;
        advance();
        if (state.triggers.publicBuildingLocations.at(3).noticeTick != noticeTick
            || !runtimePublicBuildingNoticeVisible(state, state.triggers.publicBuildingLocations.at(3)))
            return fail("notice repeated or did not become visible");
        ++stockBuildings;
    }
    if (stockBuildings != 7) return fail("expected six superweapons and the original dummy chronosphere");
    if (outputRoot && (hiddenPixels.empty() || revealedPixels.empty() || hiddenPixels == revealedPixels))
        return fail("actual hidden/revealed views did not differ");
    auto& building = state.world.accessObjects()[2];
    building.type = "GAPOWR";
    state.triggers.publicBuildingLocations.clear(); state.vision.reset();
    advance();
    if (state.vision.isCellExploredBy("Americans", 80, 80) || !state.triggers.publicBuildingLocations.empty())
        return fail("ordinary power plant exposed its location");
    building.type = "NAMISL"; advance();
    const auto builtTick = state.triggers.publicBuildingLocations.at(3).noticeTick;
    building.owner = "YuriCountry"; advance();
    const auto captured = state.triggers.publicBuildingLocations.at(3);
    if (captured.owner != "yuricountry" || captured.noticeTick <= builtTick)
        return fail("capture did not issue a new location notice");
    state.economy.owner = "YuriCountry";
    if (runtimePublicBuildingNoticeVisible(state, captured)) return fail("owner received its own enemy-building alert");
    state.economy.owner = "Americans";
    state.vision.resetShroud("Americans"); advance();
    if (!state.vision.isCellExploredBy("Americans", 80, 80)
        || state.triggers.publicBuildingLocations.at(3).noticeTick != captured.noticeTick)
        return fail("reshrouding lost the original public location or replayed the alert");
    std::ostringstream saved;
    if (!writeRuntimeStateToStream(state, saved, error)) return false;
    IniFile ini; if (!ini.loadFromString(saved.str())) return fail("saved state unreadable");
    state.triggers.publicBuildingLocations.clear();
    if (!restoreRuntimePublicBuildingLocations(state, ini, error)
        || state.triggers.publicBuildingLocations.size() != 1
        || state.triggers.publicBuildingLocations.at(3).noticeTick != captured.noticeTick
        || state.triggers.publicBuildingLocations.at(3).owner != captured.owner) return fail("saved notice lost");
    advance();
    if (state.triggers.publicBuildingLocations.at(3).noticeTick != captured.noticeTick)
        return fail("loading replayed a completed construction alert");
    IniFile legacy;
    if (!restoreRuntimePublicBuildingLocations(state, legacy, error)) return false;
    advance();
    if (state.triggers.publicBuildingLocations.at(3).noticeTick != -1
        || runtimePublicBuildingNoticeVisible(state, state.triggers.publicBuildingLocations.at(3)))
        return fail("legacy save reconstruction replayed an alert");
    add("GAGAP", "Russians", 80, 80, true);
    advance();
    if (state.vision.isCellExploredBy("Americans", 80, 80)) return fail("public location pierced a working Gap Generator");
    state.world.accessObjects().back().poweredOperational = false;
    advance();
    if (!state.vision.isCellExploredBy("Americans", 80, 80)) return fail("depowered gap did not restore the public location");
    state.world.accessObjects()[2].alive = false; state.world.accessObjects()[2].health = 0;
    state.vision.resetShroud("Americans"); advance();
    if (state.vision.isCellExploredBy("Americans", 80, 80)) return fail("destroyed superweapon kept revealing terrain");
    std::cout << "Building reveal: seven stock buildings, construction completion, capture, no-power visibility, "
        << "radius, radar notices, ordinary-building exclusion, reshroud, gap, saved/legacy alerts and actual views passed\n";
    return true;
}
