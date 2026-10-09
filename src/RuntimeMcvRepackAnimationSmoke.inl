bool runRuntimeMcvRepackAnimationSmoke(const RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    std::string scene;
    const auto fail = [&](const std::string& why) {
        error = "MCV repack animation " + scene + ": " + why; return false;
    };
    const char* output = std::getenv("CONCORD_MCV_REPACK_ANIMATION_DIR");
    const std::string root = output && *output ? output : "build/smoke/mcv-repack-animation/checks";
    std::filesystem::create_directories(root);
    IniFile native; RulesDatabase rules;
    if (!native.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(native)) return fail("stock rules unavailable");
    const auto make = [&](const std::string& country, int hz, bool noCooldown) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = live.config; s.config.fixedTickHz = hz; s.config.noCooldown = noCooldown;
        s.config.playerCountry = country; s.config.playerSlots.clear(); s.config.mcvRepacks = true;
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.crates = s.config.shortGame = false;
        s.config.gameMode = "Battle"; s.config.gameModeRulesOverride.clear();
        s.rules = rules; s.art = live.art; s.theater = live.theater; s.report = live.report;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.economy.owner = country; s.economy.credits = 10000; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.runtimePreferencePersistenceEnabled = false;
        s.world = live.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y = 48; y <= 84; ++y) for (int x = 48; x <= 84; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance yard; yard.instanceId = 1; yard.type = runtimeConstructionYardForCountry(country);
        yard.kind = WorldObjectKind::Structure; yard.owner = country; yard.alive = true; yard.currentAmmo = -1;
        yard.x = yard.y = 64; yard.cell = runtimeCellKey(64, 64); yard.mission = "Guard";
        yard.facing = 96; yard.veterancy = 2; yard.veterancyExperience = 100;
        yard.health = runtimeMaxHealthForObject(s, yard) * 3 / 5;
        s.world.accessObjects().push_back(yard); s.selectedObjectIds = {1};
        initializeRuntimeNavigation(s); initializeRuntimeLocalProductionLanes(s.economy);
        s.structureFootprints = buildRuntimeStructureFootprints(s); rebuildRuntimeEconomyPower(s);
        return box;
    };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false);
    };
    const auto mobileIds = [](const RuntimeWindowState& s) {
        std::vector<int> ids;
        for (const auto& object : s.world.getObjects())
            if (object.alive && object.kind == WorldObjectKind::Unit
                && (object.type == "AMCV" || object.type == "SMCV" || object.type == "PCV")) ids.push_back(object.instanceId);
        return ids;
    };
    int cases = 0, renders = 0, reloads = 0, boundaryCases = 0;
    std::ofstream records(root + "/repack-frames.csv");
    records << "type,hz,no_cooldown,elapsed,total,expected_frame,structure_sprites,voxel_bodies\n";
    const auto draw = [&](RuntimeWindowState& s, const RuntimeBuildupTiming& timing,
        const std::string& buildup, const std::string& type, int elapsed, bool outputImage) {
        RuntimeSnapshotActiveStateScope active(s);
        const int expected = elapsed < timing.fixedTicks
            ? timing.frames - 1 - static_cast<int>(static_cast<std::int64_t>(elapsed)
                * timing.frames / timing.fixedTicks) : -1;
        s.renderCache.auxiliaryShpSpritesByImage.clear();
        std::vector<std::uint32_t> pixels;
        const std::string image = outputImage ? root + "/repack-" + type + "-" + std::to_string(elapsed) + ".bmp" : "";
        const auto stats = drawRuntimeStateOffscreenAtCell(s, {65, 65}, 640, 480, error, image,
            false, false, -1, &pixels);
        if (!stats || !stats->rendered) return fail("world render: " + error);
        if (outputImage) {
            for (const auto& object : s.world.getObjects()) if (object.alive) {
                std::cout << "Repack position " << type << " elapsed=" << elapsed
                    << " type=" << object.type << " cell=" << object.x << ',' << object.y
                    << " level=" << object.level << " render=" << object.hasRenderPosition
                    << ':' << object.renderX << ',' << object.renderY << ',' << object.renderLevel
                    << " moving=" << object.renderMoveActive << " mission=" << object.mission << '\n';
            }
            if (elapsed == timing.fixedTicks
                && !saveRuntimeState(s, root + "/repack-" + type + "-complete.ini", error))
                return fail("completion position artifact: " + error);
        }
        if (elapsed < timing.fixedTicks) {
            bool decoded = false; const std::string prefix = toLowerAsciiLocal(buildup) + "|";
            for (const auto& [key, frame] : s.renderCache.auxiliaryShpSpritesByImage)
                if (key.rfind(prefix, 0) == 0 && frame.loaded && frame.frameIndex == expected) decoded = true;
            if (!decoded || stats->visibleVoxelBodies != 0
                || (elapsed == timing.fixedTicks / 2 && stats->visibleStructureSprites == 0))
                return fail("wrong reverse frame or premature MCV: " + std::to_string(expected));
            if (stats->buildingArtAnimationLayers || stats->structureTurretAnimDrawn)
                return fail("active structure animation leaked into reverse buildup");
        } else if (stats->visibleStructureSprites != 0 || stats->visibleVoxelBodies != 1)
            return fail("completion did not replace the visible yard with one MCV");
        if (elapsed == timing.fixedTicks / 2) {
            const auto hash = s.world.deterministicSimulationStateHash(); const int tick = s.tick;
            std::vector<std::uint32_t> paused;
            if (!drawRuntimeStateOffscreenAtCell(s, {65, 65}, 640, 480, error, "", false, false, -1, &paused)
                || paused != pixels || s.tick != tick || s.world.deterministicSimulationStateHash() != hash)
                return fail("paused rendering advances or changes the repack frame");
        }
        records << type << ',' << s.config.fixedTickHz << ',' << s.config.noCooldown << ',' << elapsed << ','
            << timing.fixedTicks << ',' << expected << ',' << stats->visibleStructureSprites << ',' << stats->visibleVoxelBodies << '\n';
        ++renders; return true;
    };
    for (const char* country : {"Americans", "Russians", "YuriCountry"})
        for (int hz : {15, 30, 60}) for (bool noCooldown : {false, true}) {
        scene = std::string(country) + " hz=" + std::to_string(hz) + " noCooldown=" + std::to_string(noCooldown);
        auto box = make(country, hz, noCooldown); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
        const auto original = *findObjectById(s, 1); const auto* building = findRuntimeBuildingType(s.rules, original);
        const auto* art = runtimeArtTypeForObject(s, original); std::string source;
        const auto timing = runtimeStructureBuildupTiming(s, original, &source);
        if (!building || !art || timing.frames <= 1 || timing.fixedTicks < 3 || source.empty())
            return fail("original buildup asset unavailable");
        const int expected = std::max(1, timing.frames
            * static_cast<int>(s.rules.getProductionRules().buildupTime * 900.0 / timing.frames)) * (hz / 15);
        const int credits = s.economy.credits; int resultId = -1;
        if (timing.fixedTicks != expected || !undeployRuntimeStructure(s, 1, &resultId) || resultId != 1)
            return fail("repack start or original animation duration");
        if (undeployRuntimeStructure(s, 1) || !mobileIds(s).empty()) return fail("duplicate start creates an MCV");
        const bool outputImage = hz == 30 && !noCooldown;
        for (int elapsed = 0; elapsed <= timing.fixedTicks; ++elapsed) {
            const auto* yard = findObjectById(s, 1); const auto ids = mobileIds(s);
            if (elapsed < timing.fixedTicks) {
                if (!yard || !runtimeStructureIsRepacking(*yard) || yard->buildAnimTicksTotal != expected
                    || yard->buildAnimTicksRemaining != expected - elapsed || !ids.empty()
                    || yard->health != original.health || runtimeCellPointForObject(*yard) != RuntimeCellPoint{64, 64})
                    return fail("yard removed/moved or countdown ended early at " + std::to_string(elapsed));
            } else {
                if (yard || ids.size() != 1 || s.economy.credits != credits || s.selectedObjectIds != ids)
                    return fail("completion identity, selection or economy");
                const auto* mcv = findObjectById(s, ids.front());
                const int health = std::clamp(static_cast<int>(static_cast<long long>(original.health)
                    * runtimeMaxHealthForObject(s, *mcv) / runtimeMaxHealthForObject(s, original)), 1, runtimeMaxHealthForObject(s, *mcv));
                if (mcv->type != building->undeploysInto || mcv->health != health
                    || mcv->veterancy != original.veterancy || mcv->veterancyExperience != original.veterancyExperience)
                    return fail("completion lost type, health or veterancy");
            }
            if (elapsed == 0 || elapsed == expected / 2 || elapsed == expected - 1 || elapsed == expected)
                if (!draw(s, timing, art->buildup, original.type, elapsed, outputImage)) return false;
            if (elapsed == expected / 2) {
                std::ostringstream saved; IniFile ini; auto restored = make(country, hz, noCooldown);
                seedRuntimeLoadScratch(*restored, s, false);
                if (!writeRuntimeStateToStream(s, saved, error) || !ini.loadFromString(saved.str())
                    || !materializeRuntimeLoadStateFromIni(*restored, ini, error)) return fail("full mid-repack load: " + error);
                const auto* copy = findObjectById(*restored, 1);
                if (!copy || !runtimeStructureIsRepacking(*copy) || restored->tick != s.tick
                    || copy->buildAnimTicksTotal != expected || copy->buildAnimTicksRemaining != expected - elapsed)
                    return fail("save/load changed repack phase");
                if (!draw(*restored, timing, art->buildup, original.type, elapsed, false)) return false;
                for (int resumed = elapsed + 1; resumed <= expected; ++resumed) {
                    step(*restored);
                    if (resumed < expected && (!findObjectById(*restored, 1) || !mobileIds(*restored).empty()))
                        return fail("restored yard completed early");
                }
                if (findObjectById(*restored, 1) || mobileIds(*restored).size() != 1)
                    return fail("restored repack did not complete exactly once");
                if (outputImage && !saveRuntimeState(s, root + "/repack-" + original.type + "-mid.ini", error))
                    return fail("save artifact: " + error);
                ++reloads;
            }
            if (elapsed < expected) step(s);
        }
        for (int i = 0; i < 5; ++i) step(s);
        if (mobileIds(s).size() != 1) return fail("completion produced duplicate MCVs");
        ++cases;
        std::cout << "MCV repack " << scene << " frames=" << timing.frames << " ticks=" << expected << " reverse/render/save=ok\n";
    }
    for (const char* country : {"Americans", "Russians", "YuriCountry"}) {
        scene = std::string(country) + " command boundaries";
        {
            auto box = make(country, 30, false); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            const int total = runtimeStructureBuildupTiming(s, *findObjectById(s, 1)).fixedTicks;
            const RuntimeCellPoint target{74, 64}; issueRuntimeMoveCommand(s, target);
            if (s.lastCommand.acceptedCount != 1 || !findObjectById(s, 1)
                || !runtimeStructureIsRepacking(*findObjectById(s, 1))) return fail("move did not queue repack");
            for (int i = 1; i < total; ++i) {
                step(s); const auto* yard = findObjectById(s, 1);
                if (!yard || !mobileIds(s).empty() || runtimeCellPointForObject(*yard) != RuntimeCellPoint{64, 64})
                    return fail("queued move escaped before animation completed");
                if (i == total / 2) {
                    std::ostringstream saved; IniFile ini; auto restored = make(country, 30, false);
                    seedRuntimeLoadScratch(*restored, s, false);
                    if (!writeRuntimeStateToStream(s, saved, error) || !ini.loadFromString(saved.str())
                        || !materializeRuntimeLoadStateFromIni(*restored, ini, error))
                        return fail("deferred move full load: " + error);
                    const auto* copy = findObjectById(*restored, 1);
                    const auto pending = restored->behaviors.find(1);
                    if (!copy || !runtimeStructureIsRepacking(*copy) || copy->buildAnimTicksTotal != total
                        || copy->buildAnimTicksRemaining != total - i || restored->tick != s.tick
                        || pending == restored->behaviors.end() || pending->second.kind != RuntimeBehaviorKind::Move
                        || pending->second.moveTarget != target)
                        return fail("save/load lost deferred move or folding phase");
                    for (int elapsed = i + 1; elapsed <= total; ++elapsed) {
                        step(*restored);
                        if (elapsed < total && (!findObjectById(*restored, 1) || !mobileIds(*restored).empty()))
                            return fail("restored queued move escaped before animation completed");
                    }
                    const auto resumedIds = mobileIds(*restored);
                    if (findObjectById(*restored, 1) || resumedIds.size() != 1)
                        return fail("restored queued move repack completion");
                    bool resumedArrived = false;
                    for (int tick = 0; tick < 900; ++tick) {
                        const auto* mcv = findObjectById(*restored, resumedIds.front());
                        if (mcv && runtimeCellPointForObject(*mcv) == target && !mcv->hasRenderPosition) {
                            resumedArrived = true; break;
                        }
                        step(*restored);
                    }
                    if (!resumedArrived) return fail("restored MCV lost the saved destination");
                    ++reloads;
                }
            }
            step(s); const auto ids = mobileIds(s);
            if (ids.size() != 1 || findObjectById(s, 1)) return fail("queued move repack completion");
            bool arrived = false;
            for (int i = 0; i < 900; ++i) {
                const auto* mcv = findObjectById(s, ids.front());
                if (mcv && runtimeCellPointForObject(*mcv) == target && !mcv->hasRenderPosition) { arrived = true; break; }
                step(s);
            }
            if (!arrived) return fail("queued destination did not transfer to the MCV");
            ++boundaryCases;
        }
        for (bool guard : {false, true}) {
            auto box = make(country, 30, false); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            issueRuntimeMoveCommand(s, {74, 64});
            const auto* begun = findObjectById(s, 1);
            if (!begun || !runtimeStructureIsRepacking(*begun)) return fail("stop/guard fixture");
            const int total = begun->buildAnimTicksRemaining;
            for (int i = 0; i < total / 2; ++i) step(s);
            if (guard) issueRuntimeGuardCommand(s); else issueRuntimeStopCommand(s);
            const auto* stopped = findObjectById(s, 1);
            if (s.lastCommand.acceptedCount != 1 || !stopped || !runtimeStructureIsRepacking(*stopped)
                || stopped->buildAnimTicksRemaining != total - total / 2)
                return fail("stop/guard interrupted or restarted the reverse animation");
            for (int i = total / 2; i < total; ++i) step(s);
            const auto ids = mobileIds(s);
            if (ids.size() != 1 || findObjectById(s, 1)) return fail("stopped repack did not complete");
            const auto origin = runtimeCellPointForObject(*findObjectById(s, ids.front()));
            for (int i = 0; i < 90; ++i) step(s);
            const auto* mcv = findObjectById(s, ids.front());
            if (!mcv || runtimeCellPointForObject(*mcv) != origin || mcv->hasRenderPosition)
                return fail("stop/guard failed to clear the deferred move");
            ++boundaryCases;
        }
        {
            auto box = make(country, 30, false); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            if (!undeployRuntimeStructure(s, 1)) return fail("engineer repair fixture");
            const int total = findObjectById(s, 1)->buildAnimTicksRemaining;
            for (int i = 0; i < total / 2; ++i) step(s);
            const int remaining = findObjectById(s, 1)->buildAnimTicksRemaining;
            const int maximum = runtimeMaxHealthForObject(s, *findObjectById(s, 1));
            WorldObjectInstance engineer; engineer.instanceId = 2; engineer.kind = WorldObjectKind::Infantry;
            engineer.type = std::string(country) == "Americans" ? "ENGINEER"
                : std::string(country) == "Russians" ? "SENGINEER" : "YENGINEER";
            engineer.owner = country; engineer.alive = true; engineer.health = 75; engineer.mission = "Enter";
            engineer.x = 63; engineer.y = 64; engineer.cell = runtimeCellKey(63, 64);
            s.world.accessObjects().push_back(engineer); refreshRuntimeObjectOccupancyAfterLocalChange(s);
            s.pendingEngineerCaptures[2] = 1; s.pendingEngineerActions[2] = RuntimeEngineerAction::Repair;
            if (completeRuntimeEngineerCapture(s, 2)) return fail("adjacent engineer repaired before entering the yard");
            auto* arriving = findMutableObjectById(s, 2);
            if (!arriving) return fail("adjacent engineer disappeared");
            arriving->x = 64; arriving->cell = runtimeCellKey(64, 64);
            refreshRuntimeObjectOccupancyAfterLocalChange(s);
            if (!completeRuntimeEngineerCapture(s, 2)) return fail("engineer could not repair folding yard: " + s.simulationStatus);
            const auto* repaired = findObjectById(s, 1);
            if (!repaired || !runtimeStructureIsRepacking(*repaired) || repaired->buildAnimTicksTotal != total
                || repaired->buildAnimTicksRemaining != remaining || repaired->health != maximum
                || findObjectById(s, 2) || s.pendingEngineerCaptures.contains(2) || s.pendingEngineerActions.contains(2))
                return fail("engineer repair changed folding phase or failed to consume the engineer");
            for (int i = 0; i < remaining; ++i) step(s);
            const auto ids = mobileIds(s);
            if (findObjectById(s, 1) || ids.size() != 1) return fail("engineer-repaired yard did not complete exactly once");
            const auto* mcv = findObjectById(s, ids.front());
            if (!mcv || mcv->health != runtimeMaxHealthForObject(s, *mcv))
                return fail("MCV did not inherit the engineer-repaired health");
            ++boundaryCases;
        }
        {
            auto box = make(country, 30, false); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            if (!undeployRuntimeStructure(s, 1)) return fail("deselection fixture");
            const int total = findObjectById(s, 1)->buildAnimTicksRemaining; s.selectedObjectIds.clear();
            for (int i = 0; i <= total; ++i) step(s);
            if (mobileIds(s).size() != 1 || !s.selectedObjectIds.empty()) return fail("completion steals cleared selection");
            ++boundaryCases;
        }
        {
            auto box = make(country, 30, false); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            if (!undeployRuntimeStructure(s, 1)) return fail("death fixture");
            const int total = findObjectById(s, 1)->buildAnimTicksRemaining;
            for (int i = 0; i < total / 2; ++i) step(s);
            RuntimeObjectExitRequest request; request.objectId = 1; request.kind = RuntimeObjectExitKind::Destroyed;
            request.tick = s.tick; request.suppressCrew = request.suppressDeathWeapon = true;
            (void)finalizeRuntimeObjectExit(s, request);
            for (int i = 0; i <= total; ++i) step(s);
            if (findObjectById(s, 1) || !mobileIds(s).empty()) return fail("destroyed repacking yard created an MCV");
            ++boundaryCases;
        }
        {
            auto box = make(country, 30, false); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
            if (!undeployRuntimeStructure(s, 1)) return fail("blocked fixture");
            const int total = findObjectById(s, 1)->buildAnimTicksRemaining; int id = 100;
            for (int y = 58; y <= 70; ++y) for (int x = 58; x <= 70; ++x) {
                WorldObjectInstance wall; wall.instanceId = id++; wall.type = "GAWALL";
                wall.kind = WorldObjectKind::Structure; wall.owner = "Neutral"; wall.alive = true;
                wall.health = 100; wall.x = x; wall.y = y; wall.cell = runtimeCellKey(x, y); wall.mission = "Guard";
                s.world.accessObjects().push_back(wall);
            }
            refreshRuntimeObjectOccupancyAfterLocalChange(s);
            for (int i = 0; i < total + 3; ++i) step(s);
            const auto* blocked = findObjectById(s, 1);
            if (!blocked || !runtimeStructureIsRepacking(*blocked) || blocked->buildAnimTicksRemaining != 1
                || !mobileIds(s).empty()) return fail("blocked completion lost, duplicated or reopened the yard");
            for (auto& object : s.world.accessObjects()) if (object.instanceId >= 100) object.alive = false;
            refreshRuntimeObjectOccupancyAfterLocalChange(s);
            for (int i = 0; i < 3; ++i) step(s);
            if (findObjectById(s, 1) || mobileIds(s).size() != 1) return fail("unblocked completion does not produce exactly one MCV");
            ++boundaryCases;
        }
    }
    std::cout << "MCV repack animation passed: " << cases << " three-faction timing/no-cooldown cases, "
        << renders << " original-asset renders, " << reloads << " full reloads, " << boundaryCases
        << " move/stop/guard/engineer-repair/deselection/death/blockage boundaries; 15/30/60 Hz\n";
    return true;
}
