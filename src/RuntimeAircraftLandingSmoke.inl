// Real production, original airport art, and full simulation ticks. The
// vertical oracle below is the retail 15 Hz non-dropship lepton step, rather
// than the runtime's landing helper or its serialized envelope fields.
bool runRuntimeAircraftLandingSmoke(const RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    IniFile stock, authoredArt; RulesDatabase rules; ArtDatabase art;
    if (!stock.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(stock)
        || !authoredArt.loadFromFile(live.report.artPath) || !art.loadFromIni(authoredArt)) {
        error = "Aircraft landing: original rules/art unavailable"; return false;
    }
    const char* output = std::getenv("CONCORD_AIRCRAFT_LANDING_OUTPUT");
    const char* requested = std::getenv("CONCORD_AIRCRAFT_LANDING_CASE");
    const std::string outputRoot = output ? output : "", filter = requested ? requested : "";
    if (!outputRoot.empty()) std::filesystem::create_directories(outputRoot);
    std::string label, stage, fixtureFailure; std::vector<std::string> failures;
    int scenes = 0, produced = 0, shots = 0, landings = 0, verticalChecks = 0, continuityChecks = 0;
    int ordinarySaves = 0, exactSaves = 0, pairedTicks = 0, bodyChecks = 0, rasterChecks = 0, captures = 0;
    int stops = 0, redirects = 0, lostHosts = 0, reloads = 0, repairs = 0, auxiliaryChecks = 0;
    const auto fail = [&](const std::string& why) {
        error = "Aircraft landing " + label + " [" + stage + "]: " + why; return false;
    };
    const auto scene = [&](const std::string& name, const auto& body) {
        if (!filter.empty() && filter != name) return;
        label = name; stage = "setup"; fixtureFailure.clear();
        bool passed = body();
        if (!fixtureFailure.empty()) { fail(fixtureFailure); passed = false; }
        if (passed) { ++scenes; std::cout << "Aircraft landing " << name << " PASS\n"; }
        else { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    };
    int landTile = -1; auto terrainCache = std::make_unique<RuntimeMapRenderCache>();
    const auto terrainMetadata = runtimeTerrainMetadataContext(live);
    for (int tile = 0; tile < static_cast<int>(live.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(live, cell);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge
            && resolveRuntimeTerrainSubTileRampType(cell, terrainMetadata, *terrainCache) == 0) {
            landTile = tile; break;
        }
    }
    if (landTile < 0) { error = "Aircraft landing: original flat land TMP unavailable"; return false; }
    const auto add = [&](RuntimeWindowState& s, int id, const std::string& type,
            RuntimeCellPoint cell, const std::string& owner = "Commander") {
        WorldObjectInstance object; object.instanceId = id; object.mapId = "Landing" + std::to_string(id);
        object.type = type; object.kind = WorldObjectKind::Structure; object.owner = owner;
        object.x = cell.x; object.y = cell.y; object.cell = runtimeCellKey(cell.x, cell.y);
        object.alive = object.poweredOperational = true; object.health = runtimeMaxHealthForObject(s, object);
        object.mission = "Sleep"; object.buildAnimTicksRemaining = 0;
        initializeRuntimeObjectAmmo(object, runtimeAmmoRulesForObject(s.rules, object));
        runtimeInitializeModExtensions(object, s.rules, s.config.fixedTickHz);
        s.world.accessObjects().push_back(std::move(object));
    };
    const auto make = [&](const std::string& hostType, const std::string& planeType, int hz, bool alternate = false) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        seedRuntimeLoadScratch(s, live, false); s.config = live.config; s.config.fixedTickHz = hz;
        s.config.gameMode = "Battle"; s.config.playerCountry = planeType == "BEAG" ? "Alliance" : "Americans";
        s.config.multiplayerActive = s.config.computerAiEnabled = s.config.noCooldown = s.config.shortGame = false;
        s.config.crates = s.config.superweapons = false; s.config.startingUnits = 0; s.config.playerSlots.clear();
        s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.rules = rules; s.art = art; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.mapFacts = {}; s.mapFacts.theater = live.mapFacts.theater; s.mapFacts.lighting = live.mapFacts.lighting;
        s.report.map.size.clear(); s.report.map.localSize.clear();
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y = 55; y <= 112; ++y) for (int x = 55; x <= 112; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = landTile; cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        add(s, 1, hostType, {75, 75}); add(s, 100, "GAPOWR", {100, 100});
        add(s, 101, "GAPOWR", {104, 100});
        add(s, 2, "NACNST", {90, 88}, "SkirmishPlayer2");
        add(s, 998, "NANRCT", {57, 57}, "SkirmishPlayer2");
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner = "Commander"; s.economy.credits = 20000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        RuntimeConfig::PlayerSlot local; local.enabled = local.human = local.local = true;
        local.name = "Commander"; local.country = s.config.playerCountry; local.color = "Gold";
        RuntimeConfig::PlayerSlot hostile; hostile.enabled = true; hostile.country = "Russians"; hostile.color = "Red";
        s.config.playerSlots = {local, hostile};
        s.skirmishCountryByOwner.clear(); s.skirmishTeamByOwner.clear(); s.playerCommands.alliances.clear();
        s.skirmishCountryByOwner["commander"] = s.config.playerCountry;
        s.skirmishCountryByOwner["skirmishplayer2"] = "Russians";
        s.humanHouseAuthorities.clear(); RuntimeHumanHouseAuthority house;
        house.owner = "Commander"; house.canonicalOwner = "commander"; house.country = s.config.playerCountry;
        house.localFacade = true; s.humanHouseAuthorities.push_back(house);
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        ensureRuntimeRepairDepotsInitialized(s); initializeRuntimeOfflineGameplayRouting(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s); }
        rebuildRuntimeEconomyPower(s);
        RuntimeBuildCatalogItem item; item.kind = RuntimeBuildKind::Unit; item.typeId = planeType;
        item.requiredFactory = "AircraftType";
        for (int n = 0; n < 4; ++n) {
            RuntimeSnapshotActiveStateScope active(s);
            if (!completeRuntimeProductionOrder(s, item)) {
                fixtureFailure = "real four-pad production rejected: " + s.simulationStatus; break;
            }
            ++produced;
        }
        if (alternate) {
            add(s, 3, hostType, {84, 75}); initializeRuntimeNavigation(s); completeRuntimeWorldRefresh(s);
            rebuildRuntimeEconomyPower(s);
        }
        return storage;
    };
    struct Pose {
        double x = 0, y = 0, z = 0, progress = -1;
        int facing = -1, hp = 0, ammo = 0, host = -1, pad = -1, cruise = 0, cellX = 0, cellY = 0;
        bool airborne = false, rendered = false, moving = false;
        bool operator==(const Pose&) const = default;
    };
    const auto pose = [&](const RuntimeWindowState& s, int id) {
        Pose p;
        if (const auto* object = findObjectById(s, id)) {
            p.x = object->hasRenderPosition ? object->renderX : double(object->x);
            p.y = object->hasRenderPosition ? object->renderY : double(object->y);
            p.z = object->hasRenderPosition ? object->renderLevel : double(object->level);
            p.facing = object->facing; p.hp = object->health; p.ammo = object->currentAmmo;
            p.host = object->dockedToObjectId; p.pad = object->dockIndex; p.cruise = object->flightLevel;
            p.cellX = object->x; p.cellY = object->y; p.airborne = object->airborne;
            p.rendered = object->hasRenderPosition; p.moving = object->renderMoveActive;
            p.progress = object->aircraftDockFlightProgress;
        }
        return p;
    };
    const auto diagnostic = [&](const RuntimeWindowState& s, int id) {
        const auto p = pose(s, id); std::ostringstream out; out << std::setprecision(17)
            << "tick=" << s.tick << " id=" << id << " XYZ=" << p.x << ',' << p.y << ',' << p.z
            << " cell=" << p.cellX << ',' << p.cellY << " cruise=" << p.cruise << " airborne=" << p.airborne
            << " moving=" << p.moving << " render=" << p.rendered << " facing=" << p.facing
            << " hp/ammo=" << p.hp << '/' << p.ammo << " dock=" << p.host << ':' << p.pad << " presentation=" << p.progress;
        if (const auto* object = findObjectById(s, id)) out << " mission=" << object->mission;
        out << " reload=" << s.aircraftServices.remainingReloadTicks(p.host)
            << " repair=" << s.aircraftServices.remainingRepairTicks(p.host)
            << " flights={" << serializeRuntimeActiveAircraftFlights(s) << '}';
        return out.str();
    };
    struct Pad { double x = 0, y = 0, z = 0; };
    const auto padFor = [&](const RuntimeWindowState& s, int hostId, int index, Pad& pad) {
        const auto* host = findObjectById(s, hostId);
        const auto* authored = host ? runtimeArtTypeForObject(s, *host) : nullptr;
        if (!host || !authored || index < 0 || index > 3) return fail("original airport/pad art missing");
        // Independent authored foundation center and lepton offsets; do not
        // ask runtimeAircraftDockSpawn for the expected point.
        int foundationX = 0, foundationY = 0; char separator = 0;
        std::istringstream foundation(authored->foundation);
        if (!(foundation >> foundationX >> separator >> foundationY) || separator != 'x'
            || foundationX <= 0 || foundationY <= 0) return fail("original airport foundation malformed");
        const auto offset = std::find_if(authored->dockingOffsets.begin(), authored->dockingOffsets.end(),
            [&](const auto& value) { return value.index == index; });
        if (offset == authored->dockingOffsets.end()) return fail("original four docking offsets missing");
        constexpr std::array<int, 4> originalX{0, 0, 256, 256}, originalY{-128, 128, -128, 128};
        if (offset->x != originalX[index] || offset->y != originalY[index] || offset->z != 0)
            return fail("original GAAIRC/AMRADR docking offsets changed");
        pad.x = (host->hasRenderPosition ? host->renderX : host->x) + foundationX * 0.5 + offset->x / 256.0;
        pad.y = (host->hasRenderPosition ? host->renderY : host->y) + foundationY * 0.5 + offset->y / 256.0;
        pad.z = host->level + std::lround(offset->z / 256.0); return true;
    };
    const auto step = [&](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
        if (s.skirmishMatchFinished && fixtureFailure.empty()) fixtureFailure = "Battle ended unexpectedly: " + s.skirmishResult;
    };
    const auto checkpoint = [&](RuntimeWindowState& s, bool exact) {
        std::ostringstream baseText, exactText, canonical; IniFile baseIni, exactIni;
        if (!writeRuntimeStateToStream(s, baseText, error) || !baseIni.loadFromString(baseText.str())) return std::unique_ptr<RuntimeWindowState>{};
        if (exact && (!writeRuntimeStateToStream(s, exactText, error)
            || !writeRuntimeMultiplayerSnapshotExtension(s, exactText, error)
            || !exactIni.loadFromString(exactText.str()) || !writeRuntimeMultiplayerSnapshotCanonicalIni(exactIni, canonical, error)
            || !exactIni.loadFromString(canonical.str()))) return std::unique_ptr<RuntimeWindowState>{};
        auto restored = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored, s, false);
        // The canonical extension omits v14 process-local AV authority.
        if (!materializeRuntimeLoadStateFromIni(*restored, baseIni, error)
            || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*restored, exactIni, error))) return std::unique_ptr<RuntimeWindowState>{};
        initializeRuntimeOfflineGameplayRouting(*restored); return restored;
    };
    const auto compare = [&](const RuntimeWindowState& s, const RuntimeWindowState& other, int id) {
        if (s.tick != other.tick || pose(s, id) != pose(other, id)
            || serializeRuntimeActiveAircraftFlights(s) != serializeRuntimeActiveAircraftFlights(other)
            || s.aircraftServices.stateHash() != other.aircraftServices.stateHash()
            || s.economy.credits != other.economy.credits)
            return fail("saved landing/aircraft-service continuation changed: source " + diagnostic(s, id)
                + " restored " + diagnostic(other, id));
        ++pairedTicks; return true;
    };
    const auto facingDifference = [](int a, int b) { const int d = std::abs(a - b) % 256; return std::min(d, 256 - d); };
    const auto inspectMotion = [&](const RuntimeWindowState& s, int id, const Pose& before, const Pose& after) {
        const auto* object = findObjectById(s, id); const auto* type = object ? findRuntimeUnitType(s.rules, *object) : nullptr;
        if (!object || !type || !after.rendered || !std::isfinite(after.x) || !std::isfinite(after.y) || !std::isfinite(after.z))
            return fail("real aircraft/continuous pose missing: " + diagnostic(s, id));
        const double maximumStep = std::min(255, type->speed * 256 / 100) * 15.0 / (256.0 * s.config.fixedTickHz);
        if (std::hypot(after.x - before.x, after.y - before.y) > maximumStep + 2.0 / 65536.0)
            return fail("horizontal motion snapped or exceeded stock Speed: " + diagnostic(s, id));
        const int maximumTurn = int(std::ceil(type->rot * 15.0 / s.config.fixedTickHz)) + 1;
        if (facingDifference(before.facing, after.facing) > maximumTurn)
            return fail("heading jumped beyond original ROT: " + diagnostic(s, id));
        ++continuityChecks; return true;
    };
    // Retail Fly::Process descent, independently expressed in lepton units.
    const auto expectedDescent = [](double height, double ground, int hz) {
        const auto remaining = std::llround((height - ground) * 65536.0);
        if (remaining <= 0) return std::int64_t{0};
        const auto nativeRate = std::clamp<std::int64_t>(remaining / (20 * 256), 20, 50);
        return std::min(remaining, std::llround(nativeRate * (65536.0 / 256.0) * 15.0 / hz));
    };
    constexpr int imageWidth = 448, imageHeight = 352;
    constexpr std::uint32_t background = 0x00212c39;
    const RECT viewport{0, 0, imageWidth, imageHeight};
    auto bodyCache = std::make_unique<RuntimeMapRenderCache>();
    const auto contextFor = [&](const RuntimeWindowState& s) {
        RuntimeMapRenderContext c; c.rules = &s.rules; c.art = &s.art; c.theater = &s.theater;
        c.assetRootPath = runtimeAssetRootPath(s.config); c.palettePath = runtimePalettePath(s.config, s.report.map);
        c.structurePalettePath = runtimeStructurePalettePath(s.config, s.report.map); c.ownerRemapColors = &s.ownerRemapColors;
        c.animationTick = s.tick; c.fixedTickHz = s.config.fixedTickHz; c.shadowsEnabled = false; c.useTileTextures = false;
        c.surfaceWidth = imageWidth; c.surfaceHeight = imageHeight; return c;
    };
    const auto cameraFor = [&](const Pad& pad, int tileWidth = 60) {
        RuntimeMapCamera camera; camera.initialized = true;
        camera.tileWidth = tileWidth; camera.tileHeight = tileWidth / 2;
        const auto projected = runtimeProjectCellXY(pad.x, pad.y, pad.z, camera);
        camera.worldOffsetX = projected.x - imageWidth / 2; camera.worldOffsetY = projected.y - imageHeight * 3 / 4;
        return camera;
    };
    const auto renderedAnchor = [&](const RuntimeWindowState& s, int id, const RuntimeMapCamera& camera, POINT& anchor) {
        const auto* object = findObjectById(s, id); if (!object) return fail("body anchor aircraft absent");
        auto c = contextFor(s);
        anchor = runtimeObjectBodyAnchorForRender(*object, viewport, c, camera);
        RuntimeParticleAirbornePresentation presentation;
        if (!captureRuntimeAircraftAirbornePresentation(s.world, *object, c, *bodyCache, presentation, &error)) return false;
        anchor.y -= runtimeAircraftAirbornePresentationLiftPixels(presentation, camera,
            -runtimeAircraftSpriteLiftPixels(*object, runtimeObjectUsesAircraftSpriteForRender(*object, c)));
        ++bodyChecks; return true;
    };
    const auto bodyRaster = [&](const RuntimeWindowState& s, int id, const RuntimeMapCamera& camera, POINT& center) {
        const auto* source = findObjectById(s, id); if (!source) return fail("body raster aircraft absent");
        GameWorld view = s.world; auto c = contextFor(s);
        view.accessTerrainCells().clear(); view.accessTerrainObjects().clear(); view.accessSmudges().clear();
        auto& objects = view.accessObjects();
        objects.erase(std::remove_if(objects.begin(), objects.end(), [](const auto& object) {
            return object.kind != WorldObjectKind::Structure;
        }), objects.end());
        std::vector<std::uint32_t> blank(imageWidth * imageHeight, background), actual(imageWidth * imageHeight, background);
        c.surfacePixels = blank.data(); auto cameraCopy = camera;
        const auto empty = drawRuntimeMap(nullptr, viewport, view, c, cameraCopy, *bodyCache);
        auto body = *source;
        // Only this isolated view uses a fixed orientation. It removes the
        // legitimate VXL bounding-box changes during ROT, so pixel movement
        // measures body translation. No flight/heading authority is changed.
        body.facing = worldObjectFacingFromMapFacing(s.rules.getPoseDir()); objects.push_back(body);
        c.surfacePixels = actual.data(); cameraCopy = camera;
        const auto drawn = drawRuntimeMap(nullptr, viewport, view, c, cameraCopy, *bodyCache);
        RECT bounds{imageWidth, imageHeight, 0, 0}; int count = 0;
        for (int y = 0; y < imageHeight; ++y) for (int x = 0; x < imageWidth; ++x) {
            const auto at = y * imageWidth + x;
            if ((actual[at] & 0xffffffU) == (blank[at] & 0xffffffU)) continue;
            bounds.left = std::min<LONG>(bounds.left, x); bounds.right = std::max<LONG>(bounds.right, x + 1);
            bounds.top = std::min<LONG>(bounds.top, y); bounds.bottom = std::max<LONG>(bounds.bottom, y + 1); ++count;
        }
        if (!empty.rendered || !drawn.rendered || drawn.visibleVoxelBodies < 1 || drawn.parachuteSprites || count < 8
            || bounds.left <= 0 || bounds.top <= 0 || bounds.right >= imageWidth || bounds.bottom >= imageHeight)
            return fail("original aircraft VXL body raster missing/clipped: pixels=" + std::to_string(count));
        center = {(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2}; ++rasterChecks; return true;
    };
    const auto capture = [&](RuntimeWindowState& s, int id, const char* phase) {
        if (outputRoot.empty()) return true;
        const auto* object = findObjectById(s, id); if (!object) return fail("capture aircraft missing");
        const auto path = std::filesystem::path(outputRoot) / (label + '-' + phase + ".bmp");
        const auto frame = drawRuntimeStateOffscreenAtCell(s, {object->x, object->y}, 800, 600, error, path.string(), false);
        if (!frame || !frame->rendered || frame->visibleVoxelBodies < 1) return fail("original landing frame unavailable: " + error);
        ++captures; return true;
    };
    const auto issueMove = [&](RuntimeWindowState& s, int id, RuntimeCellPoint cell) {
        RuntimeSnapshotActiveStateScope active(s); s.selectedObjectIds = {id}; const auto before = pose(s, id);
        if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Move, cell)
            || s.lastCommand.acceptedCount != 1) return fail("real Move rejected: " + s.interactionStatus);
        if (pose(s, id) != before) return fail("Move changed pose/height before a tick: " + diagnostic(s, id));
        ++redirects; return true;
    };
    const auto run = [&](const std::string& hostType, const std::string& planeType, int hz, int requestedPad,
            const std::string& boundary = "") {
        auto storage = make(hostType, planeType, hz, boundary == "host-loss"); auto& s = *storage;
        if (!fixtureFailure.empty()) return fail(fixtureFailure);
        int id = -1; std::set<int> actualPads;
        for (const auto& object : s.world.getObjects()) if (object.type == planeType) {
            if (object.dockedToObjectId != 1 || object.airborne || object.dockIndex < 0 || object.dockIndex > 3
                || !actualPads.insert(object.dockIndex).second) return fail("real production did not fill four distinct original pads");
            if (object.dockIndex == requestedPad) id = object.instanceId;
        }
        if (actualPads.size() != 4 || id < 0) return fail("four real produced aircraft missing");
        auto* aircraft = findMutableObjectById(s, id); const auto* profile = findRuntimeUnitType(s.rules, *aircraft);
        const auto* ammo = runtimeAmmoRulesForObject(s.rules, *aircraft);
        if (!profile || !ammo || ammo->ammo != 1 || !ammo->airportBound || !ammo->manualReload || aircraft->currentAmmo != 1)
            return fail("original airport fighter magazine/locomotor rules unavailable");
        const int maxHp = runtimeMaxHealthForObject(s, *aircraft), startHp = maxHp - 32;
        aircraft->health = startHp; Pad pad;
        if (!padFor(s, 1, requestedPad, pad)) return false;
        const auto initial = pose(s, id);
        if (initial.x != pad.x || initial.y != pad.y || initial.z != pad.z || initial.airborne)
            return fail("real produced aircraft was not at authored fractional pad: " + diagnostic(s, id));
        const auto* target = findObjectById(s, 2); const int targetHp = target ? target->health : 0;
        stage = "real attack and sortie"; s.selectedObjectIds = {id};
        { RuntimeSnapshotActiveStateScope active(s);
            if (!target || !dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                    runtimeCellPointForObject(*target), target) || s.lastCommand.acceptedCount != 1)
                return fail("real original fighter attack rejected: " + s.interactionStatus);
        }
        const bool saveLanding = boundary.empty() && requestedPad == 1;
        const bool pixelFlight = boundary.empty() && requestedPad == 0;
        const bool writeFrames = pixelFlight && hz == 30;
        std::unique_ptr<RuntimeWindowState> ordinary, exact;
        auto previous = pose(s, id); bool observedAirborne = previous.airborne;
        bool fired = false, damagedTarget = false, descended = false, landed = false, boundaryDone = boundary.empty();
        bool sawOverPad = false, saved = false, reloaded = false, repaired = false;
        int descentTicks = 0, landingTick = -1, reloadTick = -1, landingHost = 1;
        int attackCount = 0; double descentStart = -1;
        POINT previousAnchor{}, previousRaster{}, previousEnlarged{};
        bool haveAnchor = false, haveRaster = false; int enlargedTick = -2;
        const int reloadInterval = int(std::ceil(s.rules.getSpecialMechanismRules().reloadRateMinutes * 60.0 * hz - 1e-9));
        const auto& serviceRules = s.rules.getSpecialMechanismRules();
        int healthPerPulse = serviceRules.repairStep, repairFee = 0;
        if (serviceRules.repairPercent > 0 && profile->cost > 0) {
            const long double pricePerHp = static_cast<long double>(serviceRules.repairPercent) * profile->cost / maxHp;
            const long double paidStep = std::max(1.0L, std::floor(pricePerHp * serviceRules.repairStep));
            repairFee = int(paidStep);
            healthPerPulse = std::min(healthPerPulse, int(std::floor(paidStep / pricePerHp + 1e-12L)));
        }
        if (reloadInterval <= 0 || healthPerPulse <= 0) return fail("original airport repair/reload clock cannot complete service");
        const int repairPulses = (maxHp - startHp + healthPerPulse - 1) / healthPerPulse;
        // Airport servicing uses the original ReloadRate cadence for both
        // timers. General RepairRate/URepairRate belong to other repair
        // mechanisms. The original whole-credit fee can buy only 7 of the
        // authored 8 HP for either stock fighter, requiring five pulses.
        // Allow one initial timer phase, then bound service independently of
        // the actual sortie/redirect travel time. Healing and reload overlap.
        const int travelLimit = hz * 80;
        const int serviceLimit = std::max(reloadInterval, repairPulses * reloadInterval) + 3;
        const int sortieStartTick = s.tick;
        std::ofstream trace;
        if (!outputRoot.empty()) {
            trace.open(std::filesystem::path(outputRoot) / (label + ".csv"));
            trace << "tick,x,y,z,airborne,heading,displayFraction,hp,ammo,host,pad,bodyY\n";
        }
        for (int n = 0; n < travelLimit + serviceLimit + 1; ++n) {
            const int creditsBeforeTick = s.economy.credits;
            step(s); if (ordinary) step(*ordinary); if (exact) step(*exact);
            if (!fixtureFailure.empty()) return fail(fixtureFailure);
            const auto current = pose(s, id); const auto* now = findObjectById(s, id);
            if (!now) return fail("original fighter disappeared: " + diagnostic(s, id));
            if (current.hp > previous.hp && (current.airborne
                || current.hp - previous.hp != std::min(healthPerPulse, maxHp - previous.hp)
                || creditsBeforeTick - s.economy.credits != repairFee))
                return fail("paid airport repair exceeded its original pulse/fee contract: hpDelta="
                    + std::to_string(current.hp - previous.hp) + " creditDelta="
                    + std::to_string(creditsBeforeTick - s.economy.credits)
                    + " expectedHP=" + std::to_string(healthPerPulse) + " fee=" + std::to_string(repairFee)
                    + " " + diagnostic(s, id));
            attackCount += s.actionSummary.attacksThisTick;
            if (current.airborne) observedAirborne = true;
            if (observedAirborne && previous.airborne && !inspectMotion(s, id, previous, current)) return false;
            if (current.ammo == 0 && !fired) { fired = true; ++shots; }
            if (fired && previous.airborne
                && std::abs(current.z - previous.z) > 50.0 * 15.0 / (256.0 * hz) + 1.0 / 65536.0)
                return fail("return/redirection height teleported past the native maximum step: " + diagnostic(s, id));
            if (const auto* actualTarget = findObjectById(s, 2)) damagedTarget |= actualTarget->health < targetHp;
            else damagedTarget = true;
            if (current.airborne && fired && current.hp != startHp)
                return fail("airport repaired a still-airborne fighter: " + diagnostic(s, id));
            if (current.airborne && fired && current.ammo != 0)
                return fail("airport reloaded before touchdown: " + diagnostic(s, id));
            if (current.host >= 0 && findObjectById(s, current.host)) {
                landingHost = current.host;
                if (!padFor(s, current.host, current.pad, pad)) return false;
            }
            const bool overPad = std::abs(current.x - pad.x) < 1e-9 && std::abs(current.y - pad.y) < 1e-9;
            if (fired && current.airborne && overPad) sawOverPad = true;
            if (fired && current.z < previous.z - 1e-9) {
                stage = "native vertical descent";
                if (!overPad || std::abs(previous.x - pad.x) > 1e-9 || std::abs(previous.y - pad.y) > 1e-9)
                    return fail("descent began before the actual pad XY: " + diagnostic(s, id));
                const auto expected = expectedDescent(previous.z, pad.z, hz);
                const auto actual = std::llround((previous.z - current.z) * 65536.0);
                if (std::abs(actual - expected) > 1 || current.z < pad.z)
                    return fail("height changed outside original 15 Hz descent law: expectedQ16=" + std::to_string(expected)
                        + " actualQ16=" + std::to_string(actual) + " " + diagnostic(s, id));
                if (!descended) {
                    descentStart = previous.z; descended = true;
                    if (writeFrames && !capture(s, id, "descent-start")) return false;
                }
                ++descentTicks; ++verticalChecks;
            } else if (descended && boundaryDone && current.airborne && overPad && previous.z > pad.z
                    && current.z == previous.z && boundary.empty()) {
                return fail("landing descent stalled above the pad: " + diagnostic(s, id));
            }
            if (descended && current.airborne && overPad && boundary.empty()) {
                const double expectedFraction = (current.z - pad.z) / (descentStart - pad.z);
                if (current.progress < 0 || std::abs(current.progress - expectedFraction) > 2.0 / 65536.0)
                    return fail("body clearance did not follow the observed descent envelope: " + diagnostic(s, id));
            }
            if (descended && overPad && (current.airborne || !landed) && (boundary.empty() || boundaryDone)) {
                POINT anchor{}; const auto camera = cameraFor(pad);
                if (!renderedAnchor(s, id, camera, anchor)) return false;
                // At 60x30 the native maximum descent contributes <1.5px at
                // 30 Hz. Allow quantization and gradually reduced roof/lift;
                // a final 34px airborne toggle cannot fit this bound.
                if (haveAnchor && std::abs(anchor.y - previousAnchor.y) > 6)
                    return fail("visible body anchor jumped during descent/touchdown: deltaY="
                        + std::to_string(anchor.y - previousAnchor.y) + " " + diagnostic(s, id));
                previousAnchor = anchor; haveAnchor = true;
                if (pixelFlight) {
                    POINT center{};
                    if (!bodyRaster(s, id, camera, center)) return false;
                    if (haveRaster && (std::abs(center.y - previousRaster.y) > 6 || std::abs(center.x - previousRaster.x) > 2))
                        return fail("actual VXL body pixels jumped during landing: delta="
                            + std::to_string(center.x - previousRaster.x) + ',' + std::to_string(center.y - previousRaster.y)
                            + " " + diagnostic(s, id));
                    previousRaster = center; haveRaster = true;
                    if (current.z <= pad.z + 0.12 || descentTicks == 1) {
                        POINT enlarged{}; if (!bodyRaster(s, id, cameraFor(pad, 120), enlarged)) return false;
                        if (enlargedTick == s.tick - 1 && (std::abs(enlarged.y - previousEnlarged.y) > 12
                            || std::abs(enlarged.x - previousEnlarged.x) > 3))
                            return fail("zoomed native body pixels jumped at touchdown: delta="
                                + std::to_string(enlarged.x - previousEnlarged.x) + ','
                                + std::to_string(enlarged.y - previousEnlarged.y) + " " + diagnostic(s, id));
                        previousEnlarged = enlarged; enlargedTick = s.tick;
                    }
                }
                if (trace) trace << s.tick << ',' << std::setprecision(17) << current.x << ',' << current.y << ',' << current.z
                    << ',' << current.airborne << ',' << current.facing << ',' << current.progress << ',' << current.hp << ',' << current.ammo
                    << ',' << current.host << ',' << current.pad << ',' << anchor.y << '\n';
            }
            if (saveLanding && !saved && descended && descentTicks >= 4 && current.z > pad.z + 0.5 && current.airborne) {
                stage = "mid-descent save"; ordinary = checkpoint(s, false); exact = checkpoint(s, true);
                if (!ordinary || !exact) return fail("landing checkpoint restore rejected: " + error);
                if (!compare(s, *ordinary, id) || !compare(s, *exact, id)) return false;
                for (int scale : {60, 120}) {
                    POINT originalBody{}, ordinaryBody{}, exactBody{}; const auto camera = cameraFor(pad, scale);
                    if (!bodyRaster(s, id, camera, originalBody) || !bodyRaster(*ordinary, id, camera, ordinaryBody)
                        || !bodyRaster(*exact, id, camera, exactBody)) return false;
                    if (originalBody.x != ordinaryBody.x || originalBody.y != ordinaryBody.y
                        || originalBody.x != exactBody.x || originalBody.y != exactBody.y)
                        return fail("restoring descent changed original native body pixels at scale=" + std::to_string(scale));
                }
                ++ordinarySaves; ++exactSaves; saved = true;
            }
            if (ordinary && (!compare(s, *ordinary, id) || !compare(s, *exact, id))) return false;
            if (!boundaryDone && descended && descentTicks == 4 && current.airborne) {
                stage = boundary + " during descent"; const auto beforeOrder = pose(s, id);
                if (boundary == "stop") {
                    RuntimeSnapshotActiveStateScope active(s); s.selectedObjectIds = {id};
                    if (!dispatchRuntimeSelectionCommandFromUi(s, RuntimeMultiplayerGameplayCommandType::Stop)
                        || pose(s, id) != beforeOrder) return fail("real Stop snapped landing pose");
                    ++stops;
                    if (!issueMove(s, id, {82, 82})) return false;
                } else if (boundary == "move") {
                    if (!issueMove(s, id, {82, 82})) return false;
                } else if (boundary == "host-loss") {
                    RuntimeSnapshotActiveStateScope active(s);
                    if (!dispatchRuntimeStructureToolFromUi(s, RuntimeSidebarTool::Sell, 1)
                        || pose(s, id).x != beforeOrder.x || pose(s, id).y != beforeOrder.y || pose(s, id).z != beforeOrder.z)
                        return fail("real airport sale teleported its returning fighter");
                    ++lostHosts; landingHost = 3;
                }
                boundaryDone = true; haveAnchor = haveRaster = false;
            }
            if (fired && !current.airborne && now->mission == "Dock") {
                if (!landed) {
                    stage = "touchdown";
                    if (!observedAirborne || !descended || !sawOverPad || !overPad || current.z != pad.z
                        || current.facing != worldObjectFacingFromMapFacing(s.rules.getPoseDir())
                        || current.host != landingHost || current.ammo != 0)
                        return fail("touchdown bypassed overhead/descent/original docking pose: " + diagnostic(s, id));
                    if (boundary == "host-loss" && current.host != 3) return fail("sold host remained landing authority");
                    landed = true; landingTick = s.tick; ++landings;
                    if (writeFrames && !capture(s, id, "touchdown")) return false;
                    int takeoffSounds = 0, landingSounds = 0;
                    for (const auto& request : now->pendingAircraftAuxSounds) {
                        if (request.takingOff) ++takeoffSounds; else ++landingSounds;
                    }
                    // Boundary redirection can be a second approach; the
                    // untouched sortie must request exactly one native Aux2.
                    if (boundary.empty() && (takeoffSounds != (profile->auxSound1.empty() ? 0 : 1)
                        || landingSounds != (profile->auxSound2.empty() ? 0 : 1)))
                        return fail("original auxiliary cue was missing/repeated: takeoff=" + std::to_string(takeoffSounds)
                            + " landing=" + std::to_string(landingSounds));
                    ++auxiliaryChecks;
                }
                if (current.ammo == 1 && !reloaded) { reloaded = true; reloadTick = s.tick; ++reloads; }
                if (current.hp == maxHp && !repaired) { repaired = true; ++repairs; }
                if (current.ammo > 1 || current.hp > maxHp || current.x != pad.x || current.y != pad.y || current.z != pad.z)
                    return fail("docked service changed position or exceeded original caps: " + diagnostic(s, id));
            }
            previous = pose(s, id);
            if (landed && reloaded && repaired && s.tick >= reloadTick + 3) break;
            if ((!landed && s.tick - sortieStartTick > travelLimit)
                || (landed && s.tick - landingTick > serviceLimit))
                return fail("original travel/service deadline exceeded: travelLimit=" + std::to_string(travelLimit)
                    + " serviceLimit=" + std::to_string(serviceLimit) + " healthPerPulse=" + std::to_string(healthPerPulse)
                    + " repairPulses=" + std::to_string(repairPulses) + " " + diagnostic(s, id));
        }
        stage = "completed real sortie";
        if (!fired || attackCount != 1 || !damagedTarget || !landed || !reloaded || !repaired || !boundaryDone
            || (saveLanding && !saved) || descentTicks < 2)
            return fail("incomplete native sortie: fired=" + std::to_string(fired) + " attacks=" + std::to_string(attackCount)
                + " targetDamage=" + std::to_string(damagedTarget) + " landed=" + std::to_string(landed)
                + " reload=" + std::to_string(reloaded) + " repaired=" + std::to_string(repaired)
                + " descentTicks=" + std::to_string(descentTicks) + " saved=" + std::to_string(saved)
                + " " + diagnostic(s, id));
        if (reloadTick - landingTick < reloadInterval - 1 || reloadTick - landingTick > reloadInterval + 1)
            return fail("reload clock did not start at physical touchdown: actual=" + std::to_string(reloadTick - landingTick)
                + " original=" + std::to_string(reloadInterval));
        if (s.rules.getDetailRulesHash() != rules.getDetailRulesHash()) return fail("original rules changed during fixture");
        return true;
    };
    for (const std::string host : {"GAAIRC", "AMRADR"}) for (const std::string plane : {"ORCA", "BEAG"})
        for (int hz : {30, 60}) for (int pad = 0; pad < 4; ++pad) {
            const auto name = plane + '-' + host + '-' + std::to_string(hz) + "-pad" + std::to_string(pad);
            scene(name, [&] { return run(host, plane, hz, pad); });
        }
    scene("ORCA-GAAIRC-30-stop", [&] { return run("GAAIRC", "ORCA", 30, 1, "stop"); });
    scene("BEAG-AMRADR-60-move", [&] { return run("AMRADR", "BEAG", 60, 1, "move"); });
    scene("ORCA-GAAIRC-30-host-loss", [&] { return run("GAAIRC", "ORCA", 30, 1, "host-loss"); });
    scene("BEAG-AMRADR-60-host-loss", [&] { return run("AMRADR", "BEAG", 60, 1, "host-loss"); });
    std::cout << "Aircraft landing: passed=" << scenes << " produced=" << produced << " realShots=" << shots
        << " touchdowns=" << landings << " nativeZChecks=" << verticalChecks << " continuousChecks=" << continuityChecks
        << " saves=" << ordinarySaves << '+' << exactSaves << " pairedTicks=" << pairedTicks
        << " bodyAnchors=" << bodyChecks << " nativeRasters=" << rasterChecks << " captures=" << captures
        << " stop=" << stops << " redirect=" << redirects << " hostLoss=" << lostHosts
        << " reload=" << reloads << " repair=" << repairs << " auxiliary=" << auxiliaryChecks << '\n';
    if (!failures.empty()) {
        error = std::to_string(failures.size()) + " aircraft landing scene(s) failed; first: " + failures.front(); return false;
    }
    if (scenes == 0) { error = "Aircraft landing: requested scene not found"; return false; }
    return true;
}
