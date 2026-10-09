// Issue 23: original weapons/targets, actual Win32 orders and complete ticks.
// A finite animation is created per round. Native animation frames and this
// engine's 15 authored frames/second clock are separate contracts.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeGattlingContinuousFireSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    IniFile originalRules, originalArt; RulesDatabase rules; ArtDatabase art;
    if (!originalRules.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(originalRules)
        || !originalArt.loadFromFile(live.report.artPath) || !art.loadFromIni(originalArt)) {
        error = "Gattling continuous fire: original rules/art unavailable"; return false;
    }
    const char* outputValue = std::getenv("CONCORD_GATTLING_CONTINUOUS_FIRE_OUTPUT");
    const char* caseValue = std::getenv("CONCORD_GATTLING_CONTINUOUS_FIRE_CASE");
    const std::string output = outputValue ? outputValue : "", filter = caseValue ? caseValue : "";
    if (!output.empty()) std::filesystem::create_directories(output);
    std::ofstream csv;
    if (!output.empty()) csv.open(output + "/gattling-rounds.csv");
    if (csv) csv << "case,hz,tick,actor,stage,heat,weapon,slot,burst,rof,anim,age,frame,remaining,total,hp\n";
    constexpr int width = 1280, screenHeight = 720;
    constexpr const char* localOwner = "SkirmishPlayer1";
    const RECT client{0, 0, width, screenHeight}, viewport = runtimeMapViewport(client);
    std::string label, stage, fixtureFailure; std::vector<std::string> failures;
    int scenes = 0, fullTicks = 0, clicks = 0, keys = 0, rounds = 0, timelineChecks = 0;
    int ordinarySaves = 0, exactSaves = 0, pairedTicks = 0, nativePixelChecks = 0, captures = 0;
    const auto fail = [&](const std::string& why) { error = "Gattling " + label + " [" + stage + "]: " + why; return false; };
    const auto integer = [&](const std::string& section, const std::string& key, int fallback) {
        return std::stoi(originalRules.getValue(section, key, std::to_string(fallback)));
    };
    const auto authoredTicks = [](int frames, int hz) { return std::max(1, static_cast<int>(std::llround(double(frames) * hz / 15.0))); };
    int landTile = -1; auto metadataCache = std::make_unique<RuntimeMapRenderCache>();
    const auto metadata = runtimeTerrainMetadataContext(live);
    for (int tile = 0; tile < static_cast<int>(live.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(live, cell);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge
            && resolveRuntimeTerrainSubTileRampType(cell, metadata, *metadataCache) == 0) { landTile = tile; break; }
    }
    if (landTile < 0) return fail("original decoded flat land TMP unavailable");
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
        ? std::filesystem::path("build/issue21_gattling_continuous_fire/fixture-maps")
        : std::filesystem::path(output) / "fixture-maps").lexically_normal();
    std::filesystem::create_directories(mapDirectory);
    std::array<std::string, 1> mapPaths;
    const std::string fixtureTheater = live.report.map.theater.empty() ? "TEMPERATE" : live.report.map.theater;
    for (int variant = 0; variant < 1; ++variant) {
        Bytes raw; raw.reserve(61 * 61 * 11 + 4);
        for (int y = 50; y <= 110; ++y) for (int x = 50; x <= 110; ++x) {
            const bool waterCell = false;
            append16(raw, x); append16(raw, y);
            append32(raw, static_cast<unsigned int>(landTile));
            raw.insert(raw.end(), {static_cast<unsigned char>(0), 0, 0});
        }
        append32(raw, 0);
        const std::string packed = base64(packTiles(raw));
        std::ostringstream text; text.imbue(std::locale::classic());
        text << "[Basic]\nName=GattlingContinuity-" << (variant ? "Water" : "Land")
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
        mapPaths[variant] = (mapDirectory / (variant ? "gattling-water.map" : "gattling-land.map")).generic_string();
        std::ofstream mapFile(mapPaths[variant], std::ios::binary | std::ios::trunc);
        mapFile << text.str(); mapFile.close();
        if (!mapFile) return fail("actual native fixture map could not be persisted: " + mapPaths[variant]);
    }

    const auto make = [&](const std::string& sourceType, int hz, int rank, bool air, bool fast) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        seedRuntimeLoadScratch(s, live, false); s.config = live.config; s.config.fixedTickHz = hz;
        s.config.gameMode = "Battle"; s.config.playerCountry = "YuriCountry"; s.config.noCooldown = fast;
        s.config.gameSpeed = fast && hz == 60 ? 6 : 4;
        s.config.multiplayerActive = s.config.computerAiEnabled = s.config.shortGame = false;
        s.config.crates = s.config.superweapons = false; s.config.startingUnits = 0; s.config.techLevel = 10;
        s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.config.keyboardReferenceLayout = false; s.config.keyBindings.clear();
        s.campaignMode = false; s.skirmishMatchFinished = s.skirmishVictory = false; s.skirmishResult.clear();
        s.rules = rules; s.art = art; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.config.defaultMapPath = mapPaths[0];
        IniFile mapIni; MapDatabase map;
        if (!mapIni.loadFromFile(s.config.defaultMapPath) || !map.loadFromIni(mapIni, &originalRules)
            || !s.world.loadFromMap(map, &s.rules)) {
            fail("actual fixture map did not parse/hydrate: " + s.config.defaultMapPath);
            return std::unique_ptr<RuntimeWindowState>{};
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
        s.report.world = s.world.getSummary();
        s.mapFacts = map.buildMapFacts(&s.theater); s.mapFactsLoaded = !s.mapFacts.cells.empty();
        if (!s.world.getSummary().loaded || !s.world.getObjects().empty() || s.world.getTerrainCells().size() != 61 * 61
            || s.mapFacts.cells.size() != 61 * 61 || s.mapFacts.terrainCellCount != 61 * 61) {
            fail("actual parsed map/world/MapFacts did not preserve the exact native terrain domain");
            return std::unique_ptr<RuntimeWindowState>{};
        }
        const MapQuery query(s.mapFacts);
        for (const auto& cell : s.world.getTerrainCells()) {
            const auto* facts = query.getCell(cell.x, cell.y);
            const bool waterCell = false;
            if (!facts || !facts->hasTerrain || cell.cellIndex512 != cell.y * 512 + cell.x
                || cell.tileIndex != (landTile)
                || cell.subTileIndex != (0) || cell.level != 0
                || facts->terrain.tileIndex != cell.tileIndex || facts->terrain.subTileIndex != cell.subTileIndex
                || facts->heightLevel != 0 || facts->isWater != waterCell || facts->isBridge) {
                fail("parsed native TMP and actual MapFacts disagree at " + std::to_string(cell.x) + ',' + std::to_string(cell.y));
                return std::unique_ptr<RuntimeWindowState>{};
            }
        }
        const auto add = [&](int id, const std::string& type, WorldObjectKind kind,
                RuntimeCellPoint cell, const std::string& owner, const char* mission) {
            WorldObjectInstance o; o.instanceId = id; o.mapId = "GattlingContinuity" + std::to_string(id);
            o.type = type; o.kind = kind; o.owner = owner; o.x = cell.x; o.y = cell.y;
            o.cell = runtimeCellKey(cell.x, cell.y); o.alive = o.poweredOperational = true; o.mission = mission;
            o.health = runtimeMaxHealthForObject(s, o); o.facing = runtimeFacingFromDelta(1, 0);
            o.turretFacing = o.facing; initializeRuntimeObjectAmmo(o, runtimeAmmoRulesForObject(s.rules, o));
            applyRuntimeInitialObjectFlightState(s.rules, o, true); runtimeInitializeModExtensions(o, s.rules, hz);
            s.world.accessObjects().push_back(std::move(o));
        };
        add(1, sourceType, sourceType == "YAGGUN" ? WorldObjectKind::Structure : WorldObjectKind::Unit, {76, 80}, localOwner, "Guard");
        findMutableObjectById(s, 1)->veterancy = rank;
        if (!fast) {
            add(2, air ? "ZEP" : "YACNST", air ? WorldObjectKind::Unit : WorldObjectKind::Structure, {79, 80}, "SkirmishPlayer2", "Sleep");
            add(3, air ? "ZEP" : "YACNST", air ? WorldObjectKind::Unit : WorldObjectKind::Structure, {76, 84}, "SkirmishPlayer2", "Sleep");
        }
        add(100, "YAPOWR", WorldObjectKind::Structure, {104, 104}, localOwner, "Sleep");
        add(998, "NANRCT", WorldObjectKind::Structure, {52, 52}, "SkirmishPlayer2", "Sleep");
        s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner = localOwner; s.economy.credits = 20000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        RuntimeConfig::PlayerSlot local; local.enabled = local.human = local.local = true;
        local.name = "Gattling local"; local.country = "YuriCountry"; local.team = "-";
        RuntimeConfig::PlayerSlot hostile; hostile.enabled = true; hostile.country = "Russians"; hostile.team = "-";
        s.config.playerSlots = {local, hostile};
        s.skirmishCountryByOwner.clear(); s.skirmishTeamByOwner.clear(); s.playerCommands.alliances.clear();
        s.skirmishCountryByOwner["skirmishplayer1"] = "YuriCountry"; s.skirmishCountryByOwner["skirmishplayer2"] = "Russians";
        s.humanHouseAuthorities.clear(); RuntimeHumanHouseAuthority house;
        house.owner = localOwner; house.canonicalOwner = "skirmishplayer1"; house.country = "YuriCountry"; house.localFacade = true;
        s.humanHouseAuthorities.push_back(house);
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        ensureRuntimeGarrisonsInitialized(s); ensureRuntimeGatesInitialized(s); ensureRuntimeRepairDepotsInitialized(s);
        (void)ensureRuntimeAircraftServicesInitialized(s); ensureRuntimeMenuKeyboardLoaded(s);
        initializeRuntimeOfflineGameplayRouting(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s); }
        rebuildRuntimeEconomyPower(s); return storage;
    };
    const auto step = [&](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s); ++fullTicks;
        if (s.skirmishMatchFinished && fixtureFailure.empty()) fixtureFailure = "Battle ended: " + s.skirmishResult;
    };
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
        input.window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"Real Gattling full-tick orders",
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
    const auto click = [&](Input& input, POINT p, bool forced = false) {
        RuntimeSnapshotActiveStateScope active(input.state);
        const WPARAM modifier = forced ? MK_CONTROL : 0;
        SendMessageW(input.window, WM_MOUSEMOVE, modifier, MAKELPARAM(p.x, p.y));
        SendMessageW(input.window, WM_LBUTTONDOWN, MK_LBUTTON | modifier, MAKELPARAM(p.x, p.y));
        SendMessageW(input.window, WM_LBUTTONUP, modifier, MAKELPARAM(p.x, p.y)); ++clicks;
    };
    const auto stop = [&](Input& input) {
        RuntimeSnapshotActiveStateScope active(input.state); applySelection(input.state, {1}, false, false);
        SendMessageW(input.window, WM_KEYDOWN, 'S', LPARAM(1));
        SendMessageW(input.window, WM_KEYUP, 'S', LPARAM(1) | (LPARAM(1) << 30) | (LPARAM(1) << 31)); ++keys;
    };
    const auto move = [&](Input& input, RuntimeCellPoint goal) {
        auto& s = input.state; RuntimeSnapshotActiveStateScope active(s); applySelection(s, {1}, false, false);
        centerRuntimeCameraOnCell(s, client, goal); ensureRuntimeCameraForViewport(s, viewport);
        POINT p = runtimeScreenPointForCell(s, viewport, goal, terrainLevelAtRuntimeCell(s, goal.x, goal.y));
        bool found = false;
        for (int dy = -8; dy <= 8 && !found; ++dy) for (int dx = -8; dx <= 8 && !found; ++dx) {
            const POINT q{p.x + dx, p.y + dy}, picked = approximateCellFromScreenPoint(s, viewport, q);
            if (pointInRectInclusive(viewport, q) && picked.x == goal.x && picked.y == goal.y
                && !findProjectedObjectAtPoint(runtimeProjectedObjectsForViewport(s, viewport), q)) { p = q; found = true; }
        }
        if (!found) return fail("actual Move pixel cannot select the requested native terrain");
        click(input, p); const auto order = s.behaviors.find(1);
        return order != s.behaviors.end() && order->second.kind == RuntimeBehaviorKind::Move && order->second.moveTarget == goal
            ? true : fail("actual WM Move rejected: " + s.interactionStatus);
    };

    const auto forceGround = [&](Input& input, RuntimeCellPoint goal) {
        auto& s = input.state; RuntimeSnapshotActiveStateScope active(s); applySelection(s, {1}, false, false);
        centerRuntimeCameraOnCell(s, client, goal); ensureRuntimeCameraForViewport(s, viewport);
        invalidateRuntimeObjectProjectionCache(s);
        const POINT center = runtimeScreenPointForCell(s, viewport, goal, terrainLevelAtRuntimeCell(s, goal.x, goal.y));
        std::optional<POINT> pixel;
        for (int y = -8; y <= 8 && !pixel; ++y) for (int x = -8; x <= 8 && !pixel; ++x) {
            const POINT p{center.x + x, center.y + y}, picked = approximateCellFromScreenPoint(s, viewport, p);
            if (pointInRectInclusive(viewport, p) && picked.x == goal.x && picked.y == goal.y
                && !findProjectedObjectAtPoint(runtimeProjectedObjectsForViewport(s, viewport), p)) pixel = p;
        }
        if (!pixel) return fail("actual empty native terrain has no exact WM force-fire pixel");
        click(input, *pixel, true); const auto order = s.behaviors.find(1);
        return order != s.behaviors.end() && order->second.kind == RuntimeBehaviorKind::Attack
            && order->second.attackGround && order->second.forceFire && order->second.attackBridgeCell == goal
            ? true : fail("real Ctrl+WM force-fire did not retain its ground intent: " + s.interactionStatus);
    };
    const auto attack = [&](Input& input, int targetId) {
        const auto pixel = objectPoint(input.state, targetId); if (!pixel) return fail("real target has no legal WM hit pixel");
        bool forced = false;
        { RuntimeSnapshotActiveStateScope active(input.state); applySelection(input.state, {1}, false, false);
            const auto* source = findObjectById(input.state, 1);
            forced = source && source->kind == WorldObjectKind::Structure; }
        // The ordinary primary-click branch requires a movable selection.
        // A selected stock defense uses the real Ctrl+object Attack route.
        click(input, *pixel, forced); const auto order = input.state.behaviors.find(1);
        return order != input.state.behaviors.end() && order->second.kind == RuntimeBehaviorKind::Attack
            && order->second.targetObjectId == targetId && order->second.forceFire == forced
            ? true : fail("real WM object attack rejected: " + input.state.interactionStatus);
    };
    const auto contextFor = [&](const RuntimeWindowState& s) {
        RuntimeMapRenderContext c; c.rules = &s.rules; c.art = &s.art; c.theater = &s.theater;
        c.assetRootPath = runtimeAssetRootPath(s.config); c.palettePath = runtimePalettePath(s.config, s.report.map);
        c.structurePalettePath = runtimeStructurePalettePath(s.config, s.report.map);
        c.structureRootPath = runtimeStructureRootPath(s.config, s.report.map);
        c.fixedTickHz = s.config.fixedTickHz; c.animationTick = s.tick;
        c.effectOnlyProbe = true; c.vision = nullptr; c.lightingTint = nullptr;
        return c;
    };
    struct NativeAnimation {
        int frames = 0, delay = 0, lifetime = 0; std::string path;
        std::vector<RuntimeShpFrameImage> images;
    };
    std::map<std::pair<std::string, int>, NativeAnimation> animations;
    auto resourceCache = std::make_unique<RuntimeMapRenderCache>();
    const auto animation = [&](const RuntimeWindowState& s, const std::string& id) -> const NativeAnimation* {
        const int hz = s.config.fixedTickHz;
        const int rate = std::stoi(originalArt.getValue(id, "Rate", "0"));
        int delayFrames = rate > 0 ? std::max(1, 900 / rate) : 1;
        const bool normalized = toLowerAsciiLocal(originalArt.getValue(id, "Normalized", "no")) == "yes";
        // Native 5FB2E0: delay 1 becomes 2 only at native GameSpeed 0/1.
        // Project gameSpeed runs in the reverse direction; 15 frames/second
        // is this project's clock mapping, not a retail wall-time claim.
        if (normalized && delayFrames == 1 && 6 - s.config.gameSpeed <= 1) delayFrames = 2;
        const int delay = (delayFrames * hz + 14) / 15;
        const auto key = std::make_pair(id, delay); auto found = animations.find(key);
        if (found != animations.end()) return &found->second;
        const bool shadow = toLowerAsciiLocal(originalArt.getValue(id, "Shadow", "no")) == "yes";
        if (shadow || !originalArt.getValue(id, "Start", "").empty() || !originalArt.getValue(id, "End", "").empty()
            || !originalArt.getValue(id, "LoopStart", "").empty() || !originalArt.getValue(id, "LoopEnd", "").empty()) {
            fail("original stock muzzle gained a shadow/partial/loop strip; this finite full-strip oracle is no longer applicable"); return nullptr;
        }
        NativeAnimation a; RuntimeShpFrameImage first; std::string resolved, decodeError; int frames = 0;
        if (!loadRuntimeAuxiliaryShpFrameImage(contextFor(s), *resourceCache, id, 0, first, resolved, a.path, decodeError, false, &frames)
            || frames <= 0 || a.path.empty()) { fail("real original muzzle SHP decode failed: " + id + " " + decodeError); return nullptr; }
        const int stockCount = id == "VTMUZZLE" ? 11 : id == "GUNFIRE" ? 4 : 6;
        if (frames != stockCount) { fail("original full SHP count differs from 6/4/11: " + id + '=' + std::to_string(frames)); return nullptr; }
        a.frames = frames; a.delay = delay; a.lifetime = frames * delay + 1;
        for (int f = 0; f < frames; ++f) {
            RuntimeShpFrameImage image;
            if (!loadRuntimeAuxiliaryShpFrameImage(contextFor(s), *resourceCache, id, f, image, resolved, a.path, decodeError, false)) {
                fail("original muzzle frame cannot decode: " + id + ' ' + std::to_string(f)); return nullptr;
            }
            a.images.push_back(std::move(image));
        }
        return &animations.emplace(key, std::move(a)).first->second;
    };
    constexpr int rasterWidth = 400, rasterHeight = 240;
    const RECT rasterViewport{0, 0, rasterWidth, rasterHeight};
    auto pixelCache = std::make_unique<RuntimeMapRenderCache>();
    const auto drawNativeFrame = [&](const RuntimeWindowState& s, const RuntimeImpactEffect& effect, const NativeAnimation& a, int frame) {
        auto actor = makeRuntimeImpactRenderActor(effect); auto c = contextFor(s);
        std::vector<RuntimeImpactRenderActor> actors{actor}; c.impactActors = &actors;
        c.surfaceWidth = rasterWidth; c.surfaceHeight = rasterHeight;
        RuntimeMapCamera camera; camera.initialized = true; camera.tileWidth = 60; camera.tileHeight = 30;
        const auto projected = runtimeProjectCellXY(actor.x, actor.y, actor.level, camera);
        camera.worldOffsetX = projected.x - rasterWidth / 2;
        camera.worldOffsetY = projected.y - rasterHeight / 2;
        constexpr std::uint32_t bg1 = 0x001b2634, bg2 = 0x00492a16;
        std::vector<std::uint32_t> first(rasterWidth * rasterHeight, bg1), second(rasterWidth * rasterHeight, bg2);
        auto copyCamera = camera; c.surfacePixels = first.data(); const auto render1 = drawRuntimeMap(nullptr, rasterViewport, s.world, c, copyCamera, *pixelCache);
        copyCamera = camera; c.surfacePixels = second.data(); const auto render2 = drawRuntimeMap(nullptr, rasterViewport, s.world, c, copyCamera, *pixelCache);
        if (!render1.rendered || !render2.rendered || render1.impactActorSnapshots != 1 || render1.impactAnimMissing)
            return fail("actual isolated finite muzzle rendering failed: " + effect.animId);
        const auto& image = a.images.at(frame); int expected = 0, actual = 0;
        RECT raw{image.width, image.height, 0, 0}, native{rasterWidth, rasterHeight, 0, 0};
        for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x) if (image.pixels[y * image.width + x] != 0) {
            ++expected; raw.left = std::min<LONG>(raw.left, x); raw.right = std::max<LONG>(raw.right, x + 1);
            raw.top = std::min<LONG>(raw.top, y); raw.bottom = std::max<LONG>(raw.bottom, y + 1);
        }
        for (int y = 0; y < rasterHeight; ++y) for (int x = 0; x < rasterWidth; ++x) {
            const std::size_t p = std::size_t(y) * rasterWidth + x;
            if ((first[p] & 0xffffffU) == bg1 && (second[p] & 0xffffffU) == bg2) continue;
            ++actual; native.left = std::min<LONG>(native.left, x); native.right = std::max<LONG>(native.right, x + 1);
            native.top = std::min<LONG>(native.top, y); native.bottom = std::max<LONG>(native.bottom, y + 1);
        }
        if (actual != expected || (actual && (native.right - native.left != raw.right - raw.left || native.bottom - native.top != raw.bottom - raw.top)))
            return fail("native muzzle opacity geometry lost original frame " + effect.animId + ':' + std::to_string(frame)
                + " pixels=" + std::to_string(actual) + '/' + std::to_string(expected));
        // The two actual draws at the same paused simulation tick must agree.
        std::vector<std::uint32_t> repeat(rasterWidth * rasterHeight, bg1); copyCamera = camera; c.surfacePixels = repeat.data();
        drawRuntimeMap(nullptr, rasterViewport, s.world, c, copyCamera, *pixelCache);
        if (first != repeat) return fail("same-tick repaint advanced a finite muzzle");
        ++nativePixelChecks; return true;
    };
    const std::array<std::string, 3> reports{"GattlingGunAttackLoop1", "GattlingGunAttackLoop2", "GattlingGunAttackLoop3"};
    const std::array<std::vector<std::string>, 3> stockSamples{{
        {"vgatlo1a", "vgatlo2a", "vgatlo2b", "vgatlo2c", "vgatlo3a"},
        {"vgatlo4a", "vgatlo5a", "vgatlo5b", "vgatlo6a"},
        {"vgatlo7a", "vgatlo8a", "vgatlo8b", "vgatlo9a"}}};
    std::map<std::string, std::uint64_t> sampleMilliseconds;
    const auto inspectSamples = [&](RuntimeWindowState& s) {
        for (int n = 0; n < 3; ++n) {
            const auto* sound = s.worldAudioCatalog.find(reports[n]);
            if (!sound || sound->samples != stockSamples[n] || !sound->loop || !sound->randomSample
                || sound->simultaneousLimit != 2 || sound->volumePercent != 60 + n * 5 + (n == 2 ? 5 : 0)
                || s.soundIni.getValue(reports[n], "Control", "") != "attack loop random all decay")
                return fail("original catalog loop/all/attack/decay/Limit/Volume changed: " + reports[n]);
            for (const auto& sample : sound->samples) {
                if (sampleMilliseconds.contains(sample)) continue;
                const auto path = findRuntimeSoundSamplePath(s, sample); if (!path) return fail("actual BAG sample missing: " + sample);
                std::ifstream input(*path, std::ios::binary);
                const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
                const auto u16 = [&](std::size_t p) { return unsigned(bytes[p]) | (unsigned(bytes[p + 1]) << 8); };
                const auto u32 = [&](std::size_t p) { return std::uint64_t(u16(p)) | (std::uint64_t(u16(p + 2)) << 16); };
                if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4)
                    || u32(4) + 8 != bytes.size()) return fail("actual sound cache is not a complete WAVE: " + sample);
                std::uint64_t data = 0, average = 0; bool format = false;
                for (std::size_t p = 12; p + 8 <= bytes.size();) {
                    const auto count = u32(p + 4), start = p + 8;
                    if (count > bytes.size() - start) return fail("invalid original BAG-decoded WAVE chunk");
                    if (!std::memcmp(bytes.data() + p, "fmt ", 4)) {
                        if (count < 16 || u16(start) != 1 || u16(start + 2) != 1 || u32(start + 4) != 22050
                            || u16(start + 12) != 2 || u16(start + 14) != 16) return fail("actual BAG decode differs from mono22050 PCM16");
                        average = u32(start + 8); format = true;
                    }
                    if (!std::memcmp(bytes.data() + p, "data", 4)) data = count;
                    p = start + static_cast<std::size_t>(count) + (count & 1U);
                }
                if (!format || average != 44100 || data == 0 || (data & 1U)) return fail("actual BAG PCM data/duration invalid");
                sampleMilliseconds[sample] = (data * 1000 + average - 1) / average;
            }
        }
        return true;
    };
    struct SoundProbe {
        std::set<std::string> seen; std::array<std::vector<std::string>, 3> body;
        std::array<int, 3> intro{}, decay{}; int plays = 0;
        std::string clockEvent; std::uint64_t clockWall = 0; int clockTick = 0;
        std::string lastPresentedEvent; std::uint64_t lastPresentedTime = 0;
    };
    const auto collectAudio = [&](RuntimeWindowState& s, SoundProbe& probe, bool restored) {
        for (const auto& action : s.runtimeWorldAudioBackendActionHistory) {
            if (action.sourceObjectId != 1 || action.kind != RuntimeWorldAudioActionKind::Play
                || (action.handleId != runtimeGattlingSoundHandle(1, 0) && action.handleId != runtimeGattlingSoundHandle(1, 1))) continue;
            const auto event = std::find(reports.begin(), reports.end(), action.eventId); if (event == reports.end()) return fail("managed source used a foreign sound event");
            const int n = static_cast<int>(event - reports.begin());
            const std::string key = std::to_string(action.handleId) + ':' + action.eventId + ':' + std::to_string(action.eventTick)
                + ':' + std::to_string(action.playbackOrdinal) + ':' + action.sampleName;
            if (!probe.seen.insert(key).second) continue;
            if (action.cell.x != 76 || action.cell.y != 80 || action.sourceObjectId != 1 || action.gainPermille <= 0)
                return fail("managed original sound lost its real source/listener position");
            const auto& samples = stockSamples[n]; ++probe.plays;
            if (action.handleId == runtimeGattlingSoundHandle(1, 1)) {
                if (action.sampleName != samples.back() || action.queueAfterCurrent) return fail("stop decay is not the original last sample");
                ++probe.decay[n]; continue;
            }
            if (!action.queueAfterCurrent) return fail("continuous original source sample is not queued");
            if (action.sampleName == samples.front()) {
                if (restored || ++probe.intro[n] != 1) return fail("load/ordinary rounds replayed a stage intro: " + action.eventId);
            } else {
                if (action.sampleName == samples.back()
                    || std::find(samples.begin() + 1, samples.end() - 1, action.sampleName) == samples.end() - 1)
                    return fail("sustained fire selected an intro/decay/foreign body sample");
                auto& body = probe.body[n]; body.push_back(action.sampleName); const std::size_t span = samples.size() - 2;
                if (body.size() % span == 0) {
                    const std::set<std::string> actual(body.end() - span, body.end());
                    const std::set<std::string> expected(samples.begin() + 1, samples.end() - 1);
                    if (actual != expected) return fail("random-all body bag repeated or omitted an original sample");
                }
            }
        }
        return true;
    };
    const auto presentAudio = [&](RuntimeWindowState& s, SoundProbe& probe, bool restored) {
        RuntimeSnapshotActiveStateScope active(s);
        if (const auto current = s.gattlingAudio.sources.find(1); current != s.gattlingAudio.sources.end()
            && probe.clockEvent != current->second.event) {
            // An actual round may create a stage before this presentation.
            // Its first queued sample (ordinal 0) gives the original duration;
            // nextSample is precisely creation time plus that duration. Do not
            // guess the epoch from a later GetTickCount64 observation.
            const auto first = std::find_if(s.runtimeWorldAudioBackendActionHistory.rbegin(),
                s.runtimeWorldAudioBackendActionHistory.rend(), [&](const RuntimeWorldAudioAction& action) {
                    return action.kind == RuntimeWorldAudioActionKind::Play && action.sourceObjectId == 1
                        && action.handleId == runtimeGattlingSoundHandle(1, 0) && action.eventId == current->second.event
                        && action.playbackOrdinal == 0 && action.queueAfterCurrent;
                });
            if (first == s.runtimeWorldAudioBackendActionHistory.rend()
                || current->second.ordinal != 2 || !sampleMilliseconds.contains(first->sampleName)
                || current->second.nextSampleMilliseconds < sampleMilliseconds.at(first->sampleName))
                return fail("actual stage has no exact initial sample schedule: " + current->second.event
                    + " ordinal=" + std::to_string(current->second.ordinal));
            probe.clockEvent = current->second.event; probe.clockTick = s.tick;
            probe.clockWall = current->second.nextSampleMilliseconds - sampleMilliseconds.at(first->sampleName);
        }
        if (!probe.clockWall) { probe.clockWall = GetTickCount64(); probe.clockTick = s.tick; }
        const auto time = probe.clockWall + std::uint64_t(s.tick - probe.clockTick) * 1000 / s.config.fixedTickHz;
        if (probe.lastPresentedEvent == probe.clockEvent && time < probe.lastPresentedTime)
            return fail("presentation time moved backwards within an original stage: " + probe.clockEvent
                + " time=" + std::to_string(time) + " previous=" + std::to_string(probe.lastPresentedTime));
        advanceRuntimeGattlingWeaponAudio(s, time);
        if (const auto current = s.gattlingAudio.sources.find(1); current != s.gattlingAudio.sources.end()
            && probe.clockEvent != current->second.event) {
            // A threshold can create the new stage in THIS call, using time
            // from the old stage. Bind its epoch now so the next frame never
            // resets this freshly queued virtual schedule to the real clock.
            probe.clockEvent = current->second.event; probe.clockWall = time; probe.clockTick = s.tick;
        }
        probe.lastPresentedEvent = probe.clockEvent; probe.lastPresentedTime = time;
        return collectAudio(s, probe, restored);
    };
    const auto checkpoint = [&](RuntimeWindowState& s, bool exact) {
        std::ostringstream baseText, exactText, canonical; IniFile base, extension;
        if (!writeRuntimeStateToStream(s, baseText, error) || !base.loadFromString(baseText.str())) return std::unique_ptr<RuntimeWindowState>{};
        if (exact && (!writeRuntimeStateToStream(s, exactText, error) || !writeRuntimeMultiplayerSnapshotExtension(s, exactText, error)
            || !extension.loadFromString(exactText.str()) || !writeRuntimeMultiplayerSnapshotCanonicalIni(extension, canonical, error)
            || !extension.loadFromString(canonical.str()))) return std::unique_ptr<RuntimeWindowState>{};
        if (!output.empty()) {
            std::ofstream payload(output + '/' + label + (exact ? "-canonical-extension.ini" : "-ordinary.ini"), std::ios::binary);
            payload << (exact ? canonical.str() : baseText.str()); if (!payload) return std::unique_ptr<RuntimeWindowState>{};
        }
        auto other = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*other, s, false);
        // This is a real canonical extension over the complete v14 base, not a
        // fabricated V15 room envelope. No map/route/heat/pose fields are patched.
        if (!materializeRuntimeLoadStateFromIni(*other, base, error)
            || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*other, extension, error))) return std::unique_ptr<RuntimeWindowState>{};
        initializeRuntimeOfflineGameplayRouting(*other); if (exact) ++exactSaves; else ++ordinarySaves; return other;
    };
    const auto describe = [&](const RuntimeWindowState& s) {
        std::ostringstream text; text << "tick=" << s.tick;
        if (const auto* o = findObjectById(s, 1)) text << " source=" << o->type << " hp=" << o->health << " rank=" << o->veterancy
            << " cooldown=" << o->weaponCooldownTicksRemaining << " mission=" << o->mission << " facing=" << o->turretFacing;
        const auto heat = s.gattlingWeaponTimers.find(1); text << " heat=" << (heat == s.gattlingWeaponTimers.end() ? -1 : heat->second);
        const auto b = s.behaviors.find(1); if (b != s.behaviors.end()) text << " behavior=" << static_cast<int>(b->second.kind)
            << '/' << static_cast<int>(b->second.state) << " target=" << b->second.targetObjectId;
        const auto burst = s.combatDetails.bursts.find(1); if (burst != s.combatDetails.bursts.end()) text << " burst=" << burst->second.weaponId
            << " remaining=" << burst->second.roundsRemaining << " next=" << burst->second.nextFireTick;
        const auto sound = s.gattlingAudio.sources.find(1); if (sound != s.gattlingAudio.sources.end()) text << " audio=" << sound->second.event
            << " ordinal=" << sound->second.ordinal << " active=" << sound->second.active;
        if (const auto* target = findObjectById(s, 2)) text << " targetHP=" << target->health;
        return text.str();
    };
    const auto effectRecord = [](const RuntimeImpactEffect& fx) {
        // The numeric impact record and exact Presentation tail jointly keep
        // the original image/frame. Neither half alone proves an exact FX.
        std::ostringstream text; text << runtimeMultiplayerSnapshotImpactRecord(fx) << '|'
            << iniHexEncode(fx.animId) << '|' << iniHexEncode(fx.renderIntent) << '|' << fx.frameIndex << '|'
            << fx.facing << '|' << fx.radius << '|' << runtimeBoolText(fx.bright) << '|' << iniHexEncode(fx.projectileImageId);
        return text.str();
    };
    const auto compare = [&](const RuntimeWindowState& a, const RuntimeWindowState& b, bool exact, int futureActor) {
        if (a.tick != b.tick || a.gattlingWeaponTimers != b.gattlingWeaponTimers || a.economy.credits != b.economy.credits
            || a.nextProjectileActorId != b.nextProjectileActorId) return fail("restored hot run/tick/credits/next actor changed: " + describe(a) + " vs " + describe(b));
        for (int id : {1, 2}) {
            const auto* x = findObjectById(a, id); const auto* y = findObjectById(b, id);
            if (!x || !y || x->health != y->health || x->weaponCooldownTicksRemaining != y->weaponCooldownTicksRemaining
                || x->currentAmmo != y->currentAmmo || x->veterancy != y->veterancy || x->facing != y->facing || x->turretFacing != y->turretFacing
                || runtimeObjectRenderX(*x) != runtimeObjectRenderX(*y) || runtimeObjectRenderY(*x) != runtimeObjectRenderY(*y)
                || runtimeObjectRenderLevel(*x) != runtimeObjectRenderLevel(*y)) return fail("restore changed genuine source/target HP/pose/reload: " + describe(a) + " vs " + describe(b));
        }
        std::ostringstream ac, bc; a.combatDetails.save(ac); b.combatDetails.save(bc);
        if (ac.str() != bc.str()) return fail("restore changed an actual pending burst/cursor: " + describe(a) + " vs " + describe(b));
        if (a.projectiles.size() != b.projectiles.size()) return fail("restored active actual rounds differ");
        for (std::size_t i = 0; i < a.projectiles.size(); ++i)
            if (serializeRuntimeProjectileEffect(a.projectiles[i]) != serializeRuntimeProjectileEffect(b.projectiles[i])) return fail("restored actual projectile fields differ");
        if (exact) {
            if (a.impactEffects.size() != b.impactEffects.size()) return fail("canonical extension lost a saved finite FX");
            for (std::size_t i = 0; i < a.impactEffects.size(); ++i)
                if (effectRecord(a.impactEffects[i]) != effectRecord(b.impactEffects[i]))
                    return fail("canonical extension changed saved/future FX fields or native clock");
        } else {
            // Ordinary v14 omits old generic FX. Every future real round and
            // its finite muzzle remains a strict bidirectional requirement.
            const auto future = [&](const RuntimeWindowState& s) {
                std::map<std::string, std::string> result;
                for (const auto& fx : s.impactEffects) if (fx.attackerObjectId == 1 && fx.projectileActorId >= futureActor) {
                    const std::string key = std::to_string(fx.projectileActorId) + ':' + fx.renderIntent + ':' + fx.lifecycleState;
                    result.emplace(key, effectRecord(fx));
                }
                return result;
            };
            if (future(a) != future(b)) return fail("ordinary future muzzle/impact physical fields/complete native timeline differ");
        }
        ++pairedTicks; return true;
    };
    struct Round {
        int tick = 0, stage = 0, burstIndex = 0; RuntimeProjectileEffect shot; RuntimeImpactEffect birth;
        const NativeAnimation* native = nullptr; std::set<int> checkedFrames; bool expired = false, pixelProbe = false;
    };
    struct RunProbe {
        std::map<int, Round> fired; std::array<int, 3> counts{}, volleyCounts{}, chosen{-1, -1, -1};
        int previousFirst = -1, previousLast = -1, previousRof = 0, previousStage = -1;
        int highestTick = -1; std::set<int> rawStages;
    };
    const auto inspectRounds = [&](RuntimeWindowState& s, RunProbe& probe, const std::string& type, int rank, bool air,
        bool beforePending, int beforeNextFire, const std::string& beforeWeapon, bool newOrders) {
        std::vector<const RuntimeProjectileEffect*> newlyBorn;
        const auto collect = [&](const auto& container) { for (const auto& shot : container)
            if (shot.attackerObjectId == 1 && shot.generation == 0 && !probe.fired.contains(shot.actorId)) newlyBorn.push_back(&shot); };
        collect(s.projectiles); collect(s.recentExpiredProjectiles);
        std::sort(newlyBorn.begin(), newlyBorn.end(), [](const auto* x, const auto* y) { return x->actorId < y->actorId; });
        for (const auto* shot : newlyBorn) {
            int indexed = -1;
            for (int slot = 0; slot < 6; ++slot) if (originalRules.getValue(type,
                std::string(rank == 2 ? "EliteWeapon" : "Weapon") + std::to_string(slot + 1), "") == shot->weaponId) { indexed = slot; break; }
            if (indexed < 0 || indexed % 2 != (air ? 1 : 0)) return fail("actual shot changed its original ground/air indexed weapon");
            const int n = indexed / 2, rof = integer(shot->weaponId, "ROF", -1), burst = integer(shot->weaponId, "Burst", 0);
            const auto* weapon = findRuntimeWeaponType(s.rules, shot->weaponId);
            if (!weapon || weapon->rof != rof || weapon->burst != burst || burst != 2 || weapon->report != reports[n]
                || weapon->damage != integer(shot->weaponId, "Damage", -1)
                || shot->warheadId != originalRules.getValue(shot->weaponId, "Warhead", ""))
                return fail("actual shot no longer uses the immutable original ROF/Burst/Damage/Warhead/Report");
            const bool second = beforePending && beforeNextFire <= s.tick && beforeWeapon == shot->weaponId;
            const auto heat = s.gattlingWeaponTimers.find(1); const int timer = heat == s.gattlingWeaponTimers.end() ? 0 : heat->second;
            const int threshold1 = integer(type, rank == 2 ? "EliteStage1" : "Stage1", -1);
            const int threshold2 = integer(type, rank == 2 ? "EliteStage2" : "Stage2", -1);
            const int expectedStage = timer >= threshold2 ? 2 : timer >= threshold1 ? 1 : 0;
            if (!second && n != expectedStage) return fail("new volley ignored original stage thresholds");
            const auto muzzle = std::find_if(s.impactEffects.begin(), s.impactEffects.end(), [&](const auto& fx) {
                return fx.attackerObjectId == 1 && fx.projectileActorId == shot->actorId && fx.renderIntent == "weaponAnim";
            });
            if (muzzle == s.impactEffects.end() || std::count_if(s.impactEffects.begin(), s.impactEffects.end(), [&](const auto& fx) {
                    return fx.attackerObjectId == 1 && fx.projectileActorId == shot->actorId && fx.renderIntent == "weaponAnim";
                }) != 1 || muzzle->weaponId != shot->weaponId || muzzle->weaponSlot != shot->weaponSlot
                || muzzle->worldX != shot->sourceX || muzzle->worldY != shot->sourceY || muzzle->worldLevel != shot->sourceZ
                || muzzle->lifecycleState != "GattlingMuzzle" || !muzzle->hasWorldPosition)
                return fail("actual paid round lost/duplicated its independent authored FLH muzzle");
            if ((n == 0 && muzzle->animId.rfind("MGUN-", 0) != 0)
                || (n > 0 && muzzle->animId != (n == 1 ? "GUNFIRE" : "VTMUZZLE"))) return fail("wrong original stage animation resource");
            const auto* native = animation(s, muzzle->animId); if (!native) return false;
            if (muzzle->animationFrameCount != native->frames || muzzle->animationSequenceSteps != native->frames
                || muzzle->animationFrameDelayTicks != native->delay || muzzle->totalTicks != native->lifetime
                || muzzle->totalTicks - muzzle->remainingTicks > 1 || muzzle->frameIndex != 0)
                return fail("creation pass compressed/missed original full finite animation: " + muzzle->animId);
            Round record; record.tick = shot->spawnTick; record.stage = n; record.burstIndex = second ? 1 : 0;
            record.shot = *shot; record.birth = *muzzle; record.native = native;
            if (probe.chosen[n] < 0) { probe.chosen[n] = shot->actorId; record.pixelProbe = true; }
            probe.fired.emplace(shot->actorId, std::move(record)); ++probe.counts[n]; ++rounds;
            if (n == 2 && probe.highestTick < 0) probe.highestTick = s.tick;
            if (second) {
                const int gap = s.tick - probe.previousFirst;
                if (gap < authoredTicks(3, s.config.fixedTickHz) || gap > authoredTicks(5, s.config.fixedTickHz))
                    return fail("actual second-round delay left original project 3..5-frame burst contract");
                if (probe.fired.size() >= 2) {
                    const auto first = std::find_if(probe.fired.begin(), probe.fired.end(), [&](const auto& entry) { return entry.second.tick == probe.previousFirst; });
                    if (first != probe.fired.end() && std::hypot(first->second.shot.sourceX - shot->sourceX,
                            first->second.shot.sourceY - shot->sourceY) <= 0.01)
                        return fail("Burst2 collapsed the original alternating gun barrels");
                }
                probe.previousLast = s.tick; probe.previousRof = rof; probe.previousStage = n;
            } else {
                if (newOrders && probe.previousLast >= 0) {
                    const int gap = s.tick - probe.previousLast;
                    const bool eliteRof = rank == 2 && originalRules.getValue(type, "EliteAbilities", "").find("ROF") != std::string::npos;
                    const int minimum = !eliteRof ? probe.previousRof : probe.previousRof == 16 ? 9 : probe.previousRof == 8 ? 4 : 2;
                    const int maximum = !eliteRof ? probe.previousRof + 2 : probe.previousRof == 16 ? 10 : probe.previousRof == 8 ? 5 : 3;
                    if (gap < authoredTicks(minimum, s.config.fixedTickHz) || gap > authoredTicks(maximum, s.config.fixedTickHz) + 1)
                        return fail("next volley bypassed original reload after its actual last round: gap=" + std::to_string(gap)
                            + " expected=" + std::to_string(authoredTicks(minimum, s.config.fixedTickHz)) + ".."
                            + std::to_string(authoredTicks(maximum, s.config.fixedTickHz) + 1));
                }
                probe.previousFirst = s.tick; ++probe.volleyCounts[n];
            }
        }
        std::set<int> activeMuzzles;
        for (const auto& effect : s.impactEffects) if (effect.attackerObjectId == 1 && effect.renderIntent == "weaponAnim") {
            const auto found = probe.fired.find(effect.projectileActorId); if (found == probe.fired.end()) return fail("finite muzzle has no real shot cause");
            activeMuzzles.insert(effect.projectileActorId); auto& shot = found->second; const auto& a = *shot.native;
            const int elapsed = effect.totalTicks - effect.remainingTicks;
            const int expectedFrame = std::min(a.frames - 1, std::max(0, elapsed - 1) / a.delay);
            const int expectedRemaining = shot.birth.remainingTicks - (s.tick - shot.tick);
            if (effect.totalTicks != a.lifetime || effect.remainingTicks != expectedRemaining || effect.frameIndex != expectedFrame
                || effect.worldX != shot.birth.worldX || effect.worldY != shot.birth.worldY || effect.worldLevel != shot.birth.worldLevel)
                return fail("finite original muzzle restarted/moved/advanced at the wrong native clock: actor=" + std::to_string(effect.projectileActorId));
            ++timelineChecks;
            if (shot.pixelProbe && shot.checkedFrames.insert(expectedFrame).second && !drawNativeFrame(s, effect, a, expectedFrame)) return false;
            if (csv) csv << label << ',' << s.config.fixedTickHz << ',' << s.tick << ',' << effect.projectileActorId << ',' << shot.stage + 1
                << ',' << (s.gattlingWeaponTimers.contains(1) ? s.gattlingWeaponTimers.at(1) : 0) << ',' << effect.weaponId << ',' << effect.weaponSlot
                << ',' << shot.burstIndex << ',' << integer(effect.weaponId, "ROF", -1) << ',' << effect.animId << ',' << elapsed << ',' << expectedFrame
                << ',' << effect.remainingTicks << ',' << effect.totalTicks << ',' << (findObjectById(s, 2) ? findObjectById(s, 2)->health : -1) << '\n';
        }
        for (auto& [id, shot] : probe.fired) if (!shot.expired && !activeMuzzles.contains(id)) {
            if (s.tick - shot.tick < shot.birth.remainingTicks) return fail("actual finite muzzle disappeared before its original last frame");
            if (shot.pixelProbe && shot.checkedFrames.size() != static_cast<std::size_t>(shot.native->frames)) return fail("not all native muzzle frames were actually rastered");
            shot.expired = true;
        }
        return true;
    };
    int bodyBags = 0, stopChecks = 0, restoredBodyChecks = 0, deviceChecks = 0, deviceFrames = 0, retargetChecks = 0;
    const auto run = [&](const std::string& type, int rank, bool air, int hz, bool fast) {
        auto box = make(type, hz, rank, air, fast); if (!box) return false;
        auto& s = *box; RuntimeSnapshotActiveStateScope active(s); Input input{s};
        stage = "true map / original rules / real UI"; if (!openInput(input) || !inspectSamples(s)) return false;
        const auto* source = findObjectById(s, 1); const auto* target = fast ? nullptr : findObjectById(s, 2);
        const int sourceHP = integer(type, "Strength", -1), targetHP = fast ? 0 : integer(air ? "ZEP" : "YACNST", "Strength", -1);
        if (!source || source->health != sourceHP || source->veterancy != rank || !source->poweredOperational
            || runtimeNoCooldownForOwner(s, localOwner) != fast || (!fast && (!target || target->health != targetHP || target->airborne != air))
            || integer(type, "WeaponCount", 0) != 6 || integer(type, "WeaponStages", 0) != 3
            || integer(type, rank == 2 ? "EliteStage1" : "Stage1", 0) != (rank == 2 ? 100 : 200)
            || integer(type, rank == 2 ? "EliteStage2" : "Stage2", 0) != (rank == 2 ? 200 : 400)
            || integer(type, "RateUp", 0) != 1 || integer(type, "RateDown", 0) != 50)
            return fail("original source/target HP, stage thresholds, power or NoCooldown qualification invalid");
        if (fast ? !forceGround(input, {79, 80}) : !attack(input, 2)) return false;
        RunProbe firing; SoundProbe sound, ordinarySound, exactSound;
        std::unique_ptr<RuntimeWindowState> ordinary, exact; int futureActor = -1;
        const bool saveCase = type == "YTNK" && rank == 0 && !air && !fast;
        const bool deviceCase = type == "YTNK" && rank == 2 && air && hz == 30 && !fast;
        bool saved = false, ready = false;
        const auto oneTick = [&](bool fireOrders) {
            const auto pending = s.combatDetails.bursts.find(1);
            const bool hadPending = pending != s.combatDetails.bursts.end();
            const int due = hadPending ? pending->second.nextFireTick : -1;
            const std::string weapon = hadPending ? pending->second.weaponId : "";
            step(s); if (ordinary) step(*ordinary); if (exact) step(*exact);
            if (!fixtureFailure.empty()) return fail(fixtureFailure);
            if (!inspectRounds(s, firing, type, rank, air, hadPending, due, weapon, fireOrders) || !presentAudio(s, sound, false)) return false;
            if (ordinary && (!presentAudio(*ordinary, ordinarySound, true) || !compare(s, *ordinary, false, futureActor))) return false;
            if (exact && (!presentAudio(*exact, exactSound, true) || !compare(s, *exact, true, futureActor))) return false;
            return true;
        };
        stage = "full-tick original indexed stages, individual rounds and complete finite native SHP";
        const int chargeFrames = rank == 2 ? 200 : 400;
        const int budget = authoredTicks(chargeFrames + 16 * 8 + 5 * 8 + 30, hz);
        for (int frame = 0; frame < budget; ++frame) {
            if (!oneTick(true)) return false;
            source = findObjectById(s, 1); target = fast ? nullptr : findObjectById(s, 2);
            if (!source || source->health != sourceHP || source->renderMoveActive || source->x != 76 || source->y != 80
                || (!fast && (!target || target->health <= 0 || target->health > targetHP))) return fail("original passive target/source did not survive its authored firing window: " + describe(s));
            const auto burst = s.combatDetails.bursts.find(1);
            if (saveCase && !saved && firing.highestTick >= 0 && burst != s.combatDetails.bursts.end()
                && burst->second.weaponId == (rank == 2 ? "AGGattling3E" : "AGGattling3") && burst->second.nextFireTick == s.tick + 1) {
                stage = "real hot pending burst: ordinary + canonical extension";
                futureActor = s.nextProjectileActorId; ordinary = checkpoint(s, false); exact = checkpoint(s, true);
                if (!ordinary || !exact || !compare(s, *ordinary, false, futureActor) || !compare(s, *exact, true, futureActor))
                    return fail("actual backed-map hot checkpoint failed: " + error);
                // Do not pre-prime restored audio. The real next full tick must
                // discharge its purchased second round BEFORE presentation.
                if (!ordinary->gattlingAudio.sources.empty() || ordinary->gattlingAudio.primed
                    || !exact->gattlingAudio.sources.empty() || exact->gattlingAudio.primed)
                    return fail("saved local device/loop metadata leaked into authoritative restore");
                saved = true; stage = "strict real first post-load shot and hot future continuity";
            }
            const bool completeStrips = std::all_of(firing.chosen.begin(), firing.chosen.end(), [&](int id) {
                return id >= 0 && firing.fired.at(id).expired && firing.fired.at(id).checkedFrames.size() == std::size_t(firing.fired.at(id).native->frames);
            });
            if (firing.counts[0] >= 2 && firing.counts[1] >= 2 && firing.counts[2] >= 6
                && sound.body[2].size() >= 4 && firing.highestTick >= 0 && s.tick - firing.highestTick >= hz * 3
                && completeStrips && (!saveCase || saved)) { ready = true; break; }
        }
        if (!ready || (!fast && (!target || target->health >= targetHP))) return fail("real highest-stage/HP/complete raw frames/body bags incomplete: " + describe(s)
            + " shots=" + std::to_string(firing.counts[0]) + '/' + std::to_string(firing.counts[1]) + '/' + std::to_string(firing.counts[2])
            + " body3=" + std::to_string(sound.body[2].size()));
        for (int n = 0; n < 3; ++n) if (sound.intro[n] != 1 || sound.decay[n] != 0) return fail("stage change restarted per-round intro or inserted an end-of-run decay");
        bodyBags += static_cast<int>(sound.body[2].size() / 2);
        if (saveCase) {
            if (ordinarySound.body[2].empty() || exactSound.body[2].empty()) return fail("restored real first shot did not resume original body audio");
            ++restoredBodyChecks; ordinary.reset(); exact.reset();
        }
        if (type == "YAGGUN" && rank == 0 && !air && hz == 30 && !fast && !output.empty()) {
            const auto frame = drawRuntimeStateOffscreenAtCell(s, {76, 80}, 800, 600, error, output + '/' + label + "-highest.bmp", false);
            if (!frame || !frame->rendered) return fail("actual highest original model/FX capture failed"); ++captures;
        }
        stage = "presentation/audio do not change simulation authority";
        std::string hashError; const auto beforeHash = runtimeStandaloneSimulationStateHash(s, hashError);
        if (!hashError.empty()) return fail("before audio authority hash: " + hashError);
        advanceRuntimeGattlingWeaponAudio(s, GetTickCount64());
        const auto afterHash = runtimeStandaloneSimulationStateHash(s, hashError);
        if (!hashError.empty() || beforeHash != afterHash) return fail("local original voice changed authoritative simulation: " + hashError);
        if (deviceCase) {
            stage = "actual original XAudio2 queued body, master muted, real wall clock";
            s.runtimeVoiceAudio = std::make_unique<RuntimeVoiceAudio>(true); s.runtimeSoundsEnabled = true;
            s.runtimeSoundEffectsEnabled = true; s.runtimeSoundVolume = 100;
            if (!s.runtimeVoiceAudio->ready()) return fail("actual XAudio2 verification device unavailable");
            const auto handle = runtimeGattlingSoundHandle(1, 0);
            advanceRuntimeGattlingWeaponAudio(s, GetTickCount64()); advanceRuntimeGattlingWeaponAudio(s, GetTickCount64());
            const auto begin = GetTickCount64(); std::uint64_t nextTick = begin, lastSamples = 0, bindingOrder = 0;
            bool consumed = false;
            while (GetTickCount64() - begin < 2400) {
                const auto now = GetTickCount64();
                if (now >= nextTick) {
                    const auto pending = s.combatDetails.bursts.find(1); const bool hadPending = pending != s.combatDetails.bursts.end();
                    const int due = hadPending ? pending->second.nextFireTick : -1;
                    const std::string weapon = hadPending ? pending->second.weaponId : "";
                    step(s); if (!inspectRounds(s, firing, type, rank, air, hadPending, due, weapon, true)) return false;
                    nextTick += 1000 / hz;
                }
                advanceRuntimeGattlingWeaponAudio(s, now); if (!collectAudio(s, sound, false)) return false;
                const auto probe = s.runtimeVoiceAudio->probe(handle); const auto binding = s.runtimeWorldAudioBackendBindings.find(handle);
                if (!probe.available || probe.masterVolume != 0 || probe.buffersQueued < 1 || !s.runtimeVoiceAudio->isPlaying(handle).value_or(false)
                    || probe.samplesPlayed < lastSamples || binding == s.runtimeWorldAudioBackendBindings.end()
                    || binding->second.action.sourceObjectId != 1 || binding->second.action.eventId != reports[2]
                    || (bindingOrder && binding->second.playbackOrder != bindingOrder))
                    return fail("actual highest original voice dropped/reopened/stopped its queued sequence");
                if (probe.samplesPlayed > lastSamples) consumed = true;
                lastSamples = probe.samplesPlayed; bindingOrder = binding->second.playbackOrder; ++deviceFrames; Sleep(10);
            }
            if (!consumed || lastSamples < 22050 || sound.intro[2] != 1) return fail("actual device failed to consume original body continuously");
            ++deviceChecks;
        }
        stage = "real S reset, original decay once, all independent finite muzzles drain";
        const std::size_t shotsBeforeStop = firing.fired.size(); stop(input);
        if (s.gattlingWeaponTimers.contains(1) || s.combatDetails.hasPendingBurst(1)
            || (s.behaviors.contains(1) && s.behaviors.at(1).kind == RuntimeBehaviorKind::Attack)) return fail("real S did not cancel/reset the original firing run");
        advanceRuntimeGattlingWeaponAudio(s, GetTickCount64()); if (!collectAudio(s, sound, false)) return false;
        if (s.gattlingAudio.sources.contains(1) || sound.decay[2] != 1) return fail("real S did not stop its source and request exactly the original final decay");
        // Guard can legitimately acquire another nearby passive enemy. Once
        // real S has independently reset/cancelled and emitted decay, this
        // quiet single-run fixture prevents a new, unrelated Guard fire run.
        findMutableObjectById(s, 1)->mission = "Sleep";
        const std::uint64_t decayStart = GetTickCount64();
        const int drain = authoredTicks(22, hz) + hz;
        for (int t = 0; t < drain; ++t) if (!oneTick(false)) return false;
        if (firing.fired.size() != shotsBeforeStop || std::any_of(firing.fired.begin(), firing.fired.end(), [](const auto& entry) { return !entry.second.expired; }))
            return fail("stop manufactured rounds or failed to drain every finite original muzzle");
        if (deviceCase) {
            const auto handle = runtimeGattlingSoundHandle(1, 1);
            const auto deadline = sampleMilliseconds.at(stockSamples[2].back()) + 500;
            while (GetTickCount64() - decayStart <= deadline && s.runtimeVoiceAudio->isPlaying(handle).value_or(false)) {
                advanceRuntimeGattlingWeaponAudio(s, GetTickCount64()); reapRuntimeWorldAudioBackendBindings(s); Sleep(10);
            }
            reapRuntimeWorldAudioBackendBindings(s);
            if (s.runtimeVoiceAudio->isPlaying(runtimeGattlingSoundHandle(1, 0)).value_or(false)
                || s.runtimeVoiceAudio->isPlaying(handle).value_or(false)
                || s.runtimeWorldAudioBackendBindings.contains(runtimeGattlingSoundHandle(1, 0)) || s.runtimeWorldAudioBackendBindings.contains(handle))
                return fail("original finite decay did not naturally finish on the actual device");
            s.runtimeSoundsEnabled = false;
        }
        ++stopChecks;
        // A new real target/order starts at the original first stage while its
        // paid cooldown is retained. No timer/reload/source pose is rewritten.
        stage = "actual new target restarts original stage once, retains paid reload";
        const int reload = findObjectById(s, 1)->weaponCooldownTicksRemaining;
        SoundProbe fresh; fresh.seen = sound.seen;
        if (fast ? !forceGround(input, {76, 84}) : !attack(input, 3)) return false;
        if (s.gattlingWeaponTimers.contains(1) || findObjectById(s, 1)->weaponCooldownTicksRemaining != reload)
            return fail("new WM target bypassed reload or retained highest heat");
        const int nextActor = s.nextProjectileActorId; bool newShot = false;
        for (int t = 0; t < authoredTicks(24, hz) + hz; ++t) {
            step(s); if (!presentAudio(s, fresh, false)) return false;
            const auto inspect = [&](const auto& container) { for (const auto& shot : container) if (shot.attackerObjectId == 1 && shot.actorId >= nextActor) {
                if (shot.weaponId != originalRules.getValue(type, std::string(rank == 2 ? "EliteWeapon" : "Weapon") + (air ? "2" : "1"), "")) return false;
                newShot = true;
            } return true; };
            if (!inspect(s.projectiles) || !inspect(s.recentExpiredProjectiles)) return fail("actual new target fired a stale highest-stage weapon");
            if (newShot && fresh.intro[0] == 1 && !fresh.body[0].empty()) break;
        }
        if (!newShot || fresh.intro[0] != 1 || fresh.body[0].empty()) return fail("new actual first stage did not create one intro plus body");
        if (type == "YTNK" && fast && hz == 30) { if (!move(input, {72, 80})) return false; }
        else stop(input);
        advanceRuntimeGattlingWeaponAudio(s, GetTickCount64()); if (!collectAudio(s, fresh, false)) return false;
        if (s.gattlingAudio.sources.contains(1) || fresh.decay[0] != 1) return fail("real replacement Move/S lost its original first-stage decay");
        findMutableObjectById(s, 1)->mission = "Sleep"; ++retargetChecks;
        std::cout << "Gattling continuity " << label << " PASS shots=" << firing.counts[0] << '/' << firing.counts[1] << '/' << firing.counts[2]
            << " nativeFrames=" << firing.fired.at(firing.chosen[0]).checkedFrames.size() << '/'
            << firing.fired.at(firing.chosen[1]).checkedFrames.size() << '/' << firing.fired.at(firing.chosen[2]).checkedFrames.size()
            << " body3=" << sound.body[2].size() << " saved=" << saved << " actualDevice=" << deviceCase << '\n';
        return true;
    };
    const auto attempt = [&](const std::string& type, int rank, bool air, int hz, bool fast) {
        label = type + '-' + std::to_string(hz) + "-rank" + std::to_string(rank) + (fast ? "-forceground-NoCooldown" : air ? "-air" : "-ground");
        if (!filter.empty() && filter != label) return;
        ++scenes; fixtureFailure.clear(); error.clear();
        if (!run(type, rank, air, hz, fast)) { failures.push_back(error); std::cout << "FAIL " << error << '\n'; }
    };
    for (const std::string type : {"YTNK", "YAGGUN"}) for (int rank : {0, 2}) for (bool air : {false, true}) for (int hz : {30, 60}) attempt(type, rank, air, hz, false);
    for (const std::string type : {"YTNK", "YAGGUN"}) for (int hz : {30, 60}) attempt(type, 0, false, hz, true);
    if (!scenes) return fail("case filter selected no actual scene");
    if (!failures.empty()) { std::ostringstream text; text << failures.size() << '/' << scenes << " Gattling continuity failures";
        for (const auto& failure : failures) text << '\n' << failure; error = text.str(); return false; }
    std::cout << "Gattling continuous fire passed " << scenes << " scenes; fullTicks=" << fullTicks << " rounds=" << rounds
        << " finiteTimeline=" << timelineChecks << " nativeSHP=" << nativePixelChecks << " bodies=" << bodyBags << " Stop=" << stopChecks
        << " newTarget=" << retargetChecks << " ordinary+canonical=" << ordinarySaves << '+' << exactSaves << " paired=" << pairedTicks
        << " restoredBody=" << restoredBodyChecks << " actualDevice=" << deviceChecks << " probes=" << deviceFrames << " BMP=" << captures
        << "; native finite animation/control and engine 15Hz clock policy; device proof uses real wall time, not desktop audio latency\n";
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
