// Native keyboard-router regressions; included after the real adapters and ordinary loader.
bool runRuntimeKeyboardRouterSmokeProbe(RuntimeWindowState& stock,std::ostream& output) {
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
    std::size_t checks=0,forceCases=0,routerCases=0,guards=0,priorities=0,sourceReads=0,acceptedCaptures=0,loadCases=0,nestedWitnesses=0;
    const auto expect=[&](bool value,const std::string& why){++checks;if(!value)output<<"Keyboard-router integration failed: "<<why<<'\n';return value;};
    if(!expect(stock.report.success&&stock.world.getSummary().loaded&&stock.rulesLoaded&&stock.artLoaded&&stock.theaterLoaded,
        "actual stock resources"))return false;
    char* requested=nullptr;std::size_t requestedLength=0;
    const auto envResult=_dupenv_s(&requested,&requestedLength,"CONCORD_KEYBOARD_ROUTER_OUTPUT");
    const std::filesystem::path directory=requested&&*requested?std::filesystem::path(requested):std::filesystem::path("keyboard-router-smoke-output");
    std::free(requested);if(!expect(envResult==0,"isolated output environment"))return false;
    std::error_code ec;std::filesystem::create_directories(directory,ec);if(!expect(!ec,"isolated output directory"))return false;
    std::ofstream trace(directory/"keyboard-router-trace.txt",std::ios::out|std::ios::trunc);
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
        state.leftPointer.active=state.leftPointer.moved=false;state.repaintRequested=false;state.interactionStatus="router initial";
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
        state.playerLocalCommands.cameraFollow=true;state.repaintRequested=false;state.interactionStatus="router initial";
        state.runtimeSoundEvents=0;state.runtimeSoundIntentHistory.clear();state.runtimeSoundPlaybackHistory.clear();state.lastRuntimeSound.clear();};
    // The following bodies are frozen original policies, not calls to the new owners.
    const auto oldForce=[&](RuntimeWindowState* campaignState,HWND window,UINT message,WPARAM wParam){
    if (campaignState != nullptr) {
        bool forceAttackHeld = campaignState->forceAttackHeld;
        if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
            && (wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL)) {
            forceAttackHeld = true;
        } else if ((message == WM_KEYUP || message == WM_SYSKEYUP)
            && (wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL)) {
            forceAttackHeld = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        } else if (message == WM_KILLFOCUS
            || (message == WM_ACTIVATEAPP && wParam == FALSE)) {
            forceAttackHeld = false;
        } else if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN
            || message == WM_LBUTTONUP) {
            forceAttackHeld = (wParam & MK_CONTROL) != 0;
        }
        if (forceAttackHeld != campaignState->forceAttackHeld) {
            campaignState->forceAttackHeld = forceAttackHeld;
            campaignState->repaintRequested = true;
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
        }
    }
    };
    const auto oldRouter=[&](RuntimeWindowState& state, const RECT& client,
    int code, bool repeated, std::uint64_t now) {
    if (state.runtimeMenuOpen || state.skirmishMatchFinished) return false;
    rejectRuntimeObserverSelection(state);
    if (handleRuntimePlayerReferenceKey(state, client, code, repeated)) return true;
    // With NumLock on the keypad is a second set of group-selection keys.
    const int key = code & 0xff;
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9)
        code = (code & ~0xff) | ('0' + key - VK_NUMPAD0);
    const auto selectable = [&](int id) { return runtimePlayerLocalSelectable(state, id); };
    const RECT viewport = runtimeMapViewport(client);
    auto& local = state.playerLocalCommands;
    RuntimeObjectCycleUiHost cycleHost(state, client, viewport);
    if (handleRuntimeObjectCycleKey({code, repeated, now}, local, state.selectedObjectIds, cycleHost))
        return true;
    RuntimeTeamKeyUiHost teamHost(state, client);
    if (handleRuntimeTeamKey({code, repeated, now}, local, state.selectedObjectIds, teamHost))
        return true;
    RuntimeViewNavigationUiHost viewHost(state, client, viewport);
    if (handleRuntimeViewNavigationKey({code, repeated, now}, local, viewHost))
        return true;
    RuntimeQuickSelectionUiHost quickHost(state, client, viewport);
    if (handleRuntimeQuickSelectionKey({code, repeated, now}, local, state.selectedObjectIds, quickHost))
        return true;
    RuntimePlanningFactoryKeyUiHost planningFactoryHost(state);
    if (handleRuntimePlanningFactoryKey({code, repeated}, local, state.selectedObjectIds, planningFactoryHost))
        return true;
    if (handleRuntimeRadarNavigationKey({code, repeated, now}, viewHost))
        return true;
    return false;
};
    struct QuitCleanup{~QuitCleanup(){MSG msg{};while(PeekMessageW(&msg,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){};}} quitCleanup;
    const auto directForce=[](RuntimeWindowState& state,HWND window,UINT message,WPARAM raw){
        RuntimeForceAttackHeldUiHost host(state,window);handleRuntimeForceAttackHeld(runtimeForceAttackInputForMessage(message,raw),host);};
    const auto prefix=[&](RuntimeWindowState& state,HWND window,UINT message,WPARAM raw,bool repeated,bool old){
        runtimeInvalidateVisionTerrainBatchForMessage(state.world);
        if(old)oldForce(&state,window,message,raw);else directForce(state,window,message,raw);
        if(state.rightPointer.active&&(message==WM_KILLFOCUS||message==WM_CANCELMODE||(message==WM_ACTIVATEAPP&&raw==FALSE))){
            state.rightPointer.active=state.rightPointer.moved=false;state.repaintRequested=true;if(GetCapture()==window)ReleaseCapture();}
        if(state.campaignMode&&state.triggers.campaignInputLocked&&!state.skirmishMatchFinished&&!state.runtimeMenuOpen&&message==WM_KEYDOWN)return true;
        if(state.skirmishMatchFinished&&GetCapture()==window)ReleaseCapture();
        const bool wasDiplomacyOpen=state.playerCommunication.diplomacyOpen;
        if(!state.runtimeMenuOpen&&!state.skirmishMatchFinished&&handleRuntimePlayerModalKey(state,static_cast<int>(raw),repeated)){
            if(wasDiplomacyOpen&&!state.playerCommunication.diplomacyOpen&&GetCapture()==window)ReleaseCapture();InvalidateRect(window,nullptr,FALSE);return true;}
        if(handleRuntimeMenuKeyCapture(state,raw,repeated)){state.repaintRequested=true;RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_NOERASE);return true;}
        const RuntimeSessionKeyInput input{static_cast<std::uint64_t>(raw),repeated};
        RuntimeResultSessionKeyUiHost result(state,window);if(handleRuntimeResultSessionKey(input,result))return true;
        const bool controlDown=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        RuntimeOptionsSessionKeyUiHost options(state,window);if(handleRuntimeOptionsSessionKey(input,options))return true;
        RuntimeSaveLoadSessionKeyUiHost files(state,window);if(handleRuntimeSaveLoadSessionKey(input,controlDown,files))return true;
        RuntimeMenuNavigationUiHost menu(state,window);return handleRuntimeMenuNavigationKey({static_cast<std::uint64_t>(raw),repeated},menu);};
    const auto directRouter=[](RuntimeWindowState& state,const RECT& area,int code,bool repeated,std::uint64_t now){
        RuntimeKeyboardRouterUiHost host(state,area);return handleRuntimeKeyboardRouterKey({code,repeated,now},host);};
    const auto driveKey=[&](RuntimeWindowState& state,HWND window,UINT message,WPARAM raw,bool repeated,bool old){
        if(prefix(state,window,message,raw,repeated,old))return true;
        RECT area{};GetClientRect(window,&area);
        // Retain the old call-argument positions; no new key-versus-clock sequencing claim.
        const bool handled=old?oldRouter(state,area,runtimePlayerCurrentKeyCode(raw),repeated,runtimeUiClockNow(state))
            :directRouter(state,area,runtimePlayerCurrentKeyCode(raw),repeated,runtimeUiClockNow(state));
        if(handled){if(state.playerCommunication.diplomacyOpen&&GetCapture()==window)ReleaseCapture();
            state.repaintRequested=true;InvalidateRect(window,nullptr,FALSE);return true;}
        RuntimeSelectionKeyWin32Host selectionHost(state,raw);
        const auto selectionResult=handleRuntimeSelectionKey({static_cast<std::int32_t>(raw),repeated,
            message==WM_SYSKEYDOWN?RuntimeKeyDownSource::SystemKeyDown:RuntimeKeyDownSource::KeyDown},selectionHost);
        if(selectionResult==RuntimeSelectionKeyResult::RepeatedConsumed)return true;
        if(selectionResult==RuntimeSelectionKeyResult::DispatchedConsumed){state.repaintRequested=true;
            RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_NOERASE);return true;}
        RuntimeCameraKeyWin32Host cameraHost(state,window);
        if(handleRuntimeCameraKey({static_cast<std::uint64_t>(raw),repeated},cameraHost)){
            state.repaintRequested=true;RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_NOERASE);return true;}
        return false;};
    const auto captureText=[&](const Witness& witness){std::ostringstream s;for(const auto& c:witness.commands){
        std::string bytes(c.payload.begin(),c.payload.end());s<<c.tick<<':'<<hexText(bytes)<<':'<<c.accepted<<':'<<hexText(c.error)<<';';}return s.str();};
    const auto triple=[&](const std::string& label,RuntimeWindowState& old,RuntimeWindowState& direct,RuntimeWindowState& native,
        WPARAM raw,bool repeated,UINT message,bool zeroAuthority){
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,label+" private real windows"))return false;
        Clock oc,dc,nc;ClockBinding ob(old,oc),db(direct,dc),nb(native,nc);Witness a(old),b(direct),c(native);
        if(!expect(fullState(old,true)==fullState(direct,true)&&fullState(old,true)==fullState(native,true),label+" complete entry state"))return false;
        const auto beforeAuthority=authority(old);bool oh=false,dh=false;
        {RuntimeSnapshotActiveStateScope active(old);oh=driveKey(old,ow.window,message,raw,repeated,true);}
        {RuntimeSnapshotActiveStateScope active(direct);dh=driveKey(direct,dw.window,message,raw,repeated,false);}
        LRESULT nr=0;{RuntimeSnapshotActiveStateScope active(native);nr=SendMessageW(nw.window,message,raw,repeated?(static_cast<LPARAM>(1)<<30):0);}
        if(fullState(old,true)!=fullState(native,true))output<<"Router mismatch label="<<label<<" old="<<hexText(fullState(old,false))<<" native="<<hexText(fullState(native,false))<<'\n';
        if(!expect(oh==dh&&fullState(old,true)==fullState(direct,true)&&fullState(old,true)==fullState(native,true),label+" old/direct/native full state"))return false;
        if(!expect(oc.reads==dc.reads&&oc.reads==nc.reads&&a.commands==b.commands&&a.commands==c.commands,
            label+" canonical actual captures payload/tick/order/result and real source counts"))return false;
        if(zeroAuthority&&!expect(authority(old)==beforeAuthority&&authority(direct)==beforeAuthority&&authority(native)==beforeAuthority&&a.commands.empty(),
            label+" original authority/tick/queue unchanged"))return false;
        for(const auto& item:a.commands)if(item.accepted)++acceptedCaptures;
        const auto print=[&](const char* path,const RuntimeWindowState& state,const Clock& clock,const Witness& witness){
            trace<<"router path="<<path<<" label="<<label<<" raw="<<static_cast<std::uint64_t>(raw)<<" repeat="<<repeated<<" message="<<message
                <<" reads="<<clock.reads<<" captures="<<witness.commands.size()<<" commands="<<captureText(witness)<<" handled="<<oh<<" nativeResult="<<nr
                <<" rawState="<<hexText(fullState(state,false))<<" normalizedState="<<hexText(fullState(state,true))<<'\n';};
        print("old",old,oc,a);print("direct",direct,dc,b);print("native",native,nc,c);sourceReads+=oc.reads+dc.reads+nc.reads;++routerCases;return true;};
    const auto disableInputCollisions=[&](RuntimeWindowState& state,WPARAM raw){
        const int code=runtimePlayerCurrentKeyCode(raw);for(auto& binding:state.runtimeMenuKeyboardBindings)if(binding.second==code)binding.second=0x7FE;
        return setBinding(state,"Options",VK_ESCAPE);};
    // Finite modifier-policy matrix. The native mouse messages also visit original W010 UI;
    // compare this policy's held result, retain all raw states, and do not claim W010 full-state equivalence.
    struct ForceCase{UINT message;std::uint64_t raw;};
    const ForceCase forceInputs[]={{WM_KEYDOWN,VK_CONTROL},{WM_SYSKEYDOWN,VK_LCONTROL},{WM_KEYDOWN,VK_RCONTROL},
        {WM_KEYUP,VK_CONTROL},{WM_SYSKEYUP,VK_LCONTROL},{WM_KEYUP,VK_RCONTROL},{WM_KILLFOCUS,0},{WM_ACTIVATEAPP,0},{WM_ACTIVATEAPP,1},
        {WM_MOUSEMOVE,0},{WM_MOUSEMOVE,MK_CONTROL},{WM_LBUTTONDOWN,0},{WM_LBUTTONDOWN,MK_CONTROL},
        {WM_LBUTTONUP,0},{WM_LBUTTONUP,MK_CONTROL},{WM_KEYDOWN,(std::uint64_t{1}<<32)|VK_CONTROL},{WM_SYSKEYUP,(std::uint64_t{1}<<32)|VK_RCONTROL}};
    for(bool initial:{false,true})for(std::size_t i=0;i<std::size(forceInputs);++i){
        const auto label="force-"+std::to_string(initial)+"-"+std::to_string(i);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native}){clean(*state);state->runtimeMenuOpen=true;resetRuntimeMenuPage(*state,RuntimeMenuPage::Keyboard);
            state->runtimeMenuKeyCaptureIndex=-1;state->forceAttackHeld=initial;state->repaintRequested=false;}
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,label+" HWNDs"))return false;
        const auto before=authority(old);const auto event=forceInputs[i];
        oldForce(&old,ow.window,event.message,static_cast<WPARAM>(event.raw));directForce(direct,dw.window,event.message,static_cast<WPARAM>(event.raw));
        {RuntimeSnapshotActiveStateScope active(native);SendMessageW(nw.window,event.message,static_cast<WPARAM>(event.raw),MAKELPARAM(-20,-20));}
        if(!expect(old.forceAttackHeld==direct.forceAttackHeld&&old.forceAttackHeld==native.forceAttackHeld
            &&old.repaintRequested==direct.repaintRequested&&(!old.repaintRequested||native.repaintRequested),label+" policy held plus isolated old/direct repaint"))return false;
        if(!expect(authority(old)==before&&authority(direct)==before&&authority(native)==before,label+" authority unchanged"))return false;
        for(const auto& path:std::array<std::pair<const char*,RuntimeWindowState*>,3>{{{"old",&old},{"direct",&direct},{"native",&native}}})
            trace<<"force path="<<path.first<<" label="<<label<<" message="<<event.message<<" raw="<<event.raw<<" held="<<path.second->forceAttackHeld
                <<" repaint="<<path.second->repaintRequested<<" rawState="<<hexText(fullState(*path.second,false))<<'\n';++forceCases;
    }    const char* actions[]={"StructureTab","ToggleRepair","ShowCoordinates","Follow","Diplomacy","ChatToAll","AllToCheer",
        "PreviousObject","TeamCreate_1","TeamSelect_1","SetView1","View1","TypeSelect","HealthNav","PlanningMode","SetPrimaryFactory","SetRallyPoint","CenterOnRadarEvent"};
    for(std::size_t action=0;action<std::size(actions);++action)for(bool repeated:{false,true}){
        const auto label="component-"+std::to_string(action)+"-"+std::to_string(repeated);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" real worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native}){clean(*state);if(!expect(disableInputCollisions(*state,VK_F13)&&setBinding(*state,actions[action],runtimePlayerCurrentKeyCode(VK_F13)),
                label+" exact binding identity and actual modifier code"))return false;}
        const bool authorityLocal=action!=6&&action!=14&&action!=15;
        if(!triple(label,old,direct,native,VK_F13,repeated,action%2?WM_SYSKEYDOWN:WM_KEYDOWN,authorityLocal))return false;
        if(action==0&&!expect(old.activeBuildTab==RuntimeBuildKind::Building,"actual reference tab wins"))return false;
        if(action==1&&!expect(old.activeSidebarTool==(repeated?RuntimeSidebarTool::None:RuntimeSidebarTool::Repair),"actual reference tool repeat policy"))return false;
        if(action==4&&!expect(old.playerCommunication.diplomacyOpen==!repeated,"actual diplomacy owner"))return false;
        if(action==5&&!expect(old.playerCommunication.chatOpen==!repeated,"actual chat entry owner"))return false;
        if(action==9&&!expect(repeated||old.selectedObjectIds==prototype->selectedObjectIds,"actual Team eligibility and selection"))return false;
        if(action==10&&!expect(old.playerLocalCommands.views[0].has_value(),"actual view owner writes/keeps owned cell"))return false;
        if(action==14&&!expect(old.playerLocalCommands.planning==!repeated,"actual planning Begin accepted and repeat consumed"))return false;
        if(action==16&&!expect(old.playerLocalCommands.rallyTargeting&&!old.playerLocalCommands.followTargeting,"actual repeated/nonrepeated rally owner"))return false;
    }
    // Reference sees raw NUMPAD before the router changes only its low byte for Team.
    for(int digit=0;digit<10;++digit){
        const auto label="numpad-"+std::to_string(digit);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        const WPARAM raw=VK_NUMPAD0+digit;const int sampled=runtimePlayerCurrentKeyCode(raw);
        const int normalized=(sampled&~0xFF)|('0'+digit);const int number=digit==0?10:digit;
        for(auto* state:{&old,&direct,&native}){clean(*state);if(!expect(disableInputCollisions(*state,raw),"actual NUMPAD raw collision removal"))return false;
            // A physical modifier may make another default Team action collide after normalization.
            for(auto& binding:state->runtimeMenuKeyboardBindings)if(binding.second==normalized)binding.second=0x7FE;
            if(!expect(setBinding(*state,"TeamSelect_"+std::to_string(number),normalized),"actual NUMPAD configured Team binding"))return false;}
        if(!triple(label,old,direct,native,raw,false,digit%2?WM_SYSKEYDOWN:WM_KEYDOWN,true))return false;
        if(!expect(old.selectedObjectIds==prototype->selectedObjectIds&&old.playerLocalCommands.lastSelectionKey=='0'+digit
            &&old.playerLocalCommands.lastSelectionAtMs==123456,"NUMPAD Team real selection and shared last-selection fields"))return false;
    }
    // Collisions span the complete router priority, not copied child algorithms.
    const std::array<std::pair<const char*,const char*>,6> collisions={{{"StructureTab","PreviousObject"},{"PreviousObject","TeamCreate_1"},
        {"TeamCreate_1","SetView1"},{"SetView1","TypeSelect"},{"TypeSelect","PlanningMode"},{"PlanningMode","CenterOnRadarEvent"}}};
    for(std::size_t i=0;i<collisions.size();++i){
        const auto label="priority-"+std::to_string(i);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native}){clean(*state);state->playerLocalCommands.teams[0].clear();state->playerLocalCommands.views[0].reset();
            if(!expect(disableInputCollisions(*state,VK_F13)&&setBinding(*state,collisions[i].first,runtimePlayerCurrentKeyCode(VK_F13))
                &&setBinding(*state,collisions[i].second,runtimePlayerCurrentKeyCode(VK_F13)),"exact colliding binding identities"))return false;}
        if(!triple(label,old,direct,native,VK_F13,false,i%2?WM_SYSKEYDOWN:WM_KEYDOWN,i!=5))return false;
        if(i==0&&!expect(old.activeBuildTab==RuntimeBuildKind::Building,"Reference beats all navigation"))return false;
        if(i==1&&!expect(old.playerLocalCommands.teams[0].empty(),"empty original Cycle consumes before Team"))return false;
        if(i==2&&!expect(old.playerLocalCommands.teams[0]==prototype->selectedObjectIds&&!old.playerLocalCommands.views[0],"Team before View"))return false;
        if(i==3&&!expect(old.playerLocalCommands.views[0].has_value()&&old.playerLocalCommands.lastSelectionKey=='?',"View before Quick"))return false;
        if(i==4&&!expect(!old.playerLocalCommands.planning&&old.playerLocalCommands.lastSelectionKey==runtimePlayerCurrentKeyCode(VK_F13),"Quick before Planning"))return false;
        if(i==5&&!expect(old.playerLocalCommands.planning,"Planning before Radar"))return false;++priorities;
    }
    for(int mode=0;mode<8;++mode){
        const auto label="guard-"+std::to_string(mode);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native}){clean(*state);if(!expect(disableInputCollisions(*state,VK_F13)
                &&setBinding(*state,"ShowCoordinates",runtimePlayerCurrentKeyCode(VK_F13)),"guard actual binding"))return false;
            if(mode==0){state->campaignMode=true;state->triggers.campaignInputLocked=true;}
            if(mode==1){state->runtimeMenuOpen=true;resetRuntimeMenuPage(*state,RuntimeMenuPage::Main);}
            if(mode==2){state->skirmishMatchFinished=true;state->resultPresentation.started=true;state->resultPresentation.bannerVisible=true;}
            if(mode==3)state->playerCommunication.chatOpen=true;
            if(mode==4){state->runtimeMenuOpen=true;resetRuntimeMenuPage(*state,RuntimeMenuPage::Keyboard);state->runtimeMenuKeyCaptureIndex=0;}
            if(mode==5){state->multiplayerLocalSpectator=true;state->leftPointer.active=state->leftPointer.moved=true;}
            if(mode==6){state->campaignMode=true;state->triggers.campaignInputLocked=true;}
            if(mode==7){state->multiplayerLocalSpectator=true;if(!expect(setBinding(*state,"SetRallyPoint",runtimePlayerCurrentKeyCode(VK_F13)),"observer actual J owner binding"))return false;
                // Earlier reference action is explicitly removed so the observed consumer is J.
                if(!expect(setBinding(*state,"ShowCoordinates",0x7FE),"observer removes prior Reference collision"))return false;}
        }
        if(!triple(label,old,direct,native,VK_F13,false,mode==6?WM_SYSKEYDOWN:WM_KEYDOWN,true))return false;
        if(mode<5&&!expect(!old.playerLocalCommands.showCoordinates,"actual upstream guard consumes before router"))return false;
        if((mode==5||mode==6)&&!expect(old.playerLocalCommands.showCoordinates,"observer/SYS boundary continues real local reference"))return false;
        if(mode==5&&!expect(old.selectedObjectIds.empty()&&!old.leftPointer.active&&!old.leftPointer.moved,"observer gate clears then continues"))return false;
        if(mode==7&&!expect(old.selectedObjectIds.empty()&&!old.playerLocalCommands.rallyTargeting&&!old.playerLocalCommands.followTargeting
            &&old.interactionStatus=="choose a factory rally point","observer clears selection before original Planning J status write"))return false;++guards;
    }
    for(int mode=0;mode<3;++mode){
        const auto label="fallback-"+std::to_string(mode);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" worlds"))return false;auto& old=*os;auto& direct=*ds;auto& native=*ns;
        const WPARAM raw=mode==1?VK_LEFT:VK_F14;
        for(auto* state:{&old,&direct,&native}){clean(*state);if(!expect(disableInputCollisions(*state,raw),"fallback clears actual collisions"))return false;
            if(mode==0&&!expect(setBinding(*state,"StopObject",runtimePlayerCurrentKeyCode(raw)),"exact original StopObject identity"))return false;
            if(mode==1&&!expect(setBinding(*state,"CenterView",0x7FE),"arrow does not hit center binding"))return false;}
        if(!triple(label,old,direct,native,raw,mode==0,mode==2?WM_SYSKEYDOWN:WM_KEYDOWN,true))return false;
        if(mode==1&&!expect(!old.playerLocalCommands.cameraFollow&&!old.projectedObjectsCacheValid,"router false reaches original camera raw arrow"))return false;
    }    // The post-capture store is a second, unconditional policy call at its original site.
    for(bool previous:{false,true})for(std::uint64_t raw:{std::uint64_t{0},std::uint64_t{MK_CONTROL},(std::uint64_t{1}<<32)|MK_CONTROL}){
        auto os=make("restore-old"),ds=make("restore-direct");if(!expect(os&&ds,"restore worlds"))return false;
        auto& old=*os;auto& direct=*ds;old.forceAttackHeld=direct.forceAttackHeld=previous;
        old.repaintRequested=direct.repaintRequested=false;const auto before=authority(old);
        // Original single assignment, independent of the new owner.
        old.forceAttackHeld=(static_cast<WPARAM>(raw)&MK_CONTROL)!=0;
        RuntimeForceAttackHeldUiHost host(direct,nullptr);handleRuntimeForceAttackHeld({RuntimeForceAttackEvent::PointerPressRestore,raw},host);
        if(!expect(old.forceAttackHeld==direct.forceAttackHeld&&!old.repaintRequested&&!direct.repaintRequested
            &&authority(old)==before&&authority(direct)==before,"unconditional restore only writes held without repaint/source/authority"))return false;
        trace<<"restore previous="<<previous<<" raw="<<raw<<" held="<<old.forceAttackHeld<<" oldDirectEqual=1 repaint=0\n";
    }
    {
        auto storage=make("nested-capture");if(!expect(static_cast<bool>(storage),"private nested capture world"))return false;auto& state=*storage;clean(state);
        RuntimePlayerDefeatNoticeSmokeWindow previousWindow(state,1024,768),targetWindow(state,1024,768);
        if(!expect(previousWindow.window&&targetWindow.window,"two private nested capture HWNDs"))return false;
        struct NestedCapture {
            WNDPROC original=nullptr;HWND previous=nullptr,target=nullptr;RuntimeWindowState* state=nullptr;
            int captures=0,focuses=0;bool falseInCallback=false;
            static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM raw,LPARAM flags){
                auto* context=static_cast<NestedCapture*>(GetPropW(window,L"ConcordKeyboardRouterCaptureWitness"));
                if(!context||!context->original)return DefWindowProcW(window,message,raw,flags);
                const LRESULT result=CallWindowProcW(context->original,window,message,raw,flags);
                if(message==WM_CAPTURECHANGED&&context->captures==0){++context->captures;
                    SendMessageW(context->target,WM_KILLFOCUS,reinterpret_cast<WPARAM>(window),0);++context->focuses;
                    context->falseInCallback=!context->state->forceAttackHeld;}
                return result;
            }
            ~NestedCapture(){if(previous){if(original)SetWindowLongPtrW(previous,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(original));
                RemovePropW(previous,L"ConcordKeyboardRouterCaptureWitness");}}
        } context;
        context.previous=previousWindow.window;context.target=targetWindow.window;context.state=&state;
        if(!expect(SetPropW(context.previous,L"ConcordKeyboardRouterCaptureWitness",&context)!=FALSE,"private callback context property"))return false;
        context.original=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(context.previous,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&NestedCapture::procedure)));
        if(!expect(context.original!=nullptr,"private callback subclass prerequisite"))return false;
        SetFocus(targetWindow.window);SetCapture(previousWindow.window);
        if(!expect(GetCapture()==previousWindow.window,"actual previous private HWND capture"))return false;
        state.forceAttackHeld=false;state.repaintRequested=false;state.leftPointer.active=state.leftPointer.moved=false;
        const POINT point{viewport.left+120,viewport.top+120};const auto before=authority(state);Witness witness(state);Clock clock;ClockBinding clockBinding(state,clock);
        {RuntimeSnapshotActiveStateScope active(state);SendMessageW(targetWindow.window,WM_LBUTTONDOWN,MK_CONTROL,MAKELPARAM(point.x,point.y));}
        if(!expect(context.captures==1&&context.focuses==1&&context.falseInCallback&&state.forceAttackHeld
            &&GetCapture()==targetWindow.window&&state.leftPointer.active&&!state.leftPointer.moved
            &&state.leftPointer.start.x==point.x&&state.leftPointer.start.y==point.y
            &&state.leftPointer.current.x==point.x&&state.leftPointer.current.y==point.y
            &&state.mouseClientPoint.x==point.x&&state.mouseClientPoint.y==point.y
            &&authority(state)==before&&witness.commands.empty()&&clock.reads==0,
            "real CaptureChanged forwards original then nested KillFocus clears held before original click restores MK_CONTROL"))return false;
        trace<<"nested kind=controlled-private-nested-message captureChanged="<<context.captures<<" nestedFocus="<<context.focuses
            <<" falseInside="<<context.falseInCallback<<" clickHeld="<<state.forceAttackHeld<<" targetCapture="<<(GetCapture()==targetWindow.window)
            <<" dragStart="<<state.leftPointer.start.x<<','<<state.leftPointer.start.y<<" captures="<<witness.commands.size()<<" reads="<<clock.reads
            <<" rawState="<<hexText(fullState(state,false))<<'\n';
        ReleaseCapture();++nestedWitnesses;
    }
    {
        auto storage=makeBase(1,true);if(!expect(static_cast<bool>(storage),"proven legal active three-house load world"))return false;auto& state=*storage;
        RuntimeSnapshotActiveStateScope active(state);if(!expect(isolate(state,"ordinary-load"),"isolated ordinary save identity"))return false;
        state.config.shortGame=true;state.tick=21;(void)advanceRuntimeParticles(state);
        if(!expect(state.particlesInitialized&&state.particles.currentTick()==21,"real active tick21 particles"))return false;
        Clock clock;ClockBinding binding(state,clock);const auto savePath=pathString(directory/"keyboard-router-load.ini");std::string error;
        const bool saved=saveRuntimeState(state,savePath,error);const bool loaded=saved&&loadRuntimeState(state,savePath,error);
        if(!expect(saved&&loaded&&state.tick==21&&state.uiClock==&clock,"ordinary scratch load keeps clock borrow/tick: "+error))return false;
        // All child references are acquired after the actual load/commit.
        ensureRuntimeMenuKeyboardLoaded(state);state.runtimeMenuOpen=false;state.runtimeMenuKeyCaptureIndex=-1;
        state.playerCommunication.chatOpen=state.playerCommunication.diplomacyOpen=state.playerCommunication.beaconTargeting=false;
        state.playerLocalCommands.followTargeting=state.playerLocalCommands.rallyTargeting=false;state.activeSidebarTool=RuntimeSidebarTool::None;
        state.leftPointer.active=state.leftPointer.moved=false;
        if(!expect(disableInputCollisions(state,VK_F13)&&setBinding(state,"TeamCreate_1",runtimePlayerCurrentKeyCode(VK_F13)),"post-load actual Team binding"))return false;
        state.selectedObjectIds.clear();for(const auto& object:state.world.getObjects())
            if(runtimePlayerLocalSelectable(state,object.instanceId)){state.selectedObjectIds={object.instanceId};break;}
        if(!expect(!state.selectedObjectIds.empty(),"actual fresh selectable owner after load"))return false;
        Witness witness(state);RuntimePlayerDefeatNoticeSmokeWindow window(state,1024,768);if(!expect(window.window!=nullptr,"post-load private HWND"))return false;
        const auto before=authority(state);const auto reads=clock.reads;
        {RuntimeSnapshotActiveStateScope fresh(state);SendMessageW(window.window,WM_KEYDOWN,VK_F13,0);}
        if(!expect(state.playerLocalCommands.teams[0]==state.selectedObjectIds&&state.tick==21&&state.uiClock==&clock
            &&clock.reads==reads+1&&authority(state)==before&&witness.commands.empty(),"fresh root Team owner after real load and one original key-clock sample"))return false;
        trace<<"load tick=21 particleTick="<<state.particles.currentTick()<<" freshRoot=1 sourceSame=1 captures=0 reads="<<(clock.reads-reads)
            <<" rawState="<<hexText(fullState(state,false))<<'\n';sourceReads+=clock.reads;++loadCases;
    }
    trace<<"checks="<<checks<<" forceCases="<<forceCases<<" routerCases="<<routerCases<<" guards="<<guards<<" priorities="<<priorities
        <<" sourceReads="<<sourceReads<<" acceptedCaptures="<<acceptedCaptures<<" nestedWitnesses="<<nestedWitnesses<<" loadCases="<<loadCases<<'\n';
    trace.flush();if(!expect(static_cast<bool>(trace),"trace flush"))return false;
    output<<"Keyboard-router integration passed: checks="<<checks<<" forceCases="<<forceCases<<" routerCases="<<routerCases<<" guards="<<guards<<" priorities="<<priorities
        <<" sourceReads="<<sourceReads<<" acceptedCaptures="<<acceptedCaptures<<" nestedWitnesses="<<nestedWitnesses<<" loadCases="<<loadCases
        <<" sameProductionCpp=1 frozenOldForceAndRouter=1 canonicalActualCaptureAndApply=1 rawNormalizedStates=retained"
        <<" nativeQueryOrder=standard-contract nativeMouseScope=force-policy-only nestedWitness=controlled-private-nested-message"
        <<" physicalModifierMatrix=not-run fullW010=not-covered fullW012=not-covered runningMP=not-run secondBackend=not-run performance=not-run"
        <<" observerEndedSave=legacy-failed-not-retested\n";return true;
}