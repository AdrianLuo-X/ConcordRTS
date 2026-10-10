bool runRuntimeRepairDepotActivitySmoke(const RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    std::string scene;
    const auto fail = [&](const std::string& why) {
        error = "repair depot activity " + scene + ": " + why; return false;
    };
    const char* output = std::getenv("CONCORD_REPAIR_DEPOT_ACTIVITY_DIR");
    const std::string root = output && *output ? output
        : "build/smoke/repair-depot-animation/checks";
    std::filesystem::create_directories(root);
    IniFile native; RulesDatabase rules;
    if (!native.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(native))
        return fail("stock rules unavailable");
    const auto make = [&](const std::string& depot, const std::string& vehicle, int hz, bool damaged) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = live.config; s.config.fixedTickHz = hz; s.config.noCooldown = false;
        s.config.playerCountry = depot == "GADEPT" ? "Americans" : "Russians";
        s.config.playerSlots.clear(); s.config.computerAiEnabled = s.config.multiplayerActive = false;
        s.config.crates = s.config.shortGame = false; s.config.gameMode = "Battle";
        s.config.gameModeRulesOverride.clear(); s.rules = rules; s.art = live.art;
        s.theater = live.theater; s.report = live.report;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.economy.owner = s.config.playerCountry; s.economy.credits = 10000;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        s.world = live.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y = 48; y <= 84; ++y) for (int x = 48; x <= 84; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
            s.terrainInfoByCell.emplace(cell.cellIndex512, RuntimeTerrainInfo{});
        }
        WorldObjectInstance host; host.instanceId = 1; host.kind = WorldObjectKind::Structure;
        host.type = depot; host.owner = s.economy.owner; host.alive = true; host.mission = "Guard";
        host.x = host.y = 64; host.cell = runtimeCellKey(64, 64); host.poweredOperational = true;
        host.health = runtimeMaxHealthForObject(s, host) / (damaged ? 3 : 1);
        auto actor = host; actor.instanceId = 2; actor.kind = WorldObjectKind::Unit; actor.type = vehicle;
        actor.x = 60; actor.y = 66; actor.cell = runtimeCellKey(actor.x, actor.y); actor.facing = 64;
        actor.currentAmmo = -1; actor.health = std::max(1, runtimeMaxHealthForObject(s, actor) - 64);
        auto power = host; power.instanceId = 100; power.type = depot == "GADEPT" ? "GAPOWR" : "NAPOWR";
        power.x = power.y = 79; power.cell = runtimeCellKey(79, 79); power.health = runtimeMaxHealthForObject(s, power);
        s.world.accessObjects() = {host, actor, power}; s.selectedObjectIds = {2};
        s.terrainByCell = buildRuntimeTerrainLookup(s.world);
        s.structureFootprints = buildRuntimeStructureFootprints(s); initializeRuntimeNavigation(s);
        initializeRuntimeLocalProductionLanes(s.economy); rebuildRuntimeEconomyPower(s);
        ensureRuntimeRepairDepotsInitialized(s); return box;
    };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false);
    };
    const auto phase = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); refreshRuntimeSpecialMechanismRenderSnapshots(s);
        for (const auto& snapshot : s.specialMechanismRenderSnapshots)
            if (snapshot.kind == "repairDepot" && snapshot.objectId == 1)
                return std::pair<int, int>{snapshot.resourceId == "SpecialAnim" ? 1
                    : snapshot.resourceId == "SpecialAnimTwo" ? 2
                    : snapshot.resourceId == "SpecialAnimThree" ? 3 : -1, snapshot.progress};
        return std::pair<int, int>{0, 0};
    };
    const auto poseIsNative = [](const RuntimeWindowState& s, const std::string& depot) {
        const auto* actor = findObjectById(s, 2);
        const double expectedX = depot == "GADEPT" ? 65.5 : 66.5;
        return actor && actor->dockedToObjectId == 1 && actor->dockIndex == 0
            && actor->x == static_cast<int>(expectedX) && actor->y == 65
            && actor->hasRenderPosition && !actor->renderMoveActive
            && std::abs(actor->renderX - expectedX) < 0.000001
            && std::abs(actor->renderY - 65.5) < 0.000001
            && std::abs(actor->renderLevel - actor->level) < 0.000001;
    };
    int cases = 0, captures = 0, reloads = 0, legacyReloads = 0, ownerCaptures = 0;
    std::ofstream records(root + "/repair-depot-frames.csv");
    records << "depot,vehicle,hz,damaged,tick,phase,age,frame,image,health,cell_x,cell_y,render_x,render_y\n";
    for (const std::string depot : {"GADEPT", "NADEPT"}) {
        struct Case { const char* vehicle; int hz; bool damaged; };
        for (const auto item : {Case{"MTNK", 30, false}, Case{"HTNK", 30, false},
                Case{"FV", 30, false}, Case{"MTNK", 15, false}, Case{"MTNK", 60, false},
                Case{"MTNK", 30, true}}) {
            scene = depot + " " + item.vehicle + " hz=" + std::to_string(item.hz)
                + " damaged=" + std::to_string(item.damaged);
            auto box = make(depot, item.vehicle, item.hz, item.damaged); auto& s = *box;
            const auto* hostArt = runtimeArtTypeForObject(s, *findObjectById(s, 1));
            if (!hostArt || hostArt->specialAnimations.size() < 3) return fail("native repair ART phases missing");
            const int initialHealth = findObjectById(s, 2)->health;
            const int maxHealth = runtimeMaxHealthForObject(s, *findObjectById(s, 2));
            const std::string tag = depot + "-" + item.vehicle + "-" + std::to_string(item.hz)
                + (item.damaged ? "-damaged" : "");
            const bool outputImages = item.hz == 30 && std::string(item.vehicle) == "MTNK";
            std::array<int, 4> firstFrames{-1, -1, -1, -1};
            std::array<bool, 4> sampled{};
            const auto draw = [&](RuntimeWindowState& probe, const std::string& suffix,
                    std::vector<std::uint32_t>* pixels = nullptr) -> std::optional<RuntimeMapRenderStats> {
                RuntimeSnapshotActiveStateScope active(probe);
                return drawRuntimeStateOffscreenAtCell(probe, {66, 65}, 800, 600, error,
                    outputImages ? root + "/" + tag + "-" + suffix + ".bmp" : "",
                    false, false, -1, pixels);
            };
            const auto inspect = [&](RuntimeWindowState& probe, int expectedPhase, int age,
                    const std::string& suffix, bool checkPaused) {
                probe.renderCache.auxiliaryShpSpritesByImage.clear();
                std::vector<std::uint32_t> pixels;
                const auto stats = draw(probe, suffix, &pixels);
                if (!stats || !stats->rendered) return fail("world render: " + error);
                if (stats->repairDepotAnimationPhase != expectedPhase
                    || stats->repairDepotAnimationLayers != (expectedPhase == 0 ? 0 : 1))
                    return fail("repair phases were absent or overlaid: expected " + std::to_string(expectedPhase)
                        + " actual=" + std::to_string(stats->repairDepotAnimationPhase)
                        + " layers=" + std::to_string(stats->repairDepotAnimationLayers));
                bool idleDecoded = false;
                for (const auto& layer : hostArt->activeAnimations) if (layer.key == "IdleAnim") {
                    const std::string imageId = item.damaged && !layer.damagedImage.empty()
                        ? layer.damagedImage : layer.image;
                    const std::string prefix = toLowerAsciiLocal(imageId) + "|";
                    for (const auto& [cacheKey, frame] : probe.renderCache.auxiliaryShpSpritesByImage)
                        if (cacheKey.rfind(prefix, 0) == 0 && frame.loaded) idleDecoded = true;
                }
                if (idleDecoded != (expectedPhase == 0))
                    return fail("native IdleAnim did not exclusively replace the repair arm");
                if (expectedPhase > 0) {
                    const char* key = expectedPhase == 1 ? "SpecialAnim" : expectedPhase == 2 ? "SpecialAnimTwo" : "SpecialAnimThree";
                    const ArtType::ActiveAnimation* authored = nullptr;
                    for (const auto& layer : hostArt->specialAnimations) if (layer.key == key) authored = &layer;
                    if (!authored) return fail("native phase layer lookup");
                    const std::string imageId = item.damaged && !authored->damagedImage.empty()
                        ? authored->damagedImage : authored->image;
                    const auto* anim = probe.art.findArtType(imageId);
                    if (!anim) return fail("native phase ART: " + imageId);
                    // Stock authored ranges are checked independently of the frame selector.
                    const int lower = anim->hasAnimationLoopStart && expectedPhase == 2
                        ? anim->animationLoopStart : anim->hasAnimationStart ? anim->animationStart : 0;
                    const int upper = expectedPhase == 2 ? anim->animationLoopEnd
                        : lower + anim->animationEnd - 1;
                    if (stats->repairDepotAnimationImage != imageId
                        || stats->repairDepotAnimationFrame < lower || stats->repairDepotAnimationFrame > upper
                        || stats->buildingSpecialAnimationLayers != 1)
                        return fail("wrong native phase image/range: " + stats->repairDepotAnimationImage
                            + " frame=" + std::to_string(stats->repairDepotAnimationFrame));
                }
                const auto* actor = findObjectById(probe, 2);
                if (actor->dockedToObjectId == 1 && (!poseIsNative(probe, depot)
                        || stats->repairDepotForegroundObjects != 1
                        || (expectedPhase > 0 && stats->repairDepotAnimationForegroundLayers != 1)))
                    return fail("docked render pose/depth or foreground repair arm changed");
                records << depot << ',' << item.vehicle << ',' << item.hz << ',' << item.damaged << ','
                    << probe.tick << ',' << expectedPhase << ',' << age << ',' << stats->repairDepotAnimationFrame << ','
                    << stats->repairDepotAnimationImage << ',' << actor->health << ',' << actor->x << ',' << actor->y
                    << ',' << actor->renderX << ',' << actor->renderY << '\n';
                if (checkPaused) {
                    const int tick = probe.tick; const auto worldHash = probe.world.deterministicSimulationStateHash();
                    const auto repairHash = probe.repairDepots.stateHash(); std::vector<std::uint32_t> paused;
                    if (!draw(probe, suffix + "-paused", &paused) || pixels != paused || probe.tick != tick
                        || probe.world.deterministicSimulationStateHash() != worldHash
                        || probe.repairDepots.stateHash() != repairHash) return fail("paused render advanced repair/animation");
                }
                ++captures; return true;
            };
            if (phase(s).first != 0) return fail("idle depot started an arm cycle without a vehicle");
            if (!inspect(s, 0, 0, "idle-before", false)) return false;
            {
                RuntimeSnapshotActiveStateScope active(s);
                if (runtimeTargetCursorIntent(s, *findObjectById(s, 1)) != RuntimeTargetCursorIntent::Enter
                    || !dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                        runtimeCellPointForObject(*findObjectById(s, 1)), findObjectById(s, 1))
                    || s.lastCommand.verb != "unitRepair" || s.lastCommand.acceptedCount != 1)
                    return fail("native Enter command was not accepted");
            }
            const auto* reserved = findRuntimeRepairDepotRelationForUnit(s.repairDepots, 2);
            if (!reserved || reserved->dockCell.x != (depot == "GADEPT" ? 65 : 66) || reserved->dockCell.y != 65)
                return fail("stock dock reservation changed logical cell");
            bool moved = false, docked = false, repaired = false, completed = false;
            for (int elapsed = 0; elapsed < item.hz * 90; ++elapsed) {
                const auto* actor = findObjectById(s, 2);
                if (!actor) return fail("vehicle disappeared");
                moved = moved || actor->x != 60 || actor->y != 66 || actor->renderMoveActive;
                if (actor->dockedToObjectId == 1) {
                    docked = true;
                    if (!moved || !poseIsNative(s, depot)) return fail("real arrival lost the fractional stock dock position");
                }
                repaired = repaired || actor->health > initialHealth;
                const auto [currentPhase, age] = phase(s);
                if (currentPhase < 0 || currentPhase > 3) return fail("invalid repair animation phase");
                if (currentPhase > 0) {
                    const int sampleAge = (depot == "GADEPT" ? 8 : 4) * item.hz / 30 * 2;
                    if (firstFrames[currentPhase] < 0) {
                        if (!inspect(s, currentPhase, age, "phase-" + std::to_string(currentPhase) + "-start", false)) return false;
                        const auto stats = draw(s, "phase-" + std::to_string(currentPhase) + "-first");
                        if (!stats) return fail("first phase frame");
                        firstFrames[currentPhase] = stats->repairDepotAnimationFrame;
                    } else if (!sampled[currentPhase] && age >= sampleAge) {
                        if (!inspect(s, currentPhase, age, "phase-" + std::to_string(currentPhase) + "-moving", true)) return false;
                        const auto stats = draw(s, "phase-" + std::to_string(currentPhase) + "-second");
                        if (!stats || stats->repairDepotAnimationFrame == firstFrames[currentPhase])
                            return fail("native repair arm frame did not advance");
                        std::ostringstream saved; IniFile ini; auto restored = std::make_unique<RuntimeWindowState>();
                        seedRuntimeLoadScratch(*restored, s, false);
                        if (!writeRuntimeStateToStream(s, saved, error) || !ini.loadFromString(saved.str())
                            || !materializeRuntimeLoadStateFromIni(*restored, ini, error)) return fail("phase load: " + error);
                        if (phase(*restored) != std::pair<int, int>{currentPhase, age}
                            || (actor->dockedToObjectId == 1 && !poseIsNative(*restored, depot)))
                            return fail("load reset animation or fractional dock pose");
                        const auto loaded = draw(*restored, "phase-" + std::to_string(currentPhase) + "-loaded");
                        if (!loaded || loaded->repairDepotAnimationPhase != stats->repairDepotAnimationPhase
                            || loaded->repairDepotAnimationFrame != stats->repairDepotAnimationFrame
                            || loaded->repairDepotAnimationImage != stats->repairDepotAnimationImage)
                            return fail("loaded repair frame differs from saved frame");
                        ++reloads; sampled[currentPhase] = true;
                        if (currentPhase == 2 && item.hz == 30 && std::string(item.vehicle) == "MTNK" && !item.damaged) {
                            clearRuntimeRenderPosition(*findMutableObjectById(*restored, 2));
                            std::ostringstream legacy; IniFile legacyIni; auto recovered = std::make_unique<RuntimeWindowState>();
                            seedRuntimeLoadScratch(*recovered, *restored, false);
                            if (!writeRuntimeStateToStream(*restored, legacy, error) || !legacyIni.loadFromString(legacy.str())
                                || !materializeRuntimeLoadStateFromIni(*recovered, legacyIni, error)
                                || !poseIsNative(*recovered, depot)) return fail("legacy docked pose recovery: " + error);
                            for (int resume = 0; resume < item.hz * 60 && findObjectById(*recovered, 2)->health < maxHealth; ++resume)
                                step(*recovered);
                            if (findObjectById(*recovered, 2)->health != maxHealth) return fail("reloaded service did not complete");
                            ++legacyReloads;
                        }
                    }
                }
                if (docked && actor->health == maxHealth && actor->dockedToObjectId < 0 && currentPhase == 0
                    && !findRuntimeRepairDepotRelationForUnit(s.repairDepots, 2)) {
                    completed = true; break;
                }
                step(s);
            }
            if (!moved || !docked || !repaired || !completed || s.economy.credits >= 10000
                || !sampled[1] || !sampled[2] || !sampled[3])
                return fail("real movement/service/phase completion incomplete: moved=" + std::to_string(moved)
                    + " docked=" + std::to_string(docked) + " repaired=" + std::to_string(repaired)
                    + " complete=" + std::to_string(completed) + " phases=" + std::to_string(sampled[1])
                    + std::to_string(sampled[2]) + std::to_string(sampled[3]) + " status=" + s.simulationStatus);
            if (!inspect(s, 0, 0, "idle-after", true)) return false;
            for (int tick = 0; tick < item.hz * 5; ++tick) step(s);
            if (phase(s).first != 0 || findObjectById(s, 2)->dockedToObjectId >= 0)
                return fail("completed repair arm restarted without a vehicle");
            ++cases; std::cout << "Repair depot activity " << scene << " real-path/native-pose/three-phase/save=ok\n";
        }
    }
    // A real capture atomically transfers a docked vehicle. The owner's pose
    // cleanup must restore its fractional dock anchor before any next tick.
    for (const std::string depot : {"GADEPT", "NADEPT"}) {
        scene = depot + " immediate capture/save boundary";
        auto box = make(depot, "MTNK", 30, false); auto& s = *box;
        findMutableObjectById(s, 2)->health = 1;
        {
            RuntimeSnapshotActiveStateScope active(s);
            if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                    runtimeCellPointForObject(*findObjectById(s, 1)), findObjectById(s, 1)))
                return fail("capture-boundary Enter command");
        }
        for (int i = 0; i < 1800 && phase(s).first != 2; ++i) step(s);
        if (phase(s).first != 2 || !poseIsNative(s, depot)) return fail("capture-boundary actual arrival/work");
        for (int i = 0; i < 16; ++i) step(s);
        const auto beforePhase = phase(s);
        const int beforeTick = s.tick;
        const int beforeHealth = findObjectById(s, 2)->health;
        const std::string oldOwner = findObjectById(s, 1)->owner;
        const std::string newOwner = oldOwner == "Americans" ? "Russians" : "Americans";
        {
            RuntimeSnapshotActiveStateScope active(s);
            const auto transition = applyRuntimeOwnerTransitionBatch(s,
                {{1, oldOwner, newOwner}}, RuntimeOwnerTransitionRepairPolicy::CaptureDockedUnits,
                "repair depot immediate capture pose smoke");
            if (!transition.applied || transition.changedObjectIds != std::vector<int>({1, 2}))
                return fail("real capture did not transfer depot and docked tank: " + transition.error);
        }
        if (s.tick != beforeTick || findObjectById(s, 1)->owner != newOwner
            || findObjectById(s, 2)->owner != newOwner || !poseIsNative(s, depot)
            || findObjectById(s, 2)->health != beforeHealth || phase(s) != beforePhase)
            return fail("capture changed the dock pose/animation before the next tick");
        std::ostringstream saved; IniFile ini; auto restored = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*restored, s, false);
        if (!writeRuntimeStateToStream(s, saved, error) || !ini.loadFromString(saved.str())
            || !materializeRuntimeLoadStateFromIni(*restored, ini, error)) return fail("captured phase load: " + error);
        if (restored->tick != beforeTick || findObjectById(*restored, 1)->owner != newOwner
            || findObjectById(*restored, 2)->owner != newOwner || !poseIsNative(*restored, depot)
            || findObjectById(*restored, 2)->health != beforeHealth || phase(*restored) != beforePhase)
            return fail("immediate capture save/load lost the native dock pose or phase");
        ++ownerCaptures;
    }
    // Exercise service availability through real power-grid and player-command changes.
    {
        scene = "GADEPT service interruption boundaries";
        auto box = make("GADEPT", "MTNK", 30, false); auto& s = *box;
        findMutableObjectById(s, 2)->health = 1;
        {
            RuntimeSnapshotActiveStateScope active(s);
            if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                    runtimeCellPointForObject(*findObjectById(s, 1)), findObjectById(s, 1)))
                return fail("boundary Enter command");
        }
        const auto waitForPhase = [&](int wanted, int limit) {
            for (int i = 0; i < limit && phase(s).first != wanted; ++i) step(s);
            return phase(s).first == wanted;
        };
        const auto captureBoundary = [&](const char* suffix) {
            RuntimeSnapshotActiveStateScope active(s);
            return drawRuntimeStateOffscreenAtCell(s, {66, 65}, 800, 600, error,
                root + "/GADEPT-boundary-" + suffix + ".bmp", false);
        };
        if (!waitForPhase(2, 1800) || !poseIsNative(s, "GADEPT")) return fail("boundary actual arrival/work");
        for (int i = 0; i < 16; ++i) step(s);
        const auto active = captureBoundary("working");
        if (!active || active->repairDepotAnimationPhase != 2 || active->repairDepotAnimationLayers != 1)
            return fail("boundary working layer");
        const auto originalPower = *findObjectById(s, 100);
        findMutableObjectById(s, 100)->alive = false;
        refreshRuntimeObjectOccupancyAfterLocalChange(s); rebuildRuntimeEconomyPower(s);
        if (findObjectById(s, 1)->poweredOperational) return fail("power plant removal did not disable repair power");
        const auto frozenPhase = phase(s); const int frozenHealth = findObjectById(s, 2)->health;
        const auto frozen = captureBoundary("power-lost");
        for (int i = 0; i < 60; ++i) step(s);
        const auto later = captureBoundary("power-paused");
        if (phase(s) != frozenPhase || findObjectById(s, 2)->health != frozenHealth
            || !poseIsNative(s, "GADEPT") || !frozen || !later
            || frozen->repairDepotAnimationPhase != 2 || later->repairDepotAnimationPhase != 2
            || frozen->repairDepotAnimationFrame != later->repairDepotAnimationFrame
            || later->repairDepotAnimationLayers != 1) return fail("power loss failed to freeze the visible arm/service");
        bool powerRetained = false;
        for (auto& object : s.world.accessObjects()) if (object.instanceId == 100) {
            object = originalPower; powerRetained = true; break;
        }
        if (!powerRetained) s.world.accessObjects().push_back(originalPower);
        refreshRuntimeObjectOccupancyAfterLocalChange(s); rebuildRuntimeEconomyPower(s);
        for (int i = 0; i < 16; ++i) step(s);
        const auto resumed = captureBoundary("power-resumed");
        if (!findObjectById(s, 1)->poweredOperational || !resumed || resumed->repairDepotAnimationPhase != 2
            || resumed->repairDepotAnimationFrame == later->repairDepotAnimationFrame)
            return fail("power restoration failed to continue the arm frame");
        s.economy.credits = 0; const int unpaidHealth = findObjectById(s, 2)->health;
        if (!waitForPhase(3, 90)) return fail("unaffordable service did not retract the arm");
        const auto poor = captureBoundary("out-of-money-retract");
        if (!poor || poor->repairDepotAnimationPhase != 3 || !waitForPhase(0, 240)
            || findObjectById(s, 2)->health != unpaidHealth || !poseIsNative(s, "GADEPT"))
            return fail("unaffordable service did not remain docked and idle");
        const auto idle = captureBoundary("out-of-money-idle");
        if (!idle || idle->repairDepotAnimationLayers != 0) return fail("unaffordable idle kept an active arm");
        s.economy.credits = 10000;
        if (!waitForPhase(1, 90) || !waitForPhase(2, 240)) return fail("funding did not restart extend/work sequence");
        {
            RuntimeSnapshotActiveStateScope command(s); s.selectedObjectIds = {2};
            issueRuntimeMoveCommand(s, {72, 71});
        }
        if (s.lastCommand.acceptedCount != 1 || findObjectById(s, 2)->dockedToObjectId >= 0
            || !waitForPhase(3, 5)) return fail("player movement did not cancel service/retract");
        const auto cancelled = captureBoundary("cancelled-retract");
        if (!cancelled || cancelled->repairDepotAnimationPhase != 3 || !waitForPhase(0, 240))
            return fail("cancelled service did not finish retraction");
        for (int i = 0; i < 900 && runtimeCellPointForObject(*findObjectById(s, 2)) != RuntimeCellPoint{72, 71}; ++i)
            step(s);
        if (runtimeCellPointForObject(*findObjectById(s, 2)) != RuntimeCellPoint{72, 71}
            || findRuntimeRepairDepotRelationForUnit(s.repairDepots, 2) || phase(s).first != 0)
            return fail("cancelled tank failed to leave for the player destination");
        std::cout << "Repair depot boundaries: real power loss/restoration, empty/refilled credits and player move cancellation passed\n";
    }
    std::cout << "Repair depot activity: " << cases << " native scenes; " << captures << " phase captures; "
        << reloads << " animation reloads; " << legacyReloads << " old dock pose recovery/continuations; "
        << ownerCaptures << " immediate capture/save pose boundaries passed\n";
    return true;
}
