// Original jumpjet rules, real Win32 orders, and complete simulation ticks.
// SHAD's VehicleTypes Landable key is ignored by retail: BalloonHover and
// the explicit deployment flags determine ordinary jumpjet landing instead.
bool runRuntimeNighthawkLandingSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    IniFile authored, authoredArt; RulesDatabase rules; ArtDatabase art;
    if (!authored.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(authored)
        || !authoredArt.loadFromFile(live.report.artPath) || !art.loadFromIni(authoredArt)) {
        error = "Nighthawk landing: original rules/art unavailable"; return false;
    }
    const auto* shad = findRuntimeUnitTypeById(rules, "SHAD");
    if (!shad || !runtimeUnitUsesJumpjetMovement(*shad) || shad->balloonHover
        || shad->isSimpleDeployer || shad->deployToLand || shad->passengers != 5 || shad->sizeLimit != 2
        || shad->jumpjetSpeed != 30 || shad->jumpjetClimb != 10 || shad->jumpjetCrash != 40
        || shad->jumpjetAccel != 12 || shad->jumpjetTurnRate != 6 || shad->jumpJetHeight != 500)
        { error = "Nighthawk landing: original SHAD locomotor/cargo parameters changed"; return false; }
    const char* output = std::getenv("CONCORD_NIGHTHAWK_LANDING_OUTPUT");
    const char* requested = std::getenv("CONCORD_NIGHTHAWK_LANDING_CASE");
    const std::string outputRoot = output ? output : "", filter = requested ? requested : "";
    if (!outputRoot.empty()) std::filesystem::create_directories(outputRoot);
    int landTile = -1; auto tileCache = std::make_unique<RuntimeMapRenderCache>();
    const auto metadata = runtimeTerrainMetadataContext(live);
    for (int tile = 0; tile < static_cast<int>(live.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(live, cell);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge
            && resolveRuntimeTerrainSubTileRampType(cell, metadata, *tileCache) == 0) { landTile = tile; break; }
    }
    if (landTile < 0) { error = "Nighthawk landing: original flat land TMP unavailable"; return false; }
    constexpr int width = 1280, screenHeight = 720;
    const RECT client{0, 0, width, screenHeight}, viewport = runtimeMapViewport(client), sidebar = runtimeSidebarRect(client);
    std::string label, stage, fixtureFailure; std::vector<std::string> failures;
    int scenes = 0, clicks = 0, horizontalChecks = 0, verticalChecks = 0, landings = 0, boarded = 0, unloaded = 0;
    int deniedEntries = 0, ordinarySaves = 0, exactSaves = 0, pairedTicks = 0, captures = 0, hoverChecks = 0;
    int unsafeGroundHoverChecks = 0;
    const auto fail = [&](const std::string& why) {
        error = "Nighthawk " + label + " [" + stage + "]: " + why; return false;
    };
    const auto scene = [&](const std::string& name, const auto& body) {
        if (!filter.empty() && filter != name) return;
        label = name; stage = "original fixture"; fixtureFailure.clear();
        if (body() && fixtureFailure.empty()) { ++scenes; std::cout << "Nighthawk " << name << " PASS\n"; }
        else { if (!fixtureFailure.empty()) fail(fixtureFailure); failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    };
    const auto structure = [&](RuntimeWindowState& s, int id, const std::string& type, RuntimeCellPoint cell,
            const std::string& owner = "Commander") {
        WorldObjectInstance object; object.instanceId = id; object.mapId = "Nighthawk" + std::to_string(id);
        object.kind = WorldObjectKind::Structure; object.type = type; object.owner = owner;
        object.x = cell.x; object.y = cell.y; object.cell = runtimeCellKey(cell.x, cell.y);
        object.alive = object.poweredOperational = true; object.mission = "Sleep";
        object.health = runtimeMaxHealthForObject(s, object); object.currentAmmo = -1;
        s.world.accessObjects().push_back(object);
    };
    const auto make = [&](int hz, const TerrainCell* water = nullptr, const TerrainCell* beach = nullptr) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        seedRuntimeLoadScratch(s, live, false); s.config = live.config; s.config.fixedTickHz = hz;
        s.config.gameMode = "Battle"; s.config.playerCountry = "Americans"; s.config.techLevel = 10;
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.noCooldown = s.config.shortGame = false;
        s.config.crates = s.config.superweapons = false; s.config.playerSlots.clear(); s.config.startingUnits = 0;
        s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.config.keyboardReferenceLayout = false; s.config.keyBindings.clear();
        s.rules = rules; s.art = art; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.mapFacts = {}; s.mapFacts.theater = live.mapFacts.theater; s.mapFacts.lighting = live.mapFacts.lighting;
        s.report.map.size.clear(); s.report.map.localSize.clear();
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y = 50; y <= 105; ++y) for (int x = 50; x <= 110; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = landTile; cell.subTileIndex = 0;
            // Only the new safety scenes author native TMP patches before any
            // navigation initialization. The eight original fixtures stay dry.
            const TerrainCell* patch = water && x >= 79 && x <= 81 && y >= 63 && y <= 65 ? water
                : beach && x >= 79 && x <= 81 && y >= 79 && y <= 81 ? beach : nullptr;
            if (patch) { cell.tileIndex = patch->tileIndex; cell.subTileIndex = patch->subTileIndex; }
            s.world.accessTerrainCells().push_back(cell);
        }
        structure(s, 100, "GAPOWR", {102, 96}); structure(s, 998, "NANRCT", {52, 52}, "SkirmishPlayer2");
        s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner = "Commander"; s.economy.credits = 20000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        RuntimeConfig::PlayerSlot local; local.enabled = local.human = local.local = true;
        local.name = "Commander"; local.country = "Americans"; local.color = "Gold";
        RuntimeConfig::PlayerSlot hostile; hostile.enabled = true; hostile.country = "Russians"; hostile.color = "Red";
        s.config.playerSlots = {local, hostile};
        s.skirmishCountryByOwner.clear(); s.skirmishTeamByOwner.clear(); s.playerCommands.alliances.clear();
        s.skirmishCountryByOwner["commander"] = "Americans"; s.skirmishCountryByOwner["skirmishplayer2"] = "Russians";
        s.humanHouseAuthorities.clear(); RuntimeHumanHouseAuthority house;
        house.owner = "Commander"; house.canonicalOwner = "commander"; house.country = "Americans"; house.localFacade = true;
        s.humanHouseAuthorities.push_back(house);
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s); ensureRuntimeRepairDepotsInitialized(s);
        initializeRuntimeOfflineGameplayRouting(s); ensureRuntimeMenuKeyboardLoaded(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s); }
        rebuildRuntimeEconomyPower(s); return box;
    };
    const auto add = [&](RuntimeWindowState& s, const std::string& type, RuntimeCellPoint cell) {
        const auto kind = findRuntimeBuildingTypeById(s.rules, type) ? RuntimeBuildKind::Building
            : runtimeBuildKindForUnitTypeId(s.rules, type);
        auto* object = addRuntimeObjectInstance(s, kind, type, cell);
        return object ? object->instanceId : -1;
    };
    struct Input {
        RuntimeWindowState& state; HWND window = nullptr; WNDPROC original = nullptr;
        ~Input() { if (window) { SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            if (original) SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
            if (GetCapture() == window) ReleaseCapture();
            releaseRuntimeBackBuffer(state.backBuffer); DestroyWindow(window); } }
    };
    const auto openInput = [&](Input& input) {
        input.window = CreateWindowExW(0, L"STATIC", L"Nighthawk real orders", WS_POPUP,
            0, 0, width, screenHeight, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!input.window) return fail("actual Win32 input window unavailable");
        SetWindowLongPtrW(input.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&input.state));
        input.original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(input.window, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(runtimeWindowProc)));
        return input.original ? true : fail("actual input procedure unavailable");
    };
    const auto click = [&](Input& input, POINT point, bool right = false) {
        SendMessageW(input.window, WM_MOUSEMOVE, 0, MAKELPARAM(point.x, point.y));
        SendMessageW(input.window, right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN,
            right ? MK_RBUTTON : MK_LBUTTON, MAKELPARAM(point.x, point.y));
        SendMessageW(input.window, right ? WM_RBUTTONUP : WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y)); ++clicks;
    };
    const auto objectPoint = [&](RuntimeWindowState& s, int id) -> std::optional<POINT> {
        const auto* object = findObjectById(s, id); if (!object) return std::nullopt;
        centerRuntimeCameraOnCell(s, client, runtimeCellPointForObject(*object)); ensureRuntimeCameraForViewport(s, viewport);
        invalidateRuntimeObjectProjectionCache(s); const auto& projected = runtimeProjectedObjectsForViewport(s, viewport);
        const auto hit = [&](POINT p) { const auto* found = findProjectedObjectAtPoint(projected, p);
            return pointInRectInclusive(viewport, p) && found && found->object && found->object->instanceId == id; };
        for (const auto& candidate : projected) if (candidate.object && candidate.object->instanceId == id) {
            for (const auto p : candidate.footprintHitCenters) if (hit(p)) return p;
            const POINT p{(candidate.bounds.left + candidate.bounds.right) / 2, (candidate.bounds.top + candidate.bounds.bottom) / 2};
            if (hit(p)) return p;
            for (int y = candidate.bounds.top; y < candidate.bounds.bottom; y += 2)
                for (int x = candidate.bounds.left; x < candidate.bounds.right; x += 2) if (hit({x, y})) return POINT{x, y};
        }
        return std::nullopt;
    };
    const auto move = [&](Input& input, int id, RuntimeCellPoint target) {
        auto& s = input.state; applySelection(s, {id}, false, false);
        centerRuntimeCameraOnCell(s, client, target); ensureRuntimeCameraForViewport(s, viewport);
        POINT p = runtimeScreenPointForCell(s, viewport, target, terrainLevelAtRuntimeCell(s, target.x, target.y));
        bool valid = false;
        for (int dy = -8; dy <= 8 && !valid; ++dy) for (int dx = -8; dx <= 8 && !valid; ++dx) {
            const POINT q{p.x + dx, p.y + dy}; const POINT picked = approximateCellFromScreenPoint(s, viewport, q);
            if (pointInRectInclusive(viewport, q) && picked.x == target.x && picked.y == target.y
                && !findProjectedObjectAtPoint(runtimeProjectedObjectsForViewport(s, viewport), q)) { p = q; valid = true; }
        }
        if (!valid) return fail("actual terrain input cannot pick empty destination");
        click(input, p);
        const auto behavior = s.behaviors.find(id);
        return behavior != s.behaviors.end() && behavior->second.kind == RuntimeBehaviorKind::Move
            && behavior->second.moveTarget == target ? true : fail("real Move rejected: " + s.interactionStatus);
    };
    struct Pose {
        double x = 0, y = 0, z = 0, presentation = -1;
        int facing = -1, hp = 0, cellX = 0, cellY = 0;
        bool airborne = false, moving = false, rendered = false;
        bool operator==(const Pose&) const = default;
    };
    const auto pose = [&](const RuntimeWindowState& s, int id) {
        Pose p; if (const auto* o = findObjectById(s, id)) {
            p.x = o->hasRenderPosition ? o->renderX : o->x; p.y = o->hasRenderPosition ? o->renderY : o->y;
            p.z = o->hasRenderPosition ? o->renderLevel : o->level; p.presentation = o->aircraftDockFlightProgress;
            p.facing = o->facing; p.hp = o->health; p.cellX = o->x; p.cellY = o->y;
            p.airborne = o->airborne; p.moving = o->renderMoveActive; p.rendered = o->hasRenderPosition;
        } return p;
    };
    const auto height = [&](const RuntimeWindowState& s, int id) {
        const auto f = s.jumpjetFlights.find(id);
        return f != s.jumpjetFlights.end() ? f->second.height
            : std::llround(pose(s, id).z * runtimeJumpjetCellQ16);
    };
    const auto diagnostic = [&](const RuntimeWindowState& s, int id) {
        const auto p = pose(s, id); std::ostringstream out; out << std::setprecision(17) << "tick=" << s.tick
            << " id=" << id << " XYZ=" << p.x << ',' << p.y << ',' << p.z << " cell=" << p.cellX << ',' << p.cellY
            << " air/move/render=" << p.airborne << '/' << p.moving << '/' << p.rendered << " height=" << height(s, id)
            << " presentation=" << p.presentation;
        if (const auto b = s.behaviors.find(id); b != s.behaviors.end()) out << " behavior=" << int(b->second.state)
            << " goal=" << b->second.moveTarget.x << ',' << b->second.moveTarget.y << " path=" << b->second.path.size();
        out << " flights=" << serializeRuntimeActiveJumpjetFlights(s); return out.str();
    };
    const auto step = [&](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
        if (s.skirmishMatchFinished && fixtureFailure.empty()) fixtureFailure = "Battle ended: " + s.skirmishResult;
    };
    const auto inspect = [&](const RuntimeWindowState& s, int id, const Pose& before, std::int64_t previousHeight) {
        const auto* o = findObjectById(s, id); const auto* type = o ? findRuntimeUnitType(s.rules, *o) : nullptr;
        const auto after = pose(s, id);
        if (!o || !type || o->health != type->strength || !std::isfinite(after.x) || !std::isfinite(after.y) || !std::isfinite(after.z))
            return fail("original live flight changed HP/pose: " + diagnostic(s, id));
        const double delta = std::hypot(after.x - before.x, after.y - before.y);
        if (delta > type->jumpjetSpeed * 15.0 / (256.0 * s.config.fixedTickHz) + 2.0 / runtimeJumpjetCellQ16)
            return fail("horizontal motion exceeded original JumpjetSpeed or snapped: " + diagnostic(s, id));
        const auto currentHeight = height(s, id);
        const auto quantum = std::llround(type->jumpjetClimb * runtimeJumpjetQ16 * 15.0 / s.config.fixedTickHz);
        const double wobbleEnvelope = type->jumpjetNoWobbles || type->jumpjetWobbles <= 0.0
            ? 0.0 : 2.0 * std::max(0.0, static_cast<double>(type->jumpjetDeviation)) / 256.0;
        if (std::abs(after.z - before.z) > double(quantum) / runtimeJumpjetCellQ16 + wobbleEnvelope
            + 1.0 / runtimeJumpjetCellQ16)
            return fail("display height snapped beyond original climb/wobble envelope: " + diagnostic(s, id));
        const auto groundHeight = std::int64_t(terrainLevelAtRuntimeCell(s, o->x, o->y)) * runtimeJumpjetCellQ16;
        const double fraction = after.airborne ? std::clamp(double(currentHeight - groundHeight)
            / std::max<std::int64_t>(1, type->jumpJetHeight * runtimeJumpjetQ16), 0.0, 1.0) : 0.0;
        if (after.presentation != fraction)
            return fail("temporary body lift fraction did not follow original physical height: " + diagnostic(s, id));
        const auto difference = std::abs(currentHeight - previousHeight);
        if (difference > quantum) return fail("normal vertical motion used crash speed or snapped: " + diagnostic(s, id));
        if (currentHeight < previousHeight) {
            const auto ground = std::int64_t(terrainLevelAtRuntimeCell(s, o->x, o->y)) * runtimeJumpjetCellQ16;
            if (currentHeight != previousHeight - std::min(quantum, previousHeight - ground)
                || delta != 0 || after.moving || after.z != double(currentHeight) / runtimeJumpjetCellQ16)
                return fail("normal descent differs from original Climb10 or moved horizontally: " + diagnostic(s, id));
            ++verticalChecks;
        }
        const auto occupancy = buildRuntimeOccupancyLookup(s);
        const auto occupied = occupancy.find(o->cell);
        if (after.airborne && occupied != occupancy.end() && occupied->second == id)
            return fail("airborne body occupied a ground slot");
        if (!after.airborne && (after.z != terrainLevelAtRuntimeCell(s, o->x, o->y)
            || after.moving || occupied == occupancy.end() || occupied->second != id))
            return fail("touchdown did not publish exact ground Z/occupancy: " + diagnostic(s, id));
        ++horizontalChecks; return fixtureFailure.empty() ? true : fail(fixtureFailure);
    };
    const auto tickFlight = [&](RuntimeWindowState& s, int id) {
        const auto before = pose(s, id); const auto h = height(s, id); step(s); return inspect(s, id, before, h);
    };
    const auto checkpoint = [&](RuntimeWindowState& s, bool exact) {
        std::ostringstream base, complete, canonical; IniFile baseIni, exactIni;
        if (!writeRuntimeStateToStream(s, base, error) || !baseIni.loadFromString(base.str())) return std::unique_ptr<RuntimeWindowState>{};
        if (exact && (!writeRuntimeStateToStream(s, complete, error) || !writeRuntimeMultiplayerSnapshotExtension(s, complete, error)
            || !exactIni.loadFromString(complete.str()) || !writeRuntimeMultiplayerSnapshotCanonicalIni(exactIni, canonical, error)
            || !exactIni.loadFromString(canonical.str()))) return std::unique_ptr<RuntimeWindowState>{};
        auto other = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*other, s, false);
        // Canonical MP extension follows the complete ordinary v14 base.
        if (!materializeRuntimeLoadStateFromIni(*other, baseIni, error)
            || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*other, exactIni, error))) return std::unique_ptr<RuntimeWindowState>{};
        initializeRuntimeOfflineGameplayRouting(*other); return other;
    };
    const auto compare = [&](const RuntimeWindowState& s, const RuntimeWindowState& other, int id) {
        if (s.tick != other.tick || pose(s, id) != pose(other, id)
            || serializeRuntimeActiveJumpjetFlights(s) != serializeRuntimeActiveJumpjetFlights(other)
            || s.economy.credits != other.economy.credits)
            return fail("descending checkpoint changed continuous pose/clock: source " + diagnostic(s, id)
                + " restored " + diagnostic(other, id));
        const auto* a = findObjectById(s, id); const auto* b = findObjectById(other, id);
        if (!a || !b || a->transportPassengerIds != b->transportPassengerIds) return fail("checkpoint changed real cargo manifest");
        for (int passenger : a->transportPassengerIds) {
            const auto* x = findRuntimeObjectByIdIncludingInactive(s, passenger);
            const auto* y = findRuntimeObjectByIdIncludingInactive(other, passenger);
            if (!x || !y || x->alive != y->alive || x->health != y->health || x->cell != y->cell || x->level != y->level)
                return fail("checkpoint changed hidden live cargo identity/HP/position");
        }
        ++pairedTicks; return true;
    };
    const auto cargoValid = [&](const RuntimeWindowState& s, int id, const std::vector<int>& passengers) {
        const auto* host = findObjectById(s, id); if (!host) return false;
        auto actual = host->transportPassengerIds, expected = passengers;
        std::sort(actual.begin(), actual.end()); std::sort(expected.begin(), expected.end());
        if (actual != expected || std::adjacent_find(actual.begin(), actual.end()) != actual.end()) return false;
        for (int passenger : passengers) {
            const auto* body = findRuntimeObjectByIdIncludingInactive(s, passenger);
            const auto* rule = body ? findRuntimeUnitType(s.rules, *body) : nullptr;
            if (!body || !rule || body->alive || body->health != rule->strength || body->mission != "InsideTransport"
                || body->cell != host->cell || body->level != host->level || body->hasRenderPosition || body->subCell != -1) return false;
        }
        return true;
    };
    const auto enter = [&](Input& input, int hostId, const std::vector<int>& passengers, bool allowed) {
        auto& s = input.state; applySelection(s, passengers, false, false); const auto p = objectPoint(s, hostId);
        const auto* host = findObjectById(s, hostId);
        if (!p || !host || runtimeTargetCursorIntent(s, *host) != (allowed ? RuntimeTargetCursorIntent::Enter : RuntimeTargetCursorIntent::NoEnter))
            return fail("real transport cursor disagrees with airborne/ground entry");
        const auto before = host->transportPassengerIds; click(input, *p);
        if (!allowed) {
            if (!s.pendingTransportTargets.empty() || findObjectById(s, hostId)->transportPassengerIds != before)
                return fail("airborne/descending/full transport admitted a passenger");
            for (int id : passengers) if (!findObjectById(s, id)) return fail("rejected passenger disappeared");
            ++deniedEntries; return true;
        }
        if (s.pendingTransportTargets.size() != passengers.size()) return fail("real ground entry did not reserve original passengers");
        for (int n = 0; n < s.config.fixedTickHz * 25 && !s.pendingTransportTargets.empty(); ++n)
            if (!tickFlight(s, hostId)) return false;
        if (!cargoValid(s, hostId, passengers)) {
            std::ostringstream detail;
            detail << "actual infantry entry did not finish with original hidden cargo: " << diagnostic(s, hostId) << " cargo=";
            if (const auto* h = findObjectById(s, hostId)) for (int id : h->transportPassengerIds) detail << id << ',';
            for (int id : passengers) {
                detail << " [passenger=" << id;
                if (const auto* actor = findRuntimeObjectByIdIncludingInactive(s, id)) detail << " live=" << actor->alive
                    << " hp=" << actor->health << " slot=" << actor->subCell << " mission=" << actor->mission
                    << " cell=" << actor->x << ',' << actor->y << " body=" << runtimeObjectRenderX(*actor) << ',' << runtimeObjectRenderY(*actor)
                    << " active=" << actor->renderMoveActive;
                if (const auto pending = s.pendingTransportTargets.find(id); pending != s.pendingTransportTargets.end()) detail << " pendingHost=" << pending->second;
                if (const auto b = s.behaviors.find(id); b != s.behaviors.end()) detail << " kind=" << int(b->second.kind)
                    << " state=" << int(b->second.state) << " goal=" << b->second.moveTarget.x << ',' << b->second.moveTarget.y
                    << " path=" << b->second.path.size() << " allowed=" << b->second.pendingAllowedOccupiedObjectId
                    << " shared=" << b->second.pendingHasSharedGoal << " transfer=" << int(b->second.transportTransferKind);
                detail << ']';
            }
            return fail(detail.str());
        }
        boarded += int(passengers.size()); return true;
    };
    const auto settle = [&](RuntimeWindowState& s, int id) {
        for (int n = 0; n < s.config.fixedTickHz * 5 && pose(s, id).airborne; ++n) if (!tickFlight(s, id)) return false;
        if (pose(s, id).airborne) return fail("idle original non-balloon jumpjet did not settle on the ground");
        for (int n = 0; n < s.config.fixedTickHz / 2; ++n) if (!tickFlight(s, id)) return false;
        return true;
    };
    const auto fly = [&](Input& input, int id, RuntimeCellPoint goal, const std::vector<int>& passengers,
            int deniedPassenger, bool save, bool pictures, bool obstruct) {
        auto& s = input.state; stage = "real Move / takeoff / cruise / destination descent";
        if (!move(input, id, goal)) return false;
        int obstacle = -1, obstructedEntry = -1;
        if (obstruct) {
            obstacle = add(s, "GAPOWR", goal); if (obstacle < 0) return fail("real original goal obstruction unavailable");
            obstructedEntry = add(s, "E1", {goal.x - 3, goal.y + 3});
            if (obstructedEntry < 0) return fail("real obstructed-ground entry passenger unavailable");
            initializeRuntimeNavigation(s); completeRuntimeWorldRefresh(s); rebuildRuntimeEconomyPower(s);
        }
        bool flew = false, descended = false, deniedCruise = deniedPassenger < 0, deniedDescent = deniedPassenger < 0;
        bool saved = false, showed = false, obstructionWitness = !obstruct, obstacleSold = !obstruct;
        int waiting = 0; std::unique_ptr<RuntimeWindowState> ordinary, exact;
        for (int n = 0; n < s.config.fixedTickHz * 45; ++n) {
            const auto h = height(s, id); if (!tickFlight(s, id)) return false;
            const auto current = pose(s, id); flew = flew || (current.airborne && current.moving && height(s, id) > 0);
            if (!passengers.empty() && (!cargoValid(s, id, passengers)
                || findObjectById(s, id)->transportPassengerIds != passengers)) return fail("flight detached/changed real passenger order");
            if (ordinary) { step(*ordinary); step(*exact);
                if (!compare(s, *ordinary, id) || !compare(s, *exact, id)) return false; }
            if (deniedPassenger >= 0 && !deniedCruise && current.airborne && current.moving && current.z > 0.5) {
                if (!enter(input, id, {deniedPassenger}, false)) return false; deniedCruise = true;
            }
            if (pictures && !showed && current.airborne && current.z > 0.75 && current.moving) {
                if (!outputRoot.empty()) {
                    const auto stats = drawRuntimeStateOffscreenAtCell(s, {current.cellX, current.cellY}, 960, 640, error,
                        outputRoot + "/nighthawk-takeoff.bmp", false, true);
                    if (!stats || !stats->rendered || stats->visibleVoxelBodies == 0) return fail("real takeoff VXL raster: " + error);
                    ++captures;
                } showed = true;
            }
            if (obstruct && !obstacleSold && current.cellX == goal.x && current.cellY == goal.y && !current.moving) {
                if (!current.airborne || height(s, id) < shad->jumpJetHeight * runtimeJumpjetQ16)
                    return fail("unsafe occupied ground caused early descent/touchdown");
                if (!runtimeCellHasHardWholeCellBlocker(s, goal, id, -1, {})) return fail("original goal obstruction lost its actual foundation");
                if (++waiting >= s.config.fixedTickHz / 2) {
                    if (!enter(input, id, {obstructedEntry}, false)) return false;
                    obstructionWitness = true;
                    const auto point = objectPoint(s, obstacle); if (!point) return fail("actual obstruction sale hit unavailable");
                    const RECT tool = runtimeSidebarToolRect(sidebar, RuntimeSidebarTool::Sell);
                    click(input, {(tool.left + tool.right) / 2, (tool.top + tool.bottom) / 2}); click(input, *point);
                    if (findObjectById(s, obstacle)) return fail("real Sell did not clear occupied landing cell");
                    const auto emptyPoint = runtimeScreenPointForCell(s, viewport, {goal.x - 5, goal.y + 5}, 0);
                    click(input, emptyPoint, true);
                    if (s.activeSidebarTool != RuntimeSidebarTool::None) return fail("real obstruction Sell cancellation failed");
                    obstacleSold = true;
                }
            }
            if (height(s, id) < h) {
                if (current.x != goal.x || current.y != goal.y) return fail("SHAD descended before physical destination XY");
                descended = true;
                if (deniedPassenger >= 0 && !deniedDescent && current.airborne) {
                    if (!enter(input, id, {deniedPassenger}, false)) return false; deniedDescent = true;
                }
                if (save && !saved && current.airborne && current.z < 1.5) {
                    stage = "descending ordinary / canonical checkpoint";
                    ordinary = checkpoint(s, false); exact = checkpoint(s, true);
                    if (!ordinary || !exact) return fail("descending load rejected: " + error);
                    if (!compare(s, *ordinary, id) || !compare(s, *exact, id)) return false;
                    ++ordinarySaves; ++exactSaves; saved = true;
                }
            }
            if (flew && !current.airborne) {
                if (!descended || current.x != goal.x || current.y != goal.y || current.z != 0
                    || !deniedCruise || !deniedDescent || (save && !saved) || !obstructionWitness || !obstacleSold)
                    return fail("real flight/denial/descent/checkpoint sequence incomplete");
                stage = "stable ground contact";
                for (int k = 0; k < s.config.fixedTickHz / 2; ++k) {
                    if (!tickFlight(s, id) || pose(s, id).airborne || pose(s, id).x != goal.x || pose(s, id).y != goal.y)
                        return fail("landed SHAD relaunched/drifted without an order");
                    if (ordinary) { step(*ordinary); step(*exact);
                        if (!compare(s, *ordinary, id) || !compare(s, *exact, id)) return false; }
                }
                if (pictures && !outputRoot.empty()) {
                    const auto stats = drawRuntimeStateOffscreenAtCell(s, goal, 960, 640, error,
                        outputRoot + "/nighthawk-landed.bmp", false, true);
                    if (!stats || !stats->rendered || stats->visibleVoxelBodies == 0) return fail("real landed VXL raster: " + error);
                    ++captures;
                }
                ++landings; return true;
            }
        }
        return fail("actual flight did not complete its natural landing: " + diagnostic(s, id));
    };
    const auto core = [&](int hz, int mode) {
        auto box = make(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s); Input input{s};
        if (!openInput(input)) return false;
        const int host = add(s, "SHAD", {64, 64}); if (host < 0) return fail("original SHAD creation failed");
        if (!settle(s, host)) return false;
        std::vector<int> passengers;
        if (mode == 1) {
            for (int n = 0; n < 5; ++n) {
                const int id = add(s, n == 4 ? "ENGINEER" : "E1", {60 + n % 3, 64 + n / 3});
                if (id < 0) return fail("original infantry creation failed"); passengers.push_back(id);
            }
            stage = "five real ground passengers enter"; if (!enter(input, host, passengers, true)) return false;
            // Preserve the actual arrival order; different original walk distances
            // legitimately board a later selected passenger before an earlier one.
            passengers = findObjectById(s, host)->transportPassengerIds;
            const int excess = add(s, "E1", {62, 66}); if (excess < 0 || !enter(input, host, {excess}, false)) return false;
        }
        int deniedPassenger = -1;
        if (mode == 2) { deniedPassenger = add(s, "E1", {78, 64}); if (deniedPassenger < 0) return fail("original denied-entry infantry missing"); }
        if (!fly(input, host, {80, 64}, passengers, deniedPassenger,
            (mode == 0 && hz == 30) || (mode == 1 && hz == 60), mode == 0 && hz == 30, false)) return false;
        if (mode == 1) {
            stage = "real selected-object Secondary unload on ground";
            applySelection(s, {host}, false, false); const auto p = objectPoint(s, host);
            if (!p) return fail("real landed unload hit missing"); click(input, *p);
            bool exitedAndSettled = false;
            for (int n = 0; n < s.config.fixedTickHz * 25; ++n) {
                const auto* actualHost = findObjectById(s, host);
                if (!actualHost) return fail("real Secondary lost the original live transport");
                exitedAndSettled = actualHost->transportPassengerIds.empty();
                for (int id : passengers) {
                    const auto* actualPassenger = findObjectById(s, id);
                    exitedAndSettled = exitedAndSettled && actualPassenger
                        && !actualPassenger->renderMoveActive && !s.behaviors.contains(id);
                }
                if (exitedAndSettled) break;
                if (!tickFlight(s, host)) return false;
            }
            if (!exitedAndSettled) {
                std::ostringstream detail;
                detail << std::setprecision(17) << "real Secondary did not naturally finish the original five-passenger egress"
                    << " tick=" << s.tick << " hz=" << s.config.fixedTickHz << " boundSeconds=25";
                const auto describe = [&](const WorldObjectInstance& actor) {
                    const auto body = runtimeTransportMotionBody(actor);
                    detail << " [id=" << actor.instanceId << " type=" << actor.type << " owner=" << actor.owner
                        << " alive=" << actor.alive << " hp=" << actor.health << " mission=" << actor.mission
                        << " cell=" << actor.x << ',' << actor.y << " slot=" << actor.subCell
                        << " body=" << body.x << ',' << body.y << ',' << body.level << " bridge=" << body.onBridge
                        << " air=" << actor.airborne << " pose=" << actor.hasRenderPosition << " active=" << actor.renderMoveActive;
                    if (const auto pending = s.pendingTransportTargets.find(actor.instanceId); pending != s.pendingTransportTargets.end())
                        detail << " pendingHost=" << pending->second;
                    else detail << " pendingHost=-1";
                    if (const auto found = s.behaviors.find(actor.instanceId); found != s.behaviors.end()) {
                        const auto& b = found->second;
                        detail << " kind=" << int(b.kind) << " state=" << int(b.state)
                            << " goal=" << b.moveTarget.x << ',' << b.moveTarget.y << " target=" << b.targetObjectId
                            << " allowed=" << b.pendingAllowedOccupiedObjectId << " transfer=" << int(b.transportTransferKind)
                            << " transferHost=" << b.transportTransferHostId << " shared=" << b.pendingHasSharedGoal
                            << " sharedGoal=" << b.pendingSharedGoal.x << ',' << b.pendingSharedGoal.y
                            << " nearby=" << b.pendingAllowNearbyTarget << " pathPending=" << b.pathRequestPending
                            << " request=" << b.pendingPathStart.x << ',' << b.pendingPathStart.y << "->" << b.pendingPathGoal.x << ',' << b.pendingPathGoal.y
                            << " clocks=" << b.moveCooldownTicks << '/' << b.renderMoveTicksRemaining << '/' << b.renderMoveTicksTotal
                            << " blocked=" << b.blockedTicks << " replans=" << b.pathReplans << " nextRepath=" << b.nextRepathTick
                            << " segment=" << b.renderFrom.x << ',' << b.renderFrom.y << "->" << b.renderTo.x << ',' << b.renderTo.y
                            << " offsets=" << b.renderFromOffsetX << ',' << b.renderFromOffsetY << "->" << b.renderToOffsetX << ',' << b.renderToOffsetY
                            << " levels=" << b.renderFromLevel << "->" << b.renderToLevel
                            << " layers=" << b.renderFromOnBridge << "->" << b.renderToOnBridge << " path=" << b.path.size() << ':';
                        for (const auto& cell : b.path) detail << cell.x << ',' << cell.y << ';';
                    } else detail << " behavior=none";
                    if (const auto reserved = s.subcells.reservations().find(actor.instanceId); reserved != s.subcells.reservations().end())
                        detail << " reservation=" << reserved->second.destination.x << ',' << reserved->second.destination.y
                            << '/' << reserved->second.subCell << "/bridge" << reserved->second.destination.onBridge;
                    else detail << " reservation=none";
                    detail << ']';
                };
                if (const auto* actualHost = findRuntimeObjectByIdIncludingInactive(s, host)) {
                    detail << " host:"; describe(*actualHost);
                    detail << " pendingUnload=" << runtimeTransportUnloadingPending(s, *actualHost) << " cargo=";
                    for (int id : actualHost->transportPassengerIds) detail << id << ',';
                } else detail << " host=missing";
                for (int id : passengers) {
                    detail << " passenger:";
                    const auto* actor = findRuntimeObjectByIdIncludingInactive(s, id);
                    if (!actor) { detail << " missingId=" << id; continue; }
                    describe(*actor);
                    const auto found = s.behaviors.find(id);
                    if (found == s.behaviors.end() || found->second.kind != RuntimeBehaviorKind::Move
                        || found->second.moveTarget.x < 0 || found->second.moveTarget.y < 0) continue;
                    const auto goal = found->second.moveTarget;
                    detail << " goalEvidence=" << id << '@' << goal.x << ',' << goal.y << " availableSlots=";
                    for (int slot : runtimeAvailableInfantrySubcells(s, *actor, goal)) detail << slot << ',';
                    detail << " occupantsAndNearbyBodies:";
                    for (const auto& other : s.world.getObjects()) {
                        if (!other.alive || other.health <= 0 || other.instanceId == id) continue;
                        const auto body = runtimeTransportMotionBody(other);
                        if ((other.x == goal.x && other.y == goal.y)
                            || std::hypot(body.x - double(goal.x), body.y - double(goal.y)) <= 1.0) describe(other);
                    }
                    detail << " sameGoalClaims=";
                    for (const auto& [otherId, b] : s.behaviors)
                        if (otherId != id && b.kind == RuntimeBehaviorKind::Move
                            && b.moveTarget.x == goal.x && b.moveTarget.y == goal.y) detail << otherId << ',';
                }
                return fail(detail.str());
            }
            if (!findObjectById(s, host)->transportPassengerIds.empty()) return fail("real Secondary did not unload original cargo");
            for (int id : passengers) {
                const auto* body = findObjectById(s, id); const auto* rule = body ? findRuntimeUnitType(s.rules, *body) : nullptr;
                if (!body || !rule || body->health != rule->strength || body->airborne || body->subCell < 0
                    || runtimeCellDistance(runtimeCellPointForObject(*body), {80, 64}) > 3)
                    return fail("real unload changed identity/HP or omitted legal ground subcell");
                ++unloaded;
            }
        } else if (mode == 2) {
            stage = "previously rejected passenger enters after touchdown";
            if (!enter(input, host, {deniedPassenger}, true)) return false;
        } else {
            if (!fly(input, host, {64, 80}, {}, -1, false, false, hz == 30)) return false;
        }
        return true;
    };
    for (int hz : {30, 60}) for (int mode = 0; mode < 3; ++mode) {
        const std::string name = std::string(mode == 0 ? "empty-repeat" : mode == 1 ? "five-cargo" : "deny-air-entry") + '-' + std::to_string(hz);
        scene(name, [&] { return core(hz, mode); });
    }
    scene("stopped-segment-resume-30", [&] {
        auto box = make(30); auto& s = *box; RuntimeSnapshotActiveStateScope active(s); Input input{s};
        if (!openInput(input)) return false; const int id = add(s, "SHAD", {64, 64});
        if (id < 0 || !settle(s, id) || !move(input, id, {80, 64})) return false;
        bool begun = false;
        for (int n = 0; n < 30 * 8 && !begun; ++n) {
            if (!tickFlight(s, id)) return false; const auto f = s.jumpjetFlights.find(id);
            begun = f != s.jumpjetFlights.end() && f->second.active && f->second.progress > 0 && f->second.progress < f->second.length;
        }
        if (!begun) return fail("real fractional segment never began");
        stage = "real Stop freezes committed segment"; applySelection(s, {id}, false, false); const auto before = pose(s, id);
        const int key = runtimeMenuKeyboardBindingCode(s, "StopObject", 'S');
        SendMessageW(input.window, WM_KEYDOWN, key, 1); SendMessageW(input.window, WM_KEYUP, key, 1);
        const auto stopped = pose(s, id);
        if (before.x != stopped.x || before.y != stopped.y || before.z != stopped.z)
            return fail("Stop snapped fractional flight pose");
        for (int n = 0; n < 15; ++n) {
            if (!tickFlight(s, id)) return false; const auto current = pose(s, id);
            if (current.x != stopped.x || current.y != stopped.y || !current.airborne)
                return fail("stopped fractional segment moved or auto-landed");
        }
        return fly(input, id, {76, 76}, {}, -1, false, false, false);
    });
    scene("native-hover-and-HIND-control-30", [&] {
        auto box = make(30); auto& s = *box; RuntimeSnapshotActiveStateScope active(s); Input input{s};
        if (!openInput(input)) return false; std::vector<int> hovering;
        int n = 0;
        for (const std::string type : {"JUMPJET", "LUNR", "DISK", "ZEP"}) {
            const auto* rule = findRuntimeUnitTypeById(s.rules, type);
            if (!rule || !rule->balloonHover) return fail("original balloon control missing: " + type);
            const int id = add(s, type, {70 + n * 4, 88}); if (id < 0) return fail("original hovering control creation failed");
            hovering.push_back(id); ++n;
        }
        const auto* hindRule = findRuntimeUnitTypeById(s.rules, "HIND");
        if (!hindRule || !runtimeUnitUsesJumpjetMovement(*hindRule) || hindRule->balloonHover
            || hindRule->isSimpleDeployer || hindRule->deployToLand) return fail("original HIND non-balloon control missing");
        const int hind = add(s, "HIND", {64, 64}); if (hind < 0 || !settle(s, hind)) return false;
        for (int k = 0; k < 30 * 6; ++k) {
            step(s);
            for (int id : hovering) {
                const auto* body = findObjectById(s, id);
                if (!body || !body->airborne || body->renderLevel <= terrainLevelAtRuntimeCell(s, body->x, body->y))
                    return fail("ordinary landing regression grounded a native BalloonHover control");
                ++hoverChecks;
            }
        }
        return fly(input, hind, {80, 64}, {}, -1, false, false, false);
    });
    for (int hz : {30, 60}) scene("native-water-beach-denial-" + std::to_string(hz), [&] {
        stage = "decode original Water2 / Beach6 TMP subtiles";
        std::optional<TerrainCell> water, beach;
        for (const auto& set : live.theater.getTileSets()) {
            const auto name = toLowerAsciiLocal(set.fileName + " " + set.setName);
            if (name.find("water") == std::string::npos && name.find("shore") == std::string::npos
                && name.find("beach") == std::string::npos) continue;
            for (int tile = set.startTileIndex; tile < set.startTileIndex + set.tilesInSet; ++tile) {
                for (int sub = 0; sub < 16; ++sub) {
                    TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = sub;
                    const auto nativeType = resolveRuntimeTerrainSubTileLandType(cell, metadata, *tileCache);
                    if (resolveRuntimeTerrainSubTileRampType(cell, metadata, *tileCache) != 0) continue;
                    if (!water && nativeType == RuntimeTerrainLandType::Water) water = cell;
                    if (!beach && nativeType == RuntimeTerrainLandType::Beach) beach = cell;
                    if (water && beach) break;
                }
                if (water && beach) break;
            }
            if (water && beach) break;
        }
        if (!water || !beach) return fail("original decoded Water2/Beach6 flat TMP subtiles unavailable");
        std::cout << "Nighthawk native unsafe-ground TMP: Water=" << water->tileIndex << ':' << water->subTileIndex
            << " Beach=" << beach->tileIndex << ':' << beach->subTileIndex << " Hz=" << hz << '\n';
        auto box = make(hz, &*water, &*beach); auto& s = *box;
        RuntimeSnapshotActiveStateScope active(s); Input input{s};
        if (!openInput(input)) return false;
        const int host = add(s, "SHAD", {64, 64}); if (host < 0 || !settle(s, host)) return false;
        const int passenger = add(s, "ENGINEER", {62, 64}), candidate = add(s, "E1", {77, 64});
        if (passenger < 0 || candidate < 0 || !enter(input, host, {passenger}, true))
            return fail("real dry-ground boarding fixture failed: " + error);
        const auto manifest = findObjectById(s, host)->transportPassengerIds;
        const auto unsafeFlight = [&](RuntimeCellPoint goal, RuntimeTerrainLandType expected) {
            stage = expected == RuntimeTerrainLandType::Water ? "real Water2 flight / finite hover / NoEnter"
                : "real Beach6 flight / finite hover / NoEnter";
            const auto terrain = std::find_if(s.world.getTerrainCells().begin(), s.world.getTerrainCells().end(),
                [&](const auto& cell) { return cell.x == goal.x && cell.y == goal.y; });
            if (terrain == s.world.getTerrainCells().end()) return fail("actual unsafe destination terrain missing");
            const auto validTerrain = [&] {
                const auto effective = runtimeNavigationTerrainInfoForCell(s, *terrain);
                return resolveRuntimeTerrainSubTileLandType(*terrain, runtimeTerrainMetadataContext(s), s.renderCache) == expected
                    && effective.movementLandType == expected && !effective.bridge && !runtimeCellIsBridge(s, goal.x, goal.y);
            };
            if (!validTerrain()) return fail("actual TMP/effective movement type or bridge exception disagrees with destination");
            if (!move(input, host, goal)) return false;
            bool flew = false, rejected = false; int stationaryTicks = 0;
            // Five seconds exceeds the original 500/(10*15) second descent.
            // A merely delayed ordinary landing therefore cannot pass.
            for (int n = 0; n < hz * 45; ++n) {
                if (!tickFlight(s, host)) return false;
                const auto current = pose(s, host);
                flew = flew || (current.airborne && current.moving && height(s, host) > 0);
                if (!cargoValid(s, host, manifest) || findObjectById(s, host)->transportPassengerIds != manifest)
                    return fail("unsafe-ground flight changed real hidden cargo order/HP/position");
                if (stationaryTicks > 0 && (current.x != goal.x || current.y != goal.y || current.moving))
                    return fail("unsafe-ground hover drifted or restarted without a new order: " + diagnostic(s, host));
                if (!flew || current.x != goal.x || current.y != goal.y || current.moving) continue;
                const auto flight = s.jumpjetFlights.find(host);
                if (!validTerrain() || !current.airborne || flight == s.jumpjetFlights.end() || flight->second.active
                    || flight->second.stopped || height(s, host) != shad->jumpJetHeight * runtimeJumpjetQ16)
                    return fail("unsafe native ground descended, landed, or changed physical goal: " + diagnostic(s, host));
                const auto occupancy = buildRuntimeOccupancyLookup(s);
                if (const auto occupied = occupancy.find(runtimeCellKey(goal.x, goal.y));
                    occupied != occupancy.end() && occupied->second == host) return fail("unsafe airborne SHAD claimed ground occupancy");
                ++unsafeGroundHoverChecks; ++stationaryTicks;
                if (!rejected) { if (!enter(input, host, {candidate}, false)) return false; rejected = true; }
                if (stationaryTicks >= hz * 5) {
                    if (!enter(input, host, {candidate}, false)) return false;
                    const auto* infantry = findObjectById(s, candidate);
                    const auto* rule = infantry ? findRuntimeUnitType(s.rules, *infantry) : nullptr;
                    if (!infantry || !rule || infantry->health != rule->strength || !s.pendingTransportTargets.empty())
                        return fail("unsafe hover consumed/damaged or queued the rejected original passenger");
                    return true;
                }
            }
            return fail("real native unsafe-ground destination was not reached and held: " + diagnostic(s, host));
        };
        if (!unsafeFlight({80, 64}, RuntimeTerrainLandType::Water)
            || !fly(input, host, {64, 80}, manifest, -1, false, false, false)
            || !unsafeFlight({80, 80}, RuntimeTerrainLandType::Beach)
            || !fly(input, host, {64, 64}, manifest, -1, false, false, false)) return false;
        return true;
    });
    std::cout << "Nighthawk results: scenes=" << scenes << " landings=" << landings << " fullTickMotion=" << horizontalChecks
        << " nativeClimbDescent=" << verticalChecks << " realBoarded=" << boarded << " realUnloaded=" << unloaded
        << " deniedAirFull=" << deniedEntries << " saves=" << ordinarySaves << '+' << exactSaves << " pairedTicks=" << pairedTicks
        << " Win32Clicks=" << clicks << " hoverControls=" << hoverChecks << " nativeUnsafeHover=" << unsafeGroundHoverChecks
        << " nativeVxlCaptures=" << captures << '\n';
    if (!failures.empty()) { error = std::to_string(failures.size()) + " Nighthawk scenes failed; first: " + failures.front(); return false; }
    if (!filter.empty() && scenes == 0) { error = "Nighthawk requested scene did not match"; return false; }
    return true;
}
