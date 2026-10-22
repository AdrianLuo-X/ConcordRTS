// Included after actual menu adapters, original owners, hidden-window fixture and ordinary loader.
bool runRuntimeMenuKeySmokeProbe(RuntimeWindowState& stock,std::ostream& output) {
    struct Clock final : RuntimeUiClock { RuntimeUiTimeMs value=1000;mutable std::size_t reads=0;
        RuntimeUiTimeMs nowMs() const noexcept override {++reads;return value;} };
    struct ClockBinding { RuntimeWindowState& state;const RuntimeUiClock* previous;
        ClockBinding(RuntimeWindowState& s,const RuntimeUiClock& c):state(s),previous(s.uiClock){s.uiClock=&c;}
        ~ClockBinding(){state.uiClock=previous;} };
    struct Witness { RuntimeWindowState& state;std::size_t captures=0;
        explicit Witness(RuntimeWindowState& s):state(s){initializeRuntimeOfflineGameplayRouting(state);
            state.multiplayerIntentRouter.setCaptureCallback([this](const RuntimeMultiplayerGameplayCommand&,std::string& e){
                ++captures;e="unexpected menu authority capture";return false;});}
        ~Witness(){state.multiplayerIntentRouter.setCaptureCallback({});} };
    std::size_t checks=0,captureCases=0,navigationCases=0,activationCases=0,guardCases=0,sourceReads=0;
    const auto expect=[&](bool value,const std::string& why){++checks;if(!value)output<<"Menu-key integration failed: "<<why<<'\n';return value;};
    if(!expect(stock.report.success&&stock.world.getSummary().loaded&&stock.rulesLoaded&&stock.artLoaded&&stock.theaterLoaded,
        "actual stock resources"))return false;
    char* requested=nullptr;std::size_t requestedLength=0;
    const auto envResult=_dupenv_s(&requested,&requestedLength,"CONCORD_MENU_KEY_OUTPUT");
    const std::filesystem::path directory=requested&&*requested?std::filesystem::path(requested):std::filesystem::path("menu-key-smoke-output");
    std::free(requested);if(!expect(envResult==0,"isolated output environment"))return false;
    std::error_code ec;std::filesystem::create_directories(directory,ec);if(!expect(!ec,"isolated output directory"))return false;
    std::ofstream trace(directory/"menu-key-trace.txt",std::ios::out|std::ios::trunc);
    if(!expect(static_cast<bool>(trace),"trace file"))return false;
    const auto hexText=[](const std::string& value){std::ostringstream s;for(unsigned char c:value)
        s<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(c);return s.str();};
    const auto wideText=[](const std::wstring& value){std::ostringstream s;for(wchar_t c:value)s<<static_cast<std::uint32_t>(c)<<',';return s.str();};
    const auto replaceExact=[](std::string value,const std::string& from,const std::string& to){
        if(from.empty())return value;std::size_t pos=0;while((pos=value.find(from,pos))!=std::string::npos){value.replace(pos,from.size(),to);pos+=to.size();}return value;};
    const auto canonical=[&](std::string value,const RuntimeWindowState& state){
        value=replaceExact(std::move(value),state.config.configPath,"<private-preferences>");
        if(state.config.localDataRootPath==pathString(directory/"save-old")||state.config.localDataRootPath==pathString(directory/"save-direct")
            ||state.config.localDataRootPath==pathString(directory/"save-native")){
            value=replaceExact(std::move(value),runtimeDefaultSavePath(state),"<private-runtime-save>");
            value=replaceExact(std::move(value),state.config.localDataRootPath,"<private-save-root>");}
        return value;};
    const auto authority=[&](const RuntimeWindowState& state){std::ostringstream s;
        s<<state.tick<<':'<<state.world.deterministicSimulationStateHash()<<':'<<hexText(state.playerCommands.serialize())<<':'<<state.playerCommands.stateHash()
            <<':'<<runtimeSerializeProductionLanes(state.economy.localProductionLanes)<<':'<<state.economy.credits
            <<':'<<state.lastCommand.valid<<':'<<state.lastCommand.verb<<':'<<state.lastCommand.tick
            <<':'<<state.lastCommand.selectedCount<<':'<<state.lastCommand.acceptedCount<<':'<<state.lastCommand.rejectedCount
            <<':'<<state.lastCommand.targetX<<':'<<state.lastCommand.targetY<<':'<<state.lastCommand.targetObjectId
            <<':'<<state.lastCommand.targetObjectType<<':'<<state.lastCommand.inputAckImmediate<<':'<<state.lastCommand.feedbackLatencyTicks;return s.str();};
    const auto fullState=[&](const RuntimeWindowState& state,bool normalized){
        std::ostringstream s;s<<"menu="<<state.runtimeMenuOpen<<','<<static_cast<int>(state.runtimeMenuPage)
            <<','<<static_cast<int>(state.runtimeMenuHover)<<','<<static_cast<int>(state.runtimeMenuPressed)<<','<<static_cast<int>(state.runtimeMenuSliderDrag)
            <<','<<state.runtimeMenuFocus<<','<<state.runtimeMenuKeyCaptureIndex<<','<<state.runtimeMenuKeyboardCategory<<','<<state.runtimeMenuKeyboardPage
            <<";status="<<hexText(normalized?canonical(state.interactionStatus,state):state.interactionStatus)
            <<";menuStatus="<<wideText(state.runtimeMenuStatus)<<";keySource="<<state.runtimeMenuKeyboardSource
            <<";keysLoaded="<<state.runtimeMenuKeyboardLoadAttempted<<";keys=";
        for(const auto& [id,code]:state.runtimeMenuKeyboardBindings)s<<id<<':'<<code<<',';
        s<<";configKeys=";for(const auto& [id,code]:state.config.keyBindings)s<<id<<':'<<code<<',';
        s<<";preferences="<<state.config.keyboardReferenceLayout<<','<<state.config.gameSpeed<<','<<state.config.scrollRate
            <<','<<state.config.visualDetail<<','<<state.config.tooltipsEnabled<<','<<state.config.targetLinesEnabled<<','<<state.config.scrollCoastingEnabled
            <<','<<state.config.seeHiddenObjects<<','<<state.config.windowWidth<<','<<state.config.windowHeight
            <<','<<state.config.musicEnabled<<','<<state.config.soundEffectsEnabled<<','<<state.config.musicVolume<<','<<state.config.soundVolume<<','<<state.config.voiceVolume
            <<";runtimePrefs="<<state.runtimeScrollRate<<','<<state.runtimeVisualDetail<<','<<state.runtimeTooltipsEnabled<<','<<state.runtimeTargetLinesEnabled
            <<','<<state.runtimeScrollCoastingEnabled<<','<<state.runtimeResolutionWidth<<','<<state.runtimeResolutionHeight
            <<','<<state.runtimeMusicEnabled<<','<<state.runtimeSoundEffectsEnabled<<','<<state.runtimeMusicVolume<<','<<state.runtimeSoundVolume<<','<<state.runtimeVoiceVolume
            <<";forceAttack="<<state.forceAttackHeld<<";repaint="<<state.repaintRequested
            <<";camera="<<state.camera.worldOffsetX<<','<<state.camera.worldOffsetY<<','<<state.camera.initialized
            <<";clamp="<<state.cameraClampViewport.left<<','<<state.cameraClampViewport.top<<','<<state.cameraClampViewport.right<<','<<state.cameraClampViewport.bottom
            <<";projection="<<state.projectedObjectsCacheValid<<','<<state.projectedObjectsCacheSignature
            <<";local="<<state.playerLocalCommands.cameraFollow<<','<<state.playerLocalCommands.followTargeting<<','<<state.playerLocalCommands.rallyTargeting
            <<','<<state.playerLocalCommands.planning<<";selection=";for(int id:state.selectedObjectIds)s<<id<<',';
        s<<";drag="<<state.leftPointer.active<<','<<state.leftPointer.moved<<','<<state.rightPointer.active<<','<<state.rightPointer.moved
            <<";beacon="<<state.playerCommunication.beaconTargeting<<";match="<<state.skirmishMatchFinished<<','<<state.skirmishVictory<<','<<state.skirmishResult
            <<";result="<<state.resultPresentation.resigned<<','<<state.resultPresentation.started<<','<<state.resultPresentation.bannerVisible
            <<','<<state.resultPresentation.startedAtMs<<','<<state.resultPresentation.bannerDurationMs<<','<<state.resultPresentation.scorePage
            <<','<<state.resultPresentation.continueHovered<<','<<state.resultPresentation.continuePressed<<','<<state.resultPresentation.scoreMusicSelected
            <<";exit="<<state.runtimeExitRequested<<";behaviors="<<state.behaviors.size()<<";authority="<<authority(state)<<";economyStatus="<<state.economy.status
            <<";sound="<<state.runtimeSoundEvents<<','<<state.lastRuntimeSound;
        for(const auto& intent:state.runtimeSoundIntentHistory)s<<";soundIntent="<<intent;
        for(const auto& sound:state.runtimeSoundPlaybackHistory)s<<";soundRecord="<<sound.tick<<','<<sound.playbackOrder<<','<<sound.intent<<','<<sound.section
            <<','<<sound.sample<<','<<sound.path<<','<<sound.status<<','<<sound.semantic<<','<<sound.eva<<','<<sound.playable<<','<<sound.played
            <<','<<sound.suppressed<<','<<sound.queued<<','<<sound.queueRequested;
        s<<";prefsPath="<<state.config.configPath<<";localDataRoot="<<state.config.localDataRootPath;return normalized?canonical(s.str(),state):s.str();};
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
    auto prototype=makeBase(0,true);if(!expect(static_cast<bool>(prototype),"real legal three-house menu prototype"))return false;
    const auto privatePreferences=[&](RuntimeWindowState& state,const std::string& name){
        state.config.configPath=pathString(directory/(name+"-preferences.ini"));
        std::ofstream file(state.config.configPath,std::ios::binary|std::ios::trunc);file<<"[Preserved]\nSentinel=keep\n";return static_cast<bool>(file);};
    const auto make=[&](const std::string& name){
        auto storage=std::make_unique<RuntimeWindowState>();auto& state=*storage;RuntimeSnapshotActiveStateScope active(state);
        state.config=prototype->config;state.report=prototype->report;state.world=prototype->world;state.rules=prototype->rules;state.art=prototype->art;
        state.theater=prototype->theater;state.mapFacts=prototype->mapFacts;state.rulesLoaded=state.artLoaded=state.theaterLoaded=true;
        state.mapFactsLoaded=prototype->mapFactsLoaded;state.economy=prototype->economy;state.terrainInfoByCell=prototype->terrainInfoByCell;
        state.terrainByCell=buildRuntimeTerrainLookup(state.world);state.triggers.bootstrapComplete=true;state.tick=7;state.visionEnabled=false;
        state.runtimeSoundsEnabled=state.runtimeMusicEnabled=false;state.runtimeSoundEffectsEnabled=false;state.runtimeSuppressPlayerAudioFeedback=true;
        state.runtimeMusicVolume=state.runtimeSoundVolume=state.runtimeVoiceVolume=0;state.runtimePreferencePersistenceEnabled=false;
        state.config.keyboardReferenceLayout=true;state.config.keyBindings.clear();if(!privatePreferences(state,name))return std::unique_ptr<RuntimeWindowState>{};
        initializeRuntimeNavigation(state);initializeRuntimeOfflineGameplayRouting(state);ensureRuntimeMenuKeyboardLoaded(state);setRuntimeMenuOpen(state,true);
        state.runtimeMenuPage=RuntimeMenuPage::Keyboard;state.runtimeMenuKeyCaptureIndex=-1;state.runtimePreferencePersistenceEnabled=true;
        state.repaintRequested=false;state.interactionStatus="menu initial";return storage;};
    // Original modifier switch shadows the migrated private wrapper for this oracle only.
    const auto runtimeKeyboardModifierOnly=[](WPARAM key)
{
    switch (key) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    default:
        return false;
    }
}
    ;
    const auto oldCapture=[&](RuntimeWindowState& state,WPARAM key)
{
    if (!state.runtimeMenuOpen
        || state.runtimeMenuPage != RuntimeMenuPage::Keyboard
        || state.runtimeMenuKeyCaptureIndex < 0) {
        return false;
    }
    if (key == VK_ESCAPE) {
        state.runtimeMenuKeyCaptureIndex = -1;
        state.runtimeMenuStatus = L"已取消改键。";
        return true;
    }
    if (runtimeKeyboardModifierOnly(key)) {
        state.runtimeMenuStatus =
            L"请同时按下组合键中的字母、数字或功能键。";
        return true;
    }

    const int bindingIndex = state.runtimeMenuKeyCaptureIndex;
    int keyCode = runtimePlayerCurrentKeyCode(key);
    bool restoredDefault = false;
    if (key == VK_BACK) {
        if (bindingIndex < 0
            || bindingIndex >= static_cast<int>(state.runtimeMenuKeyboardBindings.size())) {
            state.runtimeMenuKeyCaptureIndex = -1;
            state.runtimeMenuStatus = L"已取消改键。";
            return true;
        }
        keyCode = runtimeKeyboardDefaultAt(bindingIndex);
        restoredDefault = true;
    }
    (void)assignRuntimeMenuKeyboardBinding(
        state,
        bindingIndex,
        keyCode,
        restoredDefault
    );
    return true;
}
    ;
    // Old navigation resolves this original local horizontal body, not the migrated wrapper.
    const auto handleRuntimeMenuHorizontalKey=[&](RuntimeWindowState& state,const RECT& clientRect,WPARAM key)
{
    if (key != VK_LEFT && key != VK_RIGHT) {
        return false;
    }
    if (state.runtimeMenuPage==RuntimeMenuPage::Keyboard) {
        runtimeKeyboardChangePage(state,key==VK_RIGHT?1:-1,
            runtimeMenuFocusedAction(state,clientRect)==RuntimeMenuAction::KeyboardCategory);
        return true;
    }
    return adjustRuntimeMenuSliderPosition(
        state,
        runtimeMenuFocusedAction(state, clientRect),
        key == VK_RIGHT ? 1 : -1
    );
}
    ;
    const auto oldNavigation=[&](RuntimeWindowState* state,HWND window,WPARAM wParam)->LRESULT {
        if (state->runtimeMenuOpen) {
            if (state->runtimeMenuPage==RuntimeMenuPage::Keyboard && (wParam==VK_PRIOR || wParam==VK_NEXT)) {
                runtimeKeyboardChangePage(*state,wParam==VK_PRIOR?-1:1,false);
                InvalidateRect(window,nullptr,FALSE);return 0;
            }
            RECT menuClientRect{};
            GetClientRect(window, &menuClientRect);
            const std::vector<RuntimeMenuButton> buttons =
                runtimeMenuFocusableButtons(*state, menuClientRect);
            if (!buttons.empty()
                && (wParam == VK_UP || wParam == VK_DOWN
                    || wParam == VK_HOME || wParam == VK_END)) {
                const int count = static_cast<int>(buttons.size());
                if (wParam == VK_HOME) {
                    state->runtimeMenuFocus = 0;
                } else if (wParam == VK_END) {
                    state->runtimeMenuFocus = count - 1;
                } else {
                    state->runtimeMenuFocus =
                        (state->runtimeMenuFocus
                            + (wParam == VK_UP ? count - 1 : 1))
                        % count;
                }
                state->runtimeMenuHover = RuntimeMenuAction::None;
                state->repaintRequested = true;
                RedrawWindow(
                    window,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
                );
                return 0;
            }
            if (!buttons.empty()
                && (wParam == VK_LEFT || wParam == VK_RIGHT)) {
                if (handleRuntimeMenuHorizontalKey(
                        *state,
                        menuClientRect,
                        wParam
                    )) {
                    emitRuntimeSound(
                        *state,
                        runtimeSoundIntentSidebarButtonClick
                    );
                    state->runtimeMenuHover = RuntimeMenuAction::None;
                    state->repaintRequested = true;
                    RedrawWindow(
                        window,
                        nullptr,
                        nullptr,
                        RDW_INVALIDATE | RDW_NOERASE
                    );
                    return 0;
                }
            }
            if (!buttons.empty()
                && (wParam == VK_RETURN || wParam == VK_SPACE)) {
                const int index = std::clamp(
                    state->runtimeMenuFocus,
                    0,
                    static_cast<int>(buttons.size()) - 1
                );
                activateRuntimeMenuAction(
                    window,
                    *state,
                    buttons[static_cast<std::size_t>(index)].action
                );
                return 0;
            }
            return 0;
        }
        return 1;};
    const auto prefix=[](RuntimeWindowState& state,HWND window,UINT message,WPARAM raw){
        runtimeInvalidateVisionTerrainBatchForMessage(state.world);
        if((message==WM_KEYDOWN||message==WM_SYSKEYDOWN)&&(raw==VK_CONTROL||raw==VK_LCONTROL||raw==VK_RCONTROL)&&!state.forceAttackHeld){
            state.forceAttackHeld=true;state.repaintRequested=true;RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_NOERASE);}};
    const auto oldDrive=[&](RuntimeWindowState& state,HWND window,UINT message,WPARAM raw,bool repeated){
        (void)repeated;prefix(state,window,message,raw);
        if(oldCapture(state,raw)){state.repaintRequested=true;RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_NOERASE);return true;}
        (void)runtimeMenuKeyboardBindingCode(state,"Options",VK_ESCAPE);return oldNavigation(&state,window,raw)==0;};
    const auto directDrive=[&](RuntimeWindowState& state,HWND window,UINT message,WPARAM raw,bool repeated){
        prefix(state,window,message,raw);RuntimeMenuCaptureUiHost captureHost(state);
        if(handleRuntimeMenuCaptureKey({static_cast<std::uint64_t>(raw),repeated},captureHost)){
            state.repaintRequested=true;RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_NOERASE);return true;}
        (void)runtimeMenuKeyboardBindingCode(state,"Options",VK_ESCAPE);RuntimeMenuNavigationUiHost host(state,window);
        return handleRuntimeMenuNavigationKey({static_cast<std::uint64_t>(raw),repeated},host);};
    const auto readFile=[](const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());};
    const auto checkPreferences=[&](const RuntimeWindowState& state){
        IniFile file;if(!file.loadFromFile(state.config.configPath)||file.getValue("Preserved","Sentinel")!="keep")return false;
        RuntimeConfigOverrides overrides;overrides.configPath=state.config.configPath;
        overrides.resourceRootPath=stock.config.resourceRootPath;overrides.localDataRootPath=stock.config.localDataRootPath;
        auto restored=std::make_unique<RuntimeWindowState>();restored->config=loadRuntimeConfig(overrides);ensureRuntimeMenuKeyboardLoaded(*restored);
        return restored->config.keyboardReferenceLayout&&restored->runtimeMenuKeyboardBindings==state.runtimeMenuKeyboardBindings;};
    const auto prepareMenu=[](RuntimeWindowState& state,RuntimeMenuPage page){
        state.runtimeMenuOpen=true;resetRuntimeMenuPage(state,page);state.runtimeMenuKeyboardCategory=0;state.runtimeMenuKeyboardPage=0;
        state.repaintRequested=false;state.interactionStatus="menu initial";state.forceAttackHeld=false;
        state.runtimeSoundEvents=0;state.runtimeSoundIntentHistory.clear();state.runtimeSoundPlaybackHistory.clear();state.lastRuntimeSound.clear();};
    const auto drive=[&](const std::string& label,RuntimeWindowState& old,RuntimeWindowState& direct,RuntimeWindowState& native,
        RuntimePlayerDefeatNoticeSmokeWindow& ow,RuntimePlayerDefeatNoticeSmokeWindow& dw,RuntimePlayerDefeatNoticeSmokeWindow& nw,
        WPARAM raw,bool repeated,UINT message,bool zeroAuthority=true){
        Clock oc,dc,nc;ClockBinding ob(old,oc),db(direct,dc),nb(native,nc);Witness a(old),b(direct),c(native);
        if(!expect(fullState(old,true)==fullState(direct,true)&&fullState(old,true)==fullState(native,true),label+" equal full entry except exact private identities"))return false;
        const auto beforeAuthority=authority(old);bool oldHandled=false,directHandled=false;LRESULT nativeResult=0;
        {RuntimeSnapshotActiveStateScope active(old);oldHandled=oldDrive(old,ow.window,message,raw,repeated);}
        {RuntimeSnapshotActiveStateScope active(direct);directHandled=directDrive(direct,dw.window,message,raw,repeated);}
        {RuntimeSnapshotActiveStateScope active(native);nativeResult=SendMessageW(nw.window,message,raw,repeated?(static_cast<LPARAM>(1)<<30):0);}
        if(fullState(old,true)!=fullState(native,true))output<<"Menu mismatch label="<<label<<" old="<<hexText(fullState(old,false))<<" native="<<hexText(fullState(native,false))<<'\n';
        if(!expect(oldHandled&&directHandled&&nativeResult==0&&fullState(old,true)==fullState(direct,true)&&fullState(old,true)==fullState(native,true),
            label+" frozen old/same production cpp/real WindowProc complete state"))return false;
        if(!expect(oc.reads==dc.reads&&oc.reads==nc.reads&&a.captures==0&&b.captures==0&&c.captures==0,label+" same clock effects and no unexpected authority capture"))return false;
        if(zeroAuthority&&!expect(authority(old)==beforeAuthority&&authority(direct)==beforeAuthority&&authority(native)==beforeAuthority,label+" real world/tick/queue unchanged"))return false;
        const auto print=[&](const char* path,const RuntimeWindowState& state,const Witness& witness,std::size_t reads,HWND window){
            RECT actual{};GetClientRect(window,&actual);trace<<"menu path="<<path<<" label="<<label<<" raw="<<static_cast<std::uint64_t>(raw)
                <<" repeat="<<repeated<<" message="<<message<<" captures="<<witness.captures<<" reads="<<reads<<" client="<<actual.right<<','<<actual.bottom
                <<" rawState="<<hexText(fullState(state,false))<<" normalizedState="<<hexText(fullState(state,true))<<'\n';};
        print("old",old,a,oc.reads,ow.window);print("direct",direct,b,dc.reads,dw.window);print("native",native,c,nc.reads,nw.window);
        sourceReads+=oc.reads+dc.reads+nc.reads;return true;};
    {
        auto os=make("catalog-old"),ds=make("catalog-direct"),ns=make("catalog-native");
        if(!expect(os&&ds&&ns,"three isolated real keyboard catalogs"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,"three catalog HWNDs"))return false;
        if(!expect(old.runtimeMenuKeyboardBindings.size()==88&&old.runtimeMenuKeyboardBindings==direct.runtimeMenuKeyboardBindings
            &&old.runtimeMenuKeyboardBindings==native.runtimeMenuKeyboardBindings,"actual 88 ids/default order"))return false;
        for(std::size_t index=0;index<88;++index){
            const auto id=old.runtimeMenuKeyboardBindings[index].first;
            for(int mode=0;mode<2;++mode){
                for(auto* state:{&old,&direct,&native}){
                    prepareMenu(*state,RuntimeMenuPage::Keyboard);
                    const auto action=runtimeKeyboardActionForIndex(static_cast<int>(index));
                    if(!expect(runtimeKeyboardBindingIndexForAction(action)==static_cast<int>(index),"actual encoded action/index roundtrip"))return false;
                    HWND window=state==&old?ow.window:state==&direct?dw.window:nw.window;
                    activateRuntimeMenuAction(window,*state,action);
                    if(!expect(state->runtimeMenuKeyCaptureIndex==static_cast<int>(index),"real menu owner establishes exact capture index"))return false;
                }
                const WPARAM raw=mode==0?VK_F20:VK_BACK;
                const auto expected=mode==0?runtimePlayerCurrentKeyCode(raw):runtimeKeyboardDefaultAt(static_cast<int>(index));
                if(!drive("catalog-"+std::to_string(index)+"-"+id+(mode?"-default":"-capture"),old,direct,native,ow,dw,nw,raw,
                    (index+mode)%2!=0,index%2?WM_SYSKEYDOWN:WM_KEYDOWN))return false;
                if(!expect(old.runtimeMenuKeyCaptureIndex==-1&&old.runtimeMenuKeyboardBindings[index].second==expected
                    &&checkPreferences(old)&&checkPreferences(direct)&&checkPreferences(native),"real assignment/default and private persisted reload"))return false;
                ++captureCases;
            }
        }
        if(!expect(readFile(old.config.configPath)==readFile(direct.config.configPath)&&readFile(old.config.configPath)==readFile(native.config.configPath),
            "three actual preference files equal with unrelated sentinel"))return false;
        trace<<"catalog ids=88 captureDefaultCases=176 persistedReload=1 sentinel=keep filesByteEqual=1\n";
    }
    const std::vector<WPARAM> specialKeys{VK_ESCAPE,VK_SHIFT,VK_LSHIFT,VK_RSHIFT,VK_CONTROL,VK_LCONTROL,VK_RCONTROL,
        VK_MENU,VK_LMENU,VK_RMENU,VK_LWIN,VK_RWIN,static_cast<WPARAM>((std::uint64_t{1}<<32)|VK_ESCAPE),
        static_cast<WPARAM>((std::uint64_t{1}<<32)|VK_CONTROL),VK_BACK,VK_F21};
    for(std::size_t index=0;index<specialKeys.size();++index){
        const auto stem="special-"+std::to_string(index);
        auto os=make(stem+"-old"),ds=make(stem+"-direct"),ns=make(stem+"-native");
        if(!expect(os&&ds&&ns,"special actual catalogs"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,"special HWNDs"))return false;
        for(auto* state:{&old,&direct,&native})state->runtimeMenuKeyCaptureIndex=index>=14?999:0;
        if(!drive(stem,old,direct,native,ow,dw,nw,specialKeys[index],index%2!=0,index%2?WM_SYSKEYDOWN:WM_KEYDOWN))return false;
        if(index==0&&!expect(old.runtimeMenuKeyCaptureIndex==-1&&old.runtimeMenuStatus==L"已取消改键。","actual Escape cancellation"))return false;
        if(index>=1&&index<=11&&!expect(old.runtimeMenuKeyCaptureIndex==0&&old.runtimeMenuStatus==L"请同时按下组合键中的字母、数字或功能键。","actual modifier-only status/no assignment"))return false;
        if(index>=12&&index<=13&&!expect(old.runtimeMenuKeyCaptureIndex==-1,"high raw bits bypass raw special handling and enter original assignment"))return false;
        if(index==14&&!expect(old.runtimeMenuKeyCaptureIndex==-1&&old.runtimeMenuStatus==L"已取消改键。","Backspace invalid index cancels after sample"))return false;
        if(index==15&&!expect(old.runtimeMenuKeyCaptureIndex==999&&old.runtimeMenuStatus==L"此按键无法指定。","ordinary invalid assignment consumed, index unchanged"))return false;
        ++captureCases;
    }
    {
        auto os=make("nav-old"),ds=make("nav-direct"),ns=make("nav-native");
        if(!expect(os&&ds&&ns,"navigation real catalogs"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,"navigation HWNDs"))return false;
        const RuntimeMenuPage pages[]{RuntimeMenuPage::Main,RuntimeMenuPage::Settings,RuntimeMenuPage::Display,
            RuntimeMenuPage::Sound,RuntimeMenuPage::Keyboard,RuntimeMenuPage::ExitConfirm};
        const WPARAM keys[]{VK_UP,VK_DOWN,VK_HOME,VK_END,VK_LEFT,VK_RIGHT,VK_PRIOR,VK_NEXT,VK_F13};
        for(std::size_t page=0;page<6;++page)for(std::size_t key=0;key<9;++key){
            for(auto* state:{&old,&direct,&native}){prepareMenu(*state,pages[page]);state->runtimeMenuFocus=key==0?-5:key==1?999:0;}
            if(!drive("nav-page-"+std::to_string(page)+"-key-"+std::to_string(key),old,direct,native,ow,dw,nw,keys[key],
                (page+key)%2!=0,key%2?WM_SYSKEYDOWN:WM_KEYDOWN))return false;++navigationCases;
        }
        const auto selectAction=[&](RuntimeWindowState& state,RuntimeMenuAction action){
            const auto buttons=runtimeMenuFocusableButtons(state,client);
            for(std::size_t index=0;index<buttons.size();++index)if(buttons[index].action==action){state.runtimeMenuFocus=static_cast<int>(index);return true;}
            return false;};
        for(bool repeated:{false,true})for(bool category:{false,true}){
            for(auto* state:{&old,&direct,&native}){prepareMenu(*state,RuntimeMenuPage::Keyboard);
                if(!expect(selectAction(*state,category?RuntimeMenuAction::KeyboardCategory:RuntimeMenuAction::KeyboardNextPage),"actual keyboard category/page action present"))return false;}
            if(!drive(category?"keyboard-horizontal-category":"keyboard-horizontal-page",old,direct,native,ow,dw,nw,VK_RIGHT,repeated,WM_KEYDOWN))return false;
            ++navigationCases;
        }
        for(bool repeated:{false,true}){
            for(auto* state:{&old,&direct,&native}){prepareMenu(*state,RuntimeMenuPage::Settings);
                if(!expect(selectAction(*state,RuntimeMenuAction::CycleScrollRate),"actual scroll-rate slider present"))return false;}
            const int before=old.runtimeScrollRate;
            if(!drive("real-slider-adjustment",old,direct,native,ow,dw,nw,VK_RIGHT,repeated,WM_SYSKEYDOWN))return false;
            if(!expect(old.runtimeScrollRate==std::clamp(before+1,0,6)&&checkPreferences(native),"real slider apply/private preferences roundtrip"))return false;
            ++navigationCases;
        }
        int rowIndex=-1;
        for(auto* state:{&old,&direct,&native}){
            prepareMenu(*state,RuntimeMenuPage::Keyboard);const auto buttons=runtimeMenuFocusableButtons(*state,client);
            auto found=std::find_if(buttons.begin(),buttons.end(),[](const RuntimeMenuButton& button){
                return runtimeKeyboardBindingIndexForAction(button.action)>=0;});
            if(!expect(found!=buttons.end(),"actual first visible keyboard command row"))return false;
            const int current=runtimeKeyboardBindingIndexForAction(found->action);
            if(rowIndex<0)rowIndex=current;
            if(!expect(current==rowIndex&&state->runtimeMenuKeyboardBindings[static_cast<std::size_t>(current)].first
                ==old.runtimeMenuKeyboardBindings[static_cast<std::size_t>(rowIndex)].first,"same real row identity in each catalog"))return false;
            state->runtimeMenuFocus=static_cast<int>(std::distance(buttons.begin(),found));
        }
        if(!drive("real-keyboard-row-select",old,direct,native,ow,dw,nw,VK_RETURN,false,WM_KEYDOWN))return false;
        if(!expect(old.runtimeMenuKeyCaptureIndex==rowIndex&&direct.runtimeMenuKeyCaptureIndex==rowIndex
            &&native.runtimeMenuKeyCaptureIndex==rowIndex,"real owned navigation activation starts capture"))return false;
        ++navigationCases;
        const auto rowCode=runtimePlayerCurrentKeyCode(VK_F22);
        if(!drive("real-keyboard-row-capture",old,direct,native,ow,dw,nw,VK_F22,true,WM_SYSKEYDOWN))return false;
        if(!expect(old.runtimeMenuKeyCaptureIndex==-1&&old.runtimeMenuKeyboardBindings[static_cast<std::size_t>(rowIndex)].second==rowCode
            &&checkPreferences(old)&&checkPreferences(direct)&&checkPreferences(native),"actual navigation-to-capture-to-persistence closure"))return false;
        ++captureCases;
    }
    const std::pair<RuntimeMenuPage,RuntimeMenuAction> actions[]{
        {RuntimeMenuPage::Main,RuntimeMenuAction::Options},{RuntimeMenuPage::Main,RuntimeMenuAction::Exit},
        {RuntimeMenuPage::Main,RuntimeMenuAction::Resume},{RuntimeMenuPage::Settings,RuntimeMenuAction::OpenDisplay},
        {RuntimeMenuPage::Settings,RuntimeMenuAction::OpenKeyboard},{RuntimeMenuPage::Display,RuntimeMenuAction::Back},
        {RuntimeMenuPage::Keyboard,RuntimeMenuAction::KeyboardCategory},{RuntimeMenuPage::Keyboard,RuntimeMenuAction::KeyboardNextPage},
        {RuntimeMenuPage::Keyboard,RuntimeMenuAction::KeyboardPreviousPage},{RuntimeMenuPage::Keyboard,RuntimeMenuAction::KeyboardResetAll},
        {RuntimeMenuPage::ExitConfirm,RuntimeMenuAction::CancelExit},{RuntimeMenuPage::ExitConfirm,RuntimeMenuAction::ConfirmExit},
        {RuntimeMenuPage::Display,RuntimeMenuAction::ApplyResolution}};
    for(std::size_t index=0;index<std::size(actions);++index){
        const auto stem="activate-"+std::to_string(index);auto os=make(stem+"-old"),ds=make(stem+"-direct"),ns=make(stem+"-native");
        if(!expect(os&&ds&&ns,"activation real states"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,"activation HWNDs"))return false;
        for(auto* state:{&old,&direct,&native}){
            prepareMenu(*state,actions[index].first);const auto buttons=runtimeMenuFocusableButtons(*state,client);
            auto found=std::find_if(buttons.begin(),buttons.end(),[&](const RuntimeMenuButton& b){return b.action==actions[index].second;});
            if(!expect(found!=buttons.end(),"actual bounded action in real layout"))return false;
            state->runtimeMenuFocus=static_cast<int>(std::distance(buttons.begin(),found));
            if(actions[index].second==RuntimeMenuAction::ApplyResolution){state->runtimeResolutionWidth=800;state->runtimeResolutionHeight=600;}
        }
        if(!drive(stem,old,direct,native,ow,dw,nw,index%2?VK_SPACE:VK_RETURN,index%3==0,index%2?WM_SYSKEYDOWN:WM_KEYDOWN,
            actions[index].second!=RuntimeMenuAction::ConfirmExit))return false;
        RECT a{},b{},c{};GetClientRect(ow.window,&a);GetClientRect(dw.window,&b);GetClientRect(nw.window,&c);
        if(!expect(EqualRect(&a,&b)&&EqualRect(&a,&c)&&IsWindow(ow.window)&&IsWindow(dw.window)&&IsWindow(nw.window),
            "original bounded activation/resize keeps real windows alive"))return false;
        if(actions[index].second==RuntimeMenuAction::ConfirmExit&&!expect(old.skirmishMatchFinished&&old.resultPresentation.resigned
            &&old.resultPresentation.started&&old.resultPresentation.bannerVisible,"actual ConfirmExit resigns into banner, no DestroyWindow"))return false;
        ++activationCases;
    }
    {
        auto os=makeBase(1,true),ds=makeBase(1,true),ns=makeBase(1,true);
        if(!expect(os&&ds&&ns,"three proven legal active save worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        int index=0;for(auto* state:{&old,&direct,&native}){
            const char* role=index==0?"old":index==1?"direct":"native";++index;
            if(!expect(privatePreferences(*state,std::string("save-")+role),"private save preference sentinel"))return false;
            const auto saveRoot=directory/(std::string("save-")+role);std::filesystem::create_directories(saveRoot,ec);
            if(!expect(!ec,"private default-save parent"))return false;
            state->config.localDataRootPath=pathString(saveRoot);
            if(!expect(runtimeDefaultSavePath(*state)==pathString(saveRoot/"runtime_save.ini"),"actual default save path isolated after real resources initialized"))return false;
            RuntimeSnapshotActiveStateScope active(*state);state->config.shortGame=true;state->tick=21;(void)advanceRuntimeParticles(*state);
            if(!expect(state->particlesInitialized&&state->particles.currentTick()==21,"real active particles tick21"))return false;
            state->runtimePreferencePersistenceEnabled=false;state->playerCommunication.beaconTargeting=false;
            state->playerLocalCommands.followTargeting=state->playerLocalCommands.rallyTargeting=false;state->activeSidebarTool=RuntimeSidebarTool::None;
        }
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,"save/load real HWNDs"))return false;
        for(RuntimeMenuAction action:{RuntimeMenuAction::Save,RuntimeMenuAction::Load}){
            for(auto* state:{&old,&direct,&native}){
                prepareMenu(*state,RuntimeMenuPage::Main);const auto buttons=runtimeMenuFocusableButtons(*state,client);
                auto found=std::find_if(buttons.begin(),buttons.end(),[&](const RuntimeMenuButton& b){return b.action==action;});
                if(!expect(found!=buttons.end(),"actual save/load menu action present"))return false;
                state->runtimeMenuFocus=static_cast<int>(std::distance(buttons.begin(),found));
            }
            if(!drive(action==RuntimeMenuAction::Save?"actual-menu-save":"actual-menu-load",old,direct,native,ow,dw,nw,VK_RETURN,false,WM_KEYDOWN,
                false))return false;
            if(!expect(old.tick==21&&direct.tick==21&&native.tick==21
                &&old.interactionStatus.starts_with(action==RuntimeMenuAction::Save?"saved ":"loaded ")
                &&std::filesystem::is_regular_file(runtimeDefaultSavePath(old))&&std::filesystem::is_regular_file(runtimeDefaultSavePath(direct))
                &&std::filesystem::is_regular_file(runtimeDefaultSavePath(native)),"genuine menu save/load owner succeeded tick21"))return false;
            ++activationCases;
        }
        // drive installs fresh clocks/capture callbacks after load. No child reference or router callback survives scratch commit.
        if(!drive("fresh-menu-root-after-load",old,direct,native,ow,dw,nw,VK_HOME,false,WM_SYSKEYDOWN))return false;
        if(!expect(old.runtimeMenuFocus==0&&direct.runtimeMenuFocus==0&&native.runtimeMenuFocus==0
            &&old.tick==21&&native.particles.currentTick()==21,"fresh-root navigation after scratch load"))return false;
        ++navigationCases;trace<<"load tick=21 actualMenuSave=1 actualMenuLoad=1 freshRoot=1 privateDefaultSave=1\n";
    }
    for(int mode=0;mode<6;++mode){
        auto storage=make("guard-"+std::to_string(mode));if(!expect(static_cast<bool>(storage),"guard state"))return false;auto& state=*storage;
        state.runtimeMenuOpen=false;state.runtimeMenuKeyCaptureIndex=-1;
        if(mode==0)state.playerCommunication.chatOpen=true;if(mode==1)state.playerCommunication.diplomacyOpen=true;
        if(mode==2){state.skirmishMatchFinished=true;state.resultPresentation.bannerVisible=true;}
        if(mode==3){state.campaignMode=true;state.triggers.campaignInputLocked=true;}
        WPARAM guardKey=VK_F13;
        if(mode==4){state.runtimeMenuOpen=true;state.runtimeMenuPage=RuntimeMenuPage::Keyboard;guardKey=VK_ESCAPE;}
        if(mode==5){
            state.runtimeMenuOpen=true;state.runtimeMenuPage=RuntimeMenuPage::Keyboard;guardKey=VK_F9;
            const auto privateRoot=directory/"guard-private-load";std::filesystem::create_directories(privateRoot,ec);
            if(!expect(!ec,"private quickload guard directory"))return false;
            state.config.localDataRootPath=pathString(privateRoot);
            if(!expect(runtimeDefaultSavePath(state)==pathString(privateRoot/"runtime_save.ini")
                &&!std::filesystem::exists(runtimeDefaultSavePath(state)),"actual default quickload is private and absent"))return false;
            const auto code=runtimePlayerCurrentKeyCode(guardKey),replacement=runtimePlayerCurrentKeyCode(VK_F24);
            for(auto& binding:state.runtimeMenuKeyboardBindings)if(binding.second==code)binding.second=replacement;
            if(!expect(runtimeMenuKeyboardBindingCode(state,"Options",VK_ESCAPE)!=code
                &&std::none_of(state.runtimeMenuKeyboardBindings.begin(),state.runtimeMenuKeyboardBindings.end(),
                    [&](const auto& binding){return binding.first!="Options"&&binding.second==code;}),
                "actual unconfigured quickload is not shadowed by menu/gameplay bindings"))return false;
        }
        Clock clock;ClockBinding binding(state,clock);Witness witness(state);RuntimePlayerDefeatNoticeSmokeWindow window(state,1024,768);
        if(!expect(window.window!=nullptr,"guard real HWND"))return false;
        const auto before=authority(state);const auto bindings=state.runtimeMenuKeyboardBindings;
        {RuntimeSnapshotActiveStateScope active(state);SendMessageW(window.window,WM_KEYDOWN,guardKey,0);}
        if(!expect(authority(state)==before&&state.runtimeMenuKeyboardBindings==bindings
            &&state.runtimeMenuKeyCaptureIndex==-1&&witness.captures==0,"earlier guards preserve authority and binding table"))return false;
        if(mode<4&&!expect(!state.runtimeMenuOpen&&clock.reads==0,"earlier modal/terminal/campaign prevents menu processing"))return false;
        if(mode==4&&!expect(state.runtimeMenuOpen&&state.runtimeMenuPage==RuntimeMenuPage::Settings&&clock.reads==0,
            "Options Escape returns to parent before menu navigation"))return false;
        if(mode==5&&!expect(state.runtimeMenuOpen&&state.runtimeMenuPage==RuntimeMenuPage::Keyboard
            &&state.interactionStatus.starts_with("load failed: ")&&!std::filesystem::exists(runtimeDefaultSavePath(state)),
            "unconfigured private quickload precedes menu navigation without loading or writing player data"))return false;
        trace<<"guard mode="<<mode<<" reads="<<clock.reads<<" captures="<<witness.captures<<" rawState="<<hexText(fullState(state,false))<<'\n';++guardCases;
    }
    trace<<"checks="<<checks<<" captureCases="<<captureCases<<" navigationCases="<<navigationCases
        <<" activationCases="<<activationCases<<" guards="<<guardCases<<" sourceReads="<<sourceReads<<'\n';
    trace.flush();if(!expect(static_cast<bool>(trace),"trace flush"))return false;
    output<<"Menu-key integration passed: checks="<<checks<<" captureCases="<<captureCases<<" navigationCases="<<navigationCases
        <<" activationCases="<<activationCases<<" guards="<<guardCases<<" sourceReads="<<sourceReads
        <<" sameProductionCpp=1 frozenOldThreeBlocksAndModifier=1 actualCatalog88=1 actualPreferencesReload=1 sentinel=preserved"
        <<" pathNormalization=exact-route-private-identities rawAndNormalizedState=retained actualMenuSaveLoad=1 activeTick21=1 freshRootAfterLoad=1"
        <<" nativeOrderedTrace=observable-complete-state standardOrderedQueries=separate-contract nativeModifierFullMatrix=not-run"
        <<" actualDestroyWindowActivation=not-applicable-original-resigns forcedCallbackRootInvalidation=not-forced"
        <<" runningMPsession=not-run externalNetwork=not-run audioDeviceContinuity=not-run IME=not-run secondWindowBackend=not-run"
        <<" geometryMigration=not-covered originalOverflowDomain=not-covered observerEndSaveFailure=preserved\n";return true;
}
