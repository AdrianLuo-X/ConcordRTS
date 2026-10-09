// The expected attachment comes from independently painted native body pixels.
// Neither the captured impact offsets nor the renderer's anchor query supplies
// the oracle. Physical projectile endpoints and original weapons stay intact.
bool runRuntimeAirBodyImpactSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->config.playerSlots.clear(); assets->config.computerAiEnabled = false;
    assets->config.gameModeRulesOverride.clear();
    assets->report = buildRuntimeStartupReport(assets->config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    IniFile stock;
    if (!assets->report.success || !stock.loadFromFile(assets->report.rulesPath)
        || !assets->rules.loadFromIni(stock)) {
        error = "Air body impact: original rules/assets unavailable"; return false;
    }
    assets->rulesLoaded = assets->artLoaded = assets->theaterLoaded = true;
    int landTile = -1, waterTile = -1;
    const int waterSet = assets->theater.getGeneralTileSetIndex("WaterSet");
    for (const auto& set : assets->theater.getTileSets())
        if (set.index == waterSet && set.tilesInSet > 0) waterTile = set.startTileIndex;
    for (int tile = 0; tile < static_cast<int>(assets->theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(*assets, cell);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge) { landTile = tile; break; }
    }
    if (landTile < 0 || waterTile < 0) { error = "Air body impact: stock coast TMP unavailable"; return false; }
    struct AirType { std::string id; WorldObjectKind kind; bool aircraft; };
    std::vector<AirType> airTypes;
    std::set<std::string> seen;
    const auto collect = [&](const auto& types, WorldObjectKind kind, bool aircraft) {
        for (const auto& type : types) {
            if (type.strength <= 0 || type.missileSpawn || (!aircraft && !runtimeUnitUsesAirMovement(type))) continue;
            // SCHD is the original Siege Chopper's deployed visual class;
            // SCHP's actual flying body is exercised without inventing a unit.
            if (type.id == "SCHD") {
                std::cout << "Air body impact stock SCHD: deployed visual class, flying SCHP tested\n"; continue;
            }
            if (seen.insert(type.id).second) airTypes.push_back({type.id, kind, aircraft});
        }
    };
    collect(assets->rules.getInfantryTypes(), WorldObjectKind::Infantry, false);
    collect(assets->rules.getVehicleTypes(), WorldObjectKind::Unit, false);
    collect(assets->rules.getAircraftTypes(), WorldObjectKind::Unit, true);
    const auto rulesHash = assets->rules.getDetailRulesHash();
    const char* output = std::getenv("CONCORD_AIR_BODY_IMPACT_OUTPUT");
    const char* selectedCase = std::getenv("CONCORD_AIR_BODY_IMPACT_CASE");
    constexpr int width = 512, height = 384;
    constexpr std::uint32_t background = 0x001d2935, secondBackground = 0x00d1bd8b;
    const RECT viewport{0, 0, width, height};
    auto nativeCache = std::make_unique<RuntimeMapRenderCache>();
    int scenes = 0, attemptedScenes = 0, hits = 0, killHits = 0, scaleChecks = 0, pauseChecks = 0, detachedChecks = 0;
    int saves = 0, snapshots = 0, hitSnapshots = 0, pairedTicks = 0, captures = 0, movingHits = 0;
    int restoredRouteCommands = 0, missileScenes = 0, restoredBodyScaleChecks = 0, nativeFlyOffsetDifferences = 0;
    int recordChecks = 0, snapshotPixelChecks = 0, nonlinearRoofChecks = 0;
    int corpseDetonations = 0, groundDetonations = 0, lostLockDetonations = 0, descendantDebris = 0;
    std::map<std::pair<std::string, int>, std::string> visibleTurnWitnesses;
    int visibleTurnWitnessScenes = 0;
    bool checkedRecord = false;
    std::string label, stage; std::vector<std::string> failures;
    const auto fail = [&](const std::string& why) {
        error = "Air body impact " + label + " [" + stage + "]: " + why; return false;
    };
    const auto contextFor = [&](const RuntimeWindowState& s) {
        RuntimeMapRenderContext c; c.rules = &s.rules; c.art = &s.art; c.theater = &s.theater;
        c.assetRootPath = runtimeAssetRootPath(s.config); c.palettePath = runtimePalettePath(s.config, s.report.map);
        c.structurePalettePath = runtimeStructurePalettePath(s.config, s.report.map);
        c.ownerRemapColors = &s.ownerRemapColors; c.animationTick = s.tick; c.fixedTickHz = s.config.fixedTickHz;
        c.specialMechanismSnapshots = &s.specialMechanismRenderSnapshots;
        c.surfaceWidth = width; c.surfaceHeight = height; c.shadowsEnabled = false;
        c.useTileTextures = false; c.vision = nullptr; c.lightingTint = nullptr;
        return c;
    };
    const auto make = [&](const std::string& sourceType, const AirType& air, int hz, int rank, bool roof) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        seedRuntimeLoadScratch(s, *assets, false); s.config = assets->config; s.config.fixedTickHz = hz;
        s.config.noCooldown = s.config.shortGame = s.config.crates = s.config.superweapons = s.config.multiplayerActive = false;
        s.config.gameMode = "Battle"; s.config.playerCountry = "Americans"; s.config.skirmishLoadSavePath.clear();
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y = 48; y <= 112; ++y) for (int x = 48; x <= 112; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = x <= 78 ? waterTile : landTile; cell.subTileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
        }
        const RuntimeCellPoint sourceCell{sourceType == "AEGIS" ? 75 : 83, 75};
        const RuntimeCellPoint targetCell{sourceCell.x + 4, sourceCell.y + 2};
        const auto add = [&](int id, const std::string& type, WorldObjectKind kind, RuntimeCellPoint cell,
            const std::string& owner, bool aircraft = false) {
            WorldObjectInstance o; o.instanceId = id; o.mapId = "AirBody" + std::to_string(id);
            o.kind = kind; o.type = type; o.owner = owner; o.x = cell.x; o.y = cell.y;
            o.cell = runtimeCellKey(o.x, o.y); o.alive = o.poweredOperational = true;
            o.authoredAircraft = aircraft; o.health = runtimeMaxHealthForObject(s, o);
            o.mission = "Sleep"; o.facing = o.turretFacing = 64;
            initializeRuntimeObjectAmmo(o, runtimeAmmoRulesForObject(s.rules, o));
            applyRuntimeInitialObjectFlightState(s.rules, o);
            runtimeInitializeModExtensions(o, s.rules, hz); s.world.accessObjects().push_back(std::move(o));
        };
        add(1, sourceType, WorldObjectKind::Unit, sourceCell, "Americans");
        findMutableObjectById(s, 1)->veterancy = rank;
        add(2, air.id, air.kind, targetCell, "Russians", air.aircraft);
        add(998, "NAPOWR", WorldObjectKind::Structure, {105, 105}, "Russians");
        // Real airport support keeps airport-bound map aircraft legal. A roof
        // scene places the aircraft over this original building's footprint.
        if (air.id == "ORCA" || air.id == "BEAG") {
            add(997, "GAAIRC", WorldObjectKind::Structure,
                roof ? RuntimeCellPoint{targetCell.x - 1, targetCell.y - 1} : RuntimeCellPoint{96, 94}, "Russians");
            add(996, "GAPOWR", WorldObjectKind::Structure, {101, 92}, "Russians");
        }
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s); initializeRuntimeOfflineGameplayRouting(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s); }
        return storage;
    };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
    };
    const auto checkpoint = [&](RuntimeWindowState& s, bool exact) {
        std::ostringstream baseText, exactText, canonical; IniFile baseIni, exactIni;
        if (!writeRuntimeStateToStream(s, baseText, error) || !baseIni.loadFromString(baseText.str())) return std::unique_ptr<RuntimeWindowState>{};
        if (exact && (!writeRuntimeStateToStream(s, exactText, error)
            || !writeRuntimeMultiplayerSnapshotExtension(s, exactText, error)
            || !exactIni.loadFromString(exactText.str()) || !writeRuntimeMultiplayerSnapshotCanonicalIni(exactIni, canonical, error)
            || !exactIni.loadFromString(canonical.str()))) return std::unique_ptr<RuntimeWindowState>{};
        auto restored = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored, s, false);
        // Canonical extension deliberately omits process-local AV authority.
        if (!materializeRuntimeLoadStateFromIni(*restored, baseIni, error)
            || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*restored, exactIni, error))) return std::unique_ptr<RuntimeWindowState>{};
        return restored;
    };
    const auto boundsFor = [&](const std::vector<std::uint32_t>& pixels,
        const std::vector<std::uint32_t>& blank, RECT& bounds) {
        bounds = {width, height, 0, 0}; int count = 0;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const auto p = y * width + x;
            if ((pixels[p] & 0xffffffU) == (blank[p] & 0xffffffU)) continue;
            bounds.left = std::min<LONG>(bounds.left, x); bounds.right = std::max<LONG>(bounds.right, x + 1);
            bounds.top = std::min<LONG>(bounds.top, y); bounds.bottom = std::max<LONG>(bounds.bottom, y + 1); ++count;
        }
        return count;
    };
    // Only body-native rendering contributes to this oracle. Draw both the
    // structure background and target on the same world/camera, then subtract.
    const auto bodyCenter = [&](const RuntimeWindowState& s, const WorldObjectInstance& pose,
        const RuntimeMapCamera& camera, POINT& center, std::vector<std::uint32_t>* bodyPixels = nullptr,
        const std::vector<SpecialMechanismRenderSnapshot>* savedSnapshots = nullptr, int savedAnimationTick = -1) {
        auto c = contextFor(s); GameWorld world = s.world;
        if (savedAnimationTick >= 0) c.animationTick = savedAnimationTick;
        world.accessTerrainCells().clear(); world.accessTerrainObjects().clear(); world.accessSmudges().clear();
        auto& objects = world.accessObjects();
        objects.erase(std::remove_if(objects.begin(), objects.end(), [](const auto& o) { return o.kind != WorldObjectKind::Structure; }), objects.end());
        std::vector<std::uint32_t> blank(width * height, background), actual(width * height, background);
        std::vector<SpecialMechanismRenderSnapshot> bodySnapshots;
        for (const auto& snapshot : savedSnapshots ? *savedSnapshots : s.specialMechanismRenderSnapshots)
            if (snapshot.kind == "spawnedMissile" && snapshot.objectId == pose.instanceId) {
                auto body = snapshot;
                // A render-only layer isolation: Preparing paints the exact
                // same pitched native body while omitting V3TRAIL exhaust.
                // Flight authority, image, XYZ, heading/pitch and offsets are
                // copied unchanged. No simulation state is modified.
                body.phase = "Preparing"; bodySnapshots.push_back(std::move(body));
            }
        const std::vector<SpecialMechanismRenderSnapshot> emptySnapshots;
        c.specialMechanismSnapshots = &emptySnapshots;
        c.surfacePixels = blank.data(); auto fixedCamera = camera;
        const auto empty = drawRuntimeMap(nullptr, viewport, world, c, fixedCamera, *nativeCache);
        objects.push_back(pose); c.specialMechanismSnapshots = &bodySnapshots;
        c.surfacePixels = actual.data(); fixedCamera = camera;
        const auto drawn = drawRuntimeMap(nullptr, viewport, world, c, fixedCamera, *nativeCache);
        RECT bounds{}; const int count = boundsFor(actual, blank, bounds);
        if (!empty.rendered || !drawn.rendered || count < 8 || drawn.parachuteSprites
            || bounds.left <= 0 || bounds.top <= 0 || bounds.right >= width || bounds.bottom >= height)
            return fail("native body raster unavailable/clipped: " + pose.type + " pixels=" + std::to_string(count));
        center = {(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
        if (bodyPixels) *bodyPixels = std::move(actual);
        return true;
    };
    // Native SHP pixels/offsets locate the global explosion canvas independently
    // of any object-attachment helper. Effects themselves do not zoom their SHP.
    const auto impactCenter = [&](const RuntimeWindowState& s, const RuntimeImpactRenderActor& actor,
        const RuntimeMapCamera& camera, POINT& center, std::vector<std::uint32_t>& pixels,
        bool detached, RuntimeMapRenderCache& cache) {
        auto c = contextFor(s); c.effectOnlyProbe = true; c.specialMechanismSnapshots = nullptr;
        std::vector<RuntimeImpactRenderActor> actors{actor}; c.impactActors = &actors;
        GameWorld world = s.world;
        if (detached) world.accessObjects().clear();
        std::vector<std::uint32_t> alternate(width * height, secondBackground), blank(width * height, background);
        pixels.assign(width * height, background); c.surfacePixels = pixels.data(); auto fixedCamera = camera;
        const auto drawn = drawRuntimeMap(nullptr, viewport, world, c, fixedCamera, cache);
        c.surfacePixels = alternate.data(); fixedCamera = camera;
        const auto second = drawRuntimeMap(nullptr, viewport, world, c, fixedCamera, cache);
        if (!drawn.rendered || !second.rendered || drawn.impactActorSnapshots != 1 || drawn.impactAnimMissing)
            return fail("real impact native resource could not render");
        // A palette color can equal one background; two backgrounds identify
        // every drawn nontransparent pixel, including translucent native art.
        std::vector<std::uint32_t> mask(width * height, 0), zero(width * height, 0);
        for (std::size_t p = 0; p < mask.size(); ++p)
            mask[p] = (pixels[p] & 0xffffffU) != background || (alternate[p] & 0xffffffU) != secondBackground;
        RECT actual{}; const int visible = boundsFor(mask, zero, actual);
        if (visible == 0) return false; // Some original animation frames are empty.
        RuntimeShpFrameImage first; std::string resolved, path, decodeError; int frameCount = 0;
        if (!loadRuntimeAuxiliaryShpFrameImage(c, cache, actor.animId, 0, first, resolved, path, decodeError, false, &frameCount)
            || frameCount <= 0 || path.empty()) return fail("original explosion SHP decode: " + decodeError);
        const auto* art = s.art.findArtType(actor.animId);
        const int delay = std::max(1, runtimeRuleFramesToTicks(runtimeRuleAnimationDelayFrames(art ? art->rate : 0), s.config.fixedTickHz));
        const int frame = actor.frameIndex >= 0 ? actor.frameIndex : std::max(0, actor.totalTicks - actor.remainingTicks) / delay % frameCount;
        RuntimeShpFrameImage image;
        if (!loadRuntimeAuxiliaryShpFrameImage(c, cache, actor.animId, frame, image, resolved, path, decodeError))
            return fail("original explosion frame decode: " + decodeError);
        RECT raw{image.width, image.height, 0, 0}; int rawCount = 0;
        for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x) {
            if (image.pixels[y * image.width + x] == 0) continue;
            raw.left = std::min<LONG>(raw.left, x); raw.right = std::max<LONG>(raw.right, x + 1);
            raw.top = std::min<LONG>(raw.top, y); raw.bottom = std::max<LONG>(raw.bottom, y + 1); ++rawCount;
        }
        if (rawCount != visible || actual.right - actual.left != raw.right - raw.left
            || actual.bottom - actual.top != raw.bottom - raw.top)
            return fail("actual explosion opacity footprint differs from original native SHP frame");
        center = {actual.left - image.offsetX - raw.left + image.globalWidth / 2,
            actual.top - image.offsetY - raw.top + image.globalHeight / 2};
        return true;
    };
    const auto facade = [&](RuntimeWindowState& s, const std::string& owner) {
        s.economy.owner = owner; s.config.playerCountry = owner; s.humanHouseAuthorities.clear();
        RuntimeHumanHouseAuthority authority; authority.owner = owner;
        authority.canonicalOwner = runtimeCanonicalHumanHouseOwner(owner); authority.country = owner; authority.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(authority));
    };
    const auto isCore = [](const std::string& type) {
        return type == "JUMPJET" || type == "SHAD" || type == "ORCA" || type == "ZEP" || type == "DISK";
    };
    const auto describeObject = [](const WorldObjectInstance* object) {
        if (!object) return std::string("absent");
        std::ostringstream out; out << std::setprecision(17) << object->type
            << " alive=" << object->alive << " hp=" << object->health << " air=" << object->airborne
            << " cell=" << object->x << ',' << object->y << " level=" << object->level
            << " xyz=" << runtimeObjectRenderX(*object) << ',' << runtimeObjectRenderY(*object)
            << ',' << runtimeDrawLevelForObject(*object) << " facing=" << object->facing
            << " render=" << object->hasRenderPosition << " renderMove=" << object->renderMoveActive
            << " mission=" << object->mission;
        return out.str();
    };
    const auto describeShot = [](const RuntimeProjectileEffect& shot) {
        std::ostringstream out; out << std::setprecision(17) << "actor=" << shot.actorId
            << " parent=" << shot.parentActorId << " generation=" << shot.generation
            << " target=" << shot.targetObjectId << " weapon=" << shot.weaponId
            << " projectile=" << shot.projectileId << " warhead=" << shot.warheadId
            << " intent=" << shot.renderIntent << " xyz=" << shot.currentX << ',' << shot.currentY << ',' << shot.currentZ
            << " endpoint=" << shot.targetX << ',' << shot.targetY << ',' << shot.targetZ
            << " remaining=" << shot.remainingTicks << " damage=" << shot.damage;
        return out.str();
    };
    const auto run = [&](const std::string& sourceType, const AirType& air, int hz, int rank,
        bool moving, bool untilKill, bool roof, const char* launcherType = nullptr) {
        const bool missileScene = launcherType != nullptr;
        label = sourceType + '-' + air.id + '-' + std::to_string(hz) + "-rank" + std::to_string(rank)
            + (missileScene ? "-native-missile-flight" : (moving ? "-moving" : "-still"))
            + (untilKill ? "-kill" : "") + (roof ? "-roof" : "");
        if (selectedCase && *selectedCase && label != selectedCase) return;
        ++attemptedScenes;
        const bool passed = [&]() {
            const AirType groundTarget{"CAEAST01", WorldObjectKind::Structure, false};
            auto storage = make(sourceType, missileScene ? groundTarget : air, hz, rank, roof); auto& s = *storage;
            RuntimeSnapshotActiveStateScope active(s);
            int targetId = 2;
            std::set<int> originalMissileVolley;
            bool launcherStopped = !missileScene;
            if (missileScene) {
                stage = "real enemy launcher Primary / native release";
                auto* destination = findMutableObjectById(s, 2);
                destination->owner = "Americans"; destination->x = 95; destination->y = 78;
                destination->cell = runtimeCellKey(destination->x, destination->y);
                WorldObjectInstance parent; parent.instanceId = 3; parent.mapId = "AirBodyMissileLauncher";
                parent.type = launcherType; parent.kind = WorldObjectKind::Unit; parent.owner = "Russians";
                parent.x = parent.type == "V3" ? (sourceType == "FV" ? 88 : 83) : 76;
                parent.y = 78; parent.cell = runtimeCellKey(parent.x, parent.y);
                parent.alive = parent.poweredOperational = true; parent.health = runtimeMaxHealthForObject(s, parent);
                parent.mission = "Sleep"; parent.facing = parent.turretFacing = runtimeFacingFromDelta(1, 0);
                initializeRuntimeObjectAmmo(parent, runtimeAmmoRulesForObject(s.rules, parent));
                runtimeInitializeModExtensions(parent, s.rules, hz); s.world.accessObjects().push_back(std::move(parent));
                step(s); // Real carrier registry/docked children, before fire.
                facade(s, "Russians"); s.selectedObjectIds = {3}; destination = findMutableObjectById(s, 2);
                if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                    runtimeCellPointForObject(*destination), destination) || s.lastCommand.acceptedCount != 1)
                    return fail("real launcher UI attack rejected");
                targetId = -1;
                for (int n = 0; n < hz * 20 && targetId < 0; ++n) {
                    step(s);
                    for (const auto& [id, missile] : s.specialMechanisms.spawnedMissiles()) {
                        if (missile.parentObjectId != 3) continue;
                        originalMissileVolley.insert(id);
                        const auto* entity = findObjectById(s, id);
                        if (targetId < 0 && missile.typeId == air.id && missile.released && entity && entity->alive && entity->airborne
                            && std::hypot(missile.x - missile.originX, missile.y - missile.originY) >= 1.0) {
                            targetId = id;
                        }
                    }
                }
                if (targetId < 0 || originalMissileVolley.empty()) return fail("original launcher did not produce a real flying missile");
            }
            const auto* target = findObjectById(s, targetId); const auto* source = findObjectById(s, 1);
            if (!target || !source || !target->airborne || target->health != runtimeMaxHealthForObject(s, *target))
                return fail("original full-health airborne map actor startup");
            const int initialHealth = target->health;
            const auto profile = runtimeWeaponProfileForObject(s, *source, target);
            const std::string weaponId = sourceType == "AEGIS" ? (rank == 2 ? "MedusaE" : "Medusa") : (rank == 2 ? "HoverMissileE" : "HoverMissile");
            if (!profile.weapon || !profile.projectile || !profile.warhead || profile.weaponId != weaponId
                || profile.weapon->rof != std::stoi(stock.getValue(weaponId, "ROF", "-1"))
                || profile.weapon->burst != std::stoi(stock.getValue(weaponId, "Burst", "0")))
                return fail("original AA/normal/elite/ROF/Burst profile changed");
            const int burst = std::max(1, profile.weapon->burst);
            const RuntimeCellPoint sourceAnchor = runtimeCellPointForObject(*source);
            // Keep each destination in the original AA range. Reissue real
            // Move before arrival, so a native landable aircraft does not
            // complete its short trip and land while a missile is still flying.
            const std::array<RuntimeCellPoint, 4> waypoints{{
                {sourceAnchor.x + 4, sourceAnchor.y - 5}, {sourceAnchor.x + 4, sourceAnchor.y + 5},
                {sourceAnchor.x - 4, sourceAnchor.y + 5}, {sourceAnchor.x - 4, sourceAnchor.y - 5}}};
            std::size_t waypoint = 0;
            const int beforeMovementFacing = target->facing;
            stage = "actual enemy Move / player Primary";
            if (moving) {
                facade(s, "Russians"); s.selectedObjectIds = {targetId};
                if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Move,
                    waypoints[waypoint]) || s.lastCommand.acceptedCount != 1) return fail("enemy facade real Move rejected");
                // Let the real path/flight authorities acquire a segment first.
                for (int n = 0; n < hz / 3; ++n) step(s);
            }
            facade(s, "Americans"); s.selectedObjectIds = {1}; target = findObjectById(s, targetId);
            if (!target || !dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                runtimeCellPointForObject(*target), target) || s.lastCommand.acceptedCount != 1) return fail("real player AA attack rejected");
            auto prediction = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*prediction, s, false);
            initializeRuntimeNavigation(*prediction);
            std::unique_ptr<RuntimeWindowState> ordinary, exact, exactHit;
            const bool flightSave = rank == 0 && !untilKill && !roof && (air.id == "JUMPJET" || air.id == "ORCA" || missileScene);
            const bool hitSave = rank == 0 && !untilKill && !roof && (air.id == "JUMPJET" || air.id == "ZEP" || missileScene);
            const bool auditCold = hz == 30 && rank == 0 && sourceType == "AEGIS" && (isCore(air.id) || missileScene);
            const bool visual = output && *output && hz == 30 && rank == 0 && sourceType == "AEGIS"
                && (air.id == "JUMPJET" || air.id == "SHAD" || air.id == "ORCA" || missileScene) && !untilKill && !roof;
            struct HitOracle {
                WorldObjectInstance body; int tick; std::map<int, POINT> centers; bool visible = false;
                std::vector<SpecialMechanismRenderSnapshot> mechanismSnapshots;
            };
            std::map<int, HitOracle> oracles; std::map<int, bool> airborneTerminals;
            std::set<int> fired, expired, seenDebris;
            std::set<int> firstChecked; bool stopped = false, damaged = false, drained = false;
            bool killed = false;
            bool savedFlight = false, savedHit = false, moved = false, turned = false, captured = false;
            bool postHitMove = false, turnedAfterHit = false, turnedBeforeHit = false; int hitFacing = -1;
            int visibleTurnTick = -1, visibleTurnActor = -1, visibleTurnRemaining = 0, visibleTurnFacing = -1;
            const double initialX = runtimeObjectRenderX(*target), initialY = runtimeObjectRenderY(*target);
            const int initialFacing = target->facing;
            const auto describeFlight = [&](const RuntimeWindowState& state) {
                std::ostringstream out; out << "jumpjets={" << serializeRuntimeActiveJumpjetFlights(state) << '}';
                const auto behavior = state.behaviors.find(targetId);
                if (behavior != state.behaviors.end()) {
                    const auto& order = behavior->second;
                    out << " kind=" << static_cast<int>(order.kind) << " state=" << static_cast<int>(order.state)
                        << " cooldown=" << order.moveCooldownTicks << " renderTicks=" << order.renderMoveTicksRemaining
                        << '/' << order.renderMoveTicksTotal << " from=" << order.renderFrom.x << ',' << order.renderFrom.y
                        << " to=" << order.renderTo.x << ',' << order.renderTo.y << " path=" << order.path.size()
                        << " pending=" << order.pathRequestPending;
                }
                if (const auto* object = findRuntimeObjectByIdIncludingInactive(state, targetId))
                    out << " groundLevel=" << terrainLevelAtRuntimeCell(state, object->x, object->y);
                return out.str();
            };
            const auto effectKey = [](const RuntimeImpactEffect& effect) {
                return std::to_string(effect.projectileActorId) + ':' + std::to_string(effect.targetObjectId)
                    + ':' + effect.renderIntent + ':' + std::to_string(effect.death);
            };
            const auto isActualAaHit = [](const RuntimeImpactEffect& effect) {
                return effect.attackerObjectId == 1 && effect.projectileActorId >= 0
                    && (effect.renderIntent == "warheadImpact" || effect.renderIntent == "deathImpact");
            };
            struct RestoredBodyProbe {
                std::unique_ptr<RuntimeWindowState> prediction;
                std::set<std::string> checkpointEffects, requiredOwnChecks, reportedDifferences;
                std::map<std::string, HitOracle> bornOracles;
                std::map<std::string, RuntimeImpactEffect> birthEffects;
                std::map<std::string, std::set<int>> visibleScales;
                std::set<std::string> coldChecked;
                std::string name;
            };
            RestoredBodyProbe ordinaryProbe, exactProbe, exactHitProbe;
            ordinaryProbe.name = "ordinary"; exactProbe.name = "exact-flight"; exactHitProbe.name = "exact-hit";
            const auto setupProbe = [&](RuntimeWindowState& restored, RestoredBodyProbe& probe) {
                probe.prediction = std::make_unique<RuntimeWindowState>();
                seedRuntimeLoadScratch(*probe.prediction, restored, false); initializeRuntimeNavigation(*probe.prediction);
                // Source-only old generic FX are intentionally absent from
                // ordinary v14. They are still checkpoint-era identities,
                // never future hits whose presence may be silently skipped.
                for (const auto& effect : s.impactEffects)
                    if (isActualAaHit(effect)) probe.checkpointEffects.insert(effectKey(effect));
                for (const auto& effect : restored.impactEffects)
                    if (isActualAaHit(effect)) probe.checkpointEffects.insert(effectKey(effect));
            };
            const auto predictBody = [&](const RuntimeWindowState& live, RuntimeWindowState& predicted) {
                predicted.tick = live.tick + 1; predicted.world.accessObjects() = live.world.getObjects();
                predicted.behaviors = live.behaviors; predicted.jumpjetFlights = live.jumpjetFlights;
                predicted.aircraftFlights = live.aircraftFlights;
                if (missileScene) {
                    predicted.specialMechanisms = live.specialMechanisms;
                    predicted.specialMechanismsInitialized = live.specialMechanismsInitialized;
                }
                RuntimeSnapshotActiveStateScope predicting(predicted);
                advanceRuntimeJumpjetFlights(predicted); reconcileRuntimeAircraftFlights(predicted);
                // AA Effects precede the real special-flight phase. Refresh
                // its current pitched body; never advance a missile early.
                if (missileScene) refreshRuntimeSpecialMechanismRenderSnapshots(predicted);
            };
            const auto describeNativeFly = [&](const RuntimeWindowState& state, const RuntimeWindowState* predicted) {
                const auto* object = findRuntimeObjectByIdIncludingInactive(predicted ? *predicted : state, targetId);
                if (!object) return std::string("absent");
                const int frames = runtimeArtSequenceFrameCount(runtimeArtTypeForObject(state, *object), "Fly");
                std::ostringstream out; out << std::setprecision(17) << "movementAnimationDistance=" << object->movementAnimationDistance
                    << " FlyFrames=" << frames << " FlyPhase=" << (frames > 0 ? worldObjectMovementAnimationPhase(*object, frames) : -1)
                    << ' ' << describeObject(object); return out.str();
            };
            const auto compare = [&](RuntimeWindowState& restored, bool full, RestoredBodyProbe& probe) {
                const auto* actual = findRuntimeObjectByIdIncludingInactive(s, targetId);
                const auto* copy = findRuntimeObjectByIdIncludingInactive(restored, targetId);
                if ((actual == nullptr) != (copy == nullptr) || (actual && copy && (actual->alive != copy->alive || actual->health != copy->health
                    || runtimeObjectRenderX(*actual) != runtimeObjectRenderX(*copy)
                    || runtimeObjectRenderY(*actual) != runtimeObjectRenderY(*copy)
                    || runtimeDrawLevelForObject(*actual) != runtimeDrawLevelForObject(*copy)
                    || actual->facing != copy->facing)) || s.projectiles.size() != restored.projectiles.size())
                    return fail(std::string(full ? "exact" : "ordinary") + " saved flight/real target pose/damage diverged at tick "
                        + std::to_string(s.tick) + " actual={" + describeObject(actual) + "} restored={" + describeObject(copy)
                        + "} projectiles=" + std::to_string(s.projectiles.size()) + '/' + std::to_string(restored.projectiles.size())
                        + " actualFlight={" + describeFlight(s) + "} restoredFlight={" + describeFlight(restored) + "}");
                for (std::size_t i = 0; i < s.projectiles.size(); ++i)
                    if (serializeRuntimeProjectileEffect(s.projectiles[i]) != serializeRuntimeProjectileEffect(restored.projectiles[i]))
                        return fail("saved original AA guidance/endpoint diverged");
                const auto compareEffect = [&](const RuntimeImpactEffect& actualEffect, const RuntimeImpactEffect& restoredEffect) {
                        const auto originalRecord = runtimeMultiplayerSnapshotImpactRecord(actualEffect);
                        const auto restoredRecord = runtimeMultiplayerSnapshotImpactRecord(restoredEffect);
                        if (originalRecord == restoredRecord) return true;
                        const auto originalFields = splitRuntimeSaveFields(originalRecord), restoredFields = splitRuntimeSaveFields(restoredRecord);
                        std::vector<std::size_t> differences;
                        if (originalFields.size() == restoredFields.size())
                            for (std::size_t field = 0; field < originalFields.size(); ++field)
                                if (originalFields[field] != restoredFields[field]) differences.push_back(field);
                        const auto key = effectKey(actualEffect);
                        std::ostringstream diagnostic; diagnostic << probe.name << " impact49 mismatch tick=" << s.tick
                            << " key=" << key << " saved=" << probe.checkpointEffects.contains(key)
                            << " first=" << (differences.empty() ? -1 : static_cast<int>(differences.front())) << " all=";
                        for (const auto field : differences) diagnostic << field << '(' << originalFields[field] << "=>" << restoredFields[field] << ") ";
                        diagnostic << " originalRecord={" << originalRecord << "} restoredRecord={" << restoredRecord
                            << "} sourceNative={" << describeNativeFly(s, prediction.get())
                            << "} restoredNative={" << describeNativeFly(restored, probe.prediction.get()) << '}';
                        const bool ownNativeOffsets = !probe.checkpointEffects.contains(key)
                            && originalFields.size() == 49 && restoredFields.size() == 49 && !differences.empty()
                            && isActualAaHit(actualEffect) && isActualAaHit(restoredEffect)
                            && actualEffect.bodyCenterAnchored && restoredEffect.bodyCenterAnchored
                            && std::all_of(differences.begin(), differences.end(), [](std::size_t field) { return field >= 43 && field <= 46; });
                        if (!ownNativeOffsets) return fail(diagnostic.str());
                        // Legacy snapshots reset airborne native Fly distance.
                        // Only a future hit may capture its own four offsets.
                        // Acceptance still requires an independent body raster
                        // from THIS replica below, at every camera/later frame.
                        probe.requiredOwnChecks.insert(key);
                        if (probe.reportedDifferences.insert(key).second) {
                            ++nativeFlyOffsetDifferences; std::cout << "Air body impact native Fly AV: " << diagnostic.str() << '\n';
                        }
                        return true;
                };
                if (full) {
                    if (s.impactEffects.size() != restored.impactEffects.size()) return fail("exact restore lost native impact");
                    for (std::size_t i = 0; i < s.impactEffects.size(); ++i)
                        if (!compareEffect(s.impactEffects[i], restored.impactEffects[i])) return false;
                } else {
                    // Ordinary v14 omits old generic effects. Actual AA hits
                    // have unique actor identities, including corpse/ground/
                    // lost-lock detonations. Trailer samples have actor=-1
                    // and share keys; they cannot be matched as hit records.
                    // Exact snapshots above still compare every FX by index.
                    for (const auto& restoredEffect : restored.impactEffects) {
                        if (!isActualAaHit(restoredEffect)) continue;
                        const auto key = effectKey(restoredEffect);
                        const auto actualEffect = std::find_if(s.impactEffects.begin(), s.impactEffects.end(),
                            [&](const auto& candidate) { return effectKey(candidate) == key; });
                        if (actualEffect == s.impactEffects.end() || !compareEffect(*actualEffect, restoredEffect))
                            return fail("ordinary existing/future impact49 mismatch: key=" + key + ' ' + error);
                    }
                    for (const auto& actualEffect : s.impactEffects) {
                        if (!isActualAaHit(actualEffect)) continue;
                        const auto key = effectKey(actualEffect);
                        if (probe.checkpointEffects.contains(key)) continue;
                        const auto restoredEffect = std::find_if(restored.impactEffects.begin(), restored.impactEffects.end(),
                            [&](const auto& candidate) { return effectKey(candidate) == key; });
                        if (restoredEffect == restored.impactEffects.end())
                            return fail("ordinary lost a future impact49: key=" + key
                                + " sourceRecord={" + runtimeMultiplayerSnapshotImpactRecord(actualEffect) + '}');
                        if (!compareEffect(actualEffect, *restoredEffect)) return false;
                    }
                }
                ++pairedTicks; return true;
            };
            const auto checkOwnBody = [&](RuntimeWindowState& restored, RestoredBodyProbe& probe) {
                if (!probe.prediction) return fail(probe.name + " independent pre-effects predictor missing");
                for (const auto& effect : restored.impactEffects) {
                    if (effect.attackerObjectId != 1 || !fired.contains(effect.projectileActorId)
                        || (effect.renderIntent != "warheadImpact" && effect.renderIntent != "deathImpact")) continue;
                    const auto key = effectKey(effect);
                    if (probe.checkpointEffects.contains(key)) continue;
                    const auto classified = airborneTerminals.find(effect.projectileActorId);
                    if (classified == airborneTerminals.end()) return fail(probe.name + " future hit lacks independent native terminal classification");
                    if (effect.bodyCenterAnchored != classified->second)
                        return fail(probe.name + " future hit body marker disagrees with native live/corpse/ground terminal");
                    if (!classified->second) {
                        if (probe.requiredOwnChecks.contains(key)) return fail(probe.name + " non-airborne hit changed center offsets");
                        continue;
                    }
                    if (!probe.bornOracles.contains(key)) {
                        const auto* body = findRuntimeObjectByIdIncludingInactive(*probe.prediction, targetId);
                        if (!body || !body->alive || !body->airborne || body->health <= 0)
                            return fail(probe.name + " future airborne hit lacks its own live pre-damage native body");
                        if (std::abs(effect.worldX - runtimeObjectRenderX(*body)) > 1e-8
                            || std::abs(effect.worldY - runtimeObjectRenderY(*body)) > 1e-8
                            || effect.worldLevel != runtimeProjectileTargetPosition(*probe.prediction, *body).level)
                            return fail(probe.name + " future airborne physical XYZ differs from its own pre-effects pose");
                        HitOracle oracle; oracle.body = *body; oracle.tick = restored.tick;
                        if (missileScene) oracle.mechanismSnapshots = probe.prediction->specialMechanismRenderSnapshots;
                        probe.bornOracles.emplace(key, std::move(oracle)); probe.birthEffects.emplace(key, effect);
                    }
                    auto& oracle = probe.bornOracles.at(key); const auto& birth = probe.birthEffects.at(key);
                    if (effect.screenOffsetX != birth.screenOffsetX || effect.screenOffsetY != birth.screenOffsetY
                        || effect.nativeFixedX != birth.nativeFixedX || effect.nativeFixedY != birth.nativeFixedY
                        || effect.bodyCenterAnchored != birth.bodyCenterAnchored
                        || effect.bodyCenterPresentation != birth.bodyCenterPresentation
                        || effect.bodyCenterSpriteOffsetY != birth.bodyCenterSpriteOffsetY
                        || effect.worldX != birth.worldX || effect.worldY != birth.worldY || effect.worldLevel != birth.worldLevel)
                        return fail(probe.name + " future hit changed its frozen birth center/profile/physical XYZ");
                    const auto actor = makeRuntimeImpactRenderActor(effect);
                    const bool fourScalesPending = probe.visibleScales[key].size() < 4;
                    for (int tileWidth : fourScalesPending ? std::vector<int>{48, 60, 72, 120} : std::vector<int>{60}) {
                        RuntimeMapCamera camera; camera.initialized = true; camera.tileWidth = tileWidth; camera.tileHeight = tileWidth / 2;
                        const auto anchor = runtimeProjectCellXY(effect.worldX, effect.worldY, effect.worldLevel, camera);
                        camera.worldOffsetX = anchor.x - width / 2; camera.worldOffsetY = anchor.y - height / 2;
                        if (!oracle.centers.contains(tileWidth)) {
                            POINT expected{};
                            if (!bodyCenter(restored, oracle.body, camera, expected, nullptr, &oracle.mechanismSnapshots, oracle.tick)) return false;
                            oracle.centers.emplace(tileWidth, expected);
                        }
                        POINT observed{}; std::vector<std::uint32_t> pixels;
                        if (!impactCenter(restored, actor, camera, observed, pixels, false, *nativeCache)) {
                            if (!error.empty()) return false;
                            continue; // Blank native frames must become visible later.
                        }
                        const auto expected = oracle.centers.at(tileWidth);
                        if (std::abs(observed.x - expected.x) > 2 || std::abs(observed.y - expected.y) > 2)
                            return fail(probe.name + " future explosion does not match its own native body frame: key=" + key
                                + " scale=" + std::to_string(tileWidth) + " expected=" + std::to_string(expected.x) + ',' + std::to_string(expected.y)
                                + " observed=" + std::to_string(observed.x) + ',' + std::to_string(observed.y)
                                + " ownNative={" + describeNativeFly(restored, probe.prediction.get()) + '}');
                        oracle.visible = true; probe.visibleScales[key].insert(tileWidth); ++restoredBodyScaleChecks;
                        if (tileWidth == 60 && probe.coldChecked.insert(key).second) {
                            auto cold = std::make_unique<RuntimeMapRenderCache>(); POINT detached{}; std::vector<std::uint32_t> coldPixels;
                            if (!impactCenter(restored, actor, camera, detached, coldPixels, true, *cold)
                                || coldPixels != pixels || detached.x != observed.x || detached.y != observed.y)
                                return fail(probe.name + " future hit moved after target removal/cold cache");
                            ++detachedChecks;
                        }
                    }
                }
                for (const auto& key : probe.requiredOwnChecks)
                    if (!probe.bornOracles.contains(key)) return fail(probe.name + " future four-offset difference lacks its own body oracle: " + key);
                return true;
            };
            const auto checkSavedPixels = [&](RuntimeWindowState& restored, const RestoredBodyProbe& probe) {
                for (const auto& effect : s.impactEffects) {
                    if (!isActualAaHit(effect)) continue;
                    const auto key = effectKey(effect);
                    if (!probe.checkpointEffects.contains(key)) continue;
                    const auto saved = std::find_if(restored.impactEffects.begin(), restored.impactEffects.end(),
                        [&](const auto& candidate) { return effectKey(candidate) == key; });
                    if (saved == restored.impactEffects.end()) return fail(probe.name + " checkpoint effect disappeared before its original native lifetime");
                    RuntimeMapCamera camera; camera.initialized = true; camera.tileWidth = 60; camera.tileHeight = 30;
                    const auto anchor = runtimeProjectCellXY(effect.hasWorldPosition ? effect.worldX : effect.cell.x,
                        effect.hasWorldPosition ? effect.worldY : effect.cell.y, effect.hasWorldPosition ? effect.worldLevel : effect.level, camera);
                    camera.worldOffsetX = anchor.x - width / 2; camera.worldOffsetY = anchor.y - height / 2;
                    const auto raster = [&](const RuntimeWindowState& state, const RuntimeImpactEffect& candidate,
                        std::vector<std::uint32_t>& pixels) {
                        auto c = contextFor(state); c.effectOnlyProbe = true; c.specialMechanismSnapshots = nullptr;
                        const std::vector<RuntimeImpactRenderActor> actors{makeRuntimeImpactRenderActor(candidate)}; c.impactActors = &actors;
                        pixels.assign(width * height, background); c.surfacePixels = pixels.data(); auto fixedCamera = camera;
                        const auto drawn = drawRuntimeMap(nullptr, viewport, state.world, c, fixedCamera, *nativeCache);
                        return drawn.rendered && drawn.impactActorSnapshots == 1 && !drawn.impactAnimMissing;
                    };
                    std::vector<std::uint32_t> originalPixels, savedPixels;
                    if (!raster(s, effect, originalPixels) || !raster(restored, *saved, savedPixels) || savedPixels != originalPixels)
                        return fail(probe.name + " already-saved complete49 effect changed original native pixels: " + key);
                    ++snapshotPixelChecks;
                }
                return true;
            };
            const auto redirect = [&](RuntimeCellPoint destination) {
                const auto issue = [&](RuntimeWindowState& state) {
                    facade(state, "Russians"); state.selectedObjectIds = {targetId};
                    const bool accepted = dispatchRuntimeTargetOrderFromUi(state,
                        RuntimeMultiplayerGameplayTargetOrderKind::Move, destination) && state.lastCommand.acceptedCount == 1;
                    facade(state, "Americans"); state.selectedObjectIds = {1}; return accepted;
                };
                return issue(s) && (!ordinary || issue(*ordinary)) && (!exact || issue(*exact)) && (!exactHit || issue(*exactHit));
            };
            for (int n = 0; n < hz * (untilKill ? 45 : (missileScene ? 95 : 18)); ++n) {
                const auto* beforeFlight = findRuntimeObjectByIdIncludingInactive(s, targetId);
                if (moving && beforeFlight && beforeFlight->alive
                    && std::hypot(runtimeObjectRenderX(*beforeFlight) - waypoints[waypoint].x,
                        runtimeObjectRenderY(*beforeFlight) - waypoints[waypoint].y) < 2.5) {
                    waypoint = (waypoint + 1) % waypoints.size();
                    if (!redirect(waypoints[waypoint])) return fail("continuous real target waypoint rejected");
                }
                // Replay only the real pre-effects flight stages in an isolated
                // prediction state. This captures facing, animation distance,
                // fractional height and XYZ before lethal damage changes them.
                predictBody(s, *prediction);
                if (ordinary) predictBody(*ordinary, *ordinaryProbe.prediction);
                if (exact) predictBody(*exact, *exactProbe.prediction);
                if (exactHit) predictBody(*exactHit, *exactHitProbe.prediction);
                const auto* predictedTarget = findRuntimeObjectByIdIncludingInactive(*prediction, targetId);
                const WorldObjectInstance preDamage = predictedTarget ? *predictedTarget : WorldObjectInstance{};
                step(s);
                if (ordinary) { step(*ordinary); if (!compare(*ordinary, false, ordinaryProbe)) return false; }
                if (exact) { step(*exact); if (!compare(*exact, true, exactProbe)) return false; }
                if (exactHit) { step(*exactHit); if (!compare(*exactHit, true, exactHitProbe)) return false; }
                if (!launcherStopped) {
                    const bool releasedVolley = std::all_of(originalMissileVolley.begin(), originalMissileVolley.end(), [&](int id) {
                        const auto flight = s.specialMechanisms.spawnedMissiles().find(id);
                        return flight == s.specialMechanisms.spawnedMissiles().end() || flight->second.released;
                    });
                    if (releasedVolley) {
                        const auto stopLauncher = [&](RuntimeWindowState& state) {
                            facade(state, "Russians"); state.selectedObjectIds = {3};
                            const bool accepted = dispatchRuntimeSelectionCommandFromUi(state,
                                RuntimeMultiplayerGameplayCommandType::Stop) && state.lastCommand.acceptedCount == 1;
                            if (accepted) findMutableObjectById(state, 3)->mission = "Sleep";
                            facade(state, "Americans"); state.selectedObjectIds = {1}; return accepted;
                        };
                        if (!stopLauncher(s) || (ordinary && !stopLauncher(*ordinary)) || (exact && !stopLauncher(*exact))
                            || (exactHit && !stopLauncher(*exactHit))) return fail("released launcher volley Stop rejected");
                        launcherStopped = true;
                    }
                }
                std::set<int> newlyExpired;
                const auto recordShot = [&](const RuntimeProjectileEffect& shot, bool terminal) {
                    if (shot.attackerObjectId != 1) return true;
                    if (shot.parentActorId >= 0 || shot.generation > 0) {
                        if (shot.parentActorId < 0 || shot.generation <= 0 || !fired.contains(shot.parentActorId)
                            || shot.weaponId != "UnitDebris" || shot.weaponSlot != "debris")
                            return fail("unexpected projectile descendant: " + describeShot(shot));
                        if (seenDebris.insert(shot.actorId).second) ++descendantDebris;
                        return true;
                    }
                    fired.insert(shot.actorId);
                    if (terminal && expired.insert(shot.actorId).second) newlyExpired.insert(shot.actorId);
                    // Fuel exhaustion legitimately severs targetObjectId and
                    // preserves a terminal XYZ. The original weapon and
                    // projectile identities must survive that native change.
                    if (shot.weaponId != weaponId || shot.projectileId != profile.projectileId
                        || shot.warheadId != profile.warheadId || (shot.targetObjectId != targetId && shot.targetObjectId != -1)
                        || (shot.targetObjectId < 0 && !terminal))
                        return fail("body attachment rewrote original missile authority: " + describeShot(shot)
                            + " target={" + describeObject(findRuntimeObjectByIdIncludingInactive(s, targetId)) + "}");
                    return true;
                };
                for (const auto& shot : s.projectiles) if (!recordShot(shot, false)) return false;
                for (const auto& shot : s.recentExpiredProjectiles) if (!recordShot(shot, true)) return false;
                const auto* current = findRuntimeObjectByIdIncludingInactive(s, targetId);
                damaged = damaged || (current && current->health < initialHealth);
                moved = moved || (current && std::hypot(runtimeObjectRenderX(*current) - initialX, runtimeObjectRenderY(*current) - initialY) > .1);
                turned = turned || (current && runtimeFacingAbsDifference(initialFacing, current->facing) > 8);
                if (oracles.empty()) turnedBeforeHit = turnedBeforeHit || (preDamage.alive
                    && runtimeFacingAbsDifference(beforeMovementFacing, preDamage.facing) > 8);
                std::set<int> lethalTerminals;
                for (const auto& effect : s.impactEffects)
                    if (effect.attackerObjectId == 1 && effect.targetObjectId == targetId && effect.death
                        && newlyExpired.contains(effect.projectileActorId)) lethalTerminals.insert(effect.projectileActorId);
                const bool newWreck = std::any_of(s.wrecks.begin(), s.wrecks.end(), [&](const auto& wreck) {
                    return wreck.object.instanceId == targetId && wreck.startTick == s.tick;
                });
                WorldObjectInstance expectedVictim = preDamage;
                for (const auto& impact : s.impactEffects) {
                    if (impact.attackerObjectId != 1 || !fired.contains(impact.projectileActorId)
                        || (impact.renderIntent != "warheadImpact" && impact.renderIntent != "deathImpact")) continue;
                    stage = "native body pixels / real explosion canvas";
                    if (impact.weaponId != weaponId || impact.projectileId != profile.projectileId
                        || impact.warheadId != profile.warheadId || !impact.hasWorldPosition)
                        return fail("real hit lost original authority or physical XYZ: actor=" + std::to_string(impact.projectileActorId)
                            + " target=" + std::to_string(impact.targetObjectId) + " preDamage={" + describeObject(&preDamage)
                            + "} current={" + describeObject(current) + "}");
                    if (!airborneTerminals.contains(impact.projectileActorId)) {
                        const auto terminal = std::find_if(s.recentExpiredProjectiles.begin(), s.recentExpiredProjectiles.end(),
                            [&](const auto& shot) { return shot.actorId == impact.projectileActorId; });
                        if (terminal == s.recentExpiredProjectiles.end()) return fail("real impact lacks its native terminal projectile");
                        const bool liveTarget = expectedVictim.instanceId == targetId && expectedVictim.alive && expectedVictim.health > 0;
                        const bool airborneHit = terminal->targetObjectId == targetId && liveTarget && preDamage.airborne;
                        airborneTerminals.emplace(impact.projectileActorId, airborneHit);
                        if (impact.bodyCenterAnchored != airborneHit)
                            return fail("airborne/body marker differs from native terminal classification: " + describeShot(*terminal)
                                + " expectedAir=" + std::to_string(airborneHit) + " flag=" + std::to_string(impact.bodyCenterAnchored)
                                + " expectedHealth=" + std::to_string(expectedVictim.health) + " preDamage={" + describeObject(&preDamage)
                                + "} current={" + describeObject(current) + "}");
                        if (!airborneHit) {
                            if (impact.bodyCenterPresentation.enabled || impact.bodyCenterSpriteOffsetY != 0)
                                return fail("corpse/ground/lost-lock impact inherited an airborne presentation");
                            const double expectedX = terminal->targetObjectId == targetId && liveTarget
                                ? runtimeObjectRenderX(preDamage) : terminal->targetX;
                            const double expectedY = terminal->targetObjectId == targetId && liveTarget
                                ? runtimeObjectRenderY(preDamage) : terminal->targetY;
                            const int expectedLevel = terminal->targetObjectId == targetId && liveTarget
                                ? runtimeProjectileTargetPosition(*prediction, preDamage).level : terminal->toLevel;
                            if (std::abs(impact.worldX - expectedX) > 1e-8 || std::abs(impact.worldY - expectedY) > 1e-8
                                || impact.worldLevel != expectedLevel)
                                return fail("native non-air detonation moved saved physical endpoint: " + describeShot(*terminal));
                            if (terminal->targetObjectId < 0) ++lostLockDetonations;
                            else if (!liveTarget) ++corpseDetonations;
                            else ++groundDetonations;
                        }
                        if (terminal->targetObjectId == targetId && liveTarget) {
                            // Only a private expectation copy changes here;
                            // the real world receives exclusively real weapon
                            // damage. Successive rounds in one Effects pass
                            // use the original armor/prone/veterancy contract.
                            const int nativeDamage = runtimeAuthoredDamageAgainst(s, expectedVictim,
                                runtimeCombatProjectileAuthoredDamage(s, *terminal, *profile.weapon),
                                terminal->warheadId, 0.0, terminal->veteranDamageMultiplier);
                            expectedVictim.health = std::max(0, expectedVictim.health - nativeDamage);
                            const auto* infantry = expectedVictim.kind == WorldObjectKind::Infantry
                                ? findRuntimeUnitType(s.rules, expectedVictim) : nullptr;
                            const auto order = prediction->behaviors.find(targetId);
                            const bool activelyMoving = order != prediction->behaviors.end()
                                && (order->second.kind == RuntimeBehaviorKind::Move
                                    || (order->second.kind == RuntimeBehaviorKind::Attack
                                        && (order->second.state == RuntimeBehaviorState::Chasing
                                            || !order->second.path.empty() || order->second.moveCooldownTicks > 0)));
                            if (nativeDamage > 0 && expectedVictim.health > 0 && infantry
                                && (expectedVictim.prone || !activelyMoving) && !infantry->fearless
                                && !runtimeVeterancyHasAbility(s, expectedVictim, VeteranAbility::Fearless)
                                && !runtimeObjectHasDeployedState(s, expectedVictim)) expectedVictim.prone = true;
                            const bool lethal = nativeDamage > 0 && expectedVictim.health == 0;
                            if (lethal && (!current || !current->alive)
                                && (newWreck || lethalTerminals.contains(impact.projectileActorId)))
                                lethalTerminals.insert(impact.projectileActorId);
                            else if (lethal) return fail("expected native kill lacks actual corpse/wreck/death event: " + describeShot(*terminal)
                                + " current={" + describeObject(current) + "}");
                            if (airborneHit && lethalTerminals.contains(impact.projectileActorId)) {
                                damaged = killed = true;
                            }
                        }
                    }
                    if (!airborneTerminals.at(impact.projectileActorId)) continue;
                    if (!impact.bodyCenterAnchored) return fail("live airborne hit lost its frozen body marker");
                    if (!checkedRecord) {
                        const auto record = runtimeMultiplayerSnapshotImpactRecord(impact);
                        const auto fields = splitRuntimeSaveFields(record); RuntimeImpactEffect decoded;
                        const auto join = [](const std::vector<std::string>& parts) {
                            std::ostringstream out; for (std::size_t i = 0; i < parts.size(); ++i) { if (i) out << '|'; out << parts[i]; } return out.str();
                        };
                        if (fields.size() != 49 || !parseRuntimeMultiplayerSnapshotImpactRecord(record, decoded)
                            || !decoded.bodyCenterAnchored || decoded.bodyCenterPresentation != impact.bodyCenterPresentation
                            || decoded.bodyCenterSpriteOffsetY != impact.bodyCenterSpriteOffsetY)
                            return fail("49-field frozen-center/clearance record roundtrip");
                        auto legacy = fields; legacy.resize(48);
                        if (!parseRuntimeMultiplayerSnapshotImpactRecord(join(legacy), decoded) || !decoded.bodyCenterAnchored
                            || decoded.bodyCenterPresentation.enabled || decoded.bodyCenterSpriteOffsetY != 0)
                            return fail("48-field legacy frozen-center record changed compatibility");
                        legacy.resize(47);
                        if (!parseRuntimeMultiplayerSnapshotImpactRecord(join(legacy), decoded) || decoded.bodyCenterAnchored)
                            return fail("47-field legacy record did not default to the original presentation");
                        auto malformed = fields; malformed[47] = "invalid";
                        if (parseRuntimeMultiplayerSnapshotImpactRecord(join(malformed), decoded)) return fail("non-boolean attachment flag accepted");
                        malformed = fields; malformed[48] = iniHexEncode("invalid");
                        if (parseRuntimeMultiplayerSnapshotImpactRecord(join(malformed), decoded)) return fail("malformed frozen roof profile accepted");
                        malformed = fields; malformed[47] = "0";
                        if (impact.bodyCenterPresentation.enabled && parseRuntimeMultiplayerSnapshotImpactRecord(join(malformed), decoded))
                            return fail("disabled body-center flag accepted an enabled frozen roof profile");
                        // This real UI fixture is offline. Use the same world
                        // simulation hash without requiring a nonexistent room.
                        std::string hashError; const auto beforeHash = runtimeStandaloneSimulationStateHash(s, hashError);
                        if (!beforeHash || !hashError.empty()) return fail("authority hash before presentation probe: " + hashError);
                        auto& mutableImpact = s.impactEffects[static_cast<std::size_t>(&impact - s.impactEffects.data())];
                        const auto original = mutableImpact;
                        mutableImpact.bodyCenterAnchored = !mutableImpact.bodyCenterAnchored;
                        ++mutableImpact.screenOffsetX; --mutableImpact.screenOffsetY;
                        ++mutableImpact.nativeFixedX; --mutableImpact.nativeFixedY;
                        mutableImpact.bodyCenterPresentation = {}; mutableImpact.bodyCenterSpriteOffsetY = 0;
                        const auto afterHash = runtimeStandaloneSimulationStateHash(s, hashError); mutableImpact = original;
                        if (!hashError.empty() || beforeHash != afterHash) return fail("presentation-only center changed authoritative hash");
                        checkedRecord = true; ++recordChecks;
                    }
                    if (!oracles.contains(impact.projectileActorId)) {
                        if (!preDamage.alive || preDamage.instanceId != targetId) return fail("real damage lacks pre-damage native pose");
                        // Impact authority rounds Height exactly once; the body
                        // remains at its original continuous pre-damage height.
                        if (std::abs(impact.worldX - runtimeObjectRenderX(preDamage)) > 1e-8
                            || std::abs(impact.worldY - runtimeObjectRenderY(preDamage)) > 1e-8
                            || impact.worldLevel != runtimeProjectileTargetPosition(*prediction, preDamage).level)
                            return fail("test pre-effects prediction disagrees with physical hit XYZ");
                        HitOracle oracle; oracle.body = preDamage; oracle.tick = s.tick;
                        if (missileScene) oracle.mechanismSnapshots = prediction->specialMechanismRenderSnapshots;
                        oracles.emplace(impact.projectileActorId, std::move(oracle)); ++hits;
                        if (lethalTerminals.contains(impact.projectileActorId)) ++killHits; if (moving) ++movingHits;
                    }
                    auto& oracle = oracles.at(impact.projectileActorId);
                    const auto actor = makeRuntimeImpactRenderActor(impact);
                    const bool first = !firstChecked.contains(impact.projectileActorId);
                    for (int tileWidth : first ? std::vector<int>{48, 60, 72, 120} : std::vector<int>{60}) {
                        RuntimeMapCamera camera; camera.initialized = true; camera.tileWidth = tileWidth; camera.tileHeight = tileWidth / 2;
                        const auto anchor = runtimeProjectCellXY(impact.worldX, impact.worldY, impact.worldLevel, camera);
                        camera.worldOffsetX = anchor.x - width / 2; camera.worldOffsetY = anchor.y - height / 2;
                        if (!oracle.centers.contains(tileWidth)) {
                            auto savedTick = s.tick; s.tick = oracle.tick; POINT expected{};
                            const bool ok = bodyCenter(s, oracle.body, camera, expected, nullptr, &oracle.mechanismSnapshots); s.tick = savedTick;
                            if (!ok) return false; oracle.centers.emplace(tileWidth, expected);
                        }
                        POINT observed{}; std::vector<std::uint32_t> impactPixels;
                        const bool visible = impactCenter(s, actor, camera, observed, impactPixels, false, *nativeCache);
                        if (!visible) { if (!error.empty()) return false; continue; }
                        const auto expected = oracle.centers.at(tileWidth);
                        if (std::abs(observed.x - expected.x) > 2 || std::abs(observed.y - expected.y) > 2)
                            return fail("explosion is outside body center: scale=" + std::to_string(tileWidth)
                                + " expected=" + std::to_string(expected.x) + "," + std::to_string(expected.y)
                                + " actual=" + std::to_string(observed.x) + "," + std::to_string(observed.y));
                        oracle.visible = true; ++scaleChecks;
                        // A post-hit turning witness requires an actual live
                        // body turn during a visible native explosion frame,
                        // whose frozen center was just independently checked.
                        // A retained old travel segment need not turn inside
                        // every weapon's original short effect lifetime.
                        if (tileWidth == 60 && moving && !turnedAfterHit && impact.remainingTicks > 0
                            && current && current->alive && hitFacing >= 0
                            && runtimeFacingAbsDifference(hitFacing, current->facing) > 0) {
                            turnedAfterHit = true; visibleTurnTick = s.tick; visibleTurnActor = impact.projectileActorId;
                            visibleTurnRemaining = impact.remainingTicks; visibleTurnFacing = current->facing;
                        }
                        if (tileWidth == 60 && exactHit && exactHitProbe.checkpointEffects.contains(effectKey(impact))) {
                            const auto restoredImpact = std::find_if(exactHit->impactEffects.begin(), exactHit->impactEffects.end(),
                                [&](const auto& candidate) { return candidate.projectileActorId == impact.projectileActorId
                                    && candidate.renderIntent == impact.renderIntent; });
                            POINT restoredCenter{}; std::vector<std::uint32_t> restoredPixels;
                            if (restoredImpact == exactHit->impactEffects.end() || !restoredImpact->bodyCenterAnchored
                                || !impactCenter(*exactHit, makeRuntimeImpactRenderActor(*restoredImpact), camera,
                                    restoredCenter, restoredPixels, false, *nativeCache)
                                || restoredPixels != impactPixels || restoredCenter.x != observed.x || restoredCenter.y != observed.y)
                                return fail("canonical post-hit restore changed native frozen-center pixels");
                            ++snapshotPixelChecks;
                        }
                        if (tileWidth == 60 && first) {
                            POINT paused{}; std::vector<std::uint32_t> repeat;
                            if (!impactCenter(s, actor, camera, paused, repeat, false, *nativeCache)
                                || repeat != impactPixels || paused.x != observed.x || paused.y != observed.y)
                                return fail("same-tick pause changed real explosion pixels");
                            ++pauseChecks;
                            if (auditCold) {
                                auto cold = std::make_unique<RuntimeMapRenderCache>(); POINT detached{};
                                if (!impactCenter(s, actor, camera, detached, repeat, true, *cold)
                                    || repeat != impactPixels || detached.x != observed.x || detached.y != observed.y)
                                    return fail("cold cache / removed target moved the saved hit explosion");
                                ++detachedChecks;
                            }
                        }
                        if (tileWidth == 60 && visual && !captured && actor.totalTicks - actor.remainingTicks >= actor.totalTicks / 2) {
                                std::vector<std::uint32_t> body; POINT unused{};
                                const int savedTick = s.tick; s.tick = oracle.tick;
                                const bool ok = bodyCenter(s, oracle.body, camera, unused, &body, &oracle.mechanismSnapshots); s.tick = savedTick;
                                if (!ok) return false;
                                // Evidence sheet: native victim, isolated real hit,
                                // and their composition. No new bitmap artwork.
                                std::vector<std::uint32_t> sheet(width * 3 * height, background);
                                for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                                    const auto p = y * width + x, row = y * width * 3;
                                    sheet[row + x] = body[p]; sheet[row + width + x] = impactPixels[p];
                                    sheet[row + width * 2 + x] = impactPixels[p] != background ? impactPixels[p] : body[p];
                                }
                                if (missileScene) {
                                    // The oracle excluded the tail. This third
                                    // evidence panel uses the genuine saved
                                    // Flight snapshot and actual renderer so
                                    // the original exhaust remains visible.
                                    auto c = contextFor(s); c.animationTick = oracle.tick;
                                    c.specialMechanismSnapshots = &oracle.mechanismSnapshots;
                                    std::vector<RuntimeImpactRenderActor> actualHit{actor}; c.impactActors = &actualHit;
                                    GameWorld view = s.world; view.accessTerrainCells().clear();
                                    view.accessTerrainObjects().clear(); view.accessSmudges().clear();
                                    auto& objects = view.accessObjects();
                                    objects.erase(std::remove_if(objects.begin(), objects.end(), [](const auto& object) {
                                        return object.kind != WorldObjectKind::Structure;
                                    }), objects.end()); objects.push_back(oracle.body);
                                    std::vector<std::uint32_t> composite(width * height, background);
                                    c.surfacePixels = composite.data(); auto viewCamera = camera;
                                    const auto drawn = drawRuntimeMap(nullptr, viewport, view, c, viewCamera, *nativeCache);
                                    if (!drawn.rendered || drawn.specialMechanismRenderActors < 1)
                                        return fail("actual native missile Flight/exhaust evidence could not render");
                                    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
                                        sheet[y * width * 3 + width * 2 + x] = composite[y * width + x];
                                }
                                if (!writeRuntimeSmokeBmp(std::string(output) + '/' + label + ".bmp", sheet.data(), width * 3, height, error)) return false;
                                captured = true; ++captures;
                        }
                    }
                    if (oracle.visible) firstChecked.insert(impact.projectileActorId);
                }
                // Every newly captured hit uses its replica's own pre-damage
                // native animation phase. Previously saved effects instead
                // remain byte-for-byte and pixel-for-pixel exact to the source.
                if ((ordinary && !checkOwnBody(*ordinary, ordinaryProbe))
                    || (exact && (!checkOwnBody(*exact, exactProbe) || !checkSavedPixels(*exact, exactProbe)))
                    || (exactHit && (!checkOwnBody(*exactHit, exactHitProbe) || !checkSavedPixels(*exactHit, exactHitProbe)))) return false;
                if (!stopped && (!current || !current->alive || (!untilKill && static_cast<int>(fired.size()) >= burst && !s.combatDetails.hasPendingBurst(1)))) {
                    const bool dead = !current || !current->alive;
                    // A lethal target legitimately cancels the remaining stock
                    // burst. Surviving single-volley targets must finish it.
                    if (!dead && (static_cast<int>(fired.size()) != burst || s.combatDetails.hasPendingBurst(1)))
                        return fail("surviving target did not receive the original complete Burst");
                    if (!dead && findObjectById(s, 1)->weaponCooldownTicksRemaining != runtimeWeaponCooldownTicks(s, *findObjectById(s, 1), *profile.weapon))
                        return fail("original complete-volley ROF changed");
                    const auto stop = [&](RuntimeWindowState& state) {
                        state.selectedObjectIds = {1};
                        if (!dispatchRuntimeSelectionCommandFromUi(state, RuntimeMultiplayerGameplayCommandType::Stop)
                            || state.lastCommand.acceptedCount != 1 || state.combatDetails.hasPendingBurst(1)) return false;
                        findMutableObjectById(state, 1)->mission = "Sleep"; return true;
                    };
                    if (!stop(s) || (ordinary && !stop(*ordinary)) || (exact && !stop(*exact))
                        || (exactHit && !stop(*exactHit))) return fail("actual post-volley Stop/cancel rejected");
                    stopped = true;
                }
                if (flightSave && stopped && !savedFlight && !s.projectiles.empty()) {
                    stage = "ordinary / canonical in-flight restore";
                    ordinary = checkpoint(s, false); exact = checkpoint(s, true);
                    if (!ordinary || !exact) return fail("flight checkpoint: " + error);
                    setupProbe(*ordinary, ordinaryProbe); setupProbe(*exact, exactProbe);
                    if (!compare(*ordinary, false, ordinaryProbe) || !compare(*exact, true, exactProbe)) return fail("flight checkpoint: " + error);
                    if (serializeRuntimeActiveJumpjetFlights(s) != serializeRuntimeActiveJumpjetFlights(*ordinary)
                        || serializeRuntimeActiveJumpjetFlights(s) != serializeRuntimeActiveJumpjetFlights(*exact))
                        return fail("flight checkpoint changed an integer/Q16 Jumpjet authority field");
                    if (moving) {
                        // Ordinary v14 rebuilds its pending route. First prove
                        // the restored flight/pose matches exactly, then issue
                        // the same real UI Move in all three states. No path
                        // or pose is copied into either restored simulation.
                        if (!redirect(waypoints[waypoint])) return fail("post-restore actual route reissue rejected");
                        restoredRouteCommands += exactHit ? 4 : 3;
                    }
                    savedFlight = true; ++saves; ++snapshots;
                }
                if (hitSave && !savedHit && !oracles.empty()) {
                    stage = "canonical post-hit frozen-center restore";
                    exactHit = checkpoint(s, true);
                    if (!exactHit) return fail("post-hit checkpoint: " + error);
                    setupProbe(*exactHit, exactHitProbe);
                    if (!compare(*exactHit, true, exactHitProbe) || !checkSavedPixels(*exactHit, exactHitProbe)) return fail("post-hit checkpoint: " + error);
                    savedHit = true; ++hitSnapshots;
                }
                if (moving && !postHitMove && !oracles.empty() && current && current->alive) {
                    stage = "actual post-hit turn with frozen explosion"; hitFacing = current->facing;
                    waypoint = (waypoint + 1) % waypoints.size();
                    RuntimeCellPoint destination = waypoints[waypoint];
                    if (air.id == "ZEP") {
                        destination = {current->x + 3, current->y};
                        const auto stopTarget = [&](RuntimeWindowState& state) {
                            const auto* before = findObjectById(state, targetId);
                            if (!before) return false;
                            const double x = runtimeObjectRenderX(*before), y = runtimeObjectRenderY(*before);
                            const double z = runtimeDrawLevelForObject(*before); const int facing = before->facing;
                            facade(state, "Russians"); state.selectedObjectIds = {targetId};
                            const bool accepted = dispatchRuntimeSelectionCommandFromUi(state,
                                RuntimeMultiplayerGameplayCommandType::Stop) && state.lastCommand.acceptedCount == 1;
                            const auto* after = findObjectById(state, targetId);
                            const bool continuous = after && runtimeObjectRenderX(*after) == x && runtimeObjectRenderY(*after) == y
                                && runtimeDrawLevelForObject(*after) == z && after->facing == facing;
                            facade(state, "Americans"); state.selectedObjectIds = {1}; return accepted && continuous;
                        };
                        // Native Stop preserves this slow segment's progress;
                        // Move resumes it before turning onto the new route.
                        // Speed=5 can leave seconds of travel while SAMWH's
                        // original explosion lasts 0.4s. Pre-hit real turning
                        // is required for ZEP; its existing hit still freezes
                        // throughout the unextended native effect lifetime.
                        if (!stopTarget(s) || (ordinary && !stopTarget(*ordinary)) || (exact && !stopTarget(*exact))
                            || (exactHit && !stopTarget(*exactHit))) return fail("post-hit Zeppelin Stop changed its continuous pose/facing");
                    }
                    if (!redirect(destination)) return fail("post-hit real target turn rejected");
                    postHitMove = true;
                }
                const bool effects = std::any_of(s.impactEffects.begin(), s.impactEffects.end(), [&](const auto& i) {
                    return i.attackerObjectId == 1 || (missileScene && i.attackerObjectId == 3);
                });
                const bool originalMissilesRemain = missileScene && std::any_of(originalMissileVolley.begin(), originalMissileVolley.end(), [&](int id) {
                    return s.specialMechanisms.spawnedMissiles().contains(id);
                });
                if (stopped && s.projectiles.empty() && !effects && !originalMissilesRemain && !s.combatDetails.hasPendingBurst(1)) { drained = true; break; }
            }
            stage = "original damage / natural completion";
            const auto* finalTarget = findRuntimeObjectByIdIncludingInactive(s, targetId);
            const bool visible = !oracles.empty() && std::all_of(oracles.begin(), oracles.end(), [](const auto& entry) { return entry.second.visible; });
            const auto ownChecksComplete = [&](const RestoredBodyProbe& probe) {
                return std::all_of(probe.bornOracles.begin(), probe.bornOracles.end(), [&](const auto& entry) {
                    const auto scales = probe.visibleScales.find(entry.first);
                    return entry.second.visible && scales != probe.visibleScales.end() && scales->second.size() == 4
                        && probe.coldChecked.contains(entry.first);
                }) && std::all_of(probe.requiredOwnChecks.begin(), probe.requiredOwnChecks.end(),
                    [&](const auto& key) { return probe.bornOracles.contains(key); });
            };
            if (!ownChecksComplete(ordinaryProbe) || !ownChecksComplete(exactProbe) || !ownChecksComplete(exactHitProbe))
                return fail("restored future hit did not execute all four own-native body cameras/lifetime/cold checks");
            if (!stopped || !drained || !damaged || !visible || fired.empty() || expired.size() != fired.size()
                || (missileScene && !launcherStopped)
                || (untilKill && (!killed || (finalTarget && finalTarget->alive))) || (moving && !moved)
                || (moving && !turnedBeforeHit)
                || (moving && finalTarget && finalTarget->alive && !postHitMove)
                || (flightSave && !savedFlight) || (hitSave && !savedHit) || s.rules.getDetailRulesHash() != rulesHash)
                return fail("incomplete native AA/hit/body/save/drain: fired=" + std::to_string(fired.size())
                    + " hits=" + std::to_string(oracles.size()) + " visible=" + std::to_string(visible)
                    + " stopped=" + std::to_string(stopped) + " drain=" + std::to_string(drained)
                    + " moved=" + std::to_string(moved) + " turned=" + std::to_string(turned)
                    + " preHitTurn=" + std::to_string(turnedBeforeHit)
                    + " postHitTurn=" + std::to_string(turnedAfterHit)
                    + " save=" + std::to_string(savedFlight) + " hitSave=" + std::to_string(savedHit));
            if (moving) {
                std::cout << "Air body impact moving evidence " << label << " preHitTurn=" << turnedBeforeHit
                    << " postHitMove=" << postHitMove << " alive=" << (finalTarget && finalTarget->alive)
                    << " visibleFrozenPostHitTurn=" << turnedAfterHit << '\n';
                if (air.id != "ZEP" && turnedAfterHit) {
                    ++visibleTurnWitnessScenes; visibleTurnWitnesses.emplace(std::make_pair(air.id, hz), label);
                    std::cout << "Air body impact visible turn witness type=" << air.id << " hz=" << hz << " label=" << label
                        << " tick=" << visibleTurnTick << " actor=" << visibleTurnActor << " remaining=" << visibleTurnRemaining
                        << " facing=" << hitFacing << "=>" << visibleTurnFacing << " nativeScale60Center=ok\n";
                }
            }
            return true;
        }();
        if (passed) { ++scenes; if (missileScene) ++missileScenes; std::cout << "Air body impact " << label << " PASS\n"; }
        else { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    };
    for (const auto& air : airTypes) for (const std::string source : {"FV", "AEGIS"})
        for (int hz : {30, 60}) for (int rank : {0, 2}) run(source, air, hz, rank, rank == 2 && isCore(air.id), false, false);
    for (const auto& air : airTypes) if (isCore(air.id)) for (const std::string source : {"FV", "AEGIS"})
        for (int hz : {30, 60}) run(source, air, hz, 0, true, false, false);
    for (const auto& air : airTypes) if (air.id == "JUMPJET") for (const std::string source : {"FV", "AEGIS"})
        for (int hz : {30, 60}) run(source, air, hz, 0, false, true, false);
    for (const auto& air : airTypes) if (air.id == "ORCA") for (const std::string source : {"FV", "AEGIS"})
        for (int hz : {30, 60}) run(source, air, hz, 0, false, false, true);
    for (const auto& pair : {std::pair{"V3", "V3ROCKET"}, std::pair{"DRED", "DMISL"}, std::pair{"BSUB", "CMISL"}})
        for (const std::string source : {"FV", "AEGIS"}) for (int hz : {30, 60})
            run(source, AirType{pair.second, WorldObjectKind::Unit, true}, hz, 0, false, false, false, pair.first);
    if (!(selectedCase && *selectedCase)) {
        for (const std::string type : {"JUMPJET", "SHAD", "DISK", "ORCA"}) for (int hz : {30, 60}) {
            const auto witness = visibleTurnWitnesses.find(std::make_pair(type, hz));
            if (witness == visibleTurnWitnesses.end())
                failures.push_back("Air body impact: missing actual live visible frozen-center post-hit turn witness for "
                    + type + '-' + std::to_string(hz));
            else std::cout << "Air body impact matrix turn coverage " << type << '-' << hz << " witness=" << witness->second << '\n';
        }
    }
    // Auxiliary native rendering boundary: a low original aircraft crosses a
    // stock roof. No projectile/damage is fabricated in the real combat cases
    // above. Here a render-only effect explicitly tests the max/clamp boundary
    // that cannot be represented by two camera samples and one affine offset.
    label = "auxiliary-native-nonlinear-roof"; stage = "independent native roof boundary";
    const bool roofPassed = [&]() {
        const auto type = std::find_if(airTypes.begin(), airTypes.end(), [](const auto& air) { return air.id == "SHAD"; });
        if (type == airTypes.end()) return fail("original SHAD unavailable for roof boundary");
        // Reuse the land-side fixture so the original power-plant foundation
        // remains entirely on actual dry TMP cells in this auxiliary scene.
        auto storage = make("FV", *type, 30, 0, false); auto& s = *storage;
        const auto* rule = findRuntimeUnitTypeById(s.rules, "SHAD");
        const auto* weapon = s.rules.findWeaponType("Medusa"); const auto* warhead = s.rules.findWarheadType("SAMWH");
        if (!rule || !weapon || !warhead) return fail("original roof boundary rule links");
        auto pose = *findObjectById(s, 2); pose.hasRenderPosition = true; pose.renderMoveActive = false;
        pose.renderX = pose.x; pose.renderY = pose.y;
        pose.renderLevel = rule->jumpJetHeight / 256.0; pose.level = pose.flightLevel = static_cast<int>(std::ceil(pose.renderLevel));
        WorldObjectInstance host; host.instanceId = 3; host.mapId = "AirBodyRoofBoundary";
        host.type = "GAPOWR"; host.kind = WorldObjectKind::Structure; host.owner = "Russians";
        host.x = pose.x - 1; host.y = pose.y - 1; host.cell = runtimeCellKey(host.x, host.y);
        host.alive = host.poweredOperational = true; host.health = runtimeMaxHealthForObject(s, host); host.mission = "Sleep";
        s.world.accessObjects() = {host, pose};
        RuntimeSnapshotActiveStateScope active(s);
        const auto physical = runtimeProjectileTargetPosition(s, pose);
        std::map<int, RuntimeMapCamera> cameras; std::map<int, POINT> centers, bareCenters, offsets, bareOffsets;
        for (int tile : {48, 60, 72, 120}) {
            RuntimeMapCamera camera; camera.initialized = true; camera.tileWidth = tile; camera.tileHeight = tile / 2;
            const auto point = runtimeProjectCellXY(physical.x, physical.y, physical.level, camera);
            camera.worldOffsetX = point.x - width / 2; camera.worldOffsetY = point.y - height / 2; cameras.emplace(tile, camera);
            POINT expected{};
            if (!bodyCenter(s, pose, camera, expected)) return false; centers.emplace(tile, expected);
            offsets.emplace(tile, POINT{expected.x - width / 2, expected.y - height / 2});
            s.world.accessObjects() = {pose};
            const bool bare = bodyCenter(s, pose, camera, expected); s.world.accessObjects() = {host, pose};
            if (!bare) return false; bareCenters.emplace(tile, expected);
            bareOffsets.emplace(tile, POINT{expected.x - width / 2, expected.y - height / 2});
        }
        const auto affine48 = [](const std::map<int, POINT>& samples) {
            const auto a = samples.at(60), b = samples.at(120);
            return POINT{static_cast<LONG>(std::lround(2.0 * a.x - b.x + (b.x - a.x) * .8)),
                static_cast<LONG>(std::lround(2.0 * a.y - b.y + (b.y - a.y) * .8))};
        };
        const auto predicted = affine48(offsets), barePredicted = affine48(bareOffsets);
        const int missed = std::max(std::abs(predicted.x - offsets.at(48).x), std::abs(predicted.y - offsets.at(48).y));
        const int bareMissed = std::max(std::abs(barePredicted.x - bareOffsets.at(48).x), std::abs(barePredicted.y - bareOffsets.at(48).y));
        if (missed <= 2 || bareMissed > 2) return fail("native auxiliary case did not isolate non-affine roof clearance: roofMiss="
            + std::to_string(missed) + " bareMiss=" + std::to_string(bareMissed));
        // This is a presentation fixture, not a simulated hit. The real helper
        // is the subject; painted body pixels above remain the independent oracle.
        const auto captured = captureRuntimeAirImpactBodyCenter(s, pose, physical);
        RuntimeImpactEffect impact; impact.projectileActorId = 500; impact.attackerObjectId = 1; impact.targetObjectId = 2;
        impact.owner = "Americans"; impact.weaponId = "Medusa"; impact.projectileId = "MedusaProjectile"; impact.warheadId = "SAMWH";
        impact.cell = physical.cell; impact.level = physical.level; impact.hasWorldPosition = true;
        impact.worldX = physical.x; impact.worldY = physical.y; impact.worldLevel = physical.level;
        impact.animId = chooseRuntimeImpactAnim(warhead, *weapon); impact.lifecycleState = "Impact"; impact.renderIntent = "warheadImpact";
        impact.totalTicks = 30; impact.remainingTicks = 26; impact.frameIndex = 3;
        applyRuntimeAirImpactBodyCenter(impact, &captured);
        if (!impact.bodyCenterAnchored || !impact.bodyCenterPresentation.enabled || impact.bodyCenterPresentation.roofs.empty())
            return fail("native low-aircraft roof boundary was not frozen");
        RuntimeImpactEffect restored;
        if (!parseRuntimeMultiplayerSnapshotImpactRecord(runtimeMultiplayerSnapshotImpactRecord(impact), restored)
            || restored.bodyCenterPresentation != impact.bodyCenterPresentation
            || restored.bodyCenterSpriteOffsetY != impact.bodyCenterSpriteOffsetY) return fail("frozen nonlinear roof profile roundtrip");
        // The 49-field extension owns geometry. Its animation image/frame are
        // supplied by the native presentation companion in full snapshots;
        // this render-only probe keeps that same companion explicitly.
        restored.animId = impact.animId; restored.frameIndex = impact.frameIndex;
        restored.facing = impact.facing; restored.projectileImageId = impact.projectileImageId; restored.bright = impact.bright;
        for (int tile : {48, 60, 72, 120}) {
            POINT observed{}, copied{}, detached{}; std::vector<std::uint32_t> pixels, copy, withoutObjects;
            if (!impactCenter(s, makeRuntimeImpactRenderActor(impact), cameras.at(tile), observed, pixels, false, *nativeCache)
                || !impactCenter(s, makeRuntimeImpactRenderActor(restored), cameras.at(tile), copied, copy, false, *nativeCache))
                return fail("native nonlinear roof effect frame invisible");
            auto cold = std::make_unique<RuntimeMapRenderCache>();
            if (!impactCenter(s, makeRuntimeImpactRenderActor(restored), cameras.at(tile), detached, withoutObjects, true, *cold)
                || pixels != copy || pixels != withoutObjects || observed.x != copied.x || observed.y != copied.y
                || observed.x != detached.x || observed.y != detached.y
                || std::abs(observed.x - centers.at(tile).x) > 2 || std::abs(observed.y - centers.at(tile).y) > 2)
                return fail("nonlinear roof center changed with zoom/codec/roof-and-target removal: tile=" + std::to_string(tile));
            ++nonlinearRoofChecks;
        }
        std::cout << "Air body impact auxiliary stock SHAD/GAPOWR nonlinear roof: old48Miss=" << missed
            << " bare48Miss=" << bareMissed << " nativeHeight=" << pose.renderLevel << " PASS\n";
        return true;
    }();
    if (!roofPassed) { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    if (!attemptedScenes) failures.push_back("Air body impact: selected scene label did not match a real combat case");
    if (!(selectedCase && *selectedCase) && (!checkedRecord || snapshotPixelChecks < 1))
        failures.push_back("Air body impact: frozen-center compatibility/hash or post-hit pixel checks did not execute");
    if (!failures.empty()) {
        std::ostringstream text; text << failures.size() << " air body impact scene(s) failed:";
        for (const auto& failure : failures) text << '\n' << failure;
        error = text.str(); return false;
    }
    std::cout << "Air body impact passed: scenes=" << scenes << " stockAirTypes=" << airTypes.size()
        << " realHits=" << hits << " lethalHits=" << killHits << " movingHits=" << movingHits
        << " nativeScaleChecks=" << scaleChecks << " pauseChecks=" << pauseChecks << " detachedColdChecks=" << detachedChecks
        << " ordinaryFlightSaves=" << saves << " exactFlightSnapshots=" << snapshots << " exactHitSnapshots=" << hitSnapshots
        << " pairedTicks=" << pairedTicks << " nativeCaptures=" << captures
        << " recordChecks=" << recordChecks << " snapshotPixelChecks=" << snapshotPixelChecks
        << " nonlinearRoofChecks=" << nonlinearRoofChecks
        << " nativeMissileScenes=" << missileScenes << " postRestoreActualMoveCommands=" << restoredRouteCommands
        << " restoredOwnNativeScaleChecks=" << restoredBodyScaleChecks << " futureNativeFlyOffsetDifferences=" << nativeFlyOffsetDifferences
        << " visibleFrozenTurnGroups=" << visibleTurnWitnesses.size() << " visibleFrozenTurnScenes=" << visibleTurnWitnessScenes
        << " corpseDetonations=" << corpseDetonations << " groundDetonations=" << groundDetonations
        << " lostLockDetonations=" << lostLockDetonations << " descendantDebris=" << descendantDebris
        << " UI/originalBurstROF/realDamage/nativeBodyCenter/physicalXYZ/frozenHit/naturalDrain=ok\n";
    error.clear(); return true;
}
