// Native keyboard-ingress regressions; included after the real adapters and ordinary loader.
bool runRuntimeKeyboardIngressSmokeProbe(RuntimeWindowState& stock,std::ostream& output) {
    struct Clock final : RuntimeUiClock { RuntimeUiTimeMs value=123456;mutable std::size_t reads=0;
        RuntimeUiTimeMs nowMs() const noexcept override {++reads;return value;} };
    struct ClockBinding { RuntimeWindowState& state;const RuntimeUiClock* previous;
        ClockBinding(RuntimeWindowState& s,const RuntimeUiClock& c):state(s),previous(s.uiClock){s.uiClock=&c;}
        ~ClockBinding(){state.uiClock=previous;} };
    struct Captured { int tick=0;std::vector<std::uint8_t> payload;bool accepted=false;std::string error;
        bool operator==(const Captured&) const = default; };
    struct Witness { RuntimeWindowState& state;std::vector<Captured> commands;
        explicit Witness(RuntimeWindowState& s):state(s){
            state.multiplayerIntentRouter.setCaptureCallback([this](const RuntimeMultiplayerGameplayCommand& command,std::string& error){
                Captured item;item.tick=state.tick;
                if(!encodeRuntimeMultiplayerGameplayCommand(command,item.payload,error))return false;
                item.accepted=dispatchRuntimeOfflineGameplayCommand(state,command,error);item.error=error;
                commands.push_back(std::move(item));return commands.back().accepted;});}
        ~Witness(){state.multiplayerIntentRouter.setCaptureCallback({});} };
    std::size_t checks=0,admissionCases=0,ingressCases=0,guards=0,priorities=0,sourceReads=0,acceptedCaptures=0,loadCases=0,captureReleases=0,destroyed=0;
    const auto expect=[&](bool value,const std::string& why){++checks;if(!value)output<<"Keyboard-ingress integration failed: "<<why<<'\n';return value;};
    if(!expect(stock.report.success&&stock.world.getSummary().loaded&&stock.rulesLoaded&&stock.artLoaded&&stock.theaterLoaded,
        "actual stock resources"))return false;
    char* requested=nullptr;std::size_t requestedLength=0;
    const auto envResult=_dupenv_s(&requested,&requestedLength,"CONCORD_KEYBOARD_INGRESS_OUTPUT");
    const std::filesystem::path directory=requested&&*requested?std::filesystem::path(requested):std::filesystem::path("keyboard-ingress-smoke-output");
    std::free(requested);if(!expect(envResult==0,"isolated output environment"))return false;
    std::error_code ec;std::filesystem::create_directories(directory,ec);if(!expect(!ec,"isolated output directory"))return false;
    std::ofstream trace(directory/"keyboard-ingress-trace.txt",std::ios::out|std::ios::trunc);
    if(!expect(static_cast<bool>(trace),"trace file"))return false;
    const auto hexText=[](const std::string& value){std::ostringstream s;for(unsigned char c:value)
        s<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(c);return s.str();};
    const auto wideText=[](const std::wstring& value){std::ostringstream s;for(wchar_t c:value)s<<static_cast<std::uint32_t>(c)<<',';return s.str();};
    const auto chatText=[](const std::u16string& value){std::ostringstream s;for(char16_t c:value)s<<static_cast<std::uint32_t>(c)<<',';return s.str();};
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
    const auto baseState=[&](const RuntimeWindowState& state,bool normalized){
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
        state.leftPointer.active=state.leftPointer.moved=false;state.repaintRequested=false;state.interactionStatus="ingress initial";
        if(!isolate(state,name))return std::unique_ptr<RuntimeWindowState>{};return storage;};
    const auto projectionState=[](const RuntimeWindowState& state){
        std::ostringstream s;s<<state.projectedObjectsCacheValid<<':'<<state.projectedObjectsCacheSignature<<':'
            <<state.projectedObjectsCacheCameraX<<','<<state.projectedObjectsCacheCameraY<<','<<state.projectedObjectsCacheTileWidth<<','<<state.projectedObjectsCacheTileHeight
            <<':'<<state.projectedObjectsCacheViewport.left<<','<<state.projectedObjectsCacheViewport.top<<','<<state.projectedObjectsCacheViewport.right<<','<<state.projectedObjectsCacheViewport.bottom;
        for(const auto& p:state.projectedObjectsCache)s<<';'<<(p.object?p.object->instanceId:-1)<<':'<<p.bounds.left<<','<<p.bounds.top<<','<<p.bounds.right<<','<<p.bounds.bottom
            <<':'<<p.center.x<<','<<p.center.y<<','<<p.renderCenter.x<<','<<p.renderCenter.y<<','<<p.hasMobileSpriteBounds<<','<<p.hasBodyHitClip<<','<<p.observerType;
        return s.str();};
    const auto fullState=[&](const RuntimeWindowState& state,bool normalized){std::ostringstream s;s<<baseState(state,normalized);
        const auto& local=state.playerLocalCommands;for(std::size_t i=0;i<local.teams.size();++i){s<<";team"<<i<<'=';for(int id:local.teams[i])s<<id<<',';}
        for(std::size_t i=0;i<local.views.size();++i){s<<";view"<<i<<'=';if(local.views[i])s<<local.views[i]->x<<','<<local.views[i]->y;else s<<"none";}
        s<<";quick="<<local.lastSelectionKey<<','<<local.lastSelectionAtMs<<','<<local.veterancyRank<<','<<local.healthRank
            <<";waypoint="<<local.selectedWaypoint<<";coordinates="<<local.showCoordinates<<";activeTab="<<static_cast<int>(state.activeBuildTab)<<";pages=";
        for(int page:state.buildPageByTab)s<<page<<',';s<<";wheels=";for(int wheel:state.buildWheelRemainderByTab)s<<wheel<<',';
        s<<";projectionFull="<<projectionState(state)<<";mouse="<<state.mouseClientPointValid<<','<<state.mouseClientPoint.x<<','<<state.mouseClientPoint.y
            <<";selectionPoints="<<state.leftPointer.start.x<<','<<state.leftPointer.start.y<<','<<state.leftPointer.current.x<<','<<state.leftPointer.current.y
            <<";chatFull="<<state.playerCommunication.chatAllies<<','<<chatText(state.playerCommunication.chatDraft)<<','<<state.playerCommunication.beaconEditing
            <<','<<state.playerCommunication.diplomacyRow<<','<<state.playerCommunication.diplomacyPressedControl<<','<<state.playerCommunication.diplomacyHoveredControl
            <<','<<state.playerCommunication.chatRecipientsExplicit<<','<<static_cast<unsigned>(state.playerCommunication.chatRecipients);
        if(state.playerCommunication.selectedBeacon)s<<";selectedBeacon="<<state.playerCommunication.selectedBeacon->x<<','<<state.playerCommunication.selectedBeacon->y;
        for(const auto& b:state.playerCommunication.beacons)s<<";beaconFull="<<b.owner<<','<<b.cell.x<<','<<b.cell.y<<','<<b.untilTick<<','<<b.level<<','<<hexText(b.text);
        for(const auto& line:state.playerCommunication.lines)s<<";chatLine="<<line.owner<<','<<hexText(line.text)<<','<<line.allies<<','<<line.untilTick;
        std::map<int,int> cheer(state.playerCommunication.cheerUntilTick.begin(),state.playerCommunication.cheerUntilTick.end());
        for(const auto& [id,until]:cheer)s<<";cheer="<<id<<','<<until;
        for(const auto& [key,tick]:state.playerCommunication.nextFeedbackTick)s<<";feedback="<<key.first<<','<<static_cast<int>(key.second)<<','<<tick;
        if(state.playerLastRadarCell)s<<";radar="<<state.playerLastRadarCell->x<<','<<state.playerLastRadarCell->y;
        s<<";admission="<<state.campaignMode<<','<<state.triggers.campaignInputLocked<<','<<state.skirmishMatchFinished<<','<<state.runtimeMenuOpen
            <<";cameraDimensions="<<state.camera.tileWidth<<','<<state.camera.tileHeight;
        return s.str();};
    const auto setBinding=[&](RuntimeWindowState& state,const std::string& id,int code){
        bool found=false;for(auto& binding:state.runtimeMenuKeyboardBindings)if(binding.first==id){binding.second=code;found=true;}
        if(!found)state.runtimeMenuKeyboardBindings.emplace_back(id,code);
        return runtimeMenuKeyboardBindingCode(state,id,-1)==code;};
    const auto clean=[&](RuntimeWindowState& state){
        state.runtimeMenuOpen=false;state.runtimeMenuKeyCaptureIndex=-1;state.playerCommunication.chatOpen=state.playerCommunication.diplomacyOpen=false;
        state.playerCommunication.beaconTargeting=false;state.playerLocalCommands.followTargeting=state.playerLocalCommands.rallyTargeting=false;
        state.leftPointer.active=state.leftPointer.moved=state.rightPointer.active=state.rightPointer.moved=false;
        state.activeSidebarTool=RuntimeSidebarTool::None;state.runtimeMenuPressed=state.runtimeMenuSliderDrag=RuntimeMenuAction::None;
        state.selectedObjectIds=prototype->selectedObjectIds;for(auto& team:state.playerLocalCommands.teams)team=state.selectedObjectIds;
        for(auto& view:state.playerLocalCommands.views)view=RuntimeMultiplayerGameplayCell{state.economy.skirmishStartCell.x,state.economy.skirmishStartCell.y};
        state.playerLastRadarCell=RuntimeMultiplayerGameplayCell{state.economy.skirmishStartCell.x,state.economy.skirmishStartCell.y};
        state.playerLocalCommands.lastSelectionKey='?';state.playerLocalCommands.lastSelectionAtMs=777;
        state.playerLocalCommands.veterancyRank=1;state.playerLocalCommands.healthRank=2;
        state.playerLocalCommands.cameraFollow=true;state.repaintRequested=false;state.interactionStatus="ingress initial";
        state.runtimeSoundEvents=0;state.runtimeSoundIntentHistory.clear();state.runtimeSoundPlaybackHistory.clear();state.lastRuntimeSound.clear();};
    // Independent original production fragments. Native null/default logic is retained verbatim.
    const auto oldAdmission=[](RuntimeWindowState* campaignState,UINT message)->LRESULT {
    if (campaignState != nullptr
        && campaignState->campaignMode
        && campaignState->triggers.campaignInputLocked
        && !campaignState->skirmishMatchFinished
        && !campaignState->runtimeMenuOpen) {
        switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_CHAR:
            return 0;
        default:
            break;
        }
    }
        return 1;
    };
    const auto oldIngress=[](HWND window,UINT message,WPARAM wParam,LPARAM lParam)->LRESULT {
        switch(message){
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state == nullptr) {
            return DefWindowProcW(window, message, wParam, lParam);
        }

        const bool wasDiplomacyOpen = state->playerCommunication.diplomacyOpen;
        if (!state->runtimeMenuOpen && !state->skirmishMatchFinished
            && handleRuntimePlayerModalKey(*state, static_cast<int>(wParam),
                (lParam & (static_cast<LPARAM>(1) << 30)) != 0)) {
            if (wasDiplomacyOpen && !state->playerCommunication.diplomacyOpen && GetCapture() == window)
                ReleaseCapture();
            InvalidateRect(window, nullptr, FALSE); return 0;
        }
        if (handleRuntimeMenuKeyCapture(*state, wParam,
                (lParam & (static_cast<LPARAM>(1) << 30)) != 0)) {
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }

        {
            const RuntimeSessionKeyInput sessionKey{
                static_cast<std::uint64_t>(wParam),
                (lParam & (static_cast<LPARAM>(1) << 30)) != 0
            };
            RuntimeResultSessionKeyUiHost resultHost(*state, window);
            if (::handleRuntimeResultSessionKey(sessionKey, resultHost)) return 0;
            const bool controlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            RuntimeOptionsSessionKeyUiHost optionsHost(*state, window);
            if (::handleRuntimeOptionsSessionKey(sessionKey, optionsHost)) return 0;
            RuntimeSaveLoadSessionKeyUiHost saveLoadHost(*state, window);
            if (::handleRuntimeSaveLoadSessionKey(sessionKey, controlDown, saveLoadHost)) return 0;
        }
        {
            RuntimeMenuNavigationUiHost host(*state, window);
            const RuntimeMenuKeyInput input{
                static_cast<std::uint64_t>(wParam),
                (lParam & (static_cast<LPARAM>(1) << 30)) != 0
            };
            if (::handleRuntimeMenuNavigationKey(input, host)) return 0;
        }

        if (handleRuntimePlayerCommandKey(*state, window, wParam, lParam)) {
            if (state->playerCommunication.diplomacyOpen && GetCapture() == window) ReleaseCapture();
            state->repaintRequested = true;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        {
            RuntimeSelectionKeyWin32Host host(*state, wParam);
            const RuntimeSelectionKeyInput input{
                static_cast<std::int32_t>(wParam),
                (lParam & (static_cast<LPARAM>(1) << 30)) != 0,
                message == WM_SYSKEYDOWN
                    ? RuntimeKeyDownSource::SystemKeyDown
                    : RuntimeKeyDownSource::KeyDown
            };
            const auto result = handleRuntimeSelectionKey(input, host);
            if (result == RuntimeSelectionKeyResult::RepeatedConsumed) return 0;
            if (result == RuntimeSelectionKeyResult::DispatchedConsumed) {
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

        {
            RuntimeCameraKeyWin32Host host(*state, window);
            const RuntimeCameraKeyInput input{
                static_cast<std::uint64_t>(wParam),
                (lParam & (static_cast<LPARAM>(1) << 30)) != 0
            };
            if (handleRuntimeCameraKey(input, host)) {
                state->repaintRequested = true;
                RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
                return 0;
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
        default:return DefWindowProcW(window,message,wParam,lParam);
        }
    };
    struct QuitCleanup{~QuitCleanup(){MSG msg{};while(PeekMessageW(&msg,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){};}} quitCleanup;
    const auto commonPrefix=[](RuntimeWindowState* state,HWND window,UINT message,WPARAM raw){
        if(state)runtimeInvalidateVisionTerrainBatchForMessage(state->world);
        if(state){RuntimeForceAttackHeldUiHost host(*state,window);handleRuntimeForceAttackHeld(runtimeForceAttackInputForMessage(message,raw),host);}
        if(state&&state->rightPointer.active&&(message==WM_KILLFOCUS||message==WM_CANCELMODE||(message==WM_ACTIVATEAPP&&raw==FALSE))){
            state->rightPointer.active=state->rightPointer.moved=false;state->repaintRequested=true;if(GetCapture()==window)ReleaseCapture();}
    };
    const auto directAdmission=[](RuntimeWindowState* state,UINT message){
        if(!state)return false;RuntimeCampaignInputAdmissionUiHost host(*state);
        return rejectRuntimeCampaignInput(runtimeCampaignInputEventForMessage(message),host);};
    const auto drive=[&](HWND window,UINT message,WPARAM raw,LPARAM flags,bool old){
        auto* state=reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        commonPrefix(state,window,message,raw);
        if(old?oldAdmission(state,message)==0:directAdmission(state,message))return LRESULT{0};
        if(state&&state->skirmishMatchFinished&&GetCapture()==window)ReleaseCapture();
        if(old)return oldIngress(window,message,raw,flags);
        if(!state||(message!=WM_KEYDOWN&&message!=WM_SYSKEYDOWN))return DefWindowProcW(window,message,raw,flags);
        RuntimeKeyboardIngressUiHost host(*state,window,flags);
        const RuntimeKeyboardIngressInput input{static_cast<std::uint64_t>(raw),(flags&(static_cast<LPARAM>(1)<<30))!=0,
            message==WM_SYSKEYDOWN?RuntimeKeyDownSource::SystemKeyDown:RuntimeKeyDownSource::KeyDown};
        return handleRuntimeKeyboardIngressKey(input,host)?LRESULT{0}:DefWindowProcW(window,message,raw,flags);};
    const auto captureText=[&](const Witness& witness){std::ostringstream s;for(const auto& c:witness.commands){
        const std::string bytes(c.payload.begin(),c.payload.end());s<<c.tick<<':'<<hexText(bytes)<<':'<<c.accepted<<':'<<hexText(c.error)<<';';}return s.str();};
    struct CaptureReadback {
        HWND window=nullptr;WNDPROC original=nullptr;std::function<std::string()> observe;std::vector<std::pair<std::string,std::string>> frames;
        std::function<std::array<bool,8>()> observeFlags;std::vector<std::array<bool,8>> beforeFlags;
        static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM raw,LPARAM flags){
            auto* self=static_cast<CaptureReadback*>(GetPropW(window,L"ConcordKeyboardIngressCaptureReadback"));
            if(!self||!self->original)return DefWindowProcW(window,message,raw,flags);
            std::string before;if(message==WM_CAPTURECHANGED){before=self->observe();self->beforeFlags.push_back(self->observeFlags());}
            const auto result=CallWindowProcW(self->original,window,message,raw,flags);
            if(message==WM_CAPTURECHANGED)self->frames.emplace_back(std::move(before),self->observe());return result;
        }
        bool attach(HWND target,std::function<std::string()> reader,std::function<std::array<bool,8>()> flags){window=target;observe=std::move(reader);observeFlags=std::move(flags);
            if(!SetPropW(window,L"ConcordKeyboardIngressCaptureReadback",this))return false;
            original=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&CaptureReadback::procedure)));
            return original!=nullptr;}
        ~CaptureReadback(){if(window&&IsWindow(window)){if(original)SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(original));RemovePropW(window,L"ConcordKeyboardIngressCaptureReadback");}}
    };
    const auto triple=[&](const std::string& label,RuntimeWindowState& old,RuntimeWindowState& direct,RuntimeWindowState& native,
        WPARAM raw,bool repeated,UINT message,bool captured,bool zeroAuthority,bool nullRoot=false,LPARAM extraFlags=0){
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,label+" private actual HWNDs"))return false;
        Clock oc,dc,nc;ClockBinding ob(old,oc),db(direct,dc),nb(native,nc);Witness a(old),b(direct),c(native);
        CaptureReadback oa,da,na;
        const auto callbackFlags=[](const RuntimeWindowState& s){return std::array<bool,8>{s.playerCommunication.diplomacyOpen,s.runtimeMenuOpen,
            s.playerCommunication.beaconTargeting,s.playerLocalCommands.followTargeting,s.playerLocalCommands.rallyTargeting,
            s.leftPointer.active,s.leftPointer.moved,s.skirmishMatchFinished};};
        if(!expect(oa.attach(ow.window,[&](){return fullState(old,true);},[&](){return callbackFlags(old);})
            &&da.attach(dw.window,[&](){return fullState(direct,true);},[&](){return callbackFlags(direct);})
            &&na.attach(nw.window,[&](){return fullState(native,true);},[&](){return callbackFlags(native);}),label+" private callback readback prerequisite"))return false;
        if(!expect(fullState(old,true)==fullState(direct,true)&&fullState(old,true)==fullState(native,true),label+" complete entry state"))return false;
        const auto beforeAuthority=authority(old);const LPARAM flags=extraFlags|(repeated?(static_cast<LPARAM>(1)<<30):0);
        const auto prepare=[&](HWND window){if(nullRoot)SetWindowLongPtrW(window,GWLP_USERDATA,0);
            if(!captured)return true;SetCapture(window);return expect(GetCapture()==window,label+" actual current route SetCapture/GetCapture prerequisite");};
        if(!prepare(ow.window))return false;LRESULT orv=0,drv=0,nrv=0;
        {RuntimeSnapshotActiveStateScope active(old);orv=drive(ow.window,message,raw,flags,true);}
        const bool ocapture=GetCapture()==ow.window;
        if(!prepare(dw.window))return false;{RuntimeSnapshotActiveStateScope active(direct);drv=drive(dw.window,message,raw,flags,false);}
        const bool dcapture=GetCapture()==dw.window;
        if(!prepare(nw.window))return false;{RuntimeSnapshotActiveStateScope active(native);nrv=SendMessageW(nw.window,message,raw,flags);}
        const bool ncapture=GetCapture()==nw.window;
        if(nullRoot){SetWindowLongPtrW(ow.window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&old));SetWindowLongPtrW(dw.window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&direct));SetWindowLongPtrW(nw.window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&native));}
        if(fullState(old,true)!=fullState(native,true))output<<"Ingress mismatch label="<<label<<" old="<<hexText(fullState(old,false))<<" native="<<hexText(fullState(native,false))<<'\n';
        if(!expect(orv==drv&&orv==nrv&&fullState(old,true)==fullState(direct,true)&&fullState(old,true)==fullState(native,true),label+" complete original/direct/WindowProc state and return"))return false;
        if(!expect(old.uiClock==&oc&&direct.uiClock==&dc&&native.uiClock==&nc&&a.commands==b.commands&&a.commands==c.commands&&oc.reads==dc.reads&&oc.reads==nc.reads
            &&oa.frames==da.frames&&oa.frames==na.frames&&oa.beforeFlags==da.beforeFlags&&oa.beforeFlags==na.beforeFlags&&ocapture==dcapture&&ocapture==ncapture
            &&IsWindow(ow.window)==IsWindow(dw.window)&&IsWindow(ow.window)==IsWindow(nw.window),label+" real ordered canonical commands/ticks/callback frames/source counts/window lifetime"))return false;
        if(zeroAuthority&&!expect(authority(old)==beforeAuthority&&authority(direct)==beforeAuthority&&authority(native)==beforeAuthority&&a.commands.empty(),label+" original world/tick/queue unchanged"))return false;
        if(captured){if(!expect(!ocapture&&!dcapture&&!ncapture&&!oa.frames.empty(),label+" actual synchronous capture release"))return false;++captureReleases;}
        if(captured&&label=="modal-5"&&!expect(!oa.beforeFlags.front()[0],"diplomacy is already closed at synchronous capture callback"))return false;
        if(captured&&label.starts_with("options-")&&!expect(!oa.beforeFlags.front()[1]&&!oa.beforeFlags.front()[2]&&!oa.beforeFlags.front()[3]
            &&!oa.beforeFlags.front()[4]&&!oa.beforeFlags.front()[5]&&!oa.beforeFlags.front()[6],"all original targeting/drag fields are cleared before ReleaseCapture and menu transition"))return false;
        if(captured&&label=="command-diplomacy"&&!expect(oa.beforeFlags.front()[0],"newly opened diplomacy is current before conditional Command ReleaseCapture"))return false;
        if(label.starts_with("selection-")&&!expect(old.playerLocalCommands.cameraFollow&&old.repaintRequested==!repeated
            &&direct.playerLocalCommands.cameraFollow&&direct.repaintRequested==!repeated
            &&native.playerLocalCommands.cameraFollow&&native.repaintRequested==!repeated,
            "Selection consumes before camera; repeated result adds no repaint, dispatched result does"))return false;
        if(!IsWindow(nw.window))++destroyed;for(const auto& item:a.commands)if(item.accepted)++acceptedCaptures;
        const auto print=[&](const char* path,const RuntimeWindowState& state,const Clock& clock,const Witness& witness,const CaptureReadback& callback,HWND window){
            trace<<"ingress path="<<path<<" label="<<label<<" raw="<<static_cast<std::uint64_t>(raw)<<" repeat="<<repeated<<" message="<<message
                <<" flags="<<static_cast<std::uint64_t>(flags)<<" result="<<nrv<<" reads="<<clock.reads<<" captures="<<witness.commands.size()<<" commands="<<captureText(witness)<<" captureChanged="<<callback.frames.size()
                <<" alive="<<(IsWindow(window)!=FALSE)<<" rawState="<<hexText(fullState(state,false))<<" normalizedState="<<hexText(fullState(state,true));
            for(const auto& frame:callback.frames)trace<<" callbackBefore="<<hexText(frame.first)<<" callbackAfter="<<hexText(frame.second);
            for(const auto& flags:callback.beforeFlags){trace<<" callbackFlags=";for(bool value:flags)trace<<value;}trace<<'\n';};
        print("old",old,oc,a,oa,ow.window);print("direct",direct,dc,b,da,dw.window);print("native",native,nc,c,na,nw.window);
        if(label=="modal-0"||label=="capture-0"||label=="options-2"||label=="menu-0"||label=="command-diplomacy"||label=="guard-5")++priorities;
        sourceReads+=oc.reads+dc.reads+nc.reads;++ingressCases;return true;};
    const auto disableInputCollisions=[&](RuntimeWindowState& state,WPARAM raw){
        ensureRuntimeMenuKeyboardLoaded(state);const int code=runtimePlayerCurrentKeyCode(raw);
        for(auto& binding:state.runtimeMenuKeyboardBindings)if(binding.second==code)binding.second=0x7FE;
        return setBinding(state,"Options",VK_ESCAPE);};    // All eighteen original native kinds are first exercised under the real blocked campaign guard.
    constexpr UINT blockedMessages[]={WM_LBUTTONDOWN,WM_LBUTTONUP,WM_LBUTTONDBLCLK,WM_RBUTTONDOWN,WM_RBUTTONUP,WM_RBUTTONDBLCLK,
        WM_MBUTTONDOWN,WM_MBUTTONUP,WM_MBUTTONDBLCLK,WM_XBUTTONDOWN,WM_XBUTTONUP,WM_XBUTTONDBLCLK,WM_MOUSEMOVE,WM_MOUSEWHEEL,WM_MOUSEHWHEEL,
        WM_KEYDOWN,WM_KEYUP,WM_CHAR};
    for(std::size_t i=0;i<std::size(blockedMessages);++i){
        const auto label="admission-blocked-"+std::to_string(i);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        const auto message=blockedMessages[i];const WPARAM raw=message==WM_KEYDOWN?VK_F13:message==WM_KEYUP?VK_CONTROL:message==WM_CHAR?'Z':MK_CONTROL;
        for(auto* state:{&old,&direct,&native}){clean(*state);state->campaignMode=true;state->triggers.campaignInputLocked=true;
            state->playerCommunication.chatOpen=message==WM_CHAR;
            if(!expect(disableInputCollisions(*state,VK_F13)&&setBinding(*state,"ShowCoordinates",runtimePlayerCurrentKeyCode(VK_F13)),"admission visible key side-effect precondition"))return false;}
        if(!triple(label,old,direct,native,raw,false,message,false,true))return false;
        if(!expect(!old.playerLocalCommands.showCoordinates&&old.playerCommunication.chatDraft.empty(),"blocked key/text leave original downstream consumer untouched"))return false;++admissionCases;
    }
    // Unblocked pointer events prove only the admission result, not W010 downstream equivalence.
    for(int mode=0;mode<4;++mode){
        const auto label="admission-allowed-pointer-"+std::to_string(mode);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native}){clean(*state);state->campaignMode=mode!=0;state->triggers.campaignInputLocked=mode!=1;
            state->skirmishMatchFinished=mode==2;state->runtimeMenuOpen=mode==3;}
        if(!expect(oldAdmission(&old,WM_MOUSEMOVE)==1&&!directAdmission(&direct,WM_MOUSEMOVE),"each original campaign guard permits pointer input"))return false;
        RuntimePlayerDefeatNoticeSmokeWindow window(native,1024,768);if(!expect(window.window!=nullptr,"allowed pointer actual HWND"))return false;
        const auto before=authority(native);Witness witness(native);Clock clock;ClockBinding binding(native,clock);
        {RuntimeSnapshotActiveStateScope active(native);SendMessageW(window.window,WM_MOUSEMOVE,0,MAKELPARAM(-20,-20));}
        if(!expect(authority(native)==before&&witness.commands.empty(),"allowed pointer witness preserves actual original world/tick/queue"))return false;
        trace<<"allowedPointer mode="<<mode<<" oldReject=0 directReject=0 scope=admission-only nativeW010Effects=retained rawState="<<hexText(fullState(native,false))<<'\n';++admissionCases;
    }
    for(int mode=0;mode<6;++mode){
        const auto label="modal-"+std::to_string(mode);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        const WPARAM raw=mode==0||mode==5?VK_ESCAPE:mode==1?VK_BACK:VK_RETURN;
        for(auto* state:{&old,&direct,&native}){clean(*state);state->playerCommunication.chatOpen=mode!=5;state->playerCommunication.diplomacyOpen=mode==5;
            state->playerCommunication.chatDraft=mode==1?std::u16string{u'A',char16_t(0xD83D),char16_t(0xDE00)}:mode==4?u"":u" ingress hello ";}
        if(!triple(label,old,direct,native,raw,mode==3,mode%2?WM_SYSKEYDOWN:WM_KEYDOWN,mode==5,mode!=2))return false;
        if(mode==0&&!expect(!old.playerCommunication.chatOpen&&old.playerCommunication.chatDraft.empty(),"real modal Escape wins Options"))return false;
        if(mode==1&&!expect(old.playerCommunication.chatDraft==u"A","existing Backspace removes real surrogate pair unchanged"))return false;
        if(mode==2&&!expect(!old.playerCommunication.chatOpen,"actual old chat submit succeeds before all later consumers"))return false;
        if(mode==3&&!expect(old.playerCommunication.chatOpen&&!old.playerCommunication.chatDraft.empty(),"repeated modal Return consumes without submit"))return false;
        if(mode==4&&!expect(!old.playerCommunication.chatOpen,"actual empty chat closes without authority command"))return false;
        if(mode==5&&!expect(!old.playerCommunication.diplomacyOpen,"closed diplomacy releases actual own capture before invalidate"))return false;
    }
    for(int mode=0;mode<4;++mode){
        const auto label="capture-"+std::to_string(mode);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        const WPARAM raw=mode==0?VK_ESCAPE:mode==1?VK_F13:mode==2?VK_BACK:VK_CONTROL;
        for(auto* state:{&old,&direct,&native}){clean(*state);state->runtimeMenuOpen=true;resetRuntimeMenuPage(*state,RuntimeMenuPage::Keyboard);
            state->runtimeMenuKeyCaptureIndex=0;if(mode==0){state->skirmishMatchFinished=true;state->resultPresentation.bannerVisible=true;}
            if(!expect(!state->runtimeMenuKeyboardBindings.empty(),"real capture catalog exists"))return false;}
        if(!triple(label,old,direct,native,raw,mode==1,mode%2?WM_SYSKEYDOWN:WM_KEYDOWN,false,true))return false;
        if(mode==0&&!expect(old.runtimeMenuKeyCaptureIndex==-1&&!old.runtimeExitRequested&&old.resultPresentation.bannerVisible,"capture Escape precedes terminal Result"))return false;
        if(mode==3&&!expect(old.runtimeMenuKeyCaptureIndex==0,"real modifier-only capture stays active before session"))return false;
    }
    for(int mode=0;mode<4;++mode){
        const auto label="result-"+std::to_string(mode);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native}){clean(*state);state->skirmishMatchFinished=true;state->resultPresentation.started=true;state->resultPresentation.bannerVisible=mode<2;}
        if(!triple(label,old,direct,native,mode==3?VK_RIGHT:VK_RETURN,mode==1,mode%2?WM_SYSKEYDOWN:WM_KEYDOWN,mode==2,true))return false;
        if(mode==0&&!expect(!old.resultPresentation.bannerVisible&&old.campaignScore.presentationStartedAtMs==123456,"Result banner consumes and samples clock before Control"))return false;
        if(mode==1&&!expect(old.resultPresentation.bannerVisible,"repeated result consumes without entering score"))return false;
        if(mode==2&&!expect(old.runtimeExitRequested,"actual Result destroys only this probe private HWND and returns immediately"))return false;
    }    for(int mode=0;mode<3;++mode){
        const auto label="options-"+std::to_string(mode);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        const WPARAM raw=mode==0?VK_ESCAPE:mode==1?VK_F13:VK_F5;
        for(auto* state:{&old,&direct,&native}){clean(*state);if(!expect(disableInputCollisions(*state,raw)
            &&setBinding(*state,"Options",runtimePlayerCurrentKeyCode(raw)),"actual Options binding prerequisite"))return false;
            if(mode<2){state->playerCommunication.beaconTargeting=true;state->playerLocalCommands.followTargeting=state->playerLocalCommands.rallyTargeting=true;
                state->leftPointer.active=state->leftPointer.moved=true;}}
        if(!triple(label,old,direct,native,raw,mode==1,mode%2?WM_SYSKEYDOWN:WM_KEYDOWN,mode!=1,true))return false;
        if(mode==1){if(!expect(!old.runtimeMenuOpen&&old.playerCommunication.beaconTargeting&&old.playerLocalCommands.followTargeting&&old.playerLocalCommands.rallyTargeting
            &&old.leftPointer.active&&old.leftPointer.moved,"repeated Options consumes without cleanup or capture release"))return false;}
        else if(!expect(old.runtimeMenuOpen==(mode==2)&&!old.playerCommunication.beaconTargeting&&!old.playerLocalCommands.followTargeting&&!old.playerLocalCommands.rallyTargeting
            &&!old.leftPointer.active&&!old.leftPointer.moved&&!std::filesystem::exists(runtimeDefaultSavePath(old)),"original targeting cancel returns before menu; clean Options opens menu before F5 file shortcut"))return false;
    }
    {
        auto os=makeBase(1,true),ds=makeBase(1,true),ns=makeBase(1,true);
        if(!expect(os&&ds&&ns,"proven legal active three-house file worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;int index=0;
        for(auto* state:{&old,&direct,&native}){RuntimeSnapshotActiveStateScope active(*state);if(!expect(isolate(*state,"files-"+std::to_string(index++)),"private ordinary file identity"))return false;
            clean(*state);state->config.shortGame=true;state->tick=21;(void)advanceRuntimeParticles(*state);
            if(!expect(state->particlesInitialized&&state->particles.currentTick()==21,"actual particles at active tick21"))return false;}
        for(bool save:{true,false}){
            const WPARAM raw=save?VK_F5:VK_F9;
            for(auto* state:{&old,&direct,&native})if(!expect(disableInputCollisions(*state,raw)&&runtimeMenuKeyboardBindingCode(*state,"Options",0)==VK_ESCAPE
                &&runtimeDefaultSavePath(*state)==pathString(std::filesystem::path(state->config.localDataRootPath)/"runtime_save.ini"),"fresh real file binding/default path prerequisite"))return false;
            if(!triple(save?"file-save":"file-load",old,direct,native,raw,false,WM_KEYDOWN,false,false))return false;
            if(!expect(old.tick==21&&direct.tick==21&&native.tick==21&&old.interactionStatus.starts_with(save?"saved ":"loaded ")
                &&std::filesystem::is_regular_file(runtimeDefaultSavePath(old))&&std::filesystem::is_regular_file(runtimeDefaultSavePath(direct))
                &&std::filesystem::is_regular_file(runtimeDefaultSavePath(native)),"real shortcut file operation and actual tick21 save/load"))return false;
        }
        for(auto* state:{&old,&direct,&native}){ensureRuntimeMenuKeyboardLoaded(*state);state->runtimeMenuOpen=false;state->runtimeMenuKeyCaptureIndex=-1;
            state->playerCommunication.chatOpen=state->playerCommunication.diplomacyOpen=false;
            if(!expect(disableInputCollisions(*state,VK_F13)&&setBinding(*state,"ShowCoordinates",runtimePlayerCurrentKeyCode(VK_F13)),"fresh root actual binding after load"))return false;}
        const bool previous=old.playerLocalCommands.showCoordinates;
        if(!triple("file-fresh-key",old,direct,native,VK_F13,false,WM_SYSKEYDOWN,false,true))return false;
        if(!expect(old.playerLocalCommands.showCoordinates!=previous&&old.tick==21&&old.particles.currentTick()==21,"first key after real scratch commit uses new child state"))return false;
        trace<<"load activeTick=21 particleTick="<<old.particles.currentTick()<<" freshRoot=1 threeActualPrivateSaveFiles=1\n";++loadCases;
    }
    for(int mode=0;mode<2;++mode){
        const auto label="menu-"+std::to_string(mode);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native}){clean(*state);state->runtimeMenuOpen=true;resetRuntimeMenuPage(*state,mode?RuntimeMenuPage::Keyboard:RuntimeMenuPage::Main);}
        if(!triple(label,old,direct,native,mode?VK_NEXT:VK_DOWN,mode!=0,mode?WM_SYSKEYDOWN:WM_KEYDOWN,false,true))return false;
        if(!expect(old.runtimeMenuOpen&&old.selectedObjectIds==prototype->selectedObjectIds&&old.playerLocalCommands.cameraFollow,"actual menu navigation consumes before gameplay router and raw-arrow camera"))return false;
    }
    for(int mode=0;mode<3;++mode){
        const std::string label=mode==1?"command-diplomacy":"command-"+std::to_string(mode);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native}){clean(*state);if(!expect(disableInputCollisions(*state,VK_F13)
            &&setBinding(*state,mode==1?"Diplomacy":mode==2?"PlanningMode":"ShowCoordinates",runtimePlayerCurrentKeyCode(VK_F13)),"actual command binding prerequisite"))return false;}
        if(!triple(label,old,direct,native,VK_F13,false,mode%2?WM_SYSKEYDOWN:WM_KEYDOWN,mode==1,mode!=2,false,LPARAM{1}<<24))return false;
        if(mode==0&&!expect(old.playerLocalCommands.showCoordinates,"actual inner router local command"))return false;
        if(mode==1&&!expect(old.playerCommunication.diplomacyOpen,"actual inner command opens diplomacy then conditionally releases capture"))return false;
        if(mode==2&&!expect(old.playerLocalCommands.planning,"real planning owner accepted at original input tick"))return false;
    }
    for(int mode=0;mode<3;++mode){
        const auto label="selection-"+std::to_string(mode);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native}){clean(*state);if(!expect(disableInputCollisions(*state,VK_F13)
            &&setBinding(*state,mode==2?"GuardObject":"StopObject",runtimePlayerCurrentKeyCode(VK_F13)),"real selection binding exact catalog ID"))return false;}
        if(!triple(label,old,direct,native,VK_F13,mode==0,mode%2?WM_SYSKEYDOWN:WM_KEYDOWN,false,mode==0))return false;
    }
    for(int mode=0;mode<3;++mode){
        const auto label="camera-or-default-"+std::to_string(mode);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        const WPARAM raw=mode==0?VK_LEFT:VK_F14;
        for(auto* state:{&old,&direct,&native}){clean(*state);if(!expect(disableInputCollisions(*state,raw),"fallback exact binding collision removal"))return false;
            if(mode==0&&!expect(setBinding(*state,"CenterView",0x7FE),"raw arrow is not center binding"))return false;}
        if(!triple(label,old,direct,native,raw,mode==0,mode==2?WM_SYSKEYDOWN:WM_KEYDOWN,false,true,mode==1))return false;
        if(mode==0&&!expect(!old.playerLocalCommands.cameraFollow&&!old.projectedObjectsCacheValid,"actual camera fallback still handles repeated raw arrow"))return false;
    }    for(int mode=0;mode<8;++mode){
        const auto label="guard-"+std::to_string(mode);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        const WPARAM raw=mode==3?static_cast<WPARAM>((std::uint64_t{1}<<32)|VK_F13):mode==5?VK_RETURN:mode==6?VK_CONTROL:VK_F13;
        for(auto* state:{&old,&direct,&native}){clean(*state);if(!expect(disableInputCollisions(*state,VK_F13)
            &&setBinding(*state,"ShowCoordinates",runtimePlayerCurrentKeyCode(VK_F13)),"guard actual binding identity"))return false;
            if(mode==0||mode==1||mode==6||mode==7){state->campaignMode=true;state->triggers.campaignInputLocked=true;}
            if(mode==2){state->multiplayerLocalSpectator=true;state->leftPointer.active=state->leftPointer.moved=true;}
            if(mode==4){state->runtimeMenuOpen=true;resetRuntimeMenuPage(*state,RuntimeMenuPage::Main);state->playerCommunication.chatOpen=true;state->playerCommunication.chatDraft=u"unchanged";}
            if(mode==5){state->skirmishMatchFinished=true;state->resultPresentation.started=true;state->resultPresentation.bannerVisible=true;
                state->playerCommunication.chatOpen=true;state->playerCommunication.chatDraft=u"terminal modal is skipped";}
        }
        const UINT message=mode==1?WM_SYSKEYDOWN:mode==6?WM_SYSKEYUP:mode==7?WM_USER+31:WM_KEYDOWN;
        if(!triple(label,old,direct,native,raw,false,message,false,true,false,(LPARAM{1}<<24)|(LPARAM{1}<<29)))return false;
        if(mode==0&&!expect(!old.playerLocalCommands.showCoordinates,"ordinary campaign key blocked after force policy"))return false;
        if((mode==1||mode==3)&&!expect(old.playerLocalCommands.showCoordinates,"SYS campaign bypass and full raw64 key reach actual consumer"))return false;
        if(mode==2&&!expect(old.playerLocalCommands.showCoordinates&&old.selectedObjectIds.empty()&&!old.leftPointer.active&&!old.leftPointer.moved,
            "original observer clear occurs in inner owner and still permits local reference"))return false;
        if(mode==4&&!expect(old.playerCommunication.chatOpen&&old.playerCommunication.chatDraft==u"unchanged"&&!old.playerLocalCommands.showCoordinates,
            "menu guard prevents modal, menu consumes before command"))return false;
        if(mode==5&&!expect(old.playerCommunication.chatOpen&&!old.resultPresentation.bannerVisible,"finished guard prevents modal before actual Result"))return false;
        ++guards;
    }
    // Null root is a native adapter guard, independently exercised for every blocked kind.
    for(const auto message:blockedMessages)if(!expect(oldAdmission(nullptr,message)==1&&!directAdmission(nullptr,message),"null root never lends admission host"))return false;
    trace<<"checks="<<checks<<" admissionCases="<<admissionCases<<" ingressCases="<<ingressCases<<" guards="<<guards<<" priorities="<<priorities
        <<" sourceReads="<<sourceReads<<" acceptedCaptures="<<acceptedCaptures<<" captureReleases="<<captureReleases<<" destroyed="<<destroyed<<" loadCases="<<loadCases<<'\n';
    trace.flush();if(!expect(static_cast<bool>(trace),"trace flush"))return false;
    output<<"Keyboard-ingress integration passed: checks="<<checks<<" admissionCases="<<admissionCases<<" ingressCases="<<ingressCases<<" guards="<<guards<<" priorities="<<priorities
        <<" sourceReads="<<sourceReads<<" acceptedCaptures="<<acceptedCaptures<<" captureReleases="<<captureReleases<<" destroyed="<<destroyed<<" loadCases="<<loadCases
        <<" sameProductionCpp=1 frozenOldCompleteNativeCase=1 frozenOldAdmission=1 canonicalActualCaptureAndApply=1 rawNormalizedStates=retained"
        <<" actualCaptureChangedReadback=1 stateBeforeRelease=observed originalNativeFlags=owned-and-forwarded wrapperKeyClockOrder=unchanged-not-fixed"
        <<" nativeControlPosition=standard-contract nativeAllowedPointerScope=admission-only actualModalAlgorithm=existing-owner-not-migrated"
        <<" physicalModifierMatrix=not-run fullW010=not-covered fullW011=not-covered fullW012=not-covered runningMP=not-run IME=not-run secondBackend=not-run performance=not-run"
        <<" observerEndedSave=legacy-failed-not-retested\n";return true;
}