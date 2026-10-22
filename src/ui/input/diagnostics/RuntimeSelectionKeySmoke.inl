// Included after the actual WindowProc, dispatchers, and ordinary load transaction.
bool runRuntimeSelectionKeySmokeProbe(RuntimeWindowState& stock, std::ostream& output) {
    std::size_t checks=0, nativeCases=0, directCases=0;
    const auto expect=[&](bool ok,const std::string& why) {
        ++checks; if(!ok) std::cerr<<"Selection-key integration failed: "<<why<<'\n'; return ok;
    };
    const char* requested=std::getenv("CONCORD_SELECTION_KEYS_OUTPUT");
    const std::filesystem::path directory=requested&&*requested?std::filesystem::path(requested):std::filesystem::path("selection-keys-smoke-output");
    std::error_code directoryError; std::filesystem::create_directories(directory,directoryError);
    if(!expect(!directoryError,"isolated output directory")) return false;
    std::ofstream trace(directory/"selection-key-trace.txt",std::ios::out|std::ios::trunc);
    if(!expect(static_cast<bool>(trace),"trace file")) return false;
    struct Capture {
        int tick=0;
        std::vector<std::uint8_t> payload;
        bool applied=false;
        bool operator==(const Capture&) const = default;
    };
    const auto make=[&]() {
        auto storage=std::make_unique<RuntimeWindowState>(); auto& state=*storage;
        state.config=stock.config; state.config.gameMode="Battle"; state.config.gameModeRulesOverride.clear();
        state.config.multiplayerActive=false; state.config.playerCountry="Americans"; state.config.playerSlots.clear();
        state.config.keyboardReferenceLayout=true; state.config.keyBindings.clear();
        state.config.showMainMenu=state.config.showSkirmishMenu=false; state.config.computerAiEnabled=false;
        state.config.configPath=pathString(directory/"private-preferences.ini"); state.config.fixedTickHz=30;
        state.runtimeSoundsEnabled=false; state.runtimeMusicEnabled=false;
        state.runtimeSuppressPlayerAudioFeedback=true; state.runtimePreferencePersistenceEnabled=false;
        state.visionEnabled=false; state.economy.owner="Player"; state.economy.skirmishStartApplied=true;
        state.economy.credits=10000; state.economy.techLevel=10; state.triggers.bootstrapComplete=true; state.tick=7;
        IniFile ini;
        if(!ini.loadFromString("[VehicleTypes]\n0=SKUNIT\n[SKUNIT]\nStrength=200\nSpeed=8\nSpeedType=Track\nMovementZone=Normal\nIsSimpleDeployer=yes\n" )
            ||!state.rules.loadFromIni(ini)) return std::unique_ptr<RuntimeWindowState>{};
        state.rulesLoaded=true;
        RuntimeHumanHouseAuthority authority; authority.owner="Player"; authority.canonicalOwner=runtimeCanonicalHumanHouseOwner("Player"); authority.localFacade=true;
        state.humanHouseAuthorities.push_back(std::move(authority));
        for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
            state.world.accessTerrainCells().push_back(cell); state.terrainInfoByCell.emplace(runtimeCellKey(x,y),RuntimeTerrainInfo{});
        }
        state.terrainByCell=buildRuntimeTerrainLookup(state.world);
        for(int id=1;id<=3;++id) {
            WorldObjectInstance object; object.instanceId=id; object.kind=WorldObjectKind::Unit; object.type="SKUNIT";
            object.mapId="SelectionKeySmoke"+std::to_string(id); object.owner=id==3?"Enemy":"Player";
            object.x=5+id*3; object.y=8; object.cell=runtimeCellKey(object.x,object.y); object.health=200;
            object.alive=true; object.poweredOperational=true; object.mission="Move";
            state.world.accessObjects().push_back(std::move(object));
            RuntimeObjectBehavior behavior; behavior.kind=RuntimeBehaviorKind::Move; behavior.state=RuntimeBehaviorState::Moving;
            behavior.moveTarget={24,24}; state.behaviors.emplace(id,std::move(behavior));
        }
        state.selectedObjectIds={2,1};
        initializeRuntimeOfflineGameplayRouting(state);
        return storage;
    };
    const auto configure=[&](RuntimeWindowState& state) {
        state.runtimeMenuKeyboardLoadAttempted=true; state.runtimeMenuKeyboardBindings.clear();
        state.runtimeMenuKeyboardBindings.emplace_back("Options",VK_ESCAPE);
        const char* ids[]={"StopObject","GuardObject","ScatterObject","DeployObject"};
        for(int index=0;index<4;++index) state.runtimeMenuKeyboardBindings.emplace_back(ids[index],runtimePlayerCurrentKeyCode(VK_F13+index));
    };
    const auto installCapture=[&](RuntimeWindowState& state,std::vector<Capture>& captures) {
        state.multiplayerIntentRouter.setCaptureCallback([&state,&captures](const RuntimeMultiplayerGameplayCommand& command,std::string& error) {
            Capture item; item.tick=state.tick;
            if(!encodeRuntimeMultiplayerGameplayCommand(command,item.payload,error)) return false;
            item.applied=dispatchRuntimeOfflineGameplayCommand(state,command,error);
            captures.push_back(std::move(item)); return captures.back().applied;
        });
    };
    const auto snapshot=[](const RuntimeWindowState& state) {
        std::ostringstream text; text<<state.tick<<':'<<state.world.deterministicSimulationStateHash()<<':'<<state.playerCommands.serialize()<<':';
        for(const auto& [id,behavior]:state.behaviors) text<<id<<','<<static_cast<int>(behavior.kind)<<','<<static_cast<int>(behavior.state)
            <<','<<behavior.moveTarget.x<<','<<behavior.moveTarget.y<<';';
        for(int id:state.selectedObjectIds) text<<"selected="<<id<<';';
        return text.str();
    };
    // Frozen pre-migration four-key block, including every native resampling site.
    // This oracle is diagnostic only; gameplay uses the production controller.
    const auto oldSelectionBlock=[](RuntimeWindowState& state,WPARAM key,LPARAM flags) {
        const int stopKey=runtimeMenuKeyboardBindingCode(state,"StopObject",'S');
        const int guardKey=runtimeMenuKeyboardBindingCode(state,"GuardObject",'G');
        const int scatterKey=runtimeMenuKeyboardBindingCode(state,"ScatterObject",'X');
        const int deployKey=runtimeMenuKeyboardBindingCode(state,"DeployObject",'D');
        const int selectionCode=runtimePlayerCurrentKeyCode(key);
        if((flags&(static_cast<LPARAM>(1)<<30))!=0
            &&(selectionCode==stopKey||selectionCode==guardKey||selectionCode==scatterKey||selectionCode==deployKey))
            return RuntimeSelectionKeyResult::RepeatedConsumed;
        if(runtimePlayerCurrentKeyCode(key)==stopKey) dispatchRuntimeSelectionCommandFromUi(state,RuntimeMultiplayerGameplayCommandType::Stop);
        else if(runtimePlayerCurrentKeyCode(key)==guardKey) dispatchRuntimeSelectionCommandFromUi(state,RuntimeMultiplayerGameplayCommandType::Guard);
        else if(runtimePlayerCurrentKeyCode(key)==scatterKey) dispatchRuntimeSelectionCommandFromUi(state,RuntimeMultiplayerGameplayCommandType::Scatter);
        else if(runtimePlayerCurrentKeyCode(key)==deployKey) dispatchRuntimeSecondaryActionForSelectionFromUi(state);
        else return RuntimeSelectionKeyResult::Unhandled;
        return RuntimeSelectionKeyResult::DispatchedConsumed;
    };
    struct DirectHost final : RuntimeSelectionKeyHost {
        RuntimeWindowState& state; std::size_t samples=0;
        explicit DirectHost(RuntimeWindowState& value):state(value){}
        std::int32_t binding(RuntimeSelectionKeyBinding id,std::int32_t fallback) override {
            const char* names[]={"StopObject","GuardObject","ScatterObject","DeployObject"};
            return runtimeMenuKeyboardBindingCode(state,names[static_cast<unsigned>(id)],fallback);
        }
        std::int32_t sampleKeyCode(const RuntimeSelectionKeyInput& input) override {
            ++samples; return runtimePlayerCurrentKeyCode(static_cast<WPARAM>(input.rawKey));
        }
        void dispatch(RuntimeSelectionKeyAction action) override {
            switch(action) {
            case RuntimeSelectionKeyAction::Stop: (void)dispatchRuntimeSelectionCommandFromUi(state,RuntimeMultiplayerGameplayCommandType::Stop); break;
            case RuntimeSelectionKeyAction::Guard: (void)dispatchRuntimeSelectionCommandFromUi(state,RuntimeMultiplayerGameplayCommandType::Guard); break;
            case RuntimeSelectionKeyAction::Scatter: (void)dispatchRuntimeSelectionCommandFromUi(state,RuntimeMultiplayerGameplayCommandType::Scatter); break;
            case RuntimeSelectionKeyAction::Deploy: (void)dispatchRuntimeSecondaryActionForSelectionFromUi(state); break;
            }
        }
    };
    const auto printCapture=[&](const char* path,int action,UINT message,bool repeated,const std::vector<Capture>& captures) {
        trace<<path<<" action="<<action<<" message="<<message<<" repeated="<<repeated<<" captures="<<captures.size();
        for(const auto& item:captures) {
            trace<<" tick="<<item.tick<<" applied="<<item.applied<<" payload=";
            for(auto byte:item.payload) trace<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(byte);
            trace<<std::dec;
        }
        trace<<'\n';
    };
    for(int action=0;action<4;++action) for(bool repeated:{false,true}) for(UINT message:{WM_KEYDOWN,WM_SYSKEYDOWN}) {
        auto oldStorage=make(),directStorage=make(),nativeStorage=make();
        if(!expect(oldStorage&&directStorage&&nativeStorage,"actual isolated world construction")) return false;
        auto& old=*oldStorage; auto& direct=*directStorage; auto& native=*nativeStorage;
        configure(old); configure(direct); configure(native);
        std::vector<Capture> oldCaptures,directCaptures,nativeCaptures;
        installCapture(old,oldCaptures); installCapture(direct,directCaptures); installCapture(native,nativeCaptures);
        const WPARAM raw=VK_F13+action; const int beforeCode=runtimePlayerCurrentKeyCode(raw);
        trace<<"native observation raw="<<raw<<" code="<<beforeCode<<" source="<<message<<'\n';
        const LPARAM flags=repeated?(static_cast<LPARAM>(1)<<30):0;
        RuntimeSelectionKeyResult oldResult;
        { RuntimeSnapshotActiveStateScope active(old); oldResult=oldSelectionBlock(old,raw,flags); }
        DirectHost host(direct);
        const RuntimeSelectionKeyInput input{static_cast<std::int32_t>(raw),repeated,message==WM_SYSKEYDOWN?RuntimeKeyDownSource::SystemKeyDown:RuntimeKeyDownSource::KeyDown};
        RuntimeSelectionKeyResult directResult;
        { RuntimeSnapshotActiveStateScope active(direct); directResult=handleRuntimeSelectionKey(input,host); }
        RuntimePlayerDefeatNoticeSmokeWindow window(native,800,600);
        if(!expect(window.window!=nullptr,"hidden real WindowProc window")) return false;
        { RuntimeSnapshotActiveStateScope active(native); SendMessageW(window.window,message,raw,flags); }
        if(!expect(runtimePlayerCurrentKeyCode(raw)==beforeCode,"native modifiers remained stable during paired trace")) return false;
        printCapture("old",action,message,repeated,oldCaptures); printCapture("direct",action,message,repeated,directCaptures); printCapture("native",action,message,repeated,nativeCaptures);
        if(!expect(oldResult==directResult&&oldCaptures==directCaptures&&oldCaptures==nativeCaptures,
                "same canonical command payload/applied result/original tick through old/direct/Win32 paths")) return false;
        if(!expect(snapshot(old)==snapshot(direct)&&snapshot(old)==snapshot(native),"actual world hash/behavior/order/selection equality")) return false;
        if(!expect(host.samples==(repeated?1:static_cast<std::size_t>(action+2)),"actual consumer original sample count")) return false;
        if(!expect(repeated?oldCaptures.empty():oldCaptures.size()==1&&oldCaptures.front().tick==7&&oldCaptures.front().applied,
                "repeat suppresses submit; nonrepeat dispatch applies synchronously at tick seven")) return false;
        if(action==3&&!repeated&&!expect(runtimeMissionIsDeploying(*findObjectById(native,1))||runtimeObjectHasDeployedState(native,*findObjectById(native,1)),
                "actual Deploy changed original unit deployment state")) return false;
        ++nativeCases; ++directCases;
    }
    // Native guards remain in WindowProc. Each starts before the migrated block.
    for(int mode=0;mode<6;++mode) {
        auto storage=make(); if(!expect(static_cast<bool>(storage),"guard world")) return false; auto& state=*storage;
        configure(state); std::vector<Capture> captures; installCapture(state,captures);
        RuntimePlayerDefeatNoticeSmokeWindow window(state,800,600); if(!expect(window.window!=nullptr,"guard window")) return false;
        if(mode==0) state.runtimeMenuOpen=true;
        if(mode==1) state.playerCommunication.chatOpen=true;
        if(mode==2) state.playerCommunication.diplomacyOpen=true;
        if(mode==3) state.skirmishMatchFinished=true;
        if(mode==4) { state.campaignMode=true; state.triggers.campaignInputLocked=true; }
        if(mode==5) { state.runtimeMenuOpen=true; state.runtimeMenuPage=RuntimeMenuPage::Keyboard; state.runtimeMenuKeyCaptureIndex=0; }
        { RuntimeSnapshotActiveStateScope active(state); SendMessageW(window.window,WM_KEYDOWN,VK_F13,0); }
        if(!expect(captures.empty()&&state.tick==7,"native outer guard bypasses selection submit mode "+std::to_string(mode))) return false;
        trace<<"guard mode="<<mode<<" captures="<<captures.size()<<" tick="<<state.tick<<'\n';
    }
    {
        auto storage=make(); auto& state=*storage; configure(state); std::vector<Capture> captures; installCapture(state,captures);
        state.campaignMode=true; state.triggers.campaignInputLocked=true;
        RuntimePlayerDefeatNoticeSmokeWindow window(state,800,600);
        if(!expect(window.window!=nullptr,"campaign system-key window")) return false;
        { RuntimeSnapshotActiveStateScope active(state); SendMessageW(window.window,WM_SYSKEYDOWN,VK_F13,0); }
        if(!expect(captures.size()==1&&captures.front().tick==7,"campaign lock retains original system-key exception")) return false;
    }
    {
        auto storage=make(); auto& state=*storage; configure(state); std::vector<Capture> captures; installCapture(state,captures);
        state.config.playerCountry="Observer"; state.selectedObjectIds={3}; state.leftPointer.active=state.leftPointer.moved=true;
        RuntimePlayerDefeatNoticeSmokeWindow window(state,800,600);
        if(!expect(window.window!=nullptr,"observer native window")) return false;
        { RuntimeSnapshotActiveStateScope active(state); SendMessageW(window.window,WM_KEYDOWN,VK_F13,0); }
        if(!expect(state.selectedObjectIds.empty()&&!state.leftPointer.active&&!state.leftPointer.moved&&captures.empty(),"observer clears selection and real router rejects command")) return false;
        const bool before=state.playerLocalCommands.showCoordinates;
        const auto code=runtimePlayerCurrentKeyCode('O'); state.runtimeMenuKeyboardBindings.emplace_back("ShowCoordinates",code);
        { RuntimeSnapshotActiveStateScope active(state); SendMessageW(window.window,WM_KEYDOWN,'O',0); }
        if(!expect(state.playerLocalCommands.showCoordinates!=before&&captures.empty(),"observer still reaches original local reference function")) return false;
    }
    {
        auto storage=make(),oldStorage=make();
        if(!expect(storage&&oldStorage,"injected enemy old/native worlds")) return false;
        auto& state=*storage; auto& old=*oldStorage; configure(state); configure(old);
        std::vector<Capture> captures,oldCaptures; installCapture(state,captures); installCapture(old,oldCaptures);
        const auto queueEnemyMove=[](RuntimeWindowState& target) {
            RuntimeMultiplayerGameplayTargetOrderCommand queued;
            queued.order=RuntimeMultiplayerGameplayTargetOrderKind::Move; queued.objectIds={3};
            queued.target=RuntimeMultiplayerGameplayCellTarget{{24,24}};
            return target.playerCommands.append("Enemy",queued);
        };
        if(!expect(queueEnemyMove(state)&&queueEnemyMove(old),"actual enemy queued order setup")) return false;
        state.selectedObjectIds={3}; old.selectedObjectIds={3};
        const auto injectedBefore=snapshot(state), injectedQueueBefore=state.playerCommands.serialize();
        const int initialCode=runtimePlayerCurrentKeyCode(VK_F13);
        RuntimeSelectionKeyResult oldResult;
        { RuntimeSnapshotActiveStateScope active(old); oldResult=oldSelectionBlock(old,VK_F13,0); }
        RuntimePlayerDefeatNoticeSmokeWindow window(state,800,600);
        if(!expect(window.window!=nullptr,"enemy native window")) return false;
        { RuntimeSnapshotActiveStateScope active(state); SendMessageW(window.window,WM_KEYDOWN,VK_F13,0); }
        printCapture("injected-enemy-old",0,WM_KEYDOWN,false,oldCaptures);
        printCapture("injected-enemy-native",0,WM_KEYDOWN,false,captures);
        trace<<"injected-enemy before="<<injectedBefore<<" after-old="<<snapshot(old)<<" after-native="<<snapshot(state)
            <<" queue-before="<<injectedQueueBefore<<" queue-after-old="<<old.playerCommands.serialize()
            <<" queue-after-native="<<state.playerCommands.serialize()
            <<" oldAccepted="<<old.lastCommand.acceptedCount<<" nativeAccepted="<<state.lastCommand.acceptedCount
            <<" oldRejected="<<old.lastCommand.rejectedCount<<" nativeRejected="<<state.lastCommand.rejectedCount<<'\n';
        // Direct foreign-ID injection bypasses the real UI gate. Shared command
        // execution also serves AI/remote borrowed selections: preserve its old
        // outcome instead of redefining the dispatcher bool as a permission gate.
        if(!expect(runtimePlayerCurrentKeyCode(VK_F13)==initialCode&&oldResult==RuntimeSelectionKeyResult::DispatchedConsumed
                &&captures==oldCaptures&&snapshot(state)==snapshot(old)
                &&state.lastCommand.acceptedCount==old.lastCommand.acceptedCount
                &&state.lastCommand.rejectedCount==old.lastCommand.rejectedCount,
                "injected enemy input retains old/native command tick/world/queue semantics")) return false;
        if(!expect(!state.playerCommands.queues.contains(3)&&!old.playerCommands.queues.contains(3),
                "original injected Stop cancels the authored-ID queue in both paths")) return false;
        captures.clear(); state.selectedObjectIds.clear();
        applyRuntimePlayerSelection(state,{3},false,false);
        if(!expect(!runtimePlayerLocalSelectable(state,3)&&state.selectedObjectIds.empty(),
                "actual UI eligibility/applySelection rejects foreign actor")) return false;
        if(!expect(queueEnemyMove(state),"real-gate enemy queue preservation fixture")) return false;
        const auto gateBefore=snapshot(state), gateQueueBefore=state.playerCommands.serialize();
        const auto gateWorldBefore=state.world.deterministicSimulationStateHash(); const int gateTick=state.tick;
        { RuntimeSnapshotActiveStateScope active(state); SendMessageW(window.window,WM_KEYDOWN,VK_F13,0); }
        printCapture("real-selection-gate",0,WM_KEYDOWN,false,captures);
        trace<<"real-selection-gate before="<<gateBefore<<" after="<<snapshot(state)
            <<" queue-before="<<gateQueueBefore<<" queue-after="<<state.playerCommands.serialize()
            <<" world-before="<<gateWorldBefore<<" world-after="<<state.world.deterministicSimulationStateHash()
            <<" tick-before="<<gateTick<<" tick-after="<<state.tick<<'\n';
        if(!expect(captures.empty()&&snapshot(state)==gateBefore&&state.world.deterministicSimulationStateHash()==gateWorldBefore
                &&state.playerCommands.serialize()==gateQueueBefore&&state.tick==gateTick,
                "real foreign-selection rejection leaves actual world/queued orders/tick unchanged")) return false;
        state.playerCommunication.diplomacyOpen=true; state.leftPointer.active=state.leftPointer.moved=true;
        SetCapture(window.window);
        if(!expect(GetCapture()==window.window,"hidden fixture actually acquired synchronous capture")) return false;
        { RuntimeSnapshotActiveStateScope active(state); SendMessageW(window.window,WM_KEYDOWN,VK_ESCAPE,0); }
        if(!expect(!state.playerCommunication.diplomacyOpen&&GetCapture()!=window.window,"original diplomacy close keeps synchronous capture release")) return false;
    }
    {
        auto storage=make(); auto& state=*storage; configure(state); std::vector<Capture> captures; installCapture(state,captures);
        RuntimePlayerDefeatNoticeSmokeWindow window(state,800,600);
        if(!expect(window.window!=nullptr,"unknown/default native window")) return false;
        const auto before=snapshot(state);
        const auto expected=DefWindowProcW(window.window,WM_SYSKEYDOWN,VK_F24,0);
        LRESULT actual; { RuntimeSnapshotActiveStateScope active(state); actual=SendMessageW(window.window,WM_SYSKEYDOWN,VK_F24,0); }
        if(!expect(actual==expected&&captures.empty()&&snapshot(state)==before,"unknown system key preserves native default fallback")) return false;
        state.runtimeMenuKeyboardLoadAttempted=false; state.runtimeMenuKeyboardBindings.clear(); state.playerCommunication.chatOpen=true;
        { RuntimeSnapshotActiveStateScope active(state); SendMessageW(window.window,WM_KEYDOWN,VK_F13,0); }
        if(!expect(!state.runtimeMenuKeyboardLoadAttempted,"chat early return does not eagerly initialize key bindings")) return false;
    }
    // Exercise a real ordinary load commit before constructing the next native
    // adapter; no child/config/selection references are retained across it.
    if(!expect(stock.report.success&&stock.world.getSummary().loaded&&!stock.world.getTerrainCells().empty()
            &&stock.rulesLoaded&&stock.artLoaded&&stock.theaterLoaded&&stock.rules.findUnitType("UnitType","AMCV"),
            "real load resource/terrain/AMCV context")) return false;
    {
        auto storage=std::make_unique<RuntimeWindowState>(); auto& state=*storage;
        RuntimeSnapshotActiveStateScope active(state);
        state.config=stock.config; state.config.gameMode="Battle"; state.config.gameModeRulesOverride.clear();
        state.config.multiplayerActive=false; state.config.playerCountry="Americans"; state.config.computerAiEnabled=false;
        state.config.shortGame=true; state.config.keyboardReferenceLayout=true; state.config.keyBindings.clear();
        state.config.showMainMenu=state.config.showSkirmishMenu=false; state.config.configPath=pathString(directory/"load-preferences.ini");
        state.config.playerSlots.assign(3,{}); for(std::size_t index=0;index<3;++index) {
            auto& slot=state.config.playerSlots[index]; slot.enabled=slot.human=true; slot.local=index==0; slot.country=index%2?"Russians":"Americans"; slot.team="-";
            slot.name="Selection key player "+std::to_string(index+1);
            const auto ownerKey=runtimeCanonicalHumanHouseOwner(runtimeSkirmishOwnerForSlot(index));
            state.skirmishCountryByOwner[ownerKey]=slot.country;
            state.skirmishTeamByOwner[ownerKey]=runtimeNormalizeSkirmishTeam(slot.team);
        }
        state.report=stock.report; state.world=stock.world; state.rules=stock.rules; state.art=stock.art; state.theater=stock.theater; state.mapFacts=stock.mapFacts;
        state.rulesLoaded=state.artLoaded=state.theaterLoaded=true; state.mapFactsLoaded=stock.mapFactsLoaded; state.terrainInfoByCell=stock.terrainInfoByCell;
        state.world.accessObjects().clear(); state.runtimeSoundsEnabled=state.runtimeMusicEnabled=false;
        state.runtimeSuppressPlayerAudioFeedback=true; state.runtimePreferencePersistenceEnabled=false; state.visionEnabled=false;
        state.runtimeVoiceVolume=state.runtimeSoundVolume=0;
        state.economy.owner=runtimeSkirmishOwnerForSlot(0); state.economy.skirmishStartApplied=true; state.triggers.bootstrapComplete=true; state.tick=21;
        initializeRuntimeNavigation(state);
        const auto& cell=state.world.getTerrainCells().front();
        auto* actor=addRuntimeObjectInstanceForOwner(state,state.economy.owner,RuntimeBuildKind::Unit,"AMCV",{cell.x,cell.y});
        if(!expect(actor!=nullptr,"real load actor")) return false;
        actor->mission="Guard"; actor->buildAnimTicksTotal=actor->buildAnimTicksRemaining=0; clearRuntimeRenderPosition(*actor);
        const int savedActorId=actor->instanceId;
        state.selectedObjectIds={savedActorId}; (void)advanceRuntimeParticles(state);
        if(!expect(state.particlesInitialized&&state.particles.currentTick()==state.tick
                &&state.particles.fixedTickHz()==std::max(1,state.config.fixedTickHz),
                "real save fixture synchronized original particle authority")) return false;
        std::string error; const auto savePath=pathString(directory/"selection-key-load.ini");
        if(!expect(saveRuntimeState(state,savePath,error),"isolated real save: "+error)) return false;
        if(!expect(loadRuntimeState(state,savePath,error)&&state.tick==21,"actual ordinary load commit: "+error)) return false;
        configure(state); initializeRuntimeOfflineGameplayRouting(state); std::vector<Capture> captures; installCapture(state,captures);
        if(!expect(!state.world.getObjects().empty(),"loaded world actor")) return false;
        applyRuntimePlayerSelection(state,{savedActorId},false,false);
        if(!expect(runtimePlayerLocalSelectable(state,savedActorId)&&state.selectedObjectIds==std::vector<int>{savedActorId},
                "saved actor ID/ownership remains selectable after real load")) return false;
        RuntimePlayerDefeatNoticeSmokeWindow window(state,800,600);
        if(!expect(window.window!=nullptr,"real loaded-state native window")) return false;
        { RuntimeSnapshotActiveStateScope messageActive(state); SendMessageW(window.window,WM_KEYDOWN,VK_F13,0); }
        if(!expect(captures.size()==1&&captures.front().tick==21&&captures.front().applied,"native key after real scratch swap uses current root state")) return false;
    }
    trace<<"checks="<<checks<<" nativeCases="<<nativeCases<<" directCases="<<directCases<<'\n'; trace.flush();
    if(!expect(static_cast<bool>(trace),"trace flush")) return false;
    output<<"Selection-key integration passed: checks="<<checks<<" nativeCases="<<nativeCases<<" directCases="<<directCases
        <<" oldNativeOracle=1 canonicalPayloadTickWorld=1 outerGuards=6 campaignSyskeyException=1 observerLocal=1"
        <<" injectedEnemyOldNativeEquivalent=1 realSelectionGate=1 queueCancelBoundary=1 actualLoadThenKey=1 nativeModifierFullMatrix=not-run completeCaptureMatrix=not-covered fullMpSnapshot=not-run\n";
    return true;
}
