// Include after the simulation, selection and smoke-state helpers. This fixture
// is shared by the headless simulation benchmark and the live frame benchmark.
namespace RuntimeMassMovementBenchmarkDetail {
using Bytes = std::vector<unsigned char>;

void append16(Bytes& out, unsigned int value) {
    out.push_back(static_cast<unsigned char>(value));
    out.push_back(static_cast<unsigned char>(value >> 8));
}

void append32(Bytes& out, unsigned int value) {
    append16(out, value);
    append16(out, value >> 16);
}

// Same legal, literal-only Format5/LZO1X representation as RuntimeRandomMap.
// The real map decoder and world hydration consume the fixture; no private
// GameWorld summary fields or hand-authored movement paths are patched.
Bytes packTiles(const Bytes& raw) {
    Bytes packed;
    for (std::size_t at = 0; at < raw.size(); at += 8192) {
        const std::size_t count = std::min<std::size_t>(8192, raw.size() - at);
        Bytes chunk;
        if (count <= 238) chunk.push_back(static_cast<unsigned char>(17 + count));
        else {
            chunk.push_back(0);
            std::size_t extra = count - 18;
            while (extra > 255) { chunk.push_back(0); extra -= 255; }
            chunk.push_back(static_cast<unsigned char>(extra));
        }
        chunk.insert(chunk.end(), raw.begin() + at, raw.begin() + at + count);
        chunk.insert(chunk.end(), {17, 0, 0});
        append16(packed, static_cast<unsigned int>(chunk.size()));
        append16(packed, static_cast<unsigned int>(count));
        packed.insert(packed.end(), chunk.begin(), chunk.end());
    }
    return packed;
}

std::string base64(const Bytes& bytes) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned int bits = (static_cast<unsigned int>(bytes[i]) << 16)
            | (i + 1 < bytes.size() ? static_cast<unsigned int>(bytes[i + 1]) << 8 : 0)
            | (i + 2 < bytes.size() ? bytes[i + 2] : 0);
        out += alphabet[(bits >> 18) & 63];
        out += alphabet[(bits >> 12) & 63];
        out += i + 1 < bytes.size() ? alphabet[(bits >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? alphabet[bits & 63] : '=';
    }
    return out;
}
} // namespace RuntimeMassMovementBenchmarkDetail

// Call once on a fresh startup state after rules/art/theater have loaded, before
// the ordinary navigation/economy/skirmish initialization. This function itself
// initializes navigation and economy. It preserves loaded assets and UI handles.
// All per-unit simulation systems remain enabled; only the source map's scripts,
// starting army, opponents and random crate spawns are excluded from the fixture.
bool prepareRuntimeMassMovementWorld(RuntimeWindowState& state, int unitCount,
    std::vector<int>& unitIds, std::string& error, bool choke = false, bool mixed = false,
    bool battle = false, int mapWidth = 96, int mapHeight = 64, bool playerScene = false,
    bool mixedRoster = false, bool vehicleOnly = false, int vehicleRosterOffset = 0) {
    const auto fail = [&](const std::string& why) {
        error = "mass movement fixture: " + why;
        return false;
    };
    if (unitCount != 48 && unitCount != 128 && unitCount != 256)
        return fail("unit count must be 48, 128 or 256");
    if (mapWidth < 96 || mapWidth > 480 || mapHeight < 64 || mapHeight > 480)
        return fail("fixture terrain dimensions are outside the bounded range");
    if (playerScene && (battle || choke || !mixed))
        return fail("player scene requires an open mixed movement fixture");
    if (playerScene && (mapWidth > 256 || mapHeight > 256))
        return fail("player diamond must fit the native 512-cell coordinate bounds");
    if (mixedRoster && !playerScene)
        return fail("the broad native roster requires a player scene");
    // Opt-in contract/benchmark authoring; every default fixture is unchanged.
    if (vehicleOnly && (!playerScene || !mixed || mixedRoster || battle || choke
            || !state.rules.findUnitType("UnitType", "HTNK")))
        return fail("vehicle roster requires native open player scene and stock HTNK" );
    constexpr std::array<const char*, 6> vehicleRoster{
        "MTNK", "SREF", "TTNK", "FV", "SCHP", "HTNK"};
    constexpr std::array<const char*, 6> infantryRoster{
        "E1", "CLEG", "JUMPJET", "SNIPE", "GHOST", "E2"};
    // Contract-only rotated stock decoding; default0 emits the original INI.
    if (vehicleRosterOffset < 0 || vehicleRosterOffset > 5
        || (vehicleRosterOffset != 0 && (!mixedRoster || vehicleOnly)))
        return fail("vehicle roster offset requires a mixed stock decoder and range0..5" );
    if (mixedRoster) {
        for (const char* type : vehicleRoster)
            if (!state.rules.findUnitType("UnitType", type)) return fail("missing stock vehicle roster type");
        for (const char* type : infantryRoster)
            if (!state.rules.findUnitType("InfantryType", type)) return fail("missing stock infantry roster type");
    }
    if (!state.rulesLoaded || state.rules.findUnitType("UnitType", "MTNK") == nullptr)
        return fail("loaded MTNK rules are required");
    if (mixed && state.rules.findUnitType("InfantryType", "E1") == nullptr)
        return fail("loaded E1 rules are required for the mixed formation");
    if (battle && (!mixed || choke
            || state.rules.findBuildingType("GACNST") == nullptr
            || state.rules.findBuildingType("GAPOWR") == nullptr
            || state.rules.findBuildingType("GAPILL") == nullptr))
        return fail("battle requires mixed units and stock GACNST/GAPOWR/GAPILL rules");
    if (state.tick != 0 || !state.behaviors.empty())
        return fail("requires a fresh startup before simulation and orders");

    int clearTile = 0;
    if (state.theaterLoaded) {
        const int clearSet = state.theater.getGeneralTileSetIndex("ClearTile");
        const auto& sets = state.theater.getTileSets();
        const auto found = std::find_if(sets.begin(), sets.end(), [&](const auto& set) {
            return set.index == clearSet && set.tilesInSet > 0;
        });
        if (found == sets.end()) return fail("theater has no ClearTile set");
        clearTile = found->startTileIndex;
    }
    const std::string theater = state.report.map.theater.empty()
        ? "TEMPERATE" : state.report.map.theater;
    const int playerOffset = playerScene ? mapWidth / 2 : 0;
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << "[Basic]\nName=MassMovement-v1\nMultiplayerOnly=no\n"
        "[Header]\nStartX=0\nStartY=0\nWidth=" << mapWidth << "\nHeight=" << mapHeight
        << "\nNumberStartingPoints=0\n[Map]\nTheater=" << theater
        << "\nSize=0,0," << mapWidth << ',' << mapHeight << "\n"
        "[SpecialFlags]\nTiberiumGrows=no\nTiberiumSpreads=no\n"
        "[Units]\n";
    const int tankCount = mixed && !vehicleOnly ? unitCount / 2 : unitCount;
    for (int index = 0; index < tankCount; ++index) {
        const int side = battle ? index / (unitCount / 4) : 0;
        const int slot = battle ? index % (unitCount / 4) : index;
        text << index << '=' << (side ? "Russians" : "Americans") << ','
            << (vehicleOnly ? (index % 2 ? "HTNK" : "MTNK") : mixedRoster ? vehicleRoster[(index + vehicleRosterOffset) % vehicleRoster.size()] : "MTNK") << ",256,"
            << (battle ? (side ? 27 : 10) + slot % 8 : playerOffset + 4 + index % 16) << ','
            << (battle ? 24 + slot / 8 : playerOffset + 20 + index / 16)
            << ",0,Guard,None,0,-1,0,-1,1,1\n";
    }
    text << "[Infantry]\n";
    for (int index = tankCount; index < unitCount; ++index) {
        const int side = battle ? (index - tankCount) / (unitCount / 4) : 0;
        const int slot = battle ? (index - tankCount) % (unitCount / 4) + unitCount / 4 : index;
        text << index - tankCount << '=' << (side ? "Russians" : "Americans") << ','
            << (mixedRoster ? infantryRoster[(index - tankCount) % infantryRoster.size()] : "E1") << ",256,"
            << (battle ? (side ? 27 : 10) + slot % 8 : playerOffset + 4 + index % 16) << ','
            << (battle ? 24 + slot / 8 : playerOffset + 20 + index / 16)
            << ",0,Guard,0,None,0,-1,0,1,1\n";
    }
    if (playerScene) {
        text << "[Structures]\n";
        constexpr std::array<const char*, 9> types{
            "GACNST", "GAPOWR", "GAPOWR", "GAREFN", "GAPILE",
            "GAWEAP", "AMRADR", "GATECH", "GADEPT"};
        for (int index = 0; index < static_cast<int>(types.size()); ++index) {
            const auto* building = state.rules.findBuildingType(types[index]);
            if (!building) return fail("player scene is missing a stock building type");
            text << index << "=Americans," << types[index] << ",256,"
                << playerOffset + 32 + index % 3 * 7 << ','
                << playerOffset + 18 + index / 3 * 7 << ",0,None\n";
        }
        for (const char* type : types)
            text << '[' << type << "]\nStrength="
                << state.rules.findBuildingType(type)->strength << '\n';
        text << "[MTNK]\nStrength=" << state.rules.findUnitType("UnitType", "MTNK")->strength
            << "\n[E1]\nStrength=" << state.rules.findUnitType("InfantryType", "E1")->strength << '\n';
        if (vehicleOnly)
            text << "[HTNK]\nStrength=" << state.rules.findUnitType("UnitType", "HTNK")->strength << '\n';
        if (mixedRoster) {
            for (const char* type : vehicleRoster)
                text << '[' << type << "]\nStrength=" << state.rules.findUnitType("UnitType", type)->strength << '\n';
            for (const char* type : infantryRoster)
                text << '[' << type << "]\nStrength=" << state.rules.findUnitType("InfantryType", type)->strength << '\n';
        }
    }
    if (battle) {
        text << "[Structures]\n";
        constexpr std::array<const char*, 4> types{"GACNST", "GAPOWR", "GAPILL", "GAPOWR"};
        for (int side = 0; side < 2; ++side) for (int index = 0; index < 4; ++index)
            text << side * 4 + index << '=' << (side ? "Russians" : "Americans")
                << ',' << types[index] << ",256," << (side ? 36 : 6) << ','
                << 20 + index * 7 << ",0,None\n";
        // Map hydration consumes Strength from the map/rules INI before
        // GameWorld receives the runtime database. Supply the actual loaded
        // values so authored 256/256 HP becomes full normal type strength.
        for (const char* type : {"GACNST", "GAPOWR", "GAPILL"})
            text << '[' << type << "]\nStrength="
                << state.rules.findBuildingType(type)->strength << '\n';
        text << "[MTNK]\nStrength=" << state.rules.findUnitType("UnitType", "MTNK")->strength
            << "\n[E1]\nStrength=" << state.rules.findUnitType("InfantryType", "E1")->strength << '\n';
    }
    RuntimeMassMovementBenchmarkDetail::Bytes tiles;
    const int tileWidth = playerScene ? mapWidth + mapHeight : mapWidth;
    const int tileHeight = playerScene ? mapWidth + mapHeight : mapHeight;
    for (int y = 1; y <= tileHeight; ++y) for (int x = 1; x <= tileWidth; ++x) {
        if (playerScene) {
            const int u = x - y + mapWidth, v = x + y - mapWidth;
            if (u < 0 || u >= 2 * mapWidth || v < 1 || v >= 2 * mapHeight) continue;
        }
        // Missing cells form a fixed wall. The only crossing is four cells
        // wide, and both starting formation and destination have ample space.
        if (choke && x == 48 && (y < 30 || y > 33)) continue;
        RuntimeMassMovementBenchmarkDetail::append16(tiles, x);
        RuntimeMassMovementBenchmarkDetail::append16(tiles, y);
        RuntimeMassMovementBenchmarkDetail::append32(tiles, clearTile);
        tiles.insert(tiles.end(), {0, 0, 0});
    }
    RuntimeMassMovementBenchmarkDetail::append32(tiles, 0);
    const std::string packed = RuntimeMassMovementBenchmarkDetail::base64(
        RuntimeMassMovementBenchmarkDetail::packTiles(tiles));
    text << "[IsoMapPack5]\n";
    for (std::size_t at = 0; at < packed.size(); at += 70)
        text << at / 70 + 1 << '=' << packed.substr(at, 70) << '\n';
    IniFile ini;
    MapDatabase map;
    GameWorld world;
    if (!ini.loadFromString(text.str()) || !map.loadFromIni(ini)
        || !world.loadFromMap(map, &state.rules)
        || world.getObjects().size() != static_cast<std::size_t>(unitCount + (battle ? 8 : playerScene ? 9 : 0))
        || world.getTerrainCells().size() != static_cast<std::size_t>(
            (playerScene ? 2 * mapWidth * mapHeight - mapWidth : mapWidth * mapHeight)
            - (choke ? mapHeight - 4 : 0))) {
        return fail("generated map did not hydrate its exact population and terrain");
    }

    state.config.defaultMapPath.clear();
    state.config.gameMode = "Standard";
    state.config.playerCountry = "Americans";
    state.config.playerSlots.clear();
    state.config.computerAiEnabled = false;
    state.config.multiplayerActive = false;
    state.config.crates = false;
    state.config.shortGame = false;
    state.config.noCooldown = false;
    state.config.simulationRandomSeed = 0x4d4153534d4f5645ULL;
    state.config.fixedTickHz = 30;
    state.config.startingUnits = 0;
    state.campaignMode = false;
    state.triggers = {};
    state.triggers.bootstrapComplete = true;
    state.computerAiByOwner.clear();
    state.computerMiningByOwner.clear();
    state.visionEnabled = battle || playerScene;
    state.runtimeSoundsEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.runtimePreferencePersistenceEnabled = false;
    state.world = std::move(world);
    state.report.map.loaded = true;
    state.report.map.name = battle ? "MassBattle-v1" : choke ? "MassMovement-v1-choke" : "MassMovement-v1-open";
    state.report.map.path = "MassMovement-v1.map";
    state.report.map.theater = theater;
    // Ordinary fixtures use a raw cell rectangle and derive their envelope
    // from decoded terrain. The player scene uses the native DXY diamond.
    state.report.map.size.clear();
    state.report.map.localSize.clear();
    if (playerScene) {
        state.report.map.size = "0,0," + std::to_string(mapWidth) + "," + std::to_string(mapHeight);
        state.report.map.localSize = state.report.map.size;
    }
    state.report.map.width = mapWidth;
    state.report.map.height = mapHeight;
    state.report.map.isoTiles = static_cast<int>(state.world.getTerrainCells().size());
    state.report.map.overlayCells = state.report.map.structures = state.report.map.triggers = 0;
    if (battle) state.report.map.structures = 8;
    if (playerScene) state.report.map.structures = 9;
    state.report.map.units = tankCount;
    state.report.map.infantry = unitCount - tankCount;
    state.report.world = state.world.getSummary();
    state.mapFacts = map.buildMapFacts(state.theaterLoaded ? &state.theater : nullptr,
        runtimeTileRootPath(state.config, state.report.map));
    state.mapFactsLoaded = !state.mapFacts.cells.empty();
    state.initialOreFieldsSeeded = true;
    state.cloakStateInitialized = false;
    state.bridgeSpecialFlagsLoaded = true;
    initializeRuntimeNavigation(state);
    // Explicitly identify the generated ClearTile surface. Missing wall cells
    // stay absent; the real pathfinder must route through the opening itself.
    for (auto& [cell, terrain] : state.terrainInfoByCell) {
        (void)cell;
        terrain = {};
        terrain.kind = RuntimeTerrainKind::Land;
        terrain.authoredLandType = terrain.movementLandType = RuntimeTerrainLandType::Clear;
    }
    state.pathTerrainCache.valid = state.pathTerrainCache.dynamicValid = false;
    initializeRuntimeEconomy(state);
    state.economy.skirmishStartApplied = true;
    state.economy.skirmishStatus = "mass movement fixture";
    state.skirmishCountryByOwner.clear();
    state.skirmishTeamByOwner.clear();
    state.skirmishDifficultyByOwner.clear();
    state.skirmishMatchFinished = false;
    state.selectedObjectIds.clear();
    unitIds.clear();
    for (auto& object : state.world.accessObjects()) {
        object.harvestAutoSearchSuspended = true;
        if ((!battle && !playerScene) || object.kind == WorldObjectKind::Unit || object.kind == WorldObjectKind::Infantry)
            unitIds.push_back(object.instanceId);
    }
    std::sort(unitIds.begin(), unitIds.end());
    state.camera = {};
    invalidateRuntimeObjectProjectionCache(state);
    (void)completeRuntimeWorldRefresh(state);
    if (battle || playerScene) (void)updateRuntimeVisionState(state);
    if (mixedRoster) {
        // Native chrono infantry may only move to explored destinations. Set
        // up the two previously scouted destination areas through real vision,
        // retaining shroud and ordinary exploration across the rest of the map.
        for (const RuntimeCellPoint goal : {RuntimeCellPoint{playerOffset + 22, playerOffset + 48},
                RuntimeCellPoint{playerOffset + 76, playerOffset + 32}})
            (void)state.vision.exploreArea(state.economy.owner, goal.x, goal.y, 8);
    }
    error.clear();
    return true;
}

bool issueRuntimeMassMovementOrder(RuntimeWindowState& state,
    const std::vector<int>& unitIds, RuntimeCellPoint target,
    std::map<int, RuntimeCellPoint>& destinations, std::string& error,
    bool allowNativeChronoRecovery = false) {
    std::unordered_set<int> recoveringChronoIds;
    if (allowNativeChronoRecovery) {
        for (int id : unitIds) {
            const auto* object = findObjectById(state, id);
            const auto* type = object ? findRuntimeUnitType(state.rules, *object) : nullptr;
            if (type && type->teleporter && state.specialMechanisms.isChronoMovementLocked(id, state.tick))
                recoveringChronoIds.insert(id);
        }
    }
    applySelection(state, unitIds, false, false);
    destinations.clear();
    RuntimeMassMovementRejectSink rejectDiagnostics;
    {
        RuntimeMassMovementRejectScope rejectionScope(&state, state.tick,
            &RuntimeMassMovementRejectSink::receive, &rejectDiagnostics);
        issueRuntimeMoveCommand(state, target, &destinations);
    }
    bool exact = state.lastCommand.acceptedCount == static_cast<int>(destinations.size())
        && state.lastCommand.rejectedCount == static_cast<int>(unitIds.size()) - static_cast<int>(destinations.size());
    for (int id : unitIds)
        exact = exact && (destinations.contains(id) || recoveringChronoIds.contains(id));
    for (const auto& [id, destination] : destinations) {
        (void)destination;
        exact = exact && std::find(unitIds.begin(), unitIds.end(), id) != unitIds.end();
    }
    if (!exact || destinations.size() > unitIds.size()) {
        std::cout << "Runtime mass movement rejection summary: tick=" << state.tick
            << " selected=" << unitIds.size() << " accepted=" << state.lastCommand.acceptedCount
            << " rejected=" << state.lastCommand.rejectedCount << " destinations=" << destinations.size()
            << " preexistingChronoRecovery=" << recoveringChronoIds.size()
            << " captured=" << rejectDiagnostics.count << " dropped=" << rejectDiagnostics.dropped << '\n';
        rejectDiagnostics.print(std::cout);
        std::size_t missing = 0;
        for (int id : unitIds) {
            if (destinations.contains(id)) continue;
            ++missing;
            if (missing > RuntimeMassMovementRejectSink::capacity) continue;
            const auto* actor = findObjectById(state, id);
            std::cout << "Runtime mass movement missing destination: tick=" << state.tick << " id=" << id
                << " preexistingChronoRecovery=" << recoveringChronoIds.contains(id)
                << " objectExists=" << (actor != nullptr)
                << " type=" << std::quoted(actor ? actor->type : std::string{})
                << " current=" << (actor ? actor->x : 0) << ',' << (actor ? actor->y : 0) << '\n';
        }
        std::cout << "Runtime mass movement missing destination count: total=" << missing
            << " printed=" << std::min(missing, RuntimeMassMovementRejectSink::capacity) << '\n';
        error = "mass movement order accepted " + std::to_string(state.lastCommand.acceptedCount)
            + "/" + std::to_string(unitIds.size());
        return false;
    }
    error.clear();
    return true;
}

// Add after prepareRuntimeMassMovementWorld and before warmup. The distant,
// unarmed target keeps the normal approach, weapon and projectile simulation
// active. Only this private fixture's target type has increased Strength;
// normal damage, armor, weapon, movement and visibility rules are retained.
bool prepareRuntimeMassCombatTarget(RuntimeWindowState& state, int& targetId,
    std::string& error, int* originalStrength = nullptr) {
    targetId = -1;
    // state.rules is an owned, mutable copy. Match the durable-target setup
    // used by RuntimeGattlingSmoke: damage clamps instance HP to type Strength.
    auto* type = const_cast<BuildingType*>(state.rules.findBuildingType("GACNST"));
    if (!state.rulesLoaded || type == nullptr) {
        error = "mass combat fixture requires loaded GACNST rules";
        return false;
    }
    if (originalStrength) *originalStrength = type->strength;
    type->strength = 1000000;
    type->uiProfile.strength = type->strength;
    WorldObjectInstance typeProbe;
    typeProbe.kind = WorldObjectKind::Structure;
    typeProbe.type = "GACNST";
    state.capabilityCache.profilesByType.erase(runtimeCapabilityCacheKeyForObject(typeProbe));
    WorldObjectInstance* target = addRuntimeObjectInstanceForOwner(state, "Russians",
        RuntimeBuildKind::Building, "GACNST", RuntimeCellPoint{28, 32});
    if (target == nullptr) {
        error = "mass combat fixture could not create target";
        return false;
    }
    targetId = target->instanceId;
    const int maximumHealth = runtimeMaxHealthForObject(state, *target);
    if (maximumHealth != type->strength) {
        error = "mass combat fixture target maximum HP did not use overridden Strength";
        return false;
    }
    target->health = maximumHealth;
    target->mission = "Guard";
    // The fixture starts with an already constructed building, as a map does.
    target->buildAnimTicksTotal = target->buildAnimTicksRemaining = 0;
    clearRuntimeRenderPosition(*target);
    state.cloakObservedHealthByObject[targetId] = target->health;
    ++state.report.map.structures;
    state.pathTerrainCache.dynamicValid = false;
    invalidateRuntimeObjectProjectionCache(state);
    (void)completeRuntimeWorldRefresh(state);
    // The live shell enables vision after fixture preparation. Give both
    // runners the same ordinary reconnaissance at the target, so a legal
    // attack remains visible while the formation approaches from far away.
    // Exploration persists through later updates; normal fog/stealth rules
    // and every per-unit simulation system continue to run.
    state.visionEnabled = true;
    (void)updateRuntimeVisionState(state);
    (void)state.vision.revealArea(state.economy.owner, 28, 32, 6);
    (void)updateRuntimeVisionState(state);
    target = findMutableObjectById(state, targetId);
    if (target == nullptr || !runtimeObjectVisibleToOwner(state, state.economy.owner, *target)) {
        error = "mass combat reconnaissance did not reveal target";
        return false;
    }
    error.clear();
    return true;
}

bool issueRuntimeMassCombatOrder(RuntimeWindowState& state,
    const std::vector<int>& unitIds, int targetId, std::string& error) {
    const WorldObjectInstance* target = findObjectById(state, targetId);
    if (target == nullptr || !target->alive) {
        error = "mass combat target is missing or dead";
        return false;
    }
    applySelection(state, unitIds, false, false);
    issueRuntimeAttackCommand(state, *target);
    if (state.lastCommand.acceptedCount != static_cast<int>(unitIds.size())) {
        error = "mass combat order accepted " + std::to_string(state.lastCommand.acceptedCount)
            + "/" + std::to_string(unitIds.size());
        return false;
    }
    error.clear();
    return true;
}

namespace RuntimeMassMovementBenchmarkDetail {
struct TimingSummary {
    double mean = 0.0;
    double p95 = 0.0;
    double maximum = 0.0;
};

TimingSummary summarize(std::vector<double> values) {
    TimingSummary result;
    if (values.empty()) return result;
    for (double value : values) result.mean += value;
    result.mean /= static_cast<double>(values.size());
    std::sort(values.begin(), values.end());
    result.p95 = values[static_cast<std::size_t>(std::ceil(values.size() * 0.95)) - 1];
    result.maximum = values.back();
    return result;
}
} // namespace RuntimeMassMovementBenchmarkDetail

#include "runtime/performance/diagnostics/RuntimeMassBattleBenchmark.inl"

// Independent entry: CONCORD_MASS_MOVEMENT_BENCHMARK=1 --command-smoke-test.
// Optional filters: UNITS=128|256|all, SCENARIO=open|retarget|choke|combat|all,
// TICKS=1..3600 (all names prefixed CONCORD_MASS_MOVEMENT_).
// MIXED=1 uses half MTNK and half E1; each starts in a different cell.
// Defaults deliberately run just the two open-ground populations, for a quick
// first diagnosis. "all" adds retargets, a four-cell opening and a durable target.
// GOLDEN_OUT and GOLDEN_IN contain deterministic fields only, never timings.
bool runRuntimeMassMovementBenchmark(const RuntimeConfig& config, std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "mass movement benchmark: " + why;
        return false;
    };
    int ticks = 360;
    if (const char* setting = std::getenv("CONCORD_MASS_MOVEMENT_TICKS")) {
        if (!parseRuntimeInt(setting, ticks) || ticks < 1 || ticks > 3600)
            return fail("TICKS must be between 1 and 3600");
    }
    const char* countSetting = std::getenv("CONCORD_MASS_MOVEMENT_UNITS");
    const std::string counts = countSetting ? countSetting : "all";
    if (counts != "all" && counts != "128" && counts != "256")
        return fail("UNITS must be 128, 256 or all");
    const char* scenarioSetting = std::getenv("CONCORD_MASS_MOVEMENT_SCENARIO");
    const std::string scenarioFilter = scenarioSetting ? scenarioSetting : "open";
    if (scenarioFilter != "all" && scenarioFilter != "open"
        && scenarioFilter != "retarget" && scenarioFilter != "choke" && scenarioFilter != "combat"
        && scenarioFilter != "battle")
        return fail("SCENARIO must be open, retarget, choke, combat, battle or all");
    if (std::getenv("CONCORD_MASS_MOVEMENT_TICKS") == nullptr
        && (scenarioFilter == "combat" || scenarioFilter == "all")) ticks = 1800;
    if (std::getenv("CONCORD_MASS_MOVEMENT_TICKS") == nullptr && scenarioFilter == "battle") ticks = 600;
    const char* mixedSetting = std::getenv("CONCORD_MASS_MOVEMENT_MIXED");
    const bool mixed = mixedSetting && parseRuntimeBool(mixedSetting);

    using GoldenKey = std::tuple<std::string, int, int>;
    using GoldenValue = std::array<std::uint64_t, 3>;
    std::map<GoldenKey, GoldenValue> expected;
    const char* goldenInput = std::getenv("CONCORD_MASS_MOVEMENT_GOLDEN_IN");
    const char* goldenOutput = std::getenv("CONCORD_MASS_MOVEMENT_GOLDEN_OUT");
    if (goldenInput && goldenOutput && std::string(goldenInput) == goldenOutput)
        return fail("GOLDEN_IN and GOLDEN_OUT must be different paths");
    if (goldenInput) {
        std::ifstream input(std::filesystem::u8path(goldenInput));
        if (!input) return fail("cannot open GOLDEN_IN");
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty() || line.front() == '#') continue;
            std::istringstream row(line);
            row.imbue(std::locale::classic());
            std::string scenario, trailing;
            int units = 0, steps = 0;
            GoldenValue hashes{};
            if (!(row >> scenario >> units >> steps >> hashes[0] >> hashes[1] >> hashes[2])
                || (row >> trailing)
                || !expected.emplace(GoldenKey{scenario, units, steps}, hashes).second)
                return fail("invalid or duplicate GOLDEN_IN row");
        }
    }
    std::ostringstream golden;
    golden.imbue(std::locale::classic());
    golden << "# MassMovement-v1: scenario units ticks initialHash trajectoryHash finalHash\n";

    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config;
    stock->report = buildRuntimeStartupReport(config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original resources did not load");
    stock->rulesLoaded = stock->report.rulesLoaded;
    stock->artLoaded = stock->report.artLoaded;
    stock->theaterLoaded = stock->report.theaterLoaded;
    std::cout << "Mass movement benchmark v1: real group orders + advanceRuntimeSimulation; "
        "fixedHz=30 grid=96x64 warmupTicks=8 samples=" << ticks
        << " hash/validation excluded from timings; move includes path; no FPS measurement\n" << std::flush;

    for (const std::string scenario : {"open", "retarget", "choke", "combat", "battle"}) {
        // Preserve the legacy "all" golden suite; battle is an explicit opt-in.
        if (scenario == "battle" && scenarioFilter == "all") continue;
        if (scenarioFilter != "all" && scenarioFilter != scenario) continue;
        const std::string label = std::string(mixed || scenario == "battle" ? "mixed-" : "tank-") + scenario;
        for (int unitCount : {128, 256}) {
            if (counts != "all" && counts != std::to_string(unitCount)) continue;
            auto storage = std::make_unique<RuntimeWindowState>();
            RuntimeWindowState& state = *storage;
            state.config = config;
            state.report = stock->report;
            state.rules = stock->rules;
            state.art = stock->art;
            state.theater = stock->theater;
            state.rulesLoaded = stock->rulesLoaded;
            state.artLoaded = stock->artLoaded;
            state.theaterLoaded = stock->theaterLoaded;
            RuntimeSnapshotActiveStateScope activeState(state);
            if (scenario == "battle") {
                GoldenValue hashes{};
                if (!runRuntimeMassBattleSample(state, unitCount, ticks, hashes, error)) return false;
                const GoldenKey key{label, unitCount, ticks};
                if (goldenInput) {
                    const auto reference = expected.find(key);
                    if (reference == expected.end() || reference->second != hashes)
                        return fail("golden mismatch for " + label + "/" + std::to_string(unitCount));
                }
                golden << label << ' ' << unitCount << ' ' << ticks << ' '
                    << hashes[0] << ' ' << hashes[1] << ' ' << hashes[2] << '\n';
                continue;
            }
            std::vector<int> unitIds;
            if (!prepareRuntimeMassMovementWorld(state, unitCount, unitIds, error, scenario == "choke", mixed))
                return false;
            const bool combat = scenario == "combat";
            int targetId = -1;
            int targetOriginalStrength = 0;
            if (combat && !prepareRuntimeMassCombatTarget(state, targetId, error,
                    &targetOriginalStrength)) return false;
            for (int warmup = 0; warmup < 8; ++warmup) {
                ++state.tick;
                (void)advanceRuntimeSimulation(state);
            }
            std::string hashError;
            const std::uint64_t initialHash = runtimeStandaloneSimulationStateHash(state, hashError);
            if (initialHash == 0) return fail("initial hash: " + hashError);
            std::uint64_t trajectoryHash = runtimeStableHashString("MassMovementTrajectory/v1");
            runtimeMultiplayerAuthoritativeMixUnsigned(trajectoryHash, initialHash);
            std::uint64_t finalHash = initialHash;
            std::array<std::vector<double>, 10> timings;
            for (auto& values : timings) values.reserve(ticks);
            std::vector<double> commandTimings;
            std::vector<double> hashTimings;
            hashTimings.reserve(ticks);
            std::map<int, RuntimeCellPoint> destinations;
            std::map<int, RuntimeCellPoint> starts;
            std::unordered_set<int> everMoved;
            std::unordered_set<int> everCompleted;
            std::unordered_set<int> shooters;
            for (int id : unitIds)
                starts.emplace(id, runtimeCellPointForObject(*findObjectById(state, id)));
            long long activeTotal = 0, animatedTotal = 0, completedEvents = 0;
            long long pathRequests = 0, repaths = 0;
            long long attackEvents = 0, projectilesTotal = 0, impactsTotal = 0;
            std::size_t projectilesPeak = 0, impactsPeak = 0;
            const int targetInitialHealth = combat ? findObjectById(state, targetId)->health : 0;
            const int targetMaximumHealth = combat
                ? runtimeMaxHealthForObject(state, *findObjectById(state, targetId)) : 0;
            int targetFinalHealth = targetInitialHealth;
            int lastAttackTick = -1;
            int lastPumpExpandedNodesPeak = 0;
            int activePeak = 0, animatedPeak = 0, completeFinal = 0, completePeak = 0;
            int maximumDuplicateCells = 0;
            std::cout << "Mass movement begin: scenario=" << label << " units=" << unitCount
                << " initialHash=" << initialHash << '\n' << std::flush;
            for (int step = 0; step < ticks; ++step) {
                ++state.tick;
                const auto tickStart = runtimeSmokeNow();
                if (step == 0 || (scenario == "retarget" && step % 90 == 0)) {
                    constexpr std::array<RuntimeCellPoint, 3> targets{{{76, 32}, {76, 48}, {76, 16}}};
                    const RuntimeCellPoint target = targets[scenario == "retarget" ? (step / 90) % 3 : 0];
                    const auto commandStart = runtimeSmokeNow();
                    const bool accepted = combat
                        ? issueRuntimeMassCombatOrder(state, unitIds, targetId, error)
                        : issueRuntimeMassMovementOrder(state, unitIds, target, destinations, error);
                    if (!accepted)
                        return false;
                    commandTimings.push_back(runtimeSmokeElapsedMs(commandStart));
                }
                const auto simulationStart = runtimeSmokeNow();
                (void)advanceRuntimeSimulation(state);
                const double simulationMs = runtimeSmokeElapsedMs(simulationStart);
                const double tickMs = runtimeSmokeElapsedMs(tickStart);
                const std::array<double, 10> sample{tickMs, simulationMs,
                    state.perf.lastSimEffectsMs, state.perf.lastSimEconomyMs,
                    state.perf.lastSimTriggersMs, state.perf.lastSimBuildMs,
                    state.perf.lastSimGuardMs, state.perf.lastSimMoveMs,
                    state.perf.lastSimAttackMs, state.perf.lastSimPathMs};
                for (std::size_t i = 0; i < sample.size(); ++i) timings[i].push_back(sample[i]);
                int active = 0, animated = 0, complete = 0, duplicates = 0;
                std::unordered_set<int> occupied;
                for (const auto& object : state.world.getObjects()) {
                    if (object.instanceId == targetId) continue;
                    if (!object.alive || !starts.contains(object.instanceId))
                        return fail("fixture population changed during simulation");
                    if (!state.terrainByCell.contains(runtimeCellKey(object.x, object.y)))
                        return fail("mover left generated passable terrain");
                    if (!occupied.insert(runtimeCellKey(object.x, object.y)).second) ++duplicates;
                    if (runtimeCellPointForObject(object) != starts.at(object.instanceId))
                        everMoved.insert(object.instanceId);
                    const auto behavior = state.behaviors.find(object.instanceId);
                    const bool moving = behavior != state.behaviors.end()
                        && behavior->second.state != RuntimeBehaviorState::Complete
                        && (behavior->second.kind == RuntimeBehaviorKind::Move
                            || (behavior->second.kind == RuntimeBehaviorKind::Attack
                                && (behavior->second.state == RuntimeBehaviorState::Chasing
                                    || behavior->second.pathRequestPending
                                    || !behavior->second.path.empty())));
                    active += moving ? 1 : 0;
                    animated += object.renderMoveActive ? 1 : 0;
                    const auto destination = destinations.find(object.instanceId);
                    if (!moving && !object.renderMoveActive && destination != destinations.end()
                        && runtimeCellPointForObject(object) == destination->second) {
                        ++complete;
                        everCompleted.insert(object.instanceId);
                    }
                }
                if (state.world.getObjects().size() != static_cast<std::size_t>(unitCount + (combat ? 1 : 0)))
                    return fail("fixture population count changed during simulation");
                if (combat) {
                    const auto* target = findObjectById(state, targetId);
                    if (target == nullptr || !target->alive || target->health <= 0)
                        return fail("durable combat target did not survive sample window");
                    targetFinalHealth = target->health;
                }
                if (state.actionSummary.attacksThisTick > 0) lastAttackTick = state.tick;
                attackEvents += state.actionSummary.attacksThisTick;
                projectilesTotal += static_cast<long long>(state.projectiles.size());
                impactsTotal += static_cast<long long>(state.impactEffects.size());
                projectilesPeak = std::max(projectilesPeak, state.projectiles.size());
                impactsPeak = std::max(impactsPeak, state.impactEffects.size());
                const auto observeShooter = [&](int attackerId) {
                    if (starts.contains(attackerId)) shooters.insert(attackerId);
                };
                for (const auto& projectile : state.projectiles)
                    observeShooter(projectile.attackerObjectId);
                for (const auto& projectile : state.recentExpiredProjectiles)
                    observeShooter(projectile.attackerObjectId);
                for (const auto& impact : state.impactEffects)
                    observeShooter(impact.attackerObjectId);
                activeTotal += active;
                animatedTotal += animated;
                activePeak = std::max(activePeak, active);
                animatedPeak = std::max(animatedPeak, animated);
                completeFinal = complete;
                completePeak = std::max(completePeak, complete);
                maximumDuplicateCells = std::max(maximumDuplicateCells, duplicates);
                completedEvents += state.actionSummary.completedCommands;
                pathRequests += state.perf.lastSimPathRequests;
                repaths += state.perf.lastSimRepathCount;
                lastPumpExpandedNodesPeak = std::max(lastPumpExpandedNodesPeak,
                    state.lastPathQueueStats.lastPumpExpandedNodes);
                // A full authoritative replay hash at every fixed tick makes
                // the golden sensitive to paths, budgets, cursors and timers,
                // not just final positions. Observing it cannot select a path.
                const auto hashStart = runtimeSmokeNow();
                finalHash = runtimeStandaloneSimulationStateHash(state, hashError);
                hashTimings.push_back(runtimeSmokeElapsedMs(hashStart));
                if (finalHash == 0) return fail("tick hash: " + hashError);
                runtimeMultiplayerAuthoritativeMixUnsigned(trajectoryHash, finalHash);
                if ((step + 1) % 60 == 0 || step + 1 == ticks) {
                    std::cout << "Mass movement progress: scenario=" << label << " units=" << unitCount
                        << " sample=" << step + 1 << " active=" << active << " animated=" << animated
                        << " completed=" << complete << " everMoved=" << everMoved.size()
                        << " attacks=" << attackEvents << " shooters=" << shooters.size()
                        << " projectiles=" << state.projectiles.size() << " impacts=" << state.impactEffects.size()
                        << " stateHash=" << finalHash << '\n' << std::flush;
                }
            }
            constexpr std::array<const char*, 10> names{"tick-with-command", "simulation", "effects",
                "economy", "triggers", "build", "guard", "move-including-path", "attack", "path-subset"};
            const auto printTiming = [&](const char* name, const std::vector<double>& values) {
                const auto summary = RuntimeMassMovementBenchmarkDetail::summarize(values);
                std::cout << "Mass movement timing: scenario=" << label << " units=" << unitCount
                    << " stage=" << name << " samples=" << values.size()
                    << " meanMs=" << summary.mean << " p95Ms=" << summary.p95
                    << " maxMs=" << summary.maximum << '\n';
            };
            for (std::size_t i = 0; i < names.size(); ++i) printTiming(names[i], timings[i]);
            printTiming("command-only", commandTimings);
            printTiming("hash-observer-excluded", hashTimings);
            std::cout << "Mass movement result: scenario=" << label << " units=" << unitCount
                << " ticks=" << ticks << " orders=" << commandTimings.size()
                << " activeMean=" << static_cast<double>(activeTotal) / ticks << " activePeak=" << activePeak
                << " animatedMean=" << static_cast<double>(animatedTotal) / ticks << " animatedPeak=" << animatedPeak
                << " everMoved=" << everMoved.size() << " completedFinal=" << completeFinal
                << " completedPeak=" << completePeak << " everCompleted=" << everCompleted.size()
                << " completedEvents=" << completedEvents << " sharedCellsMax=" << maximumDuplicateCells
                << " pathRequests=" << pathRequests << " repaths=" << repaths
                << " lastPumpExpandedNodesPeak=" << lastPumpExpandedNodesPeak
                << " attacks=" << attackEvents << " shooters=" << shooters.size()
                << " projectilesMean=" << static_cast<double>(projectilesTotal) / ticks
                << " projectilesPeak=" << projectilesPeak
                << " impactsMean=" << static_cast<double>(impactsTotal) / ticks
                << " impactsPeak=" << impactsPeak
                << " targetOriginalStrength=" << targetOriginalStrength
                << " targetMaxHealth=" << targetMaximumHealth
                << " targetInitialHealth=" << targetInitialHealth
                << " targetFinalHealth=" << targetFinalHealth
                << " targetAlive=" << (combat ? "yes" : "n/a")
                << " targetDamage=" << targetInitialHealth - targetFinalHealth
                << " lastAttackTick=" << lastAttackTick
                << " initialHash=" << initialHash << " trajectoryHash=" << trajectoryHash
                << " finalHash=" << finalHash << '\n' << std::flush;
            if (ticks >= 60 && everMoved.empty()) return fail("no unit moved in the sample window");
            if (combat && (everMoved.size() < unitIds.size() * 3 / 4
                    || attackEvents == 0 || projectilesPeak == 0 || impactsPeak == 0
                    || targetFinalHealth <= 0 || targetFinalHealth >= targetInitialHealth
                    || lastAttackTick < state.tick - 180))
                return fail("combat requires movement, sustained fire, damage and a surviving target");
            const GoldenKey key{label, unitCount, ticks};
            const GoldenValue hashes{initialHash, trajectoryHash, finalHash};
            if (goldenInput) {
                const auto reference = expected.find(key);
                if (reference == expected.end() || reference->second != hashes)
                    return fail("golden mismatch for " + label + "/" + std::to_string(unitCount));
            }
            golden << label << ' ' << unitCount << ' ' << ticks << ' '
                << initialHash << ' ' << trajectoryHash << ' ' << finalHash << '\n';
        }
    }
    if (goldenOutput) {
        std::ofstream output(std::filesystem::u8path(goldenOutput));
        if (!output || !(output << golden.str())) return fail("cannot write GOLDEN_OUT");
    }
    error.clear();
    return true;
}
