// Issue 22/20. The sidebar and both targets enter through the real window
// procedure. An empty area is a valid cast, even when its ground pixel also
// picks a sprite; the picked object is recorded separately from the wire hint.
// Ordinary14 restores authority/particles, not the transient CHRONO animations.
// The canonical extension is rebuilt after explicitly clearing those effects.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeChronosphereEmptyGroundSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    const char* configured = std::getenv("CONCORD_CHRONOSPHERE_EMPTY_GROUND_OUTPUT");
    const std::filesystem::path output = configured && *configured
        ? configured : "build/issue22_20_chronosphere_empty_ground";
    const char* selected = std::getenv("CONCORD_CHRONOSPHERE_EMPTY_GROUND_CASE");
    const std::string filter = selected ? selected : "";
    std::filesystem::create_directories(output / "native-original");
    std::string label, stage;
    const auto fail = [&](const std::string& why) {
        error = "Chronosphere empty ground " + label + " [" + stage + "]: " + why; return false;
    };
    std::ofstream cases(output / "cases.csv"), clicks(output / "clicks.csv"), ticks(output / "ticks.csv"),
        objects(output / "objects.csv"), events(output / "events.csv"), checkpoints(output / "checkpoints.csv"),
        assets(output / "assets.csv");
    if (!cases || !clicks || !ticks || !objects || !events || !checkpoints || !assets) return fail("output unavailable");
    cases << "case,hz,scenario,source_x,source_y,destination_x,destination_y,source_id,destination_id,source_occupied,destination_occupied,cancelled,command_tick,captured_commands,accepted_commands,applied_events,warp_in_events,warp_away_events,recharge_ticks,natural_ticks,ordinary14,canonical_extension,paired_ticks,recharge_clock_ticks,result\n";
    clicks << "case,stage,tick,requested_x,requested_y,pixel_x,pixel_y,picked_x,picked_y,picked_object,picked_type,picked_owner,wire_object,intent_targets,marker,accepted_commands,interaction_hex\n";
    ticks << std::setprecision(17) << "case,branch,stage,tick,hz,state,remaining,recharge,progress,superweapon_hash,particle_hash,particle_tick,particle_systems,particles,local_intent,local_marker,chrono_source,chrono_destination\n";
    objects << std::setprecision(17) << "case,branch,stage,tick,object,type,owner,kind,hp,alive,x,y,z,body_x,body_y,body_z,mission\n";
    events << "case,branch,tick,sequence,event,object,target,source_x,source_y,destination_x,destination_y,reason_hex\n";
    checkpoints << "case,format,tick,hz,superweapon_hash,particle_hash,particle_tick,chrono_source,chrono_destination,result\n";
    assets << "kind,id,key,value,source,source_fnv,raw_frames\n";
    IniFile raw, rawArt; RulesDatabase original; ArtDatabase originalArt;
    if (!raw.loadFromFile(live.report.rulesPath) || !original.loadFromIni(raw)
        || !rawArt.loadFromFile(live.report.artPath) || !originalArt.loadFromIni(rawArt)) return fail("original rules/art unavailable");
    const auto authored = [](const std::string& text) {
        return trimAsciiLocal(text.substr(0, text.find(';')));
    };
    const std::string powerId = "ChronoSphereSpecial";
    const auto* rule = original.findSuperWeaponType(powerId);
    const auto* helper = original.findSuperWeaponType("ChronoWarpSpecial");
    const auto* tankType = original.findUnitType("vehicle", "MTNK");
    const auto* sourceType = original.findBuildingType("GACSPH");
    const auto* powerType = original.findBuildingType("NANRCT");
    const auto* groundBuilding = original.findBuildingType("GAPOWR");
    const double rechargeMinutes = std::stod(authored(raw.getValue(powerId, "RechargeTime", "0")));
    if (!rule || !helper || !tankType || !sourceType || !powerType || !groundBuilding
        || tankType->strength != 300 || sourceType->strength != 750 || powerType->strength != 1000
        || sourceType->superWeapon != powerId || !rule->preClick || !helper->postClick
        || helper->preDependent != "ChronoSphere" || rechargeMinutes != 7.0
        || rule->rechargeTimeMinutes != rechargeMinutes || rule->targetBuildings || !rule->targetEnemy
        || rule->placementAnim != authored(raw.getValue("General", "ChronoPlacement", "CHRONOAR"))
        || rule->firstAnim != authored(raw.getValue("General", "ChronoBlast", ""))
        || rule->secondAnim != authored(raw.getValue("General", "ChronoBlastDest", "")))
        return fail("original Chronosphere pairing, charge, or stock types differ");
    const std::uint64_t rulesHash = original.getDetailRulesHash();
    assets << "rule," << powerId << ",RechargeTime," << rechargeMinutes << ',' << live.report.rulesPath << ",0,0\n"
        << "rule," << powerId << ",Range," << authored(raw.getValue(powerId, "Range", "")) << ',' << live.report.rulesPath << ",0,0\n"
        << "unit,MTNK,Strength,300," << live.report.rulesPath << ",0,0\n"
        << "building,GACSPH,Strength,750," << live.report.rulesPath << ",0,0\n"
        << "building,NANRCT,Strength,1000," << live.report.rulesPath << ",0,0\n";
    // Retain the actual MIX bytes/header, independently of the animation
    // resolver. The original stock SHPs are only evidence, never rewritten.
    std::set<std::string> animIds{rule->placementAnim, rule->firstAnim, rule->secondAnim, rule->launchAnim, rule->impactAnim};
    std::map<std::string, int> rawFrameCounts;
    for (const auto& anim : animIds) {
        const std::string image = authored(rawArt.getValue(anim, "Image", anim));
        const std::string filename = toLowerAsciiLocal(image) + ".shp";
        std::vector<std::uint8_t> bytes; std::string source;
        for (const char* directory : {"ra2md", "ra2"}) {
            const auto folder = std::filesystem::path(runtimeAssetRootPath(live.config)) / directory;
            if (!std::filesystem::is_directory(folder)) continue;
            std::vector<std::filesystem::path> archives;
            for (const auto& file : std::filesystem::directory_iterator(folder))
                if (file.is_regular_file() && toLowerAsciiLocal(file.path().extension().string()) == ".mix") archives.push_back(file.path());
            std::sort(archives.begin(), archives.end());
            for (const auto& path : archives) {
                MixArchive archive; if (!archive.loadFromPath(path)) continue;
                const auto* entry = archive.findEntryByName(filename); if (!entry) continue;
                if (!archive.readEntryData(*entry, bytes, error)) return fail("original SHP read: " + error);
                source = pathString(path) + "!" + filename; break;
            }
            if (!source.empty()) break;
        }
        if (bytes.size() < 8 || source.empty()) return fail("original animation bytes missing: " + anim);
        const int frames = int(bytes[6]) | int(bytes[7]) << 8;
        if (frames <= 0) return fail("original animation header invalid: " + anim);
        std::uint64_t fingerprint = 14695981039346656037ULL;
        for (const auto value : bytes) fingerprint = (fingerprint ^ value) * 1099511628211ULL;
        std::ofstream file(output / "native-original" / filename, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) return fail("original SHP evidence write");
        rawFrameCounts[anim] = frames;
        assets << "animation," << anim << ",Image," << image << ',' << source << ',' << fingerprint << ',' << frames << '\n';
    }
    int landTile = -1;
    for (int tile = 0; tile < static_cast<int>(live.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(live, cell);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge) { landTile = tile; break; }
    }
    if (landTile < 0) return fail("original passable Clear TMP unavailable");
    assets << "terrain,Clear,TileIndex," << landTile << ",original-theater,0,0\n";
    const RECT client{0, 0, 1024, 768}, viewport = runtimeMapViewport(client);
    const auto add = [&](RuntimeWindowState& s, const std::string& type, WorldObjectKind kind,
        RuntimeCellPoint cell, const std::string& owner) {
        auto context = runtimeSuperWeaponContext(s); WorldObjectInstance object;
        object.instanceId = context.allocateObjectId(); object.kind = kind; object.type = type; object.owner = owner;
        object.x = cell.x; object.y = cell.y; object.cell = runtimeCellKey(cell.x, cell.y);
        object.alive = object.poweredOperational = true; object.mission = "Sleep";
        object.health = runtimeMaxHealthForObject(s, object);
        initializeRuntimeObjectAmmo(object, runtimeAmmoRulesForObject(s.rules, object));
        applyRuntimeInitialObjectFlightState(s.rules, object); runtimeInitializeModExtensions(object, s.rules, s.config.fixedTickHz);
        const int id = object.instanceId; s.world.accessObjects().push_back(std::move(object)); return id;
    };
    const auto make = [&](int hz) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        seedRuntimeLoadScratch(s, live, false); s.config = live.config; s.tick = 0;
        s.config.fixedTickHz = hz; s.config.playerCountry = "Americans"; s.config.gameMode = "Battle";
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.noCooldown = s.config.crates = s.config.shortGame = false;
        s.config.superweapons = true; s.config.startingUnits = 0;
        s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.rules = original; s.art = originalArt; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.mapFacts = {}; s.mapFacts.theater = live.mapFacts.theater; s.mapFacts.lighting = live.mapFacts.lighting;
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.campaignMode = false;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y = 55; y <= 124; ++y) for (int x = 55; x <= 124; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = landTile; cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        add(s, "GACSPH", WorldObjectKind::Structure, {62, 62}, "Commander");
        add(s, "NANRCT", WorldObjectKind::Structure, {62, 84}, "Commander");
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = s.visionEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.skirmishMatchFinished = s.skirmishVictory = false; s.skirmishResult.clear();
        s.computerAiByOwner.clear(); s.computerMiningByOwner.clear();
        ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner = "Commander"; s.economy.credits = 10000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; s.triggers.loaded = s.triggers.bootstrapComplete = true;
        RuntimeConfig::PlayerSlot local; local.enabled = local.human = local.local = true;
        local.name = "Commander"; local.country = "Americans"; local.color = "Gold";
        RuntimeConfig::PlayerSlot enemy; enemy.enabled = true; enemy.human = enemy.local = false;
        enemy.name = "Quiet hostile"; enemy.country = "Russians"; enemy.color = "Red";
        s.config.playerSlots = {local, enemy};
        s.skirmishCountryByOwner = {{"commander", "Americans"}, {"skirmishplayer2", "Russians"}};
        s.skirmishTeamByOwner.clear(); s.skirmishDifficultyByOwner.clear(); s.playerCommands.alliances.clear();
        add(s, "NANRCT", WorldObjectKind::Structure, {56, 56}, "SkirmishPlayer2");
        s.humanHouseAuthorities.clear(); RuntimeHumanHouseAuthority authority;
        authority.owner = "Commander"; authority.canonicalOwner = "commander"; authority.country = "Americans"; authority.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(authority));
        initializeRuntimeNavigation(s); ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        ensureRuntimeSuperWeaponsInitialized(s); rebuildRuntimeEconomyPower(s); initializeRuntimeOfflineGameplayRouting(s);
        return box;
    };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
    };
    if (std::getenv("CONCORD_CHRONOSPHERE_RELOCATION_HOST_CONTRACT"))
        return runRuntimeChronosphereRelocationHostContract(make, add, step, error);
    const auto animCount = [](const RuntimeWindowState& s, const std::string& anim) {
        return static_cast<int>(std::count_if(s.impactEffects.begin(), s.impactEffects.end(), [&](const auto& e) {
            return e.animId == anim && e.remainingTicks > 0;
        }));
    };
    const auto impactRecords = [](const RuntimeWindowState& s) {
        std::vector<std::string> result;
        for (const auto& e : s.impactEffects) {
            result.push_back(runtimeMultiplayerSnapshotImpactRecord(e) + '|' + e.animId + '|' + e.renderIntent
                + '|' + std::to_string(e.frameIndex) + '|' + std::to_string(e.radius) + '|' + std::to_string(e.facing));
        }
        return result;
    };
    const auto worldRecords = [](const RuntimeWindowState& s) {
        std::map<int, std::string> result;
        for (const auto& o : s.world.getObjects()) {
            std::ostringstream text; text << std::setprecision(17) << o.type << '|' << o.owner << '|' << int(o.kind)
                << '|' << o.health << '|' << o.alive << '|' << o.x << '|' << o.y << '|' << o.level << '|' << o.cell
                << '|' << o.mission << '|' << o.onBridge << '|' << runtimeObjectRenderX(o) << '|' << runtimeObjectRenderY(o)
                << '|' << runtimeDrawLevelForObject(o);
            result.emplace(o.instanceId, text.str());
        }
        return result;
    };
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"ConcordChronosphereEmptyGroundSmoke";
    WNDCLASSW wc{}; wc.lpfnWndProc = runtimeWindowProc; wc.hInstance = instance; wc.lpszClassName = className;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return fail("real input window class");
    struct ClassCleanup { HINSTANCE instance; const wchar_t* name; ~ClassCleanup() { UnregisterClassW(name, instance); } } classCleanup{instance, className};
    HWND window = CreateWindowExW(0, className, L"Chronosphere empty ground", WS_POPUP, 0, 0, 1024, 768, nullptr, nullptr, instance, nullptr);
    if (!window) return fail("real input window");
    struct WindowCleanup { HWND window; ~WindowCleanup() {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0); SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DefWindowProcW));
        if (GetCapture() == window) ReleaseCapture(); DestroyWindow(window);
    } } windowCleanup{window};
    const auto mouse = [&](RuntimeWindowState& s, POINT point, bool right = false) {
        RuntimeSnapshotActiveStateScope active(s);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));
        SendMessageW(window, right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, right ? MK_RBUTTON : MK_LBUTTON, MAKELPARAM(point.x, point.y));
        SendMessageW(window, right ? WM_RBUTTONUP : WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
    };
    const auto button = [&](RuntimeWindowState& s) -> std::optional<POINT> {
        s.activeBuildTab = RuntimeBuildKind::Defense; const RECT sidebar = runtimeSidebarRect(client);
        for (int page = 0; page < runtimeBuildPageCount(s, sidebar); ++page) {
            runtimeSetActiveBuildPage(s, page); const int offset = static_cast<int>(runtimeVisibleBuildCatalogIndices(s, sidebar).size());
            const auto visible = runtimeVisibleSuperWeaponsOnBuildPage(s, sidebar);
            for (std::size_t n = 0; n < visible.size(); ++n) if (visible[n]->superWeaponId == powerId) {
                const auto rect = runtimeBuildButtonRect(sidebar, offset + static_cast<int>(n));
                return POINT{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
            }
        }
        return std::nullopt;
    };
    struct Pick { POINT point{}, cell{}; int id = -1; std::string type, owner; };
    const auto pick = [&](RuntimeWindowState& s, RuntimeCellPoint cell, int expectedObject, Pick& result) {
        RuntimeSnapshotActiveStateScope active(s);
        centerRuntimeCameraOnCell(s, client, cell); ensureRuntimeCameraForViewport(s, viewport);
        const POINT initial = runtimeScreenPointForCell(s, viewport, cell, terrainLevelAtRuntimeCell(s, cell.x, cell.y));
        const auto& projected = runtimeProjectedObjectsForViewport(s, viewport);
        // Search only pixels actually owned by this cell. This cannot replace
        // an intended ground target with a neighbouring sprite/terrain cell.
        const int radius = std::max(1, s.camera.tileHeight);
        for (int distance = 0; distance <= radius; ++distance) for (int dy = -distance; dy <= distance; ++dy)
            for (int dx = -distance; dx <= distance; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != distance) continue;
                const POINT point{initial.x + dx, initial.y + dy};
                if (point.x < viewport.left || point.x >= viewport.right || point.y < viewport.top || point.y >= viewport.bottom) continue;
                const POINT owned = approximateCellFromScreenPoint(s, viewport, point);
                if (owned.x != cell.x || owned.y != cell.y) continue;
                const auto* object = findProjectedObjectAtPoint(projected, point);
                const auto* body = object ? object->object : nullptr; const int id = body ? body->instanceId : -1;
                if (id != expectedObject) continue;
                result.point = point; result.cell = owned; result.id = id;
                result.type = body ? body->type : ""; result.owner = body ? body->owner : ""; return true;
            }
        return fail("requested native ground pixel cannot pick expected object=" + std::to_string(expectedObject)
            + " cell=" + std::to_string(cell.x) + ',' + std::to_string(cell.y));
    };
    int passed = 0, casts = 0, ordinaryCount = 0, canonicalCount = 0, paired = 0, clockChecks = 0, cancelCount = 0;
    std::vector<std::string> failures;
    const std::array<std::string, 8> scenarios{"empty-empty", "occupied-empty", "empty-occupied-own", "occupied-occupied-own",
        "edge-empty-empty", "cancel-empty-empty", "empty-enemy-hover", "empty-building-hover"};
    for (int hz : {30, 60}) for (const auto& scenario : scenarios) {
        label = scenario + '-' + std::to_string(hz); if (!filter.empty() && filter != label) continue;
        stage = "fixture";
        auto box = make(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
        RuntimeCellPoint source{90, 90}, destination{104, 100};
        if (scenario == "edge-empty-empty") { source = {55, 124}; destination = {124, 55}; }
        const bool sourceOccupied = scenario == "occupied-empty" || scenario == "occupied-occupied-own";
        const bool destinationOccupied = scenario == "empty-occupied-own" || scenario == "occupied-occupied-own"
            || scenario == "empty-enemy-hover" || scenario == "empty-building-hover";
        const bool cancelled = scenario == "cancel-empty-empty";
        const bool blocked = scenario == "occupied-occupied-own";
        const int sourceId = sourceOccupied ? add(s, "MTNK", WorldObjectKind::Unit, source, "Commander") : -1;
        const int destinationId = destinationOccupied ? add(s, scenario == "empty-building-hover" ? "GAPOWR" : "MTNK",
            scenario == "empty-building-hover" ? WorldObjectKind::Structure : WorldObjectKind::Unit, destination,
            scenario == "empty-enemy-hover" ? "SkirmishPlayer2" : "Commander") : -1;
        initializeRuntimeNavigation(s); ensureRuntimeSubcellsInitialized(s); rebuildRuntimeEconomyPower(s);
        int captured = 0, accepted = 0, commandTick = -1, applied = 0, warpIn = 0, warpAway = 0;
        int naturalTicks = 0, casePaired = 0, caseClock = 0, caseOrdinary = 0, caseCanonical = 0;
        RuntimeMultiplayerGameplayFireSuperWeaponCommand capturedBody; std::string captureReason;
        s.multiplayerIntentRouter.setCaptureCallback([&](const auto& command, std::string& reason) {
            const auto* body = std::get_if<RuntimeMultiplayerGameplayFireSuperWeaponCommand>(&command.body);
            if (body) { ++captured; capturedBody = *body; commandTick = s.tick; }
            const bool ok = dispatchRuntimeOfflineGameplayCommand(s, command, reason);
            if (body) { captureReason = reason; if (ok) ++accepted; } return ok;
        });
        std::map<const RuntimeWindowState*, std::uint64_t> lastEvents;
        const auto collect = [&](const RuntimeWindowState& q, const char* branch) {
            auto& previous = lastEvents[&q];
            for (const auto& event : q.recentReplayEvents) if (event.sequence > previous) {
                previous = event.sequence;
                if (event.superWeaponId != powerId) continue;
                events << label << ',' << branch << ',' << event.tick << ',' << event.sequence << ',' << event.superWeaponEvent
                    << ',' << event.objectId << ',' << event.targetObjectId << ',' << event.sourceCell.x << ',' << event.sourceCell.y
                    << ',' << event.secondTargetCell.x << ',' << event.secondTargetCell.y << ',' << iniHexEncode(event.reason) << '\n';
                if (&q == &s) {
                    if (event.superWeaponEvent == "ChronoSphereApplied") ++applied;
                    if (event.superWeaponEvent == "ChronoWarpIn") ++warpIn;
                    if (event.superWeaponEvent == "ChronoWarpAway") ++warpAway;
                }
            }
        };
        const auto sample = [&](const RuntimeWindowState& q, const char* branch, const char* recordStage) {
            const auto* snapshot = q.superWeapons.findSidebarSnapshot("Commander", powerId);
            if (!snapshot) return fail("original live sidebar snapshot missing");
            ticks << label << ',' << branch << ',' << recordStage << ',' << q.tick << ',' << hz << ',' << int(snapshot->state)
                << ',' << snapshot->remainingTicks << ',' << snapshot->rechargeTicks << ',' << snapshot->progress << ',' << q.superWeapons.stateHash()
                << ',' << q.particles.stateHash() << ',' << q.particles.currentTick() << ',' << q.particles.systems().size() << ',' << q.particles.particles().size()
                << ',' << q.multiplayerIntentRouter.superWeapon().has_value() << ',' << q.localChronosphereMarker.has_value()
                << ',' << animCount(q, rule->firstAnim) << ',' << animCount(q, rule->secondAnim) << '\n';
            for (const auto& object : q.world.getObjects()) objects << label << ',' << branch << ',' << recordStage << ',' << q.tick << ',' << object.instanceId
                << ',' << object.type << ',' << object.owner << ',' << int(object.kind) << ',' << object.health << ',' << object.alive << ',' << object.x << ',' << object.y
                << ',' << object.level << ',' << runtimeObjectRenderX(object) << ',' << runtimeObjectRenderY(object) << ',' << runtimeDrawLevelForObject(object) << ',' << object.mission << '\n';
            collect(q, branch); return true;
        };
        const auto observeClick = [&](const char* clickStage, RuntimeCellPoint cell, const Pick& picked) {
            const auto& intent = s.multiplayerIntentRouter.superWeapon(); int wireId = -1;
            if (std::string(clickStage) == "second" && !capturedBody.targets.empty()) wireId = capturedBody.targets.back().objectId.value_or(-1);
            else if (intent && !intent->targets.empty()) wireId = intent->targets.back().objectId.value_or(-1);
            clicks << label << ',' << clickStage << ',' << s.tick << ',' << cell.x << ',' << cell.y << ',' << picked.point.x << ',' << picked.point.y
                << ',' << picked.cell.x << ',' << picked.cell.y << ',' << picked.id << ',' << picked.type << ',' << picked.owner << ',' << wireId
                << ',' << (intent ? intent->targets.size() : 0U) << ',' << s.localChronosphereMarker.has_value() << ',' << accepted << ',' << iniHexEncode(s.interactionStatus) << '\n';
        };
        const auto body = [&]() {
            if (s.rules.getDetailRulesHash() != rulesHash || !s.world.getSummary().loaded || s.world.getTerrainCells().size() != 4900
                || runtimeOwnersAreAllied(s, "Commander", "SkirmishPlayer2")) return fail("original rule/map/opponent authority");
            for (const auto& object : s.world.getObjects()) if (object.health != runtimeMaxHealthForObject(s, object)) return fail("fixture did not start at stock maximum HP");
            // The only readiness shortcut is existing smoke setup. All target
            // collection, launch, recharge, relocation and effects are real.
            auto context = runtimeSuperWeaponContext(s); context.emit = runtimeSuperWeaponEventSink(s);
            s.superWeapons.tick(context);
            if (!s.superWeapons.forceReady("Commander", powerId, &context)) return fail("initial ready fixture");
            step(s); collect(s, "original");
            const int recharge = static_cast<int>(std::llround(rechargeMinutes * 60.0 * hz));
            const auto* ready = s.superWeapons.findSidebarSnapshot("Commander", powerId);
            if (!ready || ready->state != SuperWeaponRuntimeState::Ready || ready->rechargeTicks != recharge
                || ready->remainingTicks != 0 || s.config.noCooldown || !s.particlesInitialized || s.particles.currentTick() != s.tick)
                return fail("stock ready/sidebar/particle clock");
            const auto openSidebar = [&]() {
                const auto point = button(s); if (!point) return fail("actual sidebar Chronosphere icon absent");
                mouse(s, *point); const auto& intent = s.multiplayerIntentRouter.superWeapon();
                return intent && intent->superWeaponId == powerId && intent->expectedTargetCount == 2 && intent->targets.empty()
                    && runtimeActiveLocalSuperWeaponTargeting(s) ? true : fail("actual sidebar click did not begin two targets: " + s.interactionStatus);
            };
            stage = "real sidebar"; if (!openSidebar()) return false;
            const auto beforeFirst = s.superWeapons.stateHash(); const auto particlesBefore = s.particles.stateHash();
            Pick first; stage = "first ground click";
            if (!pick(s, source, sourceId, first)) return false;
            mouse(s, first.point); observeClick("first", source, first);
            const auto& firstIntent = s.multiplayerIntentRouter.superWeapon();
            if (!firstIntent || firstIntent->targets.size() != 1 || captured || accepted || !s.localChronosphereMarker
                || s.localChronosphereMarker->animation.cell.x != source.x || s.localChronosphereMarker->animation.cell.y != source.y
                || s.localChronosphereMarker->animation.animationFrameCount <= 0
                || s.localChronosphereMarker->animation.animationFrameCount > rawFrameCounts.at(rule->placementAnim)
                || s.superWeapons.stateHash() != beforeFirst
                || s.particles.stateHash() != particlesBefore) return fail("first real click lost local marker or consumed authoritative charge");
            if (!sample(s, "original", "first-click")) return false;
            if (cancelled) {
                stage = "real right-click cancel"; mouse(s, first.point, true); observeClick("cancel", source, first);
                if (s.multiplayerIntentRouter.superWeapon() || s.localChronosphereMarker || s.chronosphereSelectionAudio.selected
                    || s.superWeapons.stateHash() != beforeFirst || captured || accepted) return fail("native right-click did not cancel without consumption");
                if (!sample(s, "original", "cancelled") || !openSidebar()) return false;
                if (!pick(s, source, sourceId, first)) return false;
                mouse(s, first.point); observeClick("first-after-cancel", source, first);
                const auto& intent = s.multiplayerIntentRouter.superWeapon();
                if (!intent || intent->targets.size() != 1 || !s.localChronosphereMarker || captured) return fail("cancelled ready power could not restart via UI");
                ++cancelCount;
            }
            stage = "second ground click"; Pick second;
            if (!pick(s, destination, destinationId, second)) return false;
            mouse(s, second.point); observeClick("second", destination, second);
            if (!sample(s, "original", "second-click")) return false;
            const std::string image = pathString(output / (label + "-second-click.bmp"));
            refreshRuntimeSpecialMechanismRenderSnapshots(s);
            if (!drawRuntimeStateOffscreenAtCell(s, destination, 960, 660, error, image, false)) return fail("actual target paint: " + error);
            if (captured != 1 || accepted != 1 || capturedBody.superWeaponId != powerId || capturedBody.targets.size() != 2
                || capturedBody.targets[0].cell.x != source.x || capturedBody.targets[0].cell.y != source.y
                || capturedBody.targets[1].cell.x != destination.x || capturedBody.targets[1].cell.y != destination.y
                || s.multiplayerIntentRouter.superWeapon() || s.localChronosphereMarker || s.chronosphereSelectionAudio.selected
                || s.superWeapons.stateOf("Commander", powerId) != SuperWeaponRuntimeState::Launching)
                return fail("two actual clicks did not release area: captured=" + std::to_string(captured) + " accepted=" + std::to_string(accepted)
                    + " picked=" + second.type + '#' + std::to_string(second.id) + " reason=" + captureReason + " status=" + s.interactionStatus);
            stage = "natural launch"; step(s); ++naturalTicks;
            if (!sample(s, "original", "launched")) return false;
            if (applied != 1 || warpIn != int(sourceOccupied && !blocked) || warpAway != int(blocked)
                || animCount(s, rule->firstAnim) != 1 || animCount(s, rule->secondAnim) != 1
                || s.superWeapons.stateOf("Commander", powerId) != SuperWeaponRuntimeState::Cooldown)
                return fail("native empty application/stock transfer/animations/launch deadline");
            for (const auto& effect : s.impactEffects) if (animIds.contains(effect.animId)
                && (effect.animationFrameCount <= 0 || effect.animationFrameCount > rawFrameCounts.at(effect.animId)))
                return fail("native animation metadata exceeds its original SHP header");
            const auto stable = [&](const RuntimeWindowState& q) {
                if (q.skirmishMatchFinished || q.rules.getDetailRulesHash() != rulesHash || q.config.noCooldown
                    || q.particles.currentTick() != q.tick || q.particles.fixedTickHz() != hz
                    || q.multiplayerIntentRouter.superWeapon() || q.localChronosphereMarker || q.chronosphereSelectionAudio.selected)
                    return fail("ended Battle, changed stock rules, clock, or retained selection");
                const auto* snapshot = q.superWeapons.findSidebarSnapshot("Commander", powerId);
                const int remaining = std::max(0, recharge - (q.tick - commandTick));
                if (!snapshot || snapshot->rechargeTicks != recharge || snapshot->remainingTicks != remaining || snapshot->chargePaused
                    || (q.tick == commandTick + 1 ? snapshot->state != SuperWeaponRuntimeState::Cooldown
                        : snapshot->state != SuperWeaponRuntimeState::Charging)) return fail("original 7-minute recharge timeline at tick=" + std::to_string(q.tick));
                if (sourceOccupied) {
                    const auto* object = findRuntimeObjectByIdIncludingInactive(q, sourceId);
                    if (!object || object->type != "MTNK" || object->owner != "Commander"
                        || (blocked ? object->alive || object->health != 0
                            : !object->alive || object->health != tankType->strength || object->x != destination.x || object->y != destination.y))
                        return fail("original Chrono transfer/blocked-destination identity HP");
                }
                if (destinationOccupied) {
                    const auto* object = findObjectById(q, destinationId);
                    if (!object || !object->alive || object->health != (scenario == "empty-building-hover" ? groundBuilding->strength : tankType->strength)
                        || object->x != destination.x || object->y != destination.y) return fail("destination occupant changed in an empty source cast");
                }
                return true;
            };
            if (!stable(s)) return false;
            stage = "real completed-command saves";
            std::ostringstream ordinaryText, extendedText, canonicalText; IniFile ordinaryIni, canonicalIni;
            if (!writeRuntimeStateToStream(s, ordinaryText, error) || !ordinaryIni.loadFromString(ordinaryText.str())
                || ordinaryIni.getValue("RuntimeSave", "Version", "") != "14"
                || ordinaryText.str().find("CHRONOAR") != std::string::npos) return fail("ordinary14 authority save: " + error);
            extendedText << ordinaryText.str();
            if (!writeRuntimeMultiplayerSnapshotExtension(s, extendedText, error) || !canonicalIni.loadFromString(extendedText.str())
                || !writeRuntimeMultiplayerSnapshotCanonicalIni(canonicalIni, canonicalText, error)
                || !canonicalIni.loadFromString(canonicalText.str())) return fail("existing contextual canonical extension: " + error);
            std::array<std::unique_ptr<RuntimeWindowState>, 2> restored;
            for (int format = 0; format < 2; ++format) {
                const char* name = format ? "canonical-extension" : "ordinary14";
                std::ofstream saved(output / (label + '-' + name + ".ini"), std::ios::binary);
                saved << (format ? canonicalText.str() : ordinaryText.str()); saved.close();
                if (!saved) return fail("checkpoint evidence write");
                restored[format] = std::make_unique<RuntimeWindowState>(); auto& q = *restored[format]; seedRuntimeLoadScratch(q, s, false);
                q.impactEffects.clear(); q.localChronosphereMarker.reset();
                { RuntimeSnapshotActiveStateScope restore(q);
                    if (!materializeRuntimeLoadStateFromIni(q, ordinaryIni, error)) return fail(std::string(name) + " materialization: " + error);
                    // No seeded impact is permitted to pass as decoded animation.
                    q.impactEffects.clear();
                    if (format && !materializeRuntimeMultiplayerSnapshotExtension(q, canonicalIni, error)) return fail(std::string(name) + " extension: " + error);
                }
                q.runtimeSoundsEnabled = q.runtimePreferencePersistenceEnabled = false;
                q.runtimeSuppressPlayerAudioFeedback = true; initializeRuntimeOfflineGameplayRouting(q);
                if (q.tick != s.tick || q.superWeapons.stateHash() != s.superWeapons.stateHash() || q.particles.stateHash() != s.particles.stateHash()
                    || worldRecords(q) != worldRecords(s) || !stable(q)
                    || (format ? impactRecords(q) != impactRecords(s) : animCount(q, rule->firstAnim) || animCount(q, rule->secondAnim)))
                    return fail(std::string(name) + " changed real authority/HP/particles or animation scope");
                checkpoints << label << ',' << name << ',' << q.tick << ',' << hz << ',' << q.superWeapons.stateHash() << ',' << q.particles.stateHash()
                    << ',' << q.particles.currentTick() << ',' << animCount(q, rule->firstAnim) << ',' << animCount(q, rule->secondAnim) << ",PASS\n";
                if (!sample(q, name, "restored")) return false;
                if (format) ++caseCanonical; else ++caseOrdinary;
            }
            stage = "natural full simulation continuation";
            const int minimum = hz * 10, maximum = hz * 15;
            for (int i = 0; i < maximum; ++i) {
                step(s); ++naturalTicks;
                for (auto& q : restored) step(*q);
                ++casePaired;
                if (!sample(s, "original", "continuation") || !stable(s)) return false;
                for (int format = 0; format < 2; ++format) {
                    auto& q = *restored[format];
                    if (!sample(q, format ? "canonical-extension" : "ordinary14", "continuation") || !stable(q)
                        || q.tick != s.tick || q.superWeapons.stateHash() != s.superWeapons.stateHash()
                        || q.particles.stateHash() != s.particles.stateHash() || worldRecords(q) != worldRecords(s)
                        || (format && impactRecords(q) != impactRecords(s))) return fail("real restored continuation diverged at tick=" + std::to_string(s.tick));
                }
                if (i + 1 >= minimum && !animCount(s, rule->firstAnim) && !animCount(s, rule->secondAnim)) break;
                if (i + 1 == maximum) return fail("native finite Chrono source/destination did not expire");
            }
            if (applied != 1 || captured != 1 || accepted != 1 || warpIn != int(sourceOccupied && !blocked) || warpAway != int(blocked))
                return fail("continuation repeated cast or transfer");
            stage = "real charging sidebar rejection";
            const auto icon = button(s); if (!icon) return fail("charging sidebar icon disappeared");
            const auto chargeHash = s.superWeapons.stateHash(); mouse(s, *icon);
            if (s.multiplayerIntentRouter.superWeapon() || s.localChronosphereMarker || captured != 1 || accepted != 1
                || s.superWeapons.stateHash() != chargeHash) return fail("charging power accepted another actual sidebar selection");
            if (!sample(s, "original", "charging-click-rejected")) return false;
            // Two small original-system clock contracts cover the whole authored
            // seven minutes. These value copies are not full-world continuation.
            if (scenario == "empty-empty") {
                stage = "original full recharge clock"; auto clock = s.superWeapons;
                auto clockContext = runtimeSuperWeaponContext(s); clockContext.emit = {};
                for (int tick = s.tick + 1; tick <= commandTick + recharge; ++tick) {
                    clockContext.tick = tick; clock.tick(clockContext); const auto* snapshot = clock.findSidebarSnapshot("Commander", powerId);
                    const int remaining = std::max(0, recharge - (tick - commandTick));
                    if (!snapshot || snapshot->remainingTicks != remaining || snapshot->rechargeTicks != recharge
                        || snapshot->state != (remaining ? SuperWeaponRuntimeState::Charging : SuperWeaponRuntimeState::Ready))
                        return fail("original system tick did not become Ready at the authored seven-minute deadline");
                    ++caseClock;
                }
                const auto* snapshot = clock.findSidebarSnapshot("Commander", powerId);
                ticks << label << ",system-clock-only,recharge-complete," << clockContext.tick << ',' << hz << ',' << int(snapshot->state)
                    << ',' << snapshot->remainingTicks << ',' << snapshot->rechargeTicks << ',' << snapshot->progress << ',' << clock.stateHash()
                    << ",0,-1,0,0,0,0,0,0\n";
            }
            return true;
        };
        const bool ok = body();
        cases << label << ',' << hz << ',' << scenario << ',' << source.x << ',' << source.y << ',' << destination.x << ',' << destination.y
            << ',' << sourceId << ',' << destinationId << ',' << sourceOccupied << ',' << destinationOccupied << ',' << cancelled << ',' << commandTick
            << ',' << captured << ',' << accepted << ',' << applied << ',' << warpIn << ',' << warpAway << ',' << static_cast<int>(std::llround(rechargeMinutes * 60.0 * hz))
            << ',' << naturalTicks << ',' << caseOrdinary << ',' << caseCanonical << ',' << casePaired << ',' << caseClock << ',' << (ok ? "PASS" : "FAIL") << '\n';
        cases.flush(); clicks.flush(); ticks.flush(); objects.flush(); events.flush(); checkpoints.flush(); assets.flush();
        if (ok) {
            ++passed; ++casts; ordinaryCount += caseOrdinary; canonicalCount += caseCanonical; paired += casePaired; clockChecks += caseClock;
            std::cout << "Chronosphere empty ground " << label << " PASS\n";
        } else { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
        // The window must never retain a pointer to a destroyed scene.
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    if (passed == 0 && failures.empty()) return fail("unknown requested case: " + filter);
    if (!failures.empty()) {
        std::ostringstream detail; detail << "Chronosphere empty ground: " << failures.size() << " failed cases";
        for (const auto& failure : failures) detail << '\n' << failure; error = detail.str(); return false;
    }
    if (!filter.empty() ? passed != 1 : passed != 16 || casts != 16 || ordinaryCount != 16 || canonicalCount != 16 || cancelCount != 2)
        return fail("actual case/cast/save/cancel coverage differs");
    std::cout << "Chronosphere empty ground passed: cases=" << passed << " realTwoClickCasts=" << casts
        << " cancelledSelections=" << cancelCount << " ordinary14=" << ordinaryCount << " canonicalExtensions=" << canonicalCount
        << " pairedTicks=" << paired << " originalSystemRechargeTicks=" << clockChecks << '\n';
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
