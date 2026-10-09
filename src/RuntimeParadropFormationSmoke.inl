// Retail unload alternates +/- 0x3FFF after popping a passenger. The half-cell
// point is a placement preference, not a second visual offset. This host uses
// a continuous world-angle adaptation of the native float lookup/integer
// coordinates; the oracle deliberately does not claim native lepton bit parity.
// Native legal infantry slots are 2/4/3, with one quarter-cell body/chute offset.
bool runRuntimeParadropFormationSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    IniFile stock, stockArt; RulesDatabase rules; ArtDatabase art;
    if (!stock.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(stock)
        || !stockArt.loadFromFile(live.report.artPath) || !art.loadFromIni(stockArt)) {
        error = "Paradrop formation: original rules/art unavailable"; return false;
    }
    const auto* american = rules.findSuperWeaponType("AmericanParaDropSpecial");
    const auto* generic = rules.findSuperWeaponType("ParaDropSpecial");
    const auto* planeType = rules.findUnitType("AircraftType", "PDPLANE");
    if (!american || !generic || !planeType || american->planeType != "PDPLANE"
        || generic->planeType != "PDPLANE" || american->parachuteAnim != "PARACH"
        || generic->parachuteAnim != "PARACH" || planeType->speed != 15 || planeType->strength != 400
        || american->paradropRadiusLeptons != 1024 || generic->paradropRadiusLeptons != 1024
        || american->supportFlightLevelLeptons != 1500 || generic->supportFlightLevelLeptons != 1500
        || american->parachuteFallRate != 3 || generic->parachuteFallRate != 3)
        { error = "Paradrop formation: original aircraft/parachute parameters changed"; return false; }
    for (const auto& pair : {std::pair{"AmerParaDropInf", "E1"}, {"AmerParaDropNum", "8"},
            {"AllyParaDropInf", "E1"}, {"AllyParaDropNum", "6"},
            {"SovParaDropInf", "E2"}, {"SovParaDropNum", "9"},
            {"YuriParaDropInf", "INIT"}, {"YuriParaDropNum", "6"}})
        if (stock.getValue("General", pair.first, "") != pair.second)
            { error = "Paradrop formation: original manifest changed " + std::string(pair.first); return false; }
    const char* outputValue = std::getenv("CONCORD_PARADROP_FORMATION_OUTPUT");
    const char* filterValue = std::getenv("CONCORD_PARADROP_FORMATION_CASE");
    const std::string outputRoot = outputValue ? outputValue : "", filter = filterValue ? filterValue : "";
    if (!outputRoot.empty()) std::filesystem::create_directories(outputRoot);
    int landTile = -1; auto metadataCache = std::make_unique<RuntimeMapRenderCache>();
    const auto terrainMetadata = runtimeTerrainMetadataContext(live);
    for (int tile = 0; tile < static_cast<int>(live.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(live, cell);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge
            && resolveRuntimeTerrainSubTileRampType(cell, terrainMetadata, *metadataCache) == 0)
            { landTile = tile; break; }
    }
    if (landTile < 0) { error = "Paradrop formation: original flat land TMP unavailable"; return false; }
    constexpr int inputWidth = 1280, inputHeight = 720;
    const RECT client{0, 0, inputWidth, inputHeight}, viewport = runtimeMapViewport(client), sidebar = runtimeSidebarRect(client);
    std::string label, stage, fixtureFailure; std::vector<std::string> failures;
    int scenes = 0, uiClicks = 0, fullTicks = 0, births = 0, fallChecks = 0, groundChecks = 0;
    int slotChecks = 0, landingChecks = 0, ordinarySaves = 0, exactSaves = 0, pairedTicks = 0;
    int nativeRasterChecks = 0, captures = 0, fallbackDrops = 0;
    const auto fail = [&](const std::string& why) { error = "Paradrop " + label + " [" + stage + "]: " + why; return false; };
    const auto scene = [&](const std::string& name, const auto& body) {
        if (!filter.empty() && name != filter) return;
        label = name; stage = "original fixture"; fixtureFailure.clear();
        const bool passed = body();
        if (passed && fixtureFailure.empty()) { ++scenes; std::cout << "Paradrop " << name << " PASS\n"; }
        else { if (!fixtureFailure.empty()) fail(fixtureFailure); failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    };
    const auto structure = [&](RuntimeWindowState& s, int id, const std::string& type,
            RuntimeCellPoint cell, const std::string& owner = "Commander") {
        WorldObjectInstance o; o.instanceId = id; o.mapId = "Paradrop" + std::to_string(id);
        o.kind = WorldObjectKind::Structure; o.type = type; o.owner = owner;
        o.x = cell.x; o.y = cell.y; o.cell = runtimeCellKey(o.x, o.y);
        o.health = runtimeMaxHealthForObject(s, o); o.alive = o.poweredOperational = true;
        o.mission = "Sleep"; o.currentAmmo = -1; s.world.accessObjects().push_back(o);
    };
    const auto make = [&](int hz, const std::string& country, bool blocked) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        seedRuntimeLoadScratch(s, live, false); s.config = live.config; s.config.fixedTickHz = hz;
        s.config.gameMode = "Battle"; s.config.playerCountry = country; s.config.techLevel = 10;
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.noCooldown = s.config.shortGame = false;
        s.config.crates = false; s.config.superweapons = true; s.config.startingUnits = 0;
        s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.rules = rules; s.art = art; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.mapFacts = {}; s.mapFacts.theater = live.mapFacts.theater; s.mapFacts.lighting = live.mapFacts.lighting;
        s.report.map.size.clear(); s.report.map.localSize.clear();
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y = 55; y <= 124; ++y) for (int x = 55; x <= 124; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = landTile; cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        structure(s, 1, "GAAIRC", {63, 64}); structure(s, 2, "CAAIRP", {69, 64});
        structure(s, 3, "NANRCT", {63, 75}); structure(s, 998, "NANRCT", {56, 56}, "SkirmishPlayer2");
        // A real original footprint rejects the first preferred cell. It is
        // authored before startup, never moved under a falling soldier.
        if (blocked) structure(s, 4, "GAPOWR", {78, 90});
        s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner = "Commander"; s.economy.credits = 20000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        RuntimeConfig::PlayerSlot local; local.enabled = local.human = local.local = true;
        local.name = "Commander"; local.country = country; local.color = "Gold";
        RuntimeConfig::PlayerSlot enemy; enemy.enabled = true; enemy.country = "Russians"; enemy.color = "Red";
        s.config.playerSlots = {local, enemy};
        s.skirmishCountryByOwner.clear(); s.skirmishTeamByOwner.clear(); s.playerCommands.alliances.clear();
        s.skirmishCountryByOwner["commander"] = country; s.skirmishCountryByOwner["skirmishplayer2"] = "Russians";
        s.humanHouseAuthorities.clear(); RuntimeHumanHouseAuthority house;
        house.owner = "Commander"; house.canonicalOwner = "commander"; house.country = country; house.localFacade = true;
        s.humanHouseAuthorities.push_back(house);
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        ensureRuntimeSuperWeaponsInitialized(s); initializeRuntimeOfflineGameplayRouting(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s);
            auto context = runtimeSuperWeaponContext(s); context.emit = runtimeSuperWeaponEventSink(s);
            s.superWeapons.tick(context); completeRuntimeWorldRefresh(s); }
        rebuildRuntimeEconomyPower(s); return storage;
    };
    const auto step = [&](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s); ++fullTicks;
        if (s.skirmishMatchFinished && fixtureFailure.empty()) fixtureFailure = "Battle ended tick="
            + std::to_string(s.tick) + " result=" + s.skirmishResult;
    };
    struct Input {
        RuntimeWindowState& state; HWND window = nullptr; WNDPROC original = nullptr;
        ~Input() { if (window) { SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            if (original) SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
            if (GetCapture() == window) ReleaseCapture(); releaseRuntimeBackBuffer(state.backBuffer); DestroyWindow(window); } }
    };
    const auto openInput = [&](Input& input) {
        input.window = CreateWindowExW(0, L"STATIC", L"Paradrop real sidebar/map", WS_POPUP,
            0, 0, inputWidth, inputHeight, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!input.window) return fail("Win32 input window unavailable");
        SetWindowLongPtrW(input.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&input.state));
        input.original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(input.window, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(runtimeWindowProc)));
        return input.original ? true : fail("Win32 input procedure unavailable");
    };
    const auto left = [&](Input& input, POINT point) {
        SendMessageW(input.window, WM_MOUSEMOVE, 0, MAKELPARAM(point.x, point.y));
        SendMessageW(input.window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
        SendMessageW(input.window, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y)); ++uiClicks;
    };
    const auto cast = [&](Input& input, const std::string& id, RuntimeCellPoint target) {
        auto& s = input.state; RuntimeSnapshotActiveStateScope active(s);
        auto context = runtimeSuperWeaponContext(s); context.emit = runtimeSuperWeaponEventSink(s);
        // Test setup readies the original grant. Target selection and delivery
        // are still submitted by the actual Win32 sidebar/map command path.
        if (!s.superWeapons.forceReady("Commander", id, &context)) return fail("original source cannot ready " + id);
        step(s); s.activeBuildTab = RuntimeBuildKind::Defense; std::optional<POINT> button;
        for (int page = 0; page < runtimeBuildPageCount(s, sidebar) && !button; ++page) {
            runtimeSetActiveBuildPage(s, page); const int offset = static_cast<int>(runtimeVisibleBuildCatalogIndices(s, sidebar).size());
            const auto visible = runtimeVisibleSuperWeaponsOnBuildPage(s, sidebar);
            for (std::size_t n = 0; n < visible.size(); ++n) if (visible[n]->superWeaponId == id) {
                const auto r = runtimeBuildButtonRect(sidebar, offset + static_cast<int>(n));
                button = POINT{(r.left + r.right) / 2, (r.top + r.bottom) / 2}; break;
            }
        }
        if (!button) return fail("original power missing from actual sidebar " + id);
        left(input, *button); const auto& intent = s.multiplayerIntentRouter.superWeapon();
        if (!intent || intent->superWeaponId != id) return fail("sidebar did not select power: " + s.interactionStatus);
        centerRuntimeCameraOnCell(s, client, target); ensureRuntimeCameraForViewport(s, viewport);
        const POINT projected = runtimeScreenPointForCell(s, viewport, target, terrainLevelAtRuntimeCell(s, target.x, target.y));
        std::optional<POINT> point;
        for (int dy = -8; dy <= 16 && !point; ++dy) for (int dx = -4; dx <= 4 && !point; ++dx) {
            const POINT p{projected.x + dx, projected.y + dy}; const POINT actual = approximateCellFromScreenPoint(s, viewport, p);
            if (p.x >= viewport.left && p.x < viewport.right && p.y >= viewport.top && p.y < viewport.bottom
                && actual.x == target.x && actual.y == target.y) point = p;
        }
        if (!point) return fail("native surface picker cannot reach actual target");
        left(input, *point);
        if (s.multiplayerIntentRouter.superWeapon()) return fail("actual map cast rejected: " + s.interactionStatus);
        return true;
    };
    const auto checkpoint = [&](RuntimeWindowState& s, bool exact) {
        std::ostringstream base, extension, canonical; IniFile baseIni, exactIni;
        if (!writeRuntimeStateToStream(s, base, error) || !baseIni.loadFromString(base.str())) return std::unique_ptr<RuntimeWindowState>{};
        if (exact && (!writeRuntimeStateToStream(s, extension, error)
            || !writeRuntimeMultiplayerSnapshotExtension(s, extension, error) || !exactIni.loadFromString(extension.str())
            || !writeRuntimeMultiplayerSnapshotCanonicalIni(exactIni, canonical, error)
            || !exactIni.loadFromString(canonical.str()))) return std::unique_ptr<RuntimeWindowState>{};
        auto result = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*result, s, false);
        // Canonical v14 text omits AV authority: restore its ordinary base,
        // then the real canonical extension, without relaxing load validation.
        if (!materializeRuntimeLoadStateFromIni(*result, baseIni, error)
            || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*result, exactIni, error))) return std::unique_ptr<RuntimeWindowState>{};
        initializeRuntimeOfflineGameplayRouting(*result); return result;
    };
    const auto describe = [](const WorldObjectInstance& o) {
        std::ostringstream text; text << std::setprecision(17) << o.instanceId << '/' << o.type << " cell=" << o.x << ',' << o.y
            << " slot=" << o.subCell << " XYZ=" << runtimeObjectRenderX(o) << ',' << runtimeObjectRenderY(o) << ',' << o.renderLevel
            << " level=" << o.level << " flight=" << o.flightLevel << " air=" << o.airborne << " move=" << o.renderMoveActive
            << " hp=" << o.health << " mission=" << o.mission << " distance=" << o.movementAnimationDistance; return text.str();
    };
    const auto relevant = [](const WorldObjectInstance& o) {
        return o.owner == "Commander" && (o.type == "PDPLANE" || o.kind == WorldObjectKind::Infantry);
    };
    const auto compare = [&](const RuntimeWindowState& s, const RuntimeWindowState& other, const std::string& kind) {
        if (s.tick != other.tick || s.superWeapons.stateHash() != other.superWeapons.stateHash())
            return fail(kind + " changed complete support authority/RNG/next drop clock tick=" + std::to_string(s.tick));
        std::size_t count = 0, counterpart = 0;
        for (const auto& o : s.world.getObjects()) if (relevant(o)) {
            ++count; const auto* p = findRuntimeObjectByIdIncludingInactive(other, o.instanceId);
            if (!p || !relevant(*p) || o.alive != p->alive || o.health != p->health || o.type != p->type
                || o.owner != p->owner || o.x != p->x || o.y != p->y || o.cell != p->cell || o.subCell != p->subCell
                || o.level != p->level || o.flightLevel != p->flightLevel || o.airborne != p->airborne || o.facing != p->facing
                || o.mission != p->mission || o.hasRenderPosition != p->hasRenderPosition || o.renderMoveActive != p->renderMoveActive
                || runtimeObjectRenderX(o) != runtimeObjectRenderX(*p) || runtimeObjectRenderY(o) != runtimeObjectRenderY(*p)
                || o.renderLevel != p->renderLevel || o.movementAnimationDistance != p->movementAnimationDistance)
                return fail(kind + " changed live/future child/plane pose: source=" + describe(o)
                    + " restored=" + (p ? describe(*p) : "missing"));
        }
        for (const auto& o : other.world.getObjects()) if (relevant(o)) ++counterpart;
        if (count != counterpart) return fail(kind + " changed future manifest or allocated an extra object");
        ++pairedTicks; return true;
    };
    const auto moduleFields = [&](const RuntimeWindowState& s, const std::string& power) {
        std::ostringstream text; s.superWeapons.save(text); IniFile ini; ini.loadFromString(text.str());
        std::pair<int, std::string> result{-1, ""};
        for (const auto& section : ini.getSectionNames()) if (section.starts_with("SuperWeapon.")
            && ini.getValue(section, "Owner", "") == "Commander" && ini.getValue(section, "Definition", "") == power) {
            result.first = std::stoi(ini.getValue(section, "TargetStage", "-1"));
            result.second = ini.getValue(section, "RngState", ""); break;
        }
        return result;
    };
    const auto offset = [](int slot) -> std::pair<double, double> {
        switch (slot) { case 2: return {.25, -.25}; case 4: return {.25, .25}; case 3: return {-.25, .25}; default: return {0, 0}; }
    };
    // Draw the actual native infantry and canopy together, and compare with
    // a center-slot render-only copy shifted by the independent original slot
    // table. 120x60 projects all quarter-cell offsets to integer pixels. This
    // proves both layers receive the offset once; no new placement helper is
    // used as a pixel oracle and no simulation object is changed.
    const auto nativeRaster = [&](const RuntimeWindowState& s, const WorldObjectInstance& soldier) {
        constexpr int width = 640, height = 480; constexpr std::uint32_t bg = 0x001d2935;
        const RECT bounds{0, 0, width, height}; auto cache = std::make_unique<RuntimeMapRenderCache>();
        GameWorld isolated = s.world; isolated.accessObjects().clear(); isolated.accessTerrainCells().clear();
        isolated.accessTerrainObjects().clear(); isolated.accessSmudges().clear();
        RuntimeMapRenderContext c; c.rules = &s.rules; c.art = &s.art; c.theater = &s.theater;
        c.assetRootPath = runtimeAssetRootPath(s.config); c.palettePath = runtimePalettePath(s.config, s.report.map);
        c.structurePalettePath = runtimeStructurePalettePath(s.config, s.report.map); c.ownerRemapColors = &s.ownerRemapColors;
        c.animationTick = s.tick; c.fixedTickHz = s.config.fixedTickHz; c.surfaceWidth = width; c.surfaceHeight = height;
        c.shadowsEnabled = false; c.useTileTextures = false; c.effectOnlyProbe = false;
        RuntimeMapCamera camera; camera.initialized = true; camera.tileWidth = 120; camera.tileHeight = 60;
        const auto projected = runtimeProjectCellXY(runtimeObjectRenderX(soldier), runtimeObjectRenderY(soldier), soldier.renderLevel, camera);
        camera.worldOffsetX = projected.x - width / 2; camera.worldOffsetY = projected.y - height / 2 - 35;
        std::vector<std::uint32_t> actual(width * height, bg), centered(width * height, bg), blank(width * height, bg);
        auto render = [&](const std::optional<WorldObjectInstance>& body, std::vector<std::uint32_t>& pixels) {
            isolated.accessObjects().clear(); if (body) isolated.accessObjects().push_back(*body);
            c.surfacePixels = pixels.data(); auto fixed = camera; return drawRuntimeMap(nullptr, bounds, isolated, c, fixed, *cache);
        };
        const auto empty = render(std::nullopt, blank); const auto drawn = render(soldier, actual);
        auto reference = soldier; reference.subCell = 0; const auto centerDrawn = render(reference, centered);
        if (!empty.rendered || !drawn.rendered || !centerDrawn.rendered || drawn.parachuteSprites != 1
            || centerDrawn.parachuteSprites != 1 || drawn.parachuteMissing || drawn.missingObjectShpSprites
            || drawn.visibleObjectShpSprites <= 0) return fail("actual native infantry/PARACH raster unavailable");
        const auto slot = offset(soldier.subCell);
        const int dx = static_cast<int>(std::lround((slot.first - slot.second) * 60));
        const int dy = static_cast<int>(std::lround((slot.first + slot.second) * 30));
        int actualPixels = 0, referencePixels = 0;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const int i = y * width + x;
            if ((actual[i] & 0xffffffU) != (blank[i] & 0xffffffU)) ++actualPixels;
            if ((centered[i] & 0xffffffU) != (blank[i] & 0xffffffU)) {
                ++referencePixels; const int tx = x + dx, ty = y + dy;
                if (tx <= 0 || ty <= 0 || tx >= width - 1 || ty >= height - 1
                    || (actual[ty * width + tx] & 0xffffffU) != (centered[i] & 0xffffffU))
                    return fail("body/canopy did not share one original quarter-slot translation " + describe(soldier));
            }
        }
        if (actualPixels < 32 || actualPixels != referencePixels)
            return fail("native body/canopy clipped or received duplicate slot offset pixels=" + std::to_string(actualPixels)
                + '/' + std::to_string(referencePixels));
        ++nativeRasterChecks; return true;
    };
    struct Case { const char* name; const char* country; const char* power; const char* infantry; int count, hz, direction; bool blocked; };
    const std::array<Case, 8> cases{{
        {"USA-east-30", "Americans", "AmericanParaDropSpecial", "E1", 8, 30, 1, false},
        {"USA-west-30", "Americans", "AmericanParaDropSpecial", "E1", 8, 30, -1, false},
        {"USA-east-60", "Americans", "AmericanParaDropSpecial", "E1", 8, 60, 1, false},
        {"USA-west-60", "Americans", "AmericanParaDropSpecial", "E1", 8, 60, -1, false},
        {"Ally-east-30", "British", "ParaDropSpecial", "E1", 6, 30, 1, false},
        {"Soviet-west-30", "Russians", "ParaDropSpecial", "E2", 9, 30, -1, false},
        {"Yuri-east-60", "YuriCountry", "ParaDropSpecial", "INIT", 6, 60, 1, false},
        {"USA-east-30-blocked", "Americans", "AmericanParaDropSpecial", "E1", 8, 30, 1, true}
    }};
    for (const auto test : cases) scene(test.name, [&]() {
        auto storage = make(test.hz, test.country, test.blocked); auto& s = *storage;
        RuntimeSnapshotActiveStateScope active(s); Input input{s}; if (!openInput(input)) return false;
        const auto* infantry = s.rules.findUnitType("InfantryType", test.infantry);
        const auto* rule = s.rules.findSuperWeaponType(test.power);
        if (!infantry || !rule) return fail("original infantry/power unavailable");
        const RuntimeCellPoint target{test.direction > 0 ? 82 : 108, 90};
        if (!cast(input, test.power, target)) return false;
        struct Born { int id, tick, cellX, cellY, slot; double bodyX, bodyY, initialZ; bool landed = false; };
        std::vector<Born> children; std::map<int, WorldObjectInstance> previous;
        int planeId = -1, launchTick = -1, lastBirthTick = -1; double launchX = 0, previousPlaneX = 0;
        bool planeSeen = false, departed = false, saved = false, raster = false, airCapture = false, groundCapture = false;
        std::string unchangedRng; std::unique_ptr<RuntimeWindowState> ordinary, exact;
        const double root6 = std::sqrt(6.0), fall = 3.0 * 15.0 / test.hz * root6 / 256.0;
        const double cruise = 1500.0 * root6 / 256.0;
        const int interval = std::max(1, test.hz / 15);
        // Original 1500-lepton height takes 1500/(3*15)=33 1/3
        // seconds to descend, in addition to ingress and the last passenger's
        // unload time. Budget those separately; two launch ticks and one
        // native frame cover clock alignment/final floating-point clipping.
        const double ingressEdge = test.direction > 0 ? 53.0 : 126.0;
        const double beforeDrop = std::max(0.0, std::abs(target.x - ingressEdge) - rule->paradropRadiusLeptons / 256.0);
        const int ingressTicks = static_cast<int>(std::ceil(beforeDrop * test.hz / planeType->speed));
        const int descentTicks = static_cast<int>(std::ceil(rule->supportFlightLevelLeptons
            * double(test.hz) / (rule->parachuteFallRate * 15.0)));
        const int deadlineTicks = 2 + ingressTicks + (test.count - 1) * interval + descentTicks + interval;
        const bool saveCase = !test.blocked && std::string(test.name) == (test.hz == 30 ? "USA-east-30" : "USA-west-60");
        const bool pictureCase = !test.blocked && test.hz == 30 && std::string(test.country) == "Americans";
        for (int frame = 0; frame < deadlineTicks; ++frame) {
            stage = "complete flight/drop/descent tick " + std::to_string(s.tick + 1);
            previous.clear(); for (const auto& o : s.world.getObjects()) if (relevant(o)) previous.emplace(o.instanceId, o);
            step(s); if (!fixtureFailure.empty()) return false;
            if (ordinary) { step(*ordinary); step(*exact);
                if (!compare(s, *ordinary, "ordinary future") || !compare(s, *exact, "canonical future")) return false; }
            const WorldObjectInstance* plane = nullptr;
            for (const auto& o : s.world.getObjects()) if (o.type == "PDPLANE" && o.owner == "Commander" && o.alive) {
                if (plane) return fail("actual single cast created two support aircraft"); plane = &o;
            }
            if (plane) {
                if (!planeSeen) {
                    planeSeen = true; planeId = plane->instanceId; launchTick = s.tick; launchX = plane->renderX;
                    if (plane->mission != (test.direction > 0 ? "SupportFlightEast" : "SupportFlightWest")
                        || plane->renderX != (test.direction > 0 ? 53.0 : 126.0)) return fail("actual original ingress direction/edge changed");
                } else if (std::abs(plane->renderX - previousPlaneX - test.direction * 15.0 / test.hz) > 1e-12)
                    return fail("original PDPLANE speed changed: " + describe(*plane));
                if (!plane->airborne || !plane->hasRenderPosition || !plane->renderMoveActive
                    || plane->health != planeType->strength || plane->renderY != target.y
                    || std::abs(plane->renderLevel - cruise) > 1e-12) return fail("original plane pose/height/HP changed");
                previousPlaneX = plane->renderX;
            } else if (planeSeen) departed = true;
            int newborns = 0;
            for (const auto& o : s.world.getObjects()) if (o.kind == WorldObjectKind::Infantry && o.owner == "Commander") {
                if (!o.alive || o.type != test.infantry || o.health != infantry->strength) return fail("manifest infantry type/native HP changed");
                const auto old = previous.find(o.instanceId);
                if (old != previous.end()) continue;
                if (!plane || ++newborns != 1 || children.size() >= static_cast<std::size_t>(test.count))
                    return fail("unexpected birth multiplicity or original aircraft missing");
                const int ordinal = static_cast<int>(children.size()), remaining = test.count - ordinal - 1;
                // Independent native constant and original slot table; the
                // current plane's post-tick pose is its actual unload pose.
                const double angle = (remaining % 2 ? -1.0 : 1.0) * (16383.0 / 65536.0) * (2.0 * std::acos(-1.0));
                const double px = plane->renderX + test.direction * .5 * std::cos(angle);
                const double py = plane->renderY + test.direction * .5 * std::sin(angle);
                const RuntimeCellPoint preferred{static_cast<int>(std::lround(px)), static_cast<int>(std::lround(py))};
                RuntimeCellPoint selected{-1, -1}; int selectedSlot = -1;
                for (int radius = 0; radius <= 4 && selectedSlot < 0; ++radius)
                    for (int dy = -radius; dy <= radius && selectedSlot < 0; ++dy) for (int dx = -radius; dx <= radius; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
                        const RuntimeCellPoint cell{preferred.x + dx, preferred.y + dy};
                        if (cell.x < 55 || cell.x > 124 || cell.y < 55 || cell.y > 124
                            || !runtimeSupportPowerCanPlace(s, o, cell.x, cell.y)) continue;
                        bool occupied = false;
                        for (const auto& resident : s.world.getObjects()) if (resident.alive
                            && resident.kind != WorldObjectKind::Structure && resident.instanceId != planeId
                            && resident.instanceId != o.instanceId && resident.x == cell.x && resident.y == cell.y) occupied = true;
                        if (occupied) continue;
                        const auto available = s.subcells.availableSubCells(s.world.getObjects(), s.world.getTerrainObjects(),
                            runtimeSubcellLocationForCell(s, cell), {o.instanceId});
                        double best = std::numeric_limits<double>::infinity();
                        for (int slot : {2, 4, 3}) if (std::find(available.begin(), available.end(), slot) != available.end()) {
                            const auto d = offset(slot); const double distance = (cell.x + d.first - px) * (cell.x + d.first - px)
                                + (cell.y + d.second - py) * (cell.y + d.second - py);
                            if (distance < best) { best = distance; selected = cell; selectedSlot = slot; }
                        }
                        if (selectedSlot >= 0) break;
                    }
                const auto d = offset(selectedSlot);
                if (selectedSlot < 0 || o.x != selected.x || o.y != selected.y || o.subCell != selectedSlot
                    || o.cell != runtimeCellKey(o.x, o.y) || o.renderX != o.x || o.renderY != o.y
                    || !o.airborne || !o.hasRenderPosition || o.mission != "ParadropDrop"
                    || std::abs(o.renderLevel - (plane->renderLevel - fall)) > 1e-12)
                    return fail("preferred search/native nearest slot or first falling pose differs ordinal=" + std::to_string(ordinal)
                        + " preferred=" + std::to_string(px) + ',' + std::to_string(py) + " expected cell/slot="
                        + std::to_string(selected.x) + ',' + std::to_string(selected.y) + '/' + std::to_string(selectedSlot)
                        + " actual=" + describe(o));
                if (!test.blocked) {
                    if (selected != preferred || std::abs(o.y + d.second - plane->renderY) > .75
                        || (ordinal && (children.back().bodyY - target.y) * (o.y + d.second - target.y) >= 0))
                        return fail("unobstructed formation lost its alternating small lateral offset");
                    if (lastBirthTick >= 0 && s.tick - lastBirthTick != interval)
                        return fail("original one-rule-frame unload interval changed");
                } else if (selected != preferred) { ++fallbackDrops; }
                if (std::abs(plane->renderX - target.x) > 4.0 + 1e-12) return fail("drop outside original 1024-lepton radius");
                children.push_back({o.instanceId, s.tick, o.x, o.y, o.subCell, o.x + d.first, o.y + d.second, o.renderLevel});
                lastBirthTick = s.tick; ++births; ++slotChecks;
            }
            const auto fields = moduleFields(s, test.power);
            if (planeSeen && unchangedRng.empty()) unchangedRng = fields.second;
            if (planeSeen && (fields.second.empty() || fields.second != unchangedRng)) return fail("deterministic lateral placement consumed support RNG");
            if (fields.first != static_cast<int>(children.size())) return fail("native unload stage consumed an unplaced passenger");
            int landed = 0;
            for (auto& born : children) {
                const auto* o = findObjectById(s, born.id); if (!o) return fail("native passenger disappeared before/after landing");
                const auto d = offset(o->subCell); const int ground = terrainLevelAtRuntimeCell(s, o->x, o->y);
                if (o->x != born.cellX || o->y != born.cellY || o->subCell != born.slot
                    || runtimeObjectRenderX(*o) + d.first != born.bodyX || runtimeObjectRenderY(*o) + d.second != born.bodyY)
                    return fail("body/canopy horizontally snapped or changed reserved native slot: " + describe(*o));
                const auto old = previous.find(o->instanceId);
                if (old != previous.end() && old->second.airborne) {
                    const double expected = std::max(double(ground), old->second.renderLevel - fall);
                    if (std::abs(o->renderLevel - expected) > 1e-12
                        || std::abs(o->movementAnimationDistance - old->second.movementAnimationDistance
                            - (old->second.renderLevel - expected)) > 1e-12)
                        return fail("original 15-Hz ParachuteMaxFallRate/native animation clock changed: " + describe(*o));
                    ++fallChecks;
                }
                if (o->airborne) {
                    if (o->mission != "ParadropDrop" || o->level != std::lround(o->renderLevel)
                        || o->flightLevel != o->level || runtimeObjectCanMove(s, *o)) return fail("falling infantry acquired ground movement/level");
                } else {
                    if (o->level != ground || o->flightLevel != 0 || !runtimeObjectCanMove(s, *o)
                        || !runtimeSupportPowerCanPlace(s, *o, o->x, o->y)) return fail("landed infantry lacks legal original ground capabilities");
                    if (!born.landed) { born.landed = true; ++landingChecks; }
                    ++landed; ++groundChecks;
                }
            }
            if (saveCase && !saved && children.size() == 2) {
                stage = "stage2 airborne ordinary/canonical future checkpoint";
                ordinary = checkpoint(s, false); exact = checkpoint(s, true);
                if (!ordinary || !exact) return fail("full checkpoint restore: " + error);
                if (!compare(s, *ordinary, "ordinary checkpoint") || !compare(s, *exact, "canonical checkpoint")) return false;
                saved = true; ++ordinarySaves; ++exactSaves;
            }
            if (!raster && children.size() == static_cast<std::size_t>(test.count)) {
                stage = "original SHP infantry and PARACH share one native slot";
                // Both parity sides are independently exercised, with the
                // same native frame and height in each render-only comparison.
                for (int n : {0, 1}) if (!nativeRaster(s, *findObjectById(s, children[n].id))) return false;
                raster = true;
            }
            if (pictureCase && !outputRoot.empty() && !airCapture && children.size() == static_cast<std::size_t>(test.count)) {
                std::string captureError; const auto stats = drawRuntimeStateOffscreenAtCell(s, target, 1000, 720, captureError,
                    outputRoot + '/' + label + "-airborne.bmp", false, false, -1, nullptr, false, nullptr, false, false);
                if (!stats || stats->parachuteSprites < test.count || stats->parachuteMissing || stats->missingObjectShpSprites)
                    return fail("actual full-world airborne native SHP capture: " + captureError);
                airCapture = true; ++captures;
            }
            if (departed && landed == test.count) {
                if (!planeSeen || children.size() != static_cast<std::size_t>(test.count) || !raster || (saveCase && !saved))
                    return fail("actual ingress/drop/landing/save/raster lifecycle incomplete");
                if (test.blocked && fallbackDrops == 0) return fail("real footprint did not exercise alternate legal placement");
                if (pictureCase && !outputRoot.empty()) {
                    std::string captureError; const auto stats = drawRuntimeStateOffscreenAtCell(s, target, 1000, 720, captureError,
                        outputRoot + '/' + label + "-ground.bmp", false, false, -1, nullptr, false, nullptr, false, false);
                    if (!stats || stats->parachuteSprites || stats->missingObjectShpSprites)
                        return fail("actual stable ground native SHP capture: " + captureError);
                    groundCapture = true; ++captures;
                }
                // After natural touchdown keep the original positions/slots
                // stable for a full native frame; no fake relocation or Stop.
                for (int n = 0; n < interval; ++n) { step(s);
                    if (ordinary) { step(*ordinary); step(*exact);
                        if (!compare(s, *ordinary, "ordinary grounded") || !compare(s, *exact, "canonical grounded")) return false; }
                    for (const auto& born : children) { const auto* o = findObjectById(s, born.id); const auto d = offset(o ? o->subCell : -1);
                        if (!o || o->airborne || o->subCell != born.slot || runtimeObjectRenderX(*o) + d.first != born.bodyX
                            || runtimeObjectRenderY(*o) + d.second != born.bodyY) return fail("native ground contact snapped within its next frame"); }
                }
                std::cout << "Paradrop evidence " << label << " count=" << children.size() << " launch=" << launchTick
                    << " edge=" << launchX << " lastDrop=" << lastBirthTick << " landed=" << s.tick
                    << " RNG=" << unchangedRng << " saved=" << saved << " captures=" << airCapture << '/' << groundCapture << '\n';
                return fixtureFailure.empty();
            }
        }
        return fail("original manifest/plane departure/natural descent did not finish count=" + std::to_string(children.size()));
    });
    if (!failures.empty()) { error = std::to_string(failures.size()) + " paradrop formation scenes failed";
        for (const auto& item : failures) error += "\n" + item; return false; }
    if (!scenes) { error = "Paradrop formation: requested case not found " + filter; return false; }
    std::cout << "Paradrop formation scenes=" << scenes << " trueUiClicks=" << uiClicks << " fullTicks=" << fullTicks
        << " originalBirths=" << births << " nativeSlots=" << slotChecks << " fallClocks=" << fallChecks
        << " landings=" << landingChecks << " groundChecks=" << groundChecks << " ordinary/canonical="
        << ordinarySaves << '/' << exactSaves << " pairedTicks=" << pairedTicks << " bodyCanopyRasters=" << nativeRasterChecks
        << " originalBMPs=" << captures << " legalFallbacks=" << fallbackDrops << '\n';
    return true;
}
