// The expected attachment comes from independently painted native body pixels.
// Neither the captured impact offsets nor the renderer's anchor query supplies
// the oracle. Physical projectile endpoints and original weapons stay intact.
bool runRuntimeFlakAirBodyImpactSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->config.playerSlots.clear(); assets->config.computerAiEnabled = false;
    assets->config.gameModeRulesOverride.clear();
    assets->report = buildRuntimeStartupReport(assets->config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    IniFile stock;
    if (!assets->report.success || !stock.loadFromFile(assets->report.rulesPath)
        || !assets->rules.loadFromIni(stock)) {
        error = "Flak air body impact: original rules/assets unavailable"; return false;
    }
    assets->rulesLoaded = assets->artLoaded = assets->theaterLoaded = true;
    if (!runRuntimeFlakImpactTargetContract(*assets, error)) return false;
    int landTile = -1, waterTile = -1;
    const int waterSet = assets->theater.getGeneralTileSetIndex("WaterSet");
    for (const auto& set : assets->theater.getTileSets())
        if (set.index == waterSet && set.tilesInSet > 0) waterTile = set.startTileIndex;
    for (int tile = 0; tile < static_cast<int>(assets->theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(*assets, cell);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge) { landTile = tile; break; }
    }
    if (landTile < 0 || waterTile < 0) { error = "Flak air body impact: stock coast TMP unavailable"; return false; }
    struct AirType { std::string id; WorldObjectKind kind; bool aircraft; };
    std::vector<AirType> airTypes;
    std::set<std::string> seen;
    const auto collect = [&](const auto& types, WorldObjectKind kind, bool aircraft) {
        for (const auto& type : types) {
            if (type.strength <= 0 || type.missileSpawn || (!aircraft && !runtimeUnitUsesAirMovement(type))) continue;
            // SCHD is the original Siege Chopper's deployed visual class;
            // SCHP's actual flying body is exercised without inventing a unit.
            if (type.id == "SCHD") {
                std::cout << "Flak air body impact stock SCHD: deployed visual class, flying SCHP tested\n"; continue;
            }
            if (seen.insert(type.id).second) airTypes.push_back({type.id, kind, aircraft});
        }
    };
    collect(assets->rules.getInfantryTypes(), WorldObjectKind::Infantry, false);
    collect(assets->rules.getVehicleTypes(), WorldObjectKind::Unit, false);
    collect(assets->rules.getAircraftTypes(), WorldObjectKind::Unit, true);
    const auto rulesHash = assets->rules.getDetailRulesHash();
    const char* output = std::getenv("CONCORD_FLAK_AIR_BODY_OUTPUT");
    const char* selectedCase = std::getenv("CONCORD_FLAK_AIR_BODY_CASE");
    constexpr int width = 512, height = 384;
    constexpr std::uint32_t background = 0x001d2935, secondBackground = 0x00d1bd8b;
    const RECT viewport{0, 0, width, height};
    auto nativeCache = std::make_unique<RuntimeMapRenderCache>();
    int scenes = 0, attemptedScenes = 0, hits = 0, misses = 0, scaleChecks = 0, recordChecks = 0, detachedChecks = 0, captures = 0;
    int selfHealBudgetChecks = 0;
    std::string label, stage; std::vector<std::string> failures;
    const auto fail = [&](const std::string& why) {
        error = "Flak air body impact " + label + " [" + stage + "]: " + why; return false;
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
        add(1, sourceType, WorldObjectKind::Infantry, sourceCell, "Americans");
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
    const auto isCore = [](const std::string& id) {
        return id == "JUMPJET" || id == "SHAD" || id == "ORCA" || id == "ZEP" || id == "DISK";
    };
    const auto run = [&](const AirType& air, int hz, int rank, bool moving,
        bool untilKill = false, bool roof = false, const char* launcherType = nullptr) {
        const bool missileScene = launcherType != nullptr;
        label = "FLAKT-" + air.id + '-' + std::to_string(hz) + "-rank" + std::to_string(rank)
            + (missileScene ? "-native-missile-flight" : (moving ? "-moving" : "-still"))
            + (untilKill ? "-kill" : "") + (roof ? "-roof" : "");
        if (selectedCase && *selectedCase && label != selectedCase) return;
        ++attemptedScenes;
        const bool passed = [&]() {
            stage = "original infantry / original airborne actor";
            const AirType groundTarget{"CAEAST01", WorldObjectKind::Structure, false};
            auto storage = make("FLAKT", missileScene ? groundTarget : air, hz, rank, roof); auto& s = *storage;
            RuntimeSnapshotActiveStateScope active(s);
            int targetId = 2; bool launcherStopped = !missileScene;
            std::set<int> missileVolley;
            if (missileScene) {
                stage = "real enemy launcher Primary / native missile release";
                auto* destination = findMutableObjectById(s, 2);
                destination->owner = "Americans"; destination->x = 95; destination->y = 78;
                destination->cell = runtimeCellKey(destination->x, destination->y);
                WorldObjectInstance parent; parent.instanceId = 3; parent.mapId = "FlakAirBodyMissileLauncher";
                parent.type = launcherType; parent.kind = WorldObjectKind::Unit; parent.owner = "Russians";
                parent.x = parent.type == "V3" ? 83 : 76; parent.y = 78; parent.cell = runtimeCellKey(parent.x, parent.y);
                parent.alive = parent.poweredOperational = true; parent.health = runtimeMaxHealthForObject(s, parent);
                parent.mission = "Sleep"; parent.facing = parent.turretFacing = runtimeFacingFromDelta(1, 0);
                initializeRuntimeObjectAmmo(parent, runtimeAmmoRulesForObject(s.rules, parent));
                runtimeInitializeModExtensions(parent, s.rules, hz); s.world.accessObjects().push_back(std::move(parent));
                step(s); facade(s, "Russians"); s.selectedObjectIds = {3}; destination = findMutableObjectById(s, 2);
                if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                    runtimeCellPointForObject(*destination), destination) || s.lastCommand.acceptedCount != 1)
                    return fail("real missile launcher Primary rejected");
                targetId = -1;
                for (int tick = 0; tick < hz * 20 && targetId < 0; ++tick) {
                    step(s);
                    for (const auto& [id, missile] : s.specialMechanisms.spawnedMissiles()) {
                        if (missile.parentObjectId != 3) continue;
                        missileVolley.insert(id);
                        const auto* entity = findObjectById(s, id);
                        if (targetId < 0 && missile.typeId == air.id && missile.released && entity && entity->alive && entity->airborne
                            && std::hypot(missile.x - missile.originX, missile.y - missile.originY) >= 1.0) targetId = id;
                    }
                }
                if (targetId < 0 || missileVolley.empty()) return fail("original launcher did not produce its real flying missile");
            }
            const auto* source = findObjectById(s, 1); const auto* target = findObjectById(s, targetId);
            if (!source || source->kind != WorldObjectKind::Infantry || !target || !target->airborne
                || target->health != runtimeMaxHealthForObject(s, *target))
                return fail("original full-health airborne actor startup");
            const int initialHealth = target->health;
            const auto profile = runtimeWeaponProfileForObject(s, *source, target);
            const std::string weaponId = rank == 2 ? "FlakGuyAAGunE" : "FlakGuyAAGun";
            if (!profile.weapon || !profile.projectile || !profile.warhead || profile.weaponId != weaponId
                || profile.projectileId != "FlakProj" || !profile.projectile->inaccurate || !profile.projectile->flakScatter
                || !profile.projectile->aa || profile.projectile->ag
                || profile.weapon->rof != std::stoi(stock.getValue(weaponId, "ROF", "-1"))
                || profile.weapon->burst != std::stoi(stock.getValue(weaponId, "Burst", "0"))
                || profile.weapon->damage != std::stoi(stock.getValue(weaponId, "Damage", "0"))
                || profile.weapon->projectile != stock.getValue(weaponId, "Projectile", "")
                || profile.weapon->warhead != stock.getValue(weaponId, "Warhead", "")
                || profile.warhead->cellSpread <= 0.0)
                return fail("original AA/normal/elite/Inaccurate/FlakScatter/ROF/Burst/Damage changed");
            const auto sourceCell = runtimeCellPointForObject(*source);
            const std::array<RuntimeCellPoint, 4> waypoints{{
                {sourceCell.x + 3, sourceCell.y - 3}, {sourceCell.x + 3, sourceCell.y + 3},
                {sourceCell.x - 3, sourceCell.y + 3}, {sourceCell.x - 3, sourceCell.y - 3}}};
            std::size_t waypoint = 0;
            const double warmStartX = runtimeObjectRenderX(*target), warmStartY = runtimeObjectRenderY(*target);
            const auto move = [&](RuntimeCellPoint cell) {
                facade(s, "Russians"); s.selectedObjectIds = {targetId};
                const bool accepted = dispatchRuntimeTargetOrderFromUi(s,
                    RuntimeMultiplayerGameplayTargetOrderKind::Move, cell) && s.lastCommand.acceptedCount == 1;
                facade(s, "Americans"); s.selectedObjectIds = {1}; return accepted;
            };
            if (moving) {
                if (!move(waypoints[waypoint])) return fail("real enemy Move rejected");
                // The original ZEP turns slowly before its first horizontal
                // segment. Wait for real travel before firing short-lived
                // Flak; never replace its native turn/flight with a fake pose.
                bool traveling = false;
                for (int tick = 0; tick < hz * 15; ++tick) {
                    step(s); target = findObjectById(s, targetId);
                    if (!target || !target->alive || !target->airborne)
                        return fail("real moving aircraft lost during native flight warm-up");
                    if (std::hypot(runtimeObjectRenderX(*target) - warmStartX,
                        runtimeObjectRenderY(*target) - warmStartY) > .25) {
                        traveling = true; break;
                    }
                }
                if (!traveling) return fail("real Move did not begin native horizontal flight in 15 seconds");
            }
            facade(s, "Americans"); s.selectedObjectIds = {1}; target = findObjectById(s, targetId);
            // The final movement gate measures subsequent travel while the
            // attack and explosion run, independently of the warm-up above.
            const double initialX = target ? runtimeObjectRenderX(*target) : warmStartX;
            const double initialY = target ? runtimeObjectRenderY(*target) : warmStartY;
            if (!target || !dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                runtimeCellPointForObject(*target), target) || s.lastCommand.acceptedCount != 1)
                return fail("real infantry UI Primary attack rejected");
            auto prediction = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*prediction, s, false);
            initializeRuntimeNavigation(*prediction);
            struct Oracle {
                WorldObjectInstance body; int tick = 0; RuntimeImpactEffect birth;
                std::vector<SpecialMechanismRenderSnapshot> mechanismSnapshots;
                std::map<int, POINT> centers; std::set<int> visibleScales;
                bool saved = false, captured = false;
            };
            std::map<int, Oracle> oracles;
            std::map<int, RuntimeProjectileEffect> fired;
            std::set<int> expired, classified;
            bool damaged = false, stopped = false, drained = false, moved = false, killed = false;
            int firstHitTick = -1;
            const auto recordShot = [&](const RuntimeProjectileEffect& shot, bool terminal) {
                if (shot.attackerObjectId != 1 || shot.parentActorId >= 0 || shot.generation > 0) return true;
                if (shot.targetObjectId != -1 || shot.weaponId != weaponId || shot.projectileId != "FlakProj"
                    || shot.warheadId != profile.warheadId || shot.damage <= 0
                    || runtimeCombatProjectileAuthoredDamage(s, shot, *profile.weapon) != profile.weapon->damage
                    || shot.aa != profile.projectile->aa || shot.ag != profile.projectile->ag
                    || shot.cellSpread != profile.warhead->cellSpread || shot.percentAtMax != profile.warhead->percentAtMax
                    || !shot.hasTargetPosition)
                    return fail("native scattered non-tracking FlakProj authority changed: actor=" + std::to_string(shot.actorId)
                        + " weapon=" + shot.weaponId + " projectile=" + shot.projectileId
                        + " target=" + std::to_string(shot.targetObjectId) + " damage=" + std::to_string(shot.damage));
                const auto previous = fired.find(shot.actorId);
                if (previous != fired.end() && (previous->second.targetX != shot.targetX
                    || previous->second.targetY != shot.targetY || previous->second.targetZ != shot.targetZ
                    || previous->second.toLevel != shot.toLevel || previous->second.toCell != shot.toCell
                    || previous->second.damage != shot.damage || previous->second.veteranDamageMultiplier != shot.veteranDamageMultiplier))
                    return fail("native scatter endpoint started tracking the aircraft");
                if (previous == fired.end()) fired.emplace(shot.actorId, shot);
                if (terminal) expired.insert(shot.actorId);
                return true;
            };
            for (int tick = 0; tick < hz * (missileScene ? 95 : (untilKill ? 45 : 25)); ++tick) {
                target = findRuntimeObjectByIdIncludingInactive(s, targetId);
                if (moving && target && target->alive && std::hypot(runtimeObjectRenderX(*target) - waypoints[waypoint].x,
                    runtimeObjectRenderY(*target) - waypoints[waypoint].y) < 1.8) {
                    waypoint = (waypoint + 1) % waypoints.size();
                    if (!move(waypoints[waypoint])) return fail("continuous real Move rejected");
                }
                // Capture only the real pre-effects flight phases, independently
                // of the attachment selector and render-anchor query.
                prediction->tick = s.tick + 1; prediction->world.accessObjects() = s.world.getObjects();
                prediction->behaviors = s.behaviors; prediction->jumpjetFlights = s.jumpjetFlights;
                prediction->aircraftFlights = s.aircraftFlights;
                if (missileScene) { prediction->specialMechanisms = s.specialMechanisms;
                    prediction->specialMechanismsInitialized = s.specialMechanismsInitialized; }
                { RuntimeSnapshotActiveStateScope predicting(*prediction);
                    advanceRuntimeJumpjetFlights(*prediction); reconcileRuntimeAircraftFlights(*prediction);
                    if (missileScene) refreshRuntimeSpecialMechanismRenderSnapshots(*prediction); }
                const auto* predicted = findRuntimeObjectByIdIncludingInactive(*prediction, targetId);
                WorldObjectInstance expectedVictim = predicted ? *predicted : WorldObjectInstance{};
                std::map<int, WorldObjectInstance> expectedBodies;
                for (const auto& object : prediction->world.getObjects())
                    if (object.owner == "Russians" && object.alive && object.airborne
                        && object.kind != WorldObjectKind::Structure) expectedBodies.emplace(object.instanceId, object);
                step(s);
                if (!launcherStopped && std::all_of(missileVolley.begin(), missileVolley.end(), [&](int id) {
                    const auto it = s.specialMechanisms.spawnedMissiles().find(id);
                    return it == s.specialMechanisms.spawnedMissiles().end() || it->second.released;
                })) {
                    facade(s, "Russians"); s.selectedObjectIds = {3};
                    if (!dispatchRuntimeSelectionCommandFromUi(s, RuntimeMultiplayerGameplayCommandType::Stop)
                        || s.lastCommand.acceptedCount != 1) return fail("released native missile volley Stop rejected");
                    findMutableObjectById(s, 3)->mission = "Sleep";
                    facade(s, "Americans"); s.selectedObjectIds = {1}; launcherStopped = true;
                }
                for (const auto& shot : s.projectiles) if (!recordShot(shot, false)) return false;
                for (const auto& shot : s.recentExpiredProjectiles) if (!recordShot(shot, true)) return false;
                target = findRuntimeObjectByIdIncludingInactive(s, targetId);
                damaged = damaged || (target && target->health < initialHealth
                    && (!missileScene || (target->alive && target->health > 0)));
                if (!missileScene) killed = killed || (damaged && (!target || !target->alive || target->health <= 0));
                moved = moved || (target && std::hypot(runtimeObjectRenderX(*target) - initialX,
                    runtimeObjectRenderY(*target) - initialY) > .1);
                for (const auto& effect : s.impactEffects) {
                    if (effect.attackerObjectId != 1 || !fired.contains(effect.projectileActorId)
                        || effect.renderIntent != "warheadImpact") continue;
                    stage = "native scatter endpoint / damage footprint / body pixels";
                    const auto& shot = fired.at(effect.projectileActorId);
                    if (effect.targetObjectId != -1 || effect.weaponId != weaponId || effect.projectileId != "FlakProj"
                        || effect.warheadId != profile.warheadId || effect.animId != "SMKPUFF"
                        || !effect.hasWorldPosition || effect.worldX != shot.targetX || effect.worldY != shot.targetY
                        || effect.worldLevel != shot.toLevel || effect.cell != shot.toCell
                        || effect.damage != shot.damage || effect.cellSpread != shot.cellSpread)
                        return fail("visual anchor moved native physical XYZ/damage/identity");
                    if (classified.insert(effect.projectileActorId).second) {
                        // Independent stock splash-footprint classification.
                        // Native launchers may release two real missile bodies;
                        // choose the nearest eligible pre-damage body without
                        // consulting the production attachment selector.
                        const WorldObjectInstance* selectedBody = nullptr;
                        double nearest = std::numeric_limits<double>::infinity(), distance = nearest;
                        for (const auto& [id, body] : expectedBodies) {
                            const double nativeDistance = runtimeCellDistanceToObjectFootprint(*prediction, effect.cell, body);
                            if (!body.alive || body.health <= 0 || nativeDistance > shot.cellSpread
                                || runtimeAuthoredDamageAgainst(s, body, profile.weapon->damage,
                                    shot.warheadId, nativeDistance, shot.veteranDamageMultiplier) <= 0) continue;
                            const double physicalDistance = std::hypot(runtimeObjectRenderX(body) - shot.targetX,
                                runtimeObjectRenderY(body) - shot.targetY);
                            if (physicalDistance < nearest || (physicalDistance == nearest
                                && (!selectedBody || id < selectedBody->instanceId))) {
                                selectedBody = &body; nearest = physicalDistance; distance = nativeDistance;
                            }
                        }
                        const bool shouldAnchor = selectedBody != nullptr;
                        if (effect.bodyCenterAnchored != shouldAnchor)
                            return fail("body marker disagrees with original scatter damage footprint: actor="
                                + std::to_string(effect.projectileActorId) + " distance=" + std::to_string(distance)
                                + " hp=" + std::to_string(expectedVictim.health) + " airborne=" + std::to_string(expectedVictim.airborne));
                        if (shouldAnchor) {
                            Oracle oracle; oracle.body = *selectedBody; oracle.tick = s.tick; oracle.birth = effect;
                            if (missileScene) oracle.mechanismSnapshots = prediction->specialMechanismRenderSnapshots;
                            oracles.emplace(effect.projectileActorId, std::move(oracle)); ++hits;
                            if (firstHitTick < 0) firstHitTick = s.tick;
                        } else {
                            if (effect.bodyCenterPresentation.enabled || effect.bodyCenterSpriteOffsetY != 0)
                                return fail("real scattered miss inherited a body presentation");
                            ++misses;
                        }
                        for (auto& [id, body] : expectedBodies) {
                            const double nativeDistance = runtimeCellDistanceToObjectFootprint(*prediction, effect.cell, body);
                            if (!body.alive || body.health <= 0 || nativeDistance > shot.cellSpread) continue;
                            const int damage = runtimeAuthoredDamageAgainst(s, body,
                                runtimeCombatProjectileAuthoredDamage(s, shot, *profile.weapon),
                                shot.warheadId, nativeDistance, shot.veteranDamageMultiplier);
                            const auto* actual = findRuntimeObjectByIdIncludingInactive(s, id);
                            const bool nativeAaDeath = missileScene && std::any_of(s.impactEffects.begin(), s.impactEffects.end(),
                                [&](const auto& death) { return death.death && death.attackerObjectId == 1
                                    && death.projectileActorId == shot.actorId && death.targetObjectId == id
                                    && death.lifecycleState == "UnitDestruction"; });
                            const bool realDamage = missileScene
                                ? nativeAaDeath || (actual && actual->alive && actual->health > 0 && actual->health < body.health)
                                : !actual || !actual->alive || actual->health < body.health;
                            if (damage > 0 && realDamage) damaged = true;
                            if (damage > 0 && body.health <= damage && (!actual || !actual->alive || actual->health <= 0)
                                && (!missileScene || nativeAaDeath)) killed = true;
                            body.health = std::max(0, body.health - damage);
                            // Match the native private damage budget for
                            // multiple rounds in one Effects pass. Only this
                            // expectation copy changes; original HP and rules
                            // remain exclusively under real combat authority.
                            const auto* infantry = body.kind == WorldObjectKind::Infantry
                                ? findRuntimeUnitType(s.rules, body) : nullptr;
                            const auto order = prediction->behaviors.find(id);
                            const bool activelyMoving = order != prediction->behaviors.end()
                                && (order->second.kind == RuntimeBehaviorKind::Move
                                    || (order->second.kind == RuntimeBehaviorKind::Attack
                                        && (order->second.state == RuntimeBehaviorState::Chasing
                                            || !order->second.path.empty() || order->second.moveCooldownTicks > 0)));
                            if (damage > 0 && body.health > 0 && infantry && (body.prone || !activelyMoving)
                                && !infantry->fearless && !runtimeVeterancyHasAbility(s, body, VeteranAbility::Fearless)
                                && !runtimeObjectHasDeployedState(s, body)) body.prone = true;
                            if (body.health == 0) body.alive = false;
                        }
                    }
                    if (!effect.bodyCenterAnchored) continue;
                    auto& oracle = oracles.at(effect.projectileActorId); const auto& birth = oracle.birth;
                    if (effect.screenOffsetX != birth.screenOffsetX || effect.screenOffsetY != birth.screenOffsetY
                        || effect.nativeFixedX != birth.nativeFixedX || effect.nativeFixedY != birth.nativeFixedY
                        || effect.bodyCenterPresentation != birth.bodyCenterPresentation
                        || effect.bodyCenterSpriteOffsetY != birth.bodyCenterSpriteOffsetY
                        || effect.worldX != birth.worldX || effect.worldY != birth.worldY || effect.worldLevel != birth.worldLevel)
                        return fail("native body center moved after hit birth");
                    const auto actor = makeRuntimeImpactRenderActor(effect);
                    for (const int tileWidth : {48, 60, 72, 120}) {
                        RuntimeMapCamera camera; camera.initialized = true; camera.tileWidth = tileWidth; camera.tileHeight = tileWidth / 2;
                        const auto projected = runtimeProjectCellXY(effect.worldX, effect.worldY, effect.worldLevel, camera);
                        camera.worldOffsetX = projected.x - width / 2; camera.worldOffsetY = projected.y - height / 2;
                        if (!oracle.centers.contains(tileWidth)) {
                            POINT center{};
                            if (!bodyCenter(s, oracle.body, camera, center, nullptr, &oracle.mechanismSnapshots, oracle.tick)) return false;
                            oracle.centers.emplace(tileWidth, center);
                        }
                        POINT observed{}; std::vector<std::uint32_t> pixels;
                        if (!impactCenter(s, actor, camera, observed, pixels, false, *nativeCache)) {
                            if (!error.empty()) return false; continue;
                        }
                        const auto expected = oracle.centers.at(tileWidth);
                        // The shared frozen 60/120 affine offsets and camera
                        // projections round independently. Keep the existing
                        // native body contract's two-pixel bound on each axis.
                        if (std::abs(observed.x - expected.x) > 2 || std::abs(observed.y - expected.y) > 2)
                            return fail("SMKPUFF native canvas outside body center: scale=" + std::to_string(tileWidth)
                                + " expected=" + std::to_string(expected.x) + ',' + std::to_string(expected.y)
                                + " actual=" + std::to_string(observed.x) + ',' + std::to_string(observed.y));
                        oracle.visibleScales.insert(tileWidth); ++scaleChecks;
                        if (tileWidth == 60 && !oracle.saved) {
                            const auto record = runtimeMultiplayerSnapshotImpactRecord(effect); RuntimeImpactEffect decoded;
                            if (splitRuntimeSaveFields(record).size() != 49
                                || !parseRuntimeMultiplayerSnapshotImpactRecord(record, decoded)
                                || runtimeMultiplayerSnapshotImpactRecord(decoded) != record)
                                return fail("49-field frozen Flak impact record roundtrip");
                            // Exact snapshots pair the geometry record with
                            // these seven native presentation fields. Preserve
                            // that real companion before rendering the decode.
                            decoded.animId = effect.animId; decoded.renderIntent = effect.renderIntent;
                            decoded.frameIndex = effect.frameIndex; decoded.facing = effect.facing;
                            decoded.radius = effect.radius; decoded.bright = effect.bright;
                            decoded.projectileImageId = effect.projectileImageId;
                            auto cold = std::make_unique<RuntimeMapRenderCache>(); POINT detached{};
                            std::vector<std::uint32_t> decodedPixels;
                            if (!impactCenter(s, makeRuntimeImpactRenderActor(decoded), camera, detached, decodedPixels, true, *cold)
                                || decodedPixels != pixels || detached.x != observed.x || detached.y != observed.y)
                                return fail("decoded hit/cold cache/removed target moved the SMKPUFF center");
                            POINT paused{}; std::vector<std::uint32_t> repeated;
                            if (!impactCenter(s, actor, camera, paused, repeated, false, *nativeCache)
                                || repeated != pixels || paused.x != observed.x || paused.y != observed.y)
                                return fail("same-tick pause changed native SMKPUFF pixels");
                            oracle.saved = true; ++recordChecks; ++detachedChecks;
                        }
                        if (tileWidth == 60 && output && *output && !oracle.captured && hz == 30 && rank == 0 && isCore(air.id)) {
                            std::vector<std::uint32_t> body; POINT unused{};
                            if (!bodyCenter(s, oracle.body, camera, unused, &body, &oracle.mechanismSnapshots, oracle.tick)) return false;
                            std::vector<std::uint32_t> sheet(width * 3 * height, background);
                            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                                const auto p = y * width + x, row = y * width * 3;
                                sheet[row + x] = body[p]; sheet[row + width + x] = pixels[p];
                                sheet[row + width * 2 + x] = pixels[p] != background ? pixels[p] : body[p];
                            }
                            if (!writeRuntimeSmokeBmp(std::string(output) + '/' + label + ".bmp", sheet.data(), width * 3, height, error)) return false;
                            oracle.captured = true; ++captures;
                        }
                    }
                }
                // Native missile flight can terminate in the later special
                // phase, independently of AA damage. Ordinary air bodies have
                // no such ambiguity: every Effects pass must match exactly the
                // original armor/prone/veterancy splash damage budget above.
                if (!missileScene) for (auto& [id, expectedBody] : expectedBodies) {
                    // Effects precede Economy in a complete tick. Original
                    // ZEP/DISK SelfHealing (and veteran SELF_HEAL) contributes
                    // one HP on its authored RepairRate boundary after hits.
                    const double repairMinutes = s.rules.getVeterancyRules().selfHealRateMinutes;
                    if (expectedBody.alive && expectedBody.health > 0
                        && std::isfinite(repairMinutes) && repairMinutes > 0.0) {
                        const int interval = std::max(1, static_cast<int>(std::lround(repairMinutes * 60.0 * hz)));
                        const auto* unit = findRuntimeUnitType(s.rules, expectedBody);
                        const int maximum = runtimeMaxHealthForObject(s, expectedBody);
                        if (s.tick > 0 && s.tick % interval == 0 && expectedBody.health < maximum
                            && ((unit && unit->selfHealing)
                                || runtimeVeterancyHasAbility(s, expectedBody, VeteranAbility::SelfHeal))) {
                            expectedBody.health = std::min(maximum, expectedBody.health + 1);
                            ++selfHealBudgetChecks;
                        }
                    }
                    const auto* actual = findRuntimeObjectByIdIncludingInactive(s, id);
                    if (actual ? (actual->health != expectedBody.health || actual->alive != expectedBody.alive)
                        : expectedBody.alive || expectedBody.health != 0)
                        return fail("original exact native HP/alive budget diverged: id=" + std::to_string(id)
                            + " expectedHealth=" + std::to_string(expectedBody.health)
                            + " actualHealth=" + std::to_string(actual ? actual->health : 0)
                            + " expectedAlive=" + std::to_string(expectedBody.alive)
                            + " actualAlive=" + std::to_string(actual && actual->alive));
                }
                // Stop through the same player UI after real damage and let
                // the original Burst and native effect lifetime finish.
                if (!stopped && damaged && firstHitTick >= 0 && (untilKill ? killed : s.tick > firstHitTick + hz / 2)) {
                    facade(s, "Americans"); s.selectedObjectIds = {1};
                    if (!dispatchRuntimeSelectionCommandFromUi(s, RuntimeMultiplayerGameplayCommandType::Stop)
                        || s.lastCommand.acceptedCount != 1) return fail("real infantry Stop rejected");
                    // Match the existing AA fixture's quiescent drain phase:
                    // Stop naturally enters Guard and can acquire the nearby
                    // V3 launcher with FLAKT's different ground weapon.
                    findMutableObjectById(s, 1)->mission = "Sleep";
                    stopped = true;
                }
                const bool ownProjectiles = std::any_of(s.projectiles.begin(), s.projectiles.end(),
                    [](const auto& shot) { return shot.attackerObjectId == 1; });
                const bool ownEffects = std::any_of(s.impactEffects.begin(), s.impactEffects.end(),
                    [](const auto& effect) { return effect.attackerObjectId == 1; });
                const bool originalMissilesRemain = missileScene && std::any_of(missileVolley.begin(), missileVolley.end(),
                    [&](int id) { return s.specialMechanisms.spawnedMissiles().contains(id); });
                if (stopped && !ownProjectiles && !ownEffects && !originalMissilesRemain && !s.combatDetails.hasPendingBurst(1)) { drained = true; break; }
            }
            stage = "real stock damage / firing and animation completion";
            const bool visible = !oracles.empty() && std::all_of(oracles.begin(), oracles.end(),
                [](const auto& entry) { return entry.second.visibleScales.size() == 4 && entry.second.saved; });
            if (!damaged || !stopped || !drained || !visible || fired.empty() || expired.size() != fired.size()
                || (moving && !moved) || (untilKill && !killed) || !launcherStopped || s.rules.getDetailRulesHash() != rulesHash)
                return fail("incomplete damage/native body/scatter/record/drain: damage=" + std::to_string(damaged)
                    + " stopped=" + std::to_string(stopped) + " drained=" + std::to_string(drained)
                    + " visible=" + std::to_string(visible) + " fired=" + std::to_string(fired.size())
                    + " expired=" + std::to_string(expired.size()) + " hits=" + std::to_string(oracles.size())
                    + " moved=" + std::to_string(moved));
            return true;
        }();
        if (passed) { ++scenes; std::cout << "Flak air body impact " << label << " PASS\n"; }
        else { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    };
    for (const auto& air : airTypes) for (int hz : {30, 60}) for (int rank : {0, 2}) run(air, hz, rank, false);
    for (const auto& air : airTypes) if (isCore(air.id)) for (int hz : {30, 60}) for (int rank : {0, 2}) run(air, hz, rank, true);
    for (const auto& air : airTypes) if (air.id == "ORCA") for (int hz : {30, 60}) run(air, hz, 0, false, false, true);
    for (const auto& air : airTypes) if (air.id == "JUMPJET" || air.id == "ASW") for (int hz : {30, 60}) run(air, hz, 0, false, true);
    for (const auto& pair : {std::pair{"V3", "V3ROCKET"}, std::pair{"DRED", "DMISL"}, std::pair{"BSUB", "CMISL"}})
        for (int hz : {30, 60}) run(AirType{pair.second, WorldObjectKind::Unit, true}, hz, 0, false, false, false, pair.first);
    if (attemptedScenes == 0) failures.push_back("Flak air body impact: selected case did not match original-air matrix");
    if (!(selectedCase && *selectedCase) && airTypes.size() != 15)
        failures.push_back("Flak air body impact: original-air matrix count changed: " + std::to_string(airTypes.size()));
    if (!failures.empty()) { error = failures.front(); return false; }
    std::cout << "Flak air body impact: originalAirTypes=" << airTypes.size() << " scenes=" << scenes
        << " hits=" << hits << " realMisses=" << misses << " nativeBodyScaleChecks=" << scaleChecks
        << " records=" << recordChecks << " detachedCold=" << detachedChecks << " captures=" << captures
        << " exactSelfHealBudgets=" << selfHealBudgetChecks << '\n';
    return true;
}
