bool runRuntimeBuildingCollapseSmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "Building collapse: " + why; return false; };
    auto storage = std::make_unique<RuntimeWindowState>(); auto& state = *storage;
    state.config = config; state.config.computerAiEnabled = false;
    state.config.playerSlots.clear(); state.config.gameMode = "Battle";
    state.config.gameModeRulesOverride.clear(); state.config.crates = false;
    state.runtimePreferencePersistenceEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true; state.runtimeSoundsEnabled = false;
    state.report = buildRuntimeStartupReport(state.config, &state.world,
        &state.theater, &state.art, &state.rules, &state.mapFacts);
    if (!state.report.success) return fail("stock data unavailable");
    state.rulesLoaded = state.report.rulesLoaded; state.artLoaded = state.report.artLoaded;
    state.theaterLoaded = state.report.theaterLoaded; state.mapFactsLoaded = false;
    state.mapFacts = {}; state.visionEnabled = false; state.triggers.bootstrapComplete = true;
    state.economy.owner = "Americans";
    state.world.accessObjects().clear(); state.world.accessTerrainObjects().clear();
    state.world.accessTerrainCells().clear(); state.world.accessSmudges().clear();
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        cell.tileIndex = 0; cell.subTileIndex = 0; state.world.accessTerrainCells().push_back(cell);
    }
    initializeRuntimeNavigation(state);
    int nextId = 1;
    const auto add = [&](const std::string& type) {
        WorldObjectInstance object; object.instanceId = nextId++;
        object.mapId = "Collapse" + std::to_string(object.instanceId);
        object.type = type; object.kind = WorldObjectKind::Structure; object.owner = "Neutral";
        object.x = 80; object.y = 80; object.cell = runtimeCellKey(80, 80);
        object.alive = true; object.health = 1000; object.mission = "Guard";
        state.world.accessObjects().push_back(object);
        invalidateRuntimeObjectProjectionCache(state); invalidateRuntimeTickOccupancyCache(state);
        return object.instanceId;
    };
    const auto die = [&](int id, RuntimeObjectExitKind kind = RuntimeObjectExitKind::Destroyed) {
        RuntimeObjectExitRequest request; request.objectId = id; request.kind = kind; request.tick = state.tick;
        request.suppressCrew = true; request.suppressDeathWeapon = true;
        return finalizeRuntimeObjectExit(state, request);
    };
    const auto advance = [&](int ticks) { for (int i = 0; i < ticks; ++i) { ++state.tick; advanceRuntimeEffects(state); } };
    const char* output = std::getenv("CONCORD_BUILDING_COLLAPSE_OUTPUT");
    const auto capture = [&](const std::string& name, int expected) {
        if (!output) return true;
        std::filesystem::create_directories(output);
        const auto rendered = drawRuntimeStateOffscreenAtCell(state, {80, 80}, 960, 800, error,
            (std::filesystem::path(output) / (name + ".bmp")).string(), false, false, -1, nullptr,
            false, nullptr, false, true);
        if (!rendered) return false;
        if (rendered->buildingCollapseSprites != expected || rendered->buildingCollapseMissing != 0)
            return fail(name + " missing original collapse sprite");
        return true;
    };
    int buildings = 0;
    for (const auto& rule : state.rules.getBuildingTypes()) {
        if (rule.destroyAnim.empty()) continue;
        state.world.accessObjects().clear(); state.buildingCollapses.clear(); state.impactEffects.clear();
        state.skirmishCrates.clear();
        const int id = add(rule.id);
        const bool images = rule.id == "CAWASH01" || rule.id == "CAPARS01" || rule.id == "CASTL04" || rule.id == "CANEWY04";
        if (images && !capture(rule.id + "-intact", 0)) return false;
        if (rule.id == "CAPARS01") {
            if (!runtimeApplyCrateObjectOnlyDamage(state, {80, 80}, id, 100000, "HE", 11))
                return fail("actual lethal projectile route failed");
        } else if (!die(id).accepted) return fail(rule.id + " destruction rejected");
        if (state.buildingCollapses.size() != 1) return fail(rule.id + " collapse absent or duplicated");
        const auto collapse = state.buildingCollapses.front();
        const auto* source = findRuntimeObjectByIdIncludingInactive(state, id);
        if (!source || source->alive || source->health != 0 || source->rubble != rule.leaveRubble
            || state.behaviors.contains(id) || collapse.animId != rule.destroyAnim || collapse.frameCount <= 1)
            return fail(rule.id + " live authority, rubble or original animation mismatch");
        if (!die(id).duplicate || state.buildingCollapses.size() != 1) return fail("duplicate death restarted collapse");
        if (images && !capture(rule.id + "-collapse-start", 1)) return false;
        const int duration = static_cast<int>((static_cast<long long>(collapse.frameCount) * 256 * state.config.fixedTickHz
            + collapse.rate * 15 - 1) / (collapse.rate * 15));
        advance(std::max(1, duration / 2));
        const int middleFrame = runtimeBuildingCollapseFrame(collapse, state.tick, state.config.fixedTickHz);
        if (middleFrame <= 0 || middleFrame >= collapse.frameCount || state.buildingCollapses.empty())
            return fail(rule.id + " animation did not progress at original Rate");
        std::ostringstream saved; if (!writeRuntimeStateToStream(state, saved, error)) return false;
        IniFile ini; if (!ini.loadFromString(saved.str())) return fail("save unreadable");
        state.buildingCollapses.clear();
        if (!restoreRuntimeBuildingCollapses(state, ini, error) || state.buildingCollapses.size() != 1
            || serializeRuntimeBuildingCollapse(state.buildingCollapses.front()) != serializeRuntimeBuildingCollapse(collapse)
            || runtimeBuildingCollapseFrame(state.buildingCollapses.front(), state.tick, state.config.fixedTickHz) != middleFrame)
            return fail("save restarted or lost the collapse");
        if (images && !capture(rule.id + "-collapse-middle", 1)) return false;
        while (runtimeBuildingCollapseFrame(collapse, state.tick + 1, state.config.fixedTickHz) < collapse.frameCount) advance(1);
        if (images && !capture(rule.id + "-collapse-last", 1)) return false;
        advance(1);
        if (!state.buildingCollapses.empty() || !findRuntimeObjectByIdIncludingInactive(state, id)->rubble)
            return fail(rule.id + " collapse did not finish in original rubble");
        if (images && !capture(rule.id + "-rubble", 0)) return false;
        std::cout << rule.id << ": " << collapse.animId << " frames=" << collapse.frameCount
            << " rate=" << collapse.rate << " ticks=" << duration << '\n';
        if (output && rule.id == "CANEWY04" && std::none_of(state.renderCache.namedPalettesByKey.begin(),
                state.renderCache.namedPalettesByKey.end(), [](const auto& entry) {
                    return entry.first.find("|named|libtem.pal") != std::string::npos && entry.second.loaded;
                })) return fail("Statue of Liberty did not resolve its original libtem palette");
        ++buildings;
    }
    if (buildings != 13) return fail("expected all 13 stock landmark types");
    state.world.accessObjects().clear(); state.skirmishCrates.clear(); state.impactEffects.clear();
    const int ordinary = add("GAPOWR");
    if (!die(ordinary).accepted || !state.buildingCollapses.empty()
        || findRuntimeObjectByIdIncludingInactive(state, ordinary)->rubble) return fail("ordinary building gained a landmark collapse");
    for (const auto kind : {RuntimeObjectExitKind::Sold, RuntimeObjectExitKind::TemporalDestroyed,
            RuntimeObjectExitKind::Consumed, RuntimeObjectExitKind::TransformSource, RuntimeObjectExitKind::ScriptRemoved}) {
        const int id = add("CAWASH01");
        if (!die(id, kind).accepted || !state.buildingCollapses.empty()
            || findRuntimeObjectByIdIncludingInactive(state, id)->rubble) return fail("non-destruction created collapse or rubble");
    }
    state.world.accessObjects().clear(); state.skirmishCrates.clear(); state.impactEffects.clear();
    const int hidden = add("CAWASH01");
    if (!die(hidden).accepted) return fail("hidden landmark death failed");
    WorldObjectInstance observer; observer.instanceId = nextId++; observer.mapId = "CollapseObserver";
    observer.kind = WorldObjectKind::Infantry; observer.type = "E1"; observer.owner = "Americans";
    observer.alive = true; observer.health = 100; observer.x = 8; observer.y = 8; observer.cell = runtimeCellKey(8, 8);
    state.world.accessObjects().push_back(observer); invalidateRuntimeObjectProjectionCache(state);
    state.visionEnabled = true; state.vision.reset(); updateRuntimeVisionState(state);
    if (!capture("unexplored-landmark-hidden", 0)) return false;
    state.buildingCollapses.clear();
    IniFile legacy;
    if (!restoreRuntimeBuildingCollapses(state, legacy, error) || !state.buildingCollapses.empty()) return fail("legacy save failed");
    std::cout << "Building collapse: " << buildings << " original landmarks, authored frames/rates/palette/shadows, "
        << "actual lethal projectile, complete animation to rubble, duplicate/noncombat exclusions, "
        << "mid-animation saves, legacy saves, shroud and native views passed\n";
    return true;
}
