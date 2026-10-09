bool runRuntimeWreckSmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "Wrecks: " + why; return false; };
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
    state.mapFacts = {}; state.visionEnabled = false;
    state.triggers.bootstrapComplete = true; state.economy.owner = "Americans";
    state.world.accessObjects().clear(); state.world.accessTerrainObjects().clear();
    state.world.accessTerrainCells().clear(); state.world.accessSmudges().clear();
    int waterTile = -1;
    for (int tile = 0; tile < 65535; ++tile) {
        TerrainCell candidate; candidate.tileIndex = tile;
        const auto info = runtimeTerrainInfoForCell(state, candidate);
        if (info.water && !info.bridge && !info.shore && !info.cliff) { waterTile = tile; break; }
    }
    if (waterTile < 0) return fail("stock water tile missing");
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        cell.tileIndex = x >= 92 ? waterTile : 0; cell.subTileIndex = 0;
        state.world.accessTerrainCells().push_back(cell);
    }
    initializeRuntimeNavigation(state);
    int nextId = 1;
    const auto add = [&](const std::string& type, bool air, bool water = false) {
        WorldObjectInstance object; object.instanceId = nextId++;
        object.mapId = "Wreck" + std::to_string(object.instanceId);
        object.type = type; object.owner = "Americans";
        object.kind = state.rules.findUnitType("InfantryType", type) ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        object.authoredAircraft = state.rules.findUnitType("AircraftType", type) != nullptr;
        object.x = water ? 100 : 80; object.y = 80; object.cell = runtimeCellKey(object.x, object.y);
        object.health = 1000; object.alive = true; object.airborne = air;
        object.level = air ? 4 : 0; object.flightLevel = object.level;
        object.facing = 80; object.turretFacing = 80; object.mission = "Guard";
        state.world.accessObjects().push_back(object);
        invalidateRuntimeObjectProjectionCache(state); invalidateRuntimeTickOccupancyCache(state);
        return object.instanceId;
    };
    const auto die = [&](int id, RuntimeObjectExitKind kind = RuntimeObjectExitKind::Destroyed) {
        RuntimeObjectExitRequest request; request.objectId = id; request.kind = kind;
        request.tick = state.tick; request.suppressCrew = true; request.suppressDeathWeapon = true;
        return finalizeRuntimeObjectExit(state, request);
    };
    const auto advance = [&](int ticks) { for (int i = 0; i < ticks; ++i) { ++state.tick; advanceRuntimeEffects(state); } };
    const char* output = std::getenv("CONCORD_WRECK_OUTPUT");
    const auto capture = [&](const std::string& name, bool water, int expected) {
        if (!output) return true;
        std::filesystem::create_directories(output);
        const auto rendered = drawRuntimeStateOffscreenAtCell(state, {water ? 100 : 80, 80}, 640, 480, error,
            (std::filesystem::path(output) / (name + ".bmp")).string(), false, false, -1, nullptr,
            false, nullptr, false, true);
        if (!rendered) return false;
        if (rendered->wreckBodySprites != expected) return fail(name + " did not render the retained body");
        return true;
    };
    int airborneTypes = 0;
    for (const char* type : {"ORCA", "BEAG", "SHAD", "ZEP", "DISK", "SCHP", "JUMPJET"}) {
        state.wrecks.clear(); state.impactEffects.clear();
        const int id = add(type, true);
        if (!findRuntimeUnitTypeById(state.rules, type)) return fail(std::string(type) + " stock rules missing");
        if (!die(id).accepted || state.wrecks.size() != 1 || !state.impactEffects.empty())
            return fail(std::string(type) + " disappeared or exploded before falling");
        const auto* dead = findRuntimeObjectByIdIncludingInactive(state, id);
        if (!dead || dead->alive || dead->health != 0 || state.behaviors.contains(id))
            return fail("wreck retained live gameplay authority");
        if (!die(id).duplicate || state.wrecks.size() != 1) return fail("repeated death duplicated the wreck");
        const auto initial = state.wrecks.front();
        const bool images = std::string(type) == "ORCA" || std::string(type) == "ZEP" || std::string(type) == "DISK";
        if (images && !capture(std::string(type) + "-fall-start", false, 1)) return false;
        advance(12);
        if (state.wrecks.size() != 1) return fail(std::string(type) + " fall ended too soon");
        const auto pose = runtimeWreckObjectPose(state.wrecks.front(), state.tick, state.config.fixedTickHz);
        if (pose.renderLevel >= initial.startLevel || pose.renderLevel <= 0)
            return fail(std::string(type) + " did not continuously descend");
        std::ostringstream saved; if (!writeRuntimeStateToStream(state, saved, error)) return false;
        IniFile ini; if (!ini.loadFromString(saved.str())) return fail("save unreadable");
        const auto record = serializeRuntimeWreck(state.wrecks.front());
        state.wrecks.clear();
        if (!restoreRuntimeWrecks(state, ini, error) || state.wrecks.size() != 1
            || serializeRuntimeWreck(state.wrecks.front()) != record) return fail("mid-fall save lost its trajectory");
        if (images && !capture(std::string(type) + "-fall-middle", false, 1)) return false;
        int limit = 0;
        while (!state.wrecks.empty() && ++limit < 1200) advance(1);
        if (!state.wrecks.empty()) return fail(std::string(type) + " never reached the ground");
        if (std::none_of(state.impactEffects.begin(), state.impactEffects.end(), [](const auto& effect) {
            return effect.lifecycleState == "WreckImpact" || effect.lifecycleState == "InfantryDeath";
        })) return fail(std::string(type) + " ground impact animation missing");
        if (images && !capture(std::string(type) + "-impact", false, 0)) return false;
        ++airborneTypes;
    }
    state.impactEffects.clear();
    for (const char* type : {"ORCA", "SHAD", "SCHP"}) {
        if (!die(add(type, false)).accepted || !state.wrecks.empty()) return fail("landed aircraft fell from the sky");
    }
    for (auto kind : {RuntimeObjectExitKind::Sold, RuntimeObjectExitKind::TemporalDestroyed,
            RuntimeObjectExitKind::Consumed, RuntimeObjectExitKind::TransformSource, RuntimeObjectExitKind::ScriptRemoved}) {
        if (!die(add("ZEP", true), kind).accepted || !state.wrecks.empty()) return fail("non-combat exit created a wreck");
    }
    for (const char* type : {"SUB", "DLPH", "SQD"}) {
        if (!die(add(type, false, true)).accepted || !state.wrecks.empty()) return fail("submarine or organic body used surface-ship sinking");
    }
    state.impactEffects.clear();
    const int ship = add("DEST", false, true);
    if (!die(ship).accepted || state.wrecks.size() != 1 || !state.wrecks.front().sinking
        || !state.impactEffects.empty()) return fail("destroyer did not enter the original sinking process");
    if (!capture("DEST-sinking-start", true, 1)) return false;
    advance(32);
    if (state.wrecks.size() != 1 || state.impactEffects.empty()) return fail("ship or surface wakes disappeared early");
    if (!capture("DEST-sinking-middle", true, 1)) return false;
    std::ostringstream shipSave; if (!writeRuntimeStateToStream(state, shipSave, error)) return false;
    IniFile shipIni; if (!shipIni.loadFromString(shipSave.str())) return false;
    const auto sinkingRecord = serializeRuntimeWreck(state.wrecks.front()); state.wrecks.clear();
    if (!restoreRuntimeWrecks(state, shipIni, error) || serializeRuntimeWreck(state.wrecks.front()) != sinkingRecord)
        return fail("mid-sinking save lost its position");
    advance(64);
    if (!capture("DEST-sinking-late", true, 1)) return false;
    advance(100);
    if (!state.wrecks.empty() || !capture("DEST-sunk", true, 0)) return fail("sunken ship did not finish");
    if (!die(add("DEST", false, false)).accepted || !state.wrecks.empty()) return fail("ship on dry land sank");
    // A high bridge is the impact surface, not the terrain underneath it.
    auto* bridgeCell = const_cast<TerrainCell*>(state.terrainByCell.at(runtimeCellKey(80, 80)));
    bridgeCell->hasOverlay = true; bridgeCell->overlayIndex = 24; bridgeCell->overlayFrame = 0;
    const int bridgeAircraft = add("SHAD", true);
    auto* bridgePlane = findMutableRuntimeObjectByIdIncludingInactive(state, bridgeAircraft);
    bridgePlane->level = 8; bridgePlane->flightLevel = 8;
    state.impactEffects.clear();
    if (!die(bridgeAircraft).accepted || state.wrecks.size() != 1) return fail("bridge flight did not begin falling");
    int bridgeTicks = 0;
    while (!state.wrecks.empty() && ++bridgeTicks < 1200) advance(1);
    if (!state.wrecks.empty() || std::none_of(state.impactEffects.begin(), state.impactEffects.end(), [](const auto& effect) {
        return effect.lifecycleState == "WreckImpact" && std::abs(effect.worldLevel - 4.0) < 0.001;
    })) return fail("aircraft fell through the high bridge");
    bridgeCell->hasOverlay = false; bridgeCell->overlayIndex = -1;
    // The actual projectile damage route must select the same retained body.
    state.impactEffects.clear();
    const int waterPlane = add("ORCA", true, true);
    if (!runtimeApplyCrateObjectOnlyDamage(state, {100, 80}, waterPlane, 100000, "HE", 11)
        || state.wrecks.size() != 1
        || findRuntimeObjectByIdIncludingInactive(state, waterPlane)->alive)
        return fail("actual lethal projectile damage failed to retain exactly one visual");
    while (!state.wrecks.empty()) advance(1);
    if (std::none_of(state.impactEffects.begin(), state.impactEffects.end(), [](const auto& effect) {
        return effect.animId == "H2O_EXP3";
    })) return fail("aircraft water impact did not splash");
    if (!capture("ORCA-water-impact", true, 0)) return false;
    // Retained enemy bodies do not reveal themselves through unexplored terrain.
    state.impactEffects.clear();
    for (auto& object : state.world.accessObjects()) { object.alive = false; object.health = 0; }
    const int observerId = add("E1", false);
    auto* observer = findMutableRuntimeObjectByIdIncludingInactive(state, observerId);
    observer->x = 8; observer->y = 8; observer->cell = runtimeCellKey(8, 8);
    const int hiddenId = add("ZEP", true);
    findMutableRuntimeObjectByIdIncludingInactive(state, hiddenId)->owner = "Russians";
    if (!die(hiddenId).accepted || state.wrecks.empty()) return fail("hidden enemy wreck absent");
    state.visionEnabled = true; state.vision.reset(); updateRuntimeVisionState(state);
    if (!capture("enemy-wreck-hidden", false, 0)) return false;
    state.wrecks.clear(); state.visionEnabled = false;
    IniFile legacy; if (!restoreRuntimeWrecks(state, legacy, error) || !state.wrecks.empty()) return fail("legacy save failed");
    std::cout << "Wrecks: " << airborneTypes << " stock aircraft, continuous fall, original surface-ship sinking, "
        << "land/water impacts, wreck exclusion from gameplay, duplicate/noncombat/landed/underwater exclusions, "
        << "mid-fall and mid-sinking saves, bridge impacts, enemy shroud, original resources and actual views passed\n";
    return true;
}
