// Included after the real private host, WindowProc, ordinary load and shared hidden-window fixture.
bool runRuntimeReferencePresentationSmokeProbe(RuntimeWindowState& stock, std::ostream& output) {
    struct Clock final : RuntimeUiClock {
        RuntimeUiTimeMs value=1000; mutable std::size_t reads=0;
        RuntimeUiTimeMs nowMs() const noexcept override { ++reads; return value; }
    };
    struct ClockBinding {
        RuntimeWindowState& state; const RuntimeUiClock* previous;
        ClockBinding(RuntimeWindowState& root,const RuntimeUiClock& source)
            :state(root),previous(root.uiClock){state.uiClock=&source;}
        ~ClockBinding(){state.uiClock=previous;}
    };
    struct AuthorityWitness {
        RuntimeWindowState& state; RuntimeMultiplayerRuntimeBridge bridge;
        std::size_t captures=0;
        AuthorityWitness(RuntimeWindowState& root,bool multiplayer):state(root){
            state.config.multiplayerActive=multiplayer;
            state.offlineGameplayIntentRouting=!multiplayer;
            if(multiplayer)state.multiplayerBridge=&bridge;
            state.multiplayerIntentRouter.setCaptureCallback(
                [this](const RuntimeMultiplayerGameplayCommand& command,std::string& error){
                    ++captures;return bridge.captureGameplayCommand(command,error);
                });
        }
        ~AuthorityWitness(){
            state.multiplayerIntentRouter.setCaptureCallback({});state.multiplayerBridge=nullptr;
        }
    };
    std::size_t checks=0,oldCases=0,directCases=0,nativeCases=0,sourceReads=0;
    std::size_t readyCases=0,pointerCases=0,guardCases=0,precedenceCases=0;
    const auto expect=[&](bool ok,const std::string& why){
        ++checks;if(!ok)output<<"Reference-presentation integration failed: "<<why<<'\n';return ok;
    };
    if(!expect(stock.report.success&&stock.world.getSummary().loaded
            &&!stock.world.getTerrainCells().empty()&&stock.rulesLoaded&&stock.artLoaded
            &&stock.theaterLoaded,"actual stock startup/terrain/rules/art/theater"))return false;
    const char* requested=std::getenv("CONCORD_REFERENCE_PRESENTATION_OUTPUT");
    const std::filesystem::path directory=requested&&*requested?std::filesystem::path(requested)
        :std::filesystem::path("reference-presentation-smoke-output");
    std::error_code directoryError;std::filesystem::create_directories(directory,directoryError);
    if(!expect(!directoryError,"isolated output directory"))return false;
    std::ofstream trace(directory/"reference-presentation-trace.txt",std::ios::out|std::ios::trunc);
    if(!expect(static_cast<bool>(trace),"trace file"))return false;
    const auto hexText=[](const std::string& value){
        std::ostringstream text;for(unsigned char byte:value)
            text<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(byte);
        return text.str();
    };
    const auto orderText=[](const RuntimeProductionLaneOrder& order){
        std::ostringstream text;text<<order.serial<<','<<order.itemKey<<','<<order.cost
            <<','<<order.totalTicks<<','<<order.remainingTicks<<','<<order.creditsSpent
            <<','<<order.creditRemainderMicros<<','<<order.zeroCostProgressMicros
            <<','<<order.rateDivisionRemainder<<','<<order.rateDivisionDenominator
            <<','<<order.requiresPlacement;return text.str();
    };
    const auto authority=[&](const RuntimeWindowState& state,const AuthorityWitness& witness){
        std::ostringstream text;text<<"tick="<<state.tick
            <<";world="<<state.world.deterministicSimulationStateHash()
            <<";queue="<<hexText(state.playerCommands.serialize())<<";queueHash="<<state.playerCommands.stateHash()
            <<";credits="<<state.economy.credits
            <<";lanes="<<hexText(runtimeSerializeProductionLanes(state.economy.localProductionLanes))
            <<";lanesHash="<<runtimeProductionLanesStateHash(state.economy.localProductionLanes)
            <<";captures="<<witness.captures<<";pending="<<witness.bridge.pendingGameplayCommandCount()
            <<";sessionTick="<<witness.bridge.orderedTransportPumpSessionView().authoritativeTick()
            <<";barrier="<<witness.bridge.lockstep().currentBarrierTick()
            <<";inputDelay="<<witness.bridge.lockstep().config().inputDelayTicks;
        return text.str();
    };
    const auto localState=[&](const RuntimeWindowState& state,bool includeStatus){
        std::ostringstream text;const auto& local=state.playerLocalCommands;
        text<<"tab="<<static_cast<int>(state.activeBuildTab)<<";pages=";
        for(int page:state.buildPageByTab)text<<page<<',';
        text<<";wheel=";for(int wheel:state.buildWheelRemainderByTab)text<<wheel<<',';
        text<<";tool="<<static_cast<int>(state.activeSidebarTool)<<";selected=";
        for(int id:state.selectedObjectIds)text<<id<<',';
        for(std::size_t index=0;index<local.teams.size();++index){
            text<<";team="<<index<<':';for(int id:local.teams[index])text<<id<<',';
        }
        for(std::size_t index=0;index<local.views.size();++index){
            text<<";view="<<index<<':';if(local.views[index])text<<local.views[index]->x<<','<<local.views[index]->y;
            else text<<"none";
        }
        text<<";local="<<local.planning<<','<<local.selectedWaypoint<<','<<local.followTargeting
            <<','<<local.rallyTargeting<<','<<local.cameraFollow<<','<<local.showCoordinates
            <<','<<local.lastSelectionKey<<','<<local.lastSelectionAtMs<<','<<local.veterancyRank<<','<<local.healthRank
            <<";beaconTarget="<<state.playerCommunication.beaconTargeting<<";beacon=";
        if(state.playerCommunication.selectedBeacon)
            text<<state.playerCommunication.selectedBeacon->x<<','<<state.playerCommunication.selectedBeacon->y;
        else text<<"none";
        text<<";drag="<<state.leftPointer.moved<<','<<state.leftPointer.active
            <<','<<state.rightPointer.moved<<','<<state.rightPointer.active
            <<";mouse="<<state.mouseClientPointValid<<','<<state.mouseClientPoint.x<<','<<state.mouseClientPoint.y
            <<";camera="<<state.camera.worldOffsetX<<','<<state.camera.worldOffsetY
            <<','<<state.camera.tileWidth<<','<<state.camera.tileHeight<<','<<state.camera.initialized
            <<";clamp="<<state.cameraClampViewport.left<<','<<state.cameraClampViewport.top
            <<','<<state.cameraClampViewport.right<<','<<state.cameraClampViewport.bottom
            <<";ledgerSelection=";
        if(state.economy.selectedPlacementLane)text<<static_cast<int>(*state.economy.selectedPlacementLane);
        else text<<"none";
        text<<','<<state.economy.selectedPlacementSerial<<";ledgerPreview="
            <<state.economy.placementCell.x<<','<<state.economy.placementCell.y<<','<<state.economy.placementValid;
        const auto placement=runtimeSelectedPlacement(state);text<<";selectedPlacement=";
        if(placement)text<<static_cast<int>(placement->lane)<<':'<<orderText(placement->order)
            <<':'<<(placement->item?placement->item->typeId:"null");
        else text<<"none";
        text<<";intent=";if(const auto& intent=state.multiplayerIntentRouter.placement())
            text<<static_cast<int>(intent->lane)<<','<<intent->productionSerial<<','<<intent->buildingTypeId
                <<','<<intent->cell.x<<','<<intent->cell.y<<','<<intent->previewValid;
        else text<<"none";
        const auto& permission=state.multiplayerIntentRouter.authorityState();
        text<<";intentAuthority="<<permission.spectator<<','<<permission.terminal<<','<<permission.localDefeated
            <<";cache="<<state.placementChecksCacheTick<<','<<state.placementChecksCacheCell.x
            <<','<<state.placementChecksCacheCell.y<<','<<static_cast<int>(state.placementChecksCacheKind)
            <<','<<state.placementChecksCacheTypeId<<','<<state.placementChecksCacheOwner
            <<','<<state.placementChecksCacheInRange<<','<<state.placementChecksCacheVision.layoutEpoch
            <<','<<state.placementChecksCacheVision.revision;
        for(const auto& check:state.placementChecksCache)
            text<<";check="<<check.cell.x<<','<<check.cell.y<<','<<check.level<<','<<check.valid<<','<<check.reason;
        text<<";sound="<<state.runtimeSoundEvents<<','<<state.lastRuntimeSound;
        for(const auto& intent:state.runtimeSoundIntentHistory)text<<";soundIntent="<<intent;
        for(const auto& record:state.runtimeSoundPlaybackHistory)
            text<<";soundRecord="<<record.tick<<','<<record.playbackOrder<<','<<record.intent
                <<','<<record.section<<','<<record.sample<<','<<record.path<<','<<record.status
                <<','<<record.semantic<<','<<record.eva<<','<<record.playable<<','<<record.played
                <<','<<record.suppressed<<','<<record.queued<<','<<record.queueRequested;
        if(includeStatus)text<<";repaint="<<state.repaintRequested<<";status="<<state.interactionStatus;
        return text.str();
    };
    constexpr RECT client{0,0,1024,768};
    const RECT viewport=runtimeMapViewport(client);
    const auto make=[&](int productionMode,bool threePlayers=false){
        auto storage=std::make_unique<RuntimeWindowState>();auto& state=*storage;
        RuntimeSnapshotActiveStateScope active(state);
        state.config=stock.config;state.config.gameMode="Battle";state.config.gameModeRulesOverride.clear();
        state.config.computerAiEnabled=false;state.config.multiplayerActive=false;
        state.config.noCooldown=false;state.config.startingCredits=100000;state.config.startingUnits=0;
        state.config.playerCountry="Americans";state.config.keyboardReferenceLayout=true;state.config.keyBindings.clear();
        state.config.showMainMenu=state.config.showSkirmishMenu=false;
        state.config.configPath=pathString(directory/"private-preferences.ini");
        state.config.playerSlots.assign(threePlayers?3:2,{});
        for(std::size_t index=0;index<state.config.playerSlots.size();++index){
            auto& slot=state.config.playerSlots[index];slot.enabled=slot.human=true;slot.local=index==0;
            slot.country=index%2?"Russians":"Americans";slot.team="-";slot.startPosition=static_cast<int>(index)+1;
            slot.name="Presentation player "+std::to_string(index+1);
            const auto owner=runtimeCanonicalHumanHouseOwner(runtimeSkirmishOwnerForSlot(index));
            state.skirmishCountryByOwner[owner]=slot.country;
            state.skirmishTeamByOwner[owner]=runtimeNormalizeSkirmishTeam(slot.team);
        }
        state.report=stock.report;state.world=stock.world;state.world.accessObjects().clear();
        state.rules=stock.rules;state.art=stock.art;state.theater=stock.theater;state.mapFacts=stock.mapFacts;
        state.rulesLoaded=state.artLoaded=state.theaterLoaded=true;state.mapFactsLoaded=stock.mapFactsLoaded;
        state.terrainInfoByCell=stock.terrainInfoByCell;state.terrainByCell=buildRuntimeTerrainLookup(state.world);
        state.runtimeSoundsEnabled=state.runtimeMusicEnabled=false;state.runtimeSuppressPlayerAudioFeedback=true;
        state.runtimePreferencePersistenceEnabled=false;state.runtimeVoiceVolume=state.runtimeSoundVolume=0;
        state.visionEnabled=false;initializeRuntimeNavigation(state);initializeRuntimeEconomy(state);
        if(!expect(initializeRuntimeSkirmishStart(state),"real skirmish production start"))return std::unique_ptr<RuntimeWindowState>{};
        if(!expect(deployRuntimeMcv(state,state.economy.skirmishStartObjectId,false),"real MCV base deployment"))
            return std::unique_ptr<RuntimeWindowState>{};
        state.triggers.bootstrapComplete=true;
        const auto* powerPointer=findRuntimeGameplayCatalogItem(state,state.economy.owner,"GAPOWR");
        const auto* wallPointer=findRuntimeGameplayCatalogItem(state,state.economy.owner,"GAWALL");
        if(!expect(powerPointer&&wallPointer,"actual GAPOWR/GAWALL catalog"))return std::unique_ptr<RuntimeWindowState>{};
        const RuntimeBuildCatalogItem power=*powerPointer,wall=*wallPointer;
        const auto finish=[&](const RuntimeBuildCatalogItem& item){
            if(!queueRuntimeBuildOrder(state,item))return false;
            ++state.tick;advanceRuntimeProductionEconomy(state);
            if(!runtimeReadyProductionForItem(state,item)){
                if(!completeRuntimeActiveProductionForProbe(state,item))return false;
                ++state.tick;advanceRuntimeProductionEconomy(state);
            }
            return runtimeReadyProductionForItem(state,item)!=nullptr;
        };
        if(productionMode==1){
            if(!expect(finish(power)&&stageRuntimeReadyPlacementForProbe(state,wall),
                    "real Structures production and stock Armory ready fixture"))
                return std::unique_ptr<RuntimeWindowState>{};
        }else if(productionMode==2){
            if(!expect(queueRuntimeBuildOrder(state,power),"real unfinished production queued"))
                return std::unique_ptr<RuntimeWindowState>{};
            ++state.tick;advanceRuntimeProductionEconomy(state);
            if(!expect(!runtimeReadyProductionForItem(state,power),"real unfinished production not ready"))
                return std::unique_ptr<RuntimeWindowState>{};
        }
        clearRuntimePlacementSelection(state);state.multiplayerIntentRouter.cancelPlacement();
        initializeRuntimeOfflineGameplayRouting(state);
        state.selectedObjectIds.clear();
        for(const auto& object:state.world.getObjects())
            if(object.alive&&runtimeCanonicalHumanHouseOwner(object.owner)==runtimeCanonicalHumanHouseOwner(state.economy.owner)){
                state.selectedObjectIds={object.instanceId};break;
            }
        if(!expect(!state.selectedObjectIds.empty(),"actual current owned base selection"))
            return std::unique_ptr<RuntimeWindowState>{};
        state.activeBuildTab=RuntimeBuildKind::Unit;state.buildPageByTab.fill(999);
        state.buildWheelRemainderByTab={2,-3,4,-5};state.activeSidebarTool=RuntimeSidebarTool::Repair;
        state.playerLocalCommands.followTargeting=state.playerLocalCommands.rallyTargeting=true;
        state.playerLocalCommands.cameraFollow=false;state.playerLocalCommands.lastSelectionKey='?';
        state.playerLocalCommands.lastSelectionAtMs=777;state.playerLocalCommands.veterancyRank=1;
        state.playerLocalCommands.healthRank=2;state.playerCommunication.beaconTargeting=true;
        state.playerCommunication.selectedBeacon=RuntimeMultiplayerGameplayCell{4,6};
        state.leftPointer.active=state.leftPointer.moved=true;
        state.mouseClientPoint={viewport.left+100,viewport.top+100};state.mouseClientPointValid=true;
        runtimePlayerCenterBase(state,client);state.interactionStatus="presentation initial";
        state.runtimeSoundEvents=0;state.runtimeSoundIntentHistory.clear();
        state.runtimeSoundPlaybackHistory.clear();state.lastRuntimeSound.clear();state.repaintRequested=false;
        return storage;
    };
    const auto setBinding=[](RuntimeWindowState& state,const std::string& action,int code){
        for(auto& binding:state.runtimeMenuKeyboardBindings)if(binding.first==action){binding.second=code;return;}
        state.runtimeMenuKeyboardBindings.emplace_back(action,code);
    };
    const char* actions[]={"StructureTab","DefenseTab","InfantryTab","UnitTab",
        "ToggleRepair","ToggleSell","CenterBase","ShowCoordinates","Follow"};
    const auto configure=[&](RuntimeWindowState& state,int action,WPARAM raw=VK_F13){
        state.runtimeMenuKeyboardLoadAttempted=true;state.runtimeMenuKeyboardBindings.clear();
        setBinding(state,"Options",VK_ESCAPE);setBinding(state,actions[action],runtimePlayerCurrentKeyCode(raw));
    };
    // Frozen original Reference front; it calls only existing native owners.
    const auto oldReference=[](RuntimeWindowState& state,const RECT& client,int code,bool repeated){
    auto& local = state.playerLocalCommands; auto& ui = state.playerCommunication;
    const auto matches = [&](const std::string& id, int fallback) {
        return code == runtimeMenuKeyboardBindingCode(state, id, fallback);
    };
    const std::pair<const char*, RuntimeBuildKind> tabs[] = {
        {"StructureTab", RuntimeBuildKind::Building}, {"DefenseTab", RuntimeBuildKind::Defense},
        {"InfantryTab", RuntimeBuildKind::Infantry}, {"UnitTab", RuntimeBuildKind::Unit}};
    constexpr int tabKeys[] = {'Q','W','E','R'};
    for (int i = 0; i < 4; ++i) if (matches(tabs[i].first, tabKeys[i])) {
        state.activeBuildTab = tabs[i].second;
        runtimeClampActiveBuildPage(state, runtimeSidebarRect(client));
        if (!repeated && runtimeBuildKindUsesSingleStructureSlot(state.activeBuildTab)) {
            for (const auto& item : state.economy.catalog) {
                if (item.kind != state.activeBuildTab) continue;
                RuntimeProductionLane lane = RuntimeProductionLane::Structures;
                const auto* ready = runtimeReadyProductionForItem(state, item, &lane);
                if (ready == nullptr) continue;
                if (beginRuntimePlacementFromUi(state, {lane, *ready, &item}, true)) {
                    state.activeSidebarTool = RuntimeSidebarTool::None;
                    local.followTargeting = local.rallyTargeting = ui.beaconTargeting = false;
                    state.leftPointer.active = state.leftPointer.moved = false;
                    state.interactionStatus = "placing " + item.typeId;
                    // A keyboard selection must follow a stationary mouse too.
                    const RECT viewport = runtimeMapViewport(client);
                    if (state.mouseClientPointValid
                        && pointInRectInclusive(viewport, state.mouseClientPoint)) {
                        updateRuntimePlacementPreview(state, viewport, state.mouseClientPoint);
                    }
                    emitRuntimeSound(state, runtimeSoundIntentSidebarButtonClick);
                    state.repaintRequested = true;
                }
                break;
            }
        }
        return true;
    }
    if (matches("ToggleRepair", 'K') || matches("ToggleSell", 'L')) {
        if (!repeated) {
            const auto tool = matches("ToggleRepair", 'K') ? RuntimeSidebarTool::Repair : RuntimeSidebarTool::Sell;
            state.activeSidebarTool = state.activeSidebarTool == tool ? RuntimeSidebarTool::None : tool;
            local.followTargeting = local.rallyTargeting = ui.beaconTargeting = false;
        }
        return true;
    }
    if (matches("CenterBase", 'H')) { runtimePlayerCenterBase(state, client); return true; }
    if (matches("ShowCoordinates", 'O')) {
        if (!repeated) local.showCoordinates = !local.showCoordinates;
        return true;
    }
    if (matches("Follow", 'F')) {
        if (!repeated) local.cameraFollow = !local.cameraFollow && !state.selectedObjectIds.empty();
        if (local.cameraFollow) runtimePlayerCenterSelection(state, client);
        return true;
    }
        return false;
    };
    const auto triple=[&](const std::string& label,int action,bool repeated,UINT message,
        bool multiplayer,int mode=0,int productionMode=1){
        auto os=make(productionMode),ds=make(productionMode),ns=make(productionMode);
        if(!expect(os&&ds&&ns,label+" independent actual worlds"))return false;
        auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native}){
            configure(*state,action);
            if(mode==1)state->mouseClientPointValid=false;
            if(mode==2)state->mouseClientPoint={viewport.right+1,viewport.bottom+1};
            if(mode==3)state->mouseClientPoint={viewport.right,viewport.bottom};
            if(mode==5)state->playerLocalCommands.cameraFollow=true;
            if(mode==6)state->selectedObjectIds.clear();
            if(mode==7){
                for(auto& object:state->world.accessObjects())
                    if(std::find(state->selectedObjectIds.begin(),state->selectedObjectIds.end(),object.instanceId)
                        !=state->selectedObjectIds.end())object.alive=false;
            }
            if(mode==8)state->playerLocalCommands.showCoordinates=true;
            if(mode==9)state->activeSidebarTool=RuntimeSidebarTool::Sell;
            if(mode==10||mode==11){
                for(auto& object:state->world.accessObjects())
                    if(object.kind==WorldObjectKind::Structure
                        &&runtimeCanonicalHumanHouseOwner(object.owner)
                            ==runtimeCanonicalHumanHouseOwner(state->economy.owner))object.alive=false;
                if(mode==11)state->economy.skirmishStartCell={-1,-1};
            }
        }
        Clock oc,dc,nc;ClockBinding ob(old,oc),db(direct,dc),nb(native,nc);
        AuthorityWitness ow(old,multiplayer),dw(direct,multiplayer),nw(native,multiplayer);
        if(mode==4){
            for(auto* state:{&old,&direct,&native}){
                RuntimeSnapshotActiveStateScope active(*state);
                const auto placement=runtimeOldestReadyPlacement(*state);
                if(!expect(placement&&beginRuntimePlacementFromUi(*state,*placement,true),
                        label+" preexisting real local placement"))return false;
                state->repaintRequested=false;state->interactionStatus="existing placement";
            }
        }
        RuntimePlayerDefeatNoticeSmokeWindow window(native,1024,768);
        if(!expect(window.window!=nullptr,label+" actual hidden HWND"))return false;
        const int code=runtimePlayerCurrentKeyCode(VK_F13);
        const auto beforeOld=authority(old,ow),beforeDirect=authority(direct,dw),beforeNative=authority(native,nw);
        const int initialSounds=old.runtimeSoundEvents;
        const bool initialFollow=old.playerLocalCommands.cameraFollow;
        const bool initialCoordinates=old.playerLocalCommands.showCoordinates;
        std::optional<RuntimeSelectedPlacement> expectedReady;
        if(action<2&&!repeated){
            for(const auto& item:old.economy.catalog){
                const auto kind=action==0?RuntimeBuildKind::Building:RuntimeBuildKind::Defense;
                if(item.kind!=kind)continue;
                RuntimeProductionLane lane=RuntimeProductionLane::Structures;
                if(const auto* ready=runtimeReadyProductionForItem(old,item,&lane)){
                    expectedReady=RuntimeSelectedPlacement{lane,*ready,&item};break;
                }
            }
        }
        bool oldHandled=false,directHandled=false;
        {RuntimeSnapshotActiveStateScope active(old);(void)runtimeUiClockNow(old);
            oldHandled=oldReference(old,client,code,repeated);}
        {RuntimeSnapshotActiveStateScope active(direct);(void)runtimeUiClockNow(direct);
            RuntimeReferencePresentationUiHost host(direct,client);
            directHandled=handleRuntimeReferencePresentationKey({code,repeated},direct.playerLocalCommands,host);}
        const bool controllerRepaint=old.repaintRequested;
        if(!expect(oldHandled&&directHandled&&localState(old,true)==localState(direct,true),
                label+" independent old/front samecpp full controller state"))return false;
        // This is the unchanged WindowProc handled epilogue, outside the controller.
        old.repaintRequested=direct.repaintRequested=true;
        LRESULT result=0;
        {RuntimeSnapshotActiveStateScope active(native);
            result=SendMessageW(window.window,message,VK_F13,repeated?(static_cast<LPARAM>(1)<<30):0);}
        const bool sameLocal=localState(old,true)==localState(native,true);
        if(!sameLocal)output<<"Reference-presentation state mismatch label="<<label
            <<" old="<<hexText(localState(old,true))<<" native="<<hexText(localState(native,true))<<'\n';
        if(!expect(result==0&&sameLocal&&runtimePlayerCurrentKeyCode(VK_F13)==code,
                label+" full native route local state incl original outer repaint"))return false;
        if(!expect(beforeOld==beforeDirect&&beforeOld==beforeNative
                &&authority(old,ow)==beforeOld&&authority(direct,dw)==beforeDirect
                &&authority(native,nw)==beforeNative&&ow.captures==0&&dw.captures==0&&nw.captures==0
                &&ow.bridge.pendingGameplayCommandCount()==0&&dw.bridge.pendingGameplayCommandCount()==0
                &&nw.bridge.pendingGameplayCommandCount()==0,
                label+" world queues credits lanes tick/currentTick/inputDelay/capture unchanged"))return false;
        if(!expect(oc.reads==1&&dc.reads==1&&nc.reads==1,label+" original argument source sampled once"))return false;
        if(expectedReady){
            const auto selected=runtimeSelectedPlacement(old);
            if(!expect(selected&&selected->lane==expectedReady->lane
                    &&orderText(selected->order)==orderText(expectedReady->order)
                    &&selected->item&&selected->item->typeId==expectedReady->item->typeId
                    &&old.activeSidebarTool==RuntimeSidebarTool::None
                    &&!old.playerCommunication.beaconTargeting&&!old.playerLocalCommands.rallyTargeting
                    &&!old.playerLocalCommands.followTargeting&&!old.leftPointer.moved&&!old.leftPointer.active
                    &&old.runtimeSoundEvents==initialSounds+1&&controllerRepaint,
                    label+" actual complete owned ready/lane/order and successful effects"))return false;
            ++readyCases;
        }else if(action<4){
            if(!expect(!runtimeSelectedPlacement(old)&&old.runtimeSoundEvents==initialSounds
                    &&old.playerCommunication.beaconTargeting&&old.playerLocalCommands.rallyTargeting
                    &&old.playerLocalCommands.followTargeting&&old.leftPointer.moved&&old.leftPointer.active
                    &&!controllerRepaint,label+" repeat/nonstructure/empty/unfinished only tab and clamp"))return false;
        }else if(action==4||action==5){
            if(!expect(old.leftPointer.moved&&old.leftPointer.active&&old.runtimeSoundEvents==initialSounds
                    &&!controllerRepaint,label+" tools preserve drag/placement/no new sound or controller repaint"))return false;
            if(!repeated&&!expect(!old.playerCommunication.beaconTargeting
                    &&!old.playerLocalCommands.rallyTargeting&&!old.playerLocalCommands.followTargeting,
                    label+" tool original target clear"))return false;
        }else if(action==6){
            if(!expect(!old.playerLocalCommands.cameraFollow,label+" H leaves follow on every repeat"))return false;
        }else if(action==7){
            if(!expect(old.playerLocalCommands.showCoordinates==(repeated?initialCoordinates:!initialCoordinates),
                    label+" O toggles only nonrepeat"))return false;
        }else if(action==8&&repeated){
            if(!expect(old.playerLocalCommands.cameraFollow==initialFollow,label+" repeated F current follow"))return false;
        }
        const auto print=[&](const char* route,const RuntimeWindowState& state,const AuthorityWitness& witness){
            trace<<"presentation path="<<route<<" label="<<label<<" action="<<action
                <<" repeat="<<repeated<<" source="<<message<<" multiplayer="<<multiplayer
                <<" raw="<<VK_F13<<" code="<<code<<" controllerRepaint="<<controllerRepaint
                <<" state="<<hexText(localState(state,true))<<" authority="<<hexText(authority(state,witness))
                <<" captures="<<witness.captures<<" pending="<<witness.bridge.pendingGameplayCommandCount()<<'\n';
        };
        print("old",old,ow);print("direct",direct,dw);print("native",native,nw);
        sourceReads+=oc.reads+dc.reads+nc.reads;++oldCases;++directCases;++nativeCases;
        return true;
    };
    // Four tabs cover both sources, repeat policies and actual offline/MP local preview.
    for(int action=0;action<4;++action)for(bool repeated:{false,true})
        for(UINT message:{WM_KEYDOWN,WM_SYSKEYDOWN})for(bool multiplayer:{false,true})
            if(!triple("tab-matrix",action,repeated,message,multiplayer))return false;
    for(int action=4;action<9;++action)for(bool repeated:{false,true})for(bool multiplayer:{false,true}){
        const UINT message=(action+static_cast<int>(repeated)+static_cast<int>(multiplayer))%2
            ?WM_SYSKEYDOWN:WM_KEYDOWN;
        if(!triple("tool-camera-matrix",action,repeated,message,multiplayer))return false;
    }
    for(int action:{0,1})for(int mode:{1,2,3}){
        if(!triple("stationary-pointer",action,false,WM_KEYDOWN,true,mode))return false;
        ++pointerCases;
    }
    for(int mode:{0,2})if(!triple(mode==0?"empty-production":"unfinished-production",
            0,false,WM_KEYDOWN,false,0,mode))return false;
    if(!triple("tool-keeps-existing-placement",4,false,WM_SYSKEYDOWN,true,4)
            ||!triple("repair-toggle-off",4,false,WM_KEYDOWN,false)
            ||!triple("sell-toggle-off",5,false,WM_KEYDOWN,true,9)
            ||!triple("follow-repeat-current",8,true,WM_SYSKEYDOWN,true,5)
            ||!triple("follow-toggle-off",8,false,WM_KEYDOWN,false,5)
            ||!triple("follow-empty",8,false,WM_KEYDOWN,true,6)
            ||!triple("follow-current-selection-invalidated",8,false,WM_SYSKEYDOWN,false,7)
            ||!triple("coordinates-toggle-back",7,false,WM_KEYDOWN,false,8)
            ||!triple("base-no-structure-fallback",6,true,WM_SYSKEYDOWN,false,10)
            ||!triple("base-no-valid-fallback",6,false,WM_KEYDOWN,true,11))return false;
    for(int mode=0;mode<6;++mode){
        auto storage=make(1);if(!expect(static_cast<bool>(storage),"guard actual world"))return false;
        auto& state=*storage;configure(state,7);Clock clock;ClockBinding binding(state,clock);
        AuthorityWitness witness(state,false);
        if(mode==0)state.runtimeMenuOpen=true;
        if(mode==1)state.playerCommunication.chatOpen=true;
        if(mode==2)state.playerCommunication.diplomacyOpen=true;
        if(mode==3)state.skirmishMatchFinished=true;
        if(mode==4){state.campaignMode=true;state.triggers.campaignInputLocked=true;}
        if(mode==5){state.runtimeMenuOpen=true;state.runtimeMenuPage=RuntimeMenuPage::Keyboard;state.runtimeMenuKeyCaptureIndex=0;}
        RuntimePlayerDefeatNoticeSmokeWindow window(state,1024,768);
        if(!expect(window.window!=nullptr,"guard hidden HWND"))return false;
        const auto before=authority(state,witness),beforeLocal=localState(state,false);
        const bool beforeRepaint=state.repaintRequested;
        {RuntimeSnapshotActiveStateScope active(state);SendMessageW(window.window,WM_KEYDOWN,VK_F13,0);}
        if(!expect(localState(state,false)==beforeLocal&&authority(state,witness)==before
                &&clock.reads==0&&witness.captures==0&&witness.bridge.pendingGameplayCommandCount()==0,
                "original outer guard blocks reference before source "+std::to_string(mode)))return false;
        trace<<"guard mode="<<mode<<" local="<<hexText(localState(state,true))
            <<" beforeRepaint="<<beforeRepaint<<" afterRepaint="<<state.repaintRequested
            <<" authority="<<hexText(authority(state,witness))<<" sourceReads=0 captures=0 pending=0\n";
        ++guardCases;
    }
    for(int mode=0;mode<8;++mode){
        auto storage=make(1);if(!expect(static_cast<bool>(storage),"priority actual world"))return false;
        auto& state=*storage;configure(state,mode==2?0:7);Clock clock;ClockBinding binding(state,clock);
        AuthorityWitness witness(state,mode%2!=0);
        const WPARAM raw=mode==1?VK_NUMPAD1:VK_F13;
        const int code=runtimePlayerCurrentKeyCode(raw),normalized=(code&~0xff)|'1';
        configure(state,mode==2?0:7,raw);
        const auto selection=state.selectedObjectIds;state.playerLocalCommands.teams[0]=selection;
        if(mode==0||mode==7){
            setBinding(state,"TeamSelect_1",code);setBinding(state,"View1",code);
            setBinding(state,"TypeSelect",code);setBinding(state,"Diplomacy",code);
        }
        if(mode==1)setBinding(state,"TeamSelect_1",normalized);
        if(mode==2)setBinding(state,"StopObject",code);
        if(mode==3){setBinding(state,"ShowCoordinates",'O');setBinding(state,"CenterBase",code);setBinding(state,"Follow",code);}
        if(mode==4){setBinding(state,"ShowCoordinates",'O');setBinding(state,"TeamSelect_1",code);}
        if(mode==5){
            state.config.playerCountry="Observer";state.multiplayerLocalSpectator=true;
            if(!expect(state.config.multiplayerActive&&state.multiplayerLocalSpectator
                    &&!runtimePlayerSelectionAllowed(state),"mode5 actual MP spectator selection gate"))return false;
        }
        if(mode==6){state.campaignMode=true;state.triggers.campaignInputLocked=true;}
        RuntimePlayerDefeatNoticeSmokeWindow window(state,1024,768);
        if(!expect(window.window!=nullptr,"priority hidden HWND"))return false;
        const auto before=authority(state,witness);
        const UINT message=mode==6?WM_SYSKEYDOWN:WM_KEYDOWN;
        {RuntimeSnapshotActiveStateScope active(state);SendMessageW(window.window,message,raw,0);}
        if(!expect(runtimePlayerCurrentKeyCode(raw)==code&&authority(state,witness)==before
                &&clock.reads==1&&witness.captures==0&&witness.bridge.pendingGameplayCommandCount()==0,
                "priority/campaign SYSKEY original route zero authority "+std::to_string(mode)))return false;
        if(mode==0||mode==1||mode==6||mode==7){
            if(!expect(state.playerLocalCommands.showCoordinates&&state.selectedObjectIds==selection
                    &&state.playerLocalCommands.lastSelectionKey=='?'&&!state.playerCommunication.diplomacyOpen,
                    "reference wins lower binding and raw NumPad before normalization"))return false;
        }else if(mode==2){
            if(!expect(runtimeSelectedPlacement(state).has_value()
                    &&state.playerLocalCommands.lastSelectionKey=='?',"ready tab consumes before authoritative Stop"))return false;
        }else if(mode==3){
            if(!expect(!state.playerLocalCommands.cameraFollow&&!state.playerLocalCommands.showCoordinates,
                    "CenterBase before Follow collision"))return false;
        }else if(mode==4){
            if(!expect(!state.playerLocalCommands.showCoordinates&&state.selectedObjectIds==selection
                    &&state.playerLocalCommands.lastSelectionKey=='1'
                    &&state.playerLocalCommands.lastSelectionAtMs==clock.value,"unhandled front reaches existing Team"))return false;
        }else if(!expect(state.playerLocalCommands.showCoordinates&&state.selectedObjectIds.empty()
                &&!state.leftPointer.moved&&!state.leftPointer.active,
                "observer clears selection then allows original local reference"))return false;
        trace<<"priority mode="<<mode<<" raw="<<raw<<" code="<<code<<" source="<<message
            <<" state="<<hexText(localState(state,true))<<" authority="<<hexText(authority(state,witness))
            <<" captures=0 pending=0\n";sourceReads+=clock.reads;++precedenceCases;
    }
    {
        auto storage=make(1,true);if(!expect(static_cast<bool>(storage),"real active load world"))return false;
        auto& state=*storage;RuntimeSnapshotActiveStateScope active(state);
        state.config.shortGame=true;state.tick=21;
        const int savedActorId=state.selectedObjectIds.front();
        std::uint64_t savedSerial=0;std::string savedOrder;
        {
            const auto* beforePower=findRuntimeGameplayCatalogItem(state,state.economy.owner,"GAPOWR");
            if(!expect(beforePower!=nullptr,"real pre-load power catalog"))return false;
            const auto* beforeReady=runtimeReadyProductionForItem(state,*beforePower);
            if(!expect(beforeReady!=nullptr,"real pre-load completed Structures order"))return false;
            savedSerial=beforeReady->serial;savedOrder=orderText(*beforeReady);
        }
        (void)advanceRuntimeParticles(state);
        if(!expect(state.particlesInitialized&&state.particles.currentTick()==21
                &&state.particles.fixedTickHz()==std::max(1,state.config.fixedTickHz),
                "real save active root/particle authority"))return false;
        Clock clock;ClockBinding binding(state,clock);std::string error;
        const auto savePath=pathString(directory/"reference-presentation-load.ini");
        if(!expect(saveRuntimeState(state,savePath,error),"real isolated save: "+error))return false;
        if(!expect(loadRuntimeState(state,savePath,error)&&state.tick==21&&state.uiClock==&clock,
                "ordinary load scratch swap and borrowed source: "+error))return false;
        // No pre-load object/catalog/ready pointer is used from this point.
        configure(state,0);setBinding(state,"Follow",runtimePlayerCurrentKeyCode(VK_F14));
        const auto* currentPower=findRuntimeGameplayCatalogItem(state,state.economy.owner,"GAPOWR");
        const auto* currentReady=currentPower?runtimeReadyProductionForItem(state,*currentPower):nullptr;
        if(!expect(currentReady&&currentReady->serial==savedSerial&&orderText(*currentReady)==savedOrder,
                "fresh current-root catalog and complete ready order after ordinary load"))return false;
        applyRuntimePlayerSelection(state,{savedActorId},false,false);
        if(!expect(runtimePlayerLocalSelectable(state,savedActorId)
                &&state.selectedObjectIds==std::vector<int>{savedActorId},"exact saved current-root actor eligibility"))
            return false;
        state.mouseClientPoint={viewport.left+100,viewport.top+100};state.mouseClientPointValid=true;
        state.playerLocalCommands.cameraFollow=false;
        AuthorityWitness witness(state,false);RuntimePlayerDefeatNoticeSmokeWindow window(state,1024,768);
        if(!expect(window.window!=nullptr,"post-load fresh host/native window"))return false;
        const auto before=authority(state,witness);const auto initialReads=clock.reads;
        {RuntimeSnapshotActiveStateScope current(state);SendMessageW(window.window,WM_KEYDOWN,VK_F13,0);}
        const auto selected=runtimeSelectedPlacement(state);
        if(!expect(selected&&selected->order.serial==savedSerial&&orderText(selected->order)==savedOrder,
                "post-load actual Q reborrows ready order from swapped root"))return false;
        clock.value=1500;
        {RuntimeSnapshotActiveStateScope current(state);SendMessageW(window.window,WM_SYSKEYDOWN,VK_F14,0);}
        if(!expect(state.playerLocalCommands.cameraFollow&&state.tick==21&&state.uiClock==&clock
                &&clock.reads==initialReads+2&&authority(state,witness)==before
                &&witness.captures==0&&witness.bridge.pendingGameplayCommandCount()==0,
                "post-load next reference keys/current root/typed source/zero authority"))return false;
        trace<<"load savedActorId="<<savedActorId<<" savedSerial="<<savedSerial
            <<" tick=21 sourceSame=1 currentRootReborrow=1 exactOrder=1 qAfterLoad=1 followAfterLoad=1"
            <<" state="<<hexText(localState(state,true))<<" authority="<<hexText(authority(state,witness))
            <<" captures=0 pending=0\n";sourceReads+=clock.reads;
    }
    trace<<"checks="<<checks<<" oldCases="<<oldCases<<" directCases="<<directCases
        <<" nativeCases="<<nativeCases<<" readyCases="<<readyCases<<" pointerCases="<<pointerCases
        <<" guards="<<guardCases<<" precedenceCases="<<precedenceCases<<'\n';
    trace.flush();if(!expect(static_cast<bool>(trace),"trace flush"))return false;
    output<<"Reference-presentation integration passed: checks="<<checks
        <<" oldCases="<<oldCases<<" directCases="<<directCases<<" nativeCases="<<nativeCases
        <<" sourceReads="<<sourceReads<<" actions=9 tabs=4 sources=2 repeats=2 readyCases="<<readyCases
        <<" pointerCases="<<pointerCases<<" guards="<<guardCases<<" precedenceCases="<<precedenceCases
        <<" fullLocalState=1 completeReadyOrder=1 realStructuresAndArmory=1 stationaryPreview=1"
        <<" stockArmoryReadyFixture=staged naturalArmoryProduction=not-covered"
        <<" actualSoundIntent=1 originalOuterRepaint=1 localIntentMayChange=1"
        <<" zeroAuthorityCapture=1 zeroPending=1 worldQueueLedgerTickInputDelayUnchanged=1"
        <<" actualLoadThenReference=1 typedClockBorrow=1 nativeModifierFullMatrix=not-run"
        <<" naturalMultipleReadyPerKind=not-representable firstRejectedReadyBreak=standard-contract"
        <<" audioDeviceContinuity=not-run fullMpSnapshot=not-run runningMpSession=not-run"
        <<" geometryAlgorithmMigration=not-covered originalOverflowDomain=not-covered\n";
    return true;
}
