bool runRuntimeNoCooldownSmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail=[&](const std::string& why){error="No cooldown: "+why;return false;};
    const char* directory=std::getenv("CONCORD_NO_COOLDOWN_DIR");
    for(int hz:{15,30,60}) {
        auto storage=std::make_unique<RuntimeWindowState>();auto& state=*storage;
        state.config=config;state.config.fixedTickHz=hz;state.config.gameMode="Battle";
        state.config.gameModeRulesOverride.clear();state.config.campaignId.clear();
        state.config.campaignMissionId.clear();state.config.noCooldown=false;
        state.config.computerAiEnabled=false;state.config.multiplayerActive=false;
        state.config.startingCredits=100000;state.config.startingUnits=0;
        state.config.playerCountry="Americans";state.config.playerSlots.clear();
        state.config.playerSlots.resize(2);
        for(std::size_t i=0;i<2;++i){auto& slot=state.config.playerSlots[i];
            slot.enabled=true;slot.human=i==0;slot.local=i==0;slot.team="-";
            slot.country=i==0?"Americans":"Russians";slot.startPosition=static_cast<int>(i)+1;}
        state.runtimeSuppressPlayerAudioFeedback=true;state.runtimePreferencePersistenceEnabled=false;
        state.report=buildRuntimeStartupReport(state.config,&state.world,&state.theater,
            &state.art,&state.rules,&state.mapFacts);
        state.rulesLoaded=state.report.rulesLoaded;state.artLoaded=state.report.artLoaded;
        state.theaterLoaded=state.report.theaterLoaded;
        if(!state.report.success)return fail("startup");
        initializeRuntimeNavigation(state);initializeRuntimeEconomy(state);
        if(!initializeRuntimeSkirmishStart(state))return fail(state.economy.status);
        if(!deployRuntimeMcv(state,state.economy.skirmishStartObjectId,false))return fail("manual opening deployment");
        state.triggers.bootstrapComplete=true;
        const auto tickProduction=[&](){++state.tick;advanceRuntimeProductionEconomy(state);};
        const auto tickWorld=[&](){++state.tick;advanceRuntimeSimulation(state);};
        const std::string owner=state.economy.owner;
        const auto* powerCatalog=findRuntimeGameplayCatalogItem(state,owner,"GAPOWR");
        if(!powerCatalog)return fail("power catalog");
        const RuntimeBuildCatalogItem power=*powerCatalog;
        if(!dispatchRuntimeQueueProductionFromUi(state,power))return fail("normal queue");
        tickProduction();
        const auto& normal=state.economy.localProductionLanes.lanes[0];
        if(!normal.active || normal.active->remainingTicks<=0 || normal.ready)
            return fail("disabled option skipped production");
        state.config.noCooldown=true;
        if(!runtimeNoCooldownForOwner(state,owner)
            || runtimeNoCooldownForOwner(state,runtimeSkirmishOwnerForSlot(1)))return fail("player/AI scope");
        state.config.multiplayerActive=true;
        if(runtimeNoCooldownForOwner(state,owner))return fail("network scope");
        state.config.multiplayerActive=false;state.campaignMode=true;
        if(runtimeNoCooldownForOwner(state,owner))return fail("campaign scope");
        state.campaignMode=false;
        tickProduction();
        if(!runtimeReadyProductionForItem(state,power))return fail("existing order did not finish in one tick");

        // Both startup metadata and the in-match transaction restore the
        // saved option, even when the currently running match has it off.
        std::ostringstream saved;
        if(!writeRuntimeStateToStream(state,saved,error))return false;
        IniFile ini;ini.loadFromString(saved.str());
        auto loaded=std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*loaded,state,state.visionEnabled);loaded->config.noCooldown=false;
        if(!materializeRuntimeLoadStateFromIni(*loaded,ini,error) || !loaded->config.noCooldown)
            return fail("save option restoration: "+error);
        const auto productionHash=runtimeProductionLanesStateHash(state.economy.localProductionLanes);
        state.config.noCooldown=false;commitRuntimeLoadedState(state,*loaded);
        if(!state.config.noCooldown || runtimeProductionLanesStateHash(state.economy.localProductionLanes)!=productionHash)
            return fail("live save transaction lost option/order");
        if(directory && hz==30) {
            const auto path=std::filesystem::path(directory)/"no-cooldown-save.ini";
            {std::ofstream file(path,std::ios::binary);file<<saved.str();if(!file.good())return fail("save file");}
            SkirmishSaveInfo info;
            if(!inspectSkirmishSave(pathString(path),info)||!info.noCooldown)return fail("main-menu save metadata");
            std::string bad=saved.str();const auto pos=bad.find("NoCooldown=yes");
            if(pos==std::string::npos)return fail("saved option field");
            bad.replace(pos,14,"NoCooldown=invalid");IniFile badIni;badIni.loadFromString(bad);
            auto rejected=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*rejected,state,false);
            if(materializeRuntimeLoadStateFromIni(*rejected,badIni,error))return fail("invalid saved option accepted");
            std::string legacy=saved.str();legacy.erase(pos,15);IniFile oldIni;oldIni.loadFromString(legacy);
            auto old=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*old,state,false);
            if(!materializeRuntimeLoadStateFromIni(*old,oldIni,error)||old->config.noCooldown)
                return fail("old saves must default off");
            error.clear();
        }
        if(directory && hz==30) {
            RuntimeBackBuffer buffer;HDC screen=GetDC(nullptr);
            const bool allocated=ensureRuntimeBackBuffer(buffer,screen,1024,768);if(screen)ReleaseDC(nullptr,screen);
            if(!allocated)return fail("sidebar surface");
            state.activeBuildTab=RuntimeBuildKind::Building;
            drawRuntimeSidebar(buffer.memoryDc,{0,0,1024,768},state,0);GdiFlush();
            const bool written=writeRuntimeSmokeBmp(std::string(directory)+"/no-cooldown-ready.bmp",
                buffer.pixels,1024,768,error);releaseRuntimeBackBuffer(buffer);if(!written)return false;
        }
        for(const char* id:{"GAPOWR","GAPILE","GAREFN","GAWEAP","AMRADR","GAWALL"}) {
            const auto* catalog=findRuntimeGameplayCatalogItem(state,owner,id);
            if(!catalog)return fail(std::string("catalog ")+id);
            const RuntimeBuildCatalogItem item=*catalog;
            if(std::string(id)!="GAPOWR") {
                const int before=state.economy.credits;
                if(!dispatchRuntimeQueueProductionFromUi(state,item))return fail(std::string("queue ")+id+": "+state.economy.status);
                tickProduction();
                const auto* ready=runtimeReadyProductionForItem(state,item);
                if(!ready || before-state.economy.credits!=ready->cost)
                    return fail(std::string("instant ready/price ")+id);
            }
            const auto ready=runtimeOldestReadyPlacement(state);
            const auto cell=findRuntimePlacementCellNearBase(state,item);
            if(!ready || !cell || !beginRuntimePlacementFromUi(state,*ready,true)
                || !placeRuntimeReadyBuilding(state,*cell))return fail(std::string("place ")+id+": "+state.economy.status);
            // Buildup is an animation, separate from sidebar production time.
            for(int i=0;i<10*hz;++i) {
                const bool building=std::any_of(state.world.getObjects().begin(),state.world.getObjects().end(),
                    [](const auto& object){return object.alive && object.buildAnimTicksRemaining>0;});
                if(!building)break;tickWorld();
            }
            tickWorld();
        }
        for(const char* id:{"E1","MTNK","ORCA"}) {
            const auto* catalog=findRuntimeGameplayCatalogItem(state,owner,id);
            if(!catalog)return fail(std::string("unit catalog ")+id);
            const RuntimeBuildCatalogItem item=*catalog;
            const auto count=[&](){return std::count_if(state.world.getObjects().begin(),state.world.getObjects().end(),
                [&](const auto& object){return object.alive && object.owner==owner && object.type==id;});};
            const auto before=count();
            for(int i=0;i<3;++i)if(!dispatchRuntimeQueueProductionFromUi(state,item))
                return fail(std::string("unit queue ")+id+": "+state.economy.status);
            for(int i=0;i<30*hz && count()<before+3;++i) {
                tickWorld();
                for(const auto& lane:state.economy.localProductionLanes.lanes)
                    if(lane.active && lane.active->itemKey==id && lane.active->remainingTicks>0)
                        return fail(std::string("unit retained production time ")+id);
            }
            if(count()!=before+3)
                return fail(std::string("repeated unit delivery ")+id+": "+state.economy.status);
        }
        std::cout<<"No cooldown runtime: hz="<<hz<<" buildings=6 infantry/tanks/aircraft=3-each save/menu/scope=ok\n";
    }
    std::cout<<"No cooldown: passed\n";return true;
}
