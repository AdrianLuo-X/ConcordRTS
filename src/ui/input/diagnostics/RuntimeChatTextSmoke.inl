// Native UTF-16 chat integration. Included after the real UI host, WndProc and ordinary loader.
bool runRuntimeChatTextSmokeProbe(RuntimeWindowState& stock,std::ostream& output) {
    static_assert(sizeof(wchar_t)==2,"The frozen Windows oracle uses native UTF-16 units");
    struct Clock final : RuntimeUiClock {RuntimeUiTimeMs value=123456;mutable std::size_t reads=0;
        RuntimeUiTimeMs nowMs() const noexcept override {++reads;return value;}};
    struct ClockBinding {RuntimeWindowState& state;const RuntimeUiClock* previous;
        ClockBinding(RuntimeWindowState& s,const RuntimeUiClock& c):state(s),previous(s.uiClock){s.uiClock=&c;}
        ~ClockBinding(){state.uiClock=previous;}};
    struct Capture {int tick=0;std::vector<std::uint8_t> payload;bool accepted=false,applied=false;std::string error;
        bool operator==(const Capture&) const=default;};
    struct Witness {RuntimeWindowState& state;int route;std::vector<Capture> commands;
        Witness(RuntimeWindowState& s,int mode):state(s),route(mode){
            state.multiplayerBridge=nullptr;state.offlineGameplayIntentRouting=false;state.multiplayerIntentRouter.setCaptureCallback({});
            if(route==0)return;
            initializeRuntimeOfflineGameplayRouting(state);
            state.multiplayerIntentRouter.setCaptureCallback([this](const RuntimeMultiplayerGameplayCommand& command,std::string& error){
                Capture c;c.tick=state.tick;if(!encodeRuntimeMultiplayerGameplayCommand(command,c.payload,error))return false;
                if(route==3){error="chat fixture capture refused";c.accepted=false;}
                else if(route==2){error.clear();c.accepted=true;}
                else c.accepted=c.applied=dispatchRuntimeOfflineGameplayCommand(state,command,error);
                c.error=error;commands.push_back(c);return c.accepted;});}
        bool replay(std::string& error){if(route!=2)return true;for(const auto& item:commands){
            RuntimeMultiplayerGameplayCommand command;
            if(!decodeRuntimeMultiplayerGameplayCommand(item.payload,command,error)||!dispatchRuntimeOfflineGameplayCommand(state,command,error))return false;}return true;}
        ~Witness(){state.multiplayerIntentRouter.setCaptureCallback({});}};
    std::size_t checks=0,codecCases=0,characterCases=0,keyCases=0,preFillCases=0,guardCases=0,loadCases=0;
    std::size_t oldCases=0,directCases=0,nativeCases=0,acceptedCommands=0,rejectedCommands=0,replays=0,sourceReads=0;
    const auto expect=[&](bool ok,const std::string& why){++checks;if(!ok)output<<"Chat-text integration failed: "<<why<<'\n';return ok;};
    if(!expect(stock.report.success&&stock.world.getSummary().loaded&&stock.rulesLoaded&&stock.artLoaded&&stock.theaterLoaded,"actual stock startup resources"))return false;
    char* requested=nullptr;std::size_t requestedLength=0;
    const auto envResult=_dupenv_s(&requested,&requestedLength,"CONCORD_CHAT_TEXT_OUTPUT");
    const std::filesystem::path directory=requested&&*requested?std::filesystem::path(requested):std::filesystem::path("chat-text-smoke-output");
    std::free(requested);if(!expect(envResult==0,"owned isolated output environment"))return false;
    std::error_code ec;std::filesystem::create_directories(directory,ec);if(!expect(!ec,"isolated output directory"))return false;
    std::ofstream trace(directory/"chat-text-trace.txt",std::ios::binary|std::ios::trunc);
    if(!expect(static_cast<bool>(trace),"trace file"))return false;
    const auto hexText=[](const std::string& value){std::ostringstream s;for(unsigned char c:value)s<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(c);return s.str();};
    const auto wideText=[](const auto& units){std::ostringstream s;for(auto unit:units)s<<static_cast<std::uint32_t>(unit)<<',';return s.str();};
    const auto replaceExact=[](std::string value,const std::string& from,const std::string& to){if(from.empty())return value;std::size_t at=0;
        while((at=value.find(from,at))!=std::string::npos){value.replace(at,from.size(),to);at+=to.size();}return value;};
    std::map<const RuntimeWindowState*,std::pair<std::string,std::string>> privateIdentities;
    const auto canonical=[&](std::string value,const RuntimeWindowState& state){const auto found=privateIdentities.find(&state);
        if(found!=privateIdentities.end()&&state.config.configPath==found->second.first&&state.config.localDataRootPath==found->second.second){
            value=replaceExact(std::move(value),found->second.first,"<private-preferences>");
            const auto save=pathString(std::filesystem::path(found->second.second)/"runtime_save.ini");
            if(runtimeDefaultSavePath(state)==save)value=replaceExact(std::move(value),save,"<private-runtime-save>");
            value=replaceExact(std::move(value),found->second.second,"<private-save-root>");}return value;};
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
            <<";chatFull="<<state.playerCommunication.chatAllies<<','<<wideText(state.playerCommunication.chatDraft)<<','<<state.playerCommunication.beaconEditing
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

    const auto setBinding=[](RuntimeWindowState& state,const std::string& action,int code){
        for(auto& entry:state.runtimeMenuKeyboardBindings)if(entry.first==action){entry.second=code;return;}
        state.runtimeMenuKeyboardBindings.emplace_back(action,code);};
    const auto clean=[&](RuntimeWindowState& state){
        state.runtimeMenuOpen=false;state.runtimeMenuKeyCaptureIndex=-1;state.skirmishMatchFinished=false;
        state.campaignMode=false;state.triggers.campaignInputLocked=false;state.playerCommunication={};
        state.playerLocalCommands={};state.activeSidebarTool=RuntimeSidebarTool::None;
        state.selectedObjectIds=prototype->selectedObjectIds;
        state.leftPointer.active=state.leftPointer.moved=state.rightPointer.active=state.rightPointer.moved=false;
        state.runtimeMenuPressed=state.runtimeMenuHover=state.runtimeMenuSliderDrag=RuntimeMenuAction::None;
        ensureRuntimeMenuKeyboardLoaded(state);setBinding(state,"Options",VK_ESCAPE);
        state.interactionStatus="chat initial";state.repaintRequested=false;
        state.runtimeSoundEvents=0;state.runtimeSoundIntentHistory.clear();state.runtimeSoundPlaybackHistory.clear();state.lastRuntimeSound.clear();};
    const auto nativeWide=[](std::u16string_view units){std::wstring text;text.reserve(units.size());for(char16_t unit:units)text.push_back(static_cast<wchar_t>(unit));return text;};
    // The complete frozen strict Windows codec body is independent of the new production encoder.
    const auto oldWin32ChatUtf8=[](const std::wstring& text)->std::string
{
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0 || size > static_cast<int>(runtimeMultiplayerGameplayMaximumIdentifierBytes)) return {};
    std::string encoded(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        encoded.data(), size, nullptr, nullptr);
    return encoded;
};

    const auto runtimePlayerChatUtf8=[&](const std::u16string& units){return oldWin32ChatUtf8(nativeWide(units));};
    const auto handleRuntimePlayerModalKey=[&](RuntimeWindowState& state, int key, bool repeated)->bool {
    auto& ui = state.playerCommunication;
    if (ui.chatOpen) {
        if (key == VK_ESCAPE) { ui.chatOpen = ui.beaconEditing = false; ui.chatDraft.clear(); }
        else if (key == VK_BACK && !ui.chatDraft.empty()) {
            const wchar_t last = ui.chatDraft.back(); ui.chatDraft.pop_back();
            if (last >= 0xdc00 && last <= 0xdfff && !ui.chatDraft.empty()
                && ui.chatDraft.back() >= 0xd800 && ui.chatDraft.back() <= 0xdbff)
                ui.chatDraft.pop_back();
        } else if (key == VK_RETURN && !repeated) {
            const auto text = trimAsciiLocal(runtimePlayerChatUtf8(ui.chatDraft));
            if (ui.beaconEditing && !runtimePlayerSelectedBeacon(state)) {
                ui.chatOpen=ui.beaconEditing=false;ui.chatDraft.clear();
            } else if (text.empty() && !ui.beaconEditing) { ui.chatOpen = false; ui.chatDraft.clear(); }
            else {
                RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
                command.kind = ui.beaconEditing ? RuntimePlayerCommunicationKind::BeaconText
                    : ui.chatAllies ? RuntimePlayerCommunicationKind::ChatAllies : RuntimePlayerCommunicationKind::ChatAll;
                if (ui.beaconEditing) command.cell=*ui.selectedBeacon;
                else command.value=runtimePlayerOutgoingChatMask(state);
                command.text = text;
                if (dispatchRuntimePlayerCommunicationFromUi(state, command)) {
                    ui.chatOpen = ui.beaconEditing = false; ui.chatDraft.clear();
                }
            }
        }
        state.repaintRequested = true;
        return true;
    }
    RuntimeDiplomacyNavigationUiHost host(state);
    return handleRuntimeDiplomacyNavigationKey({key, repeated}, ui, host);
};
    const auto oldIngress=[&](HWND window,UINT message,WPARAM wParam,LPARAM lParam)->LRESULT {
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
    const auto oldCharacter=[](HWND window,UINT message,WPARAM wParam,LPARAM lParam)->LRESULT {
        switch(message){
    case WM_CHAR:
    {
        auto* state = reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state && state->playerCommunication.chatOpen && !state->runtimeMenuOpen) {
            auto& draft = state->playerCommunication.chatDraft;
            if (wParam >= 32 && wParam != 127 && draft.size() < 80)
                draft.push_back(static_cast<wchar_t>(wParam));
            state->repaintRequested = true; InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

        default:return DefWindowProcW(window,message,wParam,lParam);
        }
    };
    struct QuitCleanup{~QuitCleanup(){MSG msg{};while(PeekMessageW(&msg,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){};}} quitCleanup;
    const auto prefix=[](RuntimeWindowState* state,HWND window,UINT message,WPARAM raw){
        if(state)runtimeInvalidateVisionTerrainBatchForMessage(state->world);
        if(state){RuntimeForceAttackHeldUiHost host(*state,window);handleRuntimeForceAttackHeld(runtimeForceAttackInputForMessage(message,raw),host);}
        if(state&&state->rightPointer.active&&(message==WM_KILLFOCUS||message==WM_CANCELMODE||(message==WM_ACTIVATEAPP&&raw==FALSE))){
            state->rightPointer.active=state->rightPointer.moved=false;state->repaintRequested=true;if(GetCapture()==window)ReleaseCapture();}
    };
    const auto drive=[&](int route,HWND window,UINT message,WPARAM raw,LPARAM flags)->LRESULT{
        if(route==2)return SendMessageW(window,message,raw,flags);
        auto* state=reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        prefix(state,window,message,raw);
        if(state){RuntimeCampaignInputAdmissionUiHost admission(*state);if(rejectRuntimeCampaignInput(runtimeCampaignInputEventForMessage(message),admission))return 0;}
        if(state&&state->skirmishMatchFinished&&GetCapture()==window)ReleaseCapture();
        if(message==WM_CHAR){
            if(route==0)return oldCharacter(window,message,raw,flags);
            if(state){RuntimeChatTextUiHost host(*state,window);
                if(handleRuntimeChatTextCharacter({static_cast<std::uint64_t>(raw)},state->playerCommunication,host))return 0;}
            return DefWindowProcW(window,message,raw,flags);
        }
        if(route==0)return oldIngress(window,message,raw,flags);
        if(!state||(message!=WM_KEYDOWN&&message!=WM_SYSKEYDOWN))return DefWindowProcW(window,message,raw,flags);
        const bool wasDiplomacy=state->playerCommunication.diplomacyOpen;
        if(!state->runtimeMenuOpen&&!state->skirmishMatchFinished){
            RuntimeChatTextUiHost host(*state);
            if(handleRuntimeChatTextKey({static_cast<std::int32_t>(raw),(flags&(LPARAM{1}<<30))!=0},state->playerCommunication,host)){
                if(wasDiplomacy&&!state->playerCommunication.diplomacyOpen&&GetCapture()==window)ReleaseCapture();
                InvalidateRect(window,nullptr,FALSE);return 0;
            }
        }
        RuntimeKeyboardIngressUiHost ingress(*state,window,flags);
        const RuntimeKeyboardIngressInput input{static_cast<std::uint64_t>(raw),(flags&(LPARAM{1}<<30))!=0,
            message==WM_SYSKEYDOWN?RuntimeKeyDownSource::SystemKeyDown:RuntimeKeyDownSource::KeyDown};
        return handleRuntimeKeyboardIngressKey(input,ingress)?LRESULT{0}:DefWindowProcW(window,message,raw,flags);
    };
    const auto captureText=[&](const Witness& witness){std::ostringstream s;for(const auto& item:witness.commands){
        s<<item.tick<<':'<<item.accepted<<':'<<item.applied<<':'<<hexText(item.error)<<':'
            <<hexText(std::string(item.payload.begin(),item.payload.end()))<<';';}return s.str();};
    const auto codec=[&](const std::u16string& units,const std::string& label){
        const auto before=units;const auto expected=oldWin32ChatUtf8(nativeWide(units));const auto actual=encodeRuntimeChatTextUtf8(units);
        if(!expect(actual==expected&&units==before,label+" strict production encoder equals independent original Windows API bytes"))return false;
        trace<<"codec label="<<label<<" units="<<wideText(units)<<" old="<<hexText(expected)<<" production="<<hexText(actual)<<'\n';++codecCases;return true;};
    const std::vector<std::pair<std::string,std::u16string>> codecInputs{
        {"empty",u""},{"ascii",u" hello "},{"bmp",u"中文 Ω"},{"supplementary",u"\U0001F600"},
        {"lone-high",std::u16string{char16_t(0xD800)}},{"lone-low",std::u16string{char16_t(0xDC00)}},
        {"reversed-pair",std::u16string{char16_t(0xDE00),char16_t(0xD83D)}},
        {"embedded-nul",std::u16string{u'A',char16_t(0),u'B'}},{"controls",std::u16string{char16_t(1),u'\t',u'\n',char16_t(127),char16_t(159)}},
        {"255-bytes",std::u16string(255,u'A')},{"256-bytes",std::u16string(256,u'A')},{"257-bytes",std::u16string(257,u'A')},
        {"255-multibyte",std::u16string(85,u'中')},{"258-multibyte",std::u16string(86,u'中')},
        {"maximum-scalar",u"\U0010FFFF"},{"noncharacters",std::u16string{char16_t(0xFFFF),char16_t(0xFFFE)}},
        {"combining",u"e\u0301"},{"before-trim-limit",u" "+std::u16string(255,u'A')+u" "}};
    for(const auto& [label,units]:codecInputs)if(!codec(units,label))return false;
    std::u16string pairs;for(int i=0;i<64;++i)pairs+=u"\U0001F600";
    if(!codec(pairs,"256-emoji-bytes"))return false;pairs+=u"\U0001F600";if(!codec(pairs,"260-emoji-bytes"))return false;
    enum class Effect {Characters,Close,Submit,Rejected,Unchanged};
    struct Scenario {
        std::string label;std::u16string draft;std::vector<std::uint64_t> characters;std::int32_t key=0;
        bool repeated=false,allies=false,editing=false;int selected=0;bool explicitRecipients=false;std::uint8_t recipients=255;
        Effect effect=Effect::Characters;std::u16string expectedDraft;std::string expectedText;std::uint8_t expectedMask=0;
    };
    std::vector<Scenario> scenarios;
    const auto chars=[&](std::string label,std::u16string initial,std::vector<std::uint64_t> values,std::u16string expected){
        Scenario s;s.label=std::move(label);s.draft=std::move(initial);s.characters=std::move(values);s.expectedDraft=std::move(expected);scenarios.push_back(std::move(s));};
    chars("bmp-units",u"",{0x4e2d,0x6587},u"中文");
    chars("paired-units",u"",{0xD83D,0xDE00},u"\U0001F600");
    chars("repeated-wm-char",u"",{'R','R'},u"RR");scenarios.back().repeated=true;
    chars("low-without-high",u"",{0xDE00},std::u16string{char16_t(0xDE00)});
    chars("filter-before-narrow",u"",{31,32,127,0x100000041ULL,0x10000007FULL,0x100000000ULL},
        std::u16string{u' ',u'A',char16_t(127),char16_t(0)});
    chars("raw-all-bits",u"",{0xffffffffffffffffULL},std::u16string{char16_t(0xffff)});
    chars("79-surrogate-split",std::u16string(79,u'A'),{0xD83D,0xDE00},std::u16string(79,u'A')+std::u16string{char16_t(0xD83D)});
    chars("80-full",std::u16string(80,u'A'),{'Z'},std::u16string(80,u'A'));
    chars("over80-prefill",std::u16string(256,u'A'),{'Z'},std::u16string(256,u'A'));
    const auto keyCase=[&](std::string label,std::u16string draft,std::int32_t key,bool repeated,Effect effect,
            std::u16string expectedDraft,std::string expectedText={},bool allies=false,bool editing=false,int selected=0){
        Scenario s;s.label=std::move(label);s.draft=std::move(draft);s.key=key;s.repeated=repeated;s.effect=effect;
        s.expectedDraft=std::move(expectedDraft);s.expectedText=std::move(expectedText);s.allies=allies;s.editing=editing;s.selected=selected;
        scenarios.push_back(std::move(s));};
    keyCase("escape-repeat",u"not sent",VK_ESCAPE,true,Effect::Close,u"");
    keyCase("back-pair",u"A\U0001F600",VK_BACK,false,Effect::Unchanged,u"A");
    keyCase("back-lone-low",u"A"+std::u16string{char16_t(0xDE00)},VK_BACK,true,Effect::Unchanged,u"A");
    keyCase("back-lone-high",u"A"+std::u16string{char16_t(0xD83D)},VK_BACK,false,Effect::Unchanged,u"A");
    keyCase("back-empty",u"",VK_BACK,false,Effect::Unchanged,u"");
    keyCase("return-repeat",u"keep",VK_RETURN,true,Effect::Unchanged,u"keep");
    keyCase("unknown-key",u"keep",VK_F13,false,Effect::Unchanged,u"keep");
    keyCase("chat-all",u"  中文 Ω  ",VK_RETURN,false,Effect::Submit,u"","中文 Ω");
    keyCase("chat-allies",u"  allied  ",VK_RETURN,false,Effect::Submit,u"","allied",true);
    keyCase("empty-chat",u"",VK_RETURN,false,Effect::Close,u"");
    keyCase("trim-chat",u" \t\r\n ",VK_RETURN,false,Effect::Close,u"");
    keyCase("invalid-surrogate-chat",std::u16string{char16_t(0xD800)},VK_RETURN,false,Effect::Close,u"");
    keyCase("256-chat",std::u16string(256,u'A'),VK_RETURN,false,Effect::Submit,u"",std::string(256,'A'));
    keyCase("257-chat",std::u16string(257,u'A'),VK_RETURN,false,Effect::Close,u"");
    keyCase("before-trim-257",u" "+std::u16string(255,u'A')+u" ",VK_RETURN,false,Effect::Close,u"");
    keyCase("embedded-nul-reject",std::u16string{u'A',char16_t(0),u'B'},VK_RETURN,false,Effect::Rejected,std::u16string{u'A',char16_t(0),u'B'});
    keyCase("interior-control-reject",u"A\tB",VK_RETURN,false,Effect::Rejected,u"A\tB");
    keyCase("del-reject",std::u16string{u'A',char16_t(127)},VK_RETURN,false,Effect::Rejected,std::u16string{u'A',char16_t(127)});
    keyCase("empty-beacon",u"",VK_RETURN,false,Effect::Submit,u"","",false,true);
    keyCase("invalid-surrogate-beacon",std::u16string{char16_t(0xD800)},VK_RETURN,false,Effect::Submit,u"","",false,true);
    keyCase("257-beacon",std::u16string(257,u'A'),VK_RETURN,false,Effect::Submit,u"","",false,true);
    keyCase("valid-beacon",u"  信标 ★  ",VK_RETURN,false,Effect::Submit,u"","信标 ★",false,true);
    keyCase("missing-beacon",u"not sent",VK_RETURN,false,Effect::Close,u"","",false,true,1);
    keyCase("foreign-beacon",u"not sent",VK_RETURN,false,Effect::Close,u"","",false,true,2);
    keyCase("expired-beacon",u"not sent",VK_RETURN,false,Effect::Close,u"","",false,true,3);
    for(std::uint8_t mask:{std::uint8_t(0),std::uint8_t(2),std::uint8_t(7),std::uint8_t(255)}){
        keyCase("recipients-"+std::to_string(mask),u"masked",VK_RETURN,false,Effect::Submit,u"","masked");
        scenarios.back().explicitRecipients=true;scenarios.back().recipients=mask;
        scenarios.back().expectedMask=static_cast<std::uint8_t>((mask&7)|1);
    }
    const auto setScenario=[&](RuntimeWindowState& state,const Scenario& test){
        clean(state);auto& ui=state.playerCommunication;ui.chatOpen=true;ui.chatAllies=test.allies;ui.beaconEditing=test.editing;ui.chatDraft=test.draft;
        ui.chatRecipientsExplicit=test.explicitRecipients;ui.chatRecipients=test.recipients;
        if(test.editing){
            RuntimeMultiplayerGameplayPlayerCommunicationCommand beacon;beacon.kind=RuntimePlayerCommunicationKind::PlaceBeacon;
            beacon.cell={state.economy.skirmishStartCell.x,state.economy.skirmishStartCell.y};
            if(!expect(findRuntimeTerrainCellAt(state,{beacon.cell.x,beacon.cell.y})&&applyRuntimePlayerCommunication(state,state.economy.owner,beacon),"actual valid-cell owned beacon"))return false;
            ui.selectedBeacon=beacon.cell;ui.beacons.back().text="old beacon";
            if(test.selected==1)ui.selectedBeacon=RuntimeMultiplayerGameplayCell{999999,999999};
            if(test.selected==2)ui.beacons.back().owner="skirmishplayer2";
            if(test.selected==3)ui.beacons.back().untilTick=state.tick;
            ui.lines.clear();
        }
        state.repaintRequested=false;return true;
    };
    const auto triple=[&](const Scenario& test,int routing){
        const auto label=test.label+"-route"+std::to_string(routing);
        auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" independent private worlds"))return false;
        auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native})if(!setScenario(*state,test))return false;
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,label+" three real private HWNDs"))return false;
        Clock oc,dc,nc;ClockBinding ob(old,oc),db(direct,dc),nb(native,nc);Witness a(old,routing),b(direct,routing),c(native,routing);
        if(!expect(fullState(old,true)==fullState(direct,true)&&fullState(old,true)==fullState(native,true),label+" complete equal entry state"))return false;
        const auto worldBefore=old.world.deterministicSimulationStateHash(),queueBefore=old.playerCommands.stateHash();
        std::vector<std::uint8_t> expectedPayload;
        if(test.effect==Effect::Submit){
            RuntimeMultiplayerGameplayPlayerCommunicationCommand expected;
            expected.kind=test.editing?RuntimePlayerCommunicationKind::BeaconText:test.allies?RuntimePlayerCommunicationKind::ChatAllies:RuntimePlayerCommunicationKind::ChatAll;
            expected.text=test.expectedText;expected.value=test.editing?0:test.expectedMask;
            if(test.editing)expected.cell={old.economy.skirmishStartCell.x,old.economy.skirmishStartCell.y};
            RuntimeMultiplayerGameplayCommand envelope;envelope.body=expected;std::string error;
            if(!expect(encodeRuntimeMultiplayerGameplayCommand(envelope,expectedPayload,error),label+" independent expected command encodes"))return false;
        }
        const auto one=[&](UINT message,std::uint64_t raw,bool repeated){
            const LPARAM flags=LPARAM{1}|(repeated?(LPARAM{1}<<30):0);
            ValidateRect(ow.window,nullptr);ValidateRect(dw.window,nullptr);ValidateRect(nw.window,nullptr);
            LRESULT results[3]{};
            {RuntimeSnapshotActiveStateScope active(old);results[0]=drive(0,ow.window,message,static_cast<WPARAM>(raw),flags);}
            {RuntimeSnapshotActiveStateScope active(direct);results[1]=drive(1,dw.window,message,static_cast<WPARAM>(raw),flags);}
            {RuntimeSnapshotActiveStateScope active(native);results[2]=drive(2,nw.window,message,static_cast<WPARAM>(raw),flags);}
            const bool invalidated[3]={GetUpdateRect(ow.window,nullptr,FALSE)!=FALSE,GetUpdateRect(dw.window,nullptr,FALSE)!=FALSE,GetUpdateRect(nw.window,nullptr,FALSE)!=FALSE};
            if(fullState(old,true)!=fullState(native,true))output<<"Chat-text mismatch label="<<label<<" old="<<hexText(fullState(old,false))<<" native="<<hexText(fullState(native,false))<<'\n';
            if(!expect(results[0]==results[1]&&results[0]==results[2]&&fullState(old,true)==fullState(direct,true)&&fullState(old,true)==fullState(native,true)
                &&a.commands==b.commands&&a.commands==c.commands&&oc.reads==dc.reads&&oc.reads==nc.reads&&invalidated[0]==invalidated[1]&&invalidated[0]==invalidated[2],
                label+" old/direct/actual WndProc complete state/return/invalidate/canonical commands/clock"))return false;
            const auto print=[&](const char* path,const RuntimeWindowState& state,const Clock& clock,const Witness& witness,int index){
                trace<<"chat path="<<path<<" label="<<label<<" message="<<message<<" raw="<<raw<<" repeated="<<repeated<<" route="<<routing
                    <<" result="<<results[index]<<" invalidated="<<invalidated[index]<<" reads="<<clock.reads<<" units="<<wideText(state.playerCommunication.chatDraft)
                    <<" rawState="<<hexText(fullState(state,false))<<" normalizedState="<<hexText(fullState(state,true))<<" commands="<<captureText(witness)<<'\n';};
            print("old",old,oc,a,0);print("direct",direct,dc,b,1);print("native",native,nc,c,2);
            return true;
        };
        for(auto raw:test.characters)if(!one(WM_CHAR,raw,test.repeated))return false;
        if(test.key&&!one(test.repeated?WM_SYSKEYDOWN:WM_KEYDOWN,static_cast<std::uint32_t>(test.key),test.repeated))return false;
        const auto& ui=old.playerCommunication;
        const bool captureRefusal=test.effect==Effect::Submit&&routing==3;
        if(!expect(ui.chatDraft==(captureRefusal?test.draft:test.expectedDraft),label+" independent exact resulting UTF16 units"))return false;
        if(test.effect==Effect::Close||(test.effect==Effect::Submit&&!captureRefusal)){
            if(!expect(!ui.chatOpen&&!ui.beaconEditing,label+" original close/accepted behavior"))return false;
        }else if(!expect(ui.chatOpen&&ui.beaconEditing==test.editing,label+" original uncommitted editor remains open"))return false;
        const std::size_t count=test.effect==Effect::Submit&&routing!=0?1:0;
        if(!expect(a.commands.size()==count,label+" original exact capture attempts"))return false;
        if(count){const auto& command=a.commands.front();
            if(!expect(command.payload==expectedPayload&&command.tick==old.tick&&command.accepted==!captureRefusal&&command.applied==(routing==1),
                label+" independently expected payload/tick/accepted/apply domain"))return false;}
        if(test.effect==Effect::Rejected||captureRefusal){
            if(!expect(!ui.lines.empty()&&ui.lines.back().owner=="系统"&&ui.lines.back().text.starts_with("操作未执行："),label+" actual unchanged system error feedback"))return false;
            ++rejectedCommands;
        }
        if(test.effect==Effect::Submit&&!captureRefusal){
            if(routing==2){std::string error;
                if(!expect(ui.lines.empty()&&a.replay(error)&&b.replay(error)&&c.replay(error),label+" intercepted capture has no optimistic effects then actual offline replay: "+error))return false;
                if(!expect(fullState(old,true)==fullState(direct,true)&&fullState(old,true)==fullState(native,true),label+" actual replay effects equal"))return false;
                trace<<"replay label="<<label<<" tick="<<old.tick<<" expected="<<hexText(std::string(expectedPayload.begin(),expectedPayload.end()))<<" state="<<hexText(fullState(old,true))<<'\n';++replays;
            }
            if(!expect(!old.playerCommunication.lines.empty(),label+" actual original apply produces local chat/beacon line"))return false;
            const auto& line=old.playerCommunication.lines.back();
            if(!expect(line.owner==runtimeCanonicalHumanHouseOwner(old.economy.owner)&&line.text==(test.editing?std::string("信标：")+test.expectedText:test.expectedText)
                &&line.allies==(test.editing||test.allies)&&line.untilTick==old.tick+15*std::max(1,old.config.fixedTickHz),label+" independently expected original line owner/text/allies/expiry"))return false;
            if(test.editing&&!expect(!old.playerCommunication.beacons.empty()&&old.playerCommunication.beacons.back().text==test.expectedText,label+" independently expected BeaconText apply"))return false;
            ++acceptedCommands;
        }
        if(test.editing&&test.selected!=0&&test.key==VK_RETURN&&!test.repeated&&!expect(!ui.selectedBeacon,label+" original invalid selected-cell reset"))return false;
        if(!expect(old.world.deterministicSimulationStateHash()==worldBefore&&old.playerCommands.stateHash()==queueBefore&&old.tick==7,
            label+" original communication leaves simulation world/command ledger/tick unchanged"))return false;
        // All observations are complete before any hidden window destructor may repaint.
        sourceReads+=oc.reads+dc.reads+nc.reads;++oldCases;++directCases;++nativeCases;
        if(test.key)++keyCases;else ++characterCases;return true;
    };
    for(const auto& test:scenarios)if(!triple(test,1))return false;
    for(const auto& test:scenarios)if(test.effect==Effect::Submit){
        if(!triple(test,0)||!triple(test,2)||!triple(test,3))return false;
    }

    // Enter-to-edit is exercised through the existing Reference owner, including permissive flags-0 prefill.
    for(int mode=0;mode<3;++mode){
        const auto label="prefill-"+std::to_string(mode);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" independent private worlds"))return false;
        auto& old=*os;auto& direct=*ds;auto& native=*ns;
        const std::string beaconText=mode==0?std::string(256,'A'):mode==1?std::string("信标 Ω"):std::string("\xff",1);
        const std::string expectedCommitted=mode==2?std::string("\xef\xbf\xbd",3):beaconText;
        std::u16string expectedUnits;for(wchar_t unit : ::widenUtf8(beaconText))expectedUnits.push_back(static_cast<char16_t>(unit));
        for(auto* state:{&old,&direct,&native}){
            clean(*state);RuntimeMultiplayerGameplayPlayerCommunicationCommand beacon;beacon.kind=RuntimePlayerCommunicationKind::PlaceBeacon;
            beacon.cell={state->economy.skirmishStartCell.x,state->economy.skirmishStartCell.y};
            if(!expect(applyRuntimePlayerCommunication(*state,state->economy.owner,beacon),label+" original valid terrain beacon placement"))return false;
            state->playerCommunication.selectedBeacon=beacon.cell;state->playerCommunication.beacons.back().text=beaconText;state->playerCommunication.lines.clear();
            for(auto& binding:state->runtimeMenuKeyboardBindings)if(binding.second==runtimePlayerCurrentKeyCode(VK_RETURN))binding.second=0x7fe;
            setBinding(*state,"Options",VK_ESCAPE);setBinding(*state,"ChatToAll",runtimePlayerCurrentKeyCode(VK_RETURN));state->repaintRequested=false;
        }
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,label+" three real HWNDs"))return false;
        Clock oc,dc,nc;ClockBinding ob(old,oc),db(direct,dc),nb(native,nc);Witness a(old,1),b(direct,1),c(native,1);
        const auto send=[&](UINT message,WPARAM raw){
            LRESULT results[3]{};
            {RuntimeSnapshotActiveStateScope active(old);results[0]=drive(0,ow.window,message,raw,LPARAM{1});}
            {RuntimeSnapshotActiveStateScope active(direct);results[1]=drive(1,dw.window,message,raw,LPARAM{1});}
            {RuntimeSnapshotActiveStateScope active(native);results[2]=drive(2,nw.window,message,raw,LPARAM{1});}
            return expect(results[0]==results[1]&&results[0]==results[2]&&fullState(old,true)==fullState(direct,true)&&fullState(old,true)==fullState(native,true)
                &&a.commands==b.commands&&a.commands==c.commands&&oc.reads==dc.reads&&oc.reads==nc.reads,label+" original prefill/typed/submit state and canonical effects");};
        if(!send(WM_KEYDOWN,VK_RETURN))return false;
        if(!expect(old.playerCommunication.chatOpen&&old.playerCommunication.beaconEditing&&old.playerCommunication.chatDraft==expectedUnits,
            label+" original flags0 UTF8 prefill exact units without80 truncation"))return false;
        if(!expect(runtimeChatTextDraftToWide(old.playerCommunication.chatDraft)==nativeWide(expectedUnits)
            &&runtimeChatTextDraftFromWide(nativeWide(expectedUnits))==expectedUnits,label+" GDI/legacy adapter preserves every UTF16 unit"))return false;
        if(mode==0&&(!send(WM_CHAR,'Z')||!expect(old.playerCommunication.chatDraft.size()==256,label+" typed append does not truncate or extend over80 prefill")))return false;
        if(!send(WM_KEYDOWN,VK_RETURN))return false;
        if(!expect(!old.playerCommunication.chatOpen&&a.commands.size()==1&&old.playerCommunication.beacons.back().text==expectedCommitted,label+" actual prefilled BeaconText commit"))return false;
        trace<<"prefill mode="<<mode<<" permissiveInvalidUtf8Fixture="<<(mode==2)<<" expectedUnits="<<wideText(expectedUnits)
            <<" command="<<captureText(a)<<" state="<<hexText(fullState(old,true))<<" reads="<<oc.reads<<'\n';
        sourceReads+=oc.reads+dc.reads+nc.reads;++preFillCases;
    }
    // The adapter's original guards are distinct for WM_CHAR and modal keydown.
    for(int mode=0;mode<6;++mode){
        const auto label="guard-"+std::to_string(mode);auto os=make(label+"-old"),ds=make(label+"-direct"),ns=make(label+"-native");
        if(!expect(os&&ds&&ns,label+" independent private worlds"))return false;
        auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native}){
            clean(*state);state->playerCommunication.chatOpen=mode!=0;state->playerCommunication.chatDraft=u"kept";
            if(mode==1)state->runtimeMenuOpen=true;
            if(mode==2){state->campaignMode=true;state->triggers.campaignInputLocked=true;}
            if(mode==3||mode==4){state->skirmishMatchFinished=true;state->resultPresentation.started=true;state->resultPresentation.bannerVisible=true;}
        }
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,label+" three actual HWNDs"))return false;
        Clock oc,dc,nc;ClockBinding ob(old,oc),db(direct,dc),nb(native,nc);Witness a(old,1),b(direct,1),c(native,1);
        if(mode==5){SetWindowLongPtrW(ow.window,GWLP_USERDATA,0);SetWindowLongPtrW(dw.window,GWLP_USERDATA,0);SetWindowLongPtrW(nw.window,GWLP_USERDATA,0);}
        const UINT message=mode==4?WM_KEYDOWN:WM_CHAR;const WPARAM raw=mode==4?VK_BACK:'Z';LRESULT results[3]{};
        {RuntimeSnapshotActiveStateScope active(old);results[0]=drive(0,ow.window,message,raw,LPARAM{1});}
        {RuntimeSnapshotActiveStateScope active(direct);results[1]=drive(1,dw.window,message,raw,LPARAM{1});}
        {RuntimeSnapshotActiveStateScope active(native);results[2]=drive(2,nw.window,message,raw,LPARAM{1});}
        if(mode==5){SetWindowLongPtrW(ow.window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&old));SetWindowLongPtrW(dw.window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&direct));SetWindowLongPtrW(nw.window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&native));}
        if(!expect(results[0]==results[1]&&results[0]==results[2]&&fullState(old,true)==fullState(direct,true)&&fullState(old,true)==fullState(native,true)
            &&a.commands.empty()&&b.commands.empty()&&c.commands.empty()&&oc.reads==dc.reads&&oc.reads==nc.reads,label+" complete original guard/default result"))return false;
        if(!expect(old.playerCommunication.chatDraft==(mode==3?u"keptZ":u"kept"),label+" WM_CHAR finished behavior remains distinct from modal finished guard"))return false;
        trace<<"guard mode="<<mode<<" message="<<message<<" result="<<results[0]<<" reads="<<oc.reads<<" state="<<hexText(fullState(old,true))<<'\n';
        sourceReads+=oc.reads+dc.reads+nc.reads;++guardCases;
    }
    {
        auto os=makeBase(1,true),ds=makeBase(1,true),ns=makeBase(1,true);
        if(!expect(os&&ds&&ns,"proven legal active3house save roots"))return false;
        auto& old=*os;auto& direct=*ds;auto& native=*ns;int index=0;
        for(auto* state:{&old,&direct,&native}){
            RuntimeSnapshotActiveStateScope active(*state);
            if(!expect(isolate(*state,"load-"+std::to_string(index++)),"exact private active save identity"))return false;
            clean(*state);state->config.shortGame=true;state->tick=21;(void)advanceRuntimeParticles(*state);
            if(!expect(state->particlesInitialized&&state->particles.currentTick()==21&&state->particles.fixedTickHz()==std::max(1,state->config.fixedTickHz),"real tick21 particle authority"))return false;
        }
        Clock oc,dc,nc;ClockBinding ob(old,oc),db(direct,dc),nb(native,nc);
        const auto load=[&](RuntimeWindowState& state,const Clock& clock){
            const auto path=runtimeDefaultSavePath(state);std::string error;
            RuntimeSnapshotActiveStateScope active(state);
            const bool saveSucceeded=saveRuntimeState(state,path,error);
            const bool loadSucceeded=saveSucceeded&&loadRuntimeState(state,path,error);
            if(!expect(saveSucceeded&&loadSucceeded&&state.tick==21&&state.uiClock==&clock,"actual ordinary save/load retains borrowed clock: "+error))return false;
            ensureRuntimeMenuKeyboardLoaded(state);state.runtimeMenuOpen=false;state.runtimeMenuKeyCaptureIndex=-1;
            state.playerCommunication.chatOpen=true;state.playerCommunication.chatDraft.clear();state.playerCommunication.beaconEditing=false;
            state.playerCommunication.lines.clear();state.repaintRequested=false;
            return expect(runtimeDefaultSavePath(state)==path,"fresh root retains exact private ordinary save path");
        };
        if(!load(old,oc)||!load(direct,dc)||!load(native,nc))return false;
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,"three actual fresh-root HWNDs"))return false;
        Witness a(old,1),b(direct,1),c(native,1);
        for(const auto& [message,raw]:std::array<std::pair<UINT,WPARAM>,3>{{{WM_CHAR,L'中'},{WM_CHAR,L'文'},{WM_KEYDOWN,VK_RETURN}}}){
            {RuntimeSnapshotActiveStateScope active(old);drive(0,ow.window,message,raw,LPARAM{1});}
            {RuntimeSnapshotActiveStateScope active(direct);drive(1,dw.window,message,raw,LPARAM{1});}
            {RuntimeSnapshotActiveStateScope active(native);drive(2,nw.window,message,raw,LPARAM{1});}
            if(!expect(fullState(old,true)==fullState(direct,true)&&fullState(old,true)==fullState(native,true)&&a.commands==b.commands&&a.commands==c.commands,
                "first text/key after scratch commit uses fresh current child state"))return false;
        }
        if(!expect(old.tick==21&&old.particles.currentTick()==21&&old.uiClock==&oc&&direct.uiClock==&dc&&native.uiClock==&nc
            &&a.commands.size()==1&&a.commands.front().applied&&!old.playerCommunication.chatOpen
            &&!old.playerCommunication.lines.empty()&&old.playerCommunication.lines.back().text=="中文","actual post-load chat applied at tick21"))return false;
        trace<<"load tick=21 particles="<<old.particles.currentTick()<<" borrowedClock=1 freshRoot=1 state="<<hexText(fullState(old,true))<<" commands="<<captureText(a)
            <<" oldReads="<<oc.reads<<" directReads="<<dc.reads<<" nativeReads="<<nc.reads<<'\n';
        sourceReads+=oc.reads+dc.reads+nc.reads;++loadCases;
    }
    trace<<"checks="<<checks<<" codecCases="<<codecCases<<" characterCases="<<characterCases<<" keyCases="<<keyCases<<" preFillCases="<<preFillCases
        <<" guards="<<guardCases<<" loadCases="<<loadCases<<" oldCases="<<oldCases<<" directCases="<<directCases<<" nativeCases="<<nativeCases
        <<" acceptedCommands="<<acceptedCommands<<" rejectedCommands="<<rejectedCommands<<" replays="<<replays<<" sourceReads="<<sourceReads<<'\n';
    trace.flush();if(!expect(static_cast<bool>(trace),"trace flush"))return false;
    output<<"Chat-text integration passed: checks="<<checks<<" codecCases="<<codecCases<<" characterCases="<<characterCases<<" keyCases="<<keyCases<<" preFillCases="<<preFillCases
        <<" guards="<<guardCases<<" loadCases="<<loadCases<<" oldCases="<<oldCases<<" directCases="<<directCases<<" nativeCases="<<nativeCases
        <<" acceptedCommands="<<acceptedCommands<<" rejectedCommands="<<rejectedCommands<<" replays="<<replays<<" sourceReads="<<sourceReads
        <<" independentOldWin32Codec=1 frozenOldModal=1 frozenOldWMChar=1 sameProductionCpp=1 realHiddenWndProc=1 raw64BeforeNarrow=1 utf16UnitsOwned=1"
        <<" canonicalExpectedPayload=independent actualOfflineApply=1 interceptedRouterCapture=1 actualOfflineReplay=1 captureRejectionKeepsDraft=1"
        <<" exactPrivateIdentities=1 actualActiveLoadFreshRoot=1 borrowedClock=1 gdiUnitCopy=exact permissivePrefill=flags0"
        <<" observedBeforeWindowDestruction=1 runningBridge=separate-new-contract physicalModifiers=not-run fullIME=not-run secondBackend=not-run"
        <<" fullSnapshot=not-run externalNetwork=not-run performance=not-run observerEndedSave=legacy-failed-not-retested\n";
    return true;
}

