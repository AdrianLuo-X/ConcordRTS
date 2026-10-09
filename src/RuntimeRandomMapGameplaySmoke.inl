bool runRuntimeRandomMapGameplaySmoke(const RuntimeConfig& config,std::string& error) {
    std::cout << std::unitbuf;
    const auto fail=[&](const std::string& why){error="Random map gameplay: "+why;return false;};
    const char* output=std::getenv("CONCORD_RANDOM_MAP_OUTPUT");
    const std::filesystem::path root=output?output:"build/smoke/random-map-gameplay";
    std::filesystem::create_directories(root);
    int passed=0;
    for (int climate=0;climate<2;++climate) for (int shape=0;shape<5;++shape) {
        const std::string id=std::to_string(shape)+"-"+std::to_string(climate);
        RuntimeRandomMapOptions options; options.environment=shape; options.theater=climate;
        options.resources=shape==0?3:1; options.players=shape==0?8:4;
        options.size=0; options.timeOfDay=climate==1&&shape==3?3:1;
        options.seed=0x914140U+shape*10+climate;
        RuntimeRandomMapResult generated; std::filesystem::path path;
        if (!runtimeGenerateRandomMap(config,options,generated,error)
            || !runtimeStoreRandomMap(generated,root/"maps",path,error)) return fail(id+": "+error);
        auto storage=std::make_unique<RuntimeWindowState>(); auto& state=*storage;
        state.config=config; state.config.defaultMapPath=path.string();
        state.config.gameMode="Battle"; state.config.gameModeRulesOverride="MPBattleMD.ini";
        state.config.computerAiEnabled=true; state.config.crates=false; state.config.startingUnits=4;
        state.config.startingCredits=200000; state.config.shortGame=false;
        state.config.playerSlots.assign(options.players,{});
        const std::string country=std::array<std::string,3>{"Americans","Russians","YuriCountry"}[(shape+climate)%3];
        for (int i=0;i<options.players;++i) {
            auto& slot=state.config.playerSlots[i]; slot.enabled=true; slot.human=i==0; slot.local=i==0;
            slot.country=country; slot.team="-"; slot.startPosition=i+1;
        }
        state.runtimeSoundsEnabled=false; state.runtimeSuppressPlayerAudioFeedback=true;
        state.runtimePreferencePersistenceEnabled=false;
        state.report=buildRuntimeStartupReport(state.config,&state.world,&state.theater,&state.art,&state.rules,&state.mapFacts);
        state.rulesLoaded=state.report.rulesLoaded; state.artLoaded=state.report.artLoaded;
        state.theaterLoaded=state.report.theaterLoaded; state.mapFactsLoaded=true;
        if (!state.report.success) return fail(id+" startup failed");
        initializeRuntimeNavigation(state); initializeRuntimeEconomy(state);
        if (!initializeRuntimeSkirmishStart(state)) return fail(id+" could not open the match: "+state.economy.skirmishStatus);
        std::vector<int> openingMcvs;
        for (const auto& object:state.world.getObjects()) {
            const auto* unit=findRuntimeUnitType(state.rules,object);
            if(object.alive&&unit&&runtimeUnitTypeIsMcv(state,*unit))openingMcvs.push_back(object.instanceId);
        }
        for(int mcv:openingMcvs)if(!deployRuntimeMcv(state,mcv,false))return fail(id+" explicit base deployment");
        state.triggers.bootstrapComplete=true; state.visionEnabled=false;
        state.lighting.reset(state.mapFacts.lighting); state.lightingInitialized=true;
        for (int i=0;i<options.players;++i) if (!runtimeOwnerHasConstructionYard(state,runtimeSkirmishOwnerForSlot(i)))
            return fail(id+" missing base at start "+std::to_string(i+1));
        ensureRuntimeComputerAiStates(state);
        for (auto& [owner,ai]:state.computerAiByOwner) { (void)owner; ai.nextThinkTick=100000000; }
        const bool soviet=runtimeCountryIsSoviet(country),yuri=runtimeCountryIsYuri(country);
        const std::array<std::string,2> buildings{yuri?"YAPOWR":soviet?"NAPOWR":"GAPOWR",yuri?"YAREFN":soviet?"NAREFN":"GAREFN"};
        for (const auto& building:buildings) {
            const auto* found=findRuntimeGameplayCatalogItem(state,state.economy.owner,building);
            if (!found) return fail(id+" missing building "+building);
            const RuntimeBuildCatalogItem item=*found;
            if (!queueRuntimeBuildOrder(state,item)) return fail(id+" cannot queue "+building+": "+state.economy.status);
            ++state.tick; advanceRuntimeProductionEconomy(state);
            if (!completeRuntimeActiveProductionForProbe(state,item)) return fail(id+" production did not start");
            ++state.tick; advanceRuntimeProductionEconomy(state);
            const auto ready=runtimeOldestReadyPlacement(state);
            if (!ready || !beginRuntimePlacementFromUi(state,*ready,true)) return fail(id+" sidebar ready action failed");
            const auto cell=findRuntimePlacementCellNearBase(state,item);
            if (!cell || !placeRuntimeReadyBuilding(state,*cell)) return fail(id+" no building space for "+building+": "+state.economy.status);
            for (int i=0;i<240;++i) {++state.tick;advanceRuntimeBuildAnimations(state);}
        }
        // A complete trip on a real generated battlefield confirms that the
        // authored mineral cells are reachable and can pay the player's account.
        if (shape==3 && climate==0) {
            const int trips=state.economy.resourceTrips;
            for (int i=0;i<5000 && state.economy.resourceTrips==trips;++i) {++state.tick;advanceRuntimeSimulation(state);}
            if (state.economy.resourceTrips==trips) {
                std::string diagnostic;
                saveRuntimeState(state,(root/"stalled-mine.ini").string(),diagnostic);
                drawRuntimeStateOffscreenAtCell(state,{generated.starts[0].x,generated.starts[0].y},960,800,diagnostic,(root/"stalled-mine.bmp").string(),false);
                return fail("generated mine did not deliver: matchFinished="+std::to_string(state.skirmishMatchFinished)
                    +" harvesters="+std::to_string(state.economy.harvesters)+" refineries="+std::to_string(state.economy.refineries)
                    +" status="+state.economy.status);
            }
            const int cash=state.economy.credits;
            const auto save=(root/"random-map-save.ini").string();
            if (!saveRuntimeState(state,save,error)) return fail(error);
            state.economy.credits+=123;
            if (!loadRuntimeState(state,save,error) || state.economy.credits!=cash) return fail("generated battlefield save/load failed: "+error);
        }
        const auto capture=[&](const std::string& suffix,RuntimeCellPoint cell) {
            const auto stats=drawRuntimeStateOffscreenAtCell(state,cell,960,800,error,(root/(id+suffix+".bmp")).string(),false);
            if (!stats || stats->texturedTerrainCells==0 || stats->fallbackTerrainCells!=0)
                return fail(id+suffix+" original terrain missing: "+error);
            return true;
        };
        const auto& start=generated.starts.front();
        if (!capture("-base",{start.x,start.y})) return false;
        if (shape==4) {
            const auto peak=std::max_element(state.world.getTerrainCells().begin(),state.world.getTerrainCells().end(),[](const auto& a,const auto& b){return a.level<b.level;});
            if (peak==state.world.getTerrainCells().end() || peak->level==0 || !capture("-mountain",{peak->x,peak->y})) return fail(id+" mountain did not rise above the base");
        }
        std::cout<<"Random map match passed: "<<id<<" / "<<country<<" / starts="<<options.players<<" / lamps="<<runtimeMapLightSources(state.world,&state.rules).size()<<'\n';
        ++passed;
    }
    std::cout<<"Random map gameplay: "<<passed<<" actual battlefields, base construction, mining, saves and original terrain frames passed.\n";
    return true;
}
