// Included after actual WindowProc, the three real UI hosts, and ordinary load.
bool runRuntimeLocalNavigationSmokeProbe(RuntimeWindowState& stock, std::ostream& output) {
    struct Clock final : RuntimeUiClock {
        RuntimeUiTimeMs value=0;
        mutable std::size_t reads=0;
        RuntimeUiTimeMs nowMs() const noexcept override { ++reads; return value; }
    };
    struct ClockBinding {
        RuntimeWindowState& state; const RuntimeUiClock* previous;
        ClockBinding(RuntimeWindowState& root,const RuntimeUiClock& clock)
            :state(root),previous(root.uiClock){state.uiClock=&clock;}
        ~ClockBinding(){state.uiClock=previous;}
    };
    // Real callback and pending queue, without hosting/joining/pumping a match.
    struct AuthorityWitness {
        RuntimeWindowState& state; RuntimeMultiplayerRuntimeBridge bridge;
        std::size_t captures=0;
        explicit AuthorityWitness(RuntimeWindowState& root):state(root){
            state.multiplayerBridge=&bridge;
            state.multiplayerIntentRouter.setCaptureCallback(
                [this](const RuntimeMultiplayerGameplayCommand& command,std::string& error){
                    ++captures; return bridge.captureGameplayCommand(command,error);
                });
        }
        ~AuthorityWitness(){
            state.multiplayerIntentRouter.setCaptureCallback({});
            state.multiplayerBridge=nullptr;
        }
    };
    std::size_t checks=0,oldCases=0,directCases=0,nativeCases=0;
    std::size_t guardCases=0,precedenceCases=0,timingCases=0,sourceReads=0,freshnessCases=0;
    const auto expect=[&](bool ok,const std::string& label){
        ++checks; if(!ok) output<<"Local-navigation integration failed: "<<label<<'\n'; return ok;
    };
    if(!expect(stock.report.success&&stock.world.getSummary().loaded
            &&!stock.world.getTerrainCells().empty()&&stock.rulesLoaded&&stock.artLoaded
            &&stock.theaterLoaded&&stock.rules.findUnitType("UnitType","AMCV")
            &&stock.rules.findUnitType("UnitType","MTNK"),
            "real startup resources/terrain/AMCV/MTNK")) return false;
    const char* requested=std::getenv("CONCORD_LOCAL_NAVIGATION_OUTPUT");
    const std::filesystem::path directory=requested&&*requested
        ?std::filesystem::path(requested):std::filesystem::path("local-navigation-smoke-output");
    std::error_code directoryError; std::filesystem::create_directories(directory,directoryError);
    if(!expect(!directoryError,"isolated output directory")) return false;
    std::ofstream trace(directory/"local-navigation-trace.txt",std::ios::out|std::ios::trunc);
    if(!expect(static_cast<bool>(trace),"trace file")) return false;
    const auto hexText=[](const std::string& value){
        std::ostringstream text;
        for(unsigned char byte:value)
            text<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(byte);
        return text.str();
    };
    const auto authority=[&](const RuntimeWindowState& state,const AuthorityWitness& witness){
        std::ostringstream text;
        text<<state.tick<<':'<<state.world.deterministicSimulationStateHash()
            <<':'<<hexText(state.playerCommands.serialize())<<':'<<witness.captures
            <<':'<<witness.bridge.pendingGameplayCommandCount()<<':'<<state.lastCommand.valid
            <<':'<<hexText(state.lastCommand.verb)<<':'<<state.lastCommand.tick
            <<':'<<state.lastCommand.selectedCount<<':'<<state.lastCommand.acceptedCount
            <<':'<<state.lastCommand.rejectedCount<<':'<<state.lastCommand.targetX
            <<':'<<state.lastCommand.targetY<<':'<<state.lastCommand.targetObjectId
            <<':'<<hexText(state.lastCommand.targetObjectType)<<':'<<state.lastCommand.inputAckImmediate
            <<':'<<state.lastCommand.feedbackLatencyTicks
            <<":queueHash:"<<state.playerCommands.stateHash()
            <<":sessionTick:"<<witness.bridge.orderedTransportPumpSessionView().authoritativeTick()
            <<":barrier:"<<witness.bridge.lockstep().currentBarrierTick()
            <<":inputDelay:"<<witness.bridge.lockstep().config().inputDelayTicks;
        return text.str();
    };
    const auto localState=[](const RuntimeWindowState& state,bool includeStatus){
        std::ostringstream text; const auto& local=state.playerLocalCommands;
        for(int id:state.selectedObjectIds) text<<"selected="<<id<<';';
        for(std::size_t index=0;index<local.teams.size();++index){
            text<<"team="<<index<<':'; for(int id:local.teams[index]) text<<id<<','; text<<';';
        }
        for(std::size_t index=0;index<local.views.size();++index){
            text<<"view="<<index<<':';
            if(local.views[index]) text<<local.views[index]->x<<','<<local.views[index]->y;
            else text<<"none";
            text<<';';
        }
        text<<"local="<<local.planning<<','<<local.selectedWaypoint<<','<<local.followTargeting
            <<','<<local.rallyTargeting<<','<<local.cameraFollow<<','<<local.showCoordinates
            <<','<<local.lastSelectionKey<<','<<local.lastSelectionAtMs
            <<','<<local.veterancyRank<<','<<local.healthRank
            <<";camera="<<state.camera.worldOffsetX<<','<<state.camera.worldOffsetY
            <<','<<state.camera.tileWidth<<','<<state.camera.tileHeight<<','<<state.camera.initialized
            <<";clamp="<<state.cameraClampViewport.left<<','<<state.cameraClampViewport.top
            <<','<<state.cameraClampViewport.right<<','<<state.cameraClampViewport.bottom<<";beacon=";
        if(state.playerCommunication.selectedBeacon)
            text<<state.playerCommunication.selectedBeacon->x<<','<<state.playerCommunication.selectedBeacon->y;
        else text<<"none";
        text<<";target="<<state.playerCommunication.beaconTargeting
            <<";drag="<<state.leftPointer.active<<','<<state.leftPointer.moved;
        if(includeStatus) text<<";status="<<state.interactionStatus;
        return text.str();
    };
    const RECT client{0,0,800,600};
    const RECT viewport=runtimeMapViewport(client);
    const auto make=[&](){
        auto storage=std::make_unique<RuntimeWindowState>(); auto& state=*storage;
        RuntimeSnapshotActiveStateScope active(state);
        state.config=stock.config;state.config.gameMode="Battle";state.config.gameModeRulesOverride.clear();
        state.config.multiplayerActive=false;state.config.playerCountry="Americans";
        state.config.playerSlots.clear();state.config.keyboardReferenceLayout=true;state.config.keyBindings.clear();
        state.config.showMainMenu=state.config.showSkirmishMenu=false;state.config.computerAiEnabled=false;
        state.config.configPath=pathString(directory/"private-preferences.ini");
        state.report=stock.report;state.world=stock.world;state.world.accessObjects().clear();
        state.rules=stock.rules;state.art=stock.art;state.theater=stock.theater;state.mapFacts=stock.mapFacts;
        state.rulesLoaded=state.artLoaded=state.theaterLoaded=true;state.mapFactsLoaded=stock.mapFactsLoaded;
        state.terrainInfoByCell=stock.terrainInfoByCell;state.terrainByCell=buildRuntimeTerrainLookup(state.world);
        state.runtimeSoundsEnabled=state.runtimeMusicEnabled=false;state.runtimeSuppressPlayerAudioFeedback=true;
        state.runtimePreferencePersistenceEnabled=false;state.runtimeVoiceVolume=state.runtimeSoundVolume=0;
        state.visionEnabled=false;state.economy.owner="Player";state.economy.skirmishStartApplied=true;
        state.economy.credits=10000;state.economy.techLevel=10;state.triggers.bootstrapComplete=true;state.tick=7;
        RuntimeHumanHouseAuthority house;house.owner="Player";
        house.canonicalOwner=runtimeCanonicalHumanHouseOwner("Player");house.localFacade=true;
        state.humanHouseAuthorities.push_back(std::move(house));
        const auto& cells=state.world.getTerrainCells();const auto& anchorCell=cells[cells.size()/2];
        const auto playable=buildRuntimeRadarProjection(state);
        if(!expect(playable.valid,"fixture requires actual playable projection"))return std::unique_ptr<RuntimeWindowState>{};
        const TerrainCell* distantCell=nullptr;long long farDistance=-1;
        for(const auto& cell:cells){
            if(!runtimeRadarTerrainCellIntersectsProjection(playable,cell))continue;
            const long long distance=std::abs(static_cast<long long>(cell.x)-anchorCell.x)
                +std::abs(static_cast<long long>(cell.y)-anchorCell.y);
            if(distance>farDistance){farDistance=distance;distantCell=&cell;}
        }
        if(!expect(distantCell!=nullptr,"fixture requires playable distant terrain"))return std::unique_ptr<RuntimeWindowState>{};
        for(int id:{1,2,3,4,5,8,9}){
            const auto& cell=id>=8?*distantCell:anchorCell;
            WorldObjectInstance object;object.instanceId=id;object.kind=WorldObjectKind::Unit;
            object.type=id>=8?"MTNK":"AMCV";object.mapId="LocalNavigation"+std::to_string(id);
            object.owner=id==3?"Enemy":"Player";object.x=cell.x;object.y=cell.y;
            object.cell=runtimeCellKey(object.x,object.y);object.alive=id!=4;object.poweredOperational=true;
            object.mission=id==5?"Limbo":"Guard";object.veterancy=id%3;
            const auto* type=state.rules.findUnitType("UnitType",object.type);
            object.health=std::max(1,type?type->strength:1);
            if(id==2||id==9)object.health=std::max(1,object.health/3);
            state.world.accessObjects().push_back(std::move(object));
        }
        state.selectedObjectIds={1};for(auto& team:state.playerLocalCommands.teams)team={1,2,8};
        for(auto& view:state.playerLocalCommands.views)view=RuntimeMultiplayerGameplayCell{distantCell->x,distantCell->y};
        state.playerLocalCommands.lastSelectionKey='?';state.playerLocalCommands.lastSelectionAtMs=777;
        state.playerCommunication.selectedBeacon=RuntimeMultiplayerGameplayCell{4,6};
        state.playerLastRadarCell=RuntimeMultiplayerGameplayCell{anchorCell.x,anchorCell.y};
        RuntimeCampaignRadarEvent event;event.cell={distantCell->x,distantCell->y};state.triggers.campaignRadarEvents.push_back(event);
        state.interactionStatus="navigation fixture initial";
        centerRuntimeCameraOnCell(state,client,{anchorCell.x,anchorCell.y});state.playerLocalCommands.cameraFollow=true;
        RuntimeMultiplayerGameplayTargetOrderCommand order;order.order=RuntimeMultiplayerGameplayTargetOrderKind::Move;
        order.objectIds={1};order.target=RuntimeMultiplayerGameplayCellTarget{{anchorCell.x,anchorCell.y}};
        if(!state.playerCommands.append("Player",order))return std::unique_ptr<RuntimeWindowState>{};
        initializeRuntimeOfflineGameplayRouting(state);return storage;
    };
    const auto setBinding=[](RuntimeWindowState& state,const std::string& name,int code){
        for(auto& binding:state.runtimeMenuKeyboardBindings)
            if(binding.first==name){binding.second=code;return;}
        state.runtimeMenuKeyboardBindings.emplace_back(name,code);
    };
    const char* actions[]={"PreviousObject","NextObject","SetView1","View1","SetView2","View2",
        "SetView3","View3","SetView4","View4","TypeSelect","CombatantSelect","VeterancyNav","HealthNav",
        "CenterOnRadarEvent"};
    const auto configure=[&](RuntimeWindowState& state,int action){
        state.runtimeMenuKeyboardLoadAttempted=true;state.runtimeMenuKeyboardBindings.clear();
        state.runtimeMenuKeyboardBindings.emplace_back("Options",VK_ESCAPE);
        setBinding(state,actions[action],runtimePlayerCurrentKeyCode(VK_F13));
    };
    const auto projectionState=[](const RuntimeWindowState& state){
        std::ostringstream text;text<<"valid="<<state.projectedObjectsCacheValid
            <<";viewport="<<state.projectedObjectsCacheViewport.left<<','<<state.projectedObjectsCacheViewport.top
            <<','<<state.projectedObjectsCacheViewport.right<<','<<state.projectedObjectsCacheViewport.bottom
            <<";camera="<<state.projectedObjectsCacheCameraX<<','<<state.projectedObjectsCacheCameraY
            <<','<<state.projectedObjectsCacheTileWidth<<','<<state.projectedObjectsCacheTileHeight
            <<";signature="<<state.projectedObjectsCacheSignature<<";gates="<<state.gateRenderSnapshots.size()
            <<";special="<<state.specialMechanismRenderSnapshots.size();
        for(const auto& item:state.projectedObjectsCache)
            text<<";projected="<<(item.object?item.object->instanceId:-1)<<','<<item.bounds.left
                <<','<<item.bounds.top<<','<<item.bounds.right<<','<<item.bounds.bottom
                <<','<<item.center.x<<','<<item.center.y<<','<<item.renderCenter.x<<','<<item.renderCenter.y
                <<','<<item.hasMobileSpriteBounds<<','<<item.hasBodyHitClip<<','<<item.observerType;
        return text.str();
    };
    const auto cachedScreen=[](const RuntimeWindowState& state,const RECT& area){
        std::set<int> ids;
        for(const auto& item:state.projectedObjectsCache)
            if(item.object&&rectsIntersect(area,item.bounds))ids.insert(item.object->instanceId);
        return ids;
    };
    // Frozen original four consumer bodies. No new navigation host/controller is called.
    const auto oldLoop=[](RuntimeWindowState& state,const RECT& client,int code,bool repeated,
        std::uint64_t now,int group){
        const auto matches=[&](const std::string& action,int fallback){
            return code==runtimeMenuKeyboardBindingCode(state,action,fallback);
        };
        const RECT viewport=runtimeMapViewport(client);auto& local=state.playerLocalCommands;
        if(group==0){
    const bool previous=matches("PreviousObject",'M');
    if (previous || matches("NextObject",'N')) {
        if (repeated) return true;
        std::vector<int> ids;
        for (const auto& object:state.world.getObjects()) {
            if (runtimePlayerMobileSelectable(state, object)) ids.push_back(object.instanceId);
        }
        if (ids.empty()) return true;
        std::sort(ids.begin(),ids.end());
        const int current=state.selectedObjectIds.empty()?(previous?std::numeric_limits<int>::max():0):state.selectedObjectIds.front();
        auto next=previous?std::lower_bound(ids.begin(),ids.end(),current):std::upper_bound(ids.begin(),ids.end(),current);
        if (previous) {if (next==ids.begin()) next=ids.end();--next;}
        else if (next==ids.end()) next=ids.begin();
        applyRuntimePlayerSelection(state,{*next},false,false);
        state.playerCommunication.selectedBeacon.reset();
        local.cameraFollow=false;
        runtimePlayerCenterSelection(state,client);
        return true;
    }
        }
        if(group==1){
    for (int index = 0; index < 4; ++index) {
        const std::string suffix = std::to_string(index + 1);
        if (matches("SetView" + suffix, (VK_F1 + index) | runtimePlayerControlKey)) {
            const RuntimeInputPixelPoint center = runtimeInputPixelBoundsMidpoint(
                {viewport.left, viewport.top}, {viewport.right, viewport.bottom});
            const POINT point = approximateCellFromScreenPoint(state, viewport,
                POINT{center.x, center.y});
            local.views[static_cast<std::size_t>(index)] = RuntimeMultiplayerGameplayCell{point.x, point.y};
            state.interactionStatus = "view " + suffix + " saved";
            return true;
        }
        if (matches("View" + suffix, VK_F1 + index)) {
            const auto& saved = local.views[static_cast<std::size_t>(index)];
            if (saved) centerRuntimeCameraOnCell(state, client, {saved->x, saved->y});
            return true;
        }
    }
        }
        if(group==2){
    const bool type = matches("TypeSelect", 'T');
    const bool combat = matches("CombatantSelect", 'P');
    const bool veteran = matches("VeterancyNav", 'Y');
    const bool health = matches("HealthNav", 'U');
    if (type || combat || veteran || health) {
        if (repeated) return true;
        const bool wholeMap = local.lastSelectionKey == code && runtimeUiWithinDoubleSelection(now, local.lastSelectionAtMs);
        std::set<int> onScreen;
        for (const auto& projected : runtimeProjectedObjectsForViewport(state, viewport))
            if (projected.object && rectsIntersect(viewport, projected.bounds))
                onScreen.insert(projected.object->instanceId);
        std::vector<RuntimePlayerSelectionCandidate> candidates;
        for (const auto& object : state.world.getObjects()) {
            if (!runtimePlayerMobileSelectable(state, object)) continue;
            const auto* unit = findRuntimeUnitType(state.rules, object);
            candidates.push_back({object.instanceId, toUpperAsciiLocal(object.type),
                object.veterancy, unit && unit->isSelectableCombatant, onScreen.contains(object.instanceId),
                object.health, unit ? std::max(1, unit->strength) : std::max(1, object.health)});
        }
        const auto selected = local.quickSelect(type ? RuntimePlayerQuickSelection::Type
            : combat ? RuntimePlayerQuickSelection::Combatant
            : health ? RuntimePlayerQuickSelection::Health : RuntimePlayerQuickSelection::Veterancy,
            state.selectedObjectIds, candidates, wholeMap, false, wholeMap);
        applyRuntimePlayerSelection(state, selected, false, false);
        state.playerCommunication.selectedBeacon.reset();
        local.lastSelectionKey = code; local.lastSelectionAtMs = now;
        return true;
    }
        }
        if(group==3){
    if (matches("CenterOnRadarEvent", VK_SPACE)) {
        if (state.playerLastRadarCell) {
            centerRuntimeCameraOnCell(state, client,
                {state.playerLastRadarCell->x, state.playerLastRadarCell->y});
        } else if (!state.triggers.campaignRadarEvents.empty()) {
            centerRuntimeCameraOnCell(state, client, state.triggers.campaignRadarEvents.back().cell);
        }
        return true;
    }
        }
        return false;
    };
    const auto directLoop=[&](RuntimeWindowState& state,int code,bool repeated,std::uint64_t now,int group){
        RuntimeObjectCycleUiHost cycle(state,client,viewport);
        RuntimeViewNavigationUiHost view(state,client,viewport);
        RuntimeQuickSelectionUiHost quick(state,client,viewport);
        const RuntimeLocalNavigationInput input{code,repeated,now};
        if(group==0)return handleRuntimeObjectCycleKey(input,state.playerLocalCommands,state.selectedObjectIds,cycle);
        if(group==1)return handleRuntimeViewNavigationKey(input,state.playerLocalCommands,view);
        if(group==2)return handleRuntimeQuickSelectionKey(input,state.playerLocalCommands,state.selectedObjectIds,quick);
        return handleRuntimeRadarNavigationKey(input,view);
    };
    const auto groupFor=[](int action){return action<2?0:action<10?1:action<14?2:3;};
    // Modes are fixture setup before the input, not new player permissions.
    const auto prepare=[&](RuntimeWindowState& state,int action,int mode,std::uint64_t last){
        configure(state,action);state.playerLocalCommands.lastSelectionAtMs=last;
        if(mode==1)state.config.playerCountry="Observer";
        if(mode==2)state.multiplayerLocalSpectator=true;
        if(mode==3){state.campaignMode=true;state.triggers.campaignInputLocked=true;}
        if(mode==4)state.selectedObjectIds.clear();
        if(mode==5)for(auto& object:state.world.accessObjects())object.alive=false;
        if(mode==6)state.playerLocalCommands.views[static_cast<std::size_t>((action-2)/2)].reset();
        if(mode==7){state.playerLastRadarCell.reset();state.triggers.campaignRadarEvents.clear();}
        if(mode==8)state.playerLastRadarCell.reset();
        if(mode==9)setBinding(state,"SetView1",runtimePlayerCurrentKeyCode(VK_F13));
        if(mode==10)for(const char* name:{"TypeSelect","CombatantSelect","VeterancyNav","HealthNav"})
            setBinding(state,name,runtimePlayerCurrentKeyCode(VK_F13));
        if(mode==11)state.playerLocalCommands.lastSelectionKey=runtimePlayerCurrentKeyCode(VK_F13);
    };
    const auto triple=[&](const std::string& label,int action,bool repeated,UINT message,
        std::uint64_t now,std::uint64_t last,int mode){
        auto oldStorage=make(),directStorage=make(),nativeStorage=make();
        if(!expect(oldStorage&&directStorage&&nativeStorage,label+" actual resource worlds"))return false;
        auto& old=*oldStorage;auto& direct=*directStorage;auto& native=*nativeStorage;
        for(auto* state:{&old,&direct,&native})prepare(*state,action,mode,last);
        if(mode==0){
            for(auto* state:{&old,&direct,&native}){
                if(!expect(runtimePlayerLocalSelectable(*state,1)&&runtimePlayerLocalSelectable(*state,2)
                        &&runtimePlayerLocalSelectable(*state,8)&&runtimePlayerLocalSelectable(*state,9),
                        label+" actual owned eligibility"))return false;
                for(int id:{3,4,5,999})if(!expect(!runtimePlayerLocalSelectable(*state,id),
                        label+" real foreign/dead/limbo/missing gate"))return false;
            }
        }
        Clock oldClock,directClock,nativeClock;oldClock.value=directClock.value=nativeClock.value=now;
        ClockBinding ob(old,oldClock),db(direct,directClock),nb(native,nativeClock);
        AuthorityWitness ow(old),dw(direct),nw(native);
        const auto oldAuthority=authority(old,ow),directAuthority=authority(direct,dw),nativeAuthority=authority(native,nw);
        const auto initialLocal=localState(old,true),initialProjection=projectionState(old);
        const int code=runtimePlayerCurrentKeyCode(VK_F13);const int group=groupFor(action);
        RuntimePlayerDefeatNoticeSmokeWindow window(native,800,600);
        if(!expect(window.window!=nullptr,label+" actual hidden window"))return false;
        const auto oldInputSignature=runtimeProjectedObjectsSignature(old),
            directInputSignature=runtimeProjectedObjectsSignature(direct),
            nativeInputSignature=runtimeProjectedObjectsSignature(native);
        if(!expect(oldInputSignature==directInputSignature&&oldInputSignature==nativeInputSignature,
                label+" equal cold or warm signature inputs before three paths"))return false;
        bool oldHandled=false,directHandled=false;
        {RuntimeSnapshotActiveStateScope active(old);rejectRuntimeObserverSelection(old);
         oldHandled=oldLoop(old,client,code,repeated,runtimeUiClockNow(old),group);}
        {RuntimeSnapshotActiveStateScope active(direct);rejectRuntimeObserverSelection(direct);
         directHandled=directLoop(direct,code,repeated,runtimeUiClockNow(direct),group);}
        LRESULT result=0;
        {RuntimeSnapshotActiveStateScope active(native);
         result=SendMessageW(window.window,message,VK_F13,repeated?(static_cast<LPARAM>(1)<<30):0);}
        if(!expect(runtimePlayerCurrentKeyCode(VK_F13)==code,label+" stable actual modifiers"))return false;
        if(!expect(oldHandled&&directHandled&&result==0&&localState(old,true)==localState(direct,true)
                &&localState(old,true)==localState(native,true),label+" full local state three paths"))return false;
        if(!expect(projectionState(old)==projectionState(direct)&&projectionState(old)==projectionState(native),
                label+" full projection cache/result/camera/snapshot effects three paths"))return false;
        if(!expect(authority(old,ow)==oldAuthority&&authority(direct,dw)==directAuthority
                &&authority(native,nw)==nativeAuthority&&oldAuthority==directAuthority&&oldAuthority==nativeAuthority,
                label+" world queue stateHash tick sessionTick inputDelay unchanged"))return false;
        if(!expect(ow.captures==0&&dw.captures==0&&nw.captures==0
                &&ow.bridge.pendingGameplayCommandCount()==0&&dw.bridge.pendingGameplayCommandCount()==0
                &&nw.bridge.pendingGameplayCommandCount()==0,label+" actual capture pending zero"))return false;
        if(!expect(oldClock.reads==1&&directClock.reads==1&&nativeClock.reads==1,
                label+" original argument samples once each"))return false;
        if(repeated&&(group==0||group==2)&&mode==0){
            if(!expect(localState(old,true)==initialLocal&&projectionState(old)==initialProjection,
                    label+" repeat no local or query effects"))return false;
        }
        if(group==2&&!repeated){
            if(!expect(old.projectedObjectsCacheValid
                    &&old.projectedObjectsCacheSignature==oldInputSignature
                    &&old.playerLocalCommands.lastSelectionKey==code
                    &&old.playerLocalCommands.lastSelectionAtMs==now&&!old.playerCommunication.selectedBeacon,
                    label+" real screen query even whole map and last writes"))return false;
        }
        if(mode==6||mode==7)if(!expect(old.playerLocalCommands.cameraFollow,
                label+" empty view or radar leaves follow untouched"))return false;
        if(mode==9)if(!expect(old.interactionStatus=="view 1 saved",label+" earliest SetView wins"))return false;
        const auto print=[&](const char* path,const RuntimeWindowState& state,const AuthorityWitness& witness){
            trace<<"navigation path="<<path<<" label="<<label<<" action="<<action<<" group="<<group
                <<" repeat="<<repeated<<" source="<<message<<" raw="<<VK_F13<<" code="<<code<<" now="<<now
                <<" state="<<hexText(localState(state,true))<<" query="<<hexText(projectionState(state))
                <<" authority="<<hexText(authority(state,witness))<<" captures="<<witness.captures
                <<" pending="<<witness.bridge.pendingGameplayCommandCount()<<'\n';
        };
        print("old",old,ow);print("direct",direct,dw);print("native",native,nw);
        sourceReads+=oldClock.reads+directClock.reads+nativeClock.reads;++oldCases;++directCases;++nativeCases;
        return true;
    };
    for(int action=0;action<15;++action)for(bool repeated:{false,true})for(UINT message:{WM_KEYDOWN,WM_SYSKEYDOWN})
        if(!triple("matrix",action,repeated,message,1000,777,0))return false;
    for(int action:{0,1})for(int mode:{4,5})
        if(!triple(mode==4?"empty-selection":"no-eligible",action,false,WM_KEYDOWN,1000,777,mode))return false;
    for(int action:{3,5,7,9})if(!triple("empty-view",action,false,WM_KEYDOWN,1000,777,6))return false;
    for(int mode:{7,8})if(!triple(mode==7?"empty-radar":"campaign-radar-back",14,true,WM_SYSKEYDOWN,1000,777,mode))return false;
    if(!triple("set-view-collision",5,false,WM_KEYDOWN,1000,777,9)
            ||!triple("quick-collision-type-priority",13,false,WM_KEYDOWN,1000,777,10))return false;
    for(int mode:{1,2})for(int action:{0,2,10,14})
        if(!triple(mode==1?"observer":"spectator",action,false,WM_KEYDOWN,1000,777,mode))return false;
    for(int action:{0,2,10,14})if(!triple("campaign-sys-exception",action,false,WM_SYSKEYDOWN,1000,777,3))return false;
    struct Timing{std::uint64_t now,last;};constexpr auto maximum=std::numeric_limits<std::uint64_t>::max();
    const Timing timing[]={{2499,2000},{2500,2000},{2501,2000},{1999,2000},{5,maximum-10},{0,0}};
    for(const auto& item:timing)for(int action:{10,11,12,13}){
        if(!triple("unsigned-quick-time",action,false,WM_KEYDOWN,item.now,item.last,11))return false;
        ++timingCases;
    }
    // Stateful geometry query must refresh without any WM_PAINT between inputs.
    {
        auto os=make(),ds=make(),ns=make();if(!expect(os&&ds&&ns,"fresh query real worlds"))return false;
        auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native})configure(*state,11);
        Clock oc,dc,nc;ClockBinding ob(old,oc),db(direct,dc),nb(native,nc);
        AuthorityWitness ow(old),dw(direct),nw(native);
        RuntimePlayerDefeatNoticeSmokeWindow window(native,800,600);
        if(!expect(window.window!=nullptr,"fresh query hidden window"))return false;
        std::set<int> previousScreen;std::uint64_t previousSignature=0;int previousCameraX=0,previousCameraY=0;
        for(int phase=0;phase<3;++phase){
            for(auto* state:{&old,&direct,&native}){
                RuntimeSnapshotActiveStateScope active(*state);
                if(phase==1){
                    const auto* object=findObjectById(*state,8);
                    if(!expect(object!=nullptr,"distantCell real MTNK exists"))return false;
                    centerRuntimeCameraOnCell(*state,client,{object->x,object->y});
                }
                if(phase==2){
                    const auto* origin=findObjectById(*state,1);
                    if(!expect(origin!=nullptr,"anchorCell real AMCV exists"))return false;
                    const int x=origin->x,y=origin->y;
                    for(auto& object:state->world.accessObjects())if(object.instanceId==8){
                        object.x=x;object.y=y;object.cell=runtimeCellKey(x,y);object.health=1;
                        clearRuntimeRenderPosition(object);
                    }
                }
            }
            oc.value=dc.value=nc.value=1000+static_cast<std::uint64_t>(phase)*100;
            const auto oa=authority(old,ow),da=authority(direct,dw),na=authority(native,nw);
            const int code=runtimePlayerCurrentKeyCode(VK_F13);
            const auto oldInputSignature=runtimeProjectedObjectsSignature(old),
                directInputSignature=runtimeProjectedObjectsSignature(direct),
                nativeInputSignature=runtimeProjectedObjectsSignature(native);
            if(!expect(oldInputSignature==directInputSignature&&oldInputSignature==nativeInputSignature,
                    "equal cold or warm signature inputs before fresh three paths"))return false;
            {RuntimeSnapshotActiveStateScope active(old);
             if(!expect(oldLoop(old,client,code,false,runtimeUiClockNow(old),2),"fresh old consumer"))return false;}
            {RuntimeSnapshotActiveStateScope active(direct);
             if(!expect(directLoop(direct,code,false,runtimeUiClockNow(direct),2),"fresh real host consumer"))return false;}
            {RuntimeSnapshotActiveStateScope active(native);SendMessageW(window.window,WM_KEYDOWN,VK_F13,0);}
            const auto screen=cachedScreen(old,viewport);
            if(!expect(old.projectedObjectsCacheValid&&direct.projectedObjectsCacheValid&&native.projectedObjectsCacheValid
                    &&screen==cachedScreen(direct,viewport)&&screen==cachedScreen(native,viewport)
                    &&projectionState(old)==projectionState(direct)&&projectionState(old)==projectionState(native)
                    &&localState(old,true)==localState(direct,true)&&localState(old,true)==localState(native,true),
                    "fresh owned IDs/cache/local three paths"))return false;
            bool realBody=false;
            for(const auto& item:old.projectedObjectsCache)
                if(item.object&&screen.contains(item.object->instanceId)&&item.hasMobileSpriteBounds)realBody=true;
            if(!expect(realBody&&old.projectedObjectsCacheSignature==oldInputSignature
                    &&old.projectedObjectsCacheCameraX==old.camera.worldOffsetX
                    &&old.projectedObjectsCacheCameraY==old.camera.worldOffsetY
                    &&runtimeRectEquals(old.projectedObjectsCacheViewport,viewport)
                    &&native.backBuffer.pixels==nullptr,"actual sprite/voxel query with no paint")){
                const auto diagnostic=[&](const char* path,const RuntimeWindowState& state,std::uint64_t inputSignature){
                    output<<"Local-navigation freshness diagnostic: phase="<<phase<<" path="<<path
                        <<" realBodyOld="<<realBody<<" cacheValid="<<state.projectedObjectsCacheValid
                        <<" signatureMatchesInput="<<(state.projectedObjectsCacheSignature==inputSignature)
                        <<" cameraXMatches="<<(state.projectedObjectsCacheCameraX==state.camera.worldOffsetX)
                        <<" cameraYMatches="<<(state.projectedObjectsCacheCameraY==state.camera.worldOffsetY)
                        <<" viewportMatches="<<runtimeRectEquals(state.projectedObjectsCacheViewport,viewport)
                        <<" nativeBufferAbsent="<<(native.backBuffer.pixels==nullptr)
                        <<" inputSignature="<<inputSignature<<" cacheSignature="<<state.projectedObjectsCacheSignature
                        <<" cacheCamera="<<state.projectedObjectsCacheCameraX<<','<<state.projectedObjectsCacheCameraY
                        <<" camera="<<state.camera.worldOffsetX<<','<<state.camera.worldOffsetY
                        <<" cacheViewport="<<state.projectedObjectsCacheViewport.left<<','<<state.projectedObjectsCacheViewport.top
                        <<','<<state.projectedObjectsCacheViewport.right<<','<<state.projectedObjectsCacheViewport.bottom
                        <<" viewport="<<viewport.left<<','<<viewport.top<<','<<viewport.right<<','<<viewport.bottom
                        <<" screen=";
                    for(int id:cachedScreen(state,viewport))output<<id<<',';
                    for(const auto& item:state.projectedObjectsCache)if(item.object)
                        output<<" body="<<item.object->instanceId<<':'<<item.object->type
                            <<':'<<item.object->x<<','<<item.object->y<<','<<item.object->level
                            <<':'<<item.hasMobileSpriteBounds<<':'<<item.hasBodyHitClip
                            <<':'<<item.bounds.left<<','<<item.bounds.top<<','<<item.bounds.right<<','<<item.bounds.bottom
                            <<':'<<item.renderCenter.x<<','<<item.renderCenter.y;
                    output<<'\n';
                };
                diagnostic("old",old,oldInputSignature);diagnostic("direct",direct,directInputSignature);
                diagnostic("native",native,nativeInputSignature);return false;
            }
            if(phase==0){
                if(!expect(screen.contains(1)&&screen.contains(2)&&!screen.contains(8),
                        "initial camera sees anchorCell real actors only"))return false;
            }else if(phase==1){
                if(!expect(screen.contains(8)&&screen.contains(9)&&!screen.contains(1)
                        &&screen!=previousScreen&&(old.camera.worldOffsetX!=previousCameraX
                            ||old.camera.worldOffsetY!=previousCameraY),"camera change refreshes screen IDs without paint"))return false;
            }else if(!expect(!screen.contains(8)&&screen.contains(9)&&screen!=previousScreen
                    &&old.projectedObjectsCacheSignature!=previousSignature,
                    "world position/health change refreshes screen IDs and signature without paint"))return false;
            if(!expect(authority(old,ow)==oa&&authority(direct,dw)==da&&authority(native,nw)==na
                    &&ow.captures==0&&dw.captures==0&&nw.captures==0
                    &&ow.bridge.pendingGameplayCommandCount()==0&&dw.bridge.pendingGameplayCommandCount()==0
                    &&nw.bridge.pendingGameplayCommandCount()==0&&runtimePlayerCurrentKeyCode(VK_F13)==code,
                    "fresh query input preserves each preconfigured authority"))return false;
            const auto print=[&](const char* path,const RuntimeWindowState& state,const AuthorityWitness& witness){
                trace<<"freshness path="<<path<<" phase="<<phase<<" screen=";
                for(int id:cachedScreen(state,viewport))trace<<id<<',';
                trace<<" query="<<hexText(projectionState(state))<<" state="<<hexText(localState(state,true))
                    <<" authority="<<hexText(authority(state,witness))
                    <<" captures="<<witness.captures<<" pending="<<witness.bridge.pendingGameplayCommandCount()<<" paint=0\n";
            };
            print("old",old,ow);print("direct",direct,dw);print("native",native,nw);
            previousScreen=screen;previousSignature=old.projectedObjectsCacheSignature;
            previousCameraX=old.camera.worldOffsetX;previousCameraY=old.camera.worldOffsetY;
            ++oldCases;++directCases;++nativeCases;++freshnessCases;
        }
        if(!expect(oc.reads==3&&dc.reads==3&&nc.reads==3,"fresh native source reads once per key"))return false;
        sourceReads+=oc.reads+dc.reads+nc.reads;
        // The standard boundary owns both query results after the private world changes.
        RuntimeSnapshotActiveStateScope active(direct);RuntimeQuickSelectionUiHost host(direct,client,viewport);
        const auto screen=host.onScreenObjectIds();const auto candidates=host.mobileCandidates(screen);
        const auto candidateText=[](const std::vector<RuntimePlayerSelectionCandidate>& values){
            std::ostringstream text;for(const auto& value:values)
                text<<value.objectId<<','<<value.type<<','<<value.veterancy<<','<<value.combatant
                    <<','<<value.onScreen<<','<<value.health<<','<<value.maximumHealth<<';';
            return text.str();
        };
        const auto beforeCandidates=candidateText(candidates);const auto beforeScreen=screen;
        direct.projectedObjectsCache.clear();direct.world.accessObjects().clear();
        if(!expect(!candidates.empty()&&!screen.empty()&&candidateText(candidates)==beforeCandidates&&screen==beforeScreen,
                "real private host returns owned IDs and candidate values"))return false;
        trace<<"owned-query-lifetime screenCount="<<screen.size()<<" candidateCount="<<candidates.size()
            <<" oldWorldCleared=1 valuesRetained=1\n";
    }
    for(int mode=0;mode<6;++mode){
        auto storage=make();if(!expect(static_cast<bool>(storage),"guard world"))return false;
        auto& state=*storage;configure(state,11);Clock clock;clock.value=1000;
        ClockBinding binding(state,clock);AuthorityWitness witness(state);
        if(mode==0)state.runtimeMenuOpen=true;
        if(mode==1)state.playerCommunication.chatOpen=true;
        if(mode==2)state.playerCommunication.diplomacyOpen=true;
        if(mode==3)state.skirmishMatchFinished=true;
        if(mode==4){state.campaignMode=true;state.triggers.campaignInputLocked=true;}
        if(mode==5){state.runtimeMenuOpen=true;state.runtimeMenuPage=RuntimeMenuPage::Keyboard;state.runtimeMenuKeyCaptureIndex=0;}
        RuntimePlayerDefeatNoticeSmokeWindow window(state,800,600);
        if(!expect(window.window!=nullptr,"guard hidden window"))return false;
        const auto local=localState(state,false),query=projectionState(state),before=authority(state,witness);
        {RuntimeSnapshotActiveStateScope active(state);SendMessageW(window.window,WM_KEYDOWN,VK_F13,0);}
        if(!expect(localState(state,false)==local&&projectionState(state)==query&&authority(state,witness)==before
                &&clock.reads==0&&witness.captures==0&&witness.bridge.pendingGameplayCommandCount()==0,
                "native original outer guard before navigation "+std::to_string(mode)))return false;
        trace<<"guard mode="<<mode<<" fullLocalUnchanged=1 queryUnchanged=1 authorityUnchanged=1 sourceReads=0\n";
        ++guardCases;
    }
    for(int mode=0;mode<8;++mode){
        auto storage=make();if(!expect(static_cast<bool>(storage),"priority world"))return false;
        auto& state=*storage;configure(state,0);Clock clock;clock.value=1000;
        ClockBinding binding(state,clock);AuthorityWitness witness(state);
        const WPARAM raw=mode==1||mode==2?VK_NUMPAD1:VK_F13;
        const int code=runtimePlayerCurrentKeyCode(raw),normalized=(code&~0xff)|'1';
        if(mode==0||mode==1||mode==7)setBinding(state,"ShowCoordinates",code);
        if(mode==1||mode==2)setBinding(state,"PreviousObject",normalized);
        if(mode==2)setBinding(state,"TeamSelect_1",normalized);
        if(mode==3){setBinding(state,"PreviousObject",'M');setBinding(state,"TeamSelect_1",code);setBinding(state,"View1",code);}
        if(mode==4){setBinding(state,"PreviousObject",'M');setBinding(state,"View1",code);setBinding(state,"TypeSelect",code);}
        if(mode==5){setBinding(state,"PreviousObject",'M');setBinding(state,"SetRallyPoint",code);setBinding(state,"CenterOnRadarEvent",code);}
        if(mode==6){setBinding(state,"TeamSelect_1",code);setBinding(state,"View1",code);}
        if(mode==7){state.config.playerCountry="Observer";state.leftPointer.active=state.leftPointer.moved=true;}
        const auto before=authority(state,witness),beforeLocal=localState(state,false);
        const int beforeX=state.camera.worldOffsetX,beforeY=state.camera.worldOffsetY;
        RuntimePlayerDefeatNoticeSmokeWindow window(state,800,600);
        if(!expect(window.window!=nullptr,"priority hidden window"))return false;
        {RuntimeSnapshotActiveStateScope active(state);SendMessageW(window.window,WM_KEYDOWN,raw,0);}
        if(!expect(runtimePlayerCurrentKeyCode(raw)==code&&authority(state,witness)==before&&clock.reads==1
                &&witness.captures==0&&witness.bridge.pendingGameplayCommandCount()==0,
                "priority real input and zero authority "+std::to_string(mode)))return false;
        if(mode==0||mode==1){
            if(!expect(state.playerLocalCommands.showCoordinates&&state.selectedObjectIds==std::vector<int>{1}
                    &&state.playerLocalCommands.cameraFollow&&state.playerCommunication.selectedBeacon
                    &&state.playerLocalCommands.lastSelectionKey=='?',"reference before raw/normalized navigation"))return false;
        }else if(mode==2||mode==6){
            if(!expect(state.selectedObjectIds==std::vector<int>{9}&&!state.playerLocalCommands.cameraFollow
                    &&!state.playerCommunication.selectedBeacon&&state.playerLocalCommands.lastSelectionKey=='?',
                    "Cycle before Team and View; NumPad normalization after reference"))return false;
        }else if(mode==3){
            if(!expect(state.selectedObjectIds==std::vector<int>({1,2,8})&&state.playerLocalCommands.lastSelectionKey=='1'
                    &&state.playerLocalCommands.lastSelectionAtMs==1000&&state.playerLocalCommands.cameraFollow,
                    "Team original position wins View collision"))return false;
        }else if(mode==4){
            if(!expect(!state.playerLocalCommands.cameraFollow&&state.playerLocalCommands.lastSelectionKey=='?'
                    &&state.selectedObjectIds==std::vector<int>{1}&&state.playerCommunication.selectedBeacon,
                    "View original position wins Quick collision"))return false;
        }else if(mode==5){
            if(!expect(state.playerLocalCommands.rallyTargeting&&!state.playerLocalCommands.followTargeting
                    &&state.playerLocalCommands.cameraFollow&&state.camera.worldOffsetX==beforeX
                    &&state.camera.worldOffsetY==beforeY&&state.playerLocalCommands.lastSelectionKey=='?',
                    "remaining rally targeting consumes before final Radar"))return false;
        }else if(!expect(state.playerLocalCommands.showCoordinates&&state.selectedObjectIds.empty()
                &&!state.leftPointer.active&&!state.leftPointer.moved,
                "observer rejection clears selection then reaches original local reference"))return false;
        trace<<"priority mode="<<mode<<" raw="<<raw<<" code="<<code<<" normalized="<<normalized
            <<" before="<<hexText(beforeLocal)<<" state="<<hexText(localState(state,true))
            <<" authority="<<hexText(authority(state,witness))<<" captures=0 pending=0\n";
        sourceReads+=clock.reads;++precedenceCases;
    }
    {
        auto storage=std::make_unique<RuntimeWindowState>();auto& state=*storage;
        RuntimeSnapshotActiveStateScope active(state);
        state.config=stock.config;state.config.gameMode="Battle";state.config.gameModeRulesOverride.clear();
        state.config.multiplayerActive=false;state.config.playerCountry="Americans";
        state.config.computerAiEnabled=false;state.config.shortGame=true;
        state.config.keyboardReferenceLayout=true;state.config.keyBindings.clear();
        state.config.showMainMenu=state.config.showSkirmishMenu=false;
        state.config.configPath=pathString(directory/"load-preferences.ini");
        state.config.playerSlots.assign(3,{});
        for(std::size_t index=0;index<3;++index){
            auto& slot=state.config.playerSlots[index];slot.enabled=slot.human=true;
            slot.local=index==0;slot.country=index%2?"Russians":"Americans";slot.team="-";
            slot.name="Navigation player "+std::to_string(index+1);
            const auto ownerKey=runtimeCanonicalHumanHouseOwner(runtimeSkirmishOwnerForSlot(index));
            state.skirmishCountryByOwner[ownerKey]=slot.country;
            state.skirmishTeamByOwner[ownerKey]=runtimeNormalizeSkirmishTeam(slot.team);
        }
        state.report=stock.report;state.world=stock.world;state.rules=stock.rules;
        state.art=stock.art;state.theater=stock.theater;state.mapFacts=stock.mapFacts;
        state.rulesLoaded=state.artLoaded=state.theaterLoaded=true;
        state.mapFactsLoaded=stock.mapFactsLoaded;state.terrainInfoByCell=stock.terrainInfoByCell;
        state.world.accessObjects().clear();state.runtimeSoundsEnabled=state.runtimeMusicEnabled=false;
        state.runtimeSuppressPlayerAudioFeedback=true;state.runtimePreferencePersistenceEnabled=false;
        state.visionEnabled=false;state.runtimeVoiceVolume=state.runtimeSoundVolume=0;
        state.economy.owner=runtimeSkirmishOwnerForSlot(0);state.economy.skirmishStartApplied=true;
        state.triggers.bootstrapComplete=true;state.tick=21;initializeRuntimeNavigation(state);
        const auto& cell=state.world.getTerrainCells().front();
        auto* actor=addRuntimeObjectInstanceForOwner(state,state.economy.owner,RuntimeBuildKind::Unit,"AMCV",{cell.x,cell.y});
        if(!expect(actor!=nullptr,"real load AMCV actor")) return false;
        actor->mission="Guard";actor->buildAnimTicksTotal=actor->buildAnimTicksRemaining=0;clearRuntimeRenderPosition(*actor);
        const int savedActorId=actor->instanceId;state.selectedObjectIds={savedActorId};(void)advanceRuntimeParticles(state);
        if(!expect(state.particlesInitialized&&state.particles.currentTick()==state.tick
                &&state.particles.fixedTickHz()==std::max(1,state.config.fixedTickHz),"real save particle authority")) return false;
        Clock clock;clock.value=1000;ClockBinding clockBinding(state,clock);
        std::string error;const auto savePath=pathString(directory/"local-navigation-load.ini");
        if(!expect(saveRuntimeState(state,savePath,error),"real isolated save: "+error)) return false;
        if(!expect(loadRuntimeState(state,savePath,error)&&state.tick==21&&state.uiClock==&clock,
                "ordinary scratch swap typed source: "+error)) return false;
        configure(state,0);initializeRuntimeOfflineGameplayRouting(state);
        applyRuntimePlayerSelection(state,{savedActorId},false,false);
        if(!expect(runtimePlayerLocalSelectable(state,savedActorId)
                &&state.selectedObjectIds==std::vector<int>{savedActorId},"exact saved actor real eligibility"))return false;
        state.playerLocalCommands.teams[0]={savedActorId};state.playerLocalCommands.views[0].reset();
        const int viewCode=runtimePlayerCurrentKeyCode(VK_F14),quickCode=runtimePlayerCurrentKeyCode(VK_F15),
            radarCode=runtimePlayerCurrentKeyCode(VK_F16),teamCode=runtimePlayerCurrentKeyCode(VK_F17);
        setBinding(state,"SetView1",viewCode);setBinding(state,"TypeSelect",quickCode);
        setBinding(state,"CenterOnRadarEvent",radarCode);setBinding(state,"TeamSelect_1",teamCode);
        const auto* loadedActor=findObjectById(state,savedActorId);
        if(!expect(loadedActor!=nullptr,"post-load exact actor"))return false;
        state.playerLastRadarCell=RuntimeMultiplayerGameplayCell{loadedActor->x,loadedActor->y};
        AuthorityWitness witness(state);RuntimePlayerDefeatNoticeSmokeWindow window(state,800,600);
        if(!expect(window.window!=nullptr,"post-load current-root native window"))return false;
        const auto before=authority(state,witness);const auto initialReads=clock.reads;
        const int cycleCode=runtimePlayerCurrentKeyCode(VK_F13);
        {RuntimeSnapshotActiveStateScope messageActive(state);SendMessageW(window.window,WM_KEYDOWN,VK_F13,0);}
        if(!expect(state.selectedObjectIds==std::vector<int>{savedActorId}&&!state.playerLocalCommands.cameraFollow
                &&clock.reads==initialReads+1,"native Cycle immediately after real load"))return false;
        clock.value=1100;
        {RuntimeSnapshotActiveStateScope messageActive(state);SendMessageW(window.window,WM_KEYDOWN,VK_F17,0);}
        if(!expect(state.playerLocalCommands.lastSelectionKey=='1'&&state.playerLocalCommands.lastSelectionAtMs==1100,
                "actual Team writes existing shared key/time"))return false;
        clock.value=1200;
        {RuntimeSnapshotActiveStateScope messageActive(state);SendMessageW(window.window,WM_KEYDOWN,VK_F14,0);}
        if(!expect(state.playerLocalCommands.views[0].has_value()&&state.interactionStatus=="view 1 saved"
                &&state.playerLocalCommands.lastSelectionKey=='1'&&state.playerLocalCommands.lastSelectionAtMs==1100,
                "native SetView keeps Team shared fields after real load"))return false;
        clock.value=1599;
        {RuntimeSnapshotActiveStateScope messageActive(state);SendMessageW(window.window,WM_KEYDOWN,VK_F15,0);}
        if(!expect(state.projectedObjectsCacheValid&&state.playerLocalCommands.lastSelectionKey==quickCode
                &&state.playerLocalCommands.lastSelectionAtMs==1599,
                "actual Quick consumes Team-shared timer with full-code distinction"))return false;
        clock.value=1600;
        {RuntimeSnapshotActiveStateScope messageActive(state);SendMessageW(window.window,WM_KEYDOWN,VK_F16,0);}
        if(!expect(state.playerLocalCommands.lastSelectionKey==quickCode&&state.playerLocalCommands.lastSelectionAtMs==1599
                &&state.tick==21&&state.uiClock==&clock&&authority(state,witness)==before
                &&witness.captures==0&&witness.bridge.pendingGameplayCommandCount()==0
                &&clock.reads==initialReads+5&&runtimePlayerCurrentKeyCode(VK_F13)==cycleCode
                &&runtimePlayerCurrentKeyCode(VK_F14)==viewCode&&runtimePlayerCurrentKeyCode(VK_F15)==quickCode
                &&runtimePlayerCurrentKeyCode(VK_F16)==radarCode&&runtimePlayerCurrentKeyCode(VK_F17)==teamCode,
                "post-load four consumers/current root/typed source/zero authority"))return false;
        trace<<"load savedActorId="<<savedActorId<<" tick=21 sourceSame=1 eligibleAfterLoad=1"
            <<" cycleAfterLoad=1 actualTeamSharedFields=1 viewAfterLoad=1 quickAfterLoad=1 radarAfterLoad=1"
            <<" state="<<hexText(localState(state,true))<<" query="<<hexText(projectionState(state))
            <<" authority="<<hexText(authority(state,witness))<<" captures=0 pending=0\n";
        sourceReads+=clock.reads;
    }
    trace<<"checks="<<checks<<" oldCases="<<oldCases<<" directCases="<<directCases<<" nativeCases="<<nativeCases
        <<" guardCases="<<guardCases<<" precedenceCases="<<precedenceCases<<" timingCases="<<timingCases
        <<" freshnessCases="<<freshnessCases<<'\n';
    trace.flush();if(!expect(static_cast<bool>(trace),"trace flush"))return false;
    output<<"Local-navigation integration passed: checks="<<checks<<" oldCases="<<oldCases
        <<" directCases="<<directCases<<" nativeCases="<<nativeCases<<" sourceReads="<<sourceReads
        <<" consumers=4 actions=15 repeats=2 sources=2 timingCases="<<timingCases
        <<" guards="<<guardCases<<" precedenceCases="<<precedenceCases<<" freshnessCases="<<freshnessCases
        <<" fullLocalState=1 queryEffectsEqual=1 actualProjectionNoPaint=1 cameraFresh=1 worldFresh=1"
        <<" realSpriteOrVoxel=1 ownedQueryLifetime=1 zeroAuthorityCapture=1 zeroPending=1"
        <<" worldQueueTickInputDelayUnchanged=1 actualLoadThenNavigation=1 actualTeamSharedFields=1 typedClockBorrow=1"
        <<" nativeModifierFullMatrix=not-run completeCaptureMatrix=not-covered fullMpSnapshot=not-run"
        <<" runningMpSession=not-run geometryAlgorithmMigration=not-covered originalOverflowDomain=not-covered\n";
    return true;
}
