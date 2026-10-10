// Manual X follows the user's short-distance policy: one legal adjacent cell
// per actor. This is not a claim that retail NearbyLocation has a hard radius
// of one, or that its scenario RNG/search order is reproduced here.
bool runRuntimeScatterDistanceSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    IniFile original, originalArt; RulesDatabase rules; ArtDatabase art;
    if (!original.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(original)
        || !originalArt.loadFromFile(live.report.artPath) || !art.loadFromIni(originalArt)) {
        error = "Scatter distance: original rules/art unavailable"; return false;
    }
    const char* output = std::getenv("CONCORD_SCATTER_DISTANCE_OUTPUT");
    const char* requested = std::getenv("CONCORD_SCATTER_DISTANCE_CASE");
    const std::string directory = output ? output : "", filter = requested ? requested : "";
    if (!directory.empty()) std::filesystem::create_directories(directory);
    std::ofstream csv;
    if (!directory.empty()) {
        csv.open(std::filesystem::path(directory) / "scatter-motion.csv");
        csv << "scene,tick,id,type,originX,originY,goalX,goalY,cellX,cellY,renderX,renderY,renderZ,subCell,facing,moving,state,path,remaining\n";
        csv << std::setprecision(17);
    }
    int landTile = -1; std::optional<TerrainCell> water;
    auto tileCache = std::make_unique<RuntimeMapRenderCache>();
    const auto metadata = runtimeTerrainMetadataContext(live);
    for (int tile = 0; tile < static_cast<int>(live.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(live, cell);
        if (landTile < 0 && info.passable && !info.water && !info.shore && !info.cliff && !info.bridge
            && resolveRuntimeTerrainSubTileRampType(cell, metadata, *tileCache) == 0) landTile = tile;
    }
    for (const auto& set : live.theater.getTileSets()) {
        const auto name = toLowerAsciiLocal(set.fileName + " " + set.setName);
        if (name.find("water") == std::string::npos && name.find("shore") == std::string::npos) continue;
        for (int tile = set.startTileIndex; tile < set.startTileIndex + set.tilesInSet && !water; ++tile)
            for (int sub = 0; sub < 16 && !water; ++sub) {
                TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = sub;
                if (resolveRuntimeTerrainSubTileLandType(cell, metadata, *tileCache) == RuntimeTerrainLandType::Water
                    && resolveRuntimeTerrainSubTileRampType(cell, metadata, *tileCache) == 0) water = cell;
            }
        if (water) break;
    }
    if (landTile < 0 || !water) { error = "Scatter distance: native flat land/water TMP unavailable"; return false; }
    constexpr int width = 1280, height = 720;
    const RECT client{0, 0, width, height}, viewport = runtimeMapViewport(client);
    std::string label, stage, fixtureFailure; std::vector<std::string> failures;
    int scenes = 0, keys = 0, routedX = 0, accepted = 0, rejected = 0, boundedGoals = 0;
    int motionChecks = 0, facingChecks = 0, stableChecks = 0, slotChecks = 0, repeatChecks = 0, canonicalChecks = 0;
    int ordinarySaves = 0, exactSaves = 0, pairedTicks = 0, hashChecks = 0;
    const auto fail = [&](const std::string& why) { error = "Scatter " + label + " [" + stage + "]: " + why; return false; };
    const auto scene = [&](const std::string& name, const auto& body) {
        if (!filter.empty() && filter != name) return;
        label = name; stage = "original fixture"; fixtureFailure.clear();
        bool passed = body();
        if (!fixtureFailure.empty()) { fail(fixtureFailure); passed = false; }
        if (passed) { ++scenes; std::cout << "Scatter " << name << " PASS\n"; }
        else { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    };
    struct Spawn { int id; const char* type; RuntimeCellPoint cell; int subCell = -1; };
    const auto make = [&](int hz, const std::vector<Spawn>& spawns, bool wet) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        seedRuntimeLoadScratch(s, live, false); s.config = live.config;
        s.config.fixedTickHz = hz; s.config.gameMode = "Battle"; s.config.playerCountry = "Americans"; s.config.techLevel = 10;
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.noCooldown = s.config.shortGame = false;
        s.config.crates = s.config.superweapons = false; s.config.startingUnits = 0; s.config.playerSlots.clear();
        s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.config.keyboardReferenceLayout = false; s.config.keyBindings.clear();
        s.campaignMode = false; s.skirmishMatchFinished = s.skirmishVictory = false; s.skirmishResult.clear();
        s.rules = rules; s.art = art; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.mapFacts = {}; s.mapFacts.theater = live.mapFacts.theater; s.mapFacts.lighting = live.mapFacts.lighting;
        s.report.map.size.clear(); s.report.map.localSize.clear();
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y = 50; y <= 124; ++y) for (int x = 50; x <= 124; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = landTile; cell.subTileIndex = 0;
            if (wet && x >= 72 && x <= 96 && y >= 95 && y <= 114) {
                cell.tileIndex = water->tileIndex; cell.subTileIndex = water->subTileIndex;
            }
            s.world.accessTerrainCells().push_back(cell);
        }
        const auto add = [&](int id, const std::string& type, WorldObjectKind kind, RuntimeCellPoint cell,
                int slot, const std::string& owner) {
            WorldObjectInstance o; o.instanceId = id; o.mapId = "Scatter" + std::to_string(id);
            o.type = type; o.kind = kind; o.owner = owner; o.x = cell.x; o.y = cell.y;
            o.cell = runtimeCellKey(cell.x, cell.y); o.alive = o.poweredOperational = true;
            // The C fixture's idle SHAD is eligible for ordinary auto-landing.
            // Sleep deliberately suppresses that mission; S would instead
            // create the native stopped-flight latch, so author idle Guard.
            o.mission = type == "SHAD" ? "Guard" : "Sleep";
            o.health = runtimeMaxHealthForObject(s, o); o.subCell = slot; o.facing = runtimeFacingFromDelta(1, 0);
            initializeRuntimeObjectAmmo(o, runtimeAmmoRulesForObject(s.rules, o));
            applyRuntimeInitialObjectFlightState(s.rules, o, true);
            runtimeInitializeModExtensions(o, s.rules, hz); s.world.accessObjects().push_back(std::move(o));
        };
        add(100, "GAPOWR", WorldObjectKind::Structure, {112, 116}, -1, "Commander");
        add(998, "NANRCT", WorldObjectKind::Structure, {52, 52}, -1, "SkirmishPlayer2");
        add(900, "MTNK", WorldObjectKind::Unit, {107, 88}, -1, "Commander");
        for (const auto& spawn : spawns) add(spawn.id, spawn.type,
            runtimeBuildKindForUnitTypeId(s.rules, spawn.type) == RuntimeBuildKind::Infantry
                ? WorldObjectKind::Infantry : WorldObjectKind::Unit, spawn.cell, spawn.subCell, "Commander");
        s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner = "Commander"; s.economy.credits = 20000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        RuntimeConfig::PlayerSlot local; local.enabled = local.human = local.local = true;
        local.name = "Commander"; local.country = "Americans"; local.color = "Gold";
        RuntimeConfig::PlayerSlot hostile; hostile.enabled = true; hostile.country = "Russians"; hostile.color = "Red";
        s.config.playerSlots = {local, hostile}; s.skirmishCountryByOwner.clear(); s.skirmishTeamByOwner.clear();
        s.playerCommands.alliances.clear(); s.skirmishCountryByOwner["commander"] = "Americans";
        s.skirmishCountryByOwner["skirmishplayer2"] = "Russians"; s.humanHouseAuthorities.clear();
        RuntimeHumanHouseAuthority house; house.owner = "Commander"; house.canonicalOwner = "commander";
        house.country = "Americans"; house.localFacade = true; s.humanHouseAuthorities.push_back(house);
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s); ensureRuntimeRepairDepotsInitialized(s);
        ensureRuntimeMenuKeyboardLoaded(s); rebuildRuntimeEconomyPower(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s); }
        return box;
    };
    struct Input {
        RuntimeWindowState& state; HWND window = nullptr; WNDPROC original = nullptr;
        bool routed = false; std::vector<RuntimeMultiplayerGameplayCommand> commands;
        ~Input() { state.multiplayerIntentRouter.setCaptureCallback({});
            if (window) { SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                if (original) SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
                if (GetCapture() == window) ReleaseCapture(); releaseRuntimeBackBuffer(state.backBuffer); DestroyWindow(window); } }
    };
    const auto open = [&](Input& input, bool routed) {
        input.routed = routed; auto& s = input.state;
        s.offlineGameplayIntentRouting = false; s.multiplayerBridge = nullptr;
        if (routed) {
            initializeRuntimeOfflineGameplayRouting(s);
            s.multiplayerIntentRouter.setCaptureCallback([&input](const auto& command, std::string& reason) {
                input.commands.push_back(command); return dispatchRuntimeOfflineGameplayCommand(input.state, command, reason);
            });
        }
        if (runtimeUsesGameplayIntentRouter(s) != routed) return fail("direct/routed input authority mismatch");
        input.window = CreateWindowExW(0, L"STATIC", L"Real scatter distance input", WS_POPUP,
            0, 0, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!input.window) return fail("actual Win32 input window unavailable");
        SetWindowLongPtrW(input.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));
        input.original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(input.window, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(runtimeWindowProc)));
        return input.original ? true : fail("actual input procedure unavailable");
    };
    const auto key = [&](Input& input, int code, bool repeat = false) {
        RuntimeSnapshotActiveStateScope active(input.state);
        SendMessageW(input.window, WM_KEYDOWN, code, LPARAM(1) | (repeat ? (LPARAM(1) << 30) : 0));
        if (!repeat) SendMessageW(input.window, WM_KEYUP, code, LPARAM(1) | (LPARAM(1) << 30) | (LPARAM(1) << 31));
        ++keys;
    };
    const auto move = [&](Input& input, int id, RuntimeCellPoint goal) {
        auto& s = input.state; applySelection(s, {id}, false, false);
        centerRuntimeCameraOnCell(s, client, goal); ensureRuntimeCameraForViewport(s, viewport);
        POINT p = runtimeScreenPointForCell(s, viewport, goal, terrainLevelAtRuntimeCell(s, goal.x, goal.y));
        bool picked = false;
        for (int dy = -8; dy <= 8 && !picked; ++dy) for (int dx = -8; dx <= 8 && !picked; ++dx) {
            const POINT q{p.x + dx, p.y + dy}, actual = approximateCellFromScreenPoint(s, viewport, q);
            if (pointInRectInclusive(viewport, q) && actual.x == goal.x && actual.y == goal.y
                && !findProjectedObjectAtPoint(runtimeProjectedObjectsForViewport(s, viewport), q)) { p = q; picked = true; }
        }
        if (!picked) return fail("actual terrain click cannot pick the requested empty cell");
        SendMessageW(input.window, WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
        SendMessageW(input.window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(p.x, p.y));
        SendMessageW(input.window, WM_LBUTTONUP, 0, MAKELPARAM(p.x, p.y));
        const auto b = s.behaviors.find(id);
        return b != s.behaviors.end() && b->second.kind == RuntimeBehaviorKind::Move && b->second.moveTarget == goal
            ? true : fail("actual map Move rejected: " + s.interactionStatus);
    };
    struct Pose {
        double x = 0, y = 0, z = 0; int cellX = 0, cellY = 0, slot = -1, facing = -1, hp = 0;
        bool moving = false, rendered = false, air = false;
        bool operator==(const Pose&) const = default;
    };
    const auto pose = [](const WorldObjectInstance& o) { return Pose{runtimeObjectRenderX(o), runtimeObjectRenderY(o),
        runtimeObjectRenderLevel(o), o.x, o.y, o.subCell, o.facing, o.health, o.renderMoveActive, o.hasRenderPosition, o.airborne}; };
    const auto record = [](const RuntimeWindowState& s, int id) {
        const auto b = s.behaviors.find(id);
        return b == s.behaviors.end() ? std::string{} : runtimeMultiplayerSnapshotBehaviorRecord(id, b->second);
    };
    const auto describe = [&](const RuntimeWindowState& s, int id) {
        std::ostringstream out; out << std::setprecision(17) << "tick=" << s.tick << " id=" << id;
        if (const auto* o = findRuntimeObjectByIdIncludingInactive(s, id)) { const auto p = pose(*o);
            out << " type=" << o->type << " cell=" << o->x << ',' << o->y << " XYZ=" << p.x << ',' << p.y << ',' << p.z
                << " slot=" << p.slot << " facing=" << p.facing << " moving/air=" << p.moving << '/' << p.air << " HP=" << p.hp
                << " alive/deployed=" << o->alive << '/' << o->deployed << " mission=" << o->mission
                << " dockedTo=" << o->dockedToObjectId;
        } else out << " missing";
        out << " behavior=" << record(s, id); return out.str();
    };
    const auto facingDifference = [](int a, int b) { const int d = std::abs(a-b) % 256; return std::min(d,256-d); };
    const auto displacementFacing = [](double dx, double dy) {
        constexpr double pi = 3.14159265358979323846;
        double angle = std::atan2(dx-dy,-(dx+dy)); if (angle < 0.0) angle += 2*pi;
        return int(std::lround(angle*256/(2*pi))) % 256;
    };
    const auto step = [&](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
        if (s.skirmishMatchFinished && fixtureFailure.empty()) fixtureFailure = "Battle ended: " + s.skirmishResult;
    };
    const auto checkpoint = [&](RuntimeWindowState& s, bool exact) {
        std::ostringstream ordinary, complete, canonical; IniFile base, snapshot;
        if (!writeRuntimeStateToStream(s, ordinary, error) || !base.loadFromString(ordinary.str())) return std::unique_ptr<RuntimeWindowState>{};
        if (exact && (!writeRuntimeStateToStream(s, complete, error) || !writeRuntimeMultiplayerSnapshotExtension(s, complete, error)
            || !snapshot.loadFromString(complete.str()) || !writeRuntimeMultiplayerSnapshotCanonicalIni(snapshot, canonical, error)
            || !snapshot.loadFromString(canonical.str()))) return std::unique_ptr<RuntimeWindowState>{};
        auto other = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*other, s, false);
        // The ordinary v14 base retains AV authority. Its canonical extension
        // restores full paths/group/RNG; no route or X command is replayed.
        if (!materializeRuntimeLoadStateFromIni(*other, base, error)
            || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*other, snapshot, error))) return std::unique_ptr<RuntimeWindowState>{};
        initializeRuntimeOfflineGameplayRouting(*other); return other;
    };
    const auto compare = [&](const RuntimeWindowState& s, const RuntimeWindowState& other,
            const std::vector<int>& ids, int ordinaryId, bool exact) {
        if (s.tick != other.tick || s.playerCommands.stateHash() != other.playerCommands.stateHash()) return fail("saved tick/command queue changed");
        for (int id : ids) {
            if (!exact && id != ordinaryId) continue;
            const auto* a = findObjectById(s, id); const auto* b = findObjectById(other, id);
            if (!a || !b || pose(*a) != pose(*b)) return fail("saved physical pose changed: source=[" + describe(s, id)
                + "] restored=[" + describe(other, id) + ']');
            const auto x = s.behaviors.find(id), y = other.behaviors.find(id);
            if ((x == s.behaviors.end()) != (y == other.behaviors.end())) return fail("saved behavior presence changed");
            if (x != s.behaviors.end()) {
                const auto& m = x->second; const auto& n = y->second;
                if (exact ? record(s, id) != record(other, id)
                    : m.kind != n.kind || m.moveTarget != n.moveTarget || m.moveCooldownTicks != n.moveCooldownTicks
                        || m.moveSegmentFrom != n.moveSegmentFrom || m.renderFrom != n.renderFrom || m.renderTo != n.renderTo
                        || m.renderFromLevel != n.renderFromLevel || m.renderToLevel != n.renderToLevel
                        || m.renderMoveTicksTotal != n.renderMoveTicksTotal || m.renderMoveTicksRemaining != n.renderMoveTicksRemaining
                        || m.renderFromOffsetX != n.renderFromOffsetX || m.renderFromOffsetY != n.renderFromOffsetY
                        || m.renderToOffsetX != n.renderToOffsetX || m.renderToOffsetY != n.renderToOffsetY
                        || m.renderArrivalDistanceAfter != n.renderArrivalDistanceAfter)
                    return fail("saved committed movement restarted/changed: source=[" + describe(s, id)
                        + "] restored=[" + describe(other, id) + ']');
            }
        }
        if (exact) {
            std::string sourceError, restoredError;
            const auto a = runtimeStandaloneSimulationStateHash(s, sourceError), b = runtimeStandaloneSimulationStateHash(other, restoredError);
            if (!sourceError.empty() || !restoredError.empty() || a != b) return fail("canonical authority hash changed: "
                + sourceError + '/' + restoredError + " source=" + std::to_string(a) + " restored=" + std::to_string(b));
            ++hashChecks;
        }
        ++pairedTicks; return true;
    };
    const auto slotOccupancy = [&](const RuntimeWindowState& s) {
        std::map<int, std::vector<const WorldObjectInstance*>> cells;
        for (const auto& o : s.world.getObjects()) {
            if (!o.alive || o.health <= 0 || o.airborne
                || (o.kind != WorldObjectKind::Unit && o.kind != WorldObjectKind::Infantry)) continue;
            // Match the real ground-domain occupancy rule: a docked Air
            // child shares its ship's cell but occupies no ground slot.
            // Landed SHAD (dock=-1) and docked ground vehicles still count.
            if (runtimeObjectMovementMode(s, o) == PathMovementMode::Air && o.dockedToObjectId >= 0) continue;
            cells[o.cell].push_back(&o);
        }
        for (const auto& [cell, objects] : cells) {
            std::set<int> slots; int infantry = 0, vehicles = 0;
            for (const auto* o : objects) {
                if (o->kind == WorldObjectKind::Unit) ++vehicles;
                else { ++infantry; if (!RuntimeSubcellSystemState::isActiveSubCell(o->subCell) || !slots.insert(o->subCell).second)
                    return fail("ground infantry lost a native unique 2/4/3 slot at " + std::to_string(cell)); }
            }
            if (vehicles > 1 || (vehicles && infantry) || infantry > 3) return fail("ground vehicle/infantry occupancy overlap at " + std::to_string(cell));
            ++slotChecks;
        }
        return true;
    };
    const auto goalsForX = [&](Input& input, const std::vector<int>& ids, std::map<int, RuntimeCellPoint>& origins,
            std::map<int, RuntimeCellPoint>& goals) {
        auto& s = input.state; RuntimeSnapshotActiveStateScope active(s); origins.clear(); goals.clear();
        const auto occupancy = buildRuntimeOccupancyLookup(s, -1); std::map<int, WorldObjectInstance> actors;
        for (int id : ids) { const auto* o = findObjectById(s, id); if (!o) return fail("X actor missing");
            origins[id] = runtimeCellPointForObject(*o); actors[id] = *o; }
        applySelection(s, ids, false, false); const auto beforeCapture = input.commands.size(); key(input, 'X');
        if (s.lastCommand.acceptedCount != int(ids.size()) || s.lastCommand.rejectedCount != 0)
            return fail("real X did not accept every legal actor: accepted=" + std::to_string(s.lastCommand.acceptedCount)
                + " rejected=" + std::to_string(s.lastCommand.rejectedCount) + " " + s.interactionStatus);
        if (input.routed) {
            if (input.commands.size() != beforeCapture + 1 || !std::get_if<RuntimeMultiplayerGameplayScatterCommand>(&input.commands.back().body))
                return fail("X did not emit exactly one real routed scatter command");
            ++routedX;
        }
        std::set<int> targetCells; const std::unordered_set<int> group(ids.begin(), ids.end());
        for (int id : ids) {
            const auto b = s.behaviors.find(id);
            if (b == s.behaviors.end() || b->second.kind != RuntimeBehaviorKind::Move) return fail("X did not install Move: " + describe(s, id));
            const auto goal = b->second.moveTarget, origin = origins.at(id);
            const int dx = std::abs(goal.x - origin.x), dy = std::abs(goal.y - origin.y);
            if (std::max(dx, dy) != 1 || !s.terrainByCell.contains(runtimeCellKey(goal.x, goal.y))
                || !targetCells.insert(runtimeCellKey(goal.x, goal.y)).second
                || !runtimeCellTransitionIsPassable(s, s.terrainByCell, occupancy, origin, goal, actors.at(id), -1, nullptr, false)
                || b->second.pendingIgnoredObjectIds != group || b->second.factoryExitClearanceHostId != -1)
                return fail("target is not a unique hard-occupancy legal immediate neighbour: " + describe(s, id));
            goals[id] = goal; ++boundedGoals;
        }
        accepted += int(ids.size());
        std::map<int, std::string> oldRecords; std::map<int, Pose> oldPoses;
        for (int id : ids) { oldRecords[id] = record(s, id); oldPoses[id] = pose(*findObjectById(s, id)); }
        const auto queue = s.playerCommands.stateHash(), capture = input.commands.size(); key(input, 'X', true);
        if (input.commands.size() != capture || s.playerCommands.stateHash() != queue) return fail("autorepeat dispatched another X/cancelled a queue");
        for (int id : ids) if (record(s, id) != oldRecords.at(id) || pose(*findObjectById(s, id)) != oldPoses.at(id))
            return fail("bit30 autorepeat changed an accepted target/pose");
        ++repeatChecks; return true;
    };
    const auto execute = [&](RuntimeWindowState& s, const std::vector<int>& ids,
            const std::map<int, RuntimeCellPoint>& origins, const std::map<int, RuntimeCellPoint>& goals, bool save) {
        const auto control = pose(*findObjectById(s, 900));
        std::unique_ptr<RuntimeWindowState> ordinary, exact; int ordinaryUntil = -1; bool saved = false;
        int budget = s.config.fixedTickHz * 20;
        // A finite allowance includes original terminal braking and a full
        // native body turn; it does not change Speed/ROT to obtain arrival.
        for (int frame = 0; frame < budget; ++frame) {
            std::map<int, Pose> before; std::map<int, RuntimeObjectBehavior> old;
            for (int id : ids) { before[id] = pose(*findObjectById(s, id));
                if (const auto b = s.behaviors.find(id); b != s.behaviors.end()) old[id] = b->second; }
            step(s); if (exact) step(*exact); if (ordinary && s.tick <= ordinaryUntil) step(*ordinary);
            for (int id : ids) {
                const auto* o = findObjectById(s, id); const auto* type = o ? findRuntimeUnitType(s.rules, *o) : nullptr;
                if (!o || !type || o->health != type->strength) return fail("moving stock actor lost health/type: " + describe(s, id));
                const auto p = pose(*o); const double travelled = std::hypot(p.x - before.at(id).x, p.y - before.at(id).y);
                const double leptons = runtimeUnitUsesJumpjetMovement(*type) && (p.air || before.at(id).air)
                    ? type->jumpjetSpeed : std::max(1.0, std::floor(type->speed * 256.0 / 100.0));
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)
                    || travelled > 1.25 * leptons * 15.0 / (256.0 * s.config.fixedTickHz) + 2.0 / 16777216.0)
                    return fail("X teleported or accelerated stock motion: " + describe(s, id));
                if (o->kind == WorldObjectKind::Unit && !runtimeUnitUsesJumpjetMovement(*type)) {
                    const auto previous = old.find(id);
                    // A completed old segment may turn toward a new segment in
                    // the same full tick. Its displacement keeps the old facing.
                    const bool endTick = previous != old.end() && previous->second.moveCooldownTicks == 1
                        && previous->second.renderMoveTicksRemaining == 1;
                    const int visibleFacing = endTick ? before.at(id).facing : p.facing;
                    if (travelled > 1e-8 && facingDifference(visibleFacing,
                            displacementFacing(p.x-before.at(id).x,p.y-before.at(id).y)) > 1)
                        return fail("ordinary vehicle/naval body drifted sideways: " + describe(s,id));
                    if (before.at(id).facing >= 0 && p.facing >= 0 && facingDifference(before.at(id).facing,p.facing) > 2*type->rot+1)
                        return fail("X bypassed original vehicle/naval ROT: " + describe(s,id));
                    ++facingChecks;
                }
                if (std::abs(p.x - origins.at(id).x) > 1.000001 || std::abs(p.y - origins.at(id).y) > 1.000001)
                    return fail("actual scatter left the one-cell neighbourhood: " + describe(s, id));
                if (const auto b = s.behaviors.find(id); b != s.behaviors.end()) {
                    if (b->second.kind != RuntimeBehaviorKind::Move || b->second.moveTarget != goals.at(id)
                        || b->second.factoryExitClearanceHostId != -1) return fail("execution retargeted or accelerated X: " + describe(s, id));
                }
                if (csv.is_open()) { const auto b = s.behaviors.find(id); csv << label << ',' << s.tick << ',' << id << ',' << o->type << ','
                    << origins.at(id).x << ',' << origins.at(id).y << ',' << goals.at(id).x << ',' << goals.at(id).y << ','
                    << o->x << ',' << o->y << ',' << p.x << ',' << p.y << ',' << p.z << ',' << o->subCell << ',' << o->facing << ','
                    << o->renderMoveActive << ',' << (b == s.behaviors.end() ? -1 : int(b->second.state)) << ','
                    << (b == s.behaviors.end() ? 0 : b->second.path.size()) << ','
                    << (b == s.behaviors.end() ? 0 : b->second.renderMoveTicksRemaining) << '\n'; }
                ++motionChecks;
            }
            if (pose(*findObjectById(s, 900)) != control) return fail("unselected control moved");
            if (!slotOccupancy(s)) return false;
            if (save && !saved) {
                const auto b = s.behaviors.find(ids.front());
                if (b != s.behaviors.end() && b->second.renderMoveTicksRemaining >= 8
                    && b->second.renderMoveTicksRemaining < b->second.renderMoveTicksTotal) {
                    ordinary = checkpoint(s, false); exact = checkpoint(s, true);
                    if (!ordinary || !exact) return fail("committed X checkpoint failed: " + error);
                    ordinaryUntil = s.tick + b->second.renderMoveTicksRemaining - 2;
                    if (!compare(s, *ordinary, ids, ids.front(), false) || !compare(s, *exact, ids, ids.front(), true)) return false;
                    saved = true; ++ordinarySaves; ++exactSaves;
                }
            } else {
                if (exact && !compare(s, *exact, ids, ids.front(), true)) return false;
                // Ordinary v14 reconstructs future routes. Compare its exact
                // current committed segment only, stopping before completion.
                if (ordinary && s.tick <= ordinaryUntil && !compare(s, *ordinary, ids, ids.front(), false)) return false;
            }
            bool complete = true;
            for (int id : ids) { const auto* o = findObjectById(s, id);
                complete = complete && runtimeCellPointForObject(*o) == goals.at(id) && !o->renderMoveActive
                    && std::abs(runtimeObjectRenderX(*o) - goals.at(id).x) < 1e-8
                    && std::abs(runtimeObjectRenderY(*o) - goals.at(id).y) < 1e-8; }
            if (complete) {
                if (save && !saved) return fail("no genuinely fractional committed X segment was saved");
                // Keep running after arrival. SHAD may naturally descend and
                // hover units may wobble in Z; neither may drift in XY or start
                // another route without another actual player X command.
                for (int n = 0; n < s.config.fixedTickHz/2; ++n) {
                    step(s); if (exact) step(*exact);
                    for (int id : ids) { const auto* o = findObjectById(s,id);
                        if (!o || o->health != findRuntimeUnitType(s.rules,*o)->strength || o->renderMoveActive
                            || runtimeCellPointForObject(*o) != goals.at(id)
                            || std::abs(runtimeObjectRenderX(*o)-goals.at(id).x) > 1e-8
                            || std::abs(runtimeObjectRenderY(*o)-goals.at(id).y) > 1e-8)
                            return fail("settled X continued moving/drifting: " + describe(s,id));
                        if (const auto b = s.behaviors.find(id); b != s.behaviors.end()
                            && (b->second.kind != RuntimeBehaviorKind::Move || b->second.moveTarget != goals.at(id)))
                            return fail("settled X acquired an unrequested new destination");
                        ++stableChecks;
                    }
                    if (pose(*findObjectById(s,900)) != control) return fail("settled unselected control moved");
                    if (!slotOccupancy(s) || (exact && !compare(s,*exact,ids,ids.front(),true))) return false;
                }
                return fixtureFailure.empty() ? true : fail(fixtureFailure);
            }
        }
        for (int id : ids) std::cout << describe(s, id) << '\n';
        return fail("original-speed one-cell orders did not all arrive within the finite turn/braking budget");
    };
    for (int hz : {30, 60}) {
        const bool routed = hz == 60;
        scene("A-stock-ground-" + std::to_string(hz), [&] {
            const std::vector<Spawn> spawns{{1,"MTNK",{75,75}}, {2,"HTNK",{81,75}}, {3,"FV",{87,75}},
                {4,"E1",{75,81},2}, {5,"ENGINEER",{81,81},4}};
            auto box = make(hz, spawns, false); auto& s = *box; RuntimeSnapshotActiveStateScope active(s); Input input{s};
            if (!open(input, routed)) return false;
            const std::vector<int> ids{1,2,3,4,5}; std::map<int, RuntimeCellPoint> origins, goals;
            for (int round = 0; round < 2; ++round) {
                stage = "actual X round " + std::to_string(round + 1);
                if (!goalsForX(input, ids, origins, goals) || !execute(s, ids, origins, goals, hz == 30 && round == 0)) return false;
            }
            return true;
        });
        scene("B-dense32-" + std::to_string(hz), [&] {
            std::vector<Spawn> spawns; int id = 1;
            for (int row = 0; row < 2; ++row) for (int column = 0; column < 4; ++column)
                for (int slot : {2,4,3}) spawns.push_back({id++, slot == 4 ? "ENGINEER" : "E1", {75 + 4*column,75 + 4*row},slot});
            for (int row = 0; row < 2; ++row) for (int column = 0; column < 4; ++column)
                spawns.push_back({id++, column % 2 ? "FV" : "MTNK", {75 + 4*column,88 + 4*row}});
            auto box = make(hz, spawns, false); auto& s = *box; RuntimeSnapshotActiveStateScope active(s); Input input{s};
            if (!open(input, routed) || !slotOccupancy(s)) return false;
            std::vector<int> ids; for (const auto& spawn : spawns) ids.push_back(spawn.id);
            std::unique_ptr<RuntimeWindowState> reverse;
            if (routed) { reverse = checkpoint(s, true); if (!reverse) return fail("canonical pre-X checkpoint failed: " + error); }
            std::map<int, RuntimeCellPoint> origins, goals; stage = "actual 32-actor X";
            if (!goalsForX(input, ids, origins, goals)) return false;
            if (reverse) {
                Input other{*reverse}; if (!open(other, true)) return false;
                auto reversedIds = ids; std::reverse(reversedIds.begin(), reversedIds.end());
                std::map<int, RuntimeCellPoint> reverseOrigins, reverseGoals;
                if (!goalsForX(other, reversedIds, reverseOrigins, reverseGoals)) return false;
                const auto* a = std::get_if<RuntimeMultiplayerGameplayScatterCommand>(&input.commands.back().body);
                const auto* b = std::get_if<RuntimeMultiplayerGameplayScatterCommand>(&other.commands.back().body);
                if (!a || !b || a->objectIds != b->objectIds || !std::is_sorted(a->objectIds.begin(), a->objectIds.end())
                    || origins != reverseOrigins || goals != reverseGoals) return fail("canonical router changed X with selection order");
                for (int actor : ids) if (record(s, actor) != record(*reverse, actor)) return fail("canonical sorted X behavior/group differs");
                ++canonicalChecks;
            }
            if (!execute(s, ids, origins, goals, false)) return false;
            stage = "second genuine X after arrival";
            return goalsForX(input, ids, origins, goals) && execute(s, ids, origins, goals, false);
        });
        scene("C-water-jumpjet-edge-" + std::to_string(hz), [&] {
            // ID1 is first in both raw and canonical selections. The first
            // candidate is outside x124: the original map-domain gate must
            // reject it before considering another local airborne neighbour.
            const std::vector<Spawn> spawns{{1,"JUMPJET",{124,83}}, {2,"DEST",{80,102}},
                {3,"DEST",{88,102}}, {4,"SHAD",{95,83}}};
            auto box = make(hz, spawns, true); auto& s = *box; RuntimeSnapshotActiveStateScope active(s); Input input{s};
            if (!open(input, routed)) return false;
            for (int n = 0; n < hz * 5; ++n) step(s);
            const auto* rock = findObjectById(s, 1); const auto* shad = findObjectById(s, 4);
            if (!rock || !shad || !rock->airborne || shad->airborne) return fail("original BalloonHover/SHAD idle flight setup did not settle naturally");
            for (int actor : {2,3}) { const auto* o = findObjectById(s, actor); const auto* type = o ? findRuntimeUnitType(s.rules,*o) : nullptr;
                const auto info = o ? s.terrainInfoByCell.find(o->cell) : s.terrainInfoByCell.end();
                if (!type || !type->naval || info == s.terrainInfoByCell.end() || info->second.movementLandType != RuntimeTerrainLandType::Water)
                    return fail("DEST lacks actual original water terrain/naval profile"); }
            std::map<int, RuntimeCellPoint> origins, goals; const std::vector<int> ids{1,2,3,4}; stage = "real naval/jumpjet/edge X";
            return goalsForX(input, ids, origins, goals) && execute(s, ids, origins, goals, false);
        });
        scene("D-blocked-deployed-" + std::to_string(hz), [&] {
            std::vector<Spawn> spawns{{1,"MTNK",{80,75}}, {20,"E1",{95,95},2}, {21,"DESO",{110,70},2}, {22,"GGI",{95,101},2}};
            int id = 2; for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                if (dx || dy) spawns.push_back({id++,"MTNK",{80+dx,75+dy}});
            auto box = make(hz, spawns, false); auto& s = *box; RuntimeSnapshotActiveStateScope active(s); Input input{s};
            if (!open(input, routed)) return false;
            std::map<int, Pose> deployedPoses; for (int actor : {20,21,22}) deployedPoses[actor] = pose(*findObjectById(s, actor));
            applySelection(s, {20,21,22}, false, false); key(input, 'D');
            for (int n = 0; n < hz*5; ++n) {
                step(s); bool complete = true;
                for (int actor : {20,21,22}) { const auto* o = findObjectById(s,actor);
                    if (!o || runtimeObjectRenderX(*o) != deployedPoses.at(actor).x || runtimeObjectRenderY(*o) != deployedPoses.at(actor).y)
                        return fail("true D deployment displaced its original body");
                    complete = complete && o->deployed && !runtimeMissionIsDeploying(*o); }
                if (complete) break;
            }
            for (int actor : {20,21,22}) { const auto* o = findObjectById(s,actor);
                if (!o || !o->deployed || runtimeMissionIsDeploying(*o) || runtimeObjectCanMove(s,*o)) return fail("true D did not finish immobile deployment"); }
            stage = "full blocked ring preserves an existing real Move";
            if (!move(input, 1, {90,75})) return false;
            const auto old = record(s,1); const auto anchor = pose(*findObjectById(s,1));
            std::map<int, Pose> blockers; for (int actor = 2; actor <= 9; ++actor) blockers[actor] = pose(*findObjectById(s,actor));
            applySelection(s,{1,20,21,22},false,false); key(input,'X');
            if (s.lastCommand.acceptedCount != 0 || s.lastCommand.rejectedCount != 4 || record(s,1) != old
                || pose(*findObjectById(s,1)) != anchor) return fail("no-candidate X erased/departed/replaced a real existing Move");
            rejected += 4;
            for (int actor : {20,21,22}) { const auto* o = findObjectById(s,actor);
                if (!o || !o->deployed || runtimeMissionIsUndeploying(*o) || o->renderMoveActive)
                    return fail("X moved or automatically folded deployed stock infantry"); }
            // Real S cancels the unreachable prior order before opening one
            // neighbour; no internal behavior/path/state is injected.
            applySelection(s,{1},false,false); key(input,'S');
            for (int n = 0; n < hz/2; ++n) step(s);
            for (const auto& [actor,p] : blockers) if (pose(*findObjectById(s,actor)) != p) return fail("rejected X moved/crushed an unselected blocker");
            int eastId = -1; for (const auto& spawn : spawns) if (spawn.cell == RuntimeCellPoint{81,75}) eastId = spawn.id;
            if (eastId < 0 || !move(input,eastId,{83,75})) return false;
            bool moved = false;
            for (int n = 0; n < hz*20; ++n) { step(s); const auto* o = findObjectById(s,eastId);
                if (o && runtimeCellPointForObject(*o) == RuntimeCellPoint{83,75} && !o->renderMoveActive) { moved = true; break; } }
            if (!moved) return fail("real blocker Move did not open exactly one adjacent cell");
            stage = "only genuinely vacated neighbour is selected";
            std::map<int,RuntimeCellPoint> origins,goals;
            if (!goalsForX(input,{1},origins,goals) || goals.at(1) != RuntimeCellPoint{81,75}) return fail("X expanded beyond the sole legal neighbour");
            if (!execute(s,{1},origins,goals,false)) return false;
            for (const auto& [actor,p] : blockers) if (actor != eastId && pose(*findObjectById(s,actor)) != p)
                return fail("accepted local X displaced/crushed a parked unselected vehicle");
            for (int actor : {20,21,22}) { const auto* o = findObjectById(s,actor);
                if (!o || !o->deployed || runtimeMissionIsUndeploying(*o) || runtimeObjectRenderX(*o) != deployedPoses.at(actor).x
                    || runtimeObjectRenderY(*o) != deployedPoses.at(actor).y || o->health != deployedPoses.at(actor).hp) {
                    const auto& expected = deployedPoses.at(actor); std::ostringstream detail;
                    detail << std::setprecision(17) << "unselected deployed control changed after X: id=" << actor
                        << " expectedXY=" << expected.x << ',' << expected.y << " expectedHP=" << expected.hp
                        << " source=[" << describe(s,actor) << ']';
                    return fail(detail.str());
                } }
            return true;
        });
    }
    if (!failures.empty()) { std::ostringstream out; out << failures.size() << " scatter scene(s) failed:";
        for (const auto& item : failures) out << '\n' << item; error = out.str(); return false; }
    if (scenes == 0) { error = "Scatter distance: requested case was not found"; return false; }
    std::cout << "Scatter distance PASS scenes=" << scenes << " keys=" << keys << " routedX=" << routedX
        << " accepted=" << accepted << " rejected=" << rejected << " oneCellGoals=" << boundedGoals << " motion=" << motionChecks
        << " facing=" << facingChecks << " stable=" << stableChecks << " slotChecks=" << slotChecks << " autorepeat=" << repeatChecks << " canonicalOrder=" << canonicalChecks
        << " ordinary/exact=" << ordinarySaves << '/' << exactSaves << " paired=" << pairedTicks << " authorityHashes=" << hashChecks << '\n';
    return true;
}
