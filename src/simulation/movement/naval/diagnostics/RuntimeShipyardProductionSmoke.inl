bool runRuntimeShipyardProductionSmoke(const RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    const char* output = std::getenv("CONCORD_SHIPYARD_PRODUCTION_OUTPUT");
    const bool diagnostic = std::getenv("CONCORD_SHIPYARD_PRODUCTION_DIAGNOSTIC") != nullptr;
    const std::string root = output ? output : "build/shipyard-production";
    std::filesystem::create_directories(root);
    std::string label;
    const auto fail = [&](const std::string& why) { error = "shipyard production " + label + ": " + why; return false; };
    IniFile native; RulesDatabase rules;
    if (!native.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(native)) return fail("native rules");
    int cases = 0, saves = 0, paired = 0, images = 0, producerCases = 0;
    int finalSaves = 0, finalPaired = 0, badRejections = 0;
    for (const auto& type : std::array<std::array<const char*, 4>, 3>{{
        {{"GAYARD", "DEST", "Americans", "GAPOWR"}},
        {{"NAYARD", "SUB", "Russians", "NAPOWR"}},
        {{"YAYARD", "BSUB", "YuriCountry", "YAPOWR"}}
    }}) for (int hz : {15, 30, 60}) for (bool damaged : {false, true}) {
        label = std::string(type[0]) + "-" + std::to_string(hz) + (damaged ? "-damaged" : "-normal");
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = live.config; s.config.fixedTickHz = hz; s.config.noCooldown = false;
        s.config.playerSlots.clear(); s.config.playerCountry = type[2];
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.crates = s.config.shortGame = false;
        s.config.gameMode = "Battle"; s.config.gameModeRulesOverride.clear();
        s.rules = rules; s.art = live.art; s.theater = live.theater; s.report = live.report;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        s.economy.owner = type[2]; s.economy.credits = 100000; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; initializeRuntimeLocalProductionLanes(s.economy);
        s.world = live.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        int waterTile = -1;
        const int waterSet = s.theater.getGeneralTileSetIndex("WaterSet");
        const auto waterMetadata = runtimeTerrainMetadataContext(s);
        for (int tile = 0; tile < static_cast<int>(s.theater.getTotalTileSlots()); ++tile) {
            TerrainCell probe; probe.tileIndex = tile; probe.subTileIndex = 0;
            if (waterSet >= 0 && s.theater.resolveTileIndex(tile).tileSetIndex == waterSet
                && resolveRuntimeTerrainSubTileLandType(probe,waterMetadata,s.renderCache) == RuntimeTerrainLandType::Water
                && resolveRuntimeTerrainSubTileRampType(probe,waterMetadata,s.renderCache) == 0) { waterTile = tile; break; }
        }
        const auto* waterRules = s.rules.findLandType("Water");
        if (waterTile < 0 || !waterRules || waterRules->floatSpeed != 100.0)
            return fail("native WaterSet/TMP Water/Ramp0/Float100 missing");
        if (!cases) std::cout << "Shipyard production terrain: tile=" << waterTile << " WaterSet=" << waterSet
            << " resource=" << s.theater.resolveTileIndex(waterTile).expectedFileName << " Float=100%\n";
        for (int y = 40; y <= 88; ++y) for (int x = 40; x <= 88; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = waterTile; cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        for (int id : {1, 2, 100, 101}) {
            WorldObjectInstance o; o.instanceId = id; o.kind = WorldObjectKind::Structure;
            o.type = id < 100 ? type[0] : type[3]; o.owner = type[2]; o.alive = true; o.mission = "Guard";
            o.x = id == 1 ? 84 : id == 2 ? 64 : 82; o.y = id == 1 ? 40 : id == 2 ? 64 : id == 100 ? 80 : 85;
            o.cell = runtimeCellKey(o.x, o.y); o.poweredOperational = true;
            o.health = runtimeMaxHealthForObject(s, o) / (damaged && id == 2 ? 3 : 1);
            s.world.accessObjects().push_back(o);
        }
        s.terrainByCell = buildRuntimeTerrainLookup(s.world); initializeRuntimeNavigation(s);
        s.structureFootprints = buildRuntimeStructureFootprints(s); rebuildRuntimeEconomyPower(s);
        RuntimeSnapshotActiveStateScope active(s);
        if (!applyRuntimePlayerPrimaryFactory(s, s.economy.owner, 2)) return fail("primary factory");
        RuntimeBuildCatalogItem item; item.kind = RuntimeBuildKind::Unit; item.typeId = type[1];
        item.requiredFactory = "UnitType"; item.naval = true; item.cost = findRuntimeUnitTypeById(s.rules, type[1])->cost;
        item.buildTicks = runtimeBaseBuildTimeTicks(s, item);
        s.economy.catalog = {item};
        const auto step = [](RuntimeWindowState& state) {
            RuntimeSnapshotActiveStateScope scope(state); ++state.tick; advanceRuntimeSimulation(state, false);
        };
        const auto draw = [&](RuntimeWindowState& state, const std::string& suffix, bool keep,
                              std::vector<std::uint32_t>* pixels = nullptr) {
            const auto stats = drawRuntimeStateOffscreenAtCell(state, {66, 65}, 800, 600, error,
                keep ? root + "/" + label + "-" + suffix + ".bmp" : "", false, true, -1, pixels);
            if (keep) ++images;
            return stats;
        };
        const auto idle = draw(s, "idle", hz == 30 || cases == 0);
        if (!idle || idle->buildingShipyardProductionAnimationLayers != 0
            || idle->buildingShipyardIdleAnimationLayers != 1) return fail("idle native layer: "
                + std::to_string(idle ? idle->buildingShipyardIdleAnimationLayers : -1)
                + " missing=" + std::to_string(idle ? idle->buildingArtAnimationMissing : -1)
                + " " + (idle ? idle->structureRectDiagnostic : error));
        if (!queueRuntimeBuildOrder(s, item)) return fail("real production queue: " + s.economy.status);
        step(s);
        if (!runtimeSetProductionHold(s.economy.localProductionLanes, RuntimeProductionLane::Ships, true))
            return fail("hold queue");
        for (int n = 0; n < 4; ++n) step(s);
        if (findObjectById(s, 2)->factoryExitAnimationStartTick >= 0) return fail("held queue started exit animation");
        runtimeSetProductionHold(s.economy.localProductionLanes, RuntimeProductionLane::Ships, false);
        if (!completeRuntimeActiveProductionForProbe(s, item)) return fail("active order completion");
        step(s);
        const int producedId = s.world.getObjects().back().instanceId;
        if (s.world.getObjects().back().type != type[1]) return fail("production did not deliver ship");
        const auto* factory = findObjectById(s, 2);
        const int start = factory->factoryExitAnimationStartTick, end = factory->factoryExitAnimationEndTick;
        const auto initial = draw(s, "start", hz == 30);
        if (!initial) return fail("initial rendering: " + error);
        std::cout << "Shipyard " << label << " start=" << start << " end=" << end
            << " layers=" << initial->buildingShipyardProductionAnimationLayers
            << " image=" << initial->shipyardProductionAnimationImage << '\n';
        if (diagnostic) { ++cases; continue; }
        if (start != s.tick || end <= start || factory->factoryExitAirborne
            || findObjectById(s, 1)->factoryExitAnimationStartTick >= 0
            || initial->buildingShipyardProductionAnimationLayers != 1
            || initial->buildingShipyardIdleAnimationLayers != 0
            || initial->buildingFactoryVehicleDoorAnimationLayers != 0
            || initial->buildingFactoryAirRoofAnimationLayers != 0)
            return fail("wrong producer/production layer or idle overlap");
        const std::string expectedImage = std::string(type[0]) + (damaged ? "_DD" : "_D");
        const auto* animation = s.art.findArtType(expectedImage);
        if (!animation || initial->shipyardProductionAnimationImage != expectedImage
            || initial->shipyardProductionAnimationFrame != runtimeFactoryExitFirstFrame(animation))
            return fail("wrong original resource/first frame");
        std::unique_ptr<RuntimeWindowState> finalClone;
        const auto exitState = [&](const RuntimeWindowState& state) {
            std::ostringstream value; value << std::setprecision(17) << "tick=" << state.tick;
            const auto* ship = findObjectById(state,producedId);
            if (ship) value << " ship=" << ship->type << ',' << ship->x << ',' << ship->y
                << " pose=" << runtimeObjectRenderX(*ship) << ',' << runtimeObjectRenderY(*ship)
                << ',' << ship->renderLevel << " active=" << ship->renderMoveActive << " health=" << ship->health;
            const auto found = state.behaviors.find(producedId);
            if (found != state.behaviors.end()) {
                const auto& move = found->second;
                // Ordinary saves rebuild the transient Queued/Moving state;
                // the committed kind, target, clocks, pose and lease persist.
                value << " move=" << static_cast<int>(move.kind)
                    << " target=" << move.moveTarget.x << ',' << move.moveTarget.y << " cooldown=" << move.moveCooldownTicks;
                if (ship && ship->renderMoveActive) value << " segment=" << move.renderFrom.x << ',' << move.renderFrom.y
                    << "->" << move.renderTo.x << ',' << move.renderTo.y << " ticks=" << move.renderMoveTicksRemaining
                    << '/' << move.renderMoveTicksTotal << " offsets=" << move.renderFromOffsetX << ',' << move.renderFromOffsetY
                    << ',' << move.renderToOffsetX << ',' << move.renderToOffsetY << " after=" << move.renderArrivalDistanceAfter;
            }
            value << '\n'; state.factoryDeliveries.save(value); return value.str();
        };
        const auto captureFinal = [&]() {
            if (finalClone) return true;
            const auto* ship = findObjectById(s,producedId); const auto move = s.behaviors.find(producedId);
            const auto lease = std::find_if(s.factoryDeliveries.leases().begin(),s.factoryDeliveries.leases().end(),
                [&](const auto& value){return value.producedObjectId == producedId;});
            if (!ship || lease == s.factoryDeliveries.leases().end() || move == s.behaviors.end()
                || runtimeCellPointForObject(*ship) != RuntimeCellPoint{lease->rallyCell.x,lease->rallyCell.y}
                || !ship->renderMoveActive || move->second.renderMoveTicksRemaining <= 0
                || move->second.renderMoveTicksRemaining >= move->second.renderMoveTicksTotal) return true;
            std::ostringstream saved; IniFile ini;
            if (!writeRuntimeStateToStream(s,saved,error) || !ini.loadFromString(saved.str()))
                return fail("final-segment save: " + error + " " + exitState(s));
            const auto fields = splitRuntimeSaveFields(ini.getValue("VehicleMovePresentation","0",""));
            std::cout << "Shipyard final save " << label << ' ' << exitState(s)
                << "VehicleMovePresentation.Count=" << ini.getValue("VehicleMovePresentation","Count","")
                << " row0=" << ini.getValue("VehicleMovePresentation","0","") << '\n';
            if (fields.size() != 17 || fields[0] != std::to_string(producedId))
                return fail("ordinary naval final segment missing presentation: " + exitState(s));
            const std::string segmentPath = root + "/" + label + "-rally-segment.ini";
            { std::ofstream evidence(segmentPath); evidence << saved.str();
                if (!evidence) return fail("rally save evidence write"); }
            finalClone = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*finalClone,s,false);
            { RuntimeSnapshotActiveStateScope destination(*finalClone);
                if (!loadRuntimeState(*finalClone,segmentPath,error))
                    return fail("final-segment full load: " + error + " " + exitState(s)); }
            if (exitState(s) != exitState(*finalClone))
                return fail("final-segment full load changed pose/clock/lease: " + exitState(s) + " restored=" + exitState(*finalClone));
            ++finalSaves;
            if (hz == 30 && !damaged) for (int bad = 0; bad < 4; ++bad) {
                auto row = fields;
                if (bad == 1) row[10] = std::to_string(move->second.renderMoveTicksRemaining + 1);
                if (bad == 2) { row[1]=row[3]; row[2]=row[4]; for (int n=13;n<17;++n) row[n]="0"; }
                if (bad == 3) row[0] = "100"; // Existing power plant: never a naval moving object.
                std::ostringstream joined; for (std::size_t n=0;n<row.size();++n) joined << (n?"|":"") << row[n];
                std::istringstream source(saved.str()); std::ostringstream rewritten; std::string line,section;
                int replaced = 0;
                while (std::getline(source,line)) {
                    if (!line.empty() && line.back()=='\r') line.pop_back();
                    if (!line.empty() && line.front()=='[') section=line;
                    if (section=="[VehicleMovePresentation]" && line.starts_with(bad==0?"Count=":"0=")) {
                        line=bad==0?"Count=0":"0="+joined.str(); ++replaced;
                    }
                    rewritten << line << '\n';
                }
                IniFile broken; std::string rejection;
                if (replaced != 1 || !broken.loadFromString(rewritten.str())) return fail("bad rally fixture rewrite");
                const std::string badPath = root + "/" + label + "-rally-bad-" + std::to_string(bad) + ".ini";
                { std::ofstream file(badPath); file << rewritten.str(); if (!file) return fail("bad rally evidence write"); }
                RuntimeSnapshotActiveStateScope destination(*finalClone);
                std::ostringstream before,after;
                if (!writeRuntimeStateToStream(*finalClone,before,error)) return fail("bad-save baseline: "+error);
                // Public loading owns scratch/commit atomicity. Do not test
                // the internal materializer directly and claim live rollback.
                if (loadRuntimeState(*finalClone,badPath,rejection)) return fail("bad rally save accepted kind="+std::to_string(bad));
                if (rejection.empty() || !writeRuntimeStateToStream(*finalClone,after,error)
                    || before.str()!=after.str() || exitState(s)!=exitState(*finalClone))
                    return fail("bad rally save changed destination kind="+std::to_string(bad)+" reason="+rejection);
                ++badRejections;
            }
            return true;
        };
        const auto advanceOriginal = [&]() {
            step(s);
            if (finalClone) {
                step(*finalClone); ++finalPaired;
                if (exitState(s)!=exitState(*finalClone)) return fail("continued final segment diverged: "
                    +exitState(s)+" restored="+exitState(*finalClone));
            }
            return captureFinal();
        };
        if (!captureFinal()) return false;
        for (int tick = s.tick; tick < start + (end - start) / 2; ++tick) if (!advanceOriginal()) return false;
        std::vector<std::uint32_t> beforePixels, restoredPixels;
        const auto middle = draw(s, "middle", hz == 30, &beforePixels);
        if (!middle || middle->shipyardProductionAnimationFrame <= initial->shipyardProductionAnimationFrame)
            return fail("production animation did not advance");
        std::ostringstream saved; IniFile ini;
        auto clone = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*clone, s, false);
        if (!writeRuntimeStateToStream(s, saved, error) || !ini.loadFromString(saved.str())) return fail("full save: " + error);
        { RuntimeSnapshotActiveStateScope destination(*clone);
            if (!materializeRuntimeLoadStateFromIni(*clone,ini,error)) return fail("full save: " + error); }
        const auto restored = draw(*clone, "restored", false, &restoredPixels);
        if (!restored || beforePixels != restoredPixels) return fail("save changed visible production frame");
        ++saves;
        while (s.tick < end) {
            if (!advanceOriginal()) return false;
            step(*clone); ++paired;
            const auto* current = findObjectById(s, 2); const auto* other = findObjectById(*clone, 2);
            if (current->factoryExitAnimationStartTick != other->factoryExitAnimationStartTick
                || current->factoryExitAnimationEndTick != other->factoryExitAnimationEndTick
                || runtimeCellPointForObject(*findObjectById(s, producedId))
                    != runtimeCellPointForObject(*findObjectById(*clone, producedId)))
                return fail("continued animation/ship exit diverged");
        }
        const auto finished = draw(s, "finished", hz == 30);
        if (!finished || finished->buildingShipyardProductionAnimationLayers != 0
            || finished->buildingShipyardIdleAnimationLayers != 1
            || findObjectById(s, 2)->factoryExitAnimationStartTick >= 0)
            return fail("one-shot animation did not restore idle");
        // Production animation duration and travel duration are independent.
        // Continue real movement when the visual animation finishes first.
        for (int n=0;n<hz*120 && (!finalClone || !s.factoryDeliveries.leases().empty());++n)
            if (!advanceOriginal()) return false;
        if (!finalClone || !s.factoryDeliveries.leases().empty() || findObjectById(s,producedId)->renderMoveActive)
            return fail("no completed saved naval final segment: " + exitState(s));
        // Both AI delivery paths share the real successful-spawn notification.
        if (hz == 30 && !damaged) for (int producer : {1, 2}) {
            s.factoryDeliveries.reset();
            for (auto& o : s.world.accessObjects()) if (o.kind == WorldObjectKind::Unit) o.alive = false;
            refreshRuntimeObjectOccupancyAfterLocalChange(s);
            RuntimeComputerAiState ai; ai.owner = s.economy.owner;
            RuntimeCampaignHouseProductionState house; house.owner = s.economy.owner;
            RuntimeCampaignTeamBuild team;
            const bool built = producer == 1 ? runtimeComputerAiBuildUnit(s, ai, item)
                : completeRuntimeCampaignAiUnit(s, house, item, team).has_value();
            int animated = 0;
            for (const auto& o : s.world.getObjects()) if (o.kind == WorldObjectKind::Structure
                && o.factoryExitAnimationStartTick == s.tick) ++animated;
            if (!built || animated != 1) return fail("AI delivery animation");
            ++producerCases;
            s.tick += hz * 30; advanceRuntimeFactoryExitAnimations(s);
        }
        ++cases;
    }
    std::cout << "Shipyard production " << (diagnostic ? "diagnostic" : "passed") << ": cases=" << cases
        << " fullSaves=" << saves << " continuedTicks=" << paired << " nativeImages=" << images
        << " aiDeliveries=" << producerCases << " finalSegmentSaves=" << finalSaves << " finalPairedTicks=" << finalPaired
        << " atomicBadSaves=" << badRejections << " primary/hold/idle/once/damaged=ok\n";
    return true;
}
