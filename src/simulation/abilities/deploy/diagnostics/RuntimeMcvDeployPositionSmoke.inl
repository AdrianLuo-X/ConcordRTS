bool runRuntimeMcvDeployPositionSmoke(const RuntimeConfig& config,std::string& error) {
    std::cout<<std::unitbuf;std::string scene;
    const auto fail=[&](const std::string& why){error="MCV deployment position: "+scene+": "+why;return false;};
    const char* output=std::getenv("CONCORD_MCV_DEPLOY_POSITION_OUTPUT");const std::string root=output?output:"";
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success)return fail("native resources");
    int cases=0,rejections=0,saves=0,reloadedTicks=0,voices=0;
    const RuntimeCellPoint start{110,215},wide{123,222};
    for(const auto& type:std::array<std::array<const char*,4>,3>{{
        {{"AMCV","Americans","GACNST","ceva063"}},{{"SMCV","Russians","NACNST","csof063"}},{{"PCV","YuriCountry","YACNST","cyur063"}}
    }})for(bool key:{false,true})for(int obstacle:{0,1,2}) {
        scene=std::string(type[0])+(key?"-D-":"-click-")+std::to_string(obstacle);
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);
        s.config=config;s.config.fixedTickHz=30;s.config.playerCountry=type[1];s.config.playerSlots.clear();
        s.config.computerAiEnabled=s.config.shortGame=s.config.crates=s.config.noCooldown=s.config.multiplayerActive=false;
        s.config.startingUnits=0;s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
        s.config.keyboardReferenceLayout=true;s.config.keyBindings.clear();
        s.world=assets->world;s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        auto& cells=s.world.accessTerrainCells();
        cells.erase(std::remove_if(cells.begin(),cells.end(),[](const auto& c){return c.x<96||c.x>140||c.y<202||c.y>240;}),cells.end());
        s.report=assets->report;s.rules=assets->rules;s.art=assets->art;s.theater=assets->theater;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.economy.owner=type[1];s.economy.credits=100000;s.economy.skirmishStartApplied=true;
        initializeRuntimeLocalProductionLanes(s.economy);s.triggers.loaded=s.triggers.bootstrapComplete=true;s.visionEnabled=false;
        s.terrainByCell=buildRuntimeTerrainLookup(s.world);initializeRuntimeNavigation(s);
        if(obstacle==0) {
            auto* block=addRuntimeObjectInstance(s,RuntimeBuildKind::Building,"GAPOWR",{start.x+1,start.y});
            if(!block)return fail("blocking structure");block->mission="Guard";block->buildAnimTicksRemaining=block->buildAnimTicksTotal=0;
        } else {
            auto* cell=findMutableRuntimeTerrainCellAt(s,{start.x+1,start.y+1});if(!cell)return fail("blocking terrain");
            if(obstacle==1)cell->level=2;
            else {
                int water=-1;
                for(int tile=0;tile<static_cast<int>(s.theater.getTotalTileSlots());++tile){TerrainCell p;p.tileIndex=tile;p.subTileIndex=0;
                    const auto info=runtimeTerrainInfoForCell(s,p);if(info.water&&!info.shore&&!info.bridge){water=tile;break;}}
                if(water<0)return fail("native water tile");cell->tileIndex=water;cell->subTileIndex=0;
            }
            s.terrainInfoByCell.clear();initializeRuntimeNavigation(s);
        }
        auto* mcv=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,type[0],start);if(!mcv)return fail("native MCV");
        const int id=mcv->instanceId;mcv->mission="Guard";s.selectedObjectIds={id};
        s.structureFootprints=buildRuntimeStructureFootprints(s);refreshRuntimeObjectOccupancyAfterLocalChange(s);
        loadRuntimeSoundDatabase(s);ensureRuntimeMenuKeyboardLoaded(s);
        const auto step=[](RuntimeWindowState& state){RuntimeSnapshotActiveStateScope scope(state);++state.tick;advanceRuntimeSimulation(state,false);};
        step(s);
        const auto item=runtimeBuildItemForBuilding(s,type[2]);
        const auto alternative=item?findRuntimeMcvDeployCell(s,*findObjectById(s,id),*item):std::optional<RuntimeCellPoint>{};
        if(!item||runtimeCellCanPlaceBuilding(s,*item,
            runtimePlacementTopLeftForAnchor(start,runtimeStructureFootprintForBuildItem(s,*item)),
            id,nullptr,s.economy.owner)||!alternative||*alternative==start)
            return fail("fixture must block the current footprint but offer nearby empty space");
        // WM_KEYDOWN's DeployObject branch uses the selection dispatcher below.
        const auto action=[&](){s.selectedObjectIds={id};return key?dispatchRuntimeSecondaryActionForSelectionFromUi(s):dispatchRuntimeSecondaryActionFromUi(s,id);};
        const auto capture=[&](const char* phase,RuntimeCellPoint cell){return root.empty()||key||obstacle!=0
            ||drawRuntimeStateOffscreenAtCell(s,cell,800,600,error,root+"/"+scene+"-"+phase+".bmp",false).has_value();};
        const auto worldBefore=s.world.deterministicSimulationStateHash();const int credits=s.economy.credits;
        if(!action())return fail("deploy input was not handled");
        mcv=findMutableObjectById(s,id);
        if(!mcv||mcv->kind!=WorldObjectKind::Unit||runtimeCellPointForObject(*mcv)!=start||runtimeOwnerHasConstructionYard(s,s.economy.owner)) {
            capture("before-relocated",start);return fail("blocked deployment moved or transformed the MCV");
        }
        if(s.world.deterministicSimulationStateHash()!=worldBefore||s.economy.credits!=credits||s.selectedObjectIds!=std::vector<int>{id}||s.behaviors.contains(id))
            return fail("failed deployment mutated unit/credits/selection or queued movement");
        if(s.runtimeSoundIntentHistory.empty()||s.runtimeSoundIntentHistory.back()!=runtimeSoundIntentCannotDeployHere||s.runtimeSoundPlaybackHistory.empty())
            return fail("missing CannotDeployHere adviser event");
        const auto voice=s.runtimeSoundPlaybackHistory.back();
        if(!voice.eva||voice.section!="EVA_CannotDeployHere"||voice.sample!=type[3]||!voice.playable)return fail("wrong/missing original faction adviser sample: "+voice.sample);
        ++voices;++rejections;if(!capture("blocked",start))return fail(error);
        for(bool exact:{false,true}) {
            std::ostringstream saved;IniFile ini;
            if(!writeRuntimeStateToStream(s,saved,error)||(exact&&!writeRuntimeMultiplayerSnapshotExtension(s,saved,error))||!ini.loadFromString(saved.str()))return fail("save: "+error);
            auto clone=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*clone,s,false);
            if(!materializeRuntimeLoadStateFromIni(*clone,ini,error,exact)||(exact&&!materializeRuntimeMultiplayerSnapshotExtension(*clone,ini,error)))return fail("reload: "+error);
            for(int tick=0;tick<60;++tick){step(*clone);++reloadedTicks;}
            const auto* restored=findObjectById(*clone,id);
            if(!restored||runtimeCellPointForObject(*restored)!=start||runtimeOwnerHasConstructionYard(*clone,clone->economy.owner))return fail("reload automatically moved/deployed failed MCV");
            ++saves;
        }
        for(int n=0;n<60;++n)step(s);
        if(!findObjectById(s,id)||runtimeCellPointForObject(*findObjectById(s,id))!=start)return fail("waiting automatically moved or deployed MCV");
        s.selectedObjectIds={id};
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,wide)||s.lastCommand.acceptedCount!=1)return fail("player move rejected");
        bool arrived=false;
        for(int n=0;n<900;++n){step(s);const auto* moved=findObjectById(s,id);
            if(!moved)return fail("move order automatically deployed MCV");
            if(runtimeCellPointForObject(*moved)==wide&&!moved->renderMoveActive){arrived=true;break;}}
        if(!arrived)return fail("MCV did not drive to chosen open ground");
        for(int n=0;n<30;++n)step(s);
        if(!findObjectById(s,id)||runtimeOwnerHasConstructionYard(s,s.economy.owner))return fail("open ground caused automatic deployment");
        if(!action())return fail("second explicit deployment rejected");
        for(int tick=0;tick<s.config.fixedTickHz*6&&findObjectById(s,id);++tick)step(s);
        if(findObjectById(s,id)||s.selectedObjectIds.size()!=1)return fail("second explicit deployment did not finish");
        const auto* yard=findObjectById(s,s.selectedObjectIds.front());
        if(!yard||yard->type!=type[2]||runtimePlacementAnchorCellForFootprint(runtimeCellPointForObject(*yard),runtimeStructureFootprintForObject(s,*yard))!=wide||s.economy.credits!=credits)return fail("yard not at player's chosen position or charged credits");
        for(int n=0;n<240;++n){++s.tick;advanceRuntimeBuildAnimations(s);}
        if(!capture("deployed",wide))return fail(error);
        ++cases;std::cout<<scene<<" blocked=retained adviser="<<voice.sample<<" manualMove=yes explicitRetry=yes\n";
    }
    std::cout<<"MCV deployment position: cases="<<cases<<" rejections="<<rejections<<" factionVoices="<<voices<<" fullSaves="<<saves<<" restoredTicks="<<reloadedTicks<<" PASS\n";
    return true;
}
