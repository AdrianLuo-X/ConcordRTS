bool runRuntimeBuildingSaleSmoke(RuntimeWindowState& live, std::string& error) {
    std::string scene;
    const auto fail = [&](const std::string& why) { error = "Building sale " + scene + ": " + why; return false; };
    const char* output = std::getenv("CONCORD_BUILDING_SALE_OUTPUT");
    const std::string root = output ? output : "build/smoke/feature-set-14/33-building-sell/checks";
    std::filesystem::create_directories(root);
    IniFile native; RulesDatabase rules;
    if (!native.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(native)) return fail("native rules");
    const auto make = [&](int hz) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = live.config; s.config.fixedTickHz = hz; s.config.noCooldown = false;
        s.config.computerAiEnabled = s.config.multiplayerActive = false; s.config.playerSlots.clear();
        s.config.gameMode = "Battle"; s.config.gameModeRulesOverride.clear();
        s.rules = rules; s.art = live.art; s.theater = live.theater; s.report = live.report;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.economy.owner = "Americans"; s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true; s.runtimePreferencePersistenceEnabled = false;
        s.world = live.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y=48; y<=84; ++y) for (int x=48; x<=84; ++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x; cell.tileIndex=cell.subTileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(s); initializeRuntimeLocalProductionLanes(s.economy);
        return box;
    };
    const auto retained = [](RuntimeWindowState& s, int id) -> WorldObjectInstance* {
        for (auto& object : s.world.accessObjects()) if (object.instanceId == id) return &object;
        return nullptr;
    };
    int scenes=0, renders=0, saves=0, frames=0;
    std::ofstream records(root + "/building-sale.csv");
    records << "type,hz,unfinished,total,remaining,frame,body,shadow,turret,layers\n";
    const auto run = [&](const BuildingType& building, int hz, int unfinished) {
        scene = building.id + " hz=" + std::to_string(hz) + " unfinished=" + std::to_string(unfinished);
        auto box = make(hz); auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
        WorldObjectInstance object; object.instanceId=1; object.type=building.id; object.kind=WorldObjectKind::Structure;
        object.owner=s.economy.owner; object.alive=true; object.currentAmmo=-1; object.x=object.y=64;
        object.cell=runtimeCellKey(64,64); object.mission="Guard"; object.health=runtimeMaxHealthForObject(s,object);
        const auto timing = runtimeStructureBuildupTiming(s,object);
        const auto* art = runtimeArtTypeForObject(s,object);
        const bool animated = !building.wall && timing.frames > 1;
        int startStage = animated ? timing.frames-1 : 0;
        if (unfinished >= 0) {
            object.mission="Construction"; object.buildAnimTicksTotal=timing.fixedTicks;
            object.buildAnimTicksRemaining=timing.fixedTicks-unfinished;
            startStage=runtimeBuildupFrameAtProgress(timing.frames,timing.fixedTicks,
                object.buildAnimTicksRemaining,hz,false);
        }
        s.world.accessObjects().push_back(object); initializeRuntimeNavigation(s);
        s.structureFootprints=buildRuntimeStructureFootprints(s); rebuildRuntimeEconomyPower(s);
        s.selectedObjectIds={1}; const int before=s.economy.credits;
        if (!sellRuntimeStructure(s,1) || findObjectById(s,1) || !s.selectedObjectIds.empty()
            || s.economy.credits-before != runtimeBuildingSaleRefund(&building,s.rules.getRefundPercent()))
            return fail("logical removal, selection or refund");
        const int refunded=s.economy.credits; const auto population=s.world.getObjects().size();
        if (sellRuntimeStructure(s,1) || s.economy.credits != refunded || s.world.getObjects().size()!=population
            || !s.projectiles.empty()) return fail("duplicate sale or destruction effect");
        auto* sold=retained(s,1); if (!sold || sold->mission!="Sold" || sold->rubble) return fail("sold tombstone");
        const int total=sold->buildAnimTicksTotal, remaining=sold->buildAnimTicksRemaining;
        const int expectedRemaining = !animated ? 0 : unfinished<0 ? timing.fixedTicks
            : static_cast<int>((static_cast<std::int64_t>(startStage)+1)*timing.fixedTicks/timing.frames);
        if (remaining!=expectedRemaining) return fail("wrong reverse duration");
        int previous=-1;
        for (int elapsed=0; elapsed<=remaining; ++elapsed) {
            sold=retained(s,1);
            if (!sold || sold->alive || sold->buildAnimTicksRemaining!=remaining-elapsed) return fail("timer lifecycle");
            const bool visible=elapsed<remaining;
            int expectedFrame=-1;
            if (visible) {
                const int stage=std::min(timing.frames-1,static_cast<int>(static_cast<std::int64_t>(total-remaining+elapsed)*timing.frames/total));
                expectedFrame=building.gate?stage:timing.frames-1-stage;
                if (previous>=0 && (building.gate?expectedFrame<previous:expectedFrame>previous)) return fail("reverse direction");
                previous=expectedFrame; ++frames;
            }
            if (elapsed==0 || elapsed==remaining/2 || elapsed==remaining-1 || elapsed==remaining) {
                s.renderCache.auxiliaryShpSpritesByImage.clear();
                const std::string image = hz==30 && unfinished<0
                    ? root + "/sale-" + building.id + "-" + std::to_string(elapsed) + ".bmp" : "";
                const auto stats=drawRuntimeStateOffscreenAtCell(s,{65,65},640,480,error,image,false,false);
                if (!stats || !stats->rendered) return fail("world rendering: "+error);
                if (stats->structureTurretAnimAttempts || stats->structureTurretAnimDrawn || stats->buildingArtAnimationLayers)
                    return fail("live turret or animation survived sale");
                if (visible) {
                    bool decoded=false;
                    const std::string prefix=toLowerAsciiLocal(art->buildup)+"|";
                    for (const auto& [key,frame]:s.renderCache.auxiliaryShpSpritesByImage)
                        if (key.rfind(prefix,0)==0 && frame.loaded && frame.frameIndex==expectedFrame) decoded=true;
                    if (!decoded || (elapsed==remaining/2 && !stats->visibleStructureSprites))
                        return fail("expected reverse frame not drawn: "+std::to_string(expectedFrame));
                } else if (stats->visibleStructureSprites || stats->structureShadowsDrawn || runtimeStructureIsSelling(*sold))
                    return fail("body/shadow retained after completion");
                records << building.id << ',' << hz << ',' << unfinished << ',' << total << ',' << remaining-elapsed
                    << ',' << expectedFrame << ',' << stats->visibleStructureSprites << ',' << stats->structureShadowsDrawn
                    << ',' << stats->structureTurretAnimDrawn << ',' << stats->buildingArtAnimationLayers << '\n';
                ++renders;
            }
            if (visible && elapsed==remaining/2 && (building.id=="GTGCAN" || building.id=="NANRCT" || building.id=="YAPOWR")) {
                std::ostringstream text; IniFile ini; auto restored=make(hz); seedRuntimeLoadScratch(*restored,s,false);
                if (!writeRuntimeStateToStream(s,text,error) || !ini.loadFromString(text.str())
                    || !materializeRuntimeLoadStateFromIni(*restored,ini,error)) return fail("mid-sale load: "+error);
                auto* copy=retained(*restored,1);
                if (!copy || copy->alive || copy->buildAnimTicksTotal!=total || copy->buildAnimTicksRemaining!=remaining-elapsed)
                    return fail("load lost reverse phase");
                for (int i=0;i<remaining-elapsed;++i) { ++restored->tick; advanceRuntimeBuildAnimations(*restored); }
                if (runtimeStructureIsSelling(*retained(*restored,1)) || restored->economy.credits!=refunded)
                    return fail("restored completion or repeated refund");
                if (!saveRuntimeState(s,root+"/sale-"+building.id+"-"+std::to_string(hz)+"-"+std::to_string(unfinished)+".ini",error))
                    return fail("save artifact: "+error);
                ++saves;
            }
            if (elapsed<remaining) { ++s.tick; advanceRuntimeBuildAnimations(s); }
        }
        ++scenes;
        std::cout << "Building sale: " << scene << " ticks=" << remaining << " PASS\n";
        return true;
    };
    for (int hz:{15,30,60}) for (const auto& building:rules.getBuildingTypes()) {
        if (building.techLevel<0 || building.owner.empty() || building.unsellable) continue;
        if (!run(building,hz,-1)) return false;
    }
    const auto* cannon=rules.findBuildingType("GTGCAN"); if (!cannon) return fail("native cannon");
    for (int hz:{15,30,60}) {
        auto box=make(hz); WorldObjectInstance object; object.kind=WorldObjectKind::Structure; object.type=cannon->id;
        const int total=runtimeStructureBuildupTiming(*box,object).fixedTicks;
        for (int elapsed:{0,total/2,total-1}) if (!run(*cannon,hz,elapsed)) return false;
    }
    std::cout << "Building sale passed: " << scenes << " scenes, " << renders << " world renders, " << frames
        << " reverse ticks, " << saves << " full reloads; 15/30/60 Hz\n";
    return true;
}
