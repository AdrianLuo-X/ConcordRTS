// Included after actual WindowProc, RuntimeTeamKeyUiHost, and ordinary load.
bool runRuntimeTeamKeySmokeProbe(RuntimeWindowState& stock, std::ostream& output) {
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
    std::size_t guardCases=0,precedenceCases=0,timingCases=0,sourceReads=0;
    const auto expect=[&](bool ok,const std::string& label){
        ++checks; if(!ok) output<<"Team-key integration failed: "<<label<<'\n'; return ok;
    };
    if(!expect(stock.report.success&&stock.world.getSummary().loaded
            &&!stock.world.getTerrainCells().empty()&&stock.rulesLoaded&&stock.artLoaded
            &&stock.theaterLoaded&&stock.rules.findUnitType("UnitType","AMCV"),
            "real startup resources/terrain/AMCV")) return false;
    const char* requested=std::getenv("CONCORD_TEAM_KEYS_OUTPUT");
    const std::filesystem::path directory=requested&&*requested
        ?std::filesystem::path(requested):std::filesystem::path("team-keys-smoke-output");
    std::error_code directoryError; std::filesystem::create_directories(directory,directoryError);
    if(!expect(!directoryError,"isolated output directory")) return false;
    std::ofstream trace(directory/"team-key-trace.txt",std::ios::out|std::ios::trunc);
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
            <<':'<<state.lastCommand.feedbackLatencyTicks;
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
    const auto make=[&](){
        auto storage=std::make_unique<RuntimeWindowState>(); auto& state=*storage;
        state.config=stock.config; state.config.gameMode="Battle"; state.config.gameModeRulesOverride.clear();
        state.config.multiplayerActive=false; state.config.playerCountry="Americans";
        state.config.playerSlots.clear(); state.config.keyboardReferenceLayout=true;
        state.config.keyBindings.clear(); state.config.showMainMenu=state.config.showSkirmishMenu=false;
        state.config.computerAiEnabled=false; state.config.fixedTickHz=30;
        state.config.configPath=pathString(directory/"private-preferences.ini");
        state.runtimeSoundsEnabled=state.runtimeMusicEnabled=false;
        state.runtimeSuppressPlayerAudioFeedback=true; state.runtimePreferencePersistenceEnabled=false;
        state.visionEnabled=false; state.economy.owner="Player"; state.economy.skirmishStartApplied=true;
        state.economy.credits=10000; state.economy.techLevel=10;
        state.triggers.bootstrapComplete=true; state.tick=7;
        IniFile ini;
        if(!ini.loadFromString("[VehicleTypes]\n0=TKUNIT\n1=TKMISSILE\n"
                "[TKUNIT]\nStrength=200\nSpeed=8\nSpeedType=Track\nMovementZone=Normal\n"
                "[TKMISSILE]\nStrength=200\nSpeed=8\nMissileSpawn=yes\n"
                "[BuildingTypes]\n0=TKHIDDEN\n[TKHIDDEN]\nStrength=200\nImage=NULL\n")
                ||!state.rules.loadFromIni(ini)) return std::unique_ptr<RuntimeWindowState>{};
        state.rulesLoaded=true;
        RuntimeHumanHouseAuthority house; house.owner="Player";
        house.canonicalOwner=runtimeCanonicalHumanHouseOwner("Player"); house.localFacade=true;
        state.humanHouseAuthorities.push_back(std::move(house));
        for(int y=0;y<32;++y) for(int x=0;x<32;++x){
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
            state.world.accessTerrainCells().push_back(cell);
            state.terrainInfoByCell.emplace(runtimeCellKey(x,y),RuntimeTerrainInfo{});
        }
        state.terrainByCell=buildRuntimeTerrainLookup(state.world);
        for(int id=1;id<=7;++id){
            WorldObjectInstance object; object.instanceId=id;
            object.kind=id==7?WorldObjectKind::Structure:WorldObjectKind::Unit;
            object.type=id==6?"TKMISSILE":id==7?"TKHIDDEN":"TKUNIT";
            object.mapId="TeamKeySmoke"+std::to_string(id); object.owner=id==3?"Enemy":"Player";
            object.x=5+id*3; object.y=8; object.cell=runtimeCellKey(object.x,object.y);
            object.health=200; object.alive=id!=4; object.poweredOperational=true;
            object.mission=id==5?"Limbo":"Guard"; state.world.accessObjects().push_back(std::move(object));
        }
        state.selectedObjectIds={2};
        for(auto& team:state.playerLocalCommands.teams) team={1,3,4,5,6,7,999};
        state.playerLocalCommands.lastSelectionKey='?'; state.playerLocalCommands.lastSelectionAtMs=777;
        state.playerLocalCommands.cameraFollow=true;
        state.camera.worldOffsetX=state.camera.worldOffsetY=123456789; state.camera.initialized=true;
        state.playerCommunication.selectedBeacon=RuntimeMultiplayerGameplayCell{4,6};
        state.interactionStatus="team fixture initial";
        initializeRuntimeOfflineGameplayRouting(state); return storage;
    };
    const auto setBinding=[](RuntimeWindowState& state,const std::string& name,int code){
        for(auto& binding:state.runtimeMenuKeyboardBindings)
            if(binding.first==name){binding.second=code;return;}
        state.runtimeMenuKeyboardBindings.emplace_back(name,code);
    };
    const auto configure=[&](RuntimeWindowState& state,int number){
        state.runtimeMenuKeyboardLoadAttempted=true; state.runtimeMenuKeyboardBindings.clear();
        state.runtimeMenuKeyboardBindings.emplace_back("Options",VK_ESCAPE);
        const char* actions[]={"TeamCreate","TeamSelect","TeamAddSelect","TeamCenter"};
        for(int action=0;action<4;++action)
            setBinding(state,std::string(actions[action])+"_"+std::to_string(number),
                runtimePlayerCurrentKeyCode(VK_F13+action));
    };
    const RECT client{0,0,800,600};
    // Frozen original loop. Oracle never calls the production controller/host.
    const auto oldLoop=[](RuntimeWindowState& state,const RECT& bounds,
        int code,bool repeated,std::uint64_t now){
        const auto matches=[&](const std::string& action,int fallback){
            return code==runtimeMenuKeyboardBindingCode(state,action,fallback);
        };
        const auto selectable=[&](int id){return runtimePlayerLocalSelectable(state,id);};
        auto& local=state.playerLocalCommands;
        for(int number=1;number<=10;++number){
            const int digit=number==10?'0':'0'+number;
            const std::string suffix="_"+std::to_string(number);
            const int index=number-1;
            if(matches("TeamCreate"+suffix,digit|runtimePlayerControlKey)){
                if(!repeated) local.teamCreate(index,state.selectedObjectIds,selectable);
                state.interactionStatus="team "+std::to_string(number)+" assigned"; return true;
            }
            const bool add=matches("TeamAddSelect"+suffix,digit|runtimePlayerShiftKey);
            const bool center=matches("TeamCenter"+suffix,digit|runtimePlayerAltKey);
            if(matches("TeamSelect"+suffix,digit)||add||center){
                if(repeated) return true;
                const auto selected=local.teamSelect(index,state.selectedObjectIds,add,selectable);
                const bool twice=local.lastSelectionKey==digit&&runtimeUiWithinDoubleSelection(now,local.lastSelectionAtMs);
                applyRuntimePlayerSelection(state,selected,false,false);
                state.playerCommunication.selectedBeacon.reset();
                if((center||twice)&&!selected.empty()){
                    long long x=0,y=0;
                    for(int id:selected) if(const auto* object=findObjectById(state,id)){
                        x+=object->x; y+=object->y;
                    }
                    centerRuntimeCameraOnCell(state,bounds,{static_cast<int>(x/selected.size()),
                        static_cast<int>(y/selected.size())});
                }
                local.lastSelectionKey=digit; local.lastSelectionAtMs=now; return true;
            }
        }
        return false;
    };
    const auto eligibilityFixture=[&](RuntimeWindowState& state){
        if(!expect(runtimePlayerLocalSelectable(state,1)&&runtimePlayerLocalSelectable(state,2),
                "actual owned actors selectable")) return false;
        for(int id:{3,4,5,6,7,999})
            if(!expect(!runtimePlayerLocalSelectable(state,id),
                "actual foreign/dead/limbo/missile/invisible/missing gate "+std::to_string(id))) return false;
        return true;
    };
    // mode: normal, observer, spectator, campaign SYSKEY exception.
    const auto triple=[&](const std::string& label,int number,int action,bool repeated,UINT message,
        std::uint64_t now,std::uint64_t last,bool sameLastKey,int mode,bool expectedCenter){
        auto oldStorage=make(),directStorage=make(),nativeStorage=make();
        if(!expect(oldStorage&&directStorage&&nativeStorage,label+" worlds")) return false;
        auto& old=*oldStorage; auto& direct=*directStorage; auto& native=*nativeStorage;
        if(!eligibilityFixture(old)||!eligibilityFixture(direct)||!eligibilityFixture(native)) return false;
        for(auto* state:{&old,&direct,&native}){
            configure(*state,number); state->playerLocalCommands.lastSelectionAtMs=last;
            if(sameLastKey) state->playerLocalCommands.lastSelectionKey=number==10?'0':'0'+number;
            if(mode==1) state->config.playerCountry="Observer";
            if(mode==2) state->multiplayerLocalSpectator=true;
            if(mode==3){state->campaignMode=true;state->triggers.campaignInputLocked=true;}
        }
        Clock oldClock,directClock,nativeClock; oldClock.value=directClock.value=nativeClock.value=now;
        ClockBinding oldBinding(old,oldClock),directBinding(direct,directClock),nativeBinding(native,nativeClock);
        AuthorityWitness oldWitness(old),directWitness(direct),nativeWitness(native);
        const auto beforeOldAuthority=authority(old,oldWitness),beforeDirectAuthority=authority(direct,directWitness),
            beforeNativeAuthority=authority(native,nativeWitness);
        const WPARAM raw=VK_F13+action; const int observedCode=runtimePlayerCurrentKeyCode(raw);
        const LPARAM flags=repeated?(static_cast<LPARAM>(1)<<30):0;
        RuntimePlayerDefeatNoticeSmokeWindow window(native,800,600);
        if(!expect(window.window!=nullptr,label+" hidden real window")) return false;
        bool oldHandled=false,directHandled=false;
        {RuntimeSnapshotActiveStateScope active(old);rejectRuntimeObserverSelection(old);
         oldHandled=oldLoop(old,client,observedCode,repeated,runtimeUiClockNow(old));}
        {RuntimeSnapshotActiveStateScope active(direct);rejectRuntimeObserverSelection(direct);
         RuntimeTeamKeyUiHost host(direct,client);
         directHandled=handleRuntimeTeamKey({observedCode,repeated,runtimeUiClockNow(direct)},
             direct.playerLocalCommands,direct.selectedObjectIds,host);}
        LRESULT nativeResult=0;
        {RuntimeSnapshotActiveStateScope active(native);nativeResult=SendMessageW(window.window,message,raw,flags);}
        if(!expect(runtimePlayerCurrentKeyCode(raw)==observedCode,label+" stable observed physical modifiers")) return false;
        if(!expect(oldHandled&&directHandled&&nativeResult==0
                &&localState(old,true)==localState(direct,true)&&localState(old,true)==localState(native,true),
                label+" complete local/teams/views/time/camera/beacon/status old-direct-native")) return false;
        if(!expect(authority(old,oldWitness)==beforeOldAuthority
                &&authority(direct,directWitness)==beforeDirectAuthority
                &&authority(native,nativeWitness)==beforeNativeAuthority
                &&beforeOldAuthority==beforeDirectAuthority&&beforeOldAuthority==beforeNativeAuthority,
                label+" unchanged world/queue/tick/lastCommand/capture/pending")) return false;
        if(!expect(oldWitness.captures==0&&directWitness.captures==0&&nativeWitness.captures==0
                &&oldWitness.bridge.pendingGameplayCommandCount()==0
                &&directWitness.bridge.pendingGameplayCommandCount()==0
                &&nativeWitness.bridge.pendingGameplayCommandCount()==0,label+" zero actual capture/pending")) return false;
        if(!expect(oldClock.reads==1&&directClock.reads==1&&nativeClock.reads==1,
                label+" caller samples once; controller does not sample")) return false;
        const bool allowed=mode!=1&&mode!=2; const auto index=static_cast<std::size_t>(number-1);
        const int digit=number==10?'0':'0'+number;
        if(action==0){
            if(!expect(old.interactionStatus=="team "+std::to_string(number)+" assigned"
                    &&old.playerLocalCommands.lastSelectionAtMs==last
                    &&old.playerCommunication.selectedBeacon.has_value(),label+" create status/timer/beacon")) return false;
            if(!repeated&&!expect(old.playerLocalCommands.teams[index]==std::vector<int>{2},
                    label+" create actual eligible selection")) return false;
        }else if(!repeated){
            const auto expectedSelection=!allowed?std::vector<int>{}
                :action==2?std::vector<int>{1,2}:std::vector<int>{1};
            if(!expect(old.selectedObjectIds==expectedSelection&&old.playerLocalCommands.lastSelectionKey==digit
                    &&old.playerLocalCommands.lastSelectionAtMs==now&&!old.playerCommunication.selectedBeacon.has_value(),
                    label+" filtered result/apply/beacon/final timer")) return false;
        }else if(!expect(old.playerLocalCommands.lastSelectionAtMs==last
                &&old.playerCommunication.selectedBeacon.has_value(),label+" repeat has no timer/beacon effects")) return false;
        if(!expect(old.playerLocalCommands.cameraFollow==!expectedCenter,label+" explicit/double camera policy")) return false;
        const auto print=[&](const char* path,const RuntimeWindowState& state,const AuthorityWitness& witness){
            trace<<"team path="<<path<<" label="<<label<<" number="<<number<<" action="<<action
                <<" repeated="<<repeated<<" source="<<message<<" raw="<<raw<<" code="<<observedCode<<" now="<<now
                <<" state="<<hexText(localState(state,true))<<" authority="<<hexText(authority(state,witness))
                <<" captures="<<witness.captures<<" pending="<<witness.bridge.pendingGameplayCommandCount()<<'\n';
        };
        print("old",old,oldWitness);print("direct",direct,directWitness);print("native",native,nativeWitness);
        sourceReads+=oldClock.reads+directClock.reads+nativeClock.reads; ++oldCases;++directCases;++nativeCases;
        return true;
    };
    for(int number=1;number<=10;++number) for(int action=0;action<4;++action)
        for(bool repeated:{false,true}) for(UINT message:{WM_KEYDOWN,WM_SYSKEYDOWN})
            if(!triple("matrix",number,action,repeated,message,1000,777,false,0,!repeated&&action==3)) return false;
    struct TimingCase{std::uint64_t now,last;bool centers;};
    constexpr auto maximum=std::numeric_limits<std::uint64_t>::max();
    const TimingCase timing[]={{2499,2000,true},{2500,2000,true},{2501,2000,false},
        {1999,2000,false},{5,maximum-10,true},{0,0,true}};
    for(const auto& item:timing){
        if(!triple("timing",1,1,false,WM_KEYDOWN,item.now,item.last,true,0,item.centers)) return false;
        ++timingCases;
    }
    for(int mode:{1,2}) for(UINT message:{WM_KEYDOWN,WM_SYSKEYDOWN})
        if(!triple(mode==1?"observer":"spectator",1,1,false,message,1000,777,false,mode,false)) return false;
    if(!triple("campaign-system-exception",1,1,false,WM_SYSKEYDOWN,1000,777,false,3,false)) return false;
    for(int mode=0;mode<6;++mode){
        auto storage=make();if(!expect(static_cast<bool>(storage),"guard world")) return false;
        auto& state=*storage;configure(state,1);
        Clock clock;clock.value=1000;ClockBinding clockBinding(state,clock);AuthorityWitness witness(state);
        if(mode==0) state.runtimeMenuOpen=true;
        if(mode==1) state.playerCommunication.chatOpen=true;
        if(mode==2) state.playerCommunication.diplomacyOpen=true;
        if(mode==3) state.skirmishMatchFinished=true;
        if(mode==4){state.campaignMode=true;state.triggers.campaignInputLocked=true;}
        if(mode==5){state.runtimeMenuOpen=true;state.runtimeMenuPage=RuntimeMenuPage::Keyboard;state.runtimeMenuKeyCaptureIndex=0;}
        RuntimePlayerDefeatNoticeSmokeWindow window(state,800,600);
        if(!expect(window.window!=nullptr,"guard hidden window")) return false;
        const auto beforeLocal=localState(state,false),beforeAuthority=authority(state,witness);
        {RuntimeSnapshotActiveStateScope active(state);SendMessageW(window.window,WM_KEYDOWN,VK_F14,0);}
        if(!expect(localState(state,false)==beforeLocal&&authority(state,witness)==beforeAuthority
                &&witness.captures==0&&witness.bridge.pendingGameplayCommandCount()==0&&clock.reads==0,
                "native outer guard before Team "+std::to_string(mode))) return false;
        trace<<"guard mode="<<mode<<" teamStateUnchanged=1 authorityUnchanged=1 captures=0 pending=0 clockReads=0\n";
        ++guardCases;
    }
    for(int mode=0;mode<5;++mode){
        auto storage=make();if(!expect(static_cast<bool>(storage),"precedence world")) return false;
        auto& state=*storage;configure(state,1);
        Clock clock;clock.value=1000;ClockBinding clockBinding(state,clock);AuthorityWitness witness(state);
        const WPARAM raw=mode==0?VK_F14:VK_NUMPAD1;
        const int code=runtimePlayerCurrentKeyCode(raw),normalized=(code&~0xff)|'1';
        setBinding(state,"TeamSelect_1",mode==0?code:normalized);
        if(mode==0||mode==1) setBinding(state,"ShowCoordinates",code);
        if(mode==2) setBinding(state,"PreviousObject",normalized);
        if(mode==3){setBinding(state,"NextObject",normalized);state.selectedObjectIds={1};}
        const auto initialTeam=state.playerLocalCommands.teams[0]; const auto beforeAuthority=authority(state,witness);
        RuntimePlayerDefeatNoticeSmokeWindow window(state,800,600);
        if(!expect(window.window!=nullptr,"precedence hidden window")) return false;
        {RuntimeSnapshotActiveStateScope active(state);SendMessageW(window.window,WM_KEYDOWN,raw,0);}
        if(!expect(runtimePlayerCurrentKeyCode(raw)==code&&authority(state,witness)==beforeAuthority
                &&witness.captures==0&&witness.bridge.pendingGameplayCommandCount()==0&&clock.reads==1,
                "precedence native sample/zero authority")) return false;
        if(mode<=1){
            if(!expect(state.playerLocalCommands.showCoordinates&&state.playerLocalCommands.lastSelectionKey=='?'
                    &&state.playerLocalCommands.lastSelectionAtMs==777&&state.playerLocalCommands.teams[0]==initialTeam
                    &&state.selectedObjectIds==std::vector<int>{2}&&state.playerCommunication.selectedBeacon.has_value(),
                    "reference before raw keypad normalization and Team")) return false;
        }else if(mode<=3){
            if(!expect(state.selectedObjectIds==std::vector<int>{mode==2?1:2}
                    &&state.playerLocalCommands.teams[0]==initialTeam&&state.playerLocalCommands.lastSelectionKey=='?'
                    &&state.playerLocalCommands.lastSelectionAtMs==777&&!state.playerLocalCommands.cameraFollow
                    &&!state.playerCommunication.selectedBeacon.has_value(),
                    "Previous/Next normalized-key priority before Team")) return false;
        }else if(!expect(state.selectedObjectIds==std::vector<int>{1}
                &&state.playerLocalCommands.teams[0]==std::vector<int>{1}
                &&state.playerLocalCommands.lastSelectionKey=='1'&&state.playerLocalCommands.lastSelectionAtMs==1000
                &&!state.playerCommunication.selectedBeacon.has_value(),"keypad reaches same Team controller")) return false;
        trace<<"precedence mode="<<mode<<" raw="<<raw<<" code="<<code<<" normalized="<<normalized
            <<" state="<<hexText(localState(state,true))<<" authority="<<hexText(authority(state,witness))
            <<" captures=0 pending=0\n";
        sourceReads+=clock.reads;++precedenceCases;
    }
    {
        auto storage=make();if(!expect(static_cast<bool>(storage),"observer reference world")) return false;
        auto& state=*storage;configure(state,1);state.config.playerCountry="Observer";
        state.leftPointer.active=state.leftPointer.moved=true;
        const int code=runtimePlayerCurrentKeyCode(VK_F18);setBinding(state,"ShowCoordinates",code);
        Clock clock;clock.value=1000;ClockBinding clockBinding(state,clock);AuthorityWitness witness(state);
        RuntimePlayerDefeatNoticeSmokeWindow window(state,800,600);
        if(!expect(window.window!=nullptr,"observer reference window")) return false;
        const auto beforeAuthority=authority(state,witness);
        {RuntimeSnapshotActiveStateScope active(state);SendMessageW(window.window,WM_KEYDOWN,VK_F18,0);}
        if(!expect(runtimePlayerCurrentKeyCode(VK_F18)==code&&state.playerLocalCommands.showCoordinates
                &&state.selectedObjectIds.empty()&&!state.leftPointer.active&&!state.leftPointer.moved
                &&authority(state,witness)==beforeAuthority&&clock.reads==1,
                "observer rejects selection but reaches local reference")) return false;
        trace<<"observer-reference selectionEmpty=1 dragEmpty=1 referenceReached=1 captures=0 pending=0\n";
        sourceReads+=clock.reads;
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
            slot.name="Team key player "+std::to_string(index+1);
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
        std::string error;const auto savePath=pathString(directory/"team-key-load.ini");
        if(!expect(saveRuntimeState(state,savePath,error),"real isolated save: "+error)) return false;
        if(!expect(loadRuntimeState(state,savePath,error)&&state.tick==21&&state.uiClock==&clock,
                "ordinary scratch swap typed source: "+error)) return false;
        configure(state,1);initializeRuntimeOfflineGameplayRouting(state);
        applyRuntimePlayerSelection(state,{savedActorId},false,false);
        if(!expect(runtimePlayerLocalSelectable(state,savedActorId)
                &&state.selectedObjectIds==std::vector<int>{savedActorId},"exact saved actor real eligibility")) return false;
        AuthorityWitness witness(state);RuntimePlayerDefeatNoticeSmokeWindow window(state,800,600);
        if(!expect(window.window!=nullptr,"post-load current-root native window")) return false;
        const auto beforeAuthority=authority(state,witness);
        const int createCode=runtimePlayerCurrentKeyCode(VK_F13),selectCode=runtimePlayerCurrentKeyCode(VK_F14),
            typeCode=runtimePlayerCurrentKeyCode(VK_F17);
        const auto initialReads=clock.reads;
        {RuntimeSnapshotActiveStateScope messageActive(state);SendMessageW(window.window,WM_KEYDOWN,VK_F13,0);}
        if(!expect(state.playerLocalCommands.teams[0]==std::vector<int>{savedActorId}
                &&clock.reads==initialReads+1,"native TeamCreate after real load")) return false;
        setBinding(state,"TypeSelect",typeCode);clock.value=1100;
        {RuntimeSnapshotActiveStateScope messageActive(state);SendMessageW(window.window,WM_KEYDOWN,VK_F17,0);}
        if(!expect(state.playerLocalCommands.lastSelectionKey==typeCode&&state.playerLocalCommands.lastSelectionAtMs==1100,
                "actual quickSelect shared key/time")) return false;
        state.playerLocalCommands.cameraFollow=true;clock.value=1200;
        {RuntimeSnapshotActiveStateScope messageActive(state);SendMessageW(window.window,WM_KEYDOWN,VK_F14,0);}
        if(!expect(state.selectedObjectIds==std::vector<int>{savedActorId}
                &&state.playerLocalCommands.lastSelectionKey=='1'&&state.playerLocalCommands.lastSelectionAtMs==1200
                &&state.playerLocalCommands.cameraFollow,"Team after quickSelect digit/full-code distinction")) return false;
        clock.value=1699;
        {RuntimeSnapshotActiveStateScope messageActive(state);SendMessageW(window.window,WM_KEYDOWN,VK_F14,0);}
        if(!expect(!state.playerLocalCommands.cameraFollow&&state.playerLocalCommands.lastSelectionAtMs==1699
                &&state.tick==21&&authority(state,witness)==beforeAuthority&&witness.captures==0
                &&witness.bridge.pendingGameplayCommandCount()==0
                &&runtimePlayerCurrentKeyCode(VK_F13)==createCode&&runtimePlayerCurrentKeyCode(VK_F14)==selectCode
                &&runtimePlayerCurrentKeyCode(VK_F17)==typeCode,"post-load double-select source and zero authority")) return false;
        trace<<"load savedActorId="<<savedActorId<<" tick=21 sourceSame=1 eligibleAfterLoad=1"
            <<" createAfterLoad=1 actualQuickSelectSharedFields=1 selectAfterLoad=1 doubleAfterLoad=1"
            <<" state="<<hexText(localState(state,true))<<" authority="<<hexText(authority(state,witness))
            <<" captures=0 pending=0\n";
        sourceReads+=clock.reads;
    }
    trace<<"checks="<<checks<<" oldCases="<<oldCases<<" directCases="<<directCases<<" nativeCases="<<nativeCases
        <<" guardCases="<<guardCases<<" precedenceCases="<<precedenceCases<<" timingCases="<<timingCases<<'\n';
    trace.flush();if(!expect(static_cast<bool>(trace),"trace flush")) return false;
    output<<"Team-key integration passed: checks="<<checks<<" oldCases="<<oldCases<<" directCases="<<directCases
        <<" nativeCases="<<nativeCases<<" sourceReads="<<sourceReads
        <<" groups=10 actions=4 repeats=2 sources=2 timingCases="<<timingCases
        <<" guards="<<guardCases<<" precedenceCases="<<precedenceCases
        <<" observerReference=1 campaignSyskeyException=1 fullLocalState=1"
        <<" zeroAuthorityCapture=1 zeroPending=1 worldQueueTickUnchanged=1"
        <<" actualLoadThenTeam=1 actualQuickSelectSharedFields=1 typedClockBorrow=1"
        <<" nativeModifierFullMatrix=not-run completeCaptureMatrix=not-covered"
        <<" fullMpSnapshot=not-run runningMpSession=not-run originalOverflowDomain=not-covered\n";
    return true;
}
