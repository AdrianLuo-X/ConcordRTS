// Issue 22: original Magnetron weapons, actual Win32 release orders, and
// complete simulation ticks. The fall oracle must be based on the confirmed
// retail release chain, never on runtimeAdvanceMagBeamFallingKinematics.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeMagnetronFallMotionSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    IniFile originalRules, originalArt; RulesDatabase rules; ArtDatabase art;
    if (!originalRules.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(originalRules)
        || !originalArt.loadFromFile(live.report.artPath) || !art.loadFromIni(originalArt)) {
        error = "Magnetron fall: original rules/art unavailable"; return false;
    }
    const auto* tele = findRuntimeUnitTypeById(rules, "TELE");
    const auto* beam = rules.findWeaponType("MagneticBeam");
    const auto* locomotor = rules.findWarheadType("LocomotorBeam");
    if (!tele || !beam || !locomotor || !beam->magBeam || !locomotor->isLocomotor
        || tele->primaryWeapon != "MagneticBeam" || tele->canPassiveAquire || beam->damage != 5000
        || beam->rof != 20 || beam->range != 12 || beam->minimumRange != 3) {
        error = "Magnetron fall: original TELE/MagneticBeam/locomotor contract changed"; return false;
    }
    const char* outputValue = std::getenv("CONCORD_MAGNETRON_FALL_MOTION_OUTPUT");
    const char* caseValue = std::getenv("CONCORD_MAGNETRON_FALL_MOTION_CASE");
    const std::string output = outputValue ? outputValue : "", filter = caseValue ? caseValue : "";
    if (!output.empty()) std::filesystem::create_directories(output);
    std::ofstream csv;
    // Keep the first failed fixture's trace; this run uses actual backed maps.
    if (!output.empty()) csv.open(output + "/magnetron-fall-backed-map.csv");
    if (csv) csv << "case,branch,hz,tick,phase,releaseTick,elapsedSeconds,x,y,z,vx,vy,vz,expectedX,expectedY,expectedZ,expectedVZ,groundZ,hp\n";
    constexpr int width = 1280, screenHeight = 720;
    constexpr const char* localOwner = "SkirmishPlayer1";
    const RECT client{0, 0, width, screenHeight}, viewport = runtimeMapViewport(client);
    std::string label, stage, fixtureFailure; std::vector<std::string> failures;
    int scenes = 0, fullTicks = 0, clicks = 0, keys = 0, trajectoryChecks = 0;
    int ordinarySaves = 0, exactSaves = 0, pairedTicks = 0, stableChecks = 0, nativeWaterChecks = 0, captures = 0;
    const auto fail = [&](const std::string& why) { error = "Magnetron fall " + label + " [" + stage + "]: " + why; return false; };

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
        ? std::filesystem::path("build/issue21_magnetron_fall_motion/fixture-maps")
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
        text << "[Basic]\nName=MagnetronFall-" << (variant ? "Water" : "Land")
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
        mapPaths[variant] = (mapDirectory / (variant ? "fall-water.map" : "fall-land.map")).generic_string();
        std::ofstream mapFile(mapPaths[variant], std::ios::binary | std::ios::trunc);
        mapFile << text.str(); mapFile.close();
        if (!mapFile) return fail("actual native fixture map could not be persisted: " + mapPaths[variant]);
    }

    const auto make = [&](const std::string& targetType, int hz, bool fast, bool naval) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        seedRuntimeLoadScratch(s, live, false); s.config = live.config; s.config.fixedTickHz = hz;
        s.config.gameMode = "Battle"; s.config.playerCountry = "YuriCountry"; s.config.noCooldown = fast;
        s.config.multiplayerActive = s.config.computerAiEnabled = s.config.shortGame = false;
        s.config.crates = s.config.superweapons = false; s.config.startingUnits = 0; s.config.techLevel = 10;
        s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.config.keyboardReferenceLayout = false; s.config.keyBindings.clear();
        s.campaignMode = false; s.skirmishMatchFinished = s.skirmishVictory = false; s.skirmishResult.clear();
        s.rules = rules; s.art = art; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.config.defaultMapPath = mapPaths[naval ? 1 : 0];
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
            const bool waterCell = naval && cell.x >= 82 && cell.x <= 96 && cell.y >= 76 && cell.y <= 85;
            if (!facts || !facts->hasTerrain || cell.cellIndex512 != cell.y * 512 + cell.x
                || cell.tileIndex != (waterCell ? water->tileIndex : landTile)
                || cell.subTileIndex != (waterCell ? water->subTileIndex : 0) || cell.level != 0
                || facts->terrain.tileIndex != cell.tileIndex || facts->terrain.subTileIndex != cell.subTileIndex
                || facts->heightLevel != 0 || facts->isWater != waterCell || facts->isBridge) {
                fail("parsed native TMP and actual MapFacts disagree at " + std::to_string(cell.x) + ',' + std::to_string(cell.y));
                return std::unique_ptr<RuntimeWindowState>{};
            }
        }
        const auto add = [&](int id, const std::string& type, WorldObjectKind kind,
                RuntimeCellPoint cell, const std::string& owner, const char* mission) {
            WorldObjectInstance o; o.instanceId = id; o.mapId = "MagnetronFall" + std::to_string(id);
            o.type = type; o.kind = kind; o.owner = owner; o.x = cell.x; o.y = cell.y;
            o.cell = runtimeCellKey(cell.x, cell.y); o.alive = o.poweredOperational = true; o.mission = mission;
            o.health = runtimeMaxHealthForObject(s, o); o.facing = runtimeFacingFromDelta(1, 0);
            o.turretFacing = o.facing; initializeRuntimeObjectAmmo(o, runtimeAmmoRulesForObject(s.rules, o));
            applyRuntimeInitialObjectFlightState(s.rules, o, true); runtimeInitializeModExtensions(o, s.rules, hz);
            s.world.accessObjects().push_back(std::move(o));
        };
        // Restored ships legitimately resume Guard. Keep the real TELE beyond
        // the stock 155mm Range=8 so the half-second landing check is peaceful,
        // without editing either unit's mission, cooldown or weapon rules.
        add(1, "TELE", WorldObjectKind::Unit, {naval ? 75 : 76, 80}, localOwner, "Guard");
        add(2, targetType, WorldObjectKind::Unit, {84, 80}, "SkirmishPlayer2", "Sleep");
        add(100, "YAPOWR", WorldObjectKind::Structure, {104, 104}, localOwner, "Sleep");
        add(998, "NANRCT", WorldObjectKind::Structure, {52, 52}, "SkirmishPlayer2", "Sleep");
        s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner = localOwner; s.economy.credits = 20000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        RuntimeConfig::PlayerSlot local; local.enabled = local.human = local.local = true;
        local.name = "Magnetron local"; local.country = "YuriCountry"; local.team = "-";
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
    struct Pose {
        double x = 0, y = 0, z = 0; int cellX = 0, cellY = 0, level = 0, hp = 0;
        bool airborne = false, render = false, moving = false, alive = false; std::string mission;
        bool operator==(const Pose&) const = default;
    };
    const auto pose = [](const WorldObjectInstance& o) {
        return Pose{runtimeObjectRenderX(o), runtimeObjectRenderY(o), runtimeObjectRenderLevel(o), o.x, o.y, o.level,
            o.health, o.airborne, o.hasRenderPosition, o.renderMoveActive, o.alive, o.mission};
    };
    const auto describe = [&](const RuntimeWindowState& s) {
        std::ostringstream out; out << std::setprecision(17) << "tick=" << s.tick;
        if (const auto* o = findRuntimeObjectByIdIncludingInactive(s, 2)) { const auto p = pose(*o);
            out << " target=" << o->type << " cell=" << p.cellX << ',' << p.cellY << " XYZ=" << p.x << ',' << p.y << ',' << p.z
                << " HP=" << p.hp << " air/render/moving/alive=" << p.airborne << '/' << p.render << '/' << p.moving << '/' << p.alive
                << " mission=" << p.mission;
        } else out << " target=missing";
        if (const auto* b = runtimeMagBeamBindingForTarget(s, 2)) out << " binding=" << b->bindingId
            << " phase=" << runtimeMagBeamPhaseName(b->phase) << " VXYZ=" << b->kinematics.velocityX << ','
            << b->kinematics.velocityY << ',' << b->kinematics.velocityZ;
        else out << " binding=none";
        out << " simulation=" << s.simulationStatus; return out.str();
    };
    const auto checkpoint = [&](RuntimeWindowState& s, bool exact) {
        std::ostringstream ordinary, complete, canonical; IniFile base, snapshot;
        if (!writeRuntimeStateToStream(s, ordinary, error) || !base.loadFromString(ordinary.str())) return std::unique_ptr<RuntimeWindowState>{};
        if (exact && (!writeRuntimeStateToStream(s, complete, error) || !writeRuntimeMultiplayerSnapshotExtension(s, complete, error)
            || !snapshot.loadFromString(complete.str()) || !writeRuntimeMultiplayerSnapshotCanonicalIni(snapshot, canonical, error)
            || !snapshot.loadFromString(canonical.str()))) return std::unique_ptr<RuntimeWindowState>{};
        if (!output.empty()) {
            const auto path = std::filesystem::path(output) / (label + (exact ? "-backed-map-canonical-extension.ini" : "-backed-map-ordinary.ini"));
            std::ofstream payload(path, std::ios::binary | std::ios::trunc);
            payload << (exact ? canonical.str() : ordinary.str());
            if (!payload) { error = "actual fall snapshot payload could not be saved"; return std::unique_ptr<RuntimeWindowState>{}; }
        }
        auto other = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*other, s, false);
        // Ordinary v14 retains AV authority; the real canonical extension is
        // applied after that base, rather than pretending to be a V15 envelope.
        if (!materializeRuntimeLoadStateFromIni(*other, base, error)
            || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*other, snapshot, error))) return std::unique_ptr<RuntimeWindowState>{};
        initializeRuntimeOfflineGameplayRouting(*other); if (exact) ++exactSaves; else ++ordinarySaves; return other;
    };
    const auto compare = [&](const RuntimeWindowState& a, const RuntimeWindowState& b) {
        const auto* x = findRuntimeObjectByIdIncludingInactive(a, 2); const auto* y = findRuntimeObjectByIdIncludingInactive(b, 2);
        if (!x || !y || a.tick != b.tick || a.magBeams.stateHash() != b.magBeams.stateHash() || pose(*x) != pose(*y)
            || a.economy.credits != b.economy.credits) return fail("saved fall clock/kinematics/physical target changed: source=["
                + describe(a) + "] restored=[" + describe(b) + ']');
        ++pairedTicks; return true;
    };
    const auto capture = [&](RuntimeWindowState& s, const char* phase) {
        if (output.empty()) return true; const auto* target = findObjectById(s, 2); if (!target) return fail("capture target missing");
        const auto path = std::filesystem::path(output) / (label + "-backed-map-" + phase + ".bmp");
        const auto frame = drawRuntimeStateOffscreenAtCell(s, {target->x, target->y}, 800, 600, error, path.string(), false);
        if (!frame || !frame->rendered || frame->visibleVoxelBodies < 1) return fail("actual original vehicle raster unavailable: " + error);
        ++captures; return true;
    };

    // Native chain: 70FEE0 / 70FF25 marks an above-ground magnetic target
    // crashing; Jumpjet.Process selects state 5. Its magnetic branch 54CAC4
    // clears horizontal movement, 54CADE consumes the OLD Crash speed, and
    // 54CAEF increments Crash by one lepton per native frame. The initial
    // Crash comes from the target type (stock fallback 5). Mapping authored
    // frames to 15 per simulation second is this engine's clock policy, not
    // a retail wall-time measurement. Its continuous Q16 adaptation does not
    // claim that each 30/60 Hz subtick matches retail integer-Z truncation.
    constexpr double authoredFramesPerSecond = 15.0;
    constexpr double levelsPerLepton = 2.449489742783178 / 256.0;
    constexpr std::int64_t q16 = 65536;
    const auto encode = [](double value) { return static_cast<std::int64_t>(std::llround(value * 65536.0)); };
    const auto decode = [](std::int64_t value) { return static_cast<double>(value) / 65536.0; };
    struct Expected {
        double x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, crash = 5;
        int ground = 0, steps = 0;
    };
    const auto nativeStep = [&](const Expected& p, int hz) {
        const double framesPerTick = authoredFramesPerSecond / hz;
        const std::int64_t acceleration = std::max<std::int64_t>(1,
            encode(levelsPerLepton * framesPerTick * framesPerTick));
        const std::int64_t oldStep = p.vz < 0 ? encode(p.vz)
            : -std::max<std::int64_t>(1, encode(p.crash * levelsPerLepton * framesPerTick));
        return std::pair<std::int64_t, std::int64_t>{oldStep, acceleration};
    };
    const auto advanceExpected = [&](Expected& p, int hz) {
        const auto [oldStep, acceleration] = nativeStep(p, hz);
        const std::int64_t ground = static_cast<std::int64_t>(p.ground) * q16;
        const std::int64_t nextZ = encode(p.z) + oldStep;
        p.z = decode(std::max(ground, nextZ)); p.vx = p.vy = 0;
        p.vz = nextZ <= ground ? 0 : decode(oldStep - acceleration); ++p.steps;
        return true;
    };
    const auto fallBudget = [&](const Expected& p, int hz) {
        const auto [oldStep, acceleration] = nativeStep(p, hz);
        const std::int64_t remaining = encode(p.z) - static_cast<std::int64_t>(p.ground) * q16;
        // Independent sum of the old-speed arithmetic progression. A complete
        // frame must contact on exactly this final step; no arbitrary timeout.
        for (std::int64_t n = 1; n <= hz * 60; ++n)
            if (n * (-oldStep) + acceleration * n * (n - 1) / 2 >= remaining) return static_cast<int>(n);
        return 0;
    };

    struct Timing { int hz = 0, steps = 0; double releaseZ = 0, releaseVZ = 0, seconds = 0; };
    std::map<std::string, std::vector<Timing>> timings;
    const auto run = [&](const std::string& type, int hz, bool fast) {
        const bool naval = type == "DEST", early = type == "FV" || naval, movementRelease = type == "HTNK";
        auto box = make(type, hz, fast, naval); if (!box) return false;
        auto& s = *box; RuntimeSnapshotActiveStateScope active(s); Input input{s};
        stage = "actual original input / terrain / local authority"; if (!openInput(input)) return false;
        const auto* targetType = findRuntimeUnitTypeById(s.rules, type);
        const auto* target = findObjectById(s, 2); const auto* source = findObjectById(s, 1);
        if (!targetType || !target || !source || target->health != targetType->strength || source->health != tele->strength
            || targetType->naval != naval || runtimeNoCooldownForOwner(s, localOwner) != fast)
            return fail("original HP/type/local NoCooldown fixture invalid");
        const int originalHP = target->health;
        const std::string crashText = originalRules.getValue(type, "JumpjetCrash",
            originalRules.getValue("JumpjetControls", "Crash", "5"));
        double originalCrash = 0; std::istringstream crashInput(crashText); crashInput >> originalCrash >> std::ws;
        if (!crashInput.eof() || !std::isfinite(originalCrash) || originalCrash <= 0 || targetType->jumpjetCrash != originalCrash)
            return fail("original target JumpjetCrash / global / fallback parameter changed");
        if (naval) {
            const auto terrain = s.terrainByCell.find(runtimeCellKey(target->x, target->y));
            if (terrain == s.terrainByCell.end() || !terrain->second
                || runtimeNavigationTerrainInfoForCell(s, *terrain->second).movementLandType != RuntimeTerrainLandType::Water
                || !runtimeMapQueryPassabilityAllows(s, {target->x, target->y}, *target))
                return fail("original Water2 naval landing fixture is not genuinely passable");
            ++nativeWaterChecks;
        }
        const auto pixel = objectPoint(s, 2); if (!pixel) return fail("actual original target has no legal mouse hit pixel");
        applySelection(s, {1}, false, false);
        if (runtimeTargetCursorIntent(s, *findObjectById(s, 2)) != RuntimeTargetCursorIntent::Attack)
            return fail("original primary attack cursor unavailable");
        click(input, *pixel); const auto attack = s.behaviors.find(1);
        if (attack == s.behaviors.end() || attack->second.kind != RuntimeBehaviorKind::Attack || attack->second.targetObjectId != 2)
            return fail("actual WM Primary did not queue the original beam: " + s.interactionStatus);
        stage = early ? "real rising release height" : "real held / pulled release height";
        bool prepared = false;
        for (int frame = 0; frame < hz * 12; ++frame) {
            step(s); if (!fixtureFailure.empty()) return fail(fixtureFailure);
            const auto* b = runtimeMagBeamBindingForTarget(s, 2); target = findObjectById(s, 2);
            if (!target || target->health != originalHP) return fail("original capture injured/removed the vehicle: " + describe(s));
            if (!b) continue;
            if (b->sourceObjectId != 1 || b->targetObjectId != 2 || b->weaponId != "MagneticBeam"
                || !target->hasRenderPosition || target->renderX != b->kinematics.worldX
                || target->renderY != b->kinematics.worldY || target->renderLevel != b->kinematics.worldZ)
                return fail("real beam did not project its exact continuous target pose: " + describe(s));
            const double height = b->kinematics.worldZ - b->kinematics.location.level;
            if (early ? b->phase == RuntimeMagBeamPhase::Lifting && height >= 0.4 && height < 1.2
                : b->phase == RuntimeMagBeamPhase::Carried && b->kinematics.velocityZ == 0
                    && (!movementRelease || b->kinematics.worldX <= 83.5)) { prepared = true; break; }
        }
        const auto* held = runtimeMagBeamBindingForTarget(s, 2);
        if (!prepared || !held) return fail("actual original weapon did not reach the release stage: " + describe(s));
        const auto beforeRelease = held->kinematics; const int commandTick = s.tick;
        stage = "actual Win32 S / map Move release";
        if (movementRelease) { if (!move(input, {72, 80})) return false; }
        else stop(input);
        if (const auto order = s.behaviors.find(1); order != s.behaviors.end() && order->second.kind == RuntimeBehaviorKind::Attack)
            return fail("actual release input retained the beam Attack");
        bool released = false;
        for (int frame = 0; frame < 3; ++frame) {
            const auto* b = runtimeMagBeamBindingForTarget(s, 2);
            if (b && b->phase == RuntimeMagBeamPhase::Falling) { released = true; break; }
            step(s); if (!fixtureFailure.empty()) return fail(fixtureFailure);
        }
        const auto* falling = runtimeMagBeamBindingForTarget(s, 2);
        if (!released || !falling || falling->phaseChangedTick < commandTick || falling->phaseChangedTick > commandTick + 1
            || falling->kinematics.worldX != beforeRelease.worldX || falling->kinematics.worldY != beforeRelease.worldY
            || falling->kinematics.worldZ != beforeRelease.worldZ)
            return fail("actual release snapped or failed to enter Falling continuously: " + describe(s));
        Expected expected{falling->kinematics.worldX, falling->kinematics.worldY, falling->kinematics.worldZ,
            falling->kinematics.velocityX, falling->kinematics.velocityY, falling->kinematics.velocityZ,
            originalCrash, falling->kinematics.location.level, 0};
        const Expected initial = expected; const int releaseTick = s.tick, budget = fallBudget(expected, hz);
        if (budget <= 0) return fail("independent native fall deadline is not finite and positive");
        if (csv) csv << std::setprecision(17) << label << ",source," << hz << ',' << releaseTick
            << ",release-transition," << releaseTick << ",0," << initial.x << ',' << initial.y << ',' << initial.z << ','
            << initial.vx << ',' << initial.vy << ',' << initial.vz << ',' << initial.x << ',' << initial.y << ',' << initial.z
            << ',' << initial.vz << ',' << initial.ground << ',' << originalHP << '\n';
        const bool saveCase = type == "MTNK" && !fast;
        std::unique_ptr<RuntimeWindowState> ordinary, exact;
        if (type == "MTNK" && hz == 30 && !fast && !capture(s, "released")) return false;
        stage = "independent native complete-tick fall trajectory";
        bool grounded = false;
        for (int frame = 0; frame < budget; ++frame) {
            if (!advanceExpected(expected, hz)) return false;
            step(s); if (ordinary) step(*ordinary); if (exact) step(*exact);
            if (!fixtureFailure.empty()) return fail(fixtureFailure);
            const auto* b = runtimeMagBeamBindingForTarget(s, 2); target = findObjectById(s, 2);
            if (!target || target->health != originalHP) return fail("legal empty native landing injured the vehicle: " + describe(s));
            const bool contact = expected.z <= expected.ground;
            if (b) {
                if (contact || b->phase != RuntimeMagBeamPhase::Falling || b->kinematics.worldX != expected.x
                    || b->kinematics.worldY != expected.y || b->kinematics.worldZ != expected.z
                    || b->kinematics.velocityX != 0 || b->kinematics.velocityY != 0
                    || b->kinematics.velocityZ != expected.vz || !target->hasRenderPosition
                    || target->renderX != expected.x || target->renderY != expected.y || target->renderLevel != expected.z)
                    return fail("actual fall differs from the independent native step=" + std::to_string(expected.steps) + " " + describe(s));
            } else if (!contact || target->airborne || target->mission == "MagBeamSuspended"
                || target->level != expected.ground || runtimeObjectRenderLevel(*target) != expected.ground
                || target->x != static_cast<int>(std::llround(expected.x)) || target->y != static_cast<int>(std::llround(expected.y)))
                return fail("contact/locomotor/ground height changed before the native touchdown: " + describe(s));
            ++trajectoryChecks;
            if (csv) csv << std::setprecision(17) << label << ",source," << hz << ',' << s.tick << ','
                << (b ? runtimeMagBeamPhaseName(b->phase) : "ground") << ',' << releaseTick << ',' << double(s.tick-releaseTick)/hz << ','
                << runtimeObjectRenderX(*target) << ',' << runtimeObjectRenderY(*target) << ',' << runtimeObjectRenderLevel(*target) << ','
                << (b ? b->kinematics.velocityX : 0) << ',' << (b ? b->kinematics.velocityY : 0) << ',' << (b ? b->kinematics.velocityZ : 0) << ','
                << expected.x << ',' << expected.y << ',' << expected.z << ',' << expected.vz << ',' << expected.ground << ',' << target->health << '\n';
            if (ordinary && !compare(s, *ordinary)) return false;
            if (exact && !compare(s, *exact)) return false;
            if (saveCase && frame == 1 && b) {
                stage = "Falling ordinary / actual canonical extension checkpoint";
                ordinary = checkpoint(s, false); exact = checkpoint(s, true);
                if (!ordinary || !exact || !compare(s, *ordinary) || !compare(s, *exact)) return fail("fall checkpoint failed: " + error);
                const auto hash = s.magBeams.stateHash(); const auto savedPose = pose(*target);
                completeRuntimeWorldRefresh(s); completeRuntimeWorldRefresh(s);
                if (hash != s.magBeams.stateHash() || savedPose != pose(*findObjectById(s, 2))) return fail("same-tick refresh advanced a paused fall");
                stage = "independent native complete-tick fall trajectory";
            }
            if (contact) { grounded = true; break; }
        }
        if (!grounded || runtimeMagBeamBindingForTarget(s, 2)) return fail("native finite fall deadline expired without real contact");
        timings[type + (fast ? "-instant" : "-normal")].push_back({hz, expected.steps, initial.z, initial.vz, double(s.tick-releaseTick)/hz});
        stage = "natural landing occupancy / stable restored locomotor";
        target = findObjectById(s, 2); const Pose landed = pose(*target);
        if (!runtimeMapQueryCell(s, {target->x, target->y}) || !runtimeMapQueryPassabilityAllows(s, {target->x, target->y}, *target)
            || runtimeMagBeamLandingOccupancy(s, 2, {target->x, target->y}).occupied)
            return fail("native landing is not genuinely legal/empty; hidden carrier aircraft cannot be hard occupants");
        for (int frame = 0; frame < hz / 2; ++frame) {
            step(s); if (ordinary) step(*ordinary); if (exact) step(*exact); target = findObjectById(s, 2);
            if (!fixtureFailure.empty()) return fail(fixtureFailure);
            if (!target || pose(*target) != landed || runtimeMagBeamBindingForTarget(s, 2)) return fail("landed vehicle drifted/restarted/regrabbed: " + describe(s));
            if (ordinary && !compare(s, *ordinary)) return false; if (exact && !compare(s, *exact)) return false; ++stableChecks;
        }
        if (type == "MTNK" && hz == 30 && !fast && !capture(s, "grounded")) return false;
        std::cout << "Magnetron fall timing: " << label << " releaseZ=" << std::setprecision(17) << initial.z
            << " incomingVZ=" << initial.vz << " originalCrash=" << originalCrash << " steps=" << expected.steps
            << " independentBudget=" << budget << " simulationSeconds=" << double(s.tick - releaseTick - hz / 2) / hz << '\n';
        return true;
    };
    const auto execute = [&](const std::string& type, int hz, bool fast) {
        label = type + '-' + std::to_string(hz) + (fast ? "-instant" : "-normal"); if (!filter.empty() && label != filter) return;
        stage = "fixture"; fixtureFailure.clear(); error.clear(); std::cout << "Magnetron fall: " << label << " begin\n";
        const bool passed = run(type, hz, fast); ++scenes;
        if (passed) std::cout << "Magnetron fall: " << label << " passed\n";
        else { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    };
    for (const std::string type : {"MTNK", "HTNK", "FV", "DEST"}) for (int hz : {30, 60}) execute(type, hz, false);
    for (int hz : {30, 60}) execute("MTNK", hz, true);
    if (scenes == 0) return fail("CASE selected no actual scene");
    if (!failures.empty()) { error = failures.front() + "; " + std::to_string(failures.size()) + " failing scenes"; return false; }
    stage = "matched original-height frequency / NoCooldown timing";
    for (const std::string mode : {"normal", "instant"}) {
        const auto found = timings.find("MTNK-" + mode);
        if (found == timings.end() || found->second.size() < 2) continue; // CASE has only its selected scene.
        const auto& a = found->second[0]; const auto& b = found->second[1];
        if (a.releaseZ != b.releaseZ || a.releaseVZ != b.releaseVZ
            || std::abs(a.seconds - b.seconds) > 1.0 / authoredFramesPerSecond)
            return fail("same original release height changed fall duration beyond one authored 15 Hz frame: "
                + std::to_string(a.seconds) + '/' + std::to_string(b.seconds));
    }
    for (int hz : {30, 60}) {
        const auto normal = timings.find("MTNK-normal"), instant = timings.find("MTNK-instant");
        if (normal == timings.end() || instant == timings.end()) continue;
        const auto a = std::find_if(normal->second.begin(), normal->second.end(), [&](const auto& p) { return p.hz == hz; });
        const auto b = std::find_if(instant->second.begin(), instant->second.end(), [&](const auto& p) { return p.hz == hz; });
        if (a != normal->second.end() && b != instant->second.end()
            && (a->releaseZ != b->releaseZ || a->releaseVZ != b->releaseVZ || a->steps != b->steps || a->seconds != b->seconds))
            return fail("local NoCooldown changed original fall physics at Hz=" + std::to_string(hz));
    }
    std::cout << "Magnetron fall: scenes=" << scenes << " fullTicks=" << fullTicks << " actualClicks=" << clicks
        << " actualS=" << keys << " nativeTrajectory=" << trajectoryChecks << " saved=" << ordinarySaves << '+' << exactSaves
        << " pairedTicks=" << pairedTicks << " stable=" << stableChecks << " nativeWater=" << nativeWaterChecks << " captures=" << captures << "; passed\n";
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
