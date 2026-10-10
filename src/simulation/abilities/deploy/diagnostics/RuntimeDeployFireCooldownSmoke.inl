// Issue 24: original self-cell DeployFire skills, real Win32 orders and full ticks.
// Retail YURI/YURIPR share ordinary weapon reload; their native unload sequences
// 27..30 ignore repeat deployment when UndeployDelay >= 0. The engine's existing
// hold starts at deploy-idle (150/75 authored frames), independently of ROF.
// DESO keeps its existing separate eruption reload; CAOS selects original primary.
// This fixture does not inject reload, alter HP, or reconstruct saved routes.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeDeployFireCooldownSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    IniFile originalRules, originalArt; RulesDatabase rules; ArtDatabase art;
    if (!originalRules.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(originalRules)
        || !originalArt.loadFromFile(live.report.artPath) || !art.loadFromIni(originalArt)) {
        error = "DeployFire cooldown: original rules/art unavailable"; return false;
    }
    const char* outputValue = std::getenv("CONCORD_DEPLOY_FIRE_COOLDOWN_OUTPUT");
    const char* caseValue = std::getenv("CONCORD_DEPLOY_FIRE_COOLDOWN_CASE");
    const std::string output = outputValue ? outputValue : "", filter = caseValue ? caseValue : "";
    if (!output.empty()) std::filesystem::create_directories(output);
    std::ofstream csv;
    if (!output.empty()) csv.open(output + "/deploy-fire-timeline.csv");
    if (csv) csv << "case,hz,tick,id,type,mission,deployed,hold,animation,reload,eruption,actors,latestActor,x,y,hp\n";
    constexpr int width = 1280, screenHeight = 720;
    constexpr const char* localOwner = "SkirmishPlayer1";
    const RECT client{0, 0, width, screenHeight}, viewport = runtimeMapViewport(client);
    std::string label, stage, fixtureFailure; std::vector<std::string> failures;
    int scenes = 0, fullTicks = 0, clicks = 0, keys = 0, noOps = 0, casts = 0, autoStands = 0;
    int ordinarySaves = 0, exactSaves = 0, pairedTicks = 0, reloadChecks = 0, fieldChecks = 0, fieldRefreshes = 0, controls = 0;
    const auto fail = [&](const std::string& why) { error = "DeployFire " + label + " [" + stage + "]: " + why; return false; };
    const auto integer = [&](const std::string& section, const std::string& key, int fallback) {
        return std::stoi(originalRules.getValue(section, key, std::to_string(fallback)));
    };
    const auto authoredTicks = [](int frames, int hz) {
        return std::max(1, static_cast<int>(std::llround(double(frames) * hz / 15.0)));
    };
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
        ? std::filesystem::path("build/issue21_deploy_fire_cooldown/fixture-maps")
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
        text << "[Basic]\nName=DeployFireCooldown-" << (variant ? "Water" : "Land")
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
        mapPaths[variant] = (mapDirectory / (variant ? "deploy-fire-water.map" : "deploy-fire-land.map")).generic_string();
        std::ofstream mapFile(mapPaths[variant], std::ios::binary | std::ios::trunc);
        mapFile << text.str(); mapFile.close();
        if (!mapFile) return fail("actual native fixture map could not be persisted: " + mapPaths[variant]);
    }

    const auto make = [&](const std::vector<std::string>& sourceTypes, int hz, bool fast) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        seedRuntimeLoadScratch(s, live, false); s.config = live.config; s.config.fixedTickHz = hz;
        s.config.gameMode = "Battle"; s.config.playerCountry = "YuriCountry"; s.config.noCooldown = fast;
        s.config.gameSpeed = 4;
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
            WorldObjectInstance o; o.instanceId = id; o.mapId = "DeployFireCooldown" + std::to_string(id);
            o.type = type; o.kind = kind; o.owner = owner; o.x = cell.x; o.y = cell.y;
            o.cell = runtimeCellKey(cell.x, cell.y); o.alive = o.poweredOperational = true; o.mission = mission;
            o.health = runtimeMaxHealthForObject(s, o); o.facing = runtimeFacingFromDelta(1, 0);
            o.turretFacing = o.facing; initializeRuntimeObjectAmmo(o, runtimeAmmoRulesForObject(s.rules, o));
            applyRuntimeInitialObjectFlightState(s.rules, o, true); runtimeInitializeModExtensions(o, s.rules, hz);
            s.world.accessObjects().push_back(std::move(o));
        };
        const std::array<RuntimeCellPoint, 4> separate{{{64, 68}, {90, 68}, {64, 94}, {94, 94}}};
        for (std::size_t i = 0; i < sourceTypes.size(); ++i) {
            add(static_cast<int>(i) + 1, sourceTypes[i], sourceTypes[i] == "CAOS" ? WorldObjectKind::Unit : WorldObjectKind::Infantry,
                sourceTypes.size() == 1 ? RuntimeCellPoint{76, 80} : separate[i], localOwner, "Guard");
        }
        add(90, "E1", WorldObjectKind::Infantry, {100, 60}, localOwner, "Sleep");
        add(91, "GGI", WorldObjectKind::Infantry, {104, 60}, localOwner, "Sleep");
        add(100, "YAPOWR", WorldObjectKind::Structure, {104, 104}, localOwner, "Sleep");
        add(998, "NANRCT", WorldObjectKind::Structure, {52, 52}, "SkirmishPlayer2", "Sleep");
        s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner = localOwner; s.economy.credits = 20000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        RuntimeConfig::PlayerSlot local; local.enabled = local.human = local.local = true;
        local.name = "DeployFire local"; local.country = "YuriCountry"; local.team = "-";
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
        input.window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"Real DeployFire full-tick orders",
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

    const auto key = [&](Input& input, int code, const std::vector<int>& ids, bool repeat = false) {
        RuntimeSnapshotActiveStateScope active(input.state);
        if (input.state.selectedObjectIds != ids) applySelection(input.state, ids, false, false);
        SendMessageW(input.window, WM_KEYDOWN, code, LPARAM(1) | (repeat ? (LPARAM(1) << 30) : 0));
        SendMessageW(input.window, WM_KEYUP, code, LPARAM(1) | (LPARAM(1) << 30) | (LPARAM(1) << 31)); ++keys;
    };
    const auto click = [&](Input& input, POINT p) {
        RuntimeSnapshotActiveStateScope active(input.state);
        SendMessageW(input.window, WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
        SendMessageW(input.window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(p.x, p.y));
        SendMessageW(input.window, WM_LBUTTONUP, 0, MAKELPARAM(p.x, p.y)); ++clicks;
    };
    const auto bodySecondary = [&](Input& input, int id) {
        { RuntimeSnapshotActiveStateScope active(input.state);
            if (input.state.selectedObjectIds != std::vector<int>{id}) applySelection(input.state, {id}, false, false); }
        const auto p = objectPoint(input.state, id);
        if (!p) return fail("selected-body pixel unavailable #" + std::to_string(id));
        click(input, *p); return true;
    };
    const auto rightControl = [&](Input& input, int id) {
        { RuntimeSnapshotActiveStateScope active(input.state); applySelection(input.state, {id}, false, false); }
        const auto p = objectPoint(input.state, id); if (!p) return fail("right-click control has no actual body pixel");
        RuntimeSnapshotActiveStateScope active(input.state);
        // Map RMB is a primary/cancel control in this engine, not a fictitious
        // deployment button. A complete drag/release follows the real WM path.
        SendMessageW(input.window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(p->x, p->y));
        SendMessageW(input.window, WM_RBUTTONUP, 0, MAKELPARAM(p->x, p->y)); ++clicks; return true;
    };
    const auto move = [&](Input& input, int id, RuntimeCellPoint goal) {
        auto& s = input.state; RuntimeSnapshotActiveStateScope active(s); applySelection(s, {id}, false, false);
        centerRuntimeCameraOnCell(s, client, goal); ensureRuntimeCameraForViewport(s, viewport); invalidateRuntimeObjectProjectionCache(s);
        const POINT center = runtimeScreenPointForCell(s, viewport, goal, terrainLevelAtRuntimeCell(s, goal.x, goal.y));
        std::optional<POINT> pixel;
        for (int dy = -8; dy <= 8 && !pixel; ++dy) for (int dx = -8; dx <= 8 && !pixel; ++dx) {
            const POINT p{center.x + dx, center.y + dy}, picked = approximateCellFromScreenPoint(s, viewport, p);
            if (pointInRectInclusive(viewport, p) && picked.x == goal.x && picked.y == goal.y
                && !findProjectedObjectAtPoint(runtimeProjectedObjectsForViewport(s, viewport), p)) pixel = p;
        }
        if (!pixel) return fail("Move cannot pick exact real terrain " + std::to_string(goal.x) + ',' + std::to_string(goal.y));
        click(input, *pixel); const auto b = s.behaviors.find(id);
        return b != s.behaviors.end() && b->second.kind == RuntimeBehaviorKind::Move && b->second.moveTarget == goal
            ? true : fail("real WM Move rejected: " + s.interactionStatus);
    };
    const auto step = [&](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s); ++fullTicks;
        if (s.skirmishMatchFinished && fixtureFailure.empty()) fixtureFailure = "Battle ended: " + s.skirmishResult;
    };
    const auto objectRecord = [](const WorldObjectInstance& o) {
        std::ostringstream text; text.imbue(std::locale::classic()); text << std::setprecision(17)
            << o.instanceId << '|' << o.type << '|' << o.mission << '|' << o.alive << '|' << o.health << '|'
            << o.deployed << '|' << o.deployStateTicksRemaining << '|' << o.buildAnimTicksRemaining << '|' << o.buildAnimTicksTotal << '|'
            << o.weaponCooldownTicksRemaining << '|' << o.radiationEruptionCooldownTicksRemaining << '|'
            << o.x << '|' << o.y << '|' << o.cell << '|' << o.level << '|' << o.onBridge << '|'
            << runtimeObjectRenderX(o) << '|' << runtimeObjectRenderY(o) << '|' << runtimeObjectRenderLevel(o) << '|'
            << o.hasRenderPosition << '|' << o.renderMoveActive << '|' << o.facing << '|' << o.turretFacing << '|'
            << o.currentAmmo << '|' << o.garrisonHostObjectId;
        return text.str();
    };
    const auto describe = [&](const RuntimeWindowState& s, int id) {
        const auto* o = findRuntimeObjectByIdIncludingInactive(s, id);
        return "tick=" + std::to_string(s.tick) + " object=" + (o ? objectRecord(*o) : "absent")
            + " status=" + s.interactionStatus;
    };
    const auto paidReload = [](const WorldObjectInstance& o) {
        return o.type == "DESO" ? o.radiationEruptionCooldownTicksRemaining : o.weaponCooldownTicksRemaining;
    };
    struct Skill { std::string type, weapon; int rof = 0, hold = -1; };
    std::map<std::string, Skill> skills;
    for (const auto& pair : std::vector<std::pair<std::string, std::string>>{
            {"YURI", "PsiWave"}, {"YURIPR", "SuperPsiWave"}, {"DESO", "RadEruptionWeapon"}, {"CAOS", "ChaosAttack"}}) {
        const auto* unit = rules.findUnitType(pair.first == "CAOS" ? "UnitType" : "InfantryType", pair.first); const auto* weapon = rules.findWeaponType(pair.second);
        Skill skill{pair.first, pair.second, integer(pair.second, "ROF", -1), integer(pair.first, "UndeployDelay", -1)};
        if (!unit || !weapon || !unit->deployFire || !weapon->areaFire || skill.rof <= 0 || weapon->rof != skill.rof
            || unit->undeployDelay != skill.hold || unit->strength != integer(pair.first, "Strength", -1))
            return fail("actual original skill/profile/Strength/ROF/hold does not match source INI " + pair.first);
        if ((pair.first == "YURI" && skill.hold != 150) || (pair.first == "YURIPR" && skill.hold != 75)
            || (pair.first == "DESO" && (skill.rof != 60 || skill.hold != -1))
            || (pair.first == "CAOS" && skill.rof != 30 && skill.rof != 45))
            return fail("unexpected original patched skill timing " + pair.first);
        skills.emplace(pair.first, std::move(skill));
    }
    struct Cast { int actor = -1, tick = -1, paid = 0; RuntimeCellPoint cell; };
    struct Probe {
        std::set<int> seen; std::map<int, std::vector<Cast>> shots;
        std::map<int, int> previousReload, holdStart, firstHeldTicks;
        std::map<int, int> previousFieldRemaining;
        int fieldRefreshes = 0;
        std::set<int> fieldActors, visualActors;
    };
    const auto observe = [&](RuntimeWindowState& s, Probe& probe, const std::vector<int>& ids) {
        RuntimeSnapshotActiveStateScope active(s);
        const auto collect = [&](const auto& projectiles) {
            for (const auto& p : projectiles) {
                if (std::find(ids.begin(), ids.end(), p.attackerObjectId) == ids.end() || probe.seen.contains(p.actorId)) continue;
                const auto* o = findObjectById(s, p.attackerObjectId); if (!o) return fail("real self-cast source disappeared");
                const auto& skill = skills.at(o->type); if (p.weaponId != skill.weapon) return fail("actual skill selected a wrong weapon " + p.weaponId);
                if (p.targetObjectId != -1 || p.toCell != runtimeCellPointForObject(*o) || p.damage != integer(skill.weapon, "Damage", -1)
                    || toLowerAsciiLocal(p.weaponSlot) != (o->type == "CAOS" ? "primary" : "deploy")
                    || p.reportSoundId != trimAsciiLocal(originalRules.getValue(skill.weapon, "Report", "")))
                    return fail("actual self-cell projectile/slot/Damage/Report differs from original: " + serializeRuntimeProjectileEffect(p));
                const int expectedPaid = paidReload(*o);
                if (expectedPaid < authoredTicks(skill.rof, s.config.fixedTickHz) || expectedPaid > authoredTicks(skill.rof + 2, s.config.fixedTickHz))
                    return fail("actual cast did not pay original ROF " + std::to_string(expectedPaid) + ": " + describe(s, o->instanceId));
                auto& history = probe.shots[o->instanceId];
                if (!history.empty() && p.spawnTick < history.back().tick + history.back().paid)
                    return fail("earlier-than-paid reload cast #" + std::to_string(o->instanceId));
                history.push_back({p.actorId, p.spawnTick, expectedPaid, runtimeCellPointForObject(*o)});
                probe.seen.insert(p.actorId); ++casts;
            }
            return true;
        };
        if (!collect(s.projectiles) || !collect(s.recentExpiredProjectiles)) return false;
        for (int id : ids) {
            const auto* o = findObjectById(s, id); if (!o || o->health != runtimeMaxHealthForObject(s, *o))
                return fail("source lost original HP: " + describe(s, id));
            if (skills.at(o->type).hold >= 0 && o->deployStateTicksRemaining >= 0 && !probe.holdStart.contains(id)) {
                probe.holdStart[id] = s.tick; probe.firstHeldTicks[id] = o->deployStateTicksRemaining;
                if (o->deployStateTicksRemaining != authoredTicks(skills.at(o->type).hold, s.config.fixedTickHz))
                    return fail("auto-undeploy hold was not original authored 150/75: " + describe(s, id));
            }
            if (csv) csv << label << ',' << s.config.fixedTickHz << ',' << s.tick << ',' << id << ',' << o->type << ','
                << o->mission << ',' << o->deployed << ',' << o->deployStateTicksRemaining << ',' << o->buildAnimTicksRemaining << ','
                << o->weaponCooldownTicksRemaining << ',' << o->radiationEruptionCooldownTicksRemaining << ',' << probe.shots[id].size() << ','
                << (probe.shots[id].empty() ? -1 : probe.shots[id].back().actor) << ',' << runtimeObjectRenderX(*o) << ','
                << runtimeObjectRenderY(*o) << ',' << o->health << '\n';
            for (const auto& fx : s.impactEffects) if (fx.attackerObjectId == id && probe.seen.contains(fx.projectileActorId)) {
                if ((o->type == "YURI" || o->type == "YURIPR") && fx.animId == "RING1") probe.visualActors.insert(fx.projectileActorId);
                if (o->type == "CAOS" && fx.animId == trimAsciiLocal(originalRules.getValue("PsychGasCreate", "AnimList", ""))) probe.visualActors.insert(fx.projectileActorId);
            }
        }
        for (const auto& entry : s.specialMechanisms.radiationAreas()) {
            const auto& site = entry.second;
            if (std::find(ids.begin(), ids.end(), site.sourceObjectId) == ids.end()) continue;
            const auto* o = findObjectById(s, site.sourceObjectId); if (!o || o->type != "DESO") return fail("radiation field wrong source");
            const bool originalCell = std::any_of(probe.shots[o->instanceId].begin(), probe.shots[o->instanceId].end(), [&](const Cast& shot) { return site.center.x == shot.cell.x && site.center.y == shot.cell.y; });
            if (!originalCell || site.radius != integer("RadEruptionWarhead", "CellSpread", -1)
                || site.maximumStrength != integer("RadEruptionWeapon", "RadLevel", -1) || site.warheadId != trimAsciiLocal(originalRules.getValue("Radiation", "RadSiteWarhead", "RadSite")))
                return fail("real radiation field differs from original self-cell/RadLevel/CellSpread");
            const auto previous = probe.previousFieldRemaining.find(site.areaId);
            if (previous != probe.previousFieldRemaining.end() && site.remainingTicks > previous->second) {
                ++probe.fieldRefreshes; ++fieldRefreshes;
            }
            probe.previousFieldRemaining[site.areaId] = site.remainingTicks;
            ++fieldChecks;
        }
        return fixtureFailure.empty() ? true : fail(fixtureFailure);
    };
    const auto noOp = [&](Input& input, int id, bool body, const char* replica = "source") {
        auto& s = input.state;
        // Selecting after load may play the original VoiceSelect. It precedes
        // the action being tested; repeat D/body LMB must not select again.
        { RuntimeSnapshotActiveStateScope active(s);
            if (s.selectedObjectIds != std::vector<int>{id}) applySelection(s, {id}, false, false); }
        const auto* source = findObjectById(s, id); if (!source) return fail("no-op source absent");
        const std::string before = objectRecord(*source); const int actor = s.nextProjectileActorId;
        const auto sounds = s.runtimeSoundEvents; const auto hash = s.specialMechanisms.stateHash();
        if (body) { if (!bodySecondary(input, id)) return false; } else key(input, 'D', {id});
        source = findObjectById(s, id);
        if (!source || objectRecord(*source) != before || s.nextProjectileActorId != actor
            || s.runtimeSoundEvents != sounds || s.specialMechanisms.stateHash() != hash)
            return fail("repeat " + std::string(replica) + ' ' + (body ? "selected-body LMB" : "D")
                + " changed pose/wait/reload/sound/pulse: poseSame=" + runtimeBoolText(source && objectRecord(*source) == before)
                + " actor=" + std::to_string(actor) + '/' + std::to_string(s.nextProjectileActorId)
                + " sounds=" + std::to_string(sounds) + '/' + std::to_string(s.runtimeSoundEvents)
                + " specialHash=" + std::to_string(hash) + '/' + std::to_string(s.specialMechanisms.stateHash())
                + " suppressed=" + runtimeBoolText(s.runtimeSuppressPlayerAudioFeedback)
                + " lastSound=" + s.lastRuntimeSound + " before=" + before + " after=" + describe(s, id));
        ++noOps; return true;
    };
    const auto effectRecord = [](const RuntimeImpactEffect& fx) {
        std::ostringstream text; text << runtimeMultiplayerSnapshotImpactRecord(fx) << '|'
            << iniHexEncode(fx.animId) << '|' << iniHexEncode(fx.renderIntent) << '|' << fx.frameIndex << '|'
            << fx.facing << '|' << fx.radius << '|' << runtimeBoolText(fx.bright) << '|' << iniHexEncode(fx.projectileImageId);
        return text.str();
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
        if (!materializeRuntimeLoadStateFromIni(*other, base, error)
            || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*other, extension, error))) return std::unique_ptr<RuntimeWindowState>{};
        initializeRuntimeOfflineGameplayRouting(*other); if (exact) ++exactSaves; else ++ordinarySaves; return other;
    };
    const auto compare = [&](const RuntimeWindowState& a, const RuntimeWindowState& b, bool exact, int futureActor) {
        if (a.tick != b.tick || a.nextProjectileActorId != b.nextProjectileActorId || a.economy.credits != b.economy.credits
            || a.specialMechanisms.stateHash() != b.specialMechanisms.stateHash()) return fail("saved skill authority/tick/actor differs");
        const auto* x = findObjectById(a, 1); const auto* y = findObjectById(b, 1);
        if (!x || !y || objectRecord(*x) != objectRecord(*y)) return fail("saved pose/hold/reload differs: " + describe(a, 1) + " vs " + describe(b, 1));
        if (a.projectiles.size() != b.projectiles.size()) return fail("saved active skill projectiles differ");
        for (std::size_t i = 0; i < a.projectiles.size(); ++i)
            if (serializeRuntimeProjectileEffect(a.projectiles[i]) != serializeRuntimeProjectileEffect(b.projectiles[i])) return fail("saved active projectile fields differ");
        std::ostringstream ac, bc; a.combatDetails.save(ac); b.combatDetails.save(bc);
        if (ac.str() != bc.str()) return fail("saved actual combat scheduler differs: source=" + ac.str()
            + " counterpart=" + bc.str());
        if (exact) {
            if (a.impactEffects.size() != b.impactEffects.size()) return fail("canonical extension lost existing skill FX");
            for (std::size_t i = 0; i < a.impactEffects.size(); ++i)
                if (effectRecord(a.impactEffects[i]) != effectRecord(b.impactEffects[i])) return fail("canonical 49+7 FX changed");
        } else {
            const auto future = [&](const RuntimeWindowState& s) {
                std::map<std::string, std::string> result;
                for (const auto& fx : s.impactEffects) if (fx.attackerObjectId == 1 && fx.projectileActorId >= futureActor)
                    result.emplace(std::to_string(fx.projectileActorId) + ':' + fx.renderIntent, effectRecord(fx));
                return result;
            };
            if (future(a) != future(b)) return fail("ordinary future skill FX 49+7 changed");
        }
        ++pairedTicks; return true;
    };

    const auto runSkill = [&](const std::string& type, int hz) {
        stage = "original fixture"; fixtureFailure.clear();
        auto storage = make({type}, hz, false); if (!storage) return false;
        auto& s = *storage; Input input{s}; if (!openInput(input)) return false;
        Probe probe; std::unique_ptr<RuntimeWindowState> ordinary, exact;
        std::unique_ptr<Input> ordinaryInput, exactInput; int futureActor = -1;
        const auto advance = [&]() {
            const auto* before = findObjectById(s, 1); if (!before) return fail("source missing before complete tick");
            const int previousPaid = paidReload(*before); const std::size_t previousShots = probe.shots[1].size();
            step(s); if (!observe(s, probe, {1})) return false;
            const auto* after = findObjectById(s, 1); if (!after) return fail("source missing after complete tick");
            if (probe.shots[1].size() == previousShots && paidReload(*after) != std::max(0, previousPaid - 1))
                return fail("paid reload restarted or vanished without an actual cast: " + describe(s, 1));
            ++reloadChecks;
            if (ordinary) {
                step(*ordinary); step(*exact);
                if (!compare(s, *ordinary, false, futureActor) || !compare(s, *exact, true, futureActor)) return false;
            }
            return true;
        };
        const auto commandAll = [&](int code) {
            key(input, code, {1});
            if (ordinaryInput) { key(*ordinaryInput, code, {1}); key(*exactInput, code, {1}); }
        };
        const auto bodyAll = [&]() {
            if (!bodySecondary(input, 1)) return false;
            if (ordinaryInput && (!bodySecondary(*ordinaryInput, 1) || !bodySecondary(*exactInput, 1))) return false;
            return true;
        };
        const auto repeatAll = [&](bool body) {
            if (!noOp(input, 1, body)) return false;
            if (ordinaryInput && (!noOp(*ordinaryInput, 1, body, "ordinary")
                || !noOp(*exactInput, 1, body, "canonical-extension"))) return false;
            return true;
        };
        const auto* source = findObjectById(s, 1); const RuntimeCellPoint anchor = runtimeCellPointForObject(*source);
        const auto& skill = skills.at(type);
        stage = "real cold D";
        commandAll('D'); source = findObjectById(s, 1);
        if (!source || !runtimeMissionIsDeploying(*source) || !source->deployed || paidReload(*source) != 0)
            return fail("real cold D did not start original pose without inventing reload: " + describe(s, 1));
        const std::string repeatedBefore = objectRecord(*source); const auto repeatedSounds = s.runtimeSoundEvents;
        key(input, 'D', {1}, true);
        if (objectRecord(*findObjectById(s, 1)) != repeatedBefore || s.runtimeSoundEvents != repeatedSounds)
            return fail("WM key autorepeat retriggered deployment");
        if (skill.hold >= 0 && (!repeatAll(false) || !repeatAll(true))) return false;
        const int deploymentBudget = hz * 5 + authoredTicks(skill.rof + 2, hz);
        for (int frame = 0; probe.shots[1].empty() && frame < deploymentBudget; ++frame) if (!advance()) return false;
        if (probe.shots[1].size() != 1) return fail("real cold deployment lacks exactly one original first cast: " + describe(s, 1));
        const Cast first = probe.shots[1].front();
        if (skill.hold >= 0) {
            stage = "native active deployment ignores repeat";
            if (!probe.holdStart.contains(1)) return fail("psychic first pulse has no real active wait");
            const int holdStart = probe.holdStart.at(1), holdTicks = authoredTicks(skill.hold, hz);
            bool stopChecked = false, rightChecked = false, saved = false, sawComplete = false;
            while (s.tick < holdStart + holdTicks) {
                source = findObjectById(s, 1);
                if (!source || !source->deployed || runtimeMissionIsUndeploying(*source)
                    || runtimeCellPointForObject(*source) != anchor || source->renderMoveActive
                    || source->deployStateTicksRemaining != holdTicks - (s.tick - holdStart))
                    return fail("hold/pose advanced or restarted before authored auto-stand boundary: " + describe(s, 1));
                sawComplete |= toLowerAsciiLocal(source->mission) == "deployedareafirecomplete";
                if (!repeatAll(false) || !repeatAll(true)) return false;
                if (!stopChecked && s.tick >= first.tick + 3) {
                    const int paid = paidReload(*source), hold = source->deployStateTicksRemaining;
                    commandAll('S'); source = findObjectById(s, 1);
                    if (!source || paidReload(*source) != paid || source->deployStateTicksRemaining != hold)
                        return fail("real Stop reset paid skill reload or native wait");
                    stopChecked = true;
                }
                if (!rightChecked && s.tick >= first.tick + 4) {
                    const int paid = paidReload(*source), hold = source->deployStateTicksRemaining;
                    if (!rightControl(input, 1)) return false;
                    if (ordinaryInput && (!rightControl(*ordinaryInput, 1) || !rightControl(*exactInput, 1))) return false;
                    source = findObjectById(s, 1);
                    if (!source || paidReload(*source) != paid || source->deployStateTicksRemaining != hold)
                        return fail("real RMB control reset paid reload/hold");
                    rightChecked = true;
                }
                if (type == "YURI" && !saved && s.tick >= first.tick + 8) {
                    stage = "actual mid-hold restore";
                    futureActor = s.nextProjectileActorId;
                    ordinary = checkpoint(s, false); exact = checkpoint(s, true);
                    if (!ordinary || !exact) return fail("mid-hold ordinary/canonical extension restore failed: " + error);
                    ordinaryInput = std::make_unique<Input>(*ordinary); exactInput = std::make_unique<Input>(*exact);
                    if (!openInput(*ordinaryInput) || !openInput(*exactInput)
                        || !compare(s, *ordinary, false, futureActor) || !compare(s, *exact, true, futureActor)) return false;
                    saved = true; stage = "strict paired hold and repeats";
                }
                if (!advance()) return false;
            }
            source = findObjectById(s, 1);
            if (!source || !runtimeMissionIsUndeploying(*source) || !source->deployed || probe.shots[1].size() != 1
                || !sawComplete || !stopChecked || !rightChecked)
                return fail("original 150/75 auto-stand or single fire-once failed at exact boundary: " + describe(s, 1));
            const int fold = source->buildAnimTicksRemaining;
            for (int frame = 0; frame < fold; ++frame) {
                if (!advance()) return false;
                source = findObjectById(s, 1);
                if (frame + 1 < fold && (!source || !runtimeMissionIsUndeploying(*source) || !source->deployed))
                    return fail("automatic native stand animation completed early");
            }
            source = findObjectById(s, 1);
            if (!source || source->deployed || source->deployStateTicksRemaining != -1 || paidReload(*source) != 0)
                return fail("automatic stand did not finish original wait/reload");
            ++autoStands;
            stage = "standing real Move then new self pulse";
            const RuntimeCellPoint goal{anchor.x + 1, anchor.y};
            if (!move(input, 1, goal)) return false;
            if (ordinaryInput && (!move(*ordinaryInput, 1, goal) || !move(*exactInput, 1, goal))) return false;
            for (int frame = 0; frame < hz * 20; ++frame) {
                if (!advance()) return false;
                source = findObjectById(s, 1);
                if (source && runtimeCellPointForObject(*source) == goal && !source->renderMoveActive && s.behaviors.find(1) == s.behaviors.end()) break;
            }
            source = findObjectById(s, 1);
            if (!source || runtimeCellPointForObject(*source) != goal || source->renderMoveActive)
                return fail("standing infantry failed actual original-speed one-cell Move");
            if (!bodyAll()) return false;
            for (int frame = 0; probe.shots[1].size() < 2 && frame < deploymentBudget; ++frame) if (!advance()) return false;
            if (probe.shots[1].size() != 2 || probe.shots[1].back().tick < first.tick + first.paid)
                return fail("second ready actual pulse not exactly once/earlier than paid ROF");
            // Complete the second original wait naturally; no timer/FX clear.
            const int secondIdle = s.tick;
            for (int frame = 0; frame < holdTicks + hz * 5; ++frame) {
                if (!advance()) return false;
                source = findObjectById(s, 1); if (source && !source->deployed && s.tick > secondIdle) break;
            }
            source = findObjectById(s, 1);
            if (!source || source->deployed || probe.shots[1].size() != 2 || probe.visualActors.size() != 2)
                return fail("second fire-once/actual RING1/natural stand did not close");
        } else {
            stage = "existing manual fold preserves paid area reload";
            const int paid = paidReload(*findObjectById(s, 1));
            commandAll('D'); source = findObjectById(s, 1);
            if (!source || !runtimeMissionIsUndeploying(*source) || paidReload(*source) != paid)
                return fail("manual fold changed existing original area reload: " + describe(s, 1));
            const int fold = source->buildAnimTicksRemaining;
            for (int frame = 0; frame < fold; ++frame) if (!advance()) return false;
            source = findObjectById(s, 1);
            if (!source || source->deployed || paidReload(*source) != std::max(0, paid - fold))
                return fail("fold animation reset or bypassed paid area reload");
            if (!bodyAll()) return false;
            stage = "redeploy cannot fire before paid ROF";
            for (int frame = 0; probe.shots[1].size() < 2 && frame < authoredTicks(skill.rof + 2, hz) + hz * 5; ++frame)
                if (!advance()) return false;
            if (probe.shots[1].size() != 2 || probe.shots[1][1].tick < first.tick + first.paid)
                return fail("redeployed actual self-cell weapon violated original paid reload");
            const Cast second = probe.shots[1][1];
            // Stay deployed and observe a third real finite pulse/field refresh.
            stage = "continuous original area refresh";
            for (int frame = 0; probe.shots[1].size() < 3 && frame < authoredTicks(skill.rof + 2, hz) + hz; ++frame)
                if (!advance()) return false;
            if (probe.shots[1].size() != 3 || probe.shots[1][2].tick < second.tick + second.paid)
                return fail("continued area-fire reset/lost its independent original ROF");
            const int paidThird = paidReload(*findObjectById(s, 1));
            commandAll('S');
            if (paidReload(*findObjectById(s, 1)) != paidThird) return fail("Stop cleared area-fire reload");
            commandAll('D'); source = findObjectById(s, 1);
            if (!source || !runtimeMissionIsUndeploying(*source) || paidReload(*source) != paidThird)
                return fail("third-pulse manual stand changed paid reload");
            const int finalFold = source->buildAnimTicksRemaining;
            for (int frame = 0; frame < finalFold; ++frame) if (!advance()) return false;
            source = findObjectById(s, 1); const int remaining = paidReload(*source);
            const RuntimeCellPoint goal{anchor.x + 1, anchor.y};
            if (!move(input, 1, goal) || paidReload(*findObjectById(s, 1)) != remaining)
                return fail("real standing Move reset original skill timer");
            for (int frame = 0; frame < hz * 20; ++frame) {
                if (!advance()) return false; source = findObjectById(s, 1);
                if (source && runtimeCellPointForObject(*source) == goal && !source->renderMoveActive && s.behaviors.find(1) == s.behaviors.end()) break;
            }
            source = findObjectById(s, 1);
            if (!source || runtimeCellPointForObject(*source) != goal || source->deployed || source->renderMoveActive || probe.shots[1].size() != 3)
                return fail("standing Move created/continued a fourth undeployed area cast");
            if (type == "DESO" && (probe.fieldRefreshes < 2 || s.specialMechanisms.radiationAreas().size() != 1))
                return fail("real continued DESO pulses did not maintain their original single cell field");
            if (type == "CAOS" && probe.visualActors.size() != 3)
                return fail("real CAOS self-cell primary lacks three finite original gas effects");
        }
        // E1/GGI are original pose toggles, not this class of area skill.
        stage = "shape-only infantry control";
        const int actor = s.nextProjectileActorId;
        for (int id : {90, 91}) {
            const auto* control = findObjectById(s, id); const int hp = control->health;
            key(input, 'D', {id});
            if (ordinaryInput) { key(*ordinaryInput, 'D', {id}); key(*exactInput, 'D', {id}); }
            const int wait = findObjectById(s, id)->buildAnimTicksRemaining;
            for (int frame = 0; frame < wait + 1; ++frame) if (!advance()) return false;
            control = findObjectById(s, id);
            if (!control || !control->deployed || control->health != hp || s.nextProjectileActorId != actor)
                return fail("shape-only E1/GGI generated an area-fire skill");
            for (const auto* counterpart : {ordinary.get(), exact.get()}) if (counterpart) {
                const auto* counterpartControl = findObjectById(*counterpart, id);
                if (!counterpartControl || !counterpartControl->deployed || counterpartControl->health != hp
                    || counterpart->nextProjectileActorId != actor || objectRecord(*counterpartControl) != objectRecord(*control))
                    return fail("paired shape-only control changed deployed pose/HP or created a skill actor #" + std::to_string(id));
            }
            key(input, 'D', {id});
            if (ordinaryInput) { key(*ordinaryInput, 'D', {id}); key(*exactInput, 'D', {id}); }
            const int stand = findObjectById(s, id)->buildAnimTicksRemaining;
            for (int frame = 0; frame < stand + 1; ++frame) if (!advance()) return false;
            control = findObjectById(s, id);
            if (!control || control->deployed || control->health != hp || s.nextProjectileActorId != actor)
                return fail("shape-only control lost original explicit fold");
            for (const auto* counterpart : {ordinary.get(), exact.get()}) if (counterpart) {
                const auto* counterpartControl = findObjectById(*counterpart, id);
                if (!counterpartControl || counterpartControl->deployed || counterpartControl->health != hp
                    || counterpart->nextProjectileActorId != actor || objectRecord(*counterpartControl) != objectRecord(*control))
                    return fail("paired shape-only control changed standing pose/HP or created a skill actor #" + std::to_string(id));
            }
            ++controls;
        }
        return true;
    };

    const auto runMixed = [&](int hz) {
        stage = "NoCooldown four real original sources"; fixtureFailure.clear();
        auto storage = make({"YURI", "YURIPR", "DESO", "CAOS"}, hz, true); if (!storage) return false;
        auto& s = *storage; Input input{s}; if (!openInput(input)) return false; Probe probe;
        const std::vector<int> ids{1, 2, 3, 4};
        if (!runtimeNoCooldownForOwner(s, localOwner)) return fail("real local NoCooldown configuration not effective");
        const auto advance = [&]() {
            std::map<int, int> paid; std::map<int, std::size_t> counts;
            for (int id : ids) { paid[id] = paidReload(*findObjectById(s, id)); counts[id] = probe.shots[id].size(); }
            step(s); if (!observe(s, probe, ids)) return false;
            for (int id : ids) {
                const auto* o = findObjectById(s, id);
                if (!o || (probe.shots[id].size() == counts[id] && paidReload(*o) != std::max(0, paid[id] - 1)))
                    return fail("mixed selection reset another actor's paid reload: " + describe(s, id));
                ++reloadChecks;
            }
            return true;
        };
        // Different real acceptance ticks create genuinely independent clocks;
        // neither initial ROF nor an active hold is manufactured for this case.
        for (int id : ids) {
            key(input, 'D', {id});
            if (!findObjectById(s, id) || !runtimeMissionIsDeploying(*findObjectById(s, id)))
                return fail("staggered original D did not enter actual deploy");
            if ((id == 1 || id == 2) && (!noOp(input, id, false) || !noOp(input, id, true))) return false;
            for (int frame = 0; frame < 2; ++frame) if (!advance()) return false;
        }
        for (int frame = 0; frame < hz * 5; ++frame) {
            bool all = true; for (int id : ids) all &= !probe.shots[id].empty();
            if (all) break; if (!advance()) return false;
        }
        for (int id : ids) if (probe.shots[id].size() != 1)
            return fail("NoCooldown first real skill count not exactly one #" + std::to_string(id));
        // Original 0..2 rule-frame ROF jitter can make remaining values equal;
        // independent launch histories, per-member decrement and paid gaps
        // establish independence without assuming numerically unequal clocks.
        if (probe.shots[1][0].tick == probe.shots[2][0].tick)
            return fail("staggered real YURI/Prime launch histories were not separate");
        stage = "mixed D ignores waiting psychic actors and folds manual actors";
        const auto yuriBefore = objectRecord(*findObjectById(s, 1)), primeBefore = objectRecord(*findObjectById(s, 2));
        std::map<int, int> beforePaid; for (int id : ids) beforePaid[id] = paidReload(*findObjectById(s, id));
        key(input, 'D', ids);
        for (int id : ids) if (paidReload(*findObjectById(s, id)) != beforePaid[id])
            return fail("mixed D erased a member's independent paid reload #" + std::to_string(id));
        if (objectRecord(*findObjectById(s, 1)) != yuriBefore || objectRecord(*findObjectById(s, 2)) != primeBefore
            || !runtimeMissionIsUndeploying(*findObjectById(s, 3)) || !runtimeMissionIsUndeploying(*findObjectById(s, 4)))
            return fail("mixed ready/cooling deployment did not honor original per-type active wait");
        if (!noOp(input, 1, true) || !noOp(input, 2, true)) return false;
        for (int frame = 0; frame < hz * 5 && (findObjectById(s, 3)->deployed || findObjectById(s, 4)->deployed); ++frame)
            if (!advance()) return false;
        if (findObjectById(s, 3)->deployed || findObjectById(s, 4)->deployed)
            return fail("mixed existing manual stand animation did not complete");
        key(input, 'D', {3, 4});
        for (int frame = 0; frame < hz * 6 && (probe.shots[3].size() < 2 || probe.shots[4].size() < 2); ++frame)
            if (!advance()) return false;
        if (probe.shots[3].size() != 2 || probe.shots[4].size() < 2 || probe.shots[1].size() != 1 || probe.shots[2].size() != 1)
            return fail("NoCooldown bypassed paid skill reload or repeated a waiting psychic pulse");
        key(input, 'D', {3, 4}); // Preserve the legitimate paid clocks while standing.
        for (int frame = 0; frame < hz * 6 && (findObjectById(s, 2)->deployed
                || findObjectById(s, 3)->deployed || findObjectById(s, 4)->deployed); ++frame)
            if (!advance()) return false;
        if (findObjectById(s, 2)->deployed || !findObjectById(s, 1)->deployed
            || findObjectById(s, 3)->deployed || findObjectById(s, 4)->deployed)
            return fail("actual Prime 75-frame ready / YURI 150-frame waiting window missing");
        stage = "mixed ready and waiting actual D";
        const auto waitingYuri = objectRecord(*findObjectById(s, 1));
        key(input, 'D', ids);
        if (objectRecord(*findObjectById(s, 1)) != waitingYuri
            || !runtimeMissionIsDeploying(*findObjectById(s, 2))
            || !runtimeMissionIsDeploying(*findObjectById(s, 3))
            || !runtimeMissionIsDeploying(*findObjectById(s, 4)))
            return fail("mixed D did not keep cooling YURI while accepting actually ready members");
        for (int frame = 0; frame < hz * 5 && probe.shots[2].size() < 2; ++frame) if (!advance()) return false;
        if (probe.shots[2].size() != 2 || probe.shots[1].size() != 1) return fail("ready Prime new pulse/waiting clone single pulse mismatch");
        for (int frame = 0; frame < hz * 5 && (probe.shots[3].size() < 3 || probe.shots[4].size() < 3); ++frame)
            if (!advance()) return false;
        if (probe.shots[3].size() < 3 || probe.shots[4].size() < 3) return fail("ready mixed area skills lost their actual new casts");
        key(input, 'D', {3, 4});
        for (int frame = 0; frame < hz * 12; ++frame) {
            if (!advance()) return false;
            bool allStanding = true; for (int id : ids) allStanding &= !findObjectById(s, id)->deployed;
            if (allStanding) break;
        }
        for (int id : ids) if (!findObjectById(s, id) || findObjectById(s, id)->deployed)
            return fail("NoCooldown changed original manual/automatic stand lifecycle #" + std::to_string(id));
        if (probe.shots[1].size() != 1 || probe.shots[2].size() != 2) return fail("waiting FireOnce replayed during natural finish");
        ++autoStands; ++autoStands; return true;
    };
    const auto scene = [&](const std::string& name, const auto& run) {
        if (!filter.empty() && name != filter) return;
        label = name; stage.clear(); error.clear();
        if (run()) { ++scenes; std::cout << "DeployFire " << name << " PASS\n"; }
        else { failures.push_back(error); std::cout << "DeployFire " << name << " FAIL: " << error << '\n'; }
    };
    for (int hz : {30, 60}) {
        for (const std::string type : {"YURI", "YURIPR", "DESO", "CAOS"})
            scene(type + '-' + std::to_string(hz), [&] { return runSkill(type, hz); });
        scene("NoCooldown-mixed-" + std::to_string(hz), [&] { return runMixed(hz); });
    }
    if (scenes == 0 && failures.empty()) return fail("CASE did not match an actual scene");
    if (!failures.empty()) {
        std::ostringstream joined; for (const auto& failure : failures) joined << failure << '\n'; error = joined.str(); return false;
    }
    std::cout << "DeployFire cooldown: scenes=" << scenes << " fullTicks=" << fullTicks << " actualCasts=" << casts
        << " repeatedNoOps=" << noOps << " reloadChecks=" << reloadChecks << " autoStands=" << autoStands
        << " fieldChecks=" << fieldChecks << " fieldRefreshes=" << fieldRefreshes << " shapeControls=" << controls << " WMkeys=" << keys << " WMclicks=" << clicks
        << " ordinary+canonical=" << ordinarySaves << '+' << exactSaves << " paired=" << pairedTicks
        << "; original ROF/HP, psychic object reload, hold and per-member waiting verified\n"
        << "MindControl/wave shared reload is native-static evidence; no dual-skill ordinary-shot experiment is claimed.\n";
    error.clear(); return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
