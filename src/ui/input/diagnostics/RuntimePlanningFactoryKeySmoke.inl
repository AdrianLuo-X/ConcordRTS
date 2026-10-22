// Included after the real private key/factory adapters, hidden WindowProc fixture and ordinary load.
bool runRuntimePlanningFactoryKeySmokeProbe(RuntimeWindowState& stock,std::ostream& output) {
    struct Clock final : RuntimeUiClock {
        RuntimeUiTimeMs value=1000;mutable std::size_t reads=0;
        RuntimeUiTimeMs nowMs() const noexcept override {++reads;return value;}
    };
    struct ClockBinding {
        RuntimeWindowState& state;const RuntimeUiClock* previous;
        ClockBinding(RuntimeWindowState& root,const RuntimeUiClock& clock):state(root),previous(root.uiClock){state.uiClock=&clock;}
        ~ClockBinding(){state.uiClock=previous;}
    };
    struct Capture {
        int tick=0;std::vector<std::uint8_t> payload;bool accepted=false,applied=false;std::string error;
        bool operator==(const Capture&) const = default;
    };
    struct Witness {
        RuntimeWindowState& state;int source=0;std::vector<Capture> commands;
        Witness(RuntimeWindowState& root,int mode):state(root),source(mode){
            if(source!=0)initializeRuntimeOfflineGameplayRouting(state);else state.offlineGameplayIntentRouting=false;
            state.multiplayerIntentRouter.setCaptureCallback([this](const RuntimeMultiplayerGameplayCommand& command,std::string& error){
                Capture item;item.tick=state.tick;
                if(!encodeRuntimeMultiplayerGameplayCommand(command,item.payload,error))return false;
                if(source==2){item.accepted=true;error.clear();}
                else item.accepted=item.applied=dispatchRuntimeOfflineGameplayCommand(state,command,error);
                item.error=error;commands.push_back(item);return item.accepted;
            });
        }
        ~Witness(){state.multiplayerIntentRouter.setCaptureCallback({});}
        bool replay(std::string& error){
            if(source!=2)return true;
            for(const auto& captured:commands){RuntimeMultiplayerGameplayCommand command;
                if(!decodeRuntimeMultiplayerGameplayCommand(captured.payload,command,error)
                    ||!dispatchRuntimeOfflineGameplayCommand(state,command,error))return false;}
            return true;
        }
    };
    std::size_t checks=0,oldCases=0,directCases=0,nativeCases=0,factoryCases=0,sourceReads=0,guardCases=0,precedenceCases=0,replays=0;
    const auto expect=[&](bool ok,const std::string& why){++checks;if(!ok)output<<"Planning-factory-key integration failed: "<<why<<'\n';return ok;};
    if(!expect(stock.report.success&&stock.world.getSummary().loaded&&!stock.world.getTerrainCells().empty()
        &&stock.rulesLoaded&&stock.artLoaded&&stock.theaterLoaded,"actual stock resources"))return false;
    char* requested=nullptr;std::size_t requestedLength=0;
    const auto environmentResult=_dupenv_s(&requested,&requestedLength,"CONCORD_PLANNING_FACTORY_KEY_OUTPUT");
    const std::filesystem::path directory=requested&&*requested?std::filesystem::path(requested):std::filesystem::path("planning-factory-key-smoke-output");
    std::free(requested);if(!expect(environmentResult==0,"isolated output environment"))return false;
    std::error_code ec;std::filesystem::create_directories(directory,ec);if(!expect(!ec,"isolated output directory"))return false;
    std::ofstream trace(directory/"planning-factory-key-trace.txt",std::ios::out|std::ios::trunc);
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
    constexpr RECT client{0,0,1024,768};const RECT viewport=runtimeMapViewport(client);
    const auto makeBase=[&](int productionMode,bool threePlayers=false){
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

    // This is the existing primary/rally click fixture's two real GAWEAP factories
    // and legal flat terrain. It is never used as the ordinary save/load fixture.
    const auto makeFactory=[&](){
        auto storage=std::make_unique<RuntimeWindowState>();auto& state=*storage;
        state.config=stock.config;state.config.computerAiEnabled=false;state.config.multiplayerActive=false;
        state.config.playerSlots.clear();state.config.playerCountry="Americans";
        state.config.showMainMenu=state.config.showSkirmishMenu=false;
        state.config.keyboardReferenceLayout=true;state.config.keyBindings.clear();
        state.config.configPath=pathString(directory/"private-preferences.ini");
        state.report=stock.report;state.rules=stock.rules;state.art=stock.art;state.theater=stock.theater;
        state.rulesLoaded=state.artLoaded=state.theaterLoaded=true;state.visionEnabled=false;
        state.runtimeSoundsEnabled=state.runtimeMusicEnabled=false;state.runtimeSuppressPlayerAudioFeedback=false;
        state.runtimePreferencePersistenceEnabled=false;state.economy.owner="Americans";
        state.economy.credits=100000;state.economy.skirmishStartApplied=true;state.triggers.bootstrapComplete=true;
        initializeRuntimeLocalProductionLanes(state.economy);state.economy.localProductionLanes.nextSerial=2;
        state.world=stock.world;state.world.accessObjects().clear();state.world.accessTerrainCells().clear();
        state.world.accessTerrainObjects().clear();state.world.accessSmudges().clear();
        for(int y=40;y<=88;++y)for(int x=40;x<=88;++x){
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);cell.tileIndex=cell.subTileIndex=0;
            state.world.accessTerrainCells().push_back(cell);state.terrainInfoByCell.emplace(cell.cellIndex512,RuntimeTerrainInfo{});
        }
        for(int id=1;id<=2;++id){
            WorldObjectInstance factory;factory.instanceId=id;factory.kind=WorldObjectKind::Structure;
            factory.type="GAWEAP";factory.owner=state.economy.owner;factory.alive=true;factory.mission="Guard";
            factory.poweredOperational=true;factory.x=54+id*10;factory.y=64;factory.cell=runtimeCellKey(factory.x,factory.y);
            factory.health=runtimeMaxHealthForObject(state,factory);state.world.accessObjects().push_back(factory);
        }
        state.terrainByCell=buildRuntimeTerrainLookup(state.world);initializeRuntimeNavigation(state);
        state.structureFootprints=buildRuntimeStructureFootprints(state);RuntimeSnapshotActiveStateScope active(state);
        if(!expect(loadRuntimeSoundDatabase(state),"native factory sound resources"))return std::unique_ptr<RuntimeWindowState>{};
        centerRuntimeCameraOnCell(state,client,{69,65});ensureRuntimeCameraForViewport(state,viewport);
        state.tick=7;state.selectedObjectIds={2};state.playerLocalCommands.selectedWaypoint=73;
        state.playerLocalCommands.rallyTargeting=state.playerLocalCommands.followTargeting=true;
        state.playerCommunication.beaconTargeting=true;state.leftPointer.active=state.leftPointer.moved=true;
        state.interactionStatus="planning factory initial";state.repaintRequested=false;
        state.runtimeSoundEvents=0;state.runtimeSoundIntentHistory.clear();state.runtimeSoundPlaybackHistory.clear();state.lastRuntimeSound.clear();
        return storage;
    };
    const auto wideText=[](const std::u16string& value){std::ostringstream s;for(char16_t unit:value)s<<static_cast<std::uint32_t>(unit)<<',';return s.str();};
    const auto fullState=[&](const RuntimeWindowState& state){
        std::ostringstream s;s<<localState(state,true);const auto& ui=state.playerCommunication;
        s<<";communication="<<ui.chatOpen<<','<<ui.chatAllies<<','<<wideText(ui.chatDraft)<<','<<ui.diplomacyOpen<<','<<ui.diplomacyRow
            <<','<<ui.diplomacyPressedControl<<','<<ui.diplomacyHoveredControl<<','<<ui.chatRecipientsExplicit<<','<<static_cast<unsigned>(ui.chatRecipients)
            <<','<<ui.tauntsEnabled<<','<<ui.beaconEditing<<','<<ui.diplomacyBackgroundSource;
        for(const auto& source:ui.diplomacyControlSources)s<<";controlSource="<<hexText(source);
        for(const auto& line:ui.lines)s<<";line="<<hexText(line.owner)<<','<<hexText(line.text)<<','<<line.allies<<','<<line.untilTick;
        for(const auto& b:ui.beacons)s<<";beaconItem="<<hexText(b.owner)<<','<<b.cell.x<<','<<b.cell.y<<','<<b.untilTick<<','<<hexText(b.text)<<','<<b.level;
        const std::map<int,int> cheers(ui.cheerUntilTick.begin(),ui.cheerUntilTick.end());for(const auto& [id,until]:cheers)s<<";cheer="<<id<<','<<until;
        for(const auto& [key,tick]:ui.nextFeedbackTick)s<<";feedback="<<hexText(key.first)<<','<<static_cast<int>(key.second)<<','<<tick;
        s<<";tick="<<state.tick<<";world="<<state.world.deterministicSimulationStateHash()<<";commands="<<hexText(state.playerCommands.serialize())
            <<";commandHash="<<state.playerCommands.stateHash()<<";credits="<<state.economy.credits
            <<";lanes="<<hexText(runtimeSerializeProductionLanes(state.economy.localProductionLanes))<<";sw="<<state.superWeapons.stateHash();
        const auto& sw=state.multiplayerIntentRouter.superWeapon();s<<";localSw=";
        if(sw){s<<sw->superWeaponId<<','<<sw->expectedTargetCount;for(const auto& t:sw->targets)s<<','<<t.cell.x<<','<<t.cell.y<<','<<(t.objectId?std::to_string(*t.objectId):std::string("none"));}else s<<"none";
        s<<";keys="<<state.runtimeMenuKeyboardLoadAttempted;for(const auto& [id,code]:state.runtimeMenuKeyboardBindings)s<<','<<id<<':'<<code;
        return s.str();
    };
    const auto captureText=[&](const Witness& witness){std::ostringstream s;for(const auto& c:witness.commands){s<<c.tick<<':'<<c.accepted<<':'<<c.applied<<':'<<hexText(c.error)<<':';
        for(auto byte:c.payload)s<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(byte);s<<';';}return s.str();};
    const auto setBinding=[](RuntimeWindowState& state,const std::string& action,int code){
        for(auto& entry:state.runtimeMenuKeyboardBindings)if(entry.first==action){entry.second=code;return;}
        state.runtimeMenuKeyboardBindings.emplace_back(action,code);
    };
    const auto oldFactory=[&](RuntimeWindowState& state,int id,std::optional<RuntimeMultiplayerGameplayCell> cell) {
    bool accepted = false;
    std::string error;
    if (runtimeUsesGameplayIntentRouter(state)) {
        refreshRuntimeMultiplayerPlayerIntentAuthority(state);
        accepted = cell ? state.multiplayerIntentRouter.setRallyPoint(id, *cell, error)
            : state.multiplayerIntentRouter.setPrimaryFactory(id, error);
    } else accepted = cell ? applyRuntimePlayerRallyPoint(state, state.economy.owner, id, *cell)
        : applyRuntimePlayerPrimaryFactory(state, state.economy.owner, id);
    state.interactionStatus = accepted ? (cell ? "factory rally point set" : "primary factory selected")
        : "select an owned production factory";
    if (accepted && !cell) emitRuntimeSound(state, "EVA_PrimaryBuildingSelected");
    state.repaintRequested = true;
    return accepted;
};
    const auto oldKey=[&](RuntimeWindowState& state,int code,bool repeated) {
        auto& local=state.playerLocalCommands;
        const auto matches=[&](const std::string& action,int fallback){return code==runtimeMenuKeyboardBindingCode(state,action,fallback);};
    if (matches("PlanningMode", 'Z')) {
        if (!repeated) dispatchRuntimePlayerWaypointFromUi(state,
            local.planning ? RuntimePlayerWaypointOperation::Commit : RuntimePlayerWaypointOperation::Begin);
        return true;
    }
    if (matches("SetPrimaryFactory", 'U' | runtimePlayerControlKey)) {
        if (!repeated && state.selectedObjectIds.size() == 1)
            oldFactory(state, state.selectedObjectIds.front(), std::nullopt);
        return true;
    }
    if (matches("SetRallyPoint", 'J')) {
        local.rallyTargeting = state.selectedObjectIds.size() == 1;
        local.followTargeting = false;
        state.interactionStatus = "choose a factory rally point";
        return true;
    }
        return false;
    };

    const char* actions[]={"PlanningMode","SetPrimaryFactory","SetRallyPoint"};
    const auto configure=[&](RuntimeWindowState& state,int action,WPARAM raw=VK_F13){
        state.runtimeMenuKeyboardLoadAttempted=true;state.runtimeMenuKeyboardBindings.clear();
        setBinding(state,"Options",VK_ESCAPE);
        setBinding(state,"PlanningMode",VK_F14);setBinding(state,"SetPrimaryFactory",VK_F15);setBinding(state,"SetRallyPoint",VK_F16);
        setBinding(state,actions[action],runtimePlayerCurrentKeyCode(raw));
    };
    const auto prepare=[&](RuntimeWindowState& state,int selectionMode,bool planning){
        state.playerLocalCommands.planning=planning;
        if(selectionMode==0)state.selectedObjectIds.clear();
        if(selectionMode==1)state.selectedObjectIds={2};
        if(selectionMode==2)state.selectedObjectIds={1,2};
        if(selectionMode==3)state.selectedObjectIds={2,2};
        if(selectionMode==4)state.selectedObjectIds={999999};
        if(selectionMode>=5){state.selectedObjectIds={2};auto* object=findMutableObjectById(state,2);
            if(!object)return false;
            if(selectionMode==5)object->owner="Neutral";
            if(selectionMode==6)object->type="GAPOWR";
            if(selectionMode==7)object->alive=false;
            if(selectionMode==8)object->buildAnimTicksRemaining=object->buildAnimTicksTotal=10;
            if(selectionMode==9)object->type="GACNST";
        }
        return true;
    };
    const auto primary=[&](const RuntimeWindowState& state){
        const auto* object=findObjectById(state,2);if(!object)return -1;
        const auto key=std::pair{runtimeCanonicalHumanHouseOwner(state.economy.owner),runtimePlayerFactoryCategory(state,*object)};
        const auto it=state.playerCommands.primaryFactories.find(key);return it==state.playerCommands.primaryFactories.end()?-1:it->second;
    };
    const auto triple=[&](const std::string& label,int action,bool repeated,int source,int selectionMode=1,bool planning=false){
        auto os=makeFactory(),ds=makeFactory(),ns=makeFactory();
        if(!expect(os&&ds&&ns,label+" three actual factory worlds"))return false;
        auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native}){configure(*state,action);
            if(!expect(prepare(*state,selectionMode,planning),label+" actual raw selection fixture"))return false;}
        Clock oc,dc,nc;ClockBinding ob(old,oc),db(direct,dc),nb(native,nc);
        Witness ow(old,source),dw(direct,source),nw(native,source);
        RuntimePlayerDefeatNoticeSmokeWindow window(native,1024,768);
        if(!expect(window.window!=nullptr,label+" actual hidden HWND"))return false;
        if(!expect(fullState(old)==fullState(direct)&&fullState(old)==fullState(native),label+" equal complete entry state"))return false;
        const auto code=runtimePlayerCurrentKeyCode(VK_F13);const auto beforeWorld=old.world.deterministicSimulationStateHash();
        const auto beforeCommands=old.playerCommands.serialize();bool oh=false,dh=false;
        {RuntimeSnapshotActiveStateScope active(old);(void)runtimeUiClockNow(old);oh=oldKey(old,code,repeated);}
        {RuntimeSnapshotActiveStateScope active(direct);(void)runtimeUiClockNow(direct);RuntimePlanningFactoryKeyUiHost host(direct);
            dh=handleRuntimePlanningFactoryKey({code,repeated},direct.playerLocalCommands,direct.selectedObjectIds,host);}
        if(!expect(oh&&dh&&fullState(old)==fullState(direct)&&ow.commands==dw.commands,label+" frozen old and same production cpp state/canonical effects"))return false;
        const bool controllerRepaint=old.repaintRequested;old.repaintRequested=direct.repaintRequested=true;
        const UINT message=(action+selectionMode+(repeated?1:0))%2?WM_KEYDOWN:WM_SYSKEYDOWN;
        LRESULT result=0;{RuntimeSnapshotActiveStateScope active(native);result=SendMessageW(window.window,message,VK_F13,repeated?(static_cast<LPARAM>(1)<<30):0);}
        if(fullState(old)!=fullState(native))output<<"Planning-factory-key mismatch label="<<label<<" old="<<hexText(fullState(old))<<" native="<<hexText(fullState(native))<<'\n';
        if(!expect(result==0&&runtimePlayerCurrentKeyCode(VK_F13)==code&&fullState(old)==fullState(native)&&ow.commands==nw.commands,
            label+" actual native state/canonical effects including original handled repaint"))return false;
        if(!expect(oc.reads==1&&dc.reads==1&&nc.reads==1,label+" original clock sampling once"))return false;
        if(repeated&&action<2&&!expect(old.world.deterministicSimulationStateHash()==beforeWorld&&old.playerCommands.serialize()==beforeCommands
            &&ow.commands.empty(),label+" repeat planning/primary has no authority action"))return false;
        if(action==2&&!expect(old.playerLocalCommands.rallyTargeting==(selectionMode==1||selectionMode>=4)&&!old.playerLocalCommands.followTargeting
            &&old.interactionStatus=="choose a factory rally point"&&ow.commands.empty(),label+" J repeat and raw size-only state/status preserved"))return false;
        if(action==1&&!repeated&&(selectionMode==0||selectionMode==2||selectionMode==3)
            &&!expect(primary(old)==-1&&ow.commands.empty()&&old.interactionStatus=="planning factory initial",label+" CtrlU size guard has no dispatch status or sound"))return false;
        if(source==0&&!expect(ow.commands.empty(),label+" genuine non-router branch does not fabricate captured command"))return false;
        for(const auto& item:ow.commands){RuntimeMultiplayerGameplayCommand command;std::string error;
            if(!expect(item.tick==7&&decodeRuntimeMultiplayerGameplayCommand(item.payload,command,error),label+" exact original tick and canonical payload"))return false;
            if(action==0){const auto* body=std::get_if<RuntimeMultiplayerGameplayWaypointCommand>(&command.body);
                if(!expect(body&&body->operation==(planning?RuntimePlayerWaypointOperation::Commit:RuntimePlayerWaypointOperation::Begin)&&body->waypointId==0,label+" original planning operation and zero id"))return false;
            }else{const auto* body=std::get_if<RuntimeMultiplayerGameplaySetPrimaryFactoryCommand>(&command.body);
                if(!expect(body&&body->objectId==(selectionMode==4?999999:2),label+" raw primary object id without premature eligibility"))return false;}
        }
        const auto print=[&](const char* path,const RuntimeWindowState& state,const Witness& witness){
            trace<<"planningFactory path="<<path<<" label="<<label<<" action="<<action<<" repeat="<<repeated<<" selectionMode="<<selectionMode<<" planning="<<planning
                <<" source="<<source<<" message="<<message<<" code="<<code<<" controllerRepaint="<<controllerRepaint<<" state="<<hexText(fullState(state))<<" capture="<<captureText(witness)<<'\n';
        };
        print("old",old,ow);print("direct",direct,dw);print("native",native,nw);
        if(source==2){std::string error;const bool a=ow.replay(error),b=dw.replay(error),c=nw.replay(error);
            if(!expect(a&&b&&c&&fullState(old)==fullState(direct)&&fullState(old)==fullState(native),label+" actual offline replay consumer after intercepted capture: "+error))return false;
            trace<<"replay label="<<label<<" action="<<action<<" commands="<<ow.commands.size()<<" tick="<<old.tick<<" state="<<hexText(fullState(old))<<'\n';++replays;
        }
        if(action==0&&!repeated&&!expect(old.playerLocalCommands.planning==!planning,label+" accepted actual planning state"))return false;
        if(action==1&&!repeated&&(selectionMode==1||selectionMode==8||selectionMode==9)){
            if(!expect(primary(old)==2&&std::count(old.runtimeSoundIntentHistory.begin(),old.runtimeSoundIntentHistory.end(),"EVA_PrimaryBuildingSelected")==1,
                label+" real primary consumer and exactly one EVA even unfinished/construction-yard keyboard boundary"))return false;
        }
        if(action==1&&!repeated&&selectionMode>=4&&selectionMode<=7
            &&!expect(primary(old)==-1&&old.interactionStatus=="select an owned production factory"
                &&std::count(old.runtimeSoundIntentHistory.begin(),old.runtimeSoundIntentHistory.end(),"EVA_PrimaryBuildingSelected")==0,label+" real validation rejects stale foreign nonfactory dead without EVA"))return false;
        sourceReads+=oc.reads+dc.reads+nc.reads;++oldCases;++directCases;++nativeCases;return true;
    };
    for(int source:{0,1})for(int action=0;action<3;++action)for(bool repeated:{false,true})for(int selectionMode=0;selectionMode<4;++selectionMode)
        if(!triple("key-matrix",action,repeated,source,selectionMode))return false;
    for(int source:{0,1})for(int selectionMode=4;selectionMode<=9;++selectionMode)
        if(!triple("raw-primary-eligibility",1,false,source,selectionMode))return false;
    for(int source:{0,1})if(!triple("planning-commit",0,false,source,1,true))return false;
    for(int action=0;action<3;++action)for(bool planning:{false,true})
        if(!triple("intercepted-replay",action,false,2,1,planning))return false;
    const auto factoryTriple=[&](int source,int mode){
        auto os=makeFactory(),ds=makeFactory(),ns=makeFactory();if(!expect(os&&ds&&ns,"shared factory independent real worlds"))return false;
        auto& old=*os;auto& direct=*ds;auto& native=*ns;
        RuntimePlayerFactoryRequest request{2,std::nullopt};
        if(mode==1)request.cell=RuntimeMultiplayerGameplayCell{73,72};
        if(mode==2)request.cell=RuntimeMultiplayerGameplayCell{0,0};
        if(mode==3)request.objectId=999999;
        for(auto* state:{&old,&direct,&native}){
            if(mode==4)findMutableObjectById(*state,2)->owner="Neutral";
            if(mode==5)findMutableObjectById(*state,2)->type="GAPOWR";
            if(mode==6)findMutableObjectById(*state,2)->alive=false;
            if(mode==7)findMutableObjectById(*state,2)->buildAnimTicksRemaining=10;
            if(mode==8)findMutableObjectById(*state,2)->type="GACNST";
        }
        Witness ow(old,source),dw(direct,source),nw(native,source);bool a=false,b=false,c=false;
        {RuntimeSnapshotActiveStateScope active(old);a=oldFactory(old,request.objectId,request.cell);}
        {RuntimeSnapshotActiveStateScope active(direct);RuntimePlayerFactoryUiHost host(direct);b=dispatchRuntimePlayerFactory(request,host);}
        {RuntimeSnapshotActiveStateScope active(native);c=dispatchRuntimePlayerFactoryFromUi(native,request.objectId,request.cell);}
        if(!expect(a==b&&a==c&&fullState(old)==fullState(direct)&&fullState(old)==fullState(native)
            &&ow.commands==dw.commands&&ow.commands==nw.commands,"shared old/samecpp/production wrapper complete effects"))return false;
        const bool valid=mode==0||mode==1||mode==7||mode==8;
        if(!expect(a==valid&&old.repaintRequested&&old.interactionStatus==(valid?(request.cell?"factory rally point set":"primary factory selected")
            :"select an owned production factory"),"original shared accepted/rejected status/repaint"))return false;
        if(!expect(std::count(old.runtimeSoundIntentHistory.begin(),old.runtimeSoundIntentHistory.end(),"EVA_PrimaryBuildingSelected")==((a&&!request.cell)?1:0),"shared exactly accepted primary EVA"))return false;
        if(mode==1&&!expect(old.playerCommands.rallyPoints.at(2).cell==RuntimeMultiplayerGameplayCell{73,72},"actual shared rally cell applied"))return false;
        if(mode==2&&!expect(old.playerCommands.rallyPoints.empty()&&request.cell.has_value(),"optional zero cell is rally and rejected by real terrain owner"))return false;
        const auto print=[&](const char* path,const RuntimeWindowState& state,const Witness& witness){
            trace<<"factory path="<<path<<" source="<<source<<" mode="<<mode<<" accepted="<<a<<" id="<<request.objectId<<" cell=";
            if(request.cell)trace<<request.cell->x<<','<<request.cell->y;else trace<<"none";
            trace<<" state="<<hexText(fullState(state))<<" capture="<<captureText(witness)<<'\n';
        };print("old",old,ow);print("direct",direct,dw);print("wrapper",native,nw);++factoryCases;return true;
    };
    for(int source:{0,1})for(int mode=0;mode<9;++mode)if(!factoryTriple(source,mode))return false;
    for(int mode=0;mode<6;++mode){
        auto storage=makeFactory();if(!expect(static_cast<bool>(storage),"outer guard world"))return false;auto& state=*storage;configure(state,0);
        if(mode==0)state.runtimeMenuOpen=true;if(mode==1)state.playerCommunication.chatOpen=true;if(mode==2)state.playerCommunication.diplomacyOpen=true;
        if(mode==3)state.skirmishMatchFinished=true;if(mode==4){state.campaignMode=true;state.triggers.campaignInputLocked=true;}
        if(mode==5){state.runtimeMenuOpen=true;state.runtimeMenuPage=RuntimeMenuPage::Keyboard;state.runtimeMenuKeyCaptureIndex=0;}
        Clock clock;ClockBinding binding(state,clock);Witness witness(state,1);RuntimePlayerDefeatNoticeSmokeWindow window(state,1024,768);
        if(!expect(window.window!=nullptr,"outer guard actual HWND"))return false;
        const auto world=state.world.deterministicSimulationStateHash();const auto commands=state.playerCommands.serialize();const int tick=state.tick;
        {RuntimeSnapshotActiveStateScope active(state);SendMessageW(window.window,WM_KEYDOWN,VK_F13,0);}
        if(!expect(clock.reads==0&&witness.commands.empty()&&state.world.deterministicSimulationStateHash()==world
            &&state.playerCommands.serialize()==commands&&state.tick==tick,"original outer guards consume before planning sample/authority"))return false;
        trace<<"guard mode="<<mode<<" state="<<hexText(fullState(state))<<" captures=0 sourceReads=0\n";++guardCases;
    }
    for(int mode=0;mode<7;++mode){
        auto storage=makeFactory();if(!expect(static_cast<bool>(storage),"priority world"))return false;auto& state=*storage;
        configure(state,mode==0?1:0);const auto code=runtimePlayerCurrentKeyCode(VK_F13);
        if(mode==0){state.activeBuildTab=RuntimeBuildKind::Unit;setBinding(state,"StructureTab",code);}
        if(mode==1){setBinding(state,"PlanningMode",code);setBinding(state,"SetPrimaryFactory",code);setBinding(state,"SetRallyPoint",code);}
        if(mode==2){state.campaignMode=true;state.triggers.campaignInputLocked=true;}
        if(mode==3)setBinding(state,"PlanningMode",runtimePlayerCurrentKeyCode(VK_F14));
        if(mode==4){state.config.playerCountry="Observer";configure(state,2);}
        RuntimeMultiplayerRuntimeBridge bridge;
        if(mode==5||mode==6){
            state.config.multiplayerActive=true;state.multiplayerBridge=&bridge;state.multiplayerLocalSpectator=mode==5;
            state.config.playerSlots.clear();
        }
        Clock clock;ClockBinding binding(state,clock);Witness witness(state,1);RuntimePlayerDefeatNoticeSmokeWindow window(state,1024,768);
        if(!expect(window.window!=nullptr,"priority actual HWND"))return false;
        const auto beforeWorld=state.world.deterministicSimulationStateHash();const auto beforeCommands=state.playerCommands.serialize();
        {RuntimeSnapshotActiveStateScope active(state);SendMessageW(window.window,mode==2||mode==3?WM_SYSKEYDOWN:WM_KEYDOWN,VK_F13,0);}
        if(!expect(clock.reads==1,"priority original key clock once"))return false;
        if(mode==0&&!expect(primary(state)==-1&&witness.commands.empty()&&state.activeBuildTab==RuntimeBuildKind::Building,"Reference wins lower primary binding"))return false;
        if((mode==1||mode==2)&&!expect(witness.commands.size()==1&&witness.commands.front().accepted&&state.playerLocalCommands.planning,"planning wins internal collision and SYSKEY bypasses campaign normal-key lock"))return false;
        if(mode==3&&!expect(witness.commands.empty()&&state.playerCommands.serialize()==beforeCommands,"unknown SYSKEY does not fabricate planning action"))return false;
        if(mode==4&&!expect(state.selectedObjectIds.empty()&&!state.playerLocalCommands.rallyTargeting&&!state.playerLocalCommands.followTargeting
            &&state.interactionStatus=="choose a factory rally point"&&witness.commands.empty(),"observer clears selection then original local J still runs"))return false;
        if(mode==5||mode==6){const auto& authority=state.multiplayerIntentRouter.authorityState();
            if(!expect(state.config.playerSlots.empty()&&(mode==5?authority.spectator:authority.localDefeated)&&witness.commands.empty()
                &&bridge.pendingGameplayCommandCount()==0&&state.world.deterministicSimulationStateHash()==beforeWorld
                &&state.playerCommands.serialize()==beforeCommands,"real spectator or missing local binding rejects planning at authority gate"))return false;
            ++guardCases;
        }
        trace<<"priority mode="<<mode<<" state="<<hexText(fullState(state))<<" capture="<<captureText(witness)<<" sourceReads="<<clock.reads<<'\n';
        state.multiplayerBridge=nullptr;sourceReads+=clock.reads;++precedenceCases;
    }
    {
        // Exact already successful active three-house/production fixture, not the matrix's fabricated factories.
        auto storage=makeBase(1,true);if(!expect(static_cast<bool>(storage),"active three-house ordinary save fixture"))return false;auto& state=*storage;
        RuntimeSnapshotActiveStateScope active(state);state.config.shortGame=true;state.tick=21;(void)advanceRuntimeParticles(state);
        if(!expect(state.particlesInitialized&&state.particles.currentTick()==21&&state.particles.fixedTickHz()==std::max(1,state.config.fixedTickHz),"active particles and authority tick21"))return false;
        Clock clock;ClockBinding binding(state,clock);const auto savePath=pathString(directory/"planning-factory-key-load.ini");std::string error;
        const bool saved=saveRuntimeState(state,savePath,error);const bool loaded=saved&&loadRuntimeState(state,savePath,error);
        if(!expect(saved&&loaded&&state.tick==21&&state.uiClock==&clock,"ordinary save/load keeps clock borrow and authority tick: "+error))return false;
        // Reacquire all local/selection/owner data after swap; no pre-load child reference survives.
        state.playerLocalCommands.planning=false;configure(state,0);Witness witness(state,1);
        RuntimePlayerDefeatNoticeSmokeWindow window(state,1024,768);if(!expect(window.window!=nullptr,"post-load actual HWND"))return false;
        const auto reads=clock.reads;{RuntimeSnapshotActiveStateScope fresh(state);SendMessageW(window.window,WM_KEYDOWN,VK_F13,0);}
        if(!expect(state.tick==21&&state.uiClock==&clock&&clock.reads==reads+1&&state.playerLocalCommands.planning
            &&witness.commands.size()==1&&witness.commands.front().accepted&&witness.commands.front().applied,"post-load next planning key uses fresh root and real consumer"))return false;
        trace<<"load tick=21 particleTick="<<state.particles.currentTick()<<" sourceSame=1 freshRoot=1 actualPlanningApplied=1 state="<<hexText(fullState(state))<<" capture="<<captureText(witness)<<'\n';
        sourceReads+=clock.reads;
    }
    trace<<"checks="<<checks<<" oldCases="<<oldCases<<" directCases="<<directCases<<" nativeCases="<<nativeCases<<" factoryCases="<<factoryCases<<" guards="<<guardCases<<" precedenceCases="<<precedenceCases<<" replays="<<replays<<'\n';
    trace.flush();if(!expect(static_cast<bool>(trace),"trace flush"))return false;
    output<<"Planning-factory-key integration passed: checks="<<checks<<" oldCases="<<oldCases<<" directCases="<<directCases<<" nativeCases="<<nativeCases
        <<" factoryCases="<<factoryCases<<" sourceReads="<<sourceReads<<" guards="<<guardCases<<" precedenceCases="<<precedenceCases<<" replays="<<replays
        <<" sameProductionCpp=1 frozenOldThreeKeysAndFactory=1 fullLocalState=1 canonicalPayloadTick=1 realNonRouterApply=1 realOfflineRouterApply=1 interceptedCaptureIsNotRunningMP=1"
        <<" actualReplayConsumer=1 actualLoadThenPlanning=1 typedClockBorrow=1 oldOuterPriority=1 primaryEva=1 optionalZeroCellIsRally=1"
        <<" nativeOrderedTrace=observable-command-sound-status-effects standardOrderedQueries=separate-contract nativeModifierFullMatrix=not-run"
        <<" runningMPsession=not-run pendingWireApply=separate-real-bridge-contract fullMpSnapshot=not-run externalNetwork=not-run audioDeviceContinuity=not-run"
        <<" geometryAlgorithmMigration=not-covered originalOverflowDomain=not-covered\n";return true;
}
