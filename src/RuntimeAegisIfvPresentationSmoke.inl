// Presentation aliasing is checked against real IFV shots. Neither the stock
// Medusa authority nor the simulation projectile records are rewritten.
bool runRuntimeAegisIfvPresentationSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->config.playerSlots.clear(); assets->config.computerAiEnabled = false;
    assets->config.gameModeRulesOverride.clear();
    assets->report = buildRuntimeStartupReport(assets->config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) { error = "Aegis/IFV original assets: " + assets->report.errorMessage; return false; }
    IniFile stock;
    if (!stock.loadFromFile(assets->report.rulesPath) || !assets->rules.loadFromIni(stock)) {
        error = "Aegis/IFV original rules unavailable"; return false;
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
    if (landTile < 0 || waterTile < 0) { error = "Aegis/IFV original coast TMP unavailable"; return false; }
    const auto* dragon = assets->art.findArtType("DRAGON");
    const auto* medusa = assets->rules.findProjectileType("MedusaProjectile");
    if (!dragon || !dragon->rotates || !dragon->useLineTrail || dragon->lineTrailColor != "216,216,255"
        || dragon->lineTrailColorDecrement != 16 || !medusa || medusa->image != "MEDUSA"
        || medusa->ranged || medusa->rot != 20 || medusa->courseLockDuration != 15 || !medusa->aa || medusa->ag) {
        error = "Aegis/IFV original missile/art authority changed"; return false;
    }
    const auto stockHash = assets->rules.getDetailRulesHash();
    const char* output = std::getenv("CONCORD_AEGIS_IFV_PRESENTATION_OUTPUT");
    std::string label, stage; std::vector<std::string> failures;
    int scenes = 0, missiles = 0, draws = 0, equivalences = 0, saves = 0, snapshots = 0, paired = 0, captures = 0;
    std::map<std::pair<int, int>, RuntimeProjectileRenderActor> ifvSamples;
    std::map<std::string, int> impactFrameCounts;
    const auto fail = [&](const std::string& why) { error = "Aegis/IFV " + label + " [" + stage + "]: " + why; return false; };
    const auto stockRulesReady = [&](const RuntimeWindowState& s) {
        if (s.rules.getDetailRulesHash() != stockHash) return false;
        for (const char* id : {"Medusa", "MedusaE"}) {
            const auto* profile = s.rules.findWeaponLifecycleProfile(id);
            if (!profile || !profile->omniFire) return false;
        }
        return true;
    };
    const auto loadStockRules = [&](RuntimeWindowState& s) {
        // Rebuild owner-indexed lifecycle caches from the unchanged stock INI.
        if (!s.rules.loadFromIni(stock) || !stockRulesReady(s)) return false;
        s.rulesLoaded = true; return true;
    };
    const auto make = [&](const std::string& sourceType, int hz, int rank, int direction) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        s.config = assets->config; s.config.fixedTickHz = hz; s.config.noCooldown = false;
        s.config.gameMode = "Battle"; s.config.playerCountry = "Americans"; s.config.skirmishLoadSavePath.clear();
        s.config.multiplayerActive = s.config.shortGame = s.config.crates = s.config.superweapons = false;
        s.report = assets->report; s.art = assets->art; s.theater = assets->theater;
        if (!loadStockRules(s)) return std::unique_ptr<RuntimeWindowState>{};
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.world = assets->world;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts.theater = assets->mapFacts.theater; s.mapFacts.lighting = assets->mapFacts.lighting;
        for (int y = 48; y <= 112; ++y) for (int x = 48; x <= 112; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = x <= 78 ? waterTile : landTile; cell.subTileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
        }
        const RuntimeCellPoint sourceCell{sourceType == "AEGIS" ? 75 : 83, 75};
        const RuntimeCellPoint targetCell{sourceCell.x + (direction == 0 ? 6 : 0), sourceCell.y + (direction == 0 ? 0 : 6)};
        const auto add = [&](int id, const std::string& type, WorldObjectKind kind, RuntimeCellPoint cell, const char* owner) {
            WorldObjectInstance o; o.instanceId = id; o.mapId = "AegisIfv" + std::to_string(id);
            o.type = type; o.kind = kind; o.owner = owner; o.x = cell.x; o.y = cell.y;
            o.cell = runtimeCellKey(cell.x, cell.y); o.alive = o.poweredOperational = true;
            o.health = runtimeMaxHealthForObject(s, o); o.mission = "Sleep";
            o.facing = o.turretFacing = runtimeFacingFromDelta(direction == 0 ? 1 : 0, direction == 0 ? 0 : 1);
            initializeRuntimeObjectAmmo(o, runtimeAmmoRulesForObject(s.rules, o));
            applyRuntimeInitialObjectFlightState(s.rules, o);
            runtimeInitializeModExtensions(o, s.rules, hz); s.world.accessObjects().push_back(std::move(o));
        };
        add(1, sourceType, WorldObjectKind::Unit, sourceCell, "Americans");
        findMutableObjectById(s, 1)->veterancy = rank;
        // Stock Kirov strength supports a complete elite burst without an
        // artificial health/rules overlay. Sleep leaves it stationary.
        add(2, "ZEP", WorldObjectKind::Unit, targetCell, "Russians");
        add(998, "NAPOWR", WorldObjectKind::Structure, {105, 105}, "Russians");
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
    const auto checkpoint = [&](RuntimeWindowState& source, bool exact) {
        std::ostringstream baseText, exactText, canonical; IniFile baseIni, exactIni;
        if (!writeRuntimeStateToStream(source, baseText, error) || !baseIni.loadFromString(baseText.str())) return std::unique_ptr<RuntimeWindowState>{};
        if (exact && (!writeRuntimeStateToStream(source, exactText, error)
            || !writeRuntimeMultiplayerSnapshotExtension(source, exactText, error)
            || !exactIni.loadFromString(exactText.str()) || !writeRuntimeMultiplayerSnapshotCanonicalIni(exactIni, canonical, error)
            || !exactIni.loadFromString(canonical.str()))) return std::unique_ptr<RuntimeWindowState>{};
        auto restored = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored, source, false);
        if (!loadStockRules(*restored)) return std::unique_ptr<RuntimeWindowState>{};
        // Canonical MP data omits process-local AV authority. The complete
        // ordinary v14 base supplies it before the exact extension is applied.
        if (!materializeRuntimeLoadStateFromIni(*restored, baseIni, error)
            || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*restored, exactIni, error))
            || !stockRulesReady(*restored)) return std::unique_ptr<RuntimeWindowState>{};
        return restored;
    };
    constexpr int width = 640, height = 360;
    constexpr std::uint32_t background = 0x001c2428;
    const RECT viewport{0, 0, width, height};
    const auto contextFor = [&](const RuntimeWindowState& s) {
        RuntimeMapRenderContext context; context.rules = &s.rules; context.art = &s.art;
        context.assetRootPath = runtimeAssetRootPath(s.config); context.palettePath = runtimePalettePath(s.config, s.report.map);
        context.structurePalettePath = runtimeStructurePalettePath(s.config, s.report.map);
        context.fixedTickHz = s.config.fixedTickHz; context.surfaceWidth = width; context.surfaceHeight = height;
        context.effectOnlyProbe = true; context.vision = nullptr; context.lightingTint = nullptr;
        return context;
    };
    for (const std::string type : {"FV", "AEGIS"}) for (int hz : {30, 60}) for (int rank : {0, 2}) for (int direction : {0, 1}) {
        label = type + '-' + std::to_string(hz) + "-rank" + std::to_string(rank) + "-direction" + std::to_string(direction);
        const bool scenePassed = [&]() {
            auto storage = make(type, hz, rank, direction);
            if (!storage) return fail("stock owner lost original Medusa OmniFire/hash");
            auto& s = *storage; RuntimeSnapshotActiveStateScope active(s);
            const auto* source = findObjectById(s, 1); const auto* target = findObjectById(s, 2);
            const int initialHealth = target ? target->health : -1;
            if (!source || !target || !target->airborne || initialHealth != runtimeMaxHealthForObject(s, *target))
                return fail("stock airborne target/health startup");
            const auto profile = runtimeWeaponProfileForObject(s, *source, target);
            const std::string expectedWeapon = type == "AEGIS" ? (rank == 2 ? "MedusaE" : "Medusa") : (rank == 2 ? "HoverMissileE" : "HoverMissile");
            if (!profile.weapon || !profile.projectile || !profile.warhead || profile.weaponId != expectedWeapon
                || profile.weapon->rof != std::stoi(stock.getValue(expectedWeapon, "ROF", "-1"))
                || profile.weapon->burst != std::stoi(stock.getValue(expectedWeapon, "Burst", "0"))
                || (type == "AEGIS" && (profile.projectileId != "MedusaProjectile" || profile.warheadId != "SAMWH")))
                return fail("original normal/elite weapon, Burst, ROF or SAM authority changed");
            const int expectedRounds = std::max(1, profile.weapon->burst);
            stage = "actual player primary attack"; s.selectedObjectIds = {1};
            if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                runtimeCellPointForObject(*target), target) || s.lastCommand.acceptedCount != 1) return fail("actual UI attack rejected");
            auto context = contextFor(s);
            auto cache = std::make_unique<RuntimeMapRenderCache>(), compareCache = std::make_unique<RuntimeMapRenderCache>();
            auto impactCache = std::make_unique<RuntimeMapRenderCache>();
            std::vector<std::uint32_t> pixels(width * height), compared(width * height), impactPixels(width * height);
            GameWorld isolated = assets->world; isolated.accessObjects().clear(); isolated.accessTerrainCells().clear();
            isolated.accessTerrainObjects().clear(); isolated.accessSmudges().clear();
            RuntimeMapCamera camera; camera.initialized = true;
            std::set<int> fired, completed, impactActors, visibleImpactActors; std::map<int, RuntimeProjectileEffect> previous;
            std::vector<RuntimeProjectileRenderActor> actors, compareActors;
            std::unique_ptr<RuntimeWindowState> ordinary, exact;
            bool stopped = false, drewLine = false, drewBody = false, sawHit = false, savedFlight = false;
            bool shotCapture = false, travelCapture = false, hitCapture = false, drained = false, curved = false;
            double initialYaw = 0; bool haveYaw = false; int firstActorId = -1;
            const bool visual = output && hz == 30 && rank == 0 && direction == 0;
            const auto capture = [&](const std::string& name, RuntimeCellPoint center) {
                const auto stats = drawRuntimeStateOffscreenAtCell(s, center, 1000, 700, error,
                    std::string(output) + '/' + label + '-' + name + ".bmp", false);
                if (!stats || !stats->rendered || stats->missileExhaustDrawn || stats->missileExhaustHeadDrawn)
                    return fail("native scene capture contains smoke or failed: " + error);
                ++captures; return true;
            };
            const auto compareFlight = [&](RuntimeWindowState& restored, bool full) {
                if (restored.projectiles.size() != s.projectiles.size()
                    || !findRuntimeObjectByIdIncludingInactive(restored, 2)
                    || findRuntimeObjectByIdIncludingInactive(restored, 2)->health != findRuntimeObjectByIdIncludingInactive(s, 2)->health)
                    return fail("restored flight/damage diverged at tick " + std::to_string(s.tick));
                for (std::size_t i = 0; i < s.projectiles.size(); ++i)
                    if (serializeRuntimeProjectileEffect(s.projectiles[i]) != serializeRuntimeProjectileEffect(restored.projectiles[i]))
                        return fail("restored original projectile pose/guidance/clock diverged");
                if (full) {
                    if (restored.impactEffects.size() != s.impactEffects.size()) return fail("exact snapshot lost impact/exhaust animation");
                    for (std::size_t i = 0; i < s.impactEffects.size(); ++i)
                        if (runtimeMultiplayerSnapshotImpactRecord(s.impactEffects[i]) != runtimeMultiplayerSnapshotImpactRecord(restored.impactEffects[i]))
                            return fail("exact snapshot changed impact/exhaust native frame clock");
                }
                ++paired; return true;
            };
            for (int n = 0; n < hz * 18; ++n) {
                step(s);
                if (ordinary) { step(*ordinary); if (!compareFlight(*ordinary, false)) return false; }
                if (exact) { step(*exact); if (!compareFlight(*exact, true)) return false; }
                for (const auto& shot : s.projectiles) if (shot.attackerObjectId == 1) {
                    fired.insert(shot.actorId);
                    if (firstActorId < 0) firstActorId = shot.actorId;
                    if (shot.weaponId != expectedWeapon || shot.projectileId != profile.projectileId
                        || (type == "AEGIS" && (shot.projectileImageId != "MEDUSA" || shot.renderIntent != "radialProjectile")))
                        return fail("presentation mapping rewrote the simulation missile identity");
                    if (const auto old = previous.find(shot.actorId); old != previous.end()) {
                        const double distance = std::hypot(std::hypot(shot.currentX - old->second.currentX,
                            shot.currentY - old->second.currentY), (shot.currentZ - old->second.currentZ) / runtimeMissileLevelsPerCell);
                        if (distance > shot.speedCellsPerTick + 1e-7) return fail("missile travel jumped");
                    }
                    previous[shot.actorId] = shot;
                    if (type == "FV") ifvSamples.try_emplace(std::pair{hz, rank}, makeRuntimeProjectileRenderActor(shot, &s.rules, &s.art));
                    if (type == "AEGIS" && shot.actorId == firstActorId && shot.homingInitialized) {
                        if (!haveYaw) { haveYaw = true; initialYaw = shot.homingYaw; }
                        const double change = std::abs(std::remainder(shot.homingYaw - initialYaw, 6.283185307179586));
                        // Original rules still declare 15; only an immutable
                        // validated Aegis/Medusa launch uses the compact three.
                        if (!shot.launchSourceValidated || shot.launchSourceType != "AEGIS")
                            return fail("real Medusa launch lost immutable Aegis provenance");
                        if (shot.elapsedTicks * 15.0 / hz < 3 && change > 1e-8)
                            return fail("Medusa turned before the compact Aegis course-lock elapsed");
                        curved = curved || change > .3;
                    }
                }
                for (const auto& shot : s.recentExpiredProjectiles) if (shot.attackerObjectId == 1) {
                    fired.insert(shot.actorId); completed.insert(shot.actorId);
                    RuntimeProjectileEffect decoded;
                    if (!parseRuntimeProjectileEffect(serializeRuntimeProjectileEffect(shot), decoded, runtimeProjectileSaveSchemaV6, true))
                        return fail("actual expired missile no longer saves legally");
                }
                for (const auto& impact : s.impactEffects) if (impact.attackerObjectId == 1 && impact.renderIntent == "warheadImpact") {
                    sawHit = true; impactActors.insert(impact.projectileActorId);
                    if (impact.weaponId != expectedWeapon || impact.projectileId != profile.projectileId
                        || impact.warheadId != profile.warheadId
                        || impact.animId != chooseRuntimeImpactAnim(profile.warhead, *profile.weapon)
                        || impact.totalTicks <= 0 || impact.remainingTicks <= 0 || impact.remainingTicks > impact.totalTicks)
                        return fail("actual hit lost its original warhead animation/clock authority");
                    if (!impactFrameCounts.contains(impact.animId)) {
                        RuntimeShpFrameImage image; std::string resolved, sourcePath, loadError; int count = 0;
                        if (!loadRuntimeAuxiliaryShpFrameImage(context, *impactCache, impact.animId, 0, image,
                            resolved, sourcePath, loadError, false, &count) || count <= 0 || sourcePath.empty())
                            return fail("original impact SHP failed to decode: " + impact.animId + " " + loadError);
                        impactFrameCounts.emplace(impact.animId, count);
                    }
                    const auto actor = makeRuntimeImpactRenderActor(impact);
                    if (impact.animationFrameCount > 0) {
                        if (impact.animationSequenceSteps <= 0 || impact.animationFrameDelayTicks <= 0
                            || impact.animationEndFrame > impactFrameCounts.at(impact.animId)
                            || impact.frameIndex < impact.animationStartFrame || impact.frameIndex >= impact.animationEndFrame
                            || actor.frameIndex != impact.frameIndex)
                            return fail("explicit impact timeline lost its native SHP frame range/clock");
                    } else {
                        // Non-crater/scorch hits keep the -1 presentation sentinel.
                        // The actual renderer derives their frame from SHP/Art Rate.
                        if (impact.frameIndex != -1 || actor.frameIndex != -1
                            || actor.totalTicks != impact.totalTicks || actor.remainingTicks != impact.remainingTicks)
                            return fail("ordinary impact no longer delegates its original frame clock to the renderer");
                    }
                    std::vector<RuntimeImpactRenderActor> oneImpact{actor};
                    auto impactContext = context; impactContext.projectileActors = nullptr;
                    impactContext.impactActors = &oneImpact; impactContext.animationTick = s.tick;
                    impactContext.surfacePixels = impactPixels.data();
                    RuntimeMapCamera impactCamera; impactCamera.initialized = true;
                    const auto impactOrigin = runtimeProjectCellXY(actor.x, actor.y, actor.level, impactCamera);
                    impactCamera.worldOffsetX = impactOrigin.x - width / 2; impactCamera.worldOffsetY = impactOrigin.y - height / 2;
                    std::fill(impactPixels.begin(), impactPixels.end(), background);
                    const auto impactStats = drawRuntimeMap(nullptr, viewport, isolated, impactContext, impactCamera, *impactCache);
                    if (impactStats.impactActorSnapshots != 1 || impactStats.impactAnimMissing)
                        return fail("actual impact frame clock could not render its native resource");
                    if (impactStats.impactActorDrawn > 0 && std::any_of(impactPixels.begin(), impactPixels.end(),
                        [](std::uint32_t pixel) { return pixel != background; })) visibleImpactActors.insert(impact.projectileActorId);
                }
                if (visual && !shotCapture && !s.projectiles.empty()) {
                    shotCapture = true;
                    if (!capture("launch", runtimeCellPointForObject(*findObjectById(s, 1)))) return false;
                }
                if (visual && !travelCapture && !s.projectiles.empty() && s.projectiles.front().elapsedTicks * 15 / hz >= 5) {
                    travelCapture = true;
                    const auto& shot = s.projectiles.front();
                    if (!capture("travel", {static_cast<int>(std::lround(shot.currentX)), static_cast<int>(std::lround(shot.currentY))})) return false;
                }
                if (visual && !hitCapture && sawHit) {
                    hitCapture = true; if (!capture("impact", runtimeCellPointForObject(*findObjectById(s, 2)))) return false;
                }
                if (!stopped && static_cast<int>(fired.size()) >= expectedRounds && !s.combatDetails.hasPendingBurst(1)) {
                    if (static_cast<int>(fired.size()) != expectedRounds) return fail("a new volley began before the complete stock Burst was observed");
                    if (findObjectById(s, 1)->weaponCooldownTicksRemaining != runtimeWeaponCooldownTicks(s, *findObjectById(s, 1), *profile.weapon))
                        return fail("the complete volley did not retain the original normal/elite ROF");
                    s.selectedObjectIds = {1};
                    if (!dispatchRuntimeSelectionCommandFromUi(s, RuntimeMultiplayerGameplayCommandType::Stop)
                        || s.lastCommand.acceptedCount != 1 || s.combatDetails.hasPendingBurst(1)) return fail("post-volley Stop failed");
                    // Stop returns to Guard in actual gameplay. This single-volley
                    // fixture observes that real command, then suppresses a later
                    // automatic reacquisition while the emitted missiles finish.
                    findMutableObjectById(s, 1)->mission = "Sleep";
                    stopped = true;
                }
                if (stopped && !savedFlight && !s.projectiles.empty() && direction == 0) {
                    stage = "original flight save/restore";
                    ordinary = checkpoint(s, false); exact = checkpoint(s, true);
                    if (!ordinary || !exact || !compareFlight(*ordinary, false) || !compareFlight(*exact, true))
                        return fail("in-flight checkpoint: " + error);
                    savedFlight = true; ++saves; ++snapshots;
                }
                stage = "native body/line-trail projection";
                if (n % std::max(1, hz / 15) == 0 || (stopped && s.projectiles.empty())) {
                    actors.clear(); compareActors.clear();
                    for (const auto& shot : s.projectiles) if (shot.attackerObjectId == 1) actors.push_back(makeRuntimeProjectileRenderActor(shot, &s.rules, &s.art));
                    if (!actors.empty()) {
                        const auto origin = runtimeProjectCellXY(actors.front().currentX, actors.front().currentY, actors.front().currentLevel, camera);
                        camera.worldOffsetX = origin.x - width / 2; camera.worldOffsetY = origin.y - height / 2;
                    }
                    context.animationTick = s.tick; context.projectileActors = &actors; context.surfacePixels = pixels.data();
                    std::fill(pixels.begin(), pixels.end(), background);
                    const auto stats = drawRuntimeMap(nullptr, viewport, isolated, context, camera, *cache); ++draws;
                    if (!actors.empty()) {
                        if (stats.projectileActorTravelDrawn <= 0 || stats.projectileDirectionalSprites <= 0
                            || stats.projectileActorTravelMissing || stats.missileExhaustDrawn || stats.missileExhaustHeadDrawn
                            || cache->lastProjectileImageId != "DRAGON") return fail("original DRAGON body/directional resource missing or legacy smoke visible");
                        drewBody = true; drewLine = drewLine || (stats.projectileLineTrailSegments > 0 && stats.projectileLineTrailPixels > 0);
                        for (const auto& actor : actors) {
                            const auto trail = cache->projectileLineTrails.find(actor.actorId);
                            if (trail == cache->projectileLineTrails.end() || trail->second.image != "DRAGON"
                                || trail->second.color != std::array<int, 3>{216, 216, 255} || trail->second.decrement != 16
                                || std::abs(trail->second.head.x - actor.currentX) > 1e-8 || std::abs(trail->second.head.z - actor.currentLevel) > 1e-8)
                                return fail("DRAGON trail art or actual flight head differs");
                            if (type == "AEGIS") {
                                const auto original = ifvSamples.find({hz, rank});
                                if (original == ifvSamples.end()) return fail("real IFV reference shot missing");
                                // Only the renderer value copy changes appearance identity.
                                // Pose, launch offsets, direction, clock, owner and progress
                                // all remain those of this real Aegis flight sample.
                                auto comparedActor = actor;
                                comparedActor.projectileId = original->second.projectileId;
                                comparedActor.projectileImageId = original->second.projectileImageId;
                                comparedActor.weaponId = original->second.weaponId;
                                comparedActor.renderIntent = original->second.renderIntent;
                                compareActors.push_back(std::move(comparedActor));
                            }
                        }
                    }
                    if (type == "AEGIS") {
                        context.projectileActors = &compareActors; context.surfacePixels = compared.data();
                        std::fill(compared.begin(), compared.end(), background);
                        const auto compareStats = drawRuntimeMap(nullptr, viewport, isolated, context, camera, *compareCache);
                        if (pixels != compared || stats.projectileActorTravelDrawn != compareStats.projectileActorTravelDrawn
                            || stats.projectileLineTrailPixels != compareStats.projectileLineTrailPixels)
                            return fail("Aegis body/trail pixels differ from the same-pose real IFV appearance");
                        if (!actors.empty()) ++equivalences;
                    }
                    if (!actors.empty() && n == std::max(1, hz / 15) * 5) {
                        const auto frozen = pixels; std::map<int, std::size_t> nodes;
                        for (const auto& [id, trail] : cache->projectileLineTrails) nodes[id] = trail.points.size();
                        context.projectileActors = &actors; context.surfacePixels = pixels.data();
                        std::fill(pixels.begin(), pixels.end(), background);
                        (void)drawRuntimeMap(nullptr, viewport, isolated, context, camera, *cache);
                        if (frozen != pixels) return fail("same-tick pause repaint changed native pixels");
                        for (const auto& [id, trail] : cache->projectileLineTrails)
                            if (nodes.at(id) != trail.points.size()) return fail("paused repaint advanced trail nodes");
                    }
                }
                const bool liveEffects = std::any_of(s.impactEffects.begin(), s.impactEffects.end(),
                    [](const auto& impact) { return impact.attackerObjectId == 1; });
                if (stopped && s.projectiles.empty() && !s.combatDetails.hasPendingBurst(1) && !liveEffects
                    && cache->projectileLineTrails.empty()) { drained = true; break; }
            }
            stage = "natural flight/impact/tail completion";
            const auto* finalTarget = findRuntimeObjectByIdIncludingInactive(s, 2);
            if (!stopped || !drained || !drewBody || !drewLine || !sawHit || !finalTarget || finalTarget->health >= initialHealth
                || static_cast<int>(fired.size()) != expectedRounds || completed.size() != fired.size()
                || impactActors.size() != fired.size() || visibleImpactActors.size() != impactActors.size() || (type == "AEGIS" && !curved)
                || (direction == 0 && !savedFlight) || !stockRulesReady(s))
                return fail("incomplete stock volley/native display/real damage/drain: fired=" + std::to_string(fired.size())
                    + " complete=" + std::to_string(completed.size()) + " impacts=" + std::to_string(impactActors.size())
                    + " drain=" + std::to_string(drained) + " curve=" + std::to_string(curved) + " tick=" + std::to_string(s.tick));
            missiles += static_cast<int>(fired.size()); return true;
        }();
        if (scenePassed) { ++scenes; std::cout << "Aegis/IFV " << label << " PASS\n"; }
        else { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    }
    if (!failures.empty()) {
        std::ostringstream summary; summary << failures.size() << " Aegis/IFV scene(s) failed:";
        for (const auto& failure : failures) summary << '\n' << failure;
        error = summary.str(); return false;
    }
    std::cout << "Aegis/IFV presentation passed: scenes=" << scenes << " stockMissiles=" << missiles
        << " nativeDraws=" << draws << " pixelEquivalences=" << equivalences << " ordinarySaves=" << saves
        << " exactSnapshots=" << snapshots << " pairedTicks=" << paired << " nativeCaptures=" << captures
        << " UI/Burst/ROF/Medusa-authority/DRAGON-body-trail/pause/impact/natural-drain=ok\n";
    return true;
}
