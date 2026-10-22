// Native session-key regressions; included after the real adapters and ordinary loader.
bool runRuntimeSessionKeySmokeProbe(RuntimeWindowState& stock,std::ostream& output) {
    struct Clock final : RuntimeUiClock { RuntimeUiTimeMs value=123456;mutable std::size_t reads=0;
        RuntimeUiTimeMs nowMs() const noexcept override {++reads;return value;} };
    struct ClockBinding { RuntimeWindowState& state;const RuntimeUiClock* previous;
        ClockBinding(RuntimeWindowState& s,const RuntimeUiClock& c):state(s),previous(s.uiClock){s.uiClock=&c;}
        ~ClockBinding(){state.uiClock=previous;} };
    struct Witness { RuntimeWindowState& state;std::size_t captures=0;
        explicit Witness(RuntimeWindowState& s):state(s){
            state.multiplayerIntentRouter.setCaptureCallback([this](const RuntimeMultiplayerGameplayCommand&,std::string& e){
                ++captures;e="unexpected session authority capture";return false;});}
        ~Witness(){state.multiplayerIntentRouter.setCaptureCallback({});} };
    std::size_t checks=0,resultCases=0,optionsCases=0,fileCases=0,guardCases=0,sourceReads=0,captureReleases=0,realDestroyed=0;
    const auto expect=[&](bool value,const std::string& why){++checks;if(!value)output<<"Session-key integration failed: "<<why<<'\n';return value;};
    if(!expect(stock.report.success&&stock.world.getSummary().loaded&&stock.rulesLoaded&&stock.artLoaded&&stock.theaterLoaded,
        "actual stock resources"))return false;
    char* requested=nullptr;std::size_t requestedLength=0;
    const auto envResult=_dupenv_s(&requested,&requestedLength,"CONCORD_SESSION_KEY_OUTPUT");
    const std::filesystem::path directory=requested&&*requested?std::filesystem::path(requested):std::filesystem::path("session-key-smoke-output");
    std::free(requested);if(!expect(envResult==0,"isolated output environment"))return false;
    std::error_code ec;std::filesystem::create_directories(directory,ec);if(!expect(!ec,"isolated output directory"))return false;
    std::ofstream trace(directory/"session-key-trace.txt",std::ios::out|std::ios::trunc);
    if(!expect(static_cast<bool>(trace),"trace file"))return false;
    const auto hexText=[](const std::string& value){std::ostringstream s;for(unsigned char c:value)
        s<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(c);return s.str();};
    const auto wideText=[](const std::wstring& value){std::ostringstream s;for(wchar_t c:value)s<<static_cast<std::uint32_t>(c)<<',';return s.str();};
    const auto replaceExact=[](std::string value,const std::string& from,const std::string& to){
        if(from.empty())return value;std::size_t pos=0;while((pos=value.find(from,pos))!=std::string::npos){value.replace(pos,from.size(),to);pos+=to.size();}return value;};
    std::map<const RuntimeWindowState*,std::pair<std::string,std::string>> privateIdentities;
    const auto canonical=[&](std::string value,const RuntimeWindowState& state){
        const auto found=privateIdentities.find(&state);
        if(found!=privateIdentities.end()&&state.config.configPath==found->second.first
            &&state.config.localDataRootPath==found->second.second){
            value=replaceExact(std::move(value),found->second.first,"<private-preferences>");
            const auto expectedSave=pathString(std::filesystem::path(found->second.second)/"runtime_save.ini");
            if(runtimeDefaultSavePath(state)==expectedSave)value=replaceExact(std::move(value),expectedSave,"<private-runtime-save>");
            value=replaceExact(std::move(value),found->second.second,"<private-save-root>");}
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
        s<<";prefsPath="<<state.config.configPath<<";localDataRoot="<<state.config.localDataRootPath;s<<";session="<<state.campaignMode<<','<<state.exitAfterPendingCampaignVideos<<','<<state.campaignScore.presentationStartedAtMs
            <<";videos=";for(const auto& movie:state.pendingCampaignVideoPaths)s<<movie<<',';
        s<<";sidebar="<<static_cast<int>(state.activeSidebarTool)<<";menuPersist="<<state.runtimePreferencePersistenceEnabled
            <<";chat="<<state.playerCommunication.chatOpen<<','<<state.playerCommunication.diplomacyOpen
            <<";particles="<<state.particlesInitialized<<','<<state.particles.currentTick()<<";superWeaponAuthority="<<state.superWeapons.stateHash();
        if(const auto& placement=state.multiplayerIntentRouter.placement())s<<";placement="<<static_cast<int>(placement->lane)<<','<<placement->productionSerial<<','<<placement->buildingTypeId<<','<<placement->cell.x<<','<<placement->cell.y<<','<<placement->previewValid;
        if(const auto& weapon=state.multiplayerIntentRouter.superWeapon())s<<";superWeapon="<<weapon->superWeaponId<<','<<weapon->expectedTargetCount<<','<<weapon->targets.size();
        return normalized?canonical(s.str(),state):s.str();};
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
    auto prototype=makeBase(0,true);if(!expect(static_cast<bool>(prototype),"real legal three-house session prototype"))return false;
    const auto isolate=[&](RuntimeWindowState& state,const std::string& name){
        state.config.configPath=pathString(directory/(name+"-preferences.ini"));
        std::ofstream preferences(state.config.configPath,std::ios::binary|std::ios::trunc);preferences<<"[Preserved]\nSentinel=keep\n";preferences.close();
        const auto saveRoot=directory/(name+"-save");std::filesystem::create_directories(saveRoot,ec);if(ec)return false;
        state.config.localDataRootPath=pathString(saveRoot);
        const bool valid=static_cast<bool>(preferences)&&runtimeDefaultSavePath(state)==pathString(saveRoot/"runtime_save.ini");
        if(valid)privateIdentities[&state]={pathString(directory/(name+"-preferences.ini")),pathString(saveRoot)};
        return valid;};
    const auto make=[&](const std::string& name){
        auto storage=std::make_unique<RuntimeWindowState>();auto& state=*storage;RuntimeSnapshotActiveStateScope active(state);
        state.config=prototype->config;state.report=prototype->report;state.world=prototype->world;state.rules=prototype->rules;state.art=prototype->art;
        state.theater=prototype->theater;state.mapFacts=prototype->mapFacts;state.rulesLoaded=state.artLoaded=state.theaterLoaded=true;
        state.mapFactsLoaded=prototype->mapFactsLoaded;state.economy=prototype->economy;state.terrainInfoByCell=prototype->terrainInfoByCell;
        state.terrainByCell=buildRuntimeTerrainLookup(state.world);state.triggers.bootstrapComplete=true;state.tick=7;state.visionEnabled=false;
        state.runtimeSoundsEnabled=state.runtimeMusicEnabled=state.runtimeSoundEffectsEnabled=false;state.runtimeSuppressPlayerAudioFeedback=true;
        state.runtimeMusicVolume=state.runtimeSoundVolume=state.runtimeVoiceVolume=0;state.runtimePreferencePersistenceEnabled=false;
        state.config.keyboardReferenceLayout=true;state.config.keyBindings.clear();
        initializeRuntimeNavigation(state);initializeRuntimeOfflineGameplayRouting(state);ensureRuntimeMenuKeyboardLoaded(state);
        state.runtimeMenuOpen=false;state.runtimeMenuKeyCaptureIndex=-1;state.activeSidebarTool=RuntimeSidebarTool::None;
        state.playerLocalCommands.followTargeting=state.playerLocalCommands.rallyTargeting=false;state.playerCommunication.beaconTargeting=false;
        state.leftPointer.active=state.leftPointer.moved=false;state.repaintRequested=false;state.interactionStatus="session initial";
        if(!isolate(state,name))return std::unique_ptr<RuntimeWindowState>{};return storage;};
    const auto oldSession=[&](RuntimeWindowState* state,HWND window,WPARAM wParam,LPARAM lParam)->LRESULT {
        if (state->skirmishMatchFinished
            && (wParam == VK_RETURN || wParam == 'R' || wParam == VK_ESCAPE)) {
            // The persistent menu host is recreated by runRuntimeWindow after
            // this mission window closes. Campaign progress already points at
            // the next mission on victory or the same mission on defeat, so
            // the Campaign selector provides both next and retry paths.
            // A victorious mission's authored PostScore movie belongs after
            // this result screen, not at the moment the victory trigger fires.
            // Esc keeps its advertised direct-to-menu behavior; Enter/R
            // confirms the result and starts the optional ending.
            if (state->exitAfterPendingCampaignVideos) {
                return 0;
            }
            if ((lParam & (static_cast<LPARAM>(1) << 30)) != 0) return 0;
            if (state->resultPresentation.bannerVisible
                && (!state->campaignMode || wParam != VK_ESCAPE)) {
                beginRuntimeResultScorePage(*state, runtimeUiClockNow(*state));
                return 0;
            }
            if (state->campaignMode && wParam != VK_ESCAPE
                && queueRuntimeCampaignPostScore(*state)) {
                state->repaintRequested = true;
                return 0;
            }
            state->runtimeExitRequested = true;
            DestroyWindow(window);
            return 0;
        }

        if (state->skirmishMatchFinished) {
            if (!state->campaignMode && !state->resultPresentation.bannerVisible
                && (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_PRIOR || wParam == VK_NEXT)) {
                const int lastPage = std::max(0, (static_cast<int>(runtimeMatchScoreRows(*state).size()) - 1) / 8);
                state->resultPresentation.scorePage = std::clamp(state->resultPresentation.scorePage
                    + ((wParam == VK_LEFT || wParam == VK_PRIOR) ? -1 : 1), 0, lastPage);
                state->repaintRequested = true;
            }
            return 0;
        }
        const bool controlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const WPARAM optionsKey = static_cast<WPARAM>(
            runtimeMenuKeyboardBindingCode(
                *state,
                "Options",
                VK_ESCAPE
            )
        );
        const bool menuNavigationKey =
            (runtimePlayerCurrentKeyCode(wParam) == static_cast<int>(optionsKey))
            || (state->runtimeMenuOpen && wParam == VK_ESCAPE);
        if (menuNavigationKey) {
            if ((lParam & (static_cast<LPARAM>(1) << 30)) != 0) return 0;
            if (!state->runtimeMenuOpen && (state->playerCommunication.beaconTargeting
                || state->playerLocalCommands.followTargeting || state->playerLocalCommands.rallyTargeting
                || state->activeSidebarTool != RuntimeSidebarTool::None)) {
                state->playerCommunication.beaconTargeting = false;
                state->playerLocalCommands.followTargeting = state->playerLocalCommands.rallyTargeting = false;
                state->activeSidebarTool = RuntimeSidebarTool::None;
                state->leftPointer.active = state->leftPointer.moved = false;
                if (GetCapture() == window) ReleaseCapture();
                state->repaintRequested = true; InvalidateRect(window, nullptr, FALSE); return 0;
            }
            if (GetCapture() == window) {
                ReleaseCapture();
            }
            if (!state->runtimeMenuOpen
                && cancelRuntimeActiveSuperWeaponTargetSelection(
                    *state,
                    wParam == VK_ESCAPE ? "escape" : "options"
                )) {
                state->repaintRequested = true;
                RedrawWindow(
                    window,
                    nullptr,
                    nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
                );
                return 0;
            }
            if (!state->runtimeMenuOpen && dismissRuntimePlacementFromUi(*state)) {
                state->repaintRequested = true;
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            bool openingMenu = false;
            if (!state->runtimeMenuOpen) {
                setRuntimeMenuOpen(*state, true);
                openingMenu = true;
            } else if (state->runtimeMenuPage != RuntimeMenuPage::Main) {
                resetRuntimeMenuPage(
                    *state,
                    runtimeMenuParentPage(state->runtimeMenuPage)
                );
            } else {
                setRuntimeMenuOpen(*state, false);
            }
            emitRuntimeSound(
                *state,
                openingMenu
                    ? runtimeSoundIntentMenuOpen
                    : runtimeSoundIntentMenuClose
            );
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }

        const int actualBindingCode = runtimePlayerCurrentKeyCode(wParam);
        const bool configuredGameplayBinding = std::any_of(
            state->runtimeMenuKeyboardBindings.begin(), state->runtimeMenuKeyboardBindings.end(),
            [&](const auto& binding) {
                return binding.first != "Options" && binding.second == actualBindingCode;
            });
        if (!configuredGameplayBinding
            && ((controlDown && (wParam == 'S' || wParam == 'L'))
                || wParam == VK_F5 || wParam == VK_F9)) {
            std::string errorMessage;
            const bool save =
                (controlDown && wParam == 'S') || wParam == VK_F5;
            const bool ok = dispatchRuntimeSaveLoadFromUi(
                *state,
                save,
                errorMessage
            );
            const bool multiplayer = state->multiplayerBridge != nullptr;
            state->interactionStatus = ok
                ? (multiplayer
                    ? std::string(save
                        ? "multiplayer save proposed"
                        : "multiplayer load proposed")
                    : std::string(save ? "saved " : "loaded ")
                        + runtimeDefaultSavePath(*state))
                : std::string(save ? "save failed: " : "load failed: ")
                    + errorMessage;
            if (!multiplayer) {
                state->economy.status = state->interactionStatus;
            }
            emitRuntimeSound(
                *state,
                ok ? (save ? "save" : "load") : "blocked"
            );
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }
        return 1;};

    // Only this standalone probe owns these hidden windows; their WM_DESTROY posts WM_QUIT.
    struct QuitCleanup { ~QuitCleanup(){MSG message{};while(PeekMessageW(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){};} } quitCleanup;
    const auto drainPrivateQuit=[](){MSG message{};while(PeekMessageW(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){};};
    const auto prefix=[&](RuntimeWindowState& state,HWND window,UINT message,WPARAM raw,bool repeated){
        runtimeInvalidateVisionTerrainBatchForMessage(state.world);
        if((raw==VK_CONTROL||raw==VK_LCONTROL||raw==VK_RCONTROL)&&!state.forceAttackHeld){
            state.forceAttackHeld=true;state.repaintRequested=true;RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_NOERASE);}
        if(state.campaignMode&&state.triggers.campaignInputLocked&&!state.skirmishMatchFinished&&!state.runtimeMenuOpen&&message==WM_KEYDOWN)return true;
        if(state.skirmishMatchFinished&&GetCapture()==window)ReleaseCapture();
        const bool wasDiplomacyOpen=state.playerCommunication.diplomacyOpen;
        if(!state.runtimeMenuOpen&&!state.skirmishMatchFinished&&handleRuntimePlayerModalKey(state,static_cast<int>(raw),repeated)){
            if(wasDiplomacyOpen&&!state.playerCommunication.diplomacyOpen&&GetCapture()==window)ReleaseCapture();
            InvalidateRect(window,nullptr,FALSE);return true;}
        if(handleRuntimeMenuKeyCapture(state,raw,repeated)){state.repaintRequested=true;
            RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_NOERASE);return true;}
        return false;};
    const auto oldDrive=[&](RuntimeWindowState& state,HWND window,UINT message,WPARAM raw,bool repeated){
        if(prefix(state,window,message,raw,repeated))return true;
        if(oldSession(&state,window,raw,repeated?(static_cast<LPARAM>(1)<<30):0)==0)return true;
        RuntimeMenuNavigationUiHost menu(state,window);
        return handleRuntimeMenuNavigationKey({static_cast<std::uint64_t>(raw),repeated},menu);};
    const auto directDrive=[&](RuntimeWindowState& state,HWND window,UINT message,WPARAM raw,bool repeated){
        if(prefix(state,window,message,raw,repeated))return true;
        const RuntimeSessionKeyInput input{static_cast<std::uint64_t>(raw),repeated};
        RuntimeResultSessionKeyUiHost result(state,window);if(handleRuntimeResultSessionKey(input,result))return true;
        const bool controlDown=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        RuntimeOptionsSessionKeyUiHost options(state,window);if(handleRuntimeOptionsSessionKey(input,options))return true;
        RuntimeSaveLoadSessionKeyUiHost files(state,window);if(handleRuntimeSaveLoadSessionKey(input,controlDown,files))return true;
        RuntimeMenuNavigationUiHost menu(state,window);
        return handleRuntimeMenuNavigationKey({static_cast<std::uint64_t>(raw),repeated},menu);};
    const auto readFile=[](const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());};
    const auto clearSessionModes=[](RuntimeWindowState& state){
        state.runtimeMenuOpen=false;state.runtimeMenuKeyCaptureIndex=-1;state.playerCommunication.chatOpen=state.playerCommunication.diplomacyOpen=false;
        state.playerCommunication.beaconTargeting=false;state.playerLocalCommands.followTargeting=state.playerLocalCommands.rallyTargeting=false;
        state.activeSidebarTool=RuntimeSidebarTool::None;state.leftPointer.active=state.leftPointer.moved=false;
        state.rightPointer.active=state.rightPointer.moved=false;state.runtimeMenuPressed=state.runtimeMenuSliderDrag=RuntimeMenuAction::None;
        state.repaintRequested=false;state.runtimeSoundEvents=0;state.runtimeSoundIntentHistory.clear();state.runtimeSoundPlaybackHistory.clear();state.lastRuntimeSound.clear();};
    const auto removeBindingCollision=[](RuntimeWindowState& state,WPARAM key){
        const int code=runtimePlayerCurrentKeyCode(key),replacement=runtimePlayerCurrentKeyCode(VK_F24);
        for(auto& binding:state.runtimeMenuKeyboardBindings)if(binding.second==code)binding.second=replacement;};
    const auto drive=[&](const std::string& label,RuntimeWindowState& old,RuntimeWindowState& direct,RuntimeWindowState& native,
        RuntimePlayerDefeatNoticeSmokeWindow& ow,RuntimePlayerDefeatNoticeSmokeWindow& dw,RuntimePlayerDefeatNoticeSmokeWindow& nw,
        WPARAM raw,bool repeated,UINT message,bool capture=false,bool zeroAuthority=true){
        Clock oc,dc,nc;ClockBinding ob(old,oc),db(direct,dc),nb(native,nc);Witness a(old),b(direct),c(native);
        drainPrivateQuit();
        if(!expect(fullState(old,true)==fullState(direct,true)&&fullState(old,true)==fullState(native,true),label+" same full entry except private identities"))return false;
        const auto beforeAuthority=authority(old);bool oldHandled=false,directHandled=false;LRESULT nativeResult=0;
        const auto establishCapture=[&](HWND window){if(!capture)return true;SetCapture(window);
            return expect(GetCapture()==window,label+" actual SetCapture/GetCapture prerequisite");};
        if(!establishCapture(ow.window))return false;
        {RuntimeSnapshotActiveStateScope active(old);oldHandled=oldDrive(old,ow.window,message,raw,repeated);}
        const bool oldCapture=GetCapture()==ow.window;drainPrivateQuit();
        if(!establishCapture(dw.window))return false;
        {RuntimeSnapshotActiveStateScope active(direct);directHandled=directDrive(direct,dw.window,message,raw,repeated);}
        const bool directCapture=GetCapture()==dw.window;drainPrivateQuit();
        if(!establishCapture(nw.window))return false;
        {RuntimeSnapshotActiveStateScope active(native);nativeResult=SendMessageW(nw.window,message,raw,repeated?(static_cast<LPARAM>(1)<<30):0);}
        const bool nativeCapture=GetCapture()==nw.window;drainPrivateQuit();
        if(fullState(old,true)!=fullState(native,true))output<<"Session mismatch label="<<label<<" old="<<hexText(fullState(old,false))<<" native="<<hexText(fullState(native,false))<<'\n';
        if(!expect(oldHandled&&directHandled&&nativeResult==0&&fullState(old,true)==fullState(direct,true)&&fullState(old,true)==fullState(native,true),
            label+" frozen old/same cpp/WindowProc complete state"))return false;
        if(!expect(oc.reads==dc.reads&&oc.reads==nc.reads&&a.captures==0&&b.captures==0&&c.captures==0,
            label+" equal actual clock effects and no gameplay authority captures"))return false;
        if(!expect(old.uiClock==&oc&&direct.uiClock==&dc&&native.uiClock==&nc,label+" borrowed provider survives original callbacks/load"))return false;
        if(!expect(IsWindow(ow.window)==IsWindow(dw.window)&&IsWindow(ow.window)==IsWindow(nw.window)
            &&oldCapture==directCapture&&oldCapture==nativeCapture,label+" same real window/capture lifetime"))return false;
        if(capture){if(!expect(!oldCapture&&!directCapture&&!nativeCapture,label+" actual synchronous ReleaseCapture"))return false;++captureReleases;}
        if(zeroAuthority&&!expect(authority(old)==beforeAuthority&&authority(direct)==beforeAuthority&&authority(native)==beforeAuthority,
            label+" world/tick/command queue unchanged"))return false;
        if(!IsWindow(nw.window))++realDestroyed;
        const auto print=[&](const char* path,const RuntimeWindowState& state,const Witness& witness,std::size_t reads,HWND window,bool captured){
            trace<<"session path="<<path<<" label="<<label<<" raw="<<static_cast<std::uint64_t>(raw)<<" repeat="<<repeated<<" message="<<message
                <<" captures="<<witness.captures<<" reads="<<reads<<" alive="<<(IsWindow(window)!=FALSE)<<" captured="<<captured
                <<" rawState="<<hexText(fullState(state,false))<<" normalizedState="<<hexText(fullState(state,true))<<'\n';};
        print("old",old,a,oc.reads,ow.window,oldCapture);print("direct",direct,b,dc.reads,dw.window,directCapture);print("native",native,c,nc.reads,nw.window,nativeCapture);
        sourceReads+=oc.reads+dc.reads+nc.reads;return true;};

    for(int mode=0;mode<12;++mode){
        const auto stem="result-"+std::to_string(mode);auto os=make(stem+"-old"),ds=make(stem+"-direct"),ns=make(stem+"-native");
        if(!expect(os&&ds&&ns,"result real worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        const bool repeated=mode==1||mode==9;
        const WPARAM raw=mode==0||mode==3?VK_RETURN:mode==1||mode==5||mode==6||mode==7||mode==8?'R':
            mode==10?VK_F14:mode==11?static_cast<WPARAM>((std::uint64_t{1}<<32)|VK_RETURN):VK_ESCAPE;
        for(auto* state:{&old,&direct,&native}){
            clearSessionModes(*state);state->skirmishMatchFinished=true;state->skirmishVictory=mode!=7;
            state->campaignMode=mode==4||mode==5||mode==6||mode==7||mode==8||mode==9;
            state->resultPresentation.started=true;state->resultPresentation.bannerVisible=mode==0||mode==1||mode==2||mode==4||mode==5;
            state->resultPresentation.scorePage=0;state->config.campaignId="gdi";
            state->config.campaignPostScoreVideoPath=mode==6||mode==7?"private-ending.bik":"";
            state->exitAfterPendingCampaignVideos=mode==2;
            if(mode==2)state->pendingCampaignVideoPaths.push_back("already-queued.bik");
        }
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,"result private HWNDs"))return false;
        if(!drive(stem,old,direct,native,ow,dw,nw,raw,repeated,mode%2?WM_SYSKEYDOWN:WM_KEYDOWN))return false;
        if((mode==0||mode==5)&&!expect(!old.resultPresentation.bannerVisible&&old.campaignScore.presentationStartedAtMs==123456
            &&IsWindow(nw.window),"banner enters actual score owner and samples injected UI clock"))return false;
        if((mode==3||mode==4||mode==7||mode==8)&&!expect(old.runtimeExitRequested&&!IsWindow(ow.window)&&!IsWindow(dw.window)&&!IsWindow(nw.window),
            "original terminal result really destroys only the three private windows"))return false;
        if(mode==6&&!expect(old.pendingCampaignVideoPaths==std::deque<std::string>{"private-ending.bik"}
            &&old.exitAfterPendingCampaignVideos&&!old.runtimeExitRequested&&IsWindow(nw.window),"real victorious campaign queue, no movie playback"))return false;
        if((mode==1||mode==2||mode==9||mode==10||mode==11)&&!expect(IsWindow(nw.window)&&!old.runtimeExitRequested,
            "pending/repeated/other/full-raw terminal key stays consumed without close"))return false;
        ++resultCases;
    }
    for(WPARAM raw:{WPARAM{VK_LEFT},WPARAM{VK_RIGHT},WPARAM{VK_PRIOR},WPARAM{VK_NEXT}})for(int initial:{-3,0,999}){
        const auto stem="score-page-"+std::to_string(raw)+"-"+std::to_string(initial);
        auto os=make(stem+"-old"),ds=make(stem+"-direct"),ns=make(stem+"-native");
        if(!expect(os&&ds&&ns,"score real worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native}){clearSessionModes(*state);state->skirmishMatchFinished=true;
            state->resultPresentation.started=true;state->resultPresentation.bannerVisible=false;state->resultPresentation.scorePage=initial;}
        const int lastPage=std::max(0,(static_cast<int>(runtimeMatchScoreRows(old).size())-1)/8);
        const int expectedPage=std::clamp(initial+((raw==VK_LEFT||raw==VK_PRIOR)?-1:1),0,lastPage);
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,"score private HWNDs"))return false;
        if(!drive(stem,old,direct,native,ow,dw,nw,raw,true,WM_KEYDOWN))return false;
        if(!expect(old.resultPresentation.scorePage==expectedPage&&old.repaintRequested&&IsWindow(nw.window),
            "actual real score-row count and original repeat paging clamp"))return false;++resultCases;
    }
    const auto setOptionsBinding=[&](RuntimeWindowState& state,WPARAM key){
        const int code=runtimePlayerCurrentKeyCode(key);bool found=false;
        for(auto& binding:state.runtimeMenuKeyboardBindings)if(binding.first=="Options"){binding.second=code;found=true;}
        return expect(found,"actual catalog contains exact Options binding id");};
    for(int mode=0;mode<17;++mode){
        const auto stem="options-"+std::to_string(mode);
        auto os=mode==14?makeBase(1,true):make(stem+"-old");
        auto ds=mode==14?makeBase(1,true):make(stem+"-direct");
        auto ns=mode==14?makeBase(1,true):make(stem+"-native");
        if(!expect(os&&ds&&ns,"Options real worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        const WPARAM raw=mode==15?VK_F13:mode==16?static_cast<WPARAM>((std::uint64_t{1}<<32)|VK_ESCAPE):VK_ESCAPE;
        int ordinal=0;
        for(auto* state:{&old,&direct,&native}){
            clearSessionModes(*state);
            if(mode==14){if(!expect(isolate(*state,stem+(ordinal==0?"-old":ordinal==1?"-direct":"-native")),"private ready-placement paths"))return false;
                ensureRuntimeMenuKeyboardLoaded(*state);}
            ++ordinal;if(!setOptionsBinding(*state,raw))return false;
            if(mode>=1&&mode<=6){state->runtimeMenuOpen=true;
                const RuntimeMenuPage pages[]{RuntimeMenuPage::Main,RuntimeMenuPage::Settings,RuntimeMenuPage::Keyboard,
                    RuntimeMenuPage::Display,RuntimeMenuPage::Sound,RuntimeMenuPage::ExitConfirm};
                resetRuntimeMenuPage(*state,pages[mode-1]);}
            if(mode==7||mode==12)state->playerCommunication.beaconTargeting=true;
            if(mode==8||mode==12)state->playerLocalCommands.followTargeting=true;
            if(mode==9||mode==12)state->playerLocalCommands.rallyTargeting=true;
            if(mode==10||mode==12)state->activeSidebarTool=RuntimeSidebarTool::Repair;
            if(mode==11)state->activeSidebarTool=RuntimeSidebarTool::Sell;
            if(mode==13||mode==16){std::string error;
                const bool begun=state->multiplayerIntentRouter.beginSuperWeapon("ChronoSphereSpecial",2,error);
                if(!expect(begun&&state->multiplayerIntentRouter.superWeapon().has_value(),"actual local superweapon target owner: "+error))return false;}
            if(mode==14){
                const auto* item=findRuntimeGameplayCatalogItem(*state,state->economy.owner,"GAPOWR");
                RuntimeProductionLane lane=RuntimeProductionLane::Structures;
                const auto* ready=item?runtimeReadyProductionForItem(*state,*item,&lane):nullptr;
                if(!expect(item&&ready,"actual ready placement catalog/order"))return false;
                const bool begun=beginRuntimePlacementFromUi(*state,{lane,*ready,item},true);
                if(!expect(begun&&runtimeSelectedPlacement(*state).has_value(),"actual ready-placement owner establishes selection"))return false;
            }
            state->leftPointer.active=state->leftPointer.moved=true;state->rightPointer.active=state->rightPointer.moved=true;
            state->runtimeMenuPressed=RuntimeMenuAction::Resume;state->runtimeMenuSliderDrag=RuntimeMenuAction::None;state->repaintRequested=false;
        }
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,"Options private HWNDs"))return false;
        if(!drive(stem,old,direct,native,ow,dw,nw,raw,false,mode%2?WM_SYSKEYDOWN:WM_KEYDOWN,true))return false;
        if(!expect(!old.rightPointer.active&&!old.rightPointer.moved&&old.runtimeMenuPressed==RuntimeMenuAction::None,
            "real ReleaseCapture delivered original WM_CAPTURECHANGED cleanup"))return false;
        if(mode>=7&&mode<=12&&!expect(!old.runtimeMenuOpen&&!old.playerCommunication.beaconTargeting
            &&!old.playerLocalCommands.followTargeting&&!old.playerLocalCommands.rallyTargeting
            &&old.activeSidebarTool==RuntimeSidebarTool::None&&!old.leftPointer.active&&!old.leftPointer.moved,
            "targeting short circuit keeps original complete cleanup"))return false;
        if((mode==13||mode==16)&&!expect(!old.multiplayerIntentRouter.superWeapon()&&!old.runtimeMenuOpen
            &&old.interactionStatus==std::string("superweapon target canceled: ")+(mode==16?"options":"escape"),
            "actual local superweapon cancellation uses complete raw-key reason"))return false;
        if(mode==14&&!expect(!runtimeSelectedPlacement(old).has_value()&&!old.runtimeMenuOpen
            &&old.interactionStatus=="building ready: click sidebar icon to place","actual placement dismissal consumes before menu"))return false;
        ++optionsCases;
    }
    for(bool targeting:{false,true}){
        const auto stem=targeting?"options-repeat-target":"options-repeat-menu";auto os=make(std::string(stem)+"-old"),ds=make(std::string(stem)+"-direct"),ns=make(std::string(stem)+"-native");
        if(!expect(os&&ds&&ns,"repeat Options worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native}){clearSessionModes(*state);if(!setOptionsBinding(*state,VK_ESCAPE))return false;
            state->playerCommunication.beaconTargeting=targeting;}
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,"repeat private HWNDs"))return false;
        if(!drive(stem,old,direct,native,ow,dw,nw,VK_ESCAPE,true,WM_SYSKEYDOWN))return false;
        if(!expect(!old.runtimeMenuOpen&&old.playerCommunication.beaconTargeting==targeting&&!old.repaintRequested,
            "repeat Options consumes without changing menu or targeting"))return false;++optionsCases;
    }

    {
        auto os=makeBase(1,true),ds=makeBase(1,true),ns=makeBase(1,true);
        if(!expect(os&&ds&&ns,"three proven legal active save worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        int role=0;for(auto* state:{&old,&direct,&native}){
            clearSessionModes(*state);const char* name=role==0?"save-old":role==1?"save-direct":"save-native";++role;
            if(!expect(isolate(*state,name),"private save root assigned only after real resource initialization"))return false;
            ensureRuntimeMenuKeyboardLoaded(*state);removeBindingCollision(*state,VK_F5);removeBindingCollision(*state,VK_F9);
            if(!setOptionsBinding(*state,VK_ESCAPE))return false;
            RuntimeSnapshotActiveStateScope active(*state);state->config.shortGame=true;state->tick=21;(void)advanceRuntimeParticles(*state);
            if(!expect(state->particlesInitialized&&state->particles.currentTick()==21,"actual active particles authority tick21"))return false;
        }
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,"save/load private HWNDs"))return false;
        for(int mode=0;mode<4;++mode){
            for(auto* state:{&old,&direct,&native}){
                ensureRuntimeMenuKeyboardLoaded(*state);removeBindingCollision(*state,VK_F5);removeBindingCollision(*state,VK_F9);
                if(!setOptionsBinding(*state,VK_ESCAPE))return false;
                const auto expectedRoot=directory/(state==&old?"save-old-save":state==&direct?"save-direct-save":"save-native-save");
                if(!expect(runtimeDefaultSavePath(*state)==pathString(expectedRoot/"runtime_save.ini")
                    &&runtimeMenuKeyboardBindingCode(*state,"Options",VK_ESCAPE)==runtimePlayerCurrentKeyCode(VK_ESCAPE),
                    "fresh-root actual shortcut binding and unchanged private default-save path"))return false;
                const int f5=runtimePlayerCurrentKeyCode(VK_F5),f9=runtimePlayerCurrentKeyCode(VK_F9);
                if(!expect(std::none_of(state->runtimeMenuKeyboardBindings.begin(),state->runtimeMenuKeyboardBindings.end(),
                    [&](const auto& binding){return binding.first!="Options"&&(binding.second==f5||binding.second==f9);}),
                    "fresh-root configured gameplay list does not shadow the selected real file shortcut"))return false;
            }
            const bool save=mode<2,repeated=mode==1||mode==3;const WPARAM raw=save?VK_F5:VK_F9;
            if(!drive(save?"actual-quick-save":"actual-quick-load",old,direct,native,ow,dw,nw,raw,repeated,
                mode%2?WM_SYSKEYDOWN:WM_KEYDOWN,false,false))return false;
            if(!expect(old.tick==21&&direct.tick==21&&native.tick==21&&native.particles.currentTick()==21
                &&old.interactionStatus.starts_with(save?"saved ":"loaded ")&&direct.interactionStatus.starts_with(save?"saved ":"loaded ")
                &&native.interactionStatus.starts_with(save?"saved ":"loaded ")
                &&old.economy.status==old.interactionStatus&&native.economy.status==native.interactionStatus,
                "original actual save/load succeeds, including repeated input and post-load status from fresh root"))return false;
            const auto a=readFile(runtimeDefaultSavePath(old)),b=readFile(runtimeDefaultSavePath(direct)),c=readFile(runtimeDefaultSavePath(native));
            if(!expect(!a.empty()&&canonical(a,old)==canonical(b,direct)&&canonical(a,old)==canonical(c,native),
                "actual save files match after only exact private path identity replacement"))return false;
            if(mode==0){std::ofstream saved(directory/"session-key-load.ini",std::ios::binary|std::ios::trunc);saved<<a;
                if(!expect(static_cast<bool>(saved),"retained actual old-route legal save artifact"))return false;}
            ++fileCases;
        }
        for(auto* state:{&old,&direct,&native})if(!setOptionsBinding(*state,VK_F13))return false;
        if(!drive("fresh-root-options-after-load",old,direct,native,ow,dw,nw,VK_F13,false,WM_KEYDOWN))return false;
        if(!expect(old.runtimeMenuOpen&&native.runtimeMenuOpen&&old.tick==21&&native.particles.currentTick()==21,
            "next key after scratch load reborrows current root"))return false;++optionsCases;
        trace<<"load tick=21 particleTick="<<native.particles.currentTick()<<" privateDefaultSave=1 actualQuickSave=1 actualQuickLoad=1 repeatedFileOperations=1 freshRoot=1\n";
    }
    for(int mode=0;mode<4;++mode){
        const auto stem="file-failure-"+std::to_string(mode);auto os=make(stem+"-old"),ds=make(stem+"-direct"),ns=make(stem+"-native");
        if(!expect(os&&ds&&ns,"file failure worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        const bool save=mode==2;const WPARAM raw=save?VK_F5:VK_F9;
        for(auto* state:{&old,&direct,&native}){
            clearSessionModes(*state);removeBindingCollision(*state,raw);if(!setOptionsBinding(*state,VK_ESCAPE))return false;
            if(!expect(!std::filesystem::exists(runtimeDefaultSavePath(*state)),"private default failure path initially absent"))return false;
            if(mode==1){std::ofstream invalid(runtimeDefaultSavePath(*state),std::ios::binary|std::ios::trunc);invalid<<"[InvalidFixture]\nNotASave=true\n";
                if(!expect(static_cast<bool>(invalid),"private malformed save file"))return false;}
            if(mode==2){std::filesystem::create_directory(runtimeDefaultSavePath(*state),ec);if(!expect(!ec,"private directory rejects ordinary file save"))return false;}
        }
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,"failed file operation HWNDs"))return false;
        if(!drive(stem,old,direct,native,ow,dw,nw,raw,mode==3,mode%2?WM_SYSKEYDOWN:WM_KEYDOWN))return false;
        if(!expect(old.interactionStatus.starts_with(save?"save failed: ":"load failed: ")
            &&old.economy.status==old.interactionStatus&&native.economy.status==native.interactionStatus,
            "real failed file owner preserves world/queue and reports original failure"))return false;
        ++fileCases;
    }
    for(bool save:{true,false}){
        const auto stem=save?"mp-rejected-save":"mp-rejected-load";auto os=make(std::string(stem)+"-old"),ds=make(std::string(stem)+"-direct"),ns=make(std::string(stem)+"-native");
        if(!expect(os&&ds&&ns,"real bridge file rejection worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        RuntimeMultiplayerRuntimeBridge oldBridge,directBridge,nativeBridge;
        RuntimeMultiplayerRuntimeBridge* bridges[]{&oldBridge,&directBridge,&nativeBridge};int index=0;
        const WPARAM raw=save?VK_F5:VK_F9;
        for(auto* state:{&old,&direct,&native}){
            clearSessionModes(*state);removeBindingCollision(*state,raw);if(!setOptionsBinding(*state,VK_ESCAPE))return false;
            auto* bridge=bridges[index++];if(!expect(!bridge->localPeerIsHost()||bridge->localPeerIsSpectator(),
                "actual bridge peer-role rejects file operation before any proposal"))return false;
            state->multiplayerBridge=bridge;state->config.multiplayerActive=true;state->economy.status="MP economy unchanged";
        }
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,"bridge rejection HWNDs"))return false;
        const bool matched=drive(stem,old,direct,native,ow,dw,nw,raw,false,WM_SYSKEYDOWN);
        for(auto* state:{&old,&direct,&native})state->multiplayerBridge=nullptr;
        if(!matched)return false;
        if(!expect(old.interactionStatus==std::string(save?"save failed: ":"load failed: ")+"only the host may save or load a multiplayer match"
            &&old.economy.status=="MP economy unchanged"&&!old.multiplayerPendingFileOperation&&!direct.multiplayerPendingFileOperation
            &&!native.multiplayerPendingFileOperation&&oldBridge.pendingGameplayCommandCount()==0&&nativeBridge.pendingGameplayCommandCount()==0,
            "real peer-role failure, post-dispatch MP economy remains unchanged, no fake proposal/pending"))return false;++fileCases;
    }
    for(int mode=0;mode<3;++mode){
        const auto stem="file-configured-"+std::to_string(mode);auto os=make(stem+"-old"),ds=make(stem+"-direct"),ns=make(stem+"-native");
        if(!expect(os&&ds&&ns,"configured binding worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        const WPARAM raw=mode==1?VK_F9:VK_F5;
        for(auto* state:{&old,&direct,&native}){clearSessionModes(*state);state->runtimeMenuOpen=true;resetRuntimeMenuPage(*state,RuntimeMenuPage::Keyboard);
            removeBindingCollision(*state,raw);if(!setOptionsBinding(*state,VK_ESCAPE))return false;
            state->runtimeMenuKeyboardBindings.emplace_back(mode==2?"options":"LiteralOther",runtimePlayerCurrentKeyCode(raw));state->repaintRequested=false;}
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,"configured binding HWNDs"))return false;
        if(!drive(stem,old,direct,native,ow,dw,nw,raw,true,WM_KEYDOWN))return false;
        if(!expect(old.interactionStatus=="session initial"&&!std::filesystem::exists(runtimeDefaultSavePath(old))
            &&old.runtimeMenuOpen&&old.runtimeMenuPage==RuntimeMenuPage::Keyboard,"exact non-Options binding blocks file shortcut before real menu consumes"))return false;++fileCases;
    }

    for(int mode=0;mode<8;++mode){
        const auto stem="guard-"+std::to_string(mode);auto os=make(stem+"-old"),ds=make(stem+"-direct"),ns=make(stem+"-native");
        if(!expect(os&&ds&&ns,"real upstream guard worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        const WPARAM raw=mode==5||mode==6?VK_RETURN:mode==7?VK_F5:VK_ESCAPE;
        for(auto* state:{&old,&direct,&native}){
            clearSessionModes(*state);if(!setOptionsBinding(*state,VK_ESCAPE))return false;
            if(mode==0)state->playerCommunication.chatOpen=true;
            if(mode==1)state->playerCommunication.diplomacyOpen=true;
            if(mode==2||mode==5||mode==7){state->runtimeMenuOpen=true;resetRuntimeMenuPage(*state,RuntimeMenuPage::Keyboard);
                state->runtimeMenuKeyCaptureIndex=0;}
            if(mode==3||mode==4){state->campaignMode=true;state->triggers.campaignInputLocked=true;}
            if(mode==5||mode==6){state->skirmishMatchFinished=true;state->resultPresentation.started=true;state->resultPresentation.bannerVisible=true;}
            if(mode==6)state->playerCommunication.chatOpen=true;
        }
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,"real guard HWNDs"))return false;
        if(!drive(stem,old,direct,native,ow,dw,nw,raw,false,mode==4?WM_SYSKEYDOWN:WM_KEYDOWN))return false;
        if((mode==0||mode==1)&&!expect(!old.runtimeMenuOpen&&!old.playerCommunication.chatOpen&&!old.playerCommunication.diplomacyOpen,
            "actual modal Escape closes modal before Options"))return false;
        if(mode==2&&!expect(old.runtimeMenuOpen&&old.runtimeMenuPage==RuntimeMenuPage::Keyboard&&old.runtimeMenuKeyCaptureIndex==-1,
            "real capture Escape cancels binding capture before Options parent"))return false;
        if(mode==3&&!expect(!old.runtimeMenuOpen&&old.interactionStatus=="session initial","campaign ordinary key guard consumes before session controller"))return false;
        if(mode==4&&!expect(old.runtimeMenuOpen,"original campaign guard does not block SYSKEY Options"))return false;
        if(mode==5&&!expect(IsWindow(nw.window)&&!old.runtimeExitRequested&&old.runtimeMenuKeyCaptureIndex==-1&&old.resultPresentation.bannerVisible,
            "binding capture wins terminal Return without destroying the private window"))return false;
        if(mode==6&&!expect(old.playerCommunication.chatOpen&&!old.resultPresentation.bannerVisible&&old.campaignScore.presentationStartedAtMs==123456,
            "terminal prevents ordinary modal route and enters actual score owner"))return false;
        if(mode==7&&!expect(old.runtimeMenuKeyCaptureIndex==-1&&!std::filesystem::exists(runtimeDefaultSavePath(old)),
            "real capture consumes F5 before shortcut file owner"))return false;
        ++guardCases;
    }
    trace<<"checks="<<checks<<" resultCases="<<resultCases<<" optionsCases="<<optionsCases<<" fileCases="<<fileCases
        <<" guards="<<guardCases<<" sourceReads="<<sourceReads<<" captureReleases="<<captureReleases<<" realDestroyed="<<realDestroyed<<'\n';
    trace.flush();if(!expect(static_cast<bool>(trace),"trace flush"))return false;
    output<<"Session-key integration passed: checks="<<checks<<" resultCases="<<resultCases<<" optionsCases="<<optionsCases<<" fileCases="<<fileCases
        <<" guards="<<guardCases<<" sourceReads="<<sourceReads<<" captureReleases="<<captureReleases<<" realDestroyed="<<realDestroyed
        <<" sameProductionCpp=1 frozenOldContinuousBlock=1 threeRealPrivateWindows=1 capturePrerequisite=SetCapture-and-GetCapture"
        <<" realCaptureChangedCallback=1 terminalPrivateDestroy=1 campaignPostScore=queued-not-played actualQuickSaveLoad=1 activeTick21=1 freshRootAfterLoad=1"
        <<" rawAndNormalizedState=retained pathNormalization=exact-route-private-identities nativeOrderedTrace=observable-complete-state standardOrderedQueries=separate-contract"
        <<" nativeModifierFullMatrix=not-run forcedCallbackRootReplacement=not-forced runningMPFileHandshake=not-run fullMpSnapshot=not-run externalNetwork=not-run"
        <<" audioDeviceContinuity=not-run moviePlayback=not-run fullIME=not-covered secondWindowBackend=not-run performance=not-run"
        <<" W012CaptureMatrix=not-covered observerEndSaveFailure=preserved originalOverflowDomain=not-covered\n";
    return true;
}
