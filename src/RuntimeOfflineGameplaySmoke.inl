bool runRuntimeOfflineGameplaySmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail=[&](const std::string& text){error="Offline gameplay: "+text;return false;};
    auto source=std::make_unique<RuntimeWindowState>();
    source->config=config;source->config.computerAiEnabled=false;source->config.shortGame=false;
    source->config.playerCountry="Americans";source->config.startingUnits=3;
    source->config.mcvRepacks=true;
    source->config.playerSlots.clear();
    RuntimeConfig::PlayerSlot player;player.enabled=player.human=player.local=true;
    player.country="Americans";player.color="Gold";player.startPosition=1;
    source->config.playerSlots.push_back(player);
    player.human=player.local=false;player.country="Russians";player.color="Red";player.startPosition=2;
    source->config.playerSlots.push_back(player);
    source->runtimeSoundsEnabled=false;source->runtimeSuppressPlayerAudioFeedback=true;
    source->runtimePreferencePersistenceEnabled=false;
    source->report=buildRuntimeStartupReport(source->config,&source->world,&source->theater,
        &source->art,&source->rules,&source->mapFacts);
    source->theaterLoaded=source->report.theaterLoaded;
    source->artLoaded=source->report.artLoaded;source->rulesLoaded=source->report.rulesLoaded;
    if(!source->report.success)return fail("original map/resources unavailable");
    if(!initializeRuntimeCooperativeSession(*source))return fail("session setup");
    loadRuntimeSoundDatabase(*source);ensureRuntimeLightingInitialized(*source);ensureRuntimeParticlesInitialized(*source);
    initializeRuntimeNavigation(*source);initializeRuntimeEconomy(*source);initializeRuntimeSkirmishStart(*source);
    if(!initializeRuntimeTriggers(*source))return fail("trigger setup");
    source->visionEnabled=true;updateRuntimeVisionState(*source);
    completeRuntimeWorldRefresh(*source);
    const auto initial=runtimeStandaloneSimulationStateHash(*source,error);
    if(!initial)return fail("initial hash: "+error);
    std::ostringstream saved;
    if(!writeRuntimeStateToStream(*source,saved,error))return fail("initial full state: "+error);
    if(!writeRuntimeMultiplayerSnapshotExtension(*source,saved,error))return fail("initial exact state: "+error);
    IniFile ini;if(!ini.loadFromString(saved.str()))return fail("initial INI");
    auto restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,*source,false);
    if(!materializeRuntimeLoadStateFromIni(*restored,ini,error))return fail("restore initial full state: "+error);
    initializeRuntimeNavigation(*restored);completeRuntimeWorldRefresh(*restored);
    if(!materializeRuntimeMultiplayerSnapshotExtension(*restored,ini,error))return fail("restore exact state: "+error);
    const auto compare=[&](const char* phase){
        std::vector<std::pair<std::string,std::uint64_t>> a,b;
        const auto first=runtimeStandaloneSimulationStateHash(*source,error,&a);
        const auto second=runtimeStandaloneSimulationStateHash(*restored,error,&b);
        if(first && first==second)return true;
        std::cout<<"Simulation source/restored world="<<source->world.deterministicSimulationStateHash()
            <<'/'<<restored->world.deterministicSimulationStateHash()<<'\n';
        const auto comparePart=[&](const char* label,auto fill){
            GameWorld left,right;fill(left,source->world);fill(right,restored->world);
            if(left.deterministicSimulationStateHash()!=right.deterministicSimulationStateHash())
                std::cout<<"Simulation world differs: "<<label<<'\n';
        };
        comparePart("terrain",[](GameWorld& a,const GameWorld& b){a.accessTerrainCells()=b.getTerrainCells();});
        comparePart("terrain objects",[](GameWorld& a,const GameWorld& b){a.accessTerrainObjects()=b.getTerrainObjects();});
        comparePart("smudges",[](GameWorld& a,const GameWorld& b){a.accessSmudges()=b.getSmudges();});
        comparePart("units/buildings",[](GameWorld& a,const GameWorld& b){a.accessObjects()=b.getObjects();});
        for(const auto& object:source->world.getObjects()) {
            const auto* other=findObjectById(*restored,object.instanceId);
            GameWorld left,right;left.accessObjects()={object};if(other)right.accessObjects()={*other};
            if(left.deterministicSimulationStateHash()!=right.deterministicSimulationStateHash())
                std::cout<<"Simulation object differs: "<<object.instanceId<<' '<<object.type<<'\n';
        }
        std::ofstream before("build/smoke/feature-set-17/23-remove-replay/foundation-source.ini");
        std::ofstream after("build/smoke/feature-set-17/23-remove-replay/foundation-restored.ini");
        std::string diagnosticError;
        writeRuntimeStateToStream(*source,before,diagnosticError);writeRuntimeStateToStream(*restored,after,diagnosticError);
        writeRuntimeMultiplayerSnapshotExtension(*source,before,diagnosticError);
        writeRuntimeMultiplayerSnapshotExtension(*restored,after,diagnosticError);
        std::string section="final";
        for(std::size_t n=0;n<std::min(a.size(),b.size());++n)if(a[n]!=b[n]){section=a[n].first;break;}
        return fail(std::string(phase)+" diverged at tick "+std::to_string(source->tick)+" "+section+" "+error);
    };
    if(!compare("initial state"))return false;
    int actor=-1;
    for(const auto& object:source->world.getObjects()) {
        if(object.alive && object.kind==WorldObjectKind::Unit
            && runtimeCanonicalHumanHouseOwner(object.owner)==runtimeCanonicalHumanHouseOwner(source->economy.owner)
            && object.instanceId!=source->economy.skirmishStartObjectId) {
            actor=object.instanceId;break;
        }
    }
    if(actor<0)return fail("local starting unit missing");
    const auto* unit=findObjectById(*source,actor);
    const RuntimeCellPoint target{unit->x+3,unit->y};
    source->selectedObjectIds=restored->selectedObjectIds={actor};
    issueRuntimeMoveCommand(*source,target);issueRuntimeMoveCommand(*restored,target);
    for(int n=0;n<90;++n) {
        ++source->tick;++restored->tick;
        const bool light=n%13==0;
        advanceRuntimeSimulation(*source,light);advanceRuntimeSimulation(*restored,light);
        completeRuntimeWorldRefresh(*source);completeRuntimeWorldRefresh(*restored);
        if(!compare("re-simulation"))return false;
    }
    const auto before=runtimeStandaloneSimulationStateHash(*source,error);
    source->camera.worldOffsetX+=100;source->camera.worldOffsetY-=80;
    source->selectedObjectIds.clear();source->runtimeSoundEvents+=100;
    if(runtimeStandaloneSimulationStateHash(*source,error)!=before)return fail("view/audio changed simulation authority hash");
    initializeRuntimeOfflineGameplayRouting(*source);
    int acceptedCommands=0;
    for(int index=0;index<180;++index) {
        if(index==90) {
            std::ostringstream saved;IniFile ini;auto loaded=std::make_unique<RuntimeWindowState>();
            seedRuntimeLoadScratch(*loaded,*source,false);
            if(!writeRuntimeStateToStream(*source,saved,error)||!ini.loadFromString(saved.str())
                ||!materializeRuntimeLoadStateFromIni(*loaded,ini,error))return fail("mid-match save: "+error);
            commitRuntimeLoadedState(*source,*loaded);
            if(!source->offlineGameplayIntentRouting)return fail("save lost local routing");
        }
        source->selectedObjectIds={actor};
        if(index%3==2) acceptedCommands+=dispatchRuntimeSelectionCommandFromUi(*source,
            RuntimeMultiplayerGameplayCommandType::Stop)?1:0;
        else acceptedCommands+=dispatchRuntimeTargetOrderFromUi(*source,
            RuntimeMultiplayerGameplayTargetOrderKind::Move,{target.x,target.y+(index%2)*3})?1:0;
        for(int step=0;step<4;++step) {
            ++source->tick;
            const bool light=(index+step)%17==0;
            advanceRuntimeSimulation(*source,light);
        }
    }
    if(acceptedCommands<100)return fail("canonical offline commands did not execute");
    if(!dispatchRuntimeSecondaryActionFromUi(*source,source->economy.skirmishStartObjectId))
        return fail("manual opening MCV deployment");
    for(int step=0;step<source->config.fixedTickHz*30;++step) {
        const auto* deployed=source->selectedObjectIds.size()==1
            ?findObjectById(*source,source->selectedObjectIds.front()):nullptr;
        if(deployed&&deployed->buildAnimTicksRemaining<=0)break;
        ++source->tick;
        advanceRuntimeSimulation(*source,false);
    }
    const auto* powerCatalog=findRuntimeGameplayCatalogItem(*source,source->economy.owner,"GAPOWR");
    if(!powerCatalog)return fail("original power plant unavailable");
    const auto powerItem=*powerCatalog;
    int yardId=-1;
    for(const auto& object:source->world.getObjects())if(object.alive && object.type=="GACNST"
        && object.owner==source->economy.owner)yardId=object.instanceId;
    if(yardId<0)return fail("local construction yard missing");
    source->selectedObjectIds={yardId};
    if(!dispatchRuntimePlayerFactoryFromUi(*source,yardId,std::nullopt)
        || !dispatchRuntimePlayerFactoryFromUi(*source,yardId,RuntimeMultiplayerGameplayCell{target.x,target.y}))
        return fail("factory selection and rally point");
    if(!dispatchRuntimeSecondaryActionFromUi(*source,yardId))return fail("yard repack");
    for(int step=0;step<source->config.fixedTickHz*30 && findObjectById(*source,yardId);++step) {
        ++source->tick;
        advanceRuntimeSimulation(*source,false);
    }
    if(source->selectedObjectIds.size()!=1)return fail("repack lost selection");
    const int mcvId=source->selectedObjectIds.front();
    const auto* mcv=findObjectById(*source,mcvId);
    if(!mcv || mcv->type!="AMCV" || !mcv->alive)return fail("repack did not select replacement MCV");
    if(!dispatchRuntimeSecondaryActionFromUi(*source,mcvId))return fail("MCV deploy");
    if(source->selectedObjectIds.size()!=1)return fail("deploy lost selection");
    const auto* yard=findObjectById(*source,source->selectedObjectIds.front());
    if(!yard || yard->type!="GACNST" || !yard->alive)return fail("deploy did not select replacement yard");
    if(!dispatchRuntimeQueueProductionFromUi(*source,powerItem))return fail("initial queue");
    // A queued item becomes active at the next simulation boundary. Right
    // click before that boundary correctly cancels a queued item, not holds it.
    ++source->tick;
    advanceRuntimeSimulation(*source,false);
    if(!dispatchRuntimeBuildButtonSecondaryActionFromUi(*source,powerItem)
        || !dispatchRuntimeQueueProductionFromUi(*source,powerItem)
        || !dispatchRuntimeBuildButtonSecondaryActionFromUi(*source,powerItem)
        || !dispatchRuntimeBuildButtonSecondaryActionFromUi(*source,powerItem)
        || !dispatchRuntimeQueueProductionFromUi(*source,powerItem))
        return fail("queue/hold/resume/cancel");
    int productionSteps=0;
    while(!runtimeReadyProductionForItem(*source,powerItem) && productionSteps<3000) {
        ++source->tick;
        advanceRuntimeSimulation(*source,false);++productionSteps;
    }
    RuntimeProductionLane lane;
    const auto* ready=runtimeReadyProductionForItem(*source,powerItem,&lane);
    const auto placement=findRuntimePlacementCellNearBase(*source,powerItem);
    if(!ready || !placement)return fail("power plant never became placeable");
    if(!source->multiplayerIntentRouter.beginPlacement(runtimeGameplayLaneForUi(lane),ready->serial,powerItem.typeId,error)
        || !source->multiplayerIntentRouter.previewPlacement({placement->x,placement->y},true,error)
        || !source->multiplayerIntentRouter.confirmPlacement(error))return fail("building placement: "+error);
    int powerId=-1;
    for(const auto& object:source->world.getObjects())if(object.alive && object.type=="GAPOWR"
        && object.owner==source->economy.owner)powerId=object.instanceId;
    if(powerId<0)return fail("placed building missing");
    (void)dispatchRuntimeStructureToolFromUi(*source,RuntimeSidebarTool::Repair,powerId);
    if(!dispatchRuntimeStructureToolFromUi(*source,RuntimeSidebarTool::Sell,powerId))return fail("building sale");
    source->selectedObjectIds={actor};
    const auto* gunner=findObjectById(*source,actor);
    if(!gunner)return fail("firing unit missing");
    if(!dispatchRuntimeTargetOrderFromUi(*source,RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell,
        {gunner->x+2,gunner->y}))return fail("forced attack");
    for(int n=0;n<90;++n) {
        ++source->tick;
        advanceRuntimeSimulation(*source,false);
    }
    activateRuntimeMenuAction(nullptr,*source,RuntimeMenuAction::ConfirmExit);
    if(!source->skirmishMatchFinished)return fail("resignation did not end the match");
    std::cout<<"Offline gameplay: "<<acceptedCommands<<" travel commands; deploy/repack, factory/rally, "
        <<"production/hold/cancel/place/repair/sell, attack, resignation, full save/reload and 90 paired ticks passed\n";
    return true;
}
