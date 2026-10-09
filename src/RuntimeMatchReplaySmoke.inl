bool runRuntimeMatchReplayMultiplayerSmoke(const RuntimeConfig& config, std::string& error);
bool runRuntimeMatchReplayCampaignSmoke(const RuntimeConfig& config, std::string& error);

bool runRuntimeMatchReplayFoundationSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail=[&](const std::string& text){error="Match replay: "+text;return false;};
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
    const auto initial=runtimeMatchReplayStateHash(*source,error);
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
        const auto first=runtimeMatchReplayStateHash(*source,error,&a);
        const auto second=runtimeMatchReplayStateHash(*restored,error,&b);
        if(first && first==second)return true;
        std::cout<<"Replay source/restored world="<<source->world.deterministicSimulationStateHash()
            <<'/'<<restored->world.deterministicSimulationStateHash()<<'\n';
        const auto comparePart=[&](const char* label,auto fill){
            GameWorld left,right;fill(left,source->world);fill(right,restored->world);
            if(left.deterministicSimulationStateHash()!=right.deterministicSimulationStateHash())
                std::cout<<"Replay world differs: "<<label<<'\n';
        };
        comparePart("terrain",[](GameWorld& a,const GameWorld& b){a.accessTerrainCells()=b.getTerrainCells();});
        comparePart("terrain objects",[](GameWorld& a,const GameWorld& b){a.accessTerrainObjects()=b.getTerrainObjects();});
        comparePart("smudges",[](GameWorld& a,const GameWorld& b){a.accessSmudges()=b.getSmudges();});
        comparePart("units/buildings",[](GameWorld& a,const GameWorld& b){a.accessObjects()=b.getObjects();});
        for(const auto& object:source->world.getObjects()) {
            const auto* other=findObjectById(*restored,object.instanceId);
            GameWorld left,right;left.accessObjects()={object};if(other)right.accessObjects()={*other};
            if(left.deterministicSimulationStateHash()!=right.deterministicSimulationStateHash())
                std::cout<<"Replay object differs: "<<object.instanceId<<' '<<object.type<<'\n';
        }
        std::ofstream before("build/smoke/feature-set-11/10-match-replay/foundation-source.ini");
        std::ofstream after("build/smoke/feature-set-11/10-match-replay/foundation-restored.ini");
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
    const auto before=runtimeMatchReplayStateHash(*source,error);
    source->camera.worldOffsetX+=100;source->camera.worldOffsetY-=80;
    source->selectedObjectIds.clear();source->runtimeSoundEvents+=100;
    if(runtimeMatchReplayStateHash(*source,error)!=before)return fail("view/audio changed replay authority hash");
    const auto replayPath=std::filesystem::path("build/smoke/feature-set-11/10-match-replay")
        /("pipeline-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".creplay");
    std::vector<std::pair<std::string,std::uint64_t>> initialTrace;
    runtimeMatchReplayStateHash(*source,error,&initialTrace);
    if(!beginRuntimeMatchRecording(*source,replayPath,error))return fail("record begin: "+error);
    const int resetTick=source->tick;
    const auto resetHash=runtimeMatchReplayStateHash(*source,error);
    const auto resetConfig=serializeRuntimeMatchReplayConfig(source->config);
    std::string resetState;
    if(!writeRuntimeMatchReplayWorld(*source,resetState,error))return fail("reset state: "+error);
    int acceptedCommands=0;
    for(int index=0;index<180;++index) {
        if(index==90) {
            if(!restoreRuntimeMatchReplayWorld(*source,resetConfig,resetState,resetTick,resetHash,error))
                return fail("recording rewind: "+error);
            markRuntimeMatchReplayReset(*source);
        }
        source->selectedObjectIds={actor};
        if(index%3==2) acceptedCommands+=dispatchRuntimeSelectionCommandFromUi(*source,
            RuntimeMultiplayerGameplayCommandType::Stop)?1:0;
        else acceptedCommands+=dispatchRuntimeTargetOrderFromUi(*source,
            RuntimeMultiplayerGameplayTargetOrderKind::Move,{target.x,target.y+(index%2)*3})?1:0;
        for(int step=0;step<4;++step) {
            recordRuntimeMatchReplaySelection(*source);++source->tick;
            const bool light=(index+step)%17==0;
            advanceRuntimeSimulation(*source,light);recordRuntimeMatchReplayStep(*source,light);
        }
    }
    if(acceptedCommands<100)return fail("canonical offline commands did not execute");
    if(!dispatchRuntimeSecondaryActionFromUi(*source,source->economy.skirmishStartObjectId))
        return fail("record manual opening MCV deployment");
    for(int step=0;step<source->config.fixedTickHz*30;++step) {
        const auto* deployed=source->selectedObjectIds.size()==1
            ?findObjectById(*source,source->selectedObjectIds.front()):nullptr;
        if(deployed&&deployed->buildAnimTicksRemaining<=0)break;
        recordRuntimeMatchReplaySelection(*source);++source->tick;
        advanceRuntimeSimulation(*source,false);recordRuntimeMatchReplayStep(*source,false);
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
        return fail("record factory selection and rally point");
    if(!dispatchRuntimeSecondaryActionFromUi(*source,yardId))return fail("record yard repack");
    for(int step=0;step<source->config.fixedTickHz*30 && findObjectById(*source,yardId);++step) {
        recordRuntimeMatchReplaySelection(*source);++source->tick;
        advanceRuntimeSimulation(*source,false);recordRuntimeMatchReplayStep(*source,false);
    }
    if(source->selectedObjectIds.size()!=1)return fail("repack lost selection");
    const int mcvId=source->selectedObjectIds.front();
    const auto* mcv=findObjectById(*source,mcvId);
    if(!mcv || mcv->type!="AMCV" || !mcv->alive)return fail("repack did not select replacement MCV");
    if(!dispatchRuntimeSecondaryActionFromUi(*source,mcvId))return fail("record MCV deploy");
    if(source->selectedObjectIds.size()!=1)return fail("deploy lost selection");
    const auto* yard=findObjectById(*source,source->selectedObjectIds.front());
    if(!yard || yard->type!="GACNST" || !yard->alive)return fail("deploy did not select replacement yard");
    if(!dispatchRuntimeQueueProductionFromUi(*source,powerItem))return fail("record initial queue");
    // A queued item becomes active at the next simulation boundary. Right
    // click before that boundary correctly cancels a queued item, not holds it.
    recordRuntimeMatchReplaySelection(*source);++source->tick;
    advanceRuntimeSimulation(*source,false);recordRuntimeMatchReplayStep(*source,false);
    if(!dispatchRuntimeBuildButtonSecondaryActionFromUi(*source,powerItem)
        || !dispatchRuntimeQueueProductionFromUi(*source,powerItem)
        || !dispatchRuntimeBuildButtonSecondaryActionFromUi(*source,powerItem)
        || !dispatchRuntimeBuildButtonSecondaryActionFromUi(*source,powerItem)
        || !dispatchRuntimeQueueProductionFromUi(*source,powerItem))
        return fail("record queue/hold/resume/cancel");
    int productionSteps=0;
    while(!runtimeReadyProductionForItem(*source,powerItem) && productionSteps<3000) {
        recordRuntimeMatchReplaySelection(*source);++source->tick;
        advanceRuntimeSimulation(*source,false);recordRuntimeMatchReplayStep(*source,false);++productionSteps;
    }
    RuntimeProductionLane lane;
    const auto* ready=runtimeReadyProductionForItem(*source,powerItem,&lane);
    const auto placement=findRuntimePlacementCellNearBase(*source,powerItem);
    if(!ready || !placement)return fail("recorded power plant never became placeable");
    if(!source->multiplayerIntentRouter.beginPlacement(runtimeGameplayLaneForUi(lane),ready->serial,powerItem.typeId,error)
        || !source->multiplayerIntentRouter.previewPlacement({placement->x,placement->y},true,error)
        || !source->multiplayerIntentRouter.confirmPlacement(error))return fail("record building placement: "+error);
    int powerId=-1;
    for(const auto& object:source->world.getObjects())if(object.alive && object.type=="GAPOWR"
        && object.owner==source->economy.owner)powerId=object.instanceId;
    if(powerId<0)return fail("placed building missing");
    (void)dispatchRuntimeStructureToolFromUi(*source,RuntimeSidebarTool::Repair,powerId);
    if(!dispatchRuntimeStructureToolFromUi(*source,RuntimeSidebarTool::Sell,powerId))return fail("record building sale");
    source->selectedObjectIds={actor};
    const auto* gunner=findObjectById(*source,actor);
    if(!gunner)return fail("firing unit missing");
    if(!dispatchRuntimeTargetOrderFromUi(*source,RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell,
        {gunner->x+2,gunner->y}))return fail("record forced attack");
    for(int n=0;n<90;++n) {
        recordRuntimeMatchReplaySelection(*source);++source->tick;
        advanceRuntimeSimulation(*source,false);recordRuntimeMatchReplayStep(*source,false);
    }
    activateRuntimeMenuAction(nullptr,*source,RuntimeMenuAction::ConfirmExit);
    if(!source->skirmishMatchFinished)return fail("recorded resignation did not end the match");
    if(!finishRuntimeMatchRecording(*source,error))return fail("record finish: "+error);
    auto viewer=std::make_unique<RuntimeWindowState>();viewer->config=config;
    if(!openRuntimeMatchReplay(replayPath,viewer->config,viewer->matchReplay,error))return fail("file reopen: "+error);
    viewer->matchReplayViewing=true;
    viewer->runtimeSoundsEnabled=false;viewer->runtimeSuppressPlayerAudioFeedback=true;
    viewer->runtimePreferencePersistenceEnabled=false;
    viewer->report=buildRuntimeStartupReport(viewer->config,&viewer->world,&viewer->theater,
        &viewer->art,&viewer->rules,&viewer->mapFacts);
    viewer->theaterLoaded=viewer->report.theaterLoaded;viewer->artLoaded=viewer->report.artLoaded;
    viewer->rulesLoaded=viewer->report.rulesLoaded;
    if(!viewer->report.success || !initializeRuntimeCooperativeSession(*viewer))return fail("cold bootstrap");
    loadRuntimeSoundDatabase(*viewer);ensureRuntimeLightingInitialized(*viewer);ensureRuntimeParticlesInitialized(*viewer);
    initializeRuntimeNavigation(*viewer);initializeRuntimeEconomy(*viewer);initializeRuntimeSkirmishStart(*viewer);
    if(!initializeRuntimeTriggers(*viewer))return fail("cold triggers");
    completeRuntimeWorldRefresh(*viewer);
    const auto& header=viewer->matchReplay->header;
    std::vector<std::pair<std::string,std::uint64_t>> restoredTrace;std::string restoredData;
    if(!restoreRuntimeMatchReplayWorld(*viewer,header.config,header.initialState,header.initialTick,header.initialHash,error,
        &restoredTrace,&restoredData)) {
        for(std::size_t n=0;n<std::min(initialTrace.size(),restoredTrace.size());++n)
            if(initialTrace[n].second!=restoredTrace[n].second) {
                std::cout<<"Replay cold restore differs: "<<initialTrace[n].first<<" / "<<restoredTrace[n].first<<'\n';break;
            }
        std::ofstream expected(replayPath.string()+".expected.ini");expected<<header.initialState;
        std::ofstream actual(replayPath.string()+".actual.ini");actual<<restoredData;
        return fail("file initial world: "+error);
    }
    viewer->matchReplay->paused=true;
    const auto pausedHash=runtimeMatchReplayStateHash(*viewer,error);
    if(advanceRuntimeMatchReplay(*viewer) || runtimeMatchReplayStateHash(*viewer,error)!=pausedHash)
        return fail("pause advanced authority");
    HWND controls=CreateWindowExW(0,L"STATIC",L"Replay controls smoke",WS_POPUP,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!controls)return fail("hidden playback control window");
    const auto originalX=viewer->camera.worldOffsetX;
    handleRuntimeMatchReplayInput(*viewer,controls,WM_KEYDOWN,VK_RIGHT,0);
    handleRuntimeMatchReplayInput(*viewer,controls,WM_KEYDOWN,VK_ADD,0);
    handleRuntimeMatchReplayInput(*viewer,controls,WM_KEYDOWN,VK_SPACE,0);
    handleRuntimeMatchReplayInput(*viewer,controls,WM_KEYDOWN,VK_SPACE,0);
    handleRuntimeMatchReplayInput(*viewer,controls,WM_RBUTTONDOWN,0,MAKELPARAM(10,10));
    handleRuntimeMatchReplayInput(*viewer,controls,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(20,30));
    handleRuntimeMatchReplayInput(*viewer,controls,WM_RBUTTONUP,0,MAKELPARAM(20,30));
    const bool controlsPassed=viewer->camera.worldOffsetX==originalX-22
        && runtimeMatchReplaySpeed(*viewer)==2.0 && viewer->matchReplay->paused
        && runtimeMatchReplayStateHash(*viewer,error)==pausedHash;
    DestroyWindow(controls);
    if(!controlsPassed)return fail("actual playback input changed authority or failed");
    viewer->selectedObjectIds={actor};
    if(dispatchRuntimeSelectionCommandFromUi(*viewer,RuntimeMultiplayerGameplayCommandType::Stop)
        || captureRuntimeOfflineReplayCommand(*viewer,RuntimeMultiplayerGameplayCommand{},error)
        || dispatchRuntimeSaveLoadFromUi(*viewer,true,error)
        || runtimeMatchReplayStateHash(*viewer,error)!=pausedHash)
        return fail("viewing accepted a game/save command");
    viewer->matchReplay->paused=false;
    for(int steps=0;steps<5000 && !viewer->matchReplay->ended && !viewer->matchReplay->failed;++steps)
        advanceRuntimeMatchReplay(*viewer);
    if(viewer->matchReplay->failed || !viewer->matchReplay->ended)
        return fail("file resimulation: "+viewer->matchReplay->error);
    if(runtimeMatchReplayStateHash(*viewer,error)!=runtimeMatchReplayStateHash(*source,error))
        return fail("final complete world differs");
    std::cout<<"Match replay pipeline: passed; 180 travel commands, production/hold/cancel/place/repair/sell, attack, "
        <<811+productionSteps<<" steps, rewind, cold file reopen, checkpoints, pause and read-only viewer\n";
    if(!runRuntimeMatchReplayMultiplayerSmoke(source->config,error))return false;
    if(!runRuntimeMatchReplayCampaignSmoke(config,error))return false;
    std::cout<<"Match replay foundation: passed; original offline world, complete initial save, 90 fixed ticks including drag pauses, view/audio hash isolation\n";
    return true;
}

bool runRuntimeMatchReplayCampaignSmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail=[&](const std::string& text){error="Match replay campaign: "+text;return false;};
    CampaignDatabaseOptions options;options.resourceRootPath=config.resourceRootPath;
    options.localDataRootPath=config.localDataRootPath;options.modRootPath=config.modRootPath;
    const auto database=loadCampaignDatabase(options);
    const auto* mission=database.findMission("gdi","all01");
    if(!mission || !mission->available)return fail("original YR mission unavailable");
    auto source=std::make_unique<RuntimeWindowState>();source->config=config;
    auto& settings=source->config;
    settings.defaultMapPath=mission->mapPath;settings.gameMode="CampaignMission";
    settings.gameModeRulesOverride.clear();settings.playerSlots.clear();settings.multiplayerActive=false;
    settings.campaignId=mission->campaignId;settings.campaignMissionId=mission->id;
    settings.campaignSourceId=mission->sourceId;settings.campaignDifficulty="Normal";
    settings.campaignPlayerHouse=mission->playerHouse;settings.campaignNextMissionId=mission->nextMissionId;
    settings.playerCountry=mission->playerCountry;settings.startingCredits=mission->startingCredits;
    settings.startingUnits=0;settings.shortGame=false;settings.crates=false;settings.computerAiEnabled=true;
    const auto bootstrap=[&](RuntimeWindowState& state) {
        state.campaignMode=true;state.campaignProgressSaved=true;
        state.runtimeSoundsEnabled=false;state.runtimeSuppressPlayerAudioFeedback=true;
        state.runtimePreferencePersistenceEnabled=false;
        state.report=buildRuntimeStartupReport(state.config,&state.world,&state.theater,&state.art,&state.rules,&state.mapFacts);
        state.theaterLoaded=state.report.theaterLoaded;state.artLoaded=state.report.artLoaded;state.rulesLoaded=state.report.rulesLoaded;
        if(!state.report.success || !initializeRuntimeCooperativeSession(state))return false;
        loadRuntimeSoundDatabase(state);ensureRuntimeLightingInitialized(state);ensureRuntimeParticlesInitialized(state);
        (void)initializeRuntimeCampaignOverlayCrates(state);
        initializeRuntimeNavigation(state);initializeRuntimeEconomy(state);
        if(!initializeRuntimeTriggers(state))return false;
        state.visionEnabled=true;updateRuntimeVisionState(state);completeRuntimeWorldRefresh(state);return true;
    };
    if(!bootstrap(*source))return fail("authored bootstrap");
    const auto path=std::filesystem::path("build/smoke/feature-set-11/10-match-replay")
        /("campaign-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".creplay");
    std::vector<std::pair<std::string,std::uint64_t>> initialTrace;
    runtimeMatchReplayStateHash(*source,error,&initialTrace);
    if(!beginRuntimeMatchRecording(*source,path,error))return fail("begin: "+error);
    for(int n=0;n<180 && !source->skirmishMatchFinished;++n) {
        recordRuntimeMatchReplaySelection(*source);++source->tick;
        advanceRuntimeSimulation(*source,false);recordRuntimeMatchReplayStep(*source,false);
    }
    if(!finishRuntimeMatchRecording(*source,error))return fail("finish: "+error);
    auto viewer=std::make_unique<RuntimeWindowState>();viewer->config=config;
    if(!openRuntimeMatchReplay(path,viewer->config,viewer->matchReplay,error))return fail("open: "+error);
    viewer->matchReplayViewing=true;
    if(!bootstrap(*viewer))return fail("cold authored bootstrap");
    const auto& header=viewer->matchReplay->header;
    std::vector<std::pair<std::string,std::uint64_t>> restoredTrace;std::string restoredData;
    if(!restoreRuntimeMatchReplayWorld(*viewer,header.config,header.initialState,header.initialTick,header.initialHash,error,
        &restoredTrace,&restoredData)) {
        for(std::size_t n=0;n<std::min(initialTrace.size(),restoredTrace.size());++n)
            if(initialTrace[n].second!=restoredTrace[n].second) {
                std::cout<<"Replay campaign restore differs: "<<initialTrace[n].first<<" / "<<restoredTrace[n].first<<'\n';break;
            }
        std::ofstream expected(path.string()+".expected.ini");expected<<header.initialState;
        std::ofstream actual(path.string()+".actual.ini");actual<<restoredData;
        return fail("restore: "+error);
    }
    for(int n=0;n<300 && !viewer->matchReplay->ended && !viewer->matchReplay->failed;++n)advanceRuntimeMatchReplay(*viewer);
    if(viewer->matchReplay->failed || !viewer->matchReplay->ended
        || runtimeMatchReplayStateHash(*viewer,error)!=runtimeMatchReplayStateHash(*source,error))
        return fail("authored triggers replay: "+viewer->matchReplay->error+" "+error);
    const auto progressProbe=path.parent_path()/("viewer-progress-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    if(std::filesystem::exists(progressProbe))return fail("progress isolation fixture already exists");
    viewer->config.localDataRootPath=progressProbe.string();viewer->campaignProgressSaved=false;
    finishRuntimeCampaignMission(*viewer,true,"replay-viewing-progress-check");
    if(std::filesystem::exists(progressProbe))return fail("viewing wrote campaign progress");
    std::cout<<"Match replay campaign: passed; original YR mission, authored triggers, complete cold restore, 180 frames, no progress writes\n";
    return true;
}

bool runRuntimeMatchReplayMultiplayerSmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail=[&](const std::string& text){error="Match replay multiplayer: "+text;return false;};
    const auto bootstrap=[&](RuntimeWindowState& state) {
        state.runtimeSoundsEnabled=false;state.runtimeSuppressPlayerAudioFeedback=true;
        state.runtimePreferencePersistenceEnabled=false;
        state.report=buildRuntimeStartupReport(state.config,&state.world,&state.theater,&state.art,&state.rules,&state.mapFacts);
        state.theaterLoaded=state.report.theaterLoaded;state.artLoaded=state.report.artLoaded;state.rulesLoaded=state.report.rulesLoaded;
        if(!state.report.success || !initializeRuntimeCooperativeSession(state))return false;
        loadRuntimeSoundDatabase(state);ensureRuntimeLightingInitialized(state);ensureRuntimeParticlesInitialized(state);
        initializeRuntimeNavigation(state);initializeRuntimeEconomy(state);initializeRuntimeSkirmishStart(state);
        if(!initializeRuntimeTriggers(state))return false;
        state.visionEnabled=true;updateRuntimeVisionState(state);completeRuntimeWorldRefresh(state);return true;
    };
    auto source=std::make_unique<RuntimeWindowState>();source->config=config;
    auto& settings=source->config;settings.multiplayerActive=true;settings.multiplayerSetupHash=0xc0521235ULL;
    settings.multiplayerCanonicalMapId="replay-original-dryheat";
    settings.multiplayerMapContentFingerprint=runtimeMatchReplayCompatibility(settings,error);
    settings.multiplayerTimelineEpoch=1;settings.simulationRandomSeed=0x450287abdULL;
    settings.playerSlots[0].peerId="replay-alpha";settings.playerSlots[1].human=true;
    settings.playerSlots[1].peerId="replay-beta";
    if(!bootstrap(*source))return fail("original two-house bootstrap");
    const auto path=std::filesystem::path("build/smoke/feature-set-11/10-match-replay")
        /("network-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".creplay");
    if(!beginRuntimeMatchRecording(*source,path,error))return fail("begin: "+error);
    std::vector<RuntimeMultiplayerLockstepApplyCommand> batch;
    for(int slot=0;slot<2;++slot) {
        const auto owner=runtimeSkirmishOwnerForSlot(slot);
        const WorldObjectInstance* unit=nullptr;
        for(const auto& object:source->world.getObjects())if(object.alive && object.kind==WorldObjectKind::Unit
            && object.owner==owner) {unit=&object;break;}
        if(!unit)return fail("human army missing");
        RuntimeMultiplayerLockstepApplyCommand item;item.tick=1;item.frameSequence=1;
        item.peerId=settings.playerSlots[slot].peerId;
        RuntimeMultiplayerGameplayTargetOrderCommand move;
        move.order=RuntimeMultiplayerGameplayTargetOrderKind::Move;move.objectIds={unit->instanceId};
        move.target=RuntimeMultiplayerGameplayCellTarget{{unit->x+3,unit->y}};
        item.command.body=move;batch.push_back(item);
    }
    auto forged=batch.back();forged.peerId=settings.playerSlots[0].peerId;forged.commandIndex=1;
    batch.insert(batch.begin()+1,forged);
    if(!applyRuntimeMultiplayerGameplayBatch(*source,batch,error)
        || source->multiplayerGameplayCommandsApplied!=2 || source->multiplayerGameplayCommandsNoOp!=1)
        return fail("confirmed commands and authority no-op: "+error);
    for(int n=0;n<180;++n) {
        recordRuntimeMatchReplaySelection(*source);++source->tick;
        advanceRuntimeSimulation(*source,false);recordRuntimeMatchReplayStep(*source,false);
    }
    if(!finishRuntimeMatchRecording(*source,error))return fail("finish: "+error);
    auto viewer=std::make_unique<RuntimeWindowState>();viewer->config=config;
    if(!openRuntimeMatchReplay(path,viewer->config,viewer->matchReplay,error))return fail("open: "+error);
    viewer->matchReplayViewing=true;
    if(!bootstrap(*viewer))return fail("cold two-house bootstrap");
    const auto& header=viewer->matchReplay->header;
    if(!restoreRuntimeMatchReplayWorld(*viewer,header.config,header.initialState,header.initialTick,header.initialHash,error))
        return fail("restore: "+error);
    for(int n=0;n<300 && !viewer->matchReplay->ended && !viewer->matchReplay->failed;++n)advanceRuntimeMatchReplay(*viewer);
    if(viewer->matchReplay->failed || !viewer->matchReplay->ended || viewer->multiplayerBridge
        || runtimeMatchReplayStateHash(*viewer,error)!=runtimeMatchReplayStateHash(*source,error))
        return fail("confirmed batch playback: "+viewer->matchReplay->error+" "+error);
    std::cout<<"Match replay multiplayer: passed; two human economies, confirmed owner-bound commands, illegal-owner no-op, 180 frames, cold offline viewing\n";
    return true;
}
