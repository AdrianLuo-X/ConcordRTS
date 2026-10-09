#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeMcvDeployTurnSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "MCV deploy turn: " + why; return false; };
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->report = buildRuntimeStartupReport(config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) return fail("native resources");
    const char* output = std::getenv("CONCORD_MCV_DEPLOY_TURN_DIR");
    const std::filesystem::path root = output ? output : "build/smoke/feature-set-20/09-mcv-turn";
    std::filesystem::create_directories(root);
    const auto fixture = [&](int hz) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        seedRuntimeLoadScratch(s, *assets, false); s.config = config; s.config.fixedTickHz = hz;
        s.config.playerSlots.clear(); s.config.computerAiEnabled = s.config.multiplayerActive = false;
        s.config.superweapons = s.config.noCooldown = s.config.crates = s.config.shortGame = false;
        s.visionEnabled = s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.economy.owner = "Americans";
        s.economy.skirmishStartApplied = true; s.triggers.loaded = s.triggers.bootstrapComplete = true;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts = {}; s.mapFactsLoaded = false; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.terrainByCell.clear(); s.terrainInfoByCell.clear();
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = -1;
        for (int y = 0; y < 48; ++y) for (int x = 0; x < 48; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(s); return box;
    };
    const auto step = [](RuntimeWindowState& s) { RuntimeSnapshotActiveStateScope scope(s); ++s.tick; advanceRuntimeSimulation(s); };
    const auto yardFor = [](const RuntimeWindowState& s, const std::string& type) -> const WorldObjectInstance* {
        for (const auto& o : s.world.getObjects()) if (o.alive && o.kind == WorldObjectKind::Structure && o.type == type) return &o;
        return nullptr;
    };
    const auto wait = [&](RuntimeWindowState& s, int id) {
        for (int tick = 0; tick < s.config.fixedTickHz * 6 && findObjectById(s, id); ++tick) step(s);
        return !findObjectById(s, id);
    };
    int cases = 0, visible = 0, turnFrames = 0, saves = 0, images = 0, extras = 0;
    // Runtime 160 is the screen's lower-left isometric edge: cell delta (0,+1).
    if (runtimeFacingFromDelta(0, 1) != 160) return fail("isometric facing convention");
    for (const char* type : {"AMCV", "SMCV", "PCV"}) for (int hz : {15, 30, 60})
        for (int initial : {0, 32, 64, 96, 128, 160, 192, 224}) for (bool key : {false, true}) {
        auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope scope(s);
        const std::string scene = std::string(type) + "/" + std::to_string(hz) + "/" + std::to_string(initial) + (key ? "/D" : "/click");
        auto* mcv = addRuntimeObjectInstanceForOwner(s, "Americans", RuntimeBuildKind::Unit, type, {20, 20});
        if (!mcv) return fail(scene + " native actor"); const int id = mcv->instanceId;
        const auto* unit = findRuntimeUnitType(s.rules, *mcv); if (!unit) return fail(scene + " rules");
        const std::string yardType = unit->deploysInto;
        mcv->facing = initial; mcv->veterancy = 1; mcv->veterancyExperience = 73;
        mcv->health = std::max(1, runtimeMaxHealthForObject(s, *mcv) * 3 / 5);
        mcv->crateArmorMultiplier = 1.25; mcv->crateFirepowerMultiplier = 1.5;
        const auto before = *mcv; const int credits = s.economy.credits;
        const bool capture = hz == 30 && initial == 0 && !key;
        if (capture && !drawRuntimeStateOffscreenAtCell(s, {20,20}, 800, 600, error,
            (root / (std::string(type) + "-0-before.bmp")).string(), false)) return fail(error);
        s.selectedObjectIds = {id};
        if (!(key ? dispatchRuntimeSecondaryActionForSelectionFromUi(s) : dispatchRuntimeSecondaryActionFromUi(s, id)))
            return fail(scene + " deploy input");
        mcv = findMutableObjectById(s, id);
        if (initial != 160 && (!mcv || yardFor(s, yardType))) {
            const auto* yard = yardFor(s, yardType);
            return fail(scene + " skipped visible turn; yardFacing=" + std::to_string(yard ? yard->facing : -1) + " expected=160");
        }
        const auto saveCheck = [&]() {
            for (bool exact : {false, true}) {
                std::ostringstream out; if (!writeRuntimeStateToStream(s, out, error)
                    || (exact && !writeRuntimeMultiplayerSnapshotExtension(s, out, error))) return false;
                IniFile ini; if (!ini.loadFromString(out.str())) return false;
                auto loaded = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*loaded, s, false);
                if (!materializeRuntimeLoadStateFromIni(*loaded, ini, error, exact)
                    || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*loaded, ini, error))) return false;
                const auto* original = findObjectById(s, id); const auto* restored = findObjectById(*loaded, id);
                if (!original || !restored || restored->facing != original->facing || restored->mission != original->mission
                    || !loaded->behaviors.contains(id) || loaded->behaviors.at(id).moveTarget != s.behaviors.at(id).moveTarget
                    || !wait(*loaded, id)) return false;
                const auto* yard = yardFor(*loaded, yardType);
                if (!yard || yard->facing != 160 || runtimePlacementAnchorCellForFootprint(runtimeCellPointForObject(*yard),runtimeStructureFootprintForObject(*loaded,*yard)) != RuntimeCellPoint{20,20}
                    || yard->veterancy != before.veterancy || loaded->economy.credits != credits) return false;
                ++saves;
            }
            return true;
        };
        int previous = initial; bool midpointSaved = false, alignedSeen = false;
        for (int tick = 0; tick < hz * 6 && findObjectById(s, id); ++tick) {
            step(s); mcv = findMutableObjectById(s, id);
            if (!mcv) break;
            if (runtimeCellPointForObject(*mcv) != RuntimeCellPoint{20,20}
                || std::abs(runtimeObjectRenderX(*mcv) - 20.0) > 1e-8
                || std::abs(runtimeObjectRenderY(*mcv) - 20.0) > 1e-8
                || mcv->veterancy != before.veterancy || mcv->health != before.health)
                return fail(scene + " turn translated actor or changed stats");
            const int delta = runtimeFacingAbsDifference(previous, mcv->facing);
            // The shared retail facing timer advances on authored 15-Hz rule
            // frames. A 30/60-Hz simulation samples unchanged poses between them.
            const int budget = runtimeRuleFramesElapsed(s.tick,hz) * (std::max(1,unit->rot)+1);
            if (delta > budget || runtimeFacingAbsDifference(mcv->facing, 160) > runtimeFacingAbsDifference(previous,160))
                return fail(scene + " facing snapped or turned away; tick=" + std::to_string(s.tick)
                    + " previous=" + std::to_string(previous) + " current=" + std::to_string(mcv->facing)
                    + " delta=" + std::to_string(delta) + " budget=" + std::to_string(budget));
            previous = mcv->facing; ++turnFrames;
            if (hz == 30 && initial == 0 && !key && !midpointSaved && mcv->facing != initial && mcv->facing != 160) {
                if (!saveCheck()) return fail(scene + " mid-turn save: " + error); midpointSaved = true;
            }
            if (mcv->facing == 160 && !alignedSeen) {
                alignedSeen = true; ++visible;
                if (capture) {
                    if (!drawRuntimeStateOffscreenAtCell(s, {20,20}, 800, 600, error,
                        (root / (std::string(type) + "-1-aligned.bmp")).string(), false)) return fail(error);
                    ++images;
                    if (!saveCheck()) return fail(scene + " aligned save: " + error);
                }
            }
        }
        const auto* yard = yardFor(s, yardType);
        if (findObjectById(s,id) || !yard || yard->facing != 160 || yard->turretFacing != 160
            || runtimePlacementAnchorCellForFootprint(runtimeCellPointForObject(*yard),runtimeStructureFootprintForObject(s,*yard)) != RuntimeCellPoint{20,20} || yard->owner != before.owner
            || yard->veterancy != before.veterancy || yard->veterancyExperience != before.veterancyExperience
            || yard->crateArmorMultiplier != before.crateArmorMultiplier || yard->crateFirepowerMultiplier != before.crateFirepowerMultiplier
            || s.economy.credits != credits || s.selectedObjectIds != std::vector<int>{yard->instanceId}
            || (initial != 160 && !alignedSeen)) return fail(scene + " final yard/selection/stats");
        ++cases;
    }
    for (int hz : {15,30,60}) for (int mode : {0,1,2,3,4}) {
        auto box = fixture(hz); auto& s = *box; RuntimeSnapshotActiveStateScope scope(s);
        auto* mcv = addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Unit,"AMCV",{20,20});
        if (!mcv) return fail("extra fixture"); const int id = mcv->instanceId; mcv->facing = 0;
        if (mode == 0) {
            mcv->facing = runtimeFacingFromDelta(1,0); s.selectedObjectIds = {id}; issueRuntimeMoveCommand(s,{21,20});
            for(int tick=0;tick<hz*4;++tick) {step(s);mcv=findMutableObjectById(s,id);if(mcv&&mcv->renderMoveActive)break;}
            if(!mcv||!mcv->renderMoveActive) return fail("moving deployment setup");
        }
        const auto origin = runtimeCellPointForObject(*mcv); const double x = runtimeObjectRenderX(*mcv), y = runtimeObjectRenderY(*mcv);
        s.selectedObjectIds = {id}; if(!dispatchRuntimeSecondaryActionFromUi(s,id)) return fail("extra deploy rejected");
        mcv=findMutableObjectById(s,id);
        if(!mcv||std::abs(runtimeObjectRenderX(*mcv)-x)>1e-8||std::abs(runtimeObjectRenderY(*mcv)-y)>1e-8) return fail("deploy input snapped committed pose");
        if(mode==4) {
            auto* foot=addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Infantry,"E1",{25,20});
            if(!foot)return fail("reservation foot fixture");const int footId=foot->instanceId;
            auto* air=addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Unit,"SHAD",{25,25});
            if(!air)return fail("reservation air fixture");const int airId=air->instanceId;air->airborne=true;air->level=12;
            const auto occupancy=buildRuntimeOccupancyLookup(s);
            const auto blocked=[&](RuntimeCellPoint cell,int moverId){const auto* mover=findObjectById(s,moverId);
                return !mover||!runtimeCellIsPassable(s,s.terrainByCell,occupancy,cell,*mover);};
            // Original GACNST is 4x4; its center anchor is one cell inside.
            const RuntimeCellPoint foundationCorner{origin.x-1,origin.y-1};
            for(int dy=0;dy<4;++dy)for(int dx=0;dx<4;++dx) {
                const RuntimeCellPoint cell{foundationCorner.x+dx,foundationCorner.y+dy};
                if(!blocked(cell,footId)||blocked(cell,airId))return fail("pending foundation ground/air domain");
            }
            if(runtimeCellTransitionIsPassable(s,s.terrainByCell,occupancy,{foundationCorner.x+4,foundationCorner.y},
                {foundationCorner.x+3,foundationCorner.y-1},*findObjectById(s,footId)))return fail("pending foundation diagonal side cell");
            issueRuntimeStopCommand(s);
            for(int dy=0;dy<4;++dy)for(int dx=0;dx<4;++dx) {
                const RuntimeCellPoint cell{foundationCorner.x+dx,foundationCorner.y+dy};
                if(cell!=origin&&blocked(cell,footId))return fail("Stop retained future foundation reservation at "
                    +std::to_string(dx)+","+std::to_string(dy));
            }
        } else if(mode==1) {issueRuntimeStopCommand(s);for(int tick=0;tick<hz*4;++tick)step(s);
            const auto* stopped=findObjectById(s,id);
            if(!stopped||yardFor(s,"GACNST")||runtimeObjectHasDeployedState(s,*stopped)
                ||!runtimeObjectCanMove(s,*stopped))return fail("Stop did not cancel pending deploy into mobile state");}
        else if(mode==2) {
            auto* blocker=addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Infantry,"E1",{origin.x+1,origin.y});
            if(!blocker)return fail("late blocker");blocker->mission="Sleep";
            for(int tick=0;tick<hz*4;++tick)step(s);
            if(!findObjectById(s,id)||yardFor(s,"GACNST"))return fail("late occupied footprint was built over");
        } else {
            if(mode==3) for(int click=0;click<20;++click)if(!dispatchRuntimeSecondaryActionFromUi(s,id))return fail("repeated deploy input");
            if(!wait(s,id))return fail("pending deploy did not finish");
            const auto* yard=yardFor(s,"GACNST");if(!yard||yard->facing!=160||runtimePlacementAnchorCellForFootprint(runtimeCellPointForObject(*yard),runtimeStructureFootprintForObject(s,*yard))!=origin)return fail("moving/repeated deployment shifted site");
        }
        ++extras;
    }
    std::cout << "MCV deploy turn passed: " << cases << " native click/D cases, " << turnFrames << " bounded turn frames, "
        << visible << " visible aligned poses, " << saves << " ordinary/exact saves, " << extras << " moving/Stop/block/repeat/reservation cases, " << images << " native aligned images\n";
    error.clear(); return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
