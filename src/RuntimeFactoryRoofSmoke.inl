bool runRuntimeFactoryRoofSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) { error = "factory roof: " + message; return false; };
    const char* output = std::getenv("CONCORD_FACTORY_ROOF_OUTPUT");
    const std::string root = output ? output : "";
    if (!root.empty()) std::filesystem::create_directories(root);
    int scenarios = 0, continuousFrames = 0;
    for (const auto& pair : std::array<std::pair<const char*, const char*>, 4>{{
            {"GAWEAP", "SHAD"}, {"NAWEAP", "ZEP"}, {"NAWEAP", "SCHP"}, {"YAWEAP", "DISK"}}}) {
        const char* factoryType = pair.first;
        for (int variant = 0; variant < 9; ++variant) {
            const int producer = variant % 3;
            const int hz = std::array<int, 3>{15, 30, 60}[variant / 3];
            auto storage = std::make_unique<RuntimeWindowState>();
            auto& s = *storage;
            seedRuntimeLoadScratch(s, live, false);
            s.config.gameSpeed = producer == 0 ? 0 : producer == 1 ? 3 : 6;
            s.config.fixedTickHz = hz;
            s.config.computerAiEnabled = false;
            s.runtimeSuppressPlayerAudioFeedback = true;
            s.runtimeSoundsEnabled = false;
            s.economy.owner = "ProbeA";
            s.economy.credits = 100000;
            initializeRuntimeLocalProductionLanes(s.economy);
            s.economy.localProductionLanes.nextSerial = 2;
            s.factoryDeliveries.reset();
            s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear();
            s.world.accessTerrainCells().clear(); s.world.accessSmudges().clear();
            for (int y = 40; y <= 88; ++y) for (int x = 40; x <= 88; ++x) {
                TerrainCell cell; cell.x = x; cell.y = y;
                cell.cellIndex512 = runtimeCellKey(x, y); cell.tileIndex = 0;
                s.world.accessTerrainCells().push_back(cell);
            }
            WorldObjectInstance factory;
            factory.instanceId = 1; factory.kind = WorldObjectKind::Structure;
            factory.type = factoryType; factory.owner = "ProbeA";
            factory.x = factory.y = 64; factory.cell = runtimeCellKey(64,64);
            factory.health = producer == 1 ? 250 : 1000;
            factory.alive = true; factory.poweredOperational = true;
            factory.mission = "Guard";
            clearRuntimeRenderPosition(factory);
            s.world.accessObjects().push_back(factory);
            initializeRuntimeNavigation(s);
            s.structureFootprints = buildRuntimeStructureFootprints(s);
            RuntimeBuildCatalogItem item;
            item.kind = RuntimeBuildKind::Unit; item.typeId = pair.second;
            item.requiredFactory = "UnitType";
            s.tick = 100;
            RuntimeComputerAiState ai; ai.owner = "ProbeA";
            RuntimeCampaignHouseProductionState house; house.owner = "ProbeA";
            RuntimeCampaignTeamBuild team;
            const bool built = producer == 0 ? completeRuntimeProductionOrder(s, item, false,
                    RuntimeProductionLane::Vehicles, 1)
                : producer == 1 ? runtimeComputerAiBuildUnit(s, ai, item)
                : completeRuntimeCampaignAiUnit(s, house, item, team).has_value();
            if (!built || s.world.getObjects().size() != 2) return fail(std::string(factoryType) + " did not produce air unit");
            const int producedId = s.world.getObjects().back().instanceId;
            auto* roof = findMutableObjectById(s, 1);
            auto* unit = findMutableObjectById(s, producedId);
            if (!roof || !unit || !roof->factoryExitAirborne || unit->factoryExitHostId != 1)
                return fail("air unit did not retain its producing factory");
            if (unit->facing != runtimeFacingFromDelta(1, 0)) return fail("initial facing does not match factory axis");
            const RuntimeCellPoint roofCell{66,65};
            if (runtimeCellPointForObject(*unit) != roofCell || unit->flightLevel != roof->level)
                return fail("aircraft was not born on the factory floor below the hatch");
            if (runtimeFactoryAirExitSpawnCell(s, *roof)) return fail("occupied roof accepted another delivery");
            const int start = roof->factoryExitAnimationStartTick;
            const auto initialBehavior = s.behaviors.find(producedId);
            if (initialBehavior == s.behaviors.end()) return fail("missing exit move");
            const auto exitGoal = initialBehavior->second.moveTarget;
            std::cout << "Factory roof setup " << factoryType << " producer=" << producer
                << " spawn=" << unit->x << ',' << unit->y
                << " goal=" << (initialBehavior == s.behaviors.end() ? -1 : initialBehavior->second.moveTarget.x)
                << ',' << (initialBehavior == s.behaviors.end() ? -1 : initialBehavior->second.moveTarget.y) << '\n';
            const auto* factoryArt = runtimeArtTypeForObject(s, *roof);
            if (!factoryArt) return fail("factory art missing");
            const auto capture = [&](const std::string& stage) {
                if (producer > 1 || hz != 30 || root.empty()) return true;
                const auto stats = drawRuntimeStateOffscreenAtCell(s, {66,65}, 800, 600, error,
                    root + "/" + factoryType + "-" + pair.second + "-" + std::to_string(producer) + "-" + stage + ".bmp", false, false);
                return stats && stats->rendered && stats->aircraftBuildingClearanceAdjustments == 0
                    && (stage != "opening" || stats->airborneForegroundBodiesDrawn == 0)
                    && (stage == "closed"
                    ? stats->buildingFactoryAirRoofAnimationLayers == 0
                    : stats->buildingFactoryAirRoofAnimationLayers >= 2);
            };
            if (!capture("opening")) return fail("opening capture: " + error);
            const int openingTicks = runtimeFactoryAirExitCloseTicks(s, *roof);
            for (int step = 1; step < openingTicks; ++step) {
                ++s.tick;
                advanceRuntimeJumpjetFlights(s);
                advanceRuntimeMoveBehavior(s, *unit, s.behaviors.at(producedId));
                advanceRuntimeFactoryExitAnimations(s);
                if (runtimeCellPointForObject(*unit) != roofCell
                    || runtimeObjectRenderX(*unit) != roofCell.x || runtimeObjectRenderY(*unit) != roofCell.y
                    || unit->flightLevel != roof->level)
                    return fail("aircraft moved or climbed through a closed roof");
            }
            ++s.tick;
            advanceRuntimeJumpjetFlights(s);
            advanceRuntimeFactoryExitAnimations(s);
            if (unit->renderLevel <= roof->level || runtimeFactoryAirExitReadyToMove(s, *unit))
                return fail("opening did not start a vertical climb before horizontal flight");
            for (int step = 0; step < 250; ++step) {
                ++s.tick;
                processRuntimePathRequests(s);
                advanceRuntimeFactoryExitAnimations(s);
                if (s.behaviors.at(producedId).moveTarget != exitGoal)
                    return fail("exit path was redirected over the factory");
                roof = findMutableObjectById(s, 1);
                if (!roof || roof->factoryExitAnimationStartTick != start
                    || roof->factoryExitAnimationEndTick <= s.tick) return fail("roof closed/restarted while exit was delayed");
                if (step > 40) for (const auto& layer : factoryArt->auxiliaryAnimations) {
                    const auto key = toLowerAsciiLocal(layer.key);
                    if (key != "roofdeployinganim" && key != "underroofdooranim") continue;
                    const auto* art = s.art.findArtType(layer.image);
                    const int expected = runtimeFactoryExitFirstFrame(art);
                    if (runtimeFactoryExitFrame(art, 1024, start, roof->factoryExitAnimationEndTick, s.tick,
                            s.config.fixedTickHz) != expected)
                        return fail("roof looped during its open hold");
                }
            }
            if (!capture("held")) return fail("held capture: " + error);
            // Repeated delivery notifications cannot reset an already open roof.
            unit = findMutableObjectById(s, producedId);
            beginRuntimeFactoryExitAnimation(s, *roof, *unit);
            if (roof->factoryExitAnimationStartTick != start) return fail("repeated notification restarted opening");
            if (producer == 0) {
                std::ostringstream saved;
                if (!writeRuntimeStateToStream(s, saved, error)) return fail(error);
                IniFile ini;
                if (!ini.loadFromString(saved.str())) return fail("save INI failed");
                auto loaded = std::make_unique<RuntimeWindowState>();
                seedRuntimeLoadScratch(*loaded, s, false);
                if (!materializeRuntimeLoadStateFromIni(*loaded, ini, error)) return fail("save reload: " + error);
                const auto* restored = findObjectById(*loaded, producedId);
                if (!restored || restored->factoryExitHostId != 1) return fail("save lost air exit relation");
                if (loaded->jumpjetFlights.at(producedId).height != s.jumpjetFlights.at(producedId).height
                    || runtimeCellPointForObject(*restored) != roofCell)
                    return fail("save lost the in-factory climb pose");
                loaded->tick += 200;
                advanceRuntimeFactoryExitAnimations(*loaded);
                const auto* restoredRoof = findObjectById(*loaded, 1);
                if (!restoredRoof || restoredRoof->factoryExitAnimationStartTick != start
                    || restoredRoof->factoryExitAnimationEndTick <= loaded->tick) return fail("loaded roof closed while occupied");
                const int count = parseRuntimeIntOrDefault(ini.getValue("Objects", "Count", "0"), 0);
                for (int index = 0; index < count; ++index) {
                    auto fields = splitRuntimeSaveFields(ini.getValue("Objects", std::to_string(index), ""));
                    if (fields.size() < 2 || parseRuntimeIntOrDefault(fields[1], -1) != producedId) continue;
                    if (fields.size() < 56) return fail("missing factory exit save field");
                    WorldObjectInstance parsed;
                    fields[55] = "-2";
                    if (parseRuntimeCompleteSavedObjectRecord(s, fields, index + 1, 15, parsed, error))
                        return fail("malformed factory host accepted");
                    fields.resize(55);
                    if (!parseRuntimeCompleteSavedObjectRecord(s, fields, index + 1, 15, parsed, error)
                        || parsed.factoryExitHostId != -1) return fail("legacy air exit default failed");
                }
            }
            // Resume the production's real queued move rather than teleporting.
            RuntimeMapRenderContext poseContext;
            poseContext.rules = &s.rules; poseContext.art = &s.art; poseContext.theater = &s.theater;
            poseContext.fixedTickHz = hz;
            const RECT poseViewport{0, 0, 800, 600};
            poseContext.animationTick = s.tick;
            POINT previousPose = runtimeObjectWorldRenderPlacement(*unit, s.world, poseViewport,
                poseContext, s.camera, s.renderCache).center;
            double previousHeight = unit->renderLevel;
            bool released = false;
            bool capturedRise = false;
            int nextCapture = 10;
            for (int step = 0; step < s.config.fixedTickHz * 120; ++step) {
                ++s.tick;
                processRuntimePathRequests(s);
                advanceRuntimeJumpjetFlights(s);
                auto behavior = s.behaviors.find(producedId);
                unit = findMutableObjectById(s, producedId);
                if (!unit) return fail("produced unit disappeared");
                const auto* profile = runtimeJumpjetProfile(s, *unit);
                const auto height = s.jumpjetFlights.at(producedId).height;
                if (height < profile->jumpJetHeight * runtimeJumpjetQ16
                    && runtimeCellPointForObject(*unit) != roofCell)
                    return fail("aircraft moved sideways before reaching flight height");
                if (runtimeCellPointForObject(*unit) == roofCell
                    && height < profile->jumpJetHeight * runtimeJumpjetQ16) {
                    if (unit->facing != runtimeFacingFromDelta(1, 0)) return fail("turned across the hatch while rising");
                    if (std::abs(runtimeObjectRenderLevel(*unit) - unit->renderLevel) > 0.000001)
                        return fail("climb height snapped to a whole cell");
                    poseContext.animationTick = s.tick;
                    const POINT pose = runtimeObjectWorldRenderPlacement(*unit, s.world, poseViewport,
                        poseContext, s.camera, s.renderCache).center;
                    const int allowedPixels = static_cast<int>(std::ceil(std::abs(unit->renderLevel - previousHeight)
                        * (s.camera.tileHeight / 2.0 + 34.0 * 256.0 / profile->jumpJetHeight))) + 1;
                    if (pose.x != previousPose.x || pose.y > previousPose.y || previousPose.y - pose.y > allowedPixels)
                        return fail("vertical render pose jumped or reversed");
                    previousPose = pose; previousHeight = unit->renderLevel;
                    ++continuousFrames;
                }
                if (nextCapture <= 100 && height * 100 >= profile->jumpJetHeight * runtimeJumpjetQ16 * nextCapture) {
                    if (!capture("rise-" + std::to_string(nextCapture))) return fail("climb capture");
                    nextCapture += 15;
                }
                if (!capturedRise && height >= profile->jumpJetHeight * runtimeJumpjetQ16 / 2) {
                    if (!capture("rising")) return fail("rising capture: " + error);
                    capturedRise = true;
                }
                if (behavior != s.behaviors.end()) advanceRuntimeMoveBehavior(s, *unit, behavior->second);
                advanceRuntimeFactoryExitAnimations(s);
                advanceRuntimeFactoryDeliveries(s);
                if (unit->factoryExitHostId < 0) { released = true; break; }
            }
            if (!released) {
                const auto stuck = s.behaviors.find(producedId);
                unit = findMutableObjectById(s, producedId);
                std::ostringstream detail;
                detail << factoryType << " producer=" << producer << " air unit never cleared its roof: "
                    << s.simulationStatus << " pos=" << runtimeObjectRenderX(*unit) << ',' << runtimeObjectRenderY(*unit);
                if (stuck != s.behaviors.end()) detail << " goal=" << stuck->second.moveTarget.x << ','
                    << stuck->second.moveTarget.y << " blocked=" << stuck->second.blockedTicks
                    << " pending=" << stuck->second.pathRequestPending << " path=" << stuck->second.path.size();
                return fail(detail.str());
            }
            roof = findMutableObjectById(s, 1);
            const int closeEnd = roof->factoryExitAnimationEndTick;
            s.tick = std::max(s.tick, closeEnd - runtimeFactoryAirExitCloseTicks(s, *roof) / 2);
            advanceRuntimeFactoryExitAnimations(s);
            if (!capture("closing")) return fail("closing capture: " + error);
            s.tick = closeEnd;
            advanceRuntimeFactoryExitAnimations(s);
            if (roof->factoryExitAnimationStartTick >= 0 || !capture("closed")) return fail("roof did not finish closing");
            if (producer == 0) {
                // A destroyed unit/host cannot leave an occupied roof behind.
                unit = findMutableObjectById(s, producedId);
                setRuntimeObjectCell(s, *unit, {69,65});
                clearRuntimeRenderPosition(*unit);
                beginRuntimeFactoryExitAnimation(s, *roof, *unit);
                unit->alive = false;
                ++s.tick;
                advanceRuntimeFactoryExitAnimations(s);
                if (unit->factoryExitHostId >= 0) return fail("dead unit retained the roof");
                s.tick = roof->factoryExitAnimationEndTick;
                advanceRuntimeFactoryExitAnimations(s);
                if (roof->factoryExitAnimationStartTick >= 0) return fail("dead unit left roof open");
                unit->alive = true;
                beginRuntimeFactoryExitAnimation(s, *roof, *unit);
                roof->alive = false;
                ++s.tick;
                advanceRuntimeFactoryExitAnimations(s);
                if (unit->factoryExitHostId >= 0 || roof->factoryExitAnimationStartTick >= 0)
                    return fail("dead factory retained exit state");
            }
            ++scenarios;
            std::cout << "Factory roof " << factoryType << " unit=" << pair.second << " producer=" << producer << " speed=" << s.config.gameSpeed
                << " hz=" << s.config.fixedTickHz << " bornInside=ok waitRoof=" << openingTicks
                << " verticalClimb=ok openOnce=ok delayedExit=250 repeatedNotify=ok realExit=ok closeOnce=ok save="
                << (producer == 0 ? "ok" : "coveredByLocalCase") << '\n';
        }
    }
    std::cout << "Factory roof passed: scenarios=" << scenarios << " continuousFrames=" << continuousFrames
        << " facing=ok nativeForeground=enabled saves=12\n";
    return true;
}
