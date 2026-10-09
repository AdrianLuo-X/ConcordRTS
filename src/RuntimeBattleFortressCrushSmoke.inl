// Issue 26: original BFRT Omni rules and real Win32 commands. The contact
// oracle below is independent of the production crusher/eligibility helpers.
// Type eligibility and physically reachable test states are reported separately.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeBattleFortressCrushSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    IniFile originalRules, originalArt; RulesDatabase rules; ArtDatabase art;
    if (!originalRules.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(originalRules)
        || !originalArt.loadFromFile(live.report.artPath) || !art.loadFromIni(originalArt)) {
        error = "Battle Fortress: actual original rules/art unavailable"; return false;
    }
    const auto* fortress = findRuntimeUnitTypeById(rules, "BFRT");
    if (!fortress || !fortress->crusher || !fortress->omniCrusher || !fortress->omniCrushResistant
        || fortress->strength != 600 || fortress->speed != 4 || fortress->primaryWeapon != "20mmRapid") {
        error = "Battle Fortress: original BFRT rules changed"; return false;
    }
    const char* outputValue = std::getenv("CONCORD_BATTLE_FORTRESS_CRUSH_OUTPUT");
    const char* caseValue = std::getenv("CONCORD_BATTLE_FORTRESS_CRUSH_CASE");
    const std::string output = outputValue ? outputValue : "", filter = caseValue ? caseValue : "";
    if (!output.empty()) std::filesystem::create_directories(output);
    std::ofstream csv, qualification;
    if (!output.empty()) {
        csv.open(std::filesystem::path(output) / "fortress-contact.csv");
        qualification.open(std::filesystem::path(output) / "original-type-qualification.csv");
    }
    if (csv) csv << "case,branch,hz,tick,source,target,type,sourceCellX,sourceCellY,sourceX,sourceY,targetX,targetY,distanceSquared,alive,hp,slot,bridge,crushedSequence\n" << std::setprecision(17);
    if (qualification) qualification << "category,id,originalRegistered,originalOmniResistant,originalCrushable,nativeTypeQualification,physicalScope\n";
    constexpr int width = 1280, screenHeight = 720;
    constexpr const char* localOwner = "SkirmishPlayer1";
    constexpr const char* hostileOwner = "SkirmishPlayer2";
    const RECT client{0, 0, width, screenHeight}, viewport = runtimeMapViewport(client);
    std::string label, stage, fixtureFailure; std::vector<std::string> failures;
    int scenes = 0, fullTicks = 0, clicks = 0, altClicks = 0, nativeVehicles = 0, nativeInfantry = 0;
    int contactChecks = 0, beforeContactChecks = 0, crushes = 0, negativeChecks = 0, stableChecks = 0;
    int ordinarySaves = 0, exactSaves = 0, pairedTicks = 0, hashChecks = 0, capturedCommands = 0, decodedCommands = 0;
    int originalSoundChecks = 0, deployedChecks = 0, landedChecks = 0, largeGroupChecks = 0, ordinaryCrusherChecks = 0;
    const auto fail = [&](const std::string& why) { error = "Fortress " + label + " [" + stage + "]: " + why; return false; };
    int landTile = -1; std::optional<TerrainCell> water;
    auto metadataCache = std::make_unique<RuntimeMapRenderCache>();
    const auto metadata = runtimeTerrainMetadataContext(live);
    for (int tile = 0; tile < static_cast<int>(live.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(live, cell);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge
            && resolveRuntimeTerrainSubTileRampType(cell, metadata, *metadataCache) == 0) { landTile = tile; break; }
    }
    for (const auto& set : live.theater.getTileSets()) {
        const auto name = toLowerAsciiLocal(set.fileName + " " + set.setName);
        if (name.find("water") == std::string::npos && name.find("shore") == std::string::npos) continue;
        for (int tile = set.startTileIndex; tile < set.startTileIndex + set.tilesInSet && !water; ++tile)
            for (int sub = 0; sub < 16 && !water; ++sub) {
                TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = sub;
                if (resolveRuntimeTerrainSubTileLandType(cell, metadata, *metadataCache) == RuntimeTerrainLandType::Water
                    && resolveRuntimeTerrainSubTileRampType(cell, metadata, *metadataCache) == 0) water = cell;
            }
        if (water) break;
    }
    if (landTile < 0 || !water) return fail("original flat land / decoded Water2 TMP unavailable");

    // Author native maps before any simulation. Navigation alone does not
    // rebuild immutable MapFacts. Both normal startup and save restoration
    // must refer to the same real, parsed ISOMapPack5 terrain and map identity.
    // Use the legal literal-only Format5 representation already employed by
    // RuntimeRandomMap; this is fixture serialization, not a runtime codec.
    using Bytes = std::vector<unsigned char>;
    const auto append16 = [](Bytes& bytes, unsigned int value) {
        bytes.push_back(static_cast<unsigned char>(value & 255));
        bytes.push_back(static_cast<unsigned char>((value >> 8) & 255));
    };
    const auto append32 = [&](Bytes& bytes, unsigned int value) {
        append16(bytes, value); append16(bytes, value >> 16);
    };
    const auto packTiles = [&](const Bytes& raw) {
        Bytes packed;
        for (std::size_t at = 0; at < raw.size(); at += 8192) {
            const std::size_t count = std::min<std::size_t>(8192, raw.size() - at);
            Bytes chunk;
            if (count <= 238) chunk.push_back(static_cast<unsigned char>(17 + count));
            else {
                chunk.push_back(0); std::size_t extra = count - 18;
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
    };
    const auto base64 = [](const Bytes& bytes) {
        constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string text; text.reserve((bytes.size() + 2) / 3 * 4);
        for (std::size_t at = 0; at < bytes.size(); at += 3) {
            const unsigned int bits = (static_cast<unsigned int>(bytes[at]) << 16)
                | (at + 1 < bytes.size() ? static_cast<unsigned int>(bytes[at + 1]) << 8 : 0)
                | (at + 2 < bytes.size() ? bytes[at + 2] : 0);
            text += alphabet[(bits >> 18) & 63]; text += alphabet[(bits >> 12) & 63];
            text += at + 1 < bytes.size() ? alphabet[(bits >> 6) & 63] : '=';
            text += at + 2 < bytes.size() ? alphabet[bits & 63] : '=';
        }
        return text;
    };
    const auto mapDirectory = std::filesystem::absolute(output.empty()
        ? std::filesystem::path("build/issue21_battle_fortress_crush/fixture-maps")
        : std::filesystem::path(output) / "fixture-maps").lexically_normal();
    std::filesystem::create_directories(mapDirectory);
    std::array<std::string, 2> mapPaths;
    const std::string fixtureTheater = live.report.map.theater.empty() ? "TEMPERATE" : live.report.map.theater;
    for (int variant = 0; variant < 2; ++variant) {
        Bytes raw; raw.reserve(61 * 61 * 11 + 4);
        for (int y = 50; y <= 110; ++y) for (int x = 50; x <= 110; ++x) {
            const bool waterCell = variant == 1 && x >= 82 && x <= 96 && y >= 76 && y <= 85;
            append16(raw, x); append16(raw, y);
            append32(raw, static_cast<unsigned int>(waterCell ? water->tileIndex : landTile));
            raw.insert(raw.end(), {static_cast<unsigned char>(waterCell ? water->subTileIndex : 0), 0, 0});
        }
        append32(raw, 0);
        const std::string packed = base64(packTiles(raw));
        std::ostringstream text; text.imbue(std::locale::classic());
        text << "[Basic]\nName=BattleFortressContact-" << (variant ? "Water" : "Land")
            << "\nMultiplayerOnly=yes\nNewINIFormat=4\n"
            << "[Header]\nStartX=0\nStartY=0\nWidth=160\nHeight=160\nNumberStartingPoints=0\n"
            << "[Map]\nTheater=" << fixtureTheater << "\nSize=0,0,80,80\nLocalSize=0,0,80,80\n"
            << "[SpecialFlags]\nTiberiumGrows=no\nTiberiumSpreads=no\n"
            << "[Lighting]\n" << std::setprecision(17)
            << "Ambient=" << live.mapFacts.lighting.base.ambient << "\nRed=" << live.mapFacts.lighting.base.red
            << "\nGreen=" << live.mapFacts.lighting.base.green << "\nBlue=" << live.mapFacts.lighting.base.blue
            << "\nGround=" << live.mapFacts.lighting.base.ground << "\nLevel=" << live.mapFacts.lighting.base.level
            << "\n[IsoMapPack5]\n";
        for (std::size_t at = 0; at < packed.size(); at += 70) text << at / 70 + 1 << '=' << packed.substr(at, 70) << '\n';
        mapPaths[variant] = (mapDirectory / (variant ? "crush-water.map" : "crush-land.map")).generic_string();
        std::ofstream mapFile(mapPaths[variant], std::ios::binary | std::ios::trunc);
        mapFile << text.str(); mapFile.close();
        if (!mapFile) return fail("actual native fixture map could not be persisted: " + mapPaths[variant]);
    }


    struct Spawn {
        int id = 0; std::string type; RuntimeCellPoint cell; std::string owner;
        int slot = -1; bool idleLanding = false; int facing = -1; bool belowBridge = false;
    };
    const auto make = [&](int hz, bool fast, const std::vector<Spawn>& spawns, bool wet = false, const std::string& mapOverride = std::string{}, bool thirdHostile = false) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        seedRuntimeLoadScratch(s, live, false); s.config = live.config;
        s.config.fixedTickHz = hz; s.config.gameMode = "Battle"; s.config.playerCountry = "Americans";
        s.config.noCooldown = fast; s.config.multiplayerActive = s.config.computerAiEnabled = s.config.shortGame = false;
        s.config.crates = false; s.config.superweapons = std::any_of(spawns.begin(), spawns.end(), [](const auto& spawn) { return spawn.type == "NAIRON" || spawn.type == "GATECH"; }); s.config.startingUnits = 0; s.config.techLevel = 10;
        s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.config.keyboardReferenceLayout = false; s.config.keyBindings.clear(); s.campaignMode = false;
        s.skirmishMatchFinished = s.skirmishVictory = false; s.skirmishResult.clear();
        s.rules = rules; s.art = art; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.config.defaultMapPath = mapOverride.empty() ? mapPaths[wet ? 1 : 0] : mapOverride; IniFile mapIni; MapDatabase map;
        if (!mapIni.loadFromFile(s.config.defaultMapPath) || !map.loadFromIni(mapIni, &originalRules)
            || !s.world.loadFromMap(map, &s.rules)) {
            fail("actual backing map parse/hydration failed"); return std::unique_ptr<RuntimeWindowState>{};
        }
        const auto& summary = map.getSummary();
        s.report.map.loaded = true; s.report.map.path = s.config.defaultMapPath;
        s.report.map.name = summary.name; s.report.map.theater = summary.theater;
        s.report.map.size = summary.size; s.report.map.localSize = summary.localSize;
        s.report.map.width = summary.width; s.report.map.height = summary.height;
        s.report.map.isoTiles = static_cast<int>(map.getIsoTiles().size());
        s.report.map.overlayCells = static_cast<int>(map.getOverlayCells().size());
        s.report.map.structures = static_cast<int>(summary.structureCount); s.report.map.units = static_cast<int>(summary.unitCount);
        s.report.map.infantry = static_cast<int>(summary.infantryCount); s.report.map.triggers = static_cast<int>(summary.triggerCount);
        s.report.world = s.world.getSummary(); s.mapFacts = map.buildMapFacts(&s.theater);
        s.mapFactsLoaded = !s.mapFacts.cells.empty();
        if (!s.world.getSummary().loaded || s.world.getTerrainCells().empty() || s.mapFacts.cells.empty() || (mapOverride.empty() && (s.world.getTerrainCells().size() != 3721 || s.mapFacts.cells.size() != 3721)))
            { fail("actual backed native terrain/domain missing"); return std::unique_ptr<RuntimeWindowState>{}; }
        const MapQuery query(s.mapFacts);
        for (const auto& cell : s.world.getTerrainCells()) {
            const auto* fact = query.getCell(cell.x, cell.y);
            if (!fact || !fact->hasTerrain || cell.cellIndex512 != cell.y * 512 + cell.x
                || fact->terrain.tileIndex != cell.tileIndex || fact->terrain.subTileIndex != cell.subTileIndex)
                { fail("native TMP/MapFacts/512 identity mismatch"); return std::unique_ptr<RuntimeWindowState>{}; }
        }
        s.terrainByCell = buildRuntimeTerrainLookup(s.world);
        s.terrainInfoByCell.clear();
        if (!mapOverride.empty()) s.world.accessObjects().clear(); // Initial scenario authoring, before any real tick.
        const auto add = [&](const Spawn& spawn) {
            WorldObjectInstance object; object.instanceId = spawn.id; object.mapId = "Fortress" + std::to_string(spawn.id);
            object.type = spawn.type; object.owner = spawn.owner; object.x = spawn.cell.x; object.y = spawn.cell.y;
            object.cell = runtimeCellKey(object.x, object.y); object.alive = object.poweredOperational = true;
            object.kind = s.rules.findBuildingType(spawn.type) ? WorldObjectKind::Structure
                : runtimeBuildKindForUnitTypeId(s.rules, spawn.type) == RuntimeBuildKind::Infantry
                    ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
            object.subCell = spawn.slot; object.mission = spawn.idleLanding ? "Guard" : "Sleep";
            object.level = terrainLevelAtRuntimeCell(s,object.x,object.y); object.onBridge = runtimeCellIsBridge(s,object.x,object.y);
            if (spawn.belowBridge) { object.onBridge=false; for(const auto& cell:s.world.getTerrainCells()) if(cell.x==object.x&&cell.y==object.y){object.level=cell.level;break;} }
            object.health = runtimeMaxHealthForObject(s, object); object.facing = spawn.facing >= 0 ? spawn.facing : runtimeFacingFromDelta(1, 0);
            object.turretFacing = object.facing;
            initializeRuntimeObjectAmmo(object, runtimeAmmoRulesForObject(s.rules, object));
            applyRuntimeInitialObjectFlightState(s.rules, object, spawn.idleLanding);
            runtimeInitializeModExtensions(object, s.rules, hz); s.world.accessObjects().push_back(std::move(object));
        };
        for (const auto& spawn : spawns) add(spawn);
        if (mapOverride.empty()) { add({100,"GAPOWR",{104,104},localOwner});add({998,"NANRCT",{52,52},hostileOwner}); }
        else {
            std::vector<RuntimeCellPoint> quiet;
            for(const auto& cell:s.world.getTerrainCells()) {
                if(quiet.size()==2)break;bool legal=true;
                for(int dy=0;dy<4&&legal;++dy)for(int dx=0;dx<4&&legal;++dx){const auto* fact=query.getCell(cell.x+dx,cell.y+dy);
                    if(!fact||!fact->hasTerrain||fact->isWater||fact->isBridge||fact->isCliff||fact->occupiedByStructure||fact->heightLevel!=cell.level)legal=false;}
                for(const auto& spawn:spawns)if(std::hypot(double(cell.x-spawn.cell.x),double(cell.y-spawn.cell.y))<16)legal=false;
                for(const auto& prior:quiet)if(std::hypot(double(cell.x-prior.x),double(cell.y-prior.y))<16)legal=false;
                if(legal)quiet.push_back({cell.x,cell.y});
            }
            if(quiet.size()!=2){fail("actual bridge map lacks two quiet legal original base foundations");return std::unique_ptr<RuntimeWindowState>{};}
            add({100,"GAPOWR",quiet[0],localOwner});add({998,"NANRCT",quiet[1],hostileOwner});
        }
        // This allied-target scenario still needs a genuine third hostile
        // survivor. Author its original base before initialization/tick zero;
        // two mutually allied houses would correctly end the Battle.
        if(thirdHostile) add({997,"NANRCT",{52,60},"SkirmishPlayer3"});
        s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner = localOwner; s.economy.credits = 20000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        RuntimeConfig::PlayerSlot local; local.enabled = local.human = local.local = true;
        local.name = "Fortress local"; local.country = "Americans"; local.team = "-";
        RuntimeConfig::PlayerSlot hostile; hostile.enabled = true; hostile.name = "Fortress hostile";
        hostile.country = "Russians"; hostile.team = "-"; s.config.playerSlots = {local, hostile};
        if(thirdHostile) { auto third=hostile;third.name="Fortress quiet third hostile";s.config.playerSlots.push_back(std::move(third)); }
        s.skirmishCountryByOwner.clear(); s.skirmishTeamByOwner.clear(); s.playerCommands.alliances.clear();
        s.skirmishCountryByOwner["skirmishplayer1"] = "Americans";
        s.skirmishCountryByOwner["skirmishplayer2"] = "Russians";
        if(thirdHostile) s.skirmishCountryByOwner["skirmishplayer3"]="Russians";
        s.humanHouseAuthorities.clear(); RuntimeHumanHouseAuthority authority;
        authority.owner = localOwner; authority.canonicalOwner = "skirmishplayer1";
        authority.country = "Americans"; authority.localFacade = true; s.humanHouseAuthorities.push_back(authority);
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        ensureRuntimeGarrisonsInitialized(s); ensureRuntimeGatesInitialized(s); ensureRuntimeRepairDepotsInitialized(s);
        (void)ensureRuntimeAircraftServicesInitialized(s); ensureRuntimeMenuKeyboardLoaded(s);
        initializeRuntimeOfflineGameplayRouting(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s); }
        rebuildRuntimeEconomyPower(s);
        if (runtimeOwnersAreAllied(s, localOwner, hostileOwner))
            { fail("original Battle fixture is unexpectedly allied"); return std::unique_ptr<RuntimeWindowState>{}; }
        return storage;
    };
    const auto step = [&](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false);
        completeRuntimeWorldRefresh(s); ++fullTicks;
        if (s.skirmishMatchFinished && fixtureFailure.empty()) fixtureFailure = "Battle ended: " + s.skirmishResult;
    };
    enum class Route { Direct, Offline, Captured };
    struct Input {
        RuntimeWindowState& state; HWND window = nullptr; WNDPROC original = nullptr;
        RuntimeMultiplayerRuntimeBridge bridge{}; Route route = Route::Offline;
        std::vector<RuntimeMultiplayerGameplayCommand> captured;
        ~Input() {
            state.multiplayerIntentRouter.setCaptureCallback({}); state.multiplayerBridge = nullptr;
            if (window) {
                if (GetCapture() == window) ReleaseCapture();
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                if (original) SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
                releaseRuntimeBackBuffer(state.backBuffer); DestroyWindow(window);
            }
        }
    };
    const auto open = [&](Input& input, Route route) {
        auto& s = input.state; input.route = route; s.multiplayerBridge = nullptr;
        s.offlineGameplayIntentRouting = false; s.multiplayerIntentRouter.setCaptureCallback({});
        if (route == Route::Offline) {
            initializeRuntimeOfflineGameplayRouting(s);
            s.multiplayerIntentRouter.setCaptureCallback([&input](const auto& command, std::string& reason) {
                input.captured.push_back(command); return dispatchRuntimeOfflineGameplayCommand(input.state, command, reason);
            });
        } else if (route == Route::Captured) {
            s.multiplayerBridge = &input.bridge;
            s.multiplayerIntentRouter.setCaptureCallback([&input](const auto& command, std::string& reason) {
                input.captured.push_back(command); reason.clear(); return true;
            });
        }
        if (runtimeUsesGameplayIntentRouter(s) != (route != Route::Direct)) return fail("actual direct/routed authority mismatch");
        input.window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"Real Fortress contact commands",
            WS_POPUP, -16000, -16000, width, screenHeight, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!input.window) return fail("hidden actual Win32 input unavailable");
        SetWindowLongPtrW(input.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));
        input.original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(input.window, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(runtimeWindowProc)));
        HDC screen = GetDC(nullptr); const bool dib = screen && ensureRuntimeBackBuffer(s.backBuffer, screen, width, screenHeight);
        if (screen) ReleaseDC(nullptr, screen);
        return input.original && dib ? true : fail("actual Win32 procedure/native DIB unavailable");
    };
    struct ThreadAlt {
        BYTE original[256]{}; bool saved = false, enabled = false;
        ThreadAlt() {
            saved = GetKeyboardState(original) != 0; if (!saved) return;
            BYTE modified[256]; std::copy(std::begin(original), std::end(original), std::begin(modified));
            modified[VK_MENU] = BYTE((modified[VK_MENU] & 1) | 0x80);
            enabled = SetKeyboardState(modified) != 0 && (GetKeyState(VK_MENU) & 0x8000) != 0;
        }
        ~ThreadAlt() { if (saved) SetKeyboardState(original); }
    };
    const auto click = [&](Input& input, POINT pixel, bool alt = false) {
        RuntimeSnapshotActiveStateScope active(input.state);
        const auto send = [&]() {
            SendMessageW(input.window, WM_MOUSEMOVE, 0, MAKELPARAM(pixel.x, pixel.y));
            SendMessageW(input.window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pixel.x, pixel.y));
            SendMessageW(input.window, WM_LBUTTONUP, 0, MAKELPARAM(pixel.x, pixel.y)); ++clicks;
        };
        if (alt) { ThreadAlt keyboard; if (!keyboard.enabled) return fail("owned-thread actual VK_MENU state unavailable");
            send(); ++altClicks; }
        else send();
        return true;
    };
    const auto objectPoint = [&](RuntimeWindowState& s, int id) -> std::optional<POINT> {
        RuntimeSnapshotActiveStateScope active(s); const auto* object = findObjectById(s, id); if (!object) return std::nullopt;
        centerRuntimeCameraOnCell(s, client, runtimeCellPointForObject(*object)); ensureRuntimeCameraForViewport(s, viewport);
        invalidateRuntimeObjectProjectionCache(s); const auto& projected = runtimeProjectedObjectsForViewport(s, viewport);
        const auto hit = [&](POINT p) { const auto* entry = findProjectedObjectAtPoint(projected, p);
            return pointInRectInclusive(viewport, p) && entry && entry->object && entry->object->instanceId == id; };
        for (const auto& candidate : projected) if (candidate.object && candidate.object->instanceId == id) {
            for (POINT p : candidate.footprintHitCenters) if (hit(p)) return p;
            POINT p{(candidate.bounds.left + candidate.bounds.right) / 2, (candidate.bounds.top + candidate.bounds.bottom) / 2};
            if (hit(p)) return p;
            for (int y = candidate.bounds.top; y < candidate.bounds.bottom; y += 2)
                for (int x = candidate.bounds.left; x < candidate.bounds.right; x += 2) if (hit({x,y})) return POINT{x,y};
        }
        return std::nullopt;
    };
    const auto cellPoint = [&](RuntimeWindowState& s, RuntimeCellPoint goal, bool empty) -> std::optional<POINT> {
        RuntimeSnapshotActiveStateScope active(s); centerRuntimeCameraOnCell(s, client, goal); ensureRuntimeCameraForViewport(s, viewport);
        POINT point = runtimeScreenPointForCell(s, viewport, goal, terrainLevelAtRuntimeCell(s, goal.x, goal.y));
        for (int dy = -9; dy <= 9; ++dy) for (int dx = -9; dx <= 9; ++dx) {
            POINT q{point.x+dx,point.y+dy}, picked = approximateCellFromScreenPoint(s, viewport, q);
            if (pointInRectInclusive(viewport,q) && picked.x == goal.x && picked.y == goal.y
                && (!empty || !findProjectedObjectAtPoint(runtimeProjectedObjectsForViewport(s, viewport),q))) return q;
        }
        return std::nullopt;
    };
    enum class Order { AltEndpoint, Through, Attack };
    const auto command = [&](Input& input, const std::vector<int>& actors, int targetId, RuntimeCellPoint goal, Order order) {
        auto& s = input.state; RuntimeSnapshotActiveStateScope active(s); applySelection(s, actors, false, false);
        const auto pixel = order == Order::Attack ? objectPoint(s,targetId) : cellPoint(s,goal,order == Order::Through);
        if (!pixel) return fail("actual command cannot pick original object/terrain");
        const auto before = s.world.deterministicSimulationStateHash(); const auto count = input.captured.size();
        if (!click(input,*pixel,order == Order::AltEndpoint)) return false;
        if (input.route == Route::Captured) {
            if (input.captured.size() != count+1 || s.world.deterministicSimulationStateHash() != before)
                return fail("network facade bypassed causal capture or duplicated command");
            const auto& sent = input.captured.back(); std::vector<std::uint8_t> bytes; RuntimeMultiplayerGameplayCommand received;
            std::string reason;
            if (!encodeRuntimeMultiplayerGameplayCommand(sent,bytes,reason)
                || !decodeRuntimeMultiplayerGameplayCommand(bytes,received,reason) || received != sent)
                return fail("actual captured command codec changed payload: " + reason);
            if (prevalidateRuntimeMultiplayerGameplayCommand(s,hostileOwner,received,reason))
                return fail("wrong authenticated house accepted own BFRT command");
            if (!prevalidateRuntimeMultiplayerGameplayCommand(s,localOwner,received,reason)
                || !applyRuntimeMultiplayerGameplayCommand(s,localOwner,received))
                return fail("actual captured authenticated receive rejected: " + reason);
            ++capturedCommands; ++decodedCommands;
        }
        for (int id : actors) {
            const auto behavior = s.behaviors.find(id);
            if (behavior == s.behaviors.end() || behavior->second.kind != (order == Order::Attack ? RuntimeBehaviorKind::Attack : RuntimeBehaviorKind::Move)
                || (order == Order::Attack && behavior->second.targetObjectId != targetId))
                return fail("actual WM order was not retained for source " + std::to_string(id) + ": " + s.interactionStatus);
            if (order == Order::AltEndpoint && actors.size() == 1 && behavior->second.moveTarget != goal)
                return fail("occupied endpoint was silently replaced by a neighbor");
        }
        return true;
    };
    const auto key = [&](Input& input, int code, const std::vector<int>& actors) {
        RuntimeSnapshotActiveStateScope active(input.state); applySelection(input.state,actors,false,false);
        SendMessageW(input.window,WM_KEYDOWN,code,1);
        SendMessageW(input.window,WM_KEYUP,code,LPARAM(1)|(LPARAM(1)<<30)|(LPARAM(1)<<31));
    };
    struct Pose {
        double x=0,y=0,z=0; int cellX=0,cellY=0,slot=-1,hp=0,facing=-1;
        bool alive=false,air=false,moving=false,render=false,bridge=false;
        bool operator==(const Pose&) const = default;
    };
    const auto pose = [](const WorldObjectInstance& o) {
        return Pose{runtimeObjectRenderX(o),runtimeObjectRenderY(o),runtimeObjectRenderLevel(o),o.x,o.y,o.subCell,o.health,o.facing,
            o.alive,o.airborne,o.renderMoveActive,o.hasRenderPosition,o.hasRenderPosition ? o.renderOnBridge : o.onBridge};
    };
    // Original active infantry locations. The quarter-cell offset is applied
    // once to raw continuous coordinates; it is not a sprite/bounding-box test.
    const auto physical = [&](const WorldObjectInstance& o) {
        double x=runtimeObjectRenderX(o),y=runtimeObjectRenderY(o);
        if (o.kind == WorldObjectKind::Infantry) {
            if (o.subCell==2) { x+=.25;y-=.25; }
            else if (o.subCell==4) { x+=.25;y+=.25; }
            else if (o.subCell==3) { x-=.25;y+=.25; }
        }
        return std::pair<double,double>{x,y};
    };
    const auto record = [](const RuntimeWindowState& s,int id) {
        const auto b=s.behaviors.find(id); return b==s.behaviors.end() ? std::string{} : runtimeMultiplayerSnapshotBehaviorRecord(id,b->second);
    };
    const auto describe = [&](const RuntimeWindowState& s,int id) {
        std::ostringstream text; text<<std::setprecision(17)<<"tick="<<s.tick<<" id="<<id;
        if (const auto* object=findRuntimeObjectByIdIncludingInactive(s,id)) { const auto p=pose(*object);
            text<<" type="<<object->type<<" cell="<<p.cellX<<','<<p.cellY<<" XYZ="<<p.x<<','<<p.y<<','<<p.z
                <<" hp="<<p.hp<<" alive/air/moving/bridge="<<p.alive<<'/'<<p.air<<'/'<<p.moving<<'/'<<p.bridge
                <<" slot="<<p.slot<<" dock="<<object->dockedToObjectId<<" mission="<<object->mission;
        } else text<<" missing";
        text<<" behavior="<<record(s,id); return text.str();
    };
    const auto crushEvents = [](const RuntimeWindowState& s,int target) {
        std::vector<RuntimeReplayEventSnapshot> result;
        for (const auto& event:s.recentReplayEvents) if (event.kind=="ObjectCrushed" && event.targetObjectId==target) result.push_back(event);
        return result;
    };
    const auto checkpoint = [&](RuntimeWindowState& s,bool exact) {
        std::ostringstream base,complete,canonical; IniFile baseIni,exactIni;
        if (!writeRuntimeStateToStream(s,base,error)||!baseIni.loadFromString(base.str())) return std::unique_ptr<RuntimeWindowState>{};
        if (exact && (!writeRuntimeStateToStream(s,complete,error)||!writeRuntimeMultiplayerSnapshotExtension(s,complete,error)
            ||!exactIni.loadFromString(complete.str())||!writeRuntimeMultiplayerSnapshotCanonicalIni(exactIni,canonical,error)
            ||!exactIni.loadFromString(canonical.str()))) return std::unique_ptr<RuntimeWindowState>{};
        if (!output.empty()) {
            std::ofstream payload(std::filesystem::path(output)/(label+(exact ? "-canonical-extension.ini" : "-ordinary.ini")),std::ios::binary);
            payload<<(exact ? canonical.str() : base.str()); if (!payload) return std::unique_ptr<RuntimeWindowState>{};
        }
        auto other=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*other,s,false);
        // A complete ordinary v14 base retains AV authority. The canonical
        // extension restores full paths/relations, not a portable V15 envelope.
        if (!materializeRuntimeLoadStateFromIni(*other,baseIni,error)
            ||(exact&&!materializeRuntimeMultiplayerSnapshotExtension(*other,exactIni,error))) return std::unique_ptr<RuntimeWindowState>{};
        other->runtimeSoundsEnabled=false; other->runtimePreferencePersistenceEnabled=false;
        other->runtimeSuppressPlayerAudioFeedback=true; initializeRuntimeOfflineGameplayRouting(*other);
        if (exact) ++exactSaves; else ++ordinarySaves; return other;
    };
    const auto compare = [&](const RuntimeWindowState& a,const RuntimeWindowState& b,bool exact) {
        if (a.tick!=b.tick) return fail("saved tick changed");
        for (int id:{1,2}) {
            const auto* x=findRuntimeObjectByIdIncludingInactive(a,id); const auto* y=findRuntimeObjectByIdIncludingInactive(b,id);
            if (!x||!y||pose(*x)!=pose(*y)||x->dockedToObjectId!=y->dockedToObjectId||x->currentAmmo!=y->currentAmmo)
                return fail("saved physical contact/slot/HP changed: source=["+describe(a,id)+"] restored=["+describe(b,id)+']');
        }
        const auto x=a.behaviors.find(1),y=b.behaviors.find(1);
        if ((x==a.behaviors.end())!=(y==b.behaviors.end())) return fail("saved contact behavior presence changed");
        if (x!=a.behaviors.end()) {
            const auto& m=x->second; const auto& n=y->second;
            if (exact ? record(a,1)!=record(b,1)
                : m.kind!=n.kind||m.targetObjectId!=n.targetObjectId||m.moveTarget!=n.moveTarget
                    ||m.moveCooldownTicks!=n.moveCooldownTicks||m.moveSegmentFrom!=n.moveSegmentFrom
                    ||m.renderFrom!=n.renderFrom||m.renderTo!=n.renderTo||m.renderFromLevel!=n.renderFromLevel||m.renderToLevel!=n.renderToLevel
                    ||m.renderFromOffsetX!=n.renderFromOffsetX||m.renderFromOffsetY!=n.renderFromOffsetY
                    ||m.renderToOffsetX!=n.renderToOffsetX||m.renderToOffsetY!=n.renderToOffsetY
                    ||m.renderFromOnBridge!=n.renderFromOnBridge||m.renderToOnBridge!=n.renderToOnBridge
                    ||m.renderMoveTicksTotal!=n.renderMoveTicksTotal||m.renderMoveTicksRemaining!=n.renderMoveTicksRemaining||m.renderArrivalDistanceAfter!=n.renderArrivalDistanceAfter)
                return fail("saved committed Omni Move/Attack segment changed: source=["+record(a,1)+"] restored=["+record(b,1)+']');
        }
        if (exact) {
            std::string ae,be; const auto ah=runtimeStandaloneSimulationStateHash(a,ae),bh=runtimeStandaloneSimulationStateHash(b,be);
            if (!ae.empty()||!be.empty()||ah!=bh) return fail("canonical complete authority changed: "+ae+" / "+be);
            ++hashChecks;
        }
        ++pairedTicks; return true;
    };
    const auto settleLanding = [&](RuntimeWindowState& s,int id) {
        const auto* object=findObjectById(s,id); if (!object) return fail("landing target missing");
        if (!object->airborne) return true;
        for (int n=0;n<s.config.fixedTickHz*8;++n) {
            step(s); object=findObjectById(s,id); if (!object) return fail("original jumpjet disappeared before real landing");
            if (!object->airborne && !object->renderMoveActive
                && runtimeObjectRenderLevel(*object)==terrainLevelAtRuntimeCell(s,object->x,object->y)) { ++landedChecks;return true; }
        }
        return fail("actual idle SHAD did not naturally reach ground");
    };
    const auto scene = [&](const std::string& name,const auto& body) {
        if (!filter.empty()&&filter!=name) return;
        label=name;stage="original backed fixture";fixtureFailure.clear();
        bool okay=body(); if (!fixtureFailure.empty()) { fail(fixtureFailure);okay=false; }
        if (okay) { ++scenes;std::cout<<"Fortress "<<name<<" PASS\n"; }
        else { failures.push_back(error);std::cout<<error<<'\n';error.clear(); }
    };
    // Independently reconstruct only the already committed ordinary vehicle
    // body's next frame when a death callback clears its presentation. This
    // uses the established two-cell constant-deceleration policy, not the new
    // Omni contact/eligibility helper or a newly fabricated simulation state.
    const auto nextPoint=[&](const WorldObjectInstance& object,const std::optional<RuntimeObjectBehavior>& segment) {
        if(!segment||segment->moveCooldownTicks<=0||segment->renderMoveTicksTotal<=0||segment->renderMoveTicksRemaining<=0)return physical(object);
        const auto& b=*segment;double fraction=std::clamp(1.0-double(b.renderMoveTicksRemaining-1)/b.renderMoveTicksTotal,0.0,1.0);
        const double length=std::hypot(double(b.renderTo.x-b.renderFrom.x),double(b.renderTo.y-b.renderFrom.y));
        if(b.renderArrivalDistanceAfter>=0&&b.renderArrivalDistanceAfter<2&&length>0&&fraction>0&&fraction<1){
            const auto stopTime=[](double distance){return distance>=2?distance+2:2*std::sqrt(2*distance);};
            const double start=stopTime(b.renderArrivalDistanceAfter+length),end=stopTime(b.renderArrivalDistanceAfter);
            const double time=start+(end-start)*fraction;
            const double remaining=time>=4?time-2:time*time/8;
            fraction=std::clamp((b.renderArrivalDistanceAfter+length-remaining)/length,0.0,1.0);
        }
        return std::pair<double,double>{b.renderFrom.x+b.renderFromOffsetX+(b.renderTo.x+b.renderToOffsetX-b.renderFrom.x-b.renderFromOffsetX)*fraction,
            b.renderFrom.y+b.renderFromOffsetY+(b.renderTo.y+b.renderToOffsetY-b.renderFrom.y-b.renderFromOffsetY)*fraction};
    };
    std::map<std::pair<int,bool>,std::vector<Pose>> cooldownMotion;
    const auto positive = [&](const std::string& type,int hz,Route route,Order order,bool fast,bool save,bool deploy) {
        const bool infantry=runtimeBuildKindForUnitTypeId(rules,type)==RuntimeBuildKind::Infantry;
        auto storage=make(hz,fast,{{1,"BFRT",{76,80},localOwner},{2,type,{80,80},hostileOwner,infantry ? 2 : -1,type=="SHAD"}});
        if (!storage) return false;auto& s=*storage;Input input{s};if (!open(input,route)) return false;
        RuntimeSnapshotActiveStateScope active(s); const auto* targetRule=findRuntimeUnitTypeById(s.rules,type);
        if (!targetRule||targetRule->strength<=0||targetRule->omniCrushResistant
            ||findObjectById(s,2)->health!=targetRule->strength||runtimeNoCooldownForOwner(s,localOwner)!=fast)
            return fail("original eligible type/HP/local NoCooldown mismatch");
        if (type=="SHAD"&&!settleLanding(s,2)) return false;
        if (deploy) {
            // The actual enemy facade issues D; no deployment flag is filled.
            const auto owner=s.economy.owner; s.economy.owner=hostileOwner;
            RuntimeHumanHouseAuthority hostile;hostile.owner=hostileOwner;hostile.canonicalOwner="skirmishplayer2";
            hostile.country="Russians";hostile.localFacade=true;s.humanHouseAuthorities.push_back(hostile);
            const auto sourceRoute=input.route; s.offlineGameplayIntentRouting=false;s.multiplayerBridge=nullptr;
            key(input,'D',{2});
            for (int n=0;n<hz*4;++n) { step(s);const auto* target=findObjectById(s,2);
                if (target&&target->deployed&&!runtimeMissionIsDeploying(*target)) break; }
            s.economy.owner=owner;s.humanHouseAuthorities.pop_back();
            if (sourceRoute==Route::Offline) initializeRuntimeOfflineGameplayRouting(s);
            else if (sourceRoute==Route::Captured) s.multiplayerBridge=&input.bridge;
            const auto* target=findObjectById(s,2);
            if (!target||!runtimeObjectHasDeployedState(s,*target)) return fail("actual enemy D did not deploy original GGI");
            ++deployedChecks;
        }
        const WorldObjectInstance victim=*findObjectById(s,2);const auto targetPoint=physical(victim);std::vector<Pose> motionTrace;
        const auto originalSource=pose(*findObjectById(s,1));
        const auto goal=order==Order::Through ? RuntimeCellPoint{82,80} : RuntimeCellPoint{80,80};
        stage="real WM occupied endpoint / through Move / Attack";
        if (!command(input,{1},2,goal,order)) return false;
        if (!crushEvents(s,2).empty()||!findObjectById(s,2)||pose(*findObjectById(s,1))!=originalSource)
            return fail("mouse dispatch itself killed/teleported before any actual simulation contact");
        std::unique_ptr<RuntimeWindowState> ordinary,exact;bool saved=false,killed=false;int ordinaryUntil=-1;
        std::optional<RuntimeSpatialAudioPosition> crushSourcePosition;
        const auto inspect = [&](RuntimeWindowState& state,const char* branch,const WorldObjectInstance& beforeTarget,const WorldObjectInstance& beforeSource,const std::optional<RuntimeObjectBehavior>& beforeSegment) {
            const auto* source=findRuntimeObjectByIdIncludingInactive(state,1);
            const auto* target=findRuntimeObjectByIdIncludingInactive(state,2);
            if (!source||!target) return fail(std::string(branch)+" lost original contact identities");
            auto sourcePoint=source->alive ? physical(*source) : nextPoint(beforeSource,beforeSegment);const auto victimPoint=physical(beforeTarget);
            const double dx=sourcePoint.first-victimPoint.first,dy=sourcePoint.second-victimPoint.second,d2=dx*dx+dy*dy;
            const auto events=crushEvents(state,2);
            if (target->alive) {
                if (target->health!=victim.health||physical(*target)!=targetPoint||target->subCell!=victim.subCell)
                    return fail(std::string(branch)+" original passive target changed before crush: "+describe(state,2));
                if (!events.empty()||source->cell==target->cell)
                    return fail(std::string(branch)+" precommit killed or logically overlapped an alive victim");
                if (d2>=.25) ++beforeContactChecks;
                else return fail(std::string(branch)+" physically eligible contact failed to crush");
            } else if (beforeTarget.alive) {
                if (events.size()!=1||events.front().objectId!=1||events.front().reason!="Crush"
                    ||events.front().renderIntentId!="death:crush"||events.front().tick!=state.tick
                    ||!(d2<.25)||pose(*source).bridge!=pose(beforeTarget).bridge)
                    return fail(std::string(branch)+" death lacks unique original physical-contact cause d2="+std::to_string(d2)
                        +" events="+std::to_string(events.size())+" source=["+describe(state,1)+"] target=["+describe(state,2)+']');
                const auto occupancy=buildRuntimeOccupancyLookup(state);
                if (std::any_of(occupancy.begin(),occupancy.end(),[](const auto& entry){return entry.second==2;}))
                    return fail(std::string(branch)+" crushed target left ghost occupancy");
                if (std::string(branch)=="source") crushSourcePosition=RuntimeSpatialAudioPosition{sourcePoint.first,sourcePoint.second,runtimeObjectRenderLevel(beforeSource)};
                ++contactChecks;
            }
            if (csv) csv<<label<<','<<branch<<','<<hz<<','<<state.tick<<",1,2,"<<type<<','<<source->x<<','<<source->y<<','
                <<sourcePoint.first<<','<<sourcePoint.second<<','<<victimPoint.first<<','<<victimPoint.second<<','<<d2<<','
                <<target->alive<<','<<target->health<<','<<target->subCell<<','<<pose(*source).bridge<<','
                <<(events.empty()?0:events.back().sequence)<<'\n';
            return true;
        };
        stage="complete ticks / strict half-cell contact / actual stock motion";
        for (int n=0;n<hz*25;++n) {
            const auto* oldTarget=findRuntimeObjectByIdIncludingInactive(s,2); if (!oldTarget) return fail("victim identity missing");
            const auto beforeTarget=*oldTarget;const auto beforeSource=*findRuntimeObjectByIdIncludingInactive(s,1);std::optional<RuntimeObjectBehavior> beforeSegment;
            if(const auto b=s.behaviors.find(1);b!=s.behaviors.end())beforeSegment=b->second;
            step(s);
            if (!inspect(s,"source",beforeTarget,beforeSource,beforeSegment)) return false;
            const auto* source=findRuntimeObjectByIdIncludingInactive(s,1);const auto* target=findRuntimeObjectByIdIncludingInactive(s,2);
            if(type=="MTNK"&&route==Route::Offline&&order==Order::AltEndpoint)motionTrace.push_back(pose(*source));
            if (source->alive) {
                const double delta=std::hypot(runtimeObjectRenderX(*source)-runtimeObjectRenderX(beforeSource),
                    runtimeObjectRenderY(*source)-runtimeObjectRenderY(beforeSource));
                const double speed=std::max(1.0,std::floor(fortress->speed*256.0/100.0));
                if (delta>1.25*speed*15.0/(256.0*hz)+2.0/16777216.0)
                    return fail("crush pursuit teleported or accelerated original BFRT Speed");
            }
            for (const auto& p:s.projectiles) if (p.attackerObjectId==1&&p.targetObjectId==2)
                return fail("Omni pursued eligible target with a remote projectile instead of contact");
            if (exact) { const auto bt=*findRuntimeObjectByIdIncludingInactive(*exact,2),bs=*findRuntimeObjectByIdIncludingInactive(*exact,1);std::optional<RuntimeObjectBehavior> bm;
                if(const auto b=exact->behaviors.find(1);b!=exact->behaviors.end())bm=b->second;step(*exact);
                if (!inspect(*exact,"canonical",bt,bs,bm)||!compare(s,*exact,true)) return false; }
            if (ordinary&&s.tick<=ordinaryUntil) { const auto bt=*findRuntimeObjectByIdIncludingInactive(*ordinary,2),bs=*findRuntimeObjectByIdIncludingInactive(*ordinary,1);std::optional<RuntimeObjectBehavior> bm;
                if(const auto b=ordinary->behaviors.find(1);b!=ordinary->behaviors.end())bm=b->second;step(*ordinary);
                if (!inspect(*ordinary,"ordinary",bt,bs,bm)||!compare(s,*ordinary,false)) return false; }
            if (save&&!saved&&target->alive) {
                const auto b=s.behaviors.find(1);const auto p=physical(*source);
                const double d2=(p.first-targetPoint.first)*(p.first-targetPoint.first)+(p.second-targetPoint.second)*(p.second-targetPoint.second);
                if (b!=s.behaviors.end()&&b->second.renderTo==RuntimeCellPoint{80,80}
                    &&b->second.renderMoveTicksRemaining>=3&&b->second.renderMoveTicksRemaining<b->second.renderMoveTicksTotal
                    &&d2>=.25&&d2<1.1) {
                    ordinary=checkpoint(s,false);exact=checkpoint(s,true);
                    if (!ordinary||!exact||!compare(s,*ordinary,false)||!compare(s,*exact,true))
                        return fail("actual precontact ordinary/canonical restoration failed: "+error);
                    ordinaryUntil=s.tick+b->second.renderMoveTicksRemaining;
                    if (!findObjectById(*ordinary,2)||findObjectById(*ordinary,2)->subCell!=victim.subCell)
                        return fail("precontact ordinary reconcile evicted alive original infantry slot");
                    saved=true;
                }
            }
            if (!target->alive) { killed=true;break; }
            if (!source->alive) return fail("original BFRT died before contact: "+describe(s,1));
        }
        if (!killed||(save&&!saved)) return fail("original physical contact/checkpoint did not occur: "+describe(s,1)+" / "+describe(s,2));
        ++crushes;
        if(type=="MTNK"&&route==Route::Offline&&order==Order::AltEndpoint){const auto old=cooldownMotion.find({hz,fast});
            if(old!=cooldownMotion.end()&&old->second!=motionTrace)return fail("identical real BFRT command changed precontact physical timeline");cooldownMotion[{hz,fast}]=motionTrace;}
        const auto* targetType=findRuntimeUnitTypeById(s.rules,type);const auto cue=targetType ? trimAsciiLocal(targetType->crushSound) : std::string{};
        if (!cue.empty()) {
            int requests=0;
            for(const auto& sound:s.runtimeSoundPlaybackHistory) if(sound.intent=="sound:"+cue&&sound.tick==s.tick) {
                ++requests;
                if(!sound.playable||!sound.spatial||!crushSourcePosition
                    ||sound.sourcePosition.x!=crushSourcePosition->x||sound.sourcePosition.y!=crushSourcePosition->y
                    ||sound.sourcePosition.level!=crushSourcePosition->level)
                    return fail("original CrushSound did not resolve at actual crusher XYZ: "+cue);
            }
            if (requests!=1) return fail("actual native CrushSound request count differs: "+cue+" count="+std::to_string(requests));
            ++originalSoundChecks;
        }
        // Original death weapons/crew may independently injure the source.
        // We never increase HP to keep it alive, nor call another kill API.
        const int stableTicks=std::max({10,hz/2,ordinaryUntil-s.tick});
        for (int n=0;n<stableTicks;++n) {
            const auto* oldSource=findRuntimeObjectByIdIncludingInactive(s,1); const std::optional<Pose> oldPose=oldSource?std::optional<Pose>{pose(*oldSource)}:std::nullopt;
            step(s); const auto* newSource=findRuntimeObjectByIdIncludingInactive(s,1);
            if(oldPose&&newSource&&newSource->alive&&oldPose->alive&&std::hypot(runtimeObjectRenderX(*newSource)-oldPose->x,runtimeObjectRenderY(*newSource)-oldPose->y)>1.25*std::floor(fortress->speed*256.0/100.0)*15.0/(256.0*hz)+2.0/16777216.0) return fail("post-contact remaining segment snapped instead of continuing original Speed");
            if (exact) { step(*exact);if (!compare(s,*exact,true)) return false; }
            if (ordinary&&s.tick<=ordinaryUntil) { step(*ordinary);if (!compare(s,*ordinary,false)) return false; }
            const auto* target=findRuntimeObjectByIdIncludingInactive(s,2);
            if (!target||target->alive||findObjectById(s,2)||crushEvents(s,2).size()!=1)
                return fail("crushed original target resurrected or received duplicate crush");
            ++stableChecks;
        }
        return true;
    };
    // These categories and IDs are independently transcribed from the retail
    // 5F6CD0 predicate and original registered lists (native_review section 4).
    // They are not derived by calling runtimeCrusherCanCrushObject.
    const auto words=[](const char* text) {std::istringstream in(text);std::vector<std::string> out;std::string word;
        while(in>>word)out.push_back(word);return out;};
    const auto groundVehicles=words("HARV APOC HTNK CAR BUS WINI PICK MTNK HORV TRUCKA TRUCKB V3 DRON HTK TNKD HOWI TTNK LTNK CMON CMIN SREF XCOMET MGTK FV DTRUCK PROPA CONA COP EUROC LIMO STANG SUVB SUVW TAXI PTRUCK SMON YCAB YTNK TELE CAOS DDBX BCAB JEEP MIND UTNK ROBO DOLY CBLC FTRK AMBU CIVP");
    const auto groundInfantry=words("E1 E2 SHK ENGINEER GHOST YURI IVAN DESO DOG CIV1 CIV2 CIV3 CTECH WEEDGUY CLEG SPY CCOMAND PTROOP CIVAN YURIPR SNIPE COW ALL TANY FLAKT TERROR SENGINEER ADOG VLADIMIR PENTGEN PRES SSRV CIVA CIVB CIVC CIVBBP CIVBFM CIVBF CIVBTM CIVSFM CIVSF CIVSTM POLARB JOSH YENGINEER GGI INIT BORIS BRUTE VIRUS CLNT ARND STLN CAML EINS MUMY RMNV SLAV WWLF YDOG YADOG");
    const std::set<std::string> resistant{"DNOA","BFRT","AMCV","SMCV","PCV","SMIN"};
    const auto* registeredVehicles=originalRules.getSection("VehicleTypes");const auto* registeredInfantry=originalRules.getSection("InfantryTypes");
    if(groundVehicles.size()!=51||groundInfantry.size()!=61||!registeredVehicles||!registeredInfantry||registeredVehicles->size()!=80||registeredInfantry->size()!=65)
        return fail("actual original registered/type-qualification counts differ from verified native list");
    const auto table=[&](const char* category,const std::vector<std::string>& ids,const char* scope,bool eligible) {
        for(const auto& id:ids) {
            const auto* type=findRuntimeUnitTypeById(rules,id);
            if(!type||type->omniCrushResistant==eligible) return fail("original type qualification/resistance changed: "+id);
            if(qualification)qualification<<category<<','<<id<<",1,"<<type->omniCrushResistant<<','<<type->crushable<<','<<eligible<<','<<scope<<'\n';
        }return true;
    };
    if(!table("ground-vehicle",groundVehicles,"real-Alt-contact-30Hz",true)
        ||!table("ground-infantry",groundInfantry,"real-Alt-contact-30Hz",true)
        ||!table("amphibious-navals",words("SAPC LCRF YHVR"),"eligible-type-land-representative",true)
        ||!table("surface-water",words("CARRIER DEST AEGIS DRED HYD VLAD CRUISE TUG CDEST"),"eligible-type-BFRT-deep-water-unreachable",true)
        ||!table("submerged",words("SUB SQD DLPH BSUB"),"eligible-type-current-offground-excluded",true)
        ||!table("jumpjet-vehicle",words("ZEP SHAD HIND SCHP DISK SCHD"),"eligible-type-air-state-excluded-landed-SHAD-physical",true)
        ||!table("resistant",words("DNOA BFRT AMCV SMCV PCV SMIN"),"real-negative",false)
        ||!table("fly-infantry",words("JUMPJET LUNR"),"eligible-type-air-state-excluded",true)
        ||!table("water-infantry",words("DNOB"),"eligible-type-BFRT-deep-water-unreachable",true)) return false;
    if(qualification)qualification<<"incomplete,DeathDummy,1,0,0,not-claimed,not-physical\nincomplete,YDUM,1,0,0,not-claimed,not-physical\n";
    for(const auto& type:groundVehicles)scene("native-vehicle-"+type+"-30",[&]{const bool okay=positive(type,30,Route::Offline,Order::AltEndpoint,false,false,false);if(okay)++nativeVehicles;return okay;});
    for(const auto& type:groundInfantry)scene("native-infantry-"+type+"-30",[&]{const bool okay=positive(type,30,Route::Offline,Order::AltEndpoint,false,false,false);if(okay)++nativeInfantry;return okay;});
    for(const auto& type:words("BRUTE TANY GGI MTNK DRON LCRF ROBO SHAD"))
        scene("representative-"+type+"-60",[&]{return positive(type,60,Route::Offline,Order::AltEndpoint,false,false,type=="GGI");});
    for(int hz:{30,60}) {
        for(Route route:{Route::Direct,Route::Offline,Route::Captured})for(Order order:{Order::AltEndpoint,Order::Through,Order::Attack}) {
            const std::string routeName=route==Route::Direct?"direct":route==Route::Offline?"offline":"captured-receive";
            const std::string orderName=order==Order::AltEndpoint?"Alt":order==Order::Through?"through":"Attack";
            scene(routeName+'-'+orderName+'-'+std::to_string(hz),[&]{return positive("MTNK",hz,route,order,false,false,false);});
        }
        scene("NoCooldown-Alt-"+std::to_string(hz),[&]{return positive("MTNK",hz,Route::Offline,Order::AltEndpoint,true,false,false);});
        scene("saved-E1-Attack-"+std::to_string(hz),[&]{return positive("E1",hz,Route::Offline,Order::Attack,false,true,false);});
    }
    const auto negative=[&](const std::string& type,bool friendly,bool allied,bool flying,bool mixed) {
        std::vector<Spawn> spawns{{1,"BFRT",{76,80},localOwner},{2,type,{80,80},friendly?localOwner:hostileOwner,
            runtimeBuildKindForUnitTypeId(rules,type)==RuntimeBuildKind::Infantry?2:-1}};
        if(mixed)spawns.push_back({3,"DNOA",{80,80},hostileOwner,4});
        auto storage=make(30,false,spawns,false,std::string{},allied);if(!storage)return false;auto& s=*storage;Input input{s};if(!open(input,Route::Offline))return false;
        RuntimeSnapshotActiveStateScope active(s);
        if(allied){s.playerCommands.alliances[{"skirmishplayer1","skirmishplayer2"}]=true;
            s.playerCommands.alliances[{"skirmishplayer2","skirmishplayer1"}]=true;
            if(!runtimeOwnersAreAllied(s,localOwner,hostileOwner))return fail("actual mutual alliance fixture did not apply");}
        const auto* victim=findObjectById(s,2);if(!victim)return fail("negative original target unavailable");
        const int health=victim->health;
        if(flying&&(!victim->airborne||runtimeObjectRenderLevel(*victim)<=terrainLevelAtRuntimeCell(s,victim->x,victim->y)))
            return fail("original flying target did not initialize its genuine air state");
        applySelection(s,{1},false,false);const auto pixel=cellPoint(s,{80,80},false);
        if(!pixel||!click(input,*pixel,true))return fail("negative occupied cell has no actual Alt click");
        stage="protected/noneligible actual terrain endpoint and complete ticks";
        for(int n=0;n<30*12;++n){step(s);
            const auto* current=findObjectById(s,2);
            if(!current||!crushEvents(s,2).empty()||(mixed&&!crushEvents(s,3).empty()))
                return fail("native excluded target was crushed/removed: "+describe(s,2));
            ++negativeChecks;
            const auto behavior=s.behaviors.find(1);const auto* source=findObjectById(s,1);
            if(!source)return fail("negative source unexpectedly disappeared");
            // The original planner may reject or choose a neighboring legal
            // destination. Stop inspecting before post-arrival Guard combat.
            if(behavior==s.behaviors.end()||behavior->second.kind!=RuntimeBehaviorKind::Move
                ||(!source->renderMoveActive&&(behavior->second.state==RuntimeBehaviorState::Complete
                    ||behavior->second.state==RuntimeBehaviorState::Blocked)))break;
        }
        victim=findObjectById(s,2);
        if(!victim||victim->health!=health||!crushEvents(s,2).empty())return fail("excluded target lost original HP or crush immunity");
        return true;
    };
    for(const auto& type:resistant)scene("resistant-"+type,[&]{return negative(type,false,false,false,false);});
    scene("friendly-MTNK",[&]{return negative("MTNK",true,false,false,false);});
    scene("allied-MTNK",[&]{return negative("MTNK",false,true,false,false);});
    scene("airborne-SHAD",[&]{return negative("SHAD",false,false,true,false);});
    scene("building-GAPOWR",[&]{return negative("GAPOWR",false,false,false,false);});
    scene("mixed-E1-DNOA",[&]{return negative("E1",false,false,false,true);});

    // Ordinary Crusher deliberately keeps its existing precommit contract.
    // Its original Crushable predicate differs from Omni physical contact;
    // the Fortress half-cell oracle must not be applied to these controls.
    const auto ordinaryCrusher=[&](int hz,const std::string& type,bool eligible) {
        const auto* tank=rules.findUnitType("UnitType","APOC");const auto* victimRule=findRuntimeUnitTypeById(rules,type);
        if(!tank||!tank->crusher||tank->omniCrusher||tank->opportunityFire||!victimRule||victimRule->crushable!=eligible)
            return fail("original ordinary Crusher/Crushable control flags changed");
        const bool infantry=runtimeBuildKindForUnitTypeId(rules,type)==RuntimeBuildKind::Infantry;
        auto storage=make(hz,false,{{1,"APOC",{76,80},localOwner},{2,type,{80,80},hostileOwner,infantry?2:-1}});
        if(!storage)return false;auto& s=*storage;Input input{s};if(!open(input,Route::Offline))return false;
        RuntimeSnapshotActiveStateScope active(s);const auto hp=findObjectById(s,2)->health;
        if(hp!=victimRule->strength)return fail("ordinary Crusher target original HP changed");
        if(!command(input,{1},2,{82,80},Order::Through))return false;
        if(!crushEvents(s,2).empty()||!findObjectById(s,2))return fail("ordinary Crusher killed during mouse dispatch");
        bool ended=false;
        for(int n=0;n<hz*25;++n){step(s);const auto* source=findObjectById(s,1);const auto* victim=findRuntimeObjectByIdIncludingInactive(s,2);
            if(!source||!victim)return fail("ordinary Crusher control identity disappeared");
            const auto events=crushEvents(s,2);const auto a=physical(*source),b=physical(*victim);
            if(csv)csv<<label<<",ordinary-crusher,"<<hz<<','<<s.tick<<",1,2,"<<type<<','<<source->x<<','<<source->y<<','
                <<a.first<<','<<a.second<<','<<b.first<<','<<b.second<<','<<(a.first-b.first)*(a.first-b.first)+(a.second-b.second)*(a.second-b.second)<<','
                <<victim->alive<<','<<victim->health<<','<<victim->subCell<<','<<pose(*source).bridge<<','<<(events.empty()?0:events.back().sequence)<<'\n';
            if(eligible&&!events.empty()) {
                if(events.size()!=1||events.front().objectId!=1||events.front().reason!="Crush"||events.front().renderIntentId!="death:crush"
                    ||events.front().tick!=s.tick||victim->alive)
                    return fail("ordinary standing E1 lacks original unique Crush cause");
                ++ordinaryCrusherChecks;ended=true;break;
            }
            if(!victim->alive||victim->health!=hp||!events.empty())return fail("ordinary Crusher changed an excluded target: "+describe(s,2));
            const auto behavior=s.behaviors.find(1);
            if(behavior==s.behaviors.end()||(!source->renderMoveActive&&(behavior->second.state==RuntimeBehaviorState::Complete
                ||behavior->second.state==RuntimeBehaviorState::Blocked))) {
                if(eligible)return fail("ordinary Crusher failed to crush original standing E1");
                ++ordinaryCrusherChecks;ended=true;break;
            }
        }
        if(!ended)return fail("ordinary Crusher control did not finish original Move");
        return true;
    };
    for(int hz:{30,60})for(const auto& type:words("E1 TANY MTNK"))
        scene("ordinary-APOC-"+type+'-'+std::to_string(hz),[&]{return ordinaryCrusher(hz,type,type=="E1");});
    const auto largeGroup=[&](int hz) {
        std::vector<Spawn> spawns;
        for(int i=0;i<12;++i)spawns.push_back({10+i,"BFRT",{72+i%3,75+i/3},localOwner});
        spawns.push_back({2,"MTNK",{80,80},hostileOwner});
        auto storage=make(hz,false,spawns);if(!storage)return false;auto& s=*storage;Input input{s};if(!open(input,Route::Captured))return false;
        std::vector<int> ids;for(int id=10;id<22;++id)ids.push_back(id);
        if(!command(input,ids,2,{80,80},Order::AltEndpoint))return false;
        if(input.captured.empty())return fail("large group emitted no canonical payload");
        const auto* payload=std::get_if<RuntimeMultiplayerGameplayTargetOrderCommand>(&input.captured.back().body);
        if(!payload||payload->objectIds.size()!=12||payload->order!=RuntimeMultiplayerGameplayTargetOrderKind::Move)
            return fail("large group did not retain actual Move selected IDs");
        bool killed=false;
        for(int n=0;n<hz*40;++n){const auto* victim=findObjectById(s,2);if(!victim)return fail("large group victim lost before death cause");
            const auto before=*victim;step(s);const auto events=crushEvents(s,2);
            if(events.empty()){if(!findObjectById(s,2)||findObjectById(s,2)->health!=before.health)return fail("large group remotely killed eligible victim");continue;}
            if(events.size()!=1||std::find(ids.begin(),ids.end(),events.front().objectId)==ids.end())return fail("large group cause/source is not unique");
            const auto* source=findRuntimeObjectByIdIncludingInactive(s,events.front().objectId);const auto a=source?physical(*source):std::pair<double,double>{-1000,-1000};const auto b=physical(before);
            if(!source||(a.first-b.first)*(a.first-b.first)+(a.second-b.second)*(a.second-b.second)>=.25)
                return fail("large group killed before actual strict half-cell contact");
            ++largeGroupChecks;killed=true;break;
        }
        if(!killed)return fail("large group>=12 rejected/avoided the original occupied endpoint");
        return true;
    };
    for(int hz:{30,60})scene("large-12-Alt-"+std::to_string(hz),[&]{return largeGroup(hz);});

    const auto stockProtection=[&](const std::string& power,const std::string& type) {
        auto storage=make(30,true,{{1,"BFRT",{76,80},localOwner},{2,type,{80,80},power=="ForceShieldSpecial"?localOwner:hostileOwner},
            {5,power=="IronCurtainSpecial"?"NAIRON":"GATECH",{90,90},localOwner}});
        if(!storage)return false;auto& s=*storage;Input input{s};if(!open(input,Route::Offline))return false;
        RuntimeSnapshotActiveStateScope active(s);ensureRuntimeSuperWeaponsInitialized(s);
        bool ready=false;
        for(int n=0;n<30*5;++n){step(s);const auto* item=s.superWeapons.findSidebarSnapshot(localOwner,power);
            if(item&&item->state==SuperWeaponRuntimeState::Ready){ready=true;break;}}
        if(!ready)return fail("original NoCooldown source never naturally made real sidebar power Ready: "+power);
        // ForceShield only protects allied structures in the real system. Use
        // a real own building here; do not manufacture a shielded vehicle.
        s.activeBuildTab=RuntimeBuildKind::Defense;const RECT sidebar=runtimeSidebarRect(client);std::optional<POINT> button;
        for(int page=0;page<runtimeBuildPageCount(s,sidebar)&&!button;++page){runtimeSetActiveBuildPage(s,page);
            const int offset=static_cast<int>(runtimeVisibleBuildCatalogIndices(s,sidebar).size());
            const auto visible=runtimeVisibleSuperWeaponsOnBuildPage(s,sidebar);
            for(std::size_t i=0;i<visible.size();++i)if(visible[i]->superWeaponId==power){const auto r=runtimeBuildButtonRect(sidebar,offset+static_cast<int>(i));button=POINT{(r.left+r.right)/2,(r.top+r.bottom)/2};break;}}
        if(!button||!click(input,*button))return fail("real protection sidebar button unavailable");
        const auto target=cellPoint(s,{80,80},false);if(!target||!click(input,*target))return fail("real protection target pixel unavailable");
        bool protectedNow=false;for(int n=0;n<30*2;++n){step(s);if(s.superWeapons.isObjectProtected(2,s.tick)){protectedNow=true;break;}}
        if(!protectedNow)return fail("actual original sidebar cast did not protect its valid target");
        applySelection(s,{1},false,false);const auto pixel=cellPoint(s,{80,80},false);if(!pixel||!click(input,*pixel,true))return false;
        const auto hp=findObjectById(s,2)->health;
        for(int n=0;n<30*6;++n){step(s);
            if(!s.superWeapons.isObjectProtected(2,s.tick)||!findObjectById(s,2)||findObjectById(s,2)->health!=hp||!crushEvents(s,2).empty())
                return fail("native live protection failed crush gate: "+describe(s,2));++negativeChecks;}
        return true;
    };
    scene("real-IronCurtain-MTNK",[&]{return stockProtection("IronCurtainSpecial","MTNK");});
    scene("real-ForceShield-building",[&]{return stockProtection("ForceShieldSpecial","GAPOWR");});

    const auto movingVictim=[&]() {
        const auto* victimRule=rules.findUnitType("UnitType","MTNK");
        if(!victimRule||victimRule->strength<=0)return fail("original MTNK moving-victim rule/HP unavailable");
        const int nativeHealth=victimRule->strength;
        auto storage=make(30,false,{{1,"BFRT",{76,80},localOwner},{2,"MTNK",{80,80},hostileOwner,-1,false,runtimeFacingFromDelta(0,-1)}});
        if(!storage)return false;auto& s=*storage;
        if(s.config.playerSlots.size()!=2)return fail("moving victim requires the original two-house fixture");
        s.config.playerSlots[1].human=true;s.config.playerSlots[1].local=false;
        if(s.config.computerAiEnabled||runtimeComputerAiPausedForOwner(s,hostileOwner))
            return fail("manually controlled hostile human was paused as an AI actor");
        Input input{s};if(!open(input,Route::Offline)||!command(input,{1},2,{80,80},Order::AltEndpoint))return false;
        bool ordered=false,logicallyLeft=false,killed=false;int movedFrames=0;
        for(int n=0;n<30*20;++n){const auto* source=findObjectById(s,1);const auto* victim=findObjectById(s,2);
            if(!source||!victim)return fail("moving victim disappeared without contact record");
            const auto sourcePoint=physical(*source),targetPoint=physical(*victim);
            if(!ordered&&std::hypot(sourcePoint.first-targetPoint.first,sourcePoint.second-targetPoint.second)<.62){
                const auto oldOwner=s.economy.owner;s.economy.owner=hostileOwner;
                RuntimeHumanHouseAuthority house;house.owner=hostileOwner;house.canonicalOwner="skirmishplayer2";house.country="Russians";house.localFacade=true;s.humanHouseAuthorities.push_back(house);
                applySelection(s,{2},false,false);const auto pixel=cellPoint(s,{80,79},true);
                const bool accepted=pixel&&click(input,*pixel);s.economy.owner=oldOwner;s.humanHouseAuthorities.pop_back();
                const auto b=s.behaviors.find(2);
                if(!accepted||b==s.behaviors.end()||b->second.kind!=RuntimeBehaviorKind::Move||b->second.moveTarget!=RuntimeCellPoint{80,79})
                    return fail("actual enemy facade Move did not commit original north goal");ordered=true;
            }
            const auto beforeTarget=*findObjectById(s,2);std::optional<RuntimeObjectBehavior> segment;
            if(const auto b=s.behaviors.find(2);b!=s.behaviors.end())segment=b->second;
            const auto expected=nextPoint(beforeTarget,segment);step(s);
            const auto* current=findRuntimeObjectByIdIncludingInactive(s,2);source=findObjectById(s,1);
            if(!current||!source)return fail("moving contact source/target identity missing");
            if(current->alive){if(current->cell!=runtimeCellKey(80,80))logicallyLeft=true;
                if(physical(*current)!=physical(beforeTarget))++movedFrames;
                if(ordered&&segment&&segment->renderMoveTicksRemaining>0&&physical(*current)!=expected)
                    return fail("independent committed victim next-frame motion differs");
                if(!crushEvents(s,2).empty()||current->health!=nativeHealth)return fail("moving target remotely killed/injured");
                const auto a=physical(*source),b=physical(*current);
                if((a.first-b.first)*(a.first-b.first)+(a.second-b.second)*(a.second-b.second)<.25)
                    return fail("moving eligible victim survived the first actual strict physical touch");
            }else{
                const auto events=crushEvents(s,2);const auto actual=physical(*source);
                const double d2=(actual.first-expected.first)*(actual.first-expected.first)+(actual.second-expected.second)*(actual.second-expected.second);
                if(!ordered||!logicallyLeft||movedFrames==0||events.size()!=1||events.front().objectId!=1||!(d2<.25))
                    return fail("moving target lost logical cell/body contact proof d2="+std::to_string(d2)
                        +" ordered="+std::to_string(ordered)+" logicallyLeft="+std::to_string(logicallyLeft)
                        +" movedFrames="+std::to_string(movedFrames)+" victim=["+describe(s,2)+']');
                if(csv)csv<<label<<",moving-victim,30,"<<s.tick<<",1,2,MTNK,"<<source->x<<','<<source->y<<','<<actual.first<<','<<actual.second<<','
                    <<expected.first<<','<<expected.second<<','<<d2<<",0,"<<current->health<<','<<current->subCell<<','<<pose(*source).bridge<<','<<events.front().sequence<<'\n';
                ++contactChecks;killed=true;break;
            }
        }
        return killed?true:fail("true moving victim never reached strict physical contact");
    };
    scene("real-moving-victim-30",movingVictim);
    const auto stoppedApproach=[&](int hz) {
        auto storage=make(hz,false,{{1,"BFRT",{76,80},localOwner},{2,"E1",{80,80},hostileOwner,2}});
        if(!storage)return false;auto& s=*storage;Input input{s};if(!open(input,Route::Offline)||!command(input,{1},2,{80,80},Order::Attack))return false;
        bool stopped=false;
        for(int n=0;n<hz*20;++n){step(s);const auto* source=findObjectById(s,1);const auto* target=findObjectById(s,2);
            if(!source||!target)return fail("S fixture killed before deliberately distant stop");
            const auto a=physical(*source),b=physical(*target);const double distance=std::hypot(a.first-b.first,a.second-b.second);
            if(source->renderMoveActive&&distance>=.7&&distance<.85){const auto before=pose(*source);key(input,'S',{1});source=findObjectById(s,1);
                if(!source||runtimeObjectRenderX(*source)!=before.x||runtimeObjectRenderY(*source)!=before.y||source->renderMoveActive||!crushEvents(s,2).empty())
                    return fail("real S snapped continuous body or crushed without physical contact");
                step(s);source=findObjectById(s,1);target=findObjectById(s,2);
                if(!source||!target||runtimeObjectRenderX(*source)!=before.x||runtimeObjectRenderY(*source)!=before.y||target->health!=rules.findUnitType("InfantryType","E1")->strength||!crushEvents(s,2).empty())
                    return fail("first complete stopped tick remotely crushed/moved original body");
                ++negativeChecks;stopped=true;break;}
        }
        return stopped?true:fail("no actual precontact moving segment to stop");
    };
    for(int hz:{30,60})scene("real-S-before-contact-"+std::to_string(hz),[&]{return stoppedApproach(hz);});

    const auto containedCargo=[&]() {
        auto storage=make(30,false,{{1,"BFRT",{76,80},localOwner},{2,"HTK",{80,80},hostileOwner},{3,"E1",{80,79},hostileOwner,2}});
        if(!storage)return false;auto& s=*storage;
        if(s.config.playerSlots.size()!=2)return fail("real boarding requires the original two-house fixture");
        s.config.playerSlots[1].human=true;s.config.playerSlots[1].local=false;
        if(s.config.computerAiEnabled||runtimeComputerAiPausedForOwner(s,hostileOwner))
            return fail("manually controlled hostile passenger was paused as an AI actor");
        Input input{s};if(!open(input,Route::Offline))return false;
        RuntimeSnapshotActiveStateScope active(s);const auto oldOwner=s.economy.owner;s.economy.owner=hostileOwner;
        RuntimeHumanHouseAuthority house;house.owner=hostileOwner;house.canonicalOwner="skirmishplayer2";house.country="Russians";house.localFacade=true;s.humanHouseAuthorities.push_back(house);
        applySelection(s,{3},false,false);const auto hostPixel=objectPoint(s,2);const bool clicked=hostPixel&&click(input,*hostPixel);
        bool boarded=false;
        if(clicked)for(int n=0;n<30*15;++n){step(s);const auto* passenger=findRuntimeObjectByIdIncludingInactive(s,3);const auto* host=findObjectById(s,2);
            if(passenger&&host&&!passenger->alive&&passenger->mission=="InsideTransport"&&std::find(host->transportPassengerIds.begin(),host->transportPassengerIds.end(),3)!=host->transportPassengerIds.end()){boarded=true;break;}}
        s.economy.owner=oldOwner;s.humanHouseAuthorities.pop_back();
        if(!boarded)return fail("original E1 did not genuinely board enemy HTK through WM");
        if(!command(input,{1},2,{80,80},Order::AltEndpoint))return false;
        bool hostCrushed=false;
        for(int n=0;n<30*25;++n){const auto* passenger=findRuntimeObjectByIdIncludingInactive(s,3);const auto* host=findObjectById(s,2);
            if(!host)return fail("cargo host lost without event");const auto before=*host;step(s);
            const auto events=crushEvents(s,2);
            if(events.empty()){
                passenger=findRuntimeObjectByIdIncludingInactive(s,3);
                if(!passenger||passenger->alive||passenger->mission!="InsideTransport"||passenger->health!=rules.findUnitType("InfantryType","E1")->strength||!crushEvents(s,3).empty())return fail("contained original E1 was treated as independent ground victim");
                ++negativeChecks;
            }else{const auto* source=findObjectById(s,1);const auto a=source?physical(*source):std::pair<double,double>{-1000,-1000},b=physical(before);
                if(!source||events.size()!=1||events.front().objectId!=1||(a.first-b.first)*(a.first-b.first)+(a.second-b.second)*(a.second-b.second)>=.25)
                    return fail("actual HTK/cargo cause lacks real contact");hostCrushed=true;break;}
        }
        return hostCrushed?true:fail("actual loaded transport never reached contact");
    };
    scene("real-contained-HTK-E1",containedCargo);
    scene("real-docked-ASW",[&]() {
        auto storage=make(30,false,{{1,"BFRT",{79,80},localOwner},{2,"DEST",{84,80},hostileOwner}},true);
        if(!storage)return false;auto& s=*storage;Input input{s};if(!open(input,Route::Offline))return false;
        int child=-1;for(int n=0;n<30*2&&child<0;++n){step(s);for(const auto& object:s.world.getObjects())
            if(object.alive&&object.type=="ASW"&&object.dockedToObjectId==2){child=object.instanceId;break;}}
        if(child<0)return fail("real original DEST did not create docked ASW");
        applySelection(s,{1},false,false);const auto point=cellPoint(s,{84,80},false);if(!point||!click(input,*point,true))return fail("actual docked target cell input unavailable");
        for(int n=0;n<30*2;++n){step(s);const auto* object=findObjectById(s,child);
            if(!object||object->dockedToObjectId!=2||!crushEvents(s,child).empty())return fail("real docked aircraft was exposed to ground crush");++negativeChecks;}
        return true;
    });
    const auto fallingVictim=[&]() {
        auto storage=make(30,false,{{1,"BFRT",{83,80},localOwner},{2,"MTNK",{84,80},hostileOwner},{3,"TELE",{76,80},localOwner}});
        if(!storage)return false;auto& s=*storage;Input input{s};if(!open(input,Route::Offline))return false;
        if(!command(input,{3},2,{84,80},Order::Attack))return false;
        bool carried=false;
        for(int n=0;n<30*12;++n){step(s);const auto* binding=runtimeMagBeamBindingForTarget(s,2);
            if(binding&&binding->phase==RuntimeMagBeamPhase::Carried&&binding->kinematics.velocityZ==0){carried=true;break;}}
        if(!carried)return fail("real TELE did not lift/carry original target");
        // While the real beam keeps the victim off-ground, physically drive
        // the Fortress into its XY neighborhood. Only then release the beam;
        // a distant no-kill frame is not evidence of the off-ground gate.
        const auto* carriedTarget=findObjectById(s,2);if(!carriedTarget)return fail("real carried target missing");
        applySelection(s,{1},false,false);const auto carriedPixel=cellPoint(s,{carriedTarget->x,carriedTarget->y},false);
        if(!carriedPixel||!click(input,*carriedPixel,true))return fail("actual Move toward carried target was unavailable");
        bool xyTouch=false;
        for(int n=0;n<30*12;++n){step(s);const auto* binding=runtimeMagBeamBindingForTarget(s,2);const auto* source=findObjectById(s,1);const auto* target=findObjectById(s,2);
            if(!binding||binding->phase!=RuntimeMagBeamPhase::Carried||!source||!target||!crushEvents(s,2).empty())return fail("real carried target lost before XY gate could be observed");
            const auto a=physical(*source),b=physical(*target);if((a.first-b.first)*(a.first-b.first)+(a.second-b.second)*(a.second-b.second)<.25){xyTouch=true;++negativeChecks;key(input,'S',{1});break;}}
        if(!xyTouch)return fail("actual carried target never touched Fortress XY for off-ground test");
        key(input,'S',{3});bool falling=false;
        for(int n=0;n<3;++n){step(s);const auto* binding=runtimeMagBeamBindingForTarget(s,2);
            if(binding&&binding->phase==RuntimeMagBeamPhase::Falling&&binding->kinematics.worldZ>binding->kinematics.location.level){falling=true;break;}}
        if(!falling)return fail("real release did not produce magnetic falling state");
        const auto* target=findObjectById(s,2);const auto originalHP=target->health;
        const RuntimeCellPoint goal{target->x,target->y};applySelection(s,{1},false,false);const auto point=cellPoint(s,goal,false);
        if(!point||!click(input,*point,true))return fail("real falling victim terrain target unavailable");
        int fallingTicks=0,touchingFallTicks=0;
        for(int n=0;n<30*5;++n){const auto* before=runtimeMagBeamBindingForTarget(s,2);
            if(!before||before->phase!=RuntimeMagBeamPhase::Falling)break;
            step(s);const auto* after=runtimeMagBeamBindingForTarget(s,2);
            // Landing damage is independently covered by issue22. Only frames
            // still genuinely falling are negative physical-crush evidence.
            if(after&&after->phase==RuntimeMagBeamPhase::Falling&&after->kinematics.worldZ>after->kinematics.location.level){
                const auto* victim=findObjectById(s,2);if(!victim||victim->health!=originalHP||!crushEvents(s,2).empty())return fail("actual off-ground magnetic victim was prematurely crushed");
                const auto* source=findObjectById(s,1);if(!source)return fail("falling XY source missing");const auto a=physical(*source),b=physical(*victim);
                if((a.first-b.first)*(a.first-b.first)+(a.second-b.second)*(a.second-b.second)<.25)++touchingFallTicks;
                ++fallingTicks;++negativeChecks;}
        }
        return fallingTicks>0&&touchingFallTicks>0?true:fail("no actual offground falling tick inside strict XY contact observed");
    };
    scene("real-magnetic-falling-victim",fallingVictim);

    const auto realBridge=[&]() {
        const auto path=std::filesystem::absolute("local_data/resources/mix_exports/multimd/2peaks.map").generic_string();
        auto preview=make(30,false,{},false,path);if(!preview)return false;
        std::optional<std::pair<RuntimeCellPoint,RuntimeCellPoint>> corridor;
        for(const auto& cell:preview->world.getTerrainCells()) {
            if(!runtimeCellIsBridge(*preview,cell.x,cell.y))continue;
            const MapQuery query(preview->mapFacts);const auto* fact=query.getCell(cell.x,cell.y);
            if(!fact||!fact->isBridge)continue;
            for(const auto direction:std::array<RuntimeCellPoint,4>{{{1,0},{-1,0},{0,1},{0,-1}}}) {
                bool valid=true;for(int i=0;i<4&&valid;++i){const int x=cell.x+i*direction.x,y=cell.y+i*direction.y;
                    if(!runtimeCellIsBridge(*preview,x,y)||terrainLevelAtRuntimeCell(*preview,x,y)!=terrainLevelAtRuntimeCell(*preview,cell.x,cell.y))valid=false;}
                if(valid){corridor=std::pair<RuntimeCellPoint,RuntimeCellPoint>{{cell.x,cell.y},{cell.x+3*direction.x,cell.y+3*direction.y}};break;}
            }
            if(corridor)break;
        }
        if(!corridor)return fail("actual original 2peaks map has no four-cell true bridge corridor");
        auto storage=make(30,false,{{1,"BFRT",corridor->first,localOwner},{2,"E1",corridor->second,hostileOwner,2}},false,path);
        if(!storage)return false;auto& s=*storage;Input input{s};if(!open(input,Route::Offline))return false;
        const auto* source=findObjectById(s,1);const auto* target=findObjectById(s,2);
        if(!source||!target||!source->onBridge||!target->onBridge
            ||!runtimeCellIsBridge(s,source->x,source->y)||!runtimeCellIsBridge(s,target->x,target->y))
            return fail("actual parsed bridge deck layer not preserved");
        const auto hp=target->health;applySelection(s,{1},false,false);const auto point=cellPoint(s,corridor->second,false);
        if(!point||!click(input,*point,true))return fail("actual original bridge cell cannot receive Alt mouse order");
        bool killed=false;
        for(int n=0;n<30*25;++n){const auto* before=findObjectById(s,2);if(!before)return fail("bridge victim missing before cause");
            const auto prior=*before;step(s);source=findObjectById(s,1);target=findRuntimeObjectByIdIncludingInactive(s,2);
            if(!source||!target)return fail("bridge identities lost");const auto events=crushEvents(s,2);
            if(!events.empty()){const auto a=physical(*source),b=physical(prior);const double d2=(a.first-b.first)*(a.first-b.first)+(a.second-b.second)*(a.second-b.second);
                if(events.size()!=1||events.front().objectId!=1||target->alive||!pose(*source).bridge||!pose(prior).bridge||!(d2<.25))return fail("actual deck contact failed strict native same-layer formula");
                ++contactChecks;killed=true;break;
            }else if(!target->alive||target->health!=hp)return fail("bridge target remotely killed before physical crush");
        }
        return killed?true:fail("actual original deck victim never physically crushed");
    };
    scene("original-bridge-same-layer",realBridge);
    // Current startup navigation promotes a Naval object authored on a bridge
    // cell to its deck. Do not force flags after startup to fabricate an
    // under-deck physical test. Mixed-layer exclusion is native/static evidence.
    if(qualification)qualification<<"bridge-under-deck,DEST,1,0,0,native-static-only,startup-navigation-promotes-to-deck\n";
    if(filter.empty())for(int hz:{30,60}) {
        const auto normal=cooldownMotion.find({hz,false}),fast=cooldownMotion.find({hz,true});
        if(normal==cooldownMotion.end()||fast==cooldownMotion.end()||normal->second!=fast->second)
            failures.push_back("NoCooldown changed actual BFRT precontact physical timeline at "+std::to_string(hz)+" Hz");
    }
    if(!failures.empty()) {
        std::ostringstream message;message<<"Battle Fortress contact failed "<<failures.size()<<" scene(s)";
        for(const auto& failure:failures)message<<'\n'<<failure;error=message.str();return false;
    }
    if(scenes==0)return fail("requested case matched no actual scenario");
    if(filter.empty()&&(nativeVehicles!=51||nativeInfantry!=61||ordinarySaves!=2||exactSaves!=2||largeGroupChecks!=2||ordinaryCrusherChecks!=6))
        return fail("full physical sweep/save/group coverage is incomplete");
    std::cout<<"Battle Fortress contact passed "<<scenes<<" actual scenes: original ground types="<<nativeVehicles<<" vehicles/"<<nativeInfantry
        <<" infantry, fullTicks="<<fullTicks<<", actual WM clicks="<<clicks<<", Alt="<<altClicks<<", beforeContact="<<beforeContactChecks
        <<", contact="<<contactChecks<<", positive deaths="<<crushes<<", exclusions="<<negativeChecks<<", stable="<<stableChecks
        <<", original resolved CrushSound="<<originalSoundChecks<<", real deployed="<<deployedChecks<<", real landed="<<landedChecks
        <<", ordinary Crusher controls="<<ordinaryCrusherChecks<<", 12-unit endpoints="<<largeGroupChecks<<", authenticated captured commands="<<capturedCommands<<", codec="<<decodedCommands
        <<", ordinary/canonical extension="<<ordinarySaves<<'/'<<exactSaves<<", paired="<<pairedTicks<<", exact authority="<<hashChecks<<'\n';
    std::cout<<"Evidence scope: native 80/65 type table is distinct from physical ground sweep; deep-water/air/dummy type categories are not claimed as reachable ground kills. "
        <<"ForceShield is tested on its original allied Structure target, not a fabricated shielded tank; audio checks resolve original CrushSound resources with playback disabled. "
        <<"Opposite bridge-layer exclusion remains native/static evidence because startup navigation promotes authored Naval bridge occupants to the deck; it is not claimed as a physical under-deck scene. "
        <<"Canonical continuation is an ordinary v14 base plus exact extension, not a portable V15 envelope.\n";
    error.clear();return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
