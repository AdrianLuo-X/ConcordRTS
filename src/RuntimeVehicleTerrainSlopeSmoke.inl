// Issue 15: raw stock terrain and model inventory for genuine player moves.
// No expected angle is obtained from the renderer's terrain-pose resolver.
namespace RuntimeVehicleTerrainSlopeOracle {
using Bytes = std::vector<unsigned char>;

struct OriginalSubTile {
    int index = -1;
    int height = 0;
    int land = -1;
    int ramp = -1;
    int offsetX = 0;
    int offsetY = 0;
    bool present = false;
};
struct OriginalTile {
    int tile = -1;
    int width = 0;
    int height = 0;
    std::string name;
    std::string source;
    Bytes bytes;
    std::vector<OriginalSubTile> subtiles;
};
struct OriginalVehicle {
    std::string id;
    std::string image;
    std::string sprite;
    std::string classification;
    std::string locomotor;
    int strength = 0;
    bool voxel = false;
    bool tilter = true;
};

inline std::uint32_t little(const Bytes& bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) throw std::runtime_error("raw TMP integer outside resource");
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8)
        | (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}
inline bool readOriginal(const RuntimeWindowState& state, const std::string& name,
    const std::filesystem::path& output, Bytes& bytes, std::string& source, std::string& error) {
    std::string ext = state.theater.getTileExtension();
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    const std::string theater = ext == "des" ? "desert" : ext == "sno" ? "snow"
        : ext == "urb" ? "urban" : ext == "ubn" ? "urbann" : ext == "lun" ? "lunar" : "temperat";
    const std::string iso = ext == "tem" ? "isotemp.mix" : ext == "sno" ? "isosnow.mix" : "iso" + ext + ".mix";
    const auto assetRoot = std::filesystem::path(runtimeAssetRootPath(state.config));
    for (const char* directory : {"ra2md", "ra2"}) {
        for (const auto& archiveName : {"iso" + ext + "md.mix", iso, theater + "md.mix",
                theater + ".mix", ext + ".mix"}) {
            const auto path = assetRoot / directory / archiveName;
            if (!std::filesystem::is_regular_file(path)) continue;
            MixArchive archive;
            if (!archive.loadFromPath(path)) continue;
            const auto* entry = archive.findEntryByName(name);
            if (!entry) continue;
            if (!archive.readEntryData(*entry, bytes, error)) return false;
            source = pathString(path) + "!" + name;
            std::ofstream file(output / name, std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!file) { error = "cannot preserve original terrain bytes: " + name; return false; }
            return true;
        }
    }
    error = "original MIX terrain resource unavailable: " + name;
    return false;
}
inline bool readTile(const RuntimeWindowState& state, int tile,
    const std::filesystem::path& output, OriginalTile& result, std::string& error) {
    const auto reference = state.theater.resolveTileIndex(tile);
    result.tile = tile;
    result.name = toLowerAsciiLocal(reference.expectedFileName);
    if (!reference.validTileSet || result.name.empty()
        || !readOriginal(state, result.name, output, result.bytes, result.source, error)) return false;
    if (result.bytes.size() < 20) { error = "original TMP header truncated: " + result.name; return false; }
    result.width = static_cast<int>(little(result.bytes, 0));
    result.height = static_cast<int>(little(result.bytes, 4));
    if (result.width <= 0 || result.height <= 0 || result.width > 16 || result.height > 16
        || result.bytes.size() < 16 + static_cast<std::size_t>(result.width * result.height) * 4) {
        error = "original TMP grid invalid: " + result.name; return false;
    }
    result.subtiles.clear();
    for (int index = 0; index < result.width * result.height; ++index) {
        OriginalSubTile sub;
        sub.index = index;
        const auto at = little(result.bytes, 16 + static_cast<std::size_t>(index) * 4);
        if (at != 0) {
            if (at + 52 > result.bytes.size()) { error = "original TMP record truncated: " + result.name; return false; }
            sub.present = true;
            sub.offsetX = static_cast<std::int32_t>(little(result.bytes, at));
            sub.offsetY = static_cast<std::int32_t>(little(result.bytes, at + 4));
            sub.height = result.bytes[at + 40];
            sub.land = result.bytes[at + 41];
            sub.ramp = result.bytes[at + 42];
        }
        result.subtiles.push_back(sub);
    }
    return true;
}
inline std::vector<OriginalVehicle> inventory(const IniFile& raw, const IniFile& art) {
    std::vector<OriginalVehicle> result;
    std::set<std::string> seen;
    const auto* registered = raw.getSection("VehicleTypes");
    if (!registered) return result;
    for (const auto& [key, id] : *registered) {
        if (!seen.insert(id).second) continue;
        OriginalVehicle row;
        row.id = id;
        row.image = raw.getValue(id, "Image", id);
        row.sprite = art.getValue(row.image, "Image", row.image);
        row.locomotor = toLowerAsciiLocal(raw.getValue(id, "Locomotor", ""));
        row.strength = std::stoi(raw.getValue(id, "Strength", "0"));
        row.voxel = parseRuntimeBool(art.getValue(row.image, "Voxel", "no"));
        row.tilter = parseRuntimeBool(raw.getValue(id, "IsTilter", "yes"));
        const auto movement = toLowerAsciiLocal(raw.getValue(id, "MovementZone", ""));
        const auto speed = toLowerAsciiLocal(raw.getValue(id, "SpeedType", ""));
        if (!raw.getSection(id)) row.classification = "missing-section";
        else if (row.strength <= 0) row.classification = "no-native-strength";
        else if (parseRuntimeBool(raw.getValue(id, "JumpJet", "no"))
            || parseRuntimeBool(raw.getValue(id, "ConsideredAircraft", "no"))
            || row.locomotor == "{92612c46-f71f-11d1-ac9f-006008055bb5}"
            || movement.find("fly") != std::string::npos || speed.find("winged") != std::string::npos)
            row.classification = "air";
        else if (parseRuntimeBool(raw.getValue(id, "Naval", "no")))
            row.classification = id == "SAPC" || id == "LCRF" || id == "YHVR" ? "amphibious-on-land" : "naval";
        else row.classification = "ground";
        result.push_back(std::move(row));
    }
    return result;
}
inline std::array<double, 3> body(const WorldObjectInstance& object) {
    const bool render = worldObjectHasGroundRenderPose(object);
    return {render ? object.renderX : double(object.x), render ? object.renderY : double(object.y),
        render ? object.renderLevel : double(object.level)};
}
inline bool bodyBridge(const WorldObjectInstance& object) {
    return worldObjectHasGroundRenderPose(object) ? object.renderOnBridge : object.onBridge;
}
inline std::string describe(const WorldObjectInstance* object) {
    if (!object) return "missing";
    std::ostringstream out;
    out << std::setprecision(17) << object->type << '#' << object->instanceId << " HP=" << object->health
        << " alive=" << object->alive << " cell=" << object->x << ',' << object->y << ',' << object->level
        << ':' << object->onBridge << " body=" << object->renderX << ',' << object->renderY << ',' << object->renderLevel
        << ':' << object->renderOnBridge << " has=" << object->hasRenderPosition << " active=" << object->renderMoveActive
        << " facing=" << object->facing << " mission=" << object->mission;
    return out.str();
}

inline void facade(RuntimeWindowState& state) {
    state.economy.owner = state.config.playerCountry = "Americans";
    state.humanHouseAuthorities.clear();
    RuntimeHumanHouseAuthority authority;
    authority.owner = authority.country = "Americans";
    authority.canonicalOwner = "americans";
    authority.localFacade = true;
    state.humanHouseAuthorities.push_back(std::move(authority));
}
inline std::unique_ptr<RuntimeWindowState> blankFixture(RuntimeWindowState& live,
    const RulesDatabase& rules, const ArtDatabase& art, int hz, int land) {
    auto box = std::make_unique<RuntimeWindowState>();
    auto& state = *box;
    seedRuntimeLoadScratch(state, live, false);
    state.rules = rules; state.art = art;
    state.rulesLoaded = state.artLoaded = state.theaterLoaded = true;
    state.config.fixedTickHz = hz;
    state.config.gameMode = "Battle";
    state.config.playerSlots.clear();
    state.config.gameModeRulesOverride.clear();
    state.config.skirmishLoadSavePath.clear();
    state.config.computerAiEnabled = state.config.multiplayerActive = state.config.noCooldown = false;
    state.config.shortGame = state.config.crates = state.config.superweapons = false;
    state.campaignMode = false;
    state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = state.visionEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.tick = 200;
    state.world.accessObjects().clear();
    state.world.accessTerrainCells().clear();
    state.world.accessTerrainObjects().clear();
    state.world.accessSmudges().clear();
    state.terrainByCell.clear(); state.terrainInfoByCell.clear();
    state.mapFacts = {}; state.mapFactsLoaded = false;
    state.report.map.size.clear(); state.report.map.localSize.clear(); state.report.map.width = state.report.map.height = -1;
    state.behaviors.clear(); state.projectiles.clear(); state.recentExpiredProjectiles.clear(); state.impactEffects.clear();
    state.selectedObjectIds.clear(); state.playerCommands = {};
    state.deathWeapons.reset(); state.pendingObjectExits.clear(); state.pendingObjectExitIds.clear();
    state.specialMechanismsInitialized = state.superWeaponsInitialized = state.subcellsInitialized = false;
    state.particles.reset(hz);
    state.particlesInitialized = state.particles.alignEmptyStateTick(state.tick);
    for (int y = 54; y <= 82; ++y) for (int x = 54; x <= 82; ++x) {
        TerrainCell cell;
        cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        cell.tileIndex = land; cell.subTileIndex = 0;
        state.world.accessTerrainCells().push_back(cell);
    }
    facade(state);
    return box;
}
inline bool initializeFixture(RuntimeWindowState& state, std::string& error) {
    initializeRuntimeNavigation(state);
    ensureRuntimeSubcellsInitialized(state);
    initializeRuntimeEconomy(state);
    initializeRuntimeTriggers(state);
    state.economy.skirmishStartApplied = true;
    state.triggers.bootstrapComplete = true;
    // This isolated route has no opposing house. Keep ordinary match victory
    // bookkeeping from ending its genuine player Move after the first ten ticks.
    state.skirmishCountryByOwner.clear(); state.skirmishTeamByOwner.clear();
    state.skirmishMatchFinished = false; state.skirmishVictory = false; state.skirmishResult.clear();
    facade(state);
    state.particles.reset(state.config.fixedTickHz);
    state.particlesInitialized = state.particles.alignEmptyStateTick(state.tick);
    ensureRuntimeSpecialMechanismsInitialized(state);
    ensureRuntimeSuperWeaponsInitialized(state);
    ensureRuntimeLightingInitialized(state);
    initializeRuntimeOfflineGameplayRouting(state);
    if (!state.particlesInitialized) { error = "native particle clock initialization"; return false; }
    return true;
}
inline void step(RuntimeWindowState& state) {
    RuntimeSnapshotActiveStateScope active(state);
    ++state.tick;
    advanceRuntimeSimulation(state, false);
    completeRuntimeWorldRefresh(state);
}
inline std::unique_ptr<RuntimeWindowState> checkpoint(RuntimeWindowState& state,
    bool canonical, const std::filesystem::path& file, std::string& error) {
    std::ostringstream payload, normalized;
    IniFile ordinary, extension;
    if (!writeRuntimeStateToStream(state, payload, error) || !ordinary.loadFromString(payload.str())
        || ordinary.getValue("RuntimeSave", "Version", "") != "14") return nullptr;
    std::string exported = payload.str();
    if (canonical) {
        if (!writeRuntimeMultiplayerSnapshotExtension(state, payload, error)
            || !extension.loadFromString(payload.str())
            || !writeRuntimeMultiplayerSnapshotCanonicalIni(extension, normalized, error)
            || !extension.loadFromString(normalized.str())) return nullptr;
        exported = normalized.str();
    }
    std::ofstream output(file, std::ios::binary);
    output << exported;
    if (!output) { error = "native checkpoint output unavailable"; return nullptr; }
    auto copy = std::make_unique<RuntimeWindowState>();
    seedRuntimeLoadScratch(*copy, state, false);
    RuntimeSnapshotActiveStateScope active(*copy);
    if (!materializeRuntimeLoadStateFromIni(*copy, ordinary, error)
        || (canonical && !materializeRuntimeMultiplayerSnapshotExtension(*copy, extension, error))) return nullptr;
    copy->runtimeSoundsEnabled = copy->runtimePreferencePersistenceEnabled = copy->visionEnabled = false;
    facade(*copy);
    initializeRuntimeOfflineGameplayRouting(*copy);
    if (copy->tick != state.tick || copy->config.fixedTickHz != state.config.fixedTickHz
        || copy->config.noCooldown || copy->particles.fixedTickHz() != state.config.fixedTickHz
        || copy->particles.stateHash() != state.particles.stateHash()) {
        error = "strict restored fixed clock differs"; return nullptr;
    }
    return copy;
}
} // namespace RuntimeVehicleTerrainSlopeOracle

#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeVehicleTerrainSlopeSmoke(RuntimeWindowState& live, std::string& error) {
    using namespace RuntimeVehicleTerrainSlopeOracle;
    std::cout << std::unitbuf;
    const char* destination = std::getenv("CONCORD_VEHICLE_TERRAIN_SLOPE_OUTPUT");
    const std::filesystem::path root = destination && *destination ? destination : "build/issue22_15_vehicle_terrain_slope";
    const char* selected = std::getenv("CONCORD_VEHICLE_TERRAIN_SLOPE_CASE");
    const std::string filter = selected ? selected : "";
    if (filter == "render-only") {
        auto context = runtimeTerrainMetadataContext(live);
        context.rules = &live.rules; context.art = &live.art;
        context.animationTick = live.tick; context.fixedTickHz = live.config.fixedTickHz;
        return runRuntimeVehicleTerrainSlopeRenderContract(context, error);
    }
    std::filesystem::create_directories(root / "native-original");
    std::string label = "startup", stage;
    const auto fail = [&](const std::string& why) {
        error = "Vehicle terrain slope " + label + " [" + stage + "]: " + why; return false;
    };
    std::ofstream census(root / "inventory.csv"), terrainCsv(root / "original_terrain.csv"),
        movement(root / "movement.csv"), saves(root / "checkpoints.csv"), cases(root / "cases.csv");
    if (!census || !terrainCsv || !movement || !saves || !cases) return fail("output unavailable");
    terrainCsv << std::setprecision(17); movement << std::setprecision(17);
    census << "type,classification,strength,image,sprite,voxel,IsTilter,locomotor,typed\n";
    terrainCsv << "tile,name,source,grid_width,grid_height,subtile,present,height,land,ramp,offset_x,offset_y\n";
    movement << "case,stage,format,tick,hz,object,type,hp,logical_x,logical_y,logical_level,logical_bridge,render,active,body_x,body_y,body_level,body_bridge,facing,pitch,roll,height,terrain_pitch,terrain_roll,terrain_height,supported,expected_pitch,expected_roll,expected_height,expected_supported\n";
    saves << "case,stage,format,tick,hz,object,type,file\n";
    cases << "case,type,hz,raw_ramp,ticks,supported_samples,uphill,downhill,stopped,ordinary,canonical,returned_flat,result\n";
    IniFile raw, rawArt; RulesDatabase rules; ArtDatabase art;
    if (!raw.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(raw)
        || !rawArt.loadFromFile(live.report.artPath) || !art.loadFromIni(rawArt)) return fail("original rules/art");
    const auto ruleHash = rules.getDetailRulesHash();
    const auto originalVehicles = inventory(raw, rawArt);
    int ground = 0, amphibious = 0, landVoxels = 0, tiltVoxels = 0, noTilt = 0, naval = 0, air = 0, unavailable = 0;
    std::set<std::string> allIDs, noTiltIDs, absentIDs;
    for (const auto& row : originalVehicles) {
        const auto* typed = rules.findUnitType("vehicle", row.id);
        const bool land = row.classification == "ground" || row.classification == "amphibious-on-land";
        if (!allIDs.insert(row.id).second) return fail("duplicate independent raw inventory");
        if (row.classification == "ground") ++ground;
        else if (row.classification == "amphibious-on-land") ++amphibious;
        else if (row.classification == "naval") ++naval;
        else if (row.classification == "air") ++air;
        else { ++unavailable; absentIDs.insert(row.id); }
        if (land) {
            landVoxels += row.voxel; tiltVoxels += row.voxel && row.tilter;
            noTilt += !row.tilter; if (!row.tilter) noTiltIDs.insert(row.id);
            if (!typed || typed->strength != row.strength || typed->isTilter != row.tilter)
                return fail("raw/typed native land vehicle differs: " + row.id);
        }
        census << row.id << ',' << row.classification << ',' << row.strength << ',' << row.image << ',' << row.sprite
            << ',' << row.voxel << ',' << row.tilter << ',' << row.locomotor << ',' << (typed != nullptr) << '\n';
    }
    if (allIDs.size() != 80 || ground != 56 || amphibious != 3 || naval != 13 || air != 6 || unavailable != 2
        || landVoxels != 58 || tiltVoxels != 57 || noTilt != 2
        || noTiltIDs != std::set<std::string>{"CAOS", "DRON"}
        || absentIDs != std::set<std::string>{"DeathDummy", "YDUM"}) return fail("original 80 / 59-on-land inventory census");
    int land = -1;
    for (int tile = 0; tile < static_cast<int>(live.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(live, cell);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge && !info.ramp
            && info.movementRampType == 0) { land = tile; break; }
    }
    const int roadSet = live.theater.getGeneralTileSetIndex("PavedRoadSlopes");
    const auto tileSet = std::find_if(live.theater.getTileSets().begin(), live.theater.getTileSets().end(),
        [&](const auto& entry) { return entry.index == roadSet; });
    if (land < 0 || roadSet != 47 || tileSet == live.theater.getTileSets().end() || tileSet->tilesInSet != 4)
        return fail("original clear ground / PavedRoadSlopes tile set");
    std::map<int, OriginalTile> tiles;
    for (int tile : {land, tileSet->startTileIndex, tileSet->startTileIndex + 2}) {
        OriginalTile original;
        if (!readTile(live, tile, root / "native-original", original, error)) return fail(error);
        for (const auto& sub : original.subtiles) terrainCsv << original.tile << ',' << original.name << ',' << original.source
            << ',' << original.width << ',' << original.height << ',' << sub.index << ',' << sub.present << ',' << sub.height
            << ',' << sub.land << ',' << sub.ramp << ',' << sub.offsetX << ',' << sub.offsetY << '\n';
        tiles.emplace(tile, std::move(original));
    }
    if (!tiles.at(land).subtiles.front().present || tiles.at(land).subtiles.front().ramp != 0)
        return fail("original clear TMP is not flat");
    for (const int nativeRamp : {1, 3}) {
        const int tile = tileSet->startTileIndex + nativeRamp - 1;
        const auto& original = tiles.at(tile);
        if (original.width != 1 || original.height != 3 || original.subtiles.size() != 3)
            return fail("original Prslpe01/03 whole-stamp dimensions");
        for (const auto& sub : original.subtiles)
            if (!sub.present || sub.height != 0 || sub.land != 11 || sub.ramp != nativeRamp)
                return fail("original road TMP height/Road/ramp metadata");
    }
    // The original GetFloorHeight ramp cases 1 and 3 are affine X planes.
    // Packed TMP Z bytes describe screen depth, not these physical heights.
    // The same original three-lane stamp is surrounded by its matching banks.
    const auto independentFloor = [&](const RuntimeWindowState& state, double x, double y, int& ramp) {
        const int cx = static_cast<int>(std::floor(x + .5)), cy = static_cast<int>(std::floor(y + .5));
        const auto found = state.terrainByCell.find(runtimeCellKey(cx, cy));
        if (found == state.terrainByCell.end() || !found->second) throw std::runtime_error("independent support outside terrain");
        const auto& cell = *found->second;
        const auto file = tiles.find(cell.tileIndex);
        if (file == tiles.end() || cell.subTileIndex < 0 || cell.subTileIndex >= static_cast<int>(file->second.subtiles.size()))
            throw std::runtime_error("independent support lacks original TMP metadata");
        const auto& sub = file->second.subtiles[cell.subTileIndex];
        if (!sub.present || (sub.ramp != 0 && sub.ramp != 1 && sub.ramp != 3))
            throw std::runtime_error("independent support is not the original affine road");
        ramp = sub.ramp;
        return double(cell.level) + (ramp == 1 ? .5 + x - cx : ramp == 3 ? .5 - x + cx : 0.0);
    };
    const auto expected = [&](const RuntimeWindowState& state, const WorldObjectInstance& object) {
        RuntimeVehicleBodyPose value;
        const auto position = body(object);
        int center = 0, left = 0, right = 0, back = 0, front = 0;
        const double h = independentFloor(state, position[0], position[1], center);
        const double x0 = independentFloor(state, position[0] - .35, position[1], left);
        const double x1 = independentFloor(state, position[0] + .35, position[1], right);
        const double y0 = independentFloor(state, position[0], position[1] - .35, back);
        const double y1 = independentFloor(state, position[0], position[1] + .35, front);
        if (!(center || left || right || back || front)) return value;
        // Original default VXL projection is independently fixed here for
        // the four native body classes below; no pose or sizing helper oracle.
        constexpr double scale = .72 / 1.55;
        const double gx = (x1 - x0) / .7 * scale, gy = (y1 - y0) / .7 * scale;
        value.roll = value.terrainRoll = std::atan(gy);
        value.pitch = value.terrainPitch = -std::atan(gx * std::cos(value.roll));
        value.terrainHeight = h - position[2]; value.terrainSupported = true;
        return value;
    };
    int passed = 0, ordinaryCount = 0, canonicalCount = 0, totalPairedTicks = 0;
    try {
        for (int hz : {30, 60}) for (int nativeRamp : {1, 3}) for (const char* type : {"MTNK", "FV", "SAPC", "ROBO"}) {
            label = "road-ramp" + std::to_string(nativeRamp) + '-' + type + '-' + std::to_string(hz);
            if (!filter.empty() && label.find(filter) == std::string::npos) continue;
            stage = "original whole road stamp";
            const auto* unit = rules.findUnitType("vehicle", type);
            if (!unit || !unit->isTilter || unit->strength != std::stoi(raw.getValue(type, "Strength", "0")))
                return fail("original native body definition");
            auto storage = blankFixture(live, rules, art, hz, land);
            auto& state = *storage;
            for (auto& cell : state.world.accessTerrainCells()) {
                cell.level = nativeRamp == 1 ? int(cell.x > 65) : int(cell.x < 65);
                if (cell.x == 65 && cell.y >= 64 && cell.y <= 66) {
                    cell.tileIndex = tileSet->startTileIndex + nativeRamp - 1;
                    cell.subTileIndex = cell.y - 64; cell.level = 0;
                }
            }
            initializeRuntimeNavigation(state);
            auto* vehicle = addRuntimeObjectInstanceForOwner(state, "Americans", RuntimeBuildKind::Unit, type, {61, 65});
            if (!vehicle || vehicle->health != unit->strength || vehicle->airborne) return fail("original full-health native vehicle");
            const int id = vehicle->instanceId;
            vehicle->mission = "Sleep"; vehicle->harvestAutoSearchSuspended = true;
            vehicle->facing = runtimeFacingFromDelta(1, 0);
            if (std::string(type) == "ROBO") {
                const auto* control = findRuntimeBuildingTypeById(rules, "GAROBO");
                auto* building = addRuntimeObjectInstanceForOwner(state, "Americans", RuntimeBuildKind::Building, "GAROBO", {75, 75});
                if (!control || !building || building->health != control->strength) return fail("original robot controller");
                building->mission = "Sleep";
                building->buildAnimTicksTotal = building->buildAnimTicksRemaining = 0;
                auto* power = addRuntimeObjectInstanceForOwner(state, "Americans", RuntimeBuildKind::Building, "NANRCT", {78, 75});
                if (!power) return fail("original powered robot controller");
                power->mission = "Sleep";
                power->buildAnimTicksTotal = power->buildAnimTicksRemaining = 0;
            }
            if (!initializeFixture(state, error)) return fail(error);
            rebuildRuntimeEconomyPower(state);
            if (std::string(type) == "ROBO" && !runtimePoweredUnitIsOperational(state, *findObjectById(state, id)))
                return fail("original ROBO is not naturally powered");
            for (int lane = 0; lane < 3; ++lane) {
                const auto& cell = *state.terrainByCell.at(runtimeCellKey(65, 64 + lane));
                const auto info = runtimeCachedTerrainInfoForCell(state, cell);
                if (!info.passable || info.movementRampType != nativeRamp || info.movementLandType != RuntimeTerrainLandType::Road)
                    return fail("native navigation did not resolve the original road stamp");
            }
            std::unique_ptr<RuntimeWindowState> ordinary, canonical;
            int pairedTicks = 0, supportedSamples = 0, caseOrdinary = 0, caseCanonical = 0;
            bool movingSaved = false, stopped = false, uphill = false, downhill = false;
            const auto replicas = [&]() {
                std::vector<std::pair<const char*, RuntimeWindowState*>> result{{"original", &state}};
                if (ordinary) result.emplace_back("ordinary14", ordinary.get());
                if (canonical) result.emplace_back("ordinary14+canonical-extension", canonical.get());
                return result;
            };
            const auto actualPose = [&](RuntimeWindowState& q, const WorldObjectInstance& object) {
                RuntimeMapRenderContext context = runtimeTerrainMetadataContext(q);
                context.rules = &q.rules; context.art = &q.art;
                context.animationTick = q.tick; context.fixedTickHz = hz;
                return resolveRuntimeVehicleBodyPose(q.world, object, context, q.renderCache);
            };
            const auto equal = [&]() {
                const auto* source = findObjectById(state, id);
                if (!source || source->health != unit->strength || !source->alive) return fail("native health/body changed: " + describe(source));
                const auto sourceBody = body(*source);
                const auto oracle = expected(state, *source);
                const auto initial = actualPose(state, *source);
                for (const auto& [format, q] : replicas()) {
                    const auto* object = findObjectById(*q, id);
                    if (!object || q->tick != state.tick || q->config.fixedTickHz != hz || q->config.noCooldown
                        || q->rules.getDetailRulesHash() != ruleHash || object->type != source->type || object->owner != source->owner
                        || object->health != source->health || object->alive != source->alive || object->x != source->x
                        || object->y != source->y || object->level != source->level || object->onBridge != source->onBridge
                        || object->facing != source->facing || object->renderMoveActive != source->renderMoveActive
                        || bodyBridge(*object) != bodyBridge(*source)) return fail(std::string(format) + " restored native object differs: " + describe(object));
                    const auto position = body(*object);
                    for (int axis = 0; axis < 3; ++axis) if (std::abs(position[axis] - sourceBody[axis]) > 1e-8)
                        return fail(std::string(format) + " continuous body differs source=" + describe(source) + " restored=" + describe(object));
                    const auto pose = actualPose(*q, *object);
                    const auto sameScalar = [](double a, double b) { return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) < 1e-10; };
                    if (!sameScalar(pose.pitch, oracle.pitch) || !sameScalar(pose.roll, oracle.roll) || !sameScalar(pose.height, 0.0)
                        || !sameScalar(pose.terrainPitch, oracle.terrainPitch) || !sameScalar(pose.terrainRoll, oracle.terrainRoll)
                        || !sameScalar(pose.terrainHeight, oracle.terrainHeight) || pose.terrainSupported != oracle.terrainSupported
                        || !sameScalar(pose.pitch, initial.pitch) || !sameScalar(pose.roll, initial.roll)) {
                        std::ostringstream detail; detail << std::setprecision(17) << format << " independent road pose differs " << describe(object)
                            << " actual=" << pose.pitch << ',' << pose.roll << ',' << pose.terrainHeight << ':' << pose.terrainSupported
                            << " expected=" << oracle.pitch << ',' << oracle.roll << ',' << oracle.terrainHeight << ':' << oracle.terrainSupported;
                        return fail(detail.str());
                    }
                    movement << label << ',' << stage << ',' << format << ',' << q->tick << ',' << hz << ',' << id << ',' << object->type
                        << ',' << object->health << ',' << object->x << ',' << object->y << ',' << object->level << ',' << object->onBridge
                        << ',' << object->hasRenderPosition << ',' << object->renderMoveActive << ',' << position[0] << ',' << position[1]
                        << ',' << position[2] << ',' << bodyBridge(*object) << ',' << object->facing << ',' << pose.pitch << ',' << pose.roll
                        << ',' << pose.height << ',' << pose.terrainPitch << ',' << pose.terrainRoll << ',' << pose.terrainHeight << ',' << pose.terrainSupported
                        << ',' << oracle.pitch << ',' << oracle.roll << ',' << oracle.terrainHeight << ',' << oracle.terrainSupported << '\n';
                }
                supportedSamples += oracle.terrainSupported;
                if (ordinary && canonical) ++pairedTicks;
                return true;
            };
            const auto tickAll = [&]() {
                for (auto [format, q] : replicas()) step(*q);
                return equal();
            };
            const auto move = [&](RuntimeCellPoint destination) {
                for (auto [format, q] : replicas()) {
                    q->selectedObjectIds = {id};
                    if (!dispatchRuntimeTargetOrderFromUi(*q, RuntimeMultiplayerGameplayTargetOrderKind::Primary, destination, nullptr)
                        || q->lastCommand.acceptedCount != 1 || !q->behaviors.contains(id))
                        return fail(std::string(format) + " real UI Move rejected: " + q->interactionStatus + ' ' + describe(findObjectById(*q, id)));
                }
                return equal();
            };
            const auto save = [&]() {
                ordinary = checkpoint(state, false, root / (label + '-' + stage + "-ordinary14.ini"), error);
                canonical = checkpoint(state, true, root / (label + '-' + stage + "-canonical-extension.ini"), error);
                if (!ordinary || !canonical) return fail("strict native checkpoint: " + error);
                ++caseOrdinary; ++ordinaryCount; ++caseCanonical; ++canonicalCount;
                saves << label << ',' << stage << ",ordinary14," << state.tick << ',' << hz << ',' << id << ',' << type << ','
                    << (label + '-' + stage + "-ordinary14.ini") << '\n';
                saves << label << ',' << stage << ",ordinary14+canonical-extension," << state.tick << ',' << hz << ',' << id << ',' << type << ','
                    << (label + '-' + stage + "-canonical-extension.ini") << '\n';
                return equal();
            };
            stage = "native approach";
            if (!equal() || !move({69, 65})) return false;
            for (int tick = 0; tick < hz * 90 && !stopped; ++tick) {
                const auto previous = body(*findObjectById(state, id));
                if (!tickAll()) return false;
                const auto* object = findObjectById(state, id);
                const auto position = body(*object);
                if (position[0] < previous[0] - 1e-8 || std::abs(position[1] - 65.0) > 1e-8)
                    return fail("native player route did not traverse the original three-lane road: " + describe(object));
                if (position[0] > 64.85 && position[0] < 65.15 && object->renderMoveActive) {
                    const auto found = state.behaviors.find(id);
                    if (found == state.behaviors.end() || found->second.kind != RuntimeBehaviorKind::Move
                        || found->second.pathRequestPending || found->second.moveCooldownTicks <= 2) continue;
                    if (std::string(type) == "MTNK" && !movingSaved) {
                        stage = "moving-slope";
                        if (!save()) return false;
                        movingSaved = true;
                    }
                    stage = "real slope Stop";
                    for (auto [format, q] : replicas()) {
                        const auto originalBody = body(*findObjectById(*q, id));
                        q->selectedObjectIds = {id};
                        if (!dispatchRuntimeSelectionCommandFromUi(*q, RuntimeMultiplayerGameplayCommandType::Stop)
                            || q->lastCommand.acceptedCount != 1 || body(*findObjectById(*q, id)) != originalBody
                            || findObjectById(*q, id)->renderMoveActive || !worldObjectHasStoppedGroundPose(*findObjectById(*q, id)))
                            return fail(std::string(format) + " real Stop snapped the fractional native body");
                    }
                    stopped = true;
                }
            }
            if (!stopped || (std::string(type) == "MTNK" && !movingSaved)) return fail("natural committed road slope save/Stop window never occurred");
            stage = "stopped-slope";
            if (!equal() || !save()) return false;
            const auto resting = body(*findObjectById(state, id));
            for (int tick = 0; tick < hz; ++tick) {
                if (!tickAll() || body(*findObjectById(state, id)) != resting || findObjectById(state, id)->renderMoveActive)
                    return fail("real stopped slope pose drifted");
            }
            stage = "leave slope";
            if (!move({69, 65})) return false;
            bool arrived = false;
            for (int tick = 0; tick < hz * 90 && !arrived; ++tick) {
                if (!tickAll()) return false;
                const auto* object = findObjectById(state, id);
                arrived = object->x == 69 && object->y == 65 && !object->renderMoveActive && !state.behaviors.contains(id);
            }
            if (!arrived || actualPose(state, *findObjectById(state, id)).active()) return fail("original vehicle did not naturally settle beyond the road supports");
            nativeRamp == 1 ? uphill = true : downhill = true;
            stage = "reverse road route";
            if (!move({61, 65})) return false;
            arrived = false;
            for (int tick = 0; tick < hz * 90 && !arrived; ++tick) {
                if (!tickAll()) return false;
                const auto* object = findObjectById(state, id);
                arrived = object->x == 61 && object->y == 65 && !object->renderMoveActive && !state.behaviors.contains(id);
            }
            if (!arrived || actualPose(state, *findObjectById(state, id)).active()) return fail("return to original flat ground retained terrain tilt");
            nativeRamp == 1 ? downhill = true : uphill = true;
            if (supportedSamples < hz || !uphill || !downhill) return fail("both real traversals / stopped support were not observed");
            cases << label << ',' << type << ',' << hz << ',' << nativeRamp << ',' << state.tick - 200 << ',' << supportedSamples
                << ',' << uphill << ',' << downhill << ',' << stopped << ',' << caseOrdinary << ',' << caseCanonical << ",1,PASS\n";
            ++passed; totalPairedTicks += pairedTicks;
            std::cout << "Vehicle terrain slope " << label << " PASS nativeRoad=" << nativeRamp << " body=" << type
                << " pairedTicks=" << pairedTicks << " ordinary14=" << caseOrdinary << " canonical=" << caseCanonical << '\n';
        }
    } catch (const std::exception& exception) { return fail(exception.what()); }
    if (!passed || (filter.empty() && (passed != 16 || ordinaryCount != 20 || canonicalCount != 20)))
        return fail("complete native road / save case count");
    std::cout << "Vehicle terrain slope native summary cases=" << passed << " originalInventory=" << allIDs.size()
        << " landTypes=" << ground + amphibious << " pairedTicks=" << totalPairedTicks
        << " ordinary14=" << ordinaryCount << " canonicalExtensions=" << canonicalCount << '\n';
    if (filter.empty()) {
        auto context = runtimeTerrainMetadataContext(live);
        context.rules = &rules; context.art = &art;
        context.animationTick = live.tick; context.fixedTickHz = live.config.fixedTickHz;
        if (!runRuntimeVehicleTerrainSlopeRenderContract(context, error)) return false;
    }
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
