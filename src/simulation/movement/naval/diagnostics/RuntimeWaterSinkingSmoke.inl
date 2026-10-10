// Question collection 24, issue 3: native deep-water arrivals and retained sinking.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeWaterSinkingSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    const char* configured = std::getenv("CONCORD_WATER_SINKING_OUTPUT");
    const std::filesystem::path root = std::filesystem::absolute(configured && *configured
        ? configured : "build/smoke/feature-set-24/03-water-sinking/checks").lexically_normal();
    std::filesystem::create_directories(root);
    const char* selected = std::getenv("CONCORD_WATER_SINKING_CASE");
    const std::string filter = selected ? selected : "";
    std::string label, stage;
    const auto fail = [&](const std::string& why) { error = "Water sinking " + label + " [" + stage + "]: " + why; return false; };
    std::ofstream cases(root / "cases.csv"), poses(root / "poses.csv"), saves(root / "saves.csv");
    cases << "case,hz,route,death_count,survivors,ordinary14,canonical,paired_ticks,native,result\n";
    poses << std::setprecision(17) << "case,branch,stage,tick,id,type,alive,hp,x,y,z,bridge,airborne,tombstone,wrecks,sinking,wreck_age,water_animations,wreck_hash\n";
    saves << "case,phase,format,tick,wreck_hash,death_hash,result\n";
    IniFile raw, rawArt; RulesDatabase original; ArtDatabase originalArt;
    if (!live.rulesLoaded || !live.artLoaded || !live.theaterLoaded
        || !raw.loadFromFile(live.report.rulesPath) || !original.loadFromIni(raw)
        || !rawArt.loadFromFile(live.report.artPath) || !originalArt.loadFromIni(rawArt)) return fail("stock assets unavailable");
    const auto rulesHash = original.getDetailRulesHash();
    const auto* tele = findRuntimeUnitTypeById(original, "TELE");
    const auto* beam = original.findWeaponType("MagneticBeam");
    if (!tele || !beam || tele->primaryWeapon != "MagneticBeam" || beam->damage != 5000
        || beam->range != 12 || beam->minimumRange != 3 || !beam->magBeam
        || raw.getValue("Water", "Track", "") != "0%" || raw.getValue("Water", "Wheel", "") != "0%"
        || raw.getValue("Water", "Hover", "") != "100%") return fail("stock beam / water rules differ");
    constexpr int mapSize = 80, nativeCells = 12720, width = 1280, screenHeight = 800;
    constexpr const char* owner = "SkirmishPlayer1";
    const RECT client{0, 0, width, screenHeight}, viewport = runtimeMapViewport(client);
    int scenes = 0, fullTicks = 0, clicks = 0, keys = 0, ordinaryCount = 0, canonicalCount = 0, pairedTicks = 0, nativeCount = 0;
    int totalDeaths = 0, totalSurvivors = 0;
    int landTile = -1; std::optional<TerrainCell> water;
    auto metadataCache = std::make_unique<RuntimeMapRenderCache>(); const auto metadata = runtimeTerrainMetadataContext(live);
    for (int tile = 0; tile < static_cast<int>(live.theater.getTotalTileSlots()); ++tile) {
        TerrainCell c; c.tileIndex = tile; c.subTileIndex = 0; const auto info = runtimeTerrainInfoForCell(live, c);
        if (info.passable && !info.water && !info.bridge && !info.shore && !info.cliff
            && resolveRuntimeTerrainSubTileRampType(c, metadata, *metadataCache) == 0) { landTile = tile; break; }
    }
    for (const auto& set : live.theater.getTileSets()) {
        if (toLowerAsciiLocal(set.fileName + " " + set.setName).find("water") == std::string::npos) continue;
        for (int tile = set.startTileIndex; tile < set.startTileIndex + set.tilesInSet && !water; ++tile)
            for (int sub = 0; sub < 16 && !water; ++sub) {
                TerrainCell c; c.tileIndex = tile; c.subTileIndex = sub;
                if (resolveRuntimeTerrainSubTileLandType(c, metadata, *metadataCache) == RuntimeTerrainLandType::Water
                    && resolveRuntimeTerrainSubTileRampType(c, metadata, *metadataCache) == 0) water = c;
            }
        if (water) break;
    }
    if (landTile < 0 || !water) return fail("decoded original clear/deep-water TMP unavailable");
    const auto& overlays = original.getOverlayTypes();
    int lowBridge = -1; for (std::size_t i = 0; i < overlays.size(); ++i) if (overlays[i].id == "LOBRDG01") lowBridge = static_cast<int>(i);
    if (lowBridge != 74) return fail("stock native low-bridge ordinal differs");
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
    const auto packOverlay = [&](const Bytes& bytes) {
        Bytes packed;
        for (std::size_t at = 0; at < bytes.size(); at += 8192) {
            const auto count = std::min<std::size_t>(8192, bytes.size() - at); Bytes chunk;
            for (std::size_t offset = 0; offset < count;) {
                std::size_t n = 1; while (offset + n < count && bytes[at + offset + n] == bytes[at + offset]) ++n;
                chunk.push_back(0xfe); append16(chunk, static_cast<unsigned int>(n)); chunk.push_back(bytes[at + offset]); offset += n;
            }
            chunk.push_back(0x80); append16(packed, static_cast<unsigned int>(chunk.size())); append16(packed, static_cast<unsigned int>(count));
            packed.insert(packed.end(), chunk.begin(), chunk.end());
        }
        return packed;
    };
    std::filesystem::create_directories(root / "fixture-maps"); std::array<std::string, 2> mapPaths;
    for (int variant = 0; variant < 2; ++variant) {
        Bytes tiles, overlay(512 * 512, 255), frames(512 * 512, 0);
        for (int y = 1; y < 2 * mapSize; ++y) for (int x = 1; x < 2 * mapSize; ++x) {
            const int u = x - y + mapSize, v = x + y - mapSize;
            if (u < 0 || u >= 2 * mapSize || v < 1 || v >= 2 * mapSize) continue;
            const bool wet = x >= 82 && x <= 88 && y >= 76 && y <= 85;
            append16(tiles, x); append16(tiles, y); append32(tiles, wet ? water->tileIndex : landTile);
            tiles.insert(tiles.end(), {static_cast<unsigned char>(wet ? water->subTileIndex : 0), 0, 0});
            if (variant && x >= 82 && x <= 88 && y >= 80 && y <= 82) {
                const int ordinal = lowBridge + (x == 82 ? 20 : x == 88 ? 18 : 0);
                overlay[y * 512 + x] = static_cast<unsigned char>(ordinal); frames[y * 512 + x] = static_cast<unsigned char>(y - 80);
            }
        }
        if (tiles.size() != nativeCells * 11) return fail("complete native diamond count differs"); append32(tiles, 0);
        std::ostringstream map; map.imbue(std::locale::classic());
        map << "[Basic]\nName=WaterSinking-" << variant << "\nMultiplayerOnly=yes\nNewINIFormat=4\n"
            << "[Header]\nStartX=0\nStartY=0\nWidth=160\nHeight=160\nNumberStartingPoints=0\n"
            << "[Map]\nTheater=" << live.report.map.theater << "\nSize=0,0,80,80\nLocalSize=0,0,80,80\n"
            << "[SpecialFlags]\nTiberiumGrows=no\nTiberiumSpreads=no\n[Lighting]\n" << std::setprecision(17)
            << "Ambient=" << live.mapFacts.lighting.base.ambient << "\nRed=" << live.mapFacts.lighting.base.red
            << "\nGreen=" << live.mapFacts.lighting.base.green << "\nBlue=" << live.mapFacts.lighting.base.blue
            << "\nGround=" << live.mapFacts.lighting.base.ground << "\nLevel=" << live.mapFacts.lighting.base.level << '\n';
        const auto section = [&](const char* name, const Bytes& bytes) {
            map << '[' << name << "]\n"; const auto encoded = base64(bytes);
            for (std::size_t at = 0; at < encoded.size(); at += 70) map << at / 70 + 1 << '=' << encoded.substr(at, 70) << '\n';
        };
        section("IsoMapPack5", packTiles(tiles));
        if (variant) { section("OverlayPack", packOverlay(overlay)); section("OverlayDataPack", packOverlay(frames)); }
        mapPaths[variant] = (root / "fixture-maps" / (variant ? "native-water-bridge80.map" : "native-water80.map")).generic_string();
        std::ofstream file(mapPaths[variant], std::ios::binary); file << map.str(); file.close(); if (!file) return fail("native fixture write");
    }
    const auto add = [&](RuntimeWindowState& s, int id, const std::string& type, WorldObjectKind kind,
                         RuntimeCellPoint p, const std::string& house = "SkirmishPlayer1") {
        WorldObjectInstance o; o.instanceId = id; o.mapId = "WaterSinking" + std::to_string(id); o.type = type; o.kind = kind; o.owner = house;
        o.x = p.x; o.y = p.y; o.cell = runtimeCellKey(p.x, p.y); o.level = terrainLevelAtRuntimeCell(s, p.x, p.y);
        o.onBridge = runtimeCellIsBridge(s, p.x, p.y); o.alive = o.poweredOperational = true; o.mission = "Sleep";
        o.health = runtimeMaxHealthForObject(s, o); o.facing = o.turretFacing = runtimeFacingFromDelta(1, 0);
        initializeRuntimeObjectAmmo(o, runtimeAmmoRulesForObject(s.rules, o)); applyRuntimeInitialObjectFlightState(s.rules, o, true);
        runtimeInitializeModExtensions(o, s.rules, s.config.fixedTickHz); s.world.accessObjects().push_back(std::move(o));
    };
    const auto make = [&](int hz, bool bridge, bool magnetic) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box; seedRuntimeLoadScratch(s, live, false);
        s.config = live.config; s.tick = 0; s.config.fixedTickHz = hz; s.config.defaultMapPath = mapPaths[bridge];
        s.config.playerCountry = magnetic ? "YuriCountry" : "Americans"; s.config.gameMode = "Battle";
        s.config.multiplayerActive = s.config.computerAiEnabled = s.config.shortGame = s.config.crates = s.config.noCooldown = false;
        s.config.superweapons = true; s.config.startingUnits = 0; s.config.techLevel = 10;
        s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.config.keyBindings.clear(); s.config.keyboardReferenceLayout = false; s.campaignMode = false;
        s.skirmishMatchFinished = s.skirmishVictory = false; s.skirmishResult.clear();
        if (!s.rules.loadFromIni(raw) || !s.art.loadFromIni(rawArt)) { fail("fixture owner-scoped original rules/art"); return std::unique_ptr<RuntimeWindowState>{}; }
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; IniFile ini; MapDatabase map;
        if (!ini.loadFromFile(s.config.defaultMapPath) || !map.loadFromIni(ini, &raw) || !s.world.loadFromMap(map, &s.rules)) {
            fail("native fixture parse/world hydration"); return std::unique_ptr<RuntimeWindowState>{};
        }
        const auto& summary = map.getSummary(); s.report.map.loaded = true; s.report.map.path = s.config.defaultMapPath;
        s.report.map.name = summary.name; s.report.map.theater = summary.theater; s.report.map.size = summary.size; s.report.map.localSize = summary.localSize;
        s.report.map.width = summary.width; s.report.map.height = summary.height; s.report.map.isoTiles = static_cast<int>(map.getIsoTiles().size());
        s.report.map.overlayCells = static_cast<int>(map.getOverlayCells().size()); s.report.map.structures = s.report.map.units = s.report.map.infantry = s.report.map.triggers = 0;
        s.report.world = s.world.getSummary(); s.mapFacts = map.buildMapFacts(&s.theater); s.mapFactsLoaded = true;
        if (!s.world.getSummary().loaded || !s.world.getObjects().empty() || s.world.getTerrainCells().size() != nativeCells
            || s.mapFacts.cells.size() != nativeCells || s.mapFacts.terrainCellCount != nativeCells) { fail("actual native terrain identity/domain"); return std::unique_ptr<RuntimeWindowState>{}; }
        const MapQuery query(s.mapFacts);
        for (const auto& c : s.world.getTerrainCells()) {
            const auto* facts = query.getCell(c.x, c.y); const bool wet = c.x >= 82 && c.x <= 88 && c.y >= 76 && c.y <= 85;
            if (!facts || !facts->hasTerrain || c.cellIndex512 != c.y * 512 + c.x || c.level != 0
                || c.tileIndex != (wet ? water->tileIndex : landTile) || c.subTileIndex != (wet ? water->subTileIndex : 0)
                || facts->terrain.tileIndex != c.tileIndex || facts->terrain.subTileIndex != c.subTileIndex || facts->heightLevel != 0) {
                fail("native TMP/facts row differs"); return std::unique_ptr<RuntimeWindowState>{};
            }
        }
        s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = s.visionEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s); initializeRuntimeNavigation(s);
        add(s, 100, magnetic ? "YAPOWR" : "NANRCT", WorldObjectKind::Structure, {104, 104});
        add(s, 101, "GACSPH", WorldObjectKind::Structure, {104, 95}); add(s, 102, "GAROBO", WorldObjectKind::Structure, {104, 85});
        add(s, 998, "NANRCT", WorldObjectKind::Structure, {52, 52}, "SkirmishPlayer2");
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner = owner; s.economy.credits = 20000; s.economy.techLevel = 10; s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        RuntimeConfig::PlayerSlot local; local.enabled = local.human = local.local = true; local.name = "Water sinking local"; local.country = s.config.playerCountry; local.team = "-";
        RuntimeConfig::PlayerSlot hostile; hostile.enabled = true; hostile.country = "Russians"; hostile.team = "-"; s.config.playerSlots = {local, hostile};
        s.skirmishCountryByOwner.clear(); s.skirmishTeamByOwner.clear(); s.playerCommands.alliances.clear();
        s.skirmishCountryByOwner["skirmishplayer1"] = local.country; s.skirmishCountryByOwner["skirmishplayer2"] = "Russians";
        s.humanHouseAuthorities.clear(); RuntimeHumanHouseAuthority house; house.owner = owner; house.canonicalOwner = "skirmishplayer1"; house.country = local.country; house.localFacade = true; s.humanHouseAuthorities.push_back(house);
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s); ensureRuntimeGarrisonsInitialized(s); ensureRuntimeGatesInitialized(s);
        ensureRuntimeRepairDepotsInitialized(s); (void)ensureRuntimeAircraftServicesInitialized(s); ensureRuntimeMenuKeyboardLoaded(s);
        ensureRuntimeSuperWeaponsInitialized(s); initializeRuntimeOfflineGameplayRouting(s); rebuildRuntimeEconomyPower(s); return box;
    };
    const auto step = [&](RuntimeWindowState& s) { RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s); ++fullTicks; };
    struct Input {
        RuntimeWindowState& state; HWND window = nullptr; WNDPROC original = nullptr;
        ~Input() {
            state.multiplayerIntentRouter.setCaptureCallback({}); state.multiplayerBridge = nullptr;
            if (window) {
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                if (original) SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
                if (GetCapture() == window) ReleaseCapture();
                releaseRuntimeBackBuffer(state.backBuffer); DestroyWindow(window);
            }
        }
    };
    const auto openInput = [&](Input& input) {
        auto& s = input.state;
        input.window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"Real Magnetron release orders",
            WS_POPUP, -16000, -16000, width, screenHeight, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!input.window) return fail("hidden Win32 input window unavailable");
        SetWindowLongPtrW(input.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));
        input.original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(input.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(runtimeWindowProc)));
        HDC screen = GetDC(nullptr); const bool dib = screen && ensureRuntimeBackBuffer(s.backBuffer, screen, width, screenHeight);
        if (screen) ReleaseDC(nullptr, screen);
        return input.original && dib && s.world.getSummary().loaded && runtimeUsesGameplayIntentRouter(s)
            ? true : fail("Win32 procedure/DIB/loaded world/offline authority unavailable");
    };
    const auto objectPoint = [&](RuntimeWindowState& s, int id) -> std::optional<POINT> {
        RuntimeSnapshotActiveStateScope active(s); const auto* o = findObjectById(s, id); if (!o) return std::nullopt;
        centerRuntimeCameraOnCell(s, client, runtimeCellPointForObject(*o)); ensureRuntimeCameraForViewport(s, viewport);
        invalidateRuntimeObjectProjectionCache(s); const auto& projected = runtimeProjectedObjectsForViewport(s, viewport);
        const auto hit = [&](POINT p) { const auto* entry = findProjectedObjectAtPoint(projected, p);
            return pointInRectInclusive(viewport, p) && entry && entry->object && entry->object->instanceId == id; };
        for (const auto& candidate : projected) if (candidate.object && candidate.object->instanceId == id) {
            for (const auto p : candidate.footprintHitCenters) if (hit(p)) return p;
            const POINT midpoint{(candidate.bounds.left + candidate.bounds.right) / 2, (candidate.bounds.top + candidate.bounds.bottom) / 2};
            if (hit(midpoint)) return midpoint;
            for (int y = candidate.bounds.top; y < candidate.bounds.bottom; y += 2)
                for (int x = candidate.bounds.left; x < candidate.bounds.right; x += 2) if (hit({x, y})) return POINT{x, y};
        }
        return std::nullopt;
    };
    const auto click = [&](Input& input, POINT p) {
        RuntimeSnapshotActiveStateScope active(input.state);
        SendMessageW(input.window, WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
        SendMessageW(input.window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(p.x, p.y));
        SendMessageW(input.window, WM_LBUTTONUP, 0, MAKELPARAM(p.x, p.y)); ++clicks;
    };
    const auto stop = [&](Input& input) {
        RuntimeSnapshotActiveStateScope active(input.state); applySelection(input.state, {1}, false, false);
        SendMessageW(input.window, WM_KEYDOWN, 'S', LPARAM(1));
        SendMessageW(input.window, WM_KEYUP, 'S', LPARAM(1) | (LPARAM(1) << 30) | (LPARAM(1) << 31)); ++keys;
    };
    const auto groundPoint = [&](RuntimeWindowState& s, RuntimeCellPoint cell) -> std::optional<POINT> {
        RuntimeSnapshotActiveStateScope active(s); centerRuntimeCameraOnCell(s, client, cell); ensureRuntimeCameraForViewport(s, viewport);
        const int effectiveLevel = terrainLevelAtRuntimeCell(s, cell.x, cell.y);
        const auto terrain = s.terrainByCell.find(runtimeCellKey(cell.x, cell.y));
        const int groundLevel = terrain != s.terrainByCell.end() && terrain->second ? terrain->second->level : effectiveLevel;
        // High bridge decks are four levels above the tile; low bridge TMP
        // anchors can also extend beyond the ground diamond. Only an actual
        // native visible surface belonging to the requested cell is clickable.
        const std::array<int, 3> levels{{effectiveLevel, groundLevel, groundLevel + 4}};
        for (int level : levels) {
            const auto initial = runtimeScreenPointForCell(s, viewport, cell, level);
            const auto picked = approximateCellFromScreenPoint(s, viewport, initial);
            if (pointInRectInclusive(viewport, initial) && picked.x == cell.x && picked.y == cell.y) return initial;
        }
        const auto& layer = s.renderCache.terrainLayer; const int nativeIndex = cell.y * 512 + cell.x;
        std::optional<POINT> closest; long long best = std::numeric_limits<long long>::max();
        // Read ownership to nominate a real pixel rather than invoking the
        // expensive full native picker for every empty search pixel. The final
        // candidate still passes the identical gameplay screen picker below.
        for (int y = viewport.top; y < viewport.bottom; ++y) for (int x = viewport.left; x < viewport.right; ++x) {
            const int lx = x - viewport.left + s.camera.worldOffsetX - layer.originX;
            const int ly = y - viewport.top + s.camera.worldOffsetY - layer.originY;
            if (lx < 0 || ly < 0 || lx >= layer.width || ly >= layer.height) continue;
            const auto offset = static_cast<std::size_t>(ly) * layer.width + lx;
            if (offset >= layer.surfaceCellIndices.size() || layer.surfaceCellIndices[offset] != nativeIndex) continue;
            long long score = std::numeric_limits<long long>::max();
            for (int level : levels) {
                const auto center = runtimeScreenPointForCell(s, viewport, cell, level);
                const long long dx = x - center.x, dy = y - center.y; score = std::min(score, dx * dx + dy * dy);
            }
            if (score < best) { best = score; closest = POINT{x, y}; }
        }
        if (closest) { const auto picked = approximateCellFromScreenPoint(s, viewport, *closest);
            if (picked.x == cell.x && picked.y == cell.y) {
                std::cout << "Water sinking native pick " << label << " cell=" << cell.x << ',' << cell.y
                    << " level=" << effectiveLevel << " pixel=" << closest->x << ',' << closest->y << "\n";
                return closest;
            }
        }
        std::cout << "Water sinking native pick absent " << label << " cell=" << cell.x << ',' << cell.y
            << " ground/effective=" << groundLevel << '/' << effectiveLevel << " layer=" << layer.width << 'x' << layer.height << "\n";
        return std::nullopt;
    };
    const auto chronoButton = [&](RuntimeWindowState& s) -> std::optional<POINT> {
        s.activeBuildTab = RuntimeBuildKind::Defense; const auto sidebar = runtimeSidebarRect(client);
        for (int page = 0; page < runtimeBuildPageCount(s, sidebar); ++page) {
            runtimeSetActiveBuildPage(s, page); const int offset = static_cast<int>(runtimeVisibleBuildCatalogIndices(s, sidebar).size());
            const auto visible = runtimeVisibleSuperWeaponsOnBuildPage(s, sidebar);
            for (std::size_t n = 0; n < visible.size(); ++n) if (visible[n]->superWeaponId == "ChronoSphereSpecial") {
                const auto r = runtimeBuildButtonRect(sidebar, offset + static_cast<int>(n)); return POINT{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
            }
        }
        return std::nullopt;
    };
    const auto nativeWater = [&](const RuntimeWindowState& s, RuntimeCellPoint p) {
        const auto found = s.terrainByCell.find(runtimeCellKey(p.x, p.y));
        return found != s.terrainByCell.end() && found->second
            && found->second->tileIndex == water->tileIndex && found->second->subTileIndex == water->subTileIndex;
    };
    const auto wreckFor = [](const RuntimeWindowState& s, int id) -> const RuntimeWreckState* {
        for (const auto& w : s.wrecks) if (w.object.instanceId == id) return &w; return nullptr;
    };
    const auto waterEffects = [](const RuntimeWindowState& s) {
        return static_cast<int>(std::count_if(s.impactEffects.begin(), s.impactEffects.end(), [](const auto& e) {
            return e.lifecycleState == "WreckWaterEntry" || e.lifecycleState == "WreckWake" || e.lifecycleState == "WreckWaterImpact";
        }));
    };
    const auto wreckHash = [](const RuntimeWindowState& s) { std::uint64_t hash = runtimeStableHashString("WaterSinkingSmoke"); hashRuntimeWrecks(hash, s); return hash; };
    const auto signature = [&](const RuntimeWindowState& s, const std::vector<int>& ids) {
        std::ostringstream out; out << std::setprecision(17) << s.tick << '|' << s.magBeams.stateHash() << '|' << s.deathWeapons.stateHash() << '|' << wreckHash(s);
        for (int id : ids) {
            const auto* o = findRuntimeObjectByIdIncludingInactive(s, id); out << "\nobject=" << id;
            if (!o) { out << "missing"; continue; }
            out << '|' << o->type << '|' << runtimeCanonicalHumanHouseOwner(o->owner) << '|' << o->alive << '|' << o->health
                << '|' << o->x << '|' << o->y << '|' << o->level << '|' << o->onBridge << '|' << o->airborne
                << '|' << runtimeObjectRenderX(*o) << '|' << runtimeObjectRenderY(*o) << '|' << runtimeObjectRenderLevel(*o) << '|' << o->mission;
        }
        for (const auto& e : s.impactEffects) if (e.lifecycleState == "WreckWaterEntry" || e.lifecycleState == "WreckWake" || e.lifecycleState == "WreckWaterImpact")
            out << "\nwater=" << runtimeCanonicalHumanHouseOwner(e.owner) << '|' << e.animId << '|' << e.renderIntent << '|' << e.lifecycleState << '|' << e.death
                << '|' << e.hasWorldPosition << '|' << e.worldX << '|' << e.worldY << '|' << e.worldLevel
                << '|' << e.cell.x << '|' << e.cell.y << '|' << e.level << '|' << e.totalTicks << '|' << e.remainingTicks
                << '|' << e.animationFrameCount << '|' << e.animationFrameDelayTicks << '|' << e.frameIndex
                << '|' << e.animationStartFrame << '|' << e.animationLoopStartFrame << '|' << e.animationLoopEndFrame << '|' << e.animationLoopCount
                << '|' << e.animationEndFrame << '|' << e.animationSequenceSteps << '|' << e.warheadImpactIntentId << '|' << e.facing << '|' << e.radius << '|' << e.bright;
        return out.str();
    };
    const auto sample = [&](const RuntimeWindowState& s, const std::vector<int>& ids, const char* branch, const char* phase) {
        for (int id : ids) {
            const auto* o = findRuntimeObjectByIdIncludingInactive(s, id); if (!o) continue; const auto* w = wreckFor(s, id);
            poses << label << ',' << branch << ',' << phase << ',' << s.tick << ',' << id << ',' << o->type << ',' << o->alive << ',' << o->health
                << ',' << runtimeObjectRenderX(*o) << ',' << runtimeObjectRenderY(*o) << ',' << runtimeObjectRenderLevel(*o)
                << ',' << o->onBridge << ',' << o->airborne << ',' << s.deathWeapons.hasTriggered(id) << ',' << s.wrecks.size()
                << ',' << (w && w->sinking) << ',' << (w ? runtimeWreckAgeFrames(*w, s.tick, s.config.fixedTickHz) : -1)
                << ',' << waterEffects(s) << ',' << wreckHash(s) << '\n';
        }
    };
    const auto checkpoint = [&](RuntimeWindowState& s, const std::vector<int>& ids, const char* phase, bool exact) {
        RuntimeSnapshotActiveStateScope active(s); std::ostringstream ordinary, complete, normalized; IniFile base, snapshot;
        if (!writeRuntimeStateToStream(s, ordinary, error) || !base.loadFromString(ordinary.str())) return std::unique_ptr<RuntimeWindowState>{};
        if (exact && (!writeRuntimeStateToStream(s, complete, error) || !writeRuntimeMultiplayerSnapshotExtension(s, complete, error)
            || !snapshot.loadFromString(complete.str()) || !writeRuntimeMultiplayerSnapshotCanonicalIni(snapshot, normalized, error)
            || !snapshot.loadFromString(normalized.str()))) return std::unique_ptr<RuntimeWindowState>{};
        std::ofstream file(root / (label + '-' + phase + (exact ? "-canonical.ini" : "-ordinary14.ini")), std::ios::binary);
        file << (exact ? normalized.str() : ordinary.str()); file.close(); if (!file) { fail("checkpoint evidence write"); return std::unique_ptr<RuntimeWindowState>{}; }
        auto other = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*other, s, false);
        other->wrecks.clear(); other->impactEffects.clear();
        { RuntimeSnapshotActiveStateScope restore(*other);
            if (!materializeRuntimeLoadStateFromIni(*other, base, error) || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*other, snapshot, error))) return std::unique_ptr<RuntimeWindowState>{};
        }
        initializeRuntimeOfflineGameplayRouting(*other);
        if (signature(s, ids) != signature(*other, ids)) { fail("checkpoint changed target authority / wreck / water animation timeline"); return std::unique_ptr<RuntimeWindowState>{}; }
        saves << label << ',' << phase << ',' << (exact ? "canonical-extension" : "ordinary14") << ',' << s.tick << ',' << wreckHash(s) << ',' << s.deathWeapons.stateHash() << ",PASS\n";
        if (exact) ++canonicalCount; else ++ordinaryCount; return other;
    };
    const auto finishSinking = [&](RuntimeWindowState& s, const std::vector<int>& ids, const std::vector<int>& sinkingIds, bool render,
                                   std::unique_ptr<RuntimeWindowState> ordinary = {}, std::unique_ptr<RuntimeWindowState> canonical = {}) {
        stage = "entry / continued native sinking";
        for (int id : sinkingIds) {
            const auto* o = findRuntimeObjectByIdIncludingInactive(s, id); const auto* w = wreckFor(s, id);
            if (!o || o->alive || o->health != 0 || !s.deathWeapons.hasTriggered(id) || !w || !w->sinking
                || w->object.type != o->type || w->object.owner != o->owner || static_cast<int>(std::lround(w->x)) != o->x
                || static_cast<int>(std::lround(w->y)) != o->y || w->object.onBridge || !nativeWater(s, {o->x, o->y}) || s.behaviors.contains(id)
                || std::find(s.selectedObjectIds.begin(), s.selectedObjectIds.end(), id) != s.selectedObjectIds.end()) return fail("death did not retain an isolated target-position sinking body");
        }
        if (render) {
            const auto* nativeObject = findRuntimeObjectByIdIncludingInactive(s, sinkingIds.front());
            const auto* nativeType = nativeObject ? findRuntimeUnitTypeById(s.rules, nativeObject->type) : nullptr;
            if (!nativeType) return fail("native sinking representative original unit type missing");
            const bool requireEntrySplash = !nativeType->naval;
            if (!runRuntimeWaterSinkingRenderProbe(s, sinkingIds.front(), error, label, requireEntrySplash)) return fail("native render: " + error); ++nativeCount;
        }
        // Check both formats at a natural entry tick, after complete simulation.
        ordinary = checkpoint(s, ids, "entry", false); canonical = checkpoint(s, ids, "entry", true);
        if (!ordinary || !canonical) return fail("entry full restore: " + error);
        const auto initialTick = s.tick; bool midSaved = false; std::map<int, int> expiryTick;
        // A native body sinks through 400 leptons in 80 authored frames.
        // Its last WAKE1 can start at frame 80; original 15 shapes at
        // Rate=120 consume 105 more frames (7 seconds). Allow the complete
        // 12.333-second body-plus-tail sequence and a finite endpoint margin.
        const int expiryBudgetTicks = s.config.fixedTickHz * 14;
        const auto paused = signature(s, ids); completeRuntimeWorldRefresh(s); completeRuntimeWorldRefresh(s);
        if (paused != signature(s, ids)) return fail("same-tick refresh advanced sink authority or animation");
        const auto repeatedHash = s.deathWeapons.stateHash(), repeatedWreck = wreckHash(s); const int repeatedEffects = waterEffects(s);
        for (int id : sinkingIds) { RuntimeObjectExitRequest again; again.objectId = id; again.tick = s.tick; again.reason = "water sink repeat guard"; (void)queueRuntimeObjectExit(s, again); }
        (void)drainRuntimeObjectExitQueue(s);
        if (repeatedHash != s.deathWeapons.stateHash() || repeatedWreck != wreckHash(s) || repeatedEffects != waterEffects(s)) return fail("repeated death restarted sinking/water entry");
        for (int t = 0; t < expiryBudgetTicks; ++t) {
            sample(s, ids, "original", "sinking"); step(s); step(*ordinary); step(*canonical);
            for (auto* other : {ordinary.get(), canonical.get()}) {
                if (signature(s, ids) != signature(*other, ids)) return fail("continued ordinary/canonical water death timeline diverged"); ++pairedTicks;
            }
            for (int id : sinkingIds) if (!wreckFor(s, id) && !expiryTick.contains(id)) expiryTick[id] = s.tick;
            if (!midSaved && s.tick - initialTick >= std::max(1, s.config.fixedTickHz / 2)) {
                stage = "mid-sinking full restore"; ordinary = checkpoint(s, ids, "middle", false); canonical = checkpoint(s, ids, "middle", true);
                if (!ordinary || !canonical) return fail("middle restore: " + error); midSaved = true; stage = "continued native sinking";
            }
            if (expiryTick.size() == sinkingIds.size() && waterEffects(s) == 0 && t >= s.config.fixedTickHz) break;
            if (t + 1 == expiryBudgetTicks) return fail("finite sink or authored water animation did not expire");
        }
        if (!midSaved || expiryTick.size() != sinkingIds.size()) return fail("natural mid-sink/expiry stages absent");
        for (int id : sinkingIds) {
            const auto* o = findRuntimeObjectByIdIncludingInactive(s, id);
            if (!o || o->alive || o->health != 0 || !s.deathWeapons.hasTriggered(id) || wreckFor(s, id)) return fail("sink expiry resurrected or retained dead body");
        }
        sample(s, ids, "original", "expired"); return true;
    };
    const auto prepareActors = [&](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); initializeRuntimeNavigation(s); ensureRuntimeSubcellsInitialized(s);
        rebuildRuntimeEconomyPower(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s);
    };
    const auto castChrono = [&](RuntimeWindowState& s, Input& input, RuntimeCellPoint from, RuntimeCellPoint to) {
        stage = "actual Chronosphere sidebar / two area targets";
        int captured = 0, accepted = 0; RuntimeMultiplayerGameplayFireSuperWeaponCommand body;
        s.multiplayerIntentRouter.setCaptureCallback([&](const auto& command, std::string& reason) {
            const auto* fire = std::get_if<RuntimeMultiplayerGameplayFireSuperWeaponCommand>(&command.body);
            if (fire) { ++captured; body = *fire; }
            const bool ok = dispatchRuntimeOfflineGameplayCommand(s, command, reason); if (fire && ok) ++accepted; return ok;
        });
        auto context = runtimeSuperWeaponContext(s); context.emit = runtimeSuperWeaponEventSink(s); s.superWeapons.tick(context);
        if (!s.superWeapons.forceReady(owner, "ChronoSphereSpecial", &context)) return fail("stock Chronosphere fixture readiness");
        step(s); const auto button = chronoButton(s); if (!button) return fail("stock Chronosphere sidebar icon absent");
        click(input, *button); const auto& targeting = s.multiplayerIntentRouter.superWeapon();
        if (!targeting || targeting->superWeaponId != "ChronoSphereSpecial" || targeting->expectedTargetCount != 2 || !targeting->targets.empty())
            return fail("real sidebar did not begin two area targets");
        auto first = groundPoint(s, from); if (!first) return fail("real first native terrain pixel absent");
        const auto beforeFirst = s.superWeapons.stateHash(); click(input, *first);
        const auto& selected = s.multiplayerIntentRouter.superWeapon();
        if (!selected || selected->targets.size() != 1 || !s.localChronosphereMarker || captured || accepted
            || s.localChronosphereMarker->animation.cell.x != from.x || s.localChronosphereMarker->animation.cell.y != from.y
            || s.superWeapons.stateHash() != beforeFirst) return fail("first area click prematurely launched/consumed charge");
        auto second = groundPoint(s, to); if (!second) return fail("real second native terrain pixel absent"); click(input, *second);
        s.multiplayerIntentRouter.setCaptureCallback({});
        if (captured != 1 || accepted != 1 || body.superWeaponId != "ChronoSphereSpecial" || body.targets.size() != 2
            || body.targets[0].cell.x != from.x || body.targets[0].cell.y != from.y
            || body.targets[1].cell.x != to.x || body.targets[1].cell.y != to.y
            || s.multiplayerIntentRouter.superWeapon() || s.localChronosphereMarker
            || s.superWeapons.stateOf(owner, "ChronoSphereSpecial") != SuperWeaponRuntimeState::Launching)
            return fail("real two-click area cast did not accept exactly one stock launch");
        step(s); return true;
    };
    const auto magneticRelease = [&](RuntimeWindowState& s, Input& input, bool drown) {
        stage = "actual MagneticBeam attack / above-water Stop release";
        const auto* target = findObjectById(s, 2); const int hp = target ? target->health : 0;
        if (!target || hp != runtimeMaxHealthForObject(s, *target)) return fail("original target maximum HP");
        if (drown && (nativeWater(s, {target->x, target->y}) || !runtimeMapQueryPassabilityAllows(s, {target->x, target->y}, *target)))
            return fail("land victim did not start on original legal land");
        if (!drown && (!nativeWater(s, {target->x, target->y}) || !runtimeMapQueryPassabilityAllows(s, {target->x, target->y}, *target)))
            return fail("water survivor did not start on original legal water");
        const auto point = objectPoint(s, 2); if (!point) return fail("original magnetic victim has no real mouse hit");
        applySelection(s, {1}, false, false);
        if (runtimeTargetCursorIntent(s, *target) != RuntimeTargetCursorIntent::Attack) return fail("original attack cursor unavailable");
        click(input, *point); const auto attack = s.behaviors.find(1);
        if (attack == s.behaviors.end() || attack->second.kind != RuntimeBehaviorKind::Attack || attack->second.targetObjectId != 2)
            return fail("WM Primary did not create actual magnetic Attack");
        bool held = false;
        for (int t = 0; t < s.config.fixedTickHz * 12; ++t) {
            step(s); target = findObjectById(s, 2); const auto* b = runtimeMagBeamBindingForTarget(s, 2);
            if (!target || target->health != hp) return fail("capture damaged/removed original victim before landing");
            if (!b) continue; const double height = b->kinematics.worldZ - b->kinematics.location.level;
            if (b->sourceObjectId != 1 || b->weaponId != "MagneticBeam" || !target->hasRenderPosition
                || target->renderX != b->kinematics.worldX || target->renderY != b->kinematics.worldY || target->renderLevel != b->kinematics.worldZ)
                return fail("original magnetic pose/binding differs");
            if (drown ? b->phase == RuntimeMagBeamPhase::Carried && b->kinematics.location.water && !b->kinematics.location.onBridge
                : b->phase == RuntimeMagBeamPhase::Lifting && height >= 0.4 && height < 1.2) { held = true; break; }
        }
        const auto* before = runtimeMagBeamBindingForTarget(s, 2); if (!held || !before) return fail("real lift/pull did not reach intended release terrain");
        const auto release = before->kinematics; const int commandTick = s.tick; stop(input);
        if (const auto order = s.behaviors.find(1); order != s.behaviors.end() && order->second.kind == RuntimeBehaviorKind::Attack)
            return fail("actual Stop retained beam Attack");
        for (int t = 0; t < 3; ++t) { const auto* b = runtimeMagBeamBindingForTarget(s, 2);
            if (b && b->phase == RuntimeMagBeamPhase::Falling) break; step(s); }
        const auto* falling = runtimeMagBeamBindingForTarget(s, 2);
        if (!falling || falling->phase != RuntimeMagBeamPhase::Falling || falling->phaseChangedTick < commandTick || falling->phaseChangedTick > commandTick + 1
            || falling->kinematics.worldX != release.worldX || falling->kinematics.worldY != release.worldY || falling->kinematics.worldZ != release.worldZ)
            return fail("Stop release snapped pose or failed the native Falling transition");
        std::unique_ptr<RuntimeWindowState> ordinary, canonical;
        if (drown) { ordinary = checkpoint(s, {2}, "falling", false); canonical = checkpoint(s, {2}, "falling", true);
            if (!ordinary || !canonical) return fail("falling authority restore: " + error); }
        bool contacted = false; double previousZ = release.worldZ;
        for (int t = 0; t < s.config.fixedTickHz * 8; ++t) {
            sample(s, {2}, "original", "falling"); step(s); if (ordinary) step(*ordinary); if (canonical) step(*canonical);
            for (auto* other : {ordinary.get(), canonical.get()}) if (other) {
                if (signature(s, {2}) != signature(*other, {2})) return fail("restored real falling/water-contact authority differs"); ++pairedTicks;
            }
            target = findRuntimeObjectByIdIncludingInactive(s, 2); if (!target) return fail("fall erased target identity");
            const auto* b = runtimeMagBeamBindingForTarget(s, 2);
            if (!b) { contacted = true; break; }
            if (b->phase != RuntimeMagBeamPhase::Falling || b->kinematics.worldX != release.worldX || b->kinematics.worldY != release.worldY
                || b->kinematics.worldZ > previousZ || !target->alive || target->health != hp) return fail("airborne fall moved horizontally or died above water");
            previousZ = b->kinematics.worldZ;
        }
        if (!contacted || !target || !nativeWater(s, {target->x, target->y}) || target->onBridge) return fail("real fall did not contact native deep water");
        if (drown) {
            const auto* w = wreckFor(s, 2);
            if (target->alive || target->health != 0 || !w || std::abs(w->x - release.worldX) > 1e-9 || std::abs(w->y - release.worldY) > 1e-9
                || waterEffects(s) < 1) {
                std::ostringstream diagnostic; diagnostic << std::setprecision(17)
                    << "real water contact failed target-position retained entry tick=" << s.tick
                    << " releaseXYZ=" << release.worldX << ',' << release.worldY << ',' << release.worldZ
                    << " targetAliveHP=" << target->alive << '/' << target->health
                    << " targetCellXYZ=" << target->x << ',' << target->y << ',' << target->level
                    << " targetRenderXYZ=" << runtimeObjectRenderX(*target) << ',' << runtimeObjectRenderY(*target) << ',' << runtimeObjectRenderLevel(*target)
                    << " targetRenderAirBridge=" << target->hasRenderPosition << '/' << target->airborne << '/' << target->onBridge
                    << " tombstone=" << s.deathWeapons.hasTriggered(2) << " wreckExists=" << bool(w)
                    << " waterAnimations=" << waterEffects(s)
                    << " splashCount=" << std::count_if(s.impactEffects.begin(), s.impactEffects.end(), [](const auto& e) {
                        return e.lifecycleState == "WreckWaterEntry" && e.animId == "H2O_EXP3";
                    });
                if (w) diagnostic << " wreckXY=" << w->x << ',' << w->y << " wreckSinking=" << w->sinking
                    << " wreckRecord=" << serializeRuntimeWreck(*w);
                sample(s, {2}, "original", "failed-water-contact");
                return fail(diagnostic.str());
            }
            return finishSinking(s, {2}, {2}, true);
        }
        if (!target->alive || target->health != hp || s.deathWeapons.hasTriggered(2) || wreckFor(s, 2)
            || !runtimeMapQueryPassabilityAllows(s, {target->x, target->y}, *target)) return fail("legal naval/amphibious water release killed original survivor");
        for (int t = 0; t < s.config.fixedTickHz; ++t) {
            step(s); target = findObjectById(s, 2);
            if (!target || !target->alive || target->health != hp || s.deathWeapons.hasTriggered(2) || wreckFor(s, 2)) return fail("legal water survivor died during natural continuation");
        }
        return true;
    };
    for (int hz : {15, 30, 60}) for (const std::string route : {"magnetic-land", "chrono-formation", "magnetic-amphibious", "chrono-bridge-hover", "magnetic-naval", "surface-ship-controls"}) {
        label = route + '-' + std::to_string(hz); if (!filter.empty() && label != filter) continue;
        stage = "native map / original actors"; const bool magnetic = route.rfind("magnetic", 0) == 0, bridge = route == "chrono-bridge-hover";
        auto box = make(hz, bridge, magnetic); if (!box) return false; auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
        const int savedOrdinary = ordinaryCount, savedCanonical = canonicalCount, savedPairs = pairedTicks, savedNative = nativeCount;
        int deaths = 0, survivors = 0;
        if (magnetic) {
            add(s, 1, "TELE", WorldObjectKind::Unit, {route == "magnetic-land" ? 80 : 75, 80});
            add(s, 2, route == "magnetic-land" ? "MTNK" : route == "magnetic-amphibious" ? "LCRF" : "DEST", WorldObjectKind::Unit,
                {route == "magnetic-land" ? 90 : 84, 80}, "SkirmishPlayer2");
        } else if (route == "chrono-formation") {
            add(s, 2, "MTNK", WorldObjectKind::Unit, {75, 80}); add(s, 3, "HTNK", WorldObjectKind::Unit, {75, 81}); add(s, 4, "FV", WorldObjectKind::Unit, {76, 80});
        } else if (bridge) {
            add(s, 2, "MTNK", WorldObjectKind::Unit, {75, 80}); add(s, 3, "ROBO", WorldObjectKind::Unit, {75, 82}); add(s, 4, "LCRF", WorldObjectKind::Unit, {74, 82});
        } else {
            add(s, 2, "DEST", WorldObjectKind::Unit, {84, 78}); add(s, 3, "CARRIER", WorldObjectKind::Unit, {86, 84}); add(s, 4, "SUB", WorldObjectKind::Unit, {88, 78});
        }
        prepareActors(s); Input input{s}; if (!openInput(input)) return false;
        if (s.rules.getDetailRulesHash() != rulesHash || s.config.noCooldown || s.config.multiplayerActive || s.config.computerAiEnabled
            || runtimeOwnersAreAllied(s, owner, "SkirmishPlayer2")) return fail("original rules/local opponent authority changed");
        if (magnetic) { if (!magneticRelease(s, input, route == "magnetic-land")) return false;
            if (route == "magnetic-land") deaths = 1; else survivors = 1;
        } else if (route == "chrono-formation") {
            if (!castChrono(s, input, {75, 80}, {84, 80})) return false;
            const std::array<RuntimeCellPoint, 3> destinations{{{84, 80}, {84, 81}, {85, 80}}};
            for (int n = 0; n < 3; ++n) { const int id = n + 2; const auto* o = findRuntimeObjectByIdIncludingInactive(s, id); const auto* w = wreckFor(s, id);
                if (!o || o->alive || o->health != 0 || o->x != destinations[n].x || o->y != destinations[n].y || o->onBridge || o->renderOnBridge
                    || !w || w->x != destinations[n].x || w->y != destinations[n].y || !w->sinking) return fail("two-stage Chrono failed exact per-unit formation water arrival"); }
            if (std::count_if(s.impactEffects.begin(), s.impactEffects.end(), [](const auto& e) { return e.lifecycleState == "WreckWaterEntry" && e.animId == "H2O_EXP3"; }) != 3)
                return fail("Chrono formation did not create one original entry per drowned vehicle");
            if (!finishSinking(s, {2, 3, 4}, {2, 3, 4}, true)) return false; deaths = 3;
        } else if (bridge) {
            const std::array<RuntimeCellPoint, 3> destinations{{{85, 81}, {85, 83}, {84, 83}}};
            stage = "native visible bridge lane / open-water landing diagnostics";
            // A complete stock bridge has three authored overlay lanes. Target
            // the middle lane proven visible by native pixel ownership diagnostics.
            const auto bridgePixel = groundPoint(s, destinations[0]);
            const auto& surface = s.renderCache.terrainLayer; std::map<int, int> visiblePixels;
            for (int y = viewport.top; y < viewport.bottom; ++y) for (int x = viewport.left; x < viewport.right; ++x) {
                const int lx = x - viewport.left + s.camera.worldOffsetX - surface.originX;
                const int ly = y - viewport.top + s.camera.worldOffsetY - surface.originY;
                if (lx < 0 || ly < 0 || lx >= surface.width || ly >= surface.height) continue;
                const auto at = static_cast<std::size_t>(ly) * surface.width + lx;
                if (at >= surface.surfaceCellIndices.size()) continue; const int native = surface.surfaceCellIndices[at];
                if (native >= 0 && native % 512 >= 84 && native % 512 <= 86 && native / 512 >= 80 && native / 512 <= 84) ++visiblePixels[native];
            }
            std::ofstream diagnostics(root / (label + "-native-bridge-cells.csv"));
            diagnostics << "case,x,y,native512,tile,subtile,raw_level,effective_level,overlay,frame,facts_water,facts_bridge,facts_low_bridge,runtime_water,runtime_bridge,visible_pixels,tank_passable,hover_passable,amphibious_passable\n";
            const MapQuery query(s.mapFacts);
            for (int y = 80; y <= 84; ++y) for (int x = 84; x <= 86; ++x) {
                const auto terrain = s.terrainByCell.find(runtimeCellKey(x, y)); const auto* facts = query.getCell(x, y);
                if (terrain == s.terrainByCell.end() || !terrain->second || !facts || !facts->hasTerrain) return fail("diagnostic actual bridge domain missing");
                const auto& c = *terrain->second; const int pixels = visiblePixels[y * 512 + x];
                diagnostics << label << ',' << x << ',' << y << ',' << c.cellIndex512 << ',' << c.tileIndex << ',' << c.subTileIndex
                    << ',' << c.level << ',' << terrainLevelAtRuntimeCell(s, x, y) << ',' << c.overlayType << ',' << c.overlayFrame
                    << ',' << facts->isWater << ',' << facts->isBridge << ',' << facts->isLowBridge << ',' << nativeWater(s, {x, y})
                    << ',' << runtimeCellIsBridge(s, x, y) << ',' << pixels;
                for (int id : {2, 3, 4}) { const auto* o = findObjectById(s, id);
                    diagnostics << ',' << (o && runtimeMapQueryPassabilityAllows(s, {x, y}, *o)); }
                diagnostics << '\n';
            }
            diagnostics.close(); if (!diagnostics) return fail("native bridge diagnostic flush");
            if (!bridgePixel || visiblePixels[81 * 512 + 85] <= 0 || !runtimeCellIsBridge(s, 85, 81))
                return fail("stock middle bridge lane has no exact native mouse surface; see native-bridge-cells.csv");
            for (int n : {1, 2}) if (!nativeWater(s, destinations[n]) || runtimeCellIsBridge(s, destinations[n].x, destinations[n].y)
                || visiblePixels[destinations[n].y * 512 + destinations[n].x] <= 0)
                return fail("Hover/Amphibious control destination is not visible native open water; see native-bridge-cells.csv");
            const auto bridgeFailure = [&](const std::string& reason, const char* phase,
                                           const std::array<RuntimeCellPoint, 3>& expected) {
                const auto* chronoRule = s.rules.findSuperWeaponType("ChronoSphereSpecial");
                std::ostringstream details; details << std::setprecision(17) << reason << " tick=" << s.tick << " phase=" << phase;
                if (chronoRule) details << " parsedRange=" << chronoRule->range << " parsedRadius=" << chronoRule->radius
                    << " effectiveRadius=" << std::max(1, chronoRule->radius > 0 ? chronoRule->radius : static_cast<int>(std::ceil(std::max(1.0, chronoRule->range))));
                else details << " parsedChronoRule=missing";
                for (int n = 0; n < 3; ++n) {
                    const int id = n + 2; const auto* o = findRuntimeObjectByIdIncludingInactive(s, id); const auto* w = wreckFor(s, id);
                    details << " object=" << id << " expectedXYZ=" << expected[n].x << ',' << expected[n].y << ',' << terrainLevelAtRuntimeCell(s, expected[n].x, expected[n].y);
                    if (o) details << " type=" << o->type << " actualAliveHP=" << o->alive << '/' << o->health
                        << " actualXYZ=" << o->x << ',' << o->y << ',' << o->level
                        << " renderXYZ=" << runtimeObjectRenderX(*o) << ',' << runtimeObjectRenderY(*o) << ',' << runtimeObjectRenderLevel(*o)
                        << " airBridgeRenderBridge=" << o->airborne << '/' << o->onBridge << '/' << o->renderOnBridge;
                    else details << " actual=missing";
                    details << " tombstone=" << s.deathWeapons.hasTriggered(id) << " wreck=" << bool(w);
                    if (w) details << " wreckRecord=" << serializeRuntimeWreck(*w);
                }
                sample(s, {2, 3, 4}, "original", phase); return fail(details.str());
            };
            const auto debugCastAuthority = [&](const char* phase) {
                std::ofstream debug(root / (label + '-' + phase + "-authority14.ini"), std::ios::binary);
                if (!writeRuntimeStateToStream(s, debug, error)) return fail("aftercast diagnostic authority save: " + error);
                debug.close(); return debug ? true : fail("aftercast diagnostic authority flush");
            };
            // Original Range=1.4 parses to radius=1: the controls at source
            // offset two must remain outside this first, real stock cast.
            if (!castChrono(s, input, {75, 80}, {85, 81}) || !debugCastAuthority("first-cast")) return false;
            const std::array<RuntimeCellPoint, 3> firstDestinations{{{85, 81}, {75, 82}, {74, 82}}};
            for (int n = 0; n < 3; ++n) {
                const int id = n + 2; const auto* o = findObjectById(s, id);
                if (!o || !o->alive || o->health != runtimeMaxHealthForObject(s, *o) || o->x != firstDestinations[n].x || o->y != firstDestinations[n].y
                    || o->airborne || s.deathWeapons.hasTriggered(id) || wreckFor(s, id)
                    || (n == 0 ? !o->onBridge || !runtimeCellIsBridge(s, o->x, o->y) : o->onBridge))
                    return bridgeFailure("first stock radius-one cast changed excluded controls or bridge survivor", "first-cast", firstDestinations);
            }
            // The second real two-area cast selects the unchanged ROBO and
            // LCRF at offsets zero/-one. MTNK is now far outside its source.
            if (!castChrono(s, input, {75, 82}, {85, 83}) || !debugCastAuthority("second-cast")) return false;
            for (int t = 0; t < hz * 2; ++t) {
                for (int n = 0; n < 3; ++n) { const int id = n + 2; const auto* o = findObjectById(s, id);
                    if (!o || !o->alive || o->health != runtimeMaxHealthForObject(s, *o) || o->x != destinations[n].x || o->y != destinations[n].y
                        || !nativeWater(s, destinations[n]) || s.deathWeapons.hasTriggered(id) || wreckFor(s, id)
                        || (n == 0 ? !o->onBridge || !runtimeCellIsBridge(s, o->x, o->y) : o->onBridge || !runtimeMapQueryPassabilityAllows(s, destinations[n], *o)))
                        return bridgeFailure("native bridge / original Hover-Amphibious Chrono landing destroyed or displaced survivor", "second-cast-continuation", destinations); }
                step(s);
            }
            survivors = 3;
        } else {
            stage = "original heavy surface ship / underwater destruction policy";
            for (int id : {2, 3, 4}) { const auto* o = findObjectById(s, id); const auto* type = o ? findRuntimeUnitTypeById(s.rules, o->type) : nullptr;
                if (!o || !type || !type->naval || !nativeWater(s, {o->x, o->y}) || !runtimeMapQueryPassabilityAllows(s, {o->x, o->y}, *o)
                    || (id == 4 ? !type->underwater : type->underwater || type->weight < type->shipSinkingWeight)) return fail("original ship eligibility fixture changed");
                RuntimeObjectExitRequest exit; exit.objectId = id; exit.tick = s.tick; exit.kind = RuntimeObjectExitKind::Destroyed;
                exit.reason = "original surface ship water sinking control"; exit.unitDeathSoundRequested = true; exit.suppressCrew = true; (void)queueRuntimeObjectExit(s, exit);
            }
            (void)drainRuntimeObjectExitQueue(s); completeRuntimeWorldRefresh(s);
            for (int id : {2, 3, 4}) { const auto* o = findRuntimeObjectByIdIncludingInactive(s, id);
                if (!o || o->alive || o->health != 0 || !s.deathWeapons.hasTriggered(id) || (id == 4 && wreckFor(s, id))) return fail("original surface / underwater exit policy differs"); }
            if (std::any_of(s.impactEffects.begin(), s.impactEffects.end(), [](const auto& e) { return e.lifecycleState == "WreckWaterEntry"; }))
                return fail("original heavy ship incorrectly gained land-vehicle entry splash");
            if (!finishSinking(s, {2, 3, 4}, {2, 3}, true)) return false; deaths = 3;
        }
        totalDeaths += deaths; totalSurvivors += survivors; ++scenes;
        cases << label << ',' << hz << ',' << route << ',' << deaths << ',' << survivors << ',' << ordinaryCount - savedOrdinary
            << ',' << canonicalCount - savedCanonical << ',' << pairedTicks - savedPairs << ',' << nativeCount - savedNative << ",PASS\n";
        std::cout << "Water sinking scene " << label << " passed\n";
    }
    if (scenes != (filter.empty() ? 18 : 1) || (filter.empty() && (totalDeaths != 21 || totalSurvivors != 15 || nativeCount != 9
        || ordinaryCount != 21 || canonicalCount != 21))) return fail("complete 18-scene/HP/native/persistence matrix count differs");
    cases.close(); poses.close(); saves.close(); if (!cases || !poses || !saves) return fail("matrix evidence flush");
    std::cout << "Water sinking smoke passed: cases=" << scenes << " deaths=" << totalDeaths << " survivors=" << totalSurvivors
        << " ordinary14=" << ordinaryCount << " canonical=" << canonicalCount << " paired_ticks=" << pairedTicks
        << " native=" << nativeCount << " full_ticks=" << fullTicks << " WM_clicks=" << clicks << " WM_Stop=" << keys << "\n";
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
