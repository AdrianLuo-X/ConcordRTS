bool runRuntimeChronoGroupSmoke(const RuntimeConfig& config,std::string& error) {
    std::cout<<std::unitbuf;std::string scene;
    const auto fail=[&](const std::string& why){error="Chrono group: "+scene+": "+why;return false;};
    const char* output=std::getenv("CONCORD_CHRONO_GROUP_OUTPUT");const std::string root=output?output:"";
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success)return fail("native resources");
    int cases=0,arrivals=0,saves=0,paired=0,noops=0;
    const RuntimeCellPoint target{130,228};
    for(const char* type:{"CLEG","CCOMAND","CIVAN"})for(int hz:{30,15,60})for(int layout:{0,1,2,3}) {
        scene=std::string(type)+"-"+std::to_string(hz)+"-"+std::to_string(layout);
        const int count=layout==0?3:layout==1?12:6;
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);
        s.config=config;s.config.fixedTickHz=hz;s.config.playerCountry="Americans";s.config.playerSlots.clear();
        s.config.computerAiEnabled=s.config.shortGame=s.config.crates=s.config.noCooldown=s.config.multiplayerActive=false;
        s.config.startingUnits=0;s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
        s.world=assets->world;s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        auto& cells=s.world.accessTerrainCells();
        cells.erase(std::remove_if(cells.begin(),cells.end(),[](const auto& c){return c.x<94||c.x>142||c.y<200||c.y>240;}),cells.end());
        if(layout==1)for(auto& c:cells)if(c.x==116)c.level=3; // An impassable, edge-to-edge height barrier.
        s.report=assets->report;s.rules=assets->rules;s.art=assets->art;s.theater=assets->theater;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.economy.owner="Americans";s.economy.credits=100000;s.economy.skirmishStartApplied=true;
        initializeRuntimeLocalProductionLanes(s.economy);s.triggers.loaded=s.triggers.bootstrapComplete=true;s.visionEnabled=false;
        s.terrainByCell=buildRuntimeTerrainLookup(s.world);initializeRuntimeNavigation(s);
        std::vector<int> ids;
        for(int n=0;n<count;++n) {
            const auto cell=layout==3 && n>=4 ? target : RuntimeCellPoint{102+n%4,207+n/4};
            auto* o=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,type,cell);
            if(!o)return fail("native infantry");o->mission="Sleep";ids.push_back(o->instanceId);
        }
        if(layout==2)for(int n=0;n<2;++n) {
            auto* o=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,"E1",target);
            if(!o)return fail("stationary destination infantry");o->mission="Sleep";
        }
        const auto step=[](RuntimeWindowState& state){RuntimeSnapshotActiveStateScope scope(state);++state.tick;advanceRuntimeSimulation(state,false);};
        step(s);s.selectedObjectIds=ids;
        if(layout==1) {
            const auto occupied=buildRuntimeOccupancyLookup(s);
            const auto* first=findObjectById(s,ids.front());
            const auto components=buildRuntimeMoveComponentByCell(s,*first,s.terrainByCell,occupied);
            const auto from=components.find(runtimeCellKey(first->x,first->y)),to=components.find(runtimeCellKey(target.x,target.y));
            if(from==components.end()||to==components.end()||from->second==to->second)return fail("height barrier did not separate walkable components");
        }
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,target)
            ||s.lastCommand.acceptedCount!=count)return fail("group command accepted "+std::to_string(s.lastCommand.acceptedCount)+" of "+std::to_string(count));
        for(int n=0;n<(layout==3?6:2);++n)step(s);
        const auto capture=[&](const char* suffix){return root.empty()||hz!=30||drawRuntimeStateOffscreenAtCell(s,target,800,600,error,root+"/"+scene+"-"+suffix+".bmp",false).has_value();};
        int arrived=0;std::set<std::pair<int,int>> occupiedSlots;
        for(const auto& o:s.world.getObjects())if(o.kind==WorldObjectKind::Infantry) {
            if(!RuntimeSubcellSystemState::isActiveSubCell(o.subCell)||!occupiedSlots.emplace(runtimeCellKey(o.x,o.y),o.subCell).second)
                return fail("invalid or overlapping infantry subcell");
        }
        for(int id:ids) {
            const auto* o=findObjectById(s,id);
            if(o&&std::max(std::abs(o->x-target.x),std::abs(o->y-target.y))<=2)++arrived;
        }
        if(arrived!=count){capture("failed");return fail("arrived "+std::to_string(arrived)+" of "+std::to_string(count)+"; others remained at source");}
        if(layout==0)for(int id:ids)if(runtimeCellPointForObject(*findObjectById(s,id))!=target)return fail("three soldiers did not share the clicked cell");
        if(!capture("arrival"))return fail("arrival rendering: "+error);
        if(hz==30) {
            for(bool exact:{false,true}) {
                std::ostringstream saved;IniFile ini;
                if(!writeRuntimeStateToStream(s,saved,error)||(exact&&!writeRuntimeMultiplayerSnapshotExtension(s,saved,error))||!ini.loadFromString(saved.str()))return fail("save: "+error);
                auto clone=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*clone,s,false);
                if(!materializeRuntimeLoadStateFromIni(*clone,ini,error,exact)||(exact&&!materializeRuntimeMultiplayerSnapshotExtension(*clone,ini,error)))return fail("reload: "+error);
                if(clone->specialMechanisms.stateHash()!=s.specialMechanisms.stateHash())return fail("recovery state changed on load");
                for(int tick=0;tick<12;++tick){step(*clone);++paired;}
                for(int id:ids){const auto* a=findObjectById(s,id);const auto* b=findObjectById(*clone,id);
                    if(!a||!b||a->x!=b->x||a->y!=b->y||a->subCell!=b->subCell)return fail("recovery moved restored infantry");}
                ++saves;
            }
        }
        for(int n=0;n<hz*40;++n) {
            bool locked=false;for(int id:ids)locked=locked||s.specialMechanisms.isChronoMovementLocked(id,s.tick);
            if(!locked)break;step(s);
        }
        for(int id:ids)if(s.specialMechanisms.isChronoMovementLocked(id,s.tick))return fail("recovery never ended");
        const auto* first=findObjectById(s,ids.front());const auto here=runtimeCellPointForObject(*first);
        s.selectedObjectIds={ids.front()};const auto effects=s.impactEffects.size();
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,here)
            ||s.lastCommand.acceptedCount!=1||s.impactEffects.size()!=effects||s.specialMechanisms.isChronoInTransit(ids.front()))
            return fail("clicking current cell started an in-place warp or rejected a no-op");
        ++noops;++cases;arrivals+=arrived;
        std::cout<<scene<<" arrived="<<arrived<<" subcells=unique noOp=yes\n";
    }
    std::cout<<"Chrono group: cases="<<cases<<" arrivals="<<arrivals<<" fullSaves="<<saves<<" recoveryTicks="<<paired<<" noOps="<<noops<<" PASS\n";
    return true;
}
