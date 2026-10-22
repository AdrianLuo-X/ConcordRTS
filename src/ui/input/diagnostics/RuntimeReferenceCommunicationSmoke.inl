// Included after the actual private adapters, WindowProc, shared hidden-window fixture and ordinary load.
bool runRuntimeReferenceCommunicationSmokeProbe(RuntimeWindowState& stock,std::ostream& output) {
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
        RuntimeWindowState& state;bool intercepted=false;std::vector<Capture> commands;
        Witness(RuntimeWindowState& value,bool captureOnly):state(value),intercepted(captureOnly){
            initializeRuntimeOfflineGameplayRouting(state);
            state.multiplayerIntentRouter.setCaptureCallback([this](const RuntimeMultiplayerGameplayCommand& command,std::string& error){
                Capture item;item.tick=state.tick;
                if(!encodeRuntimeMultiplayerGameplayCommand(command,item.payload,error))return false;
                if(intercepted){item.accepted=true;error.clear();}
                else item.accepted=item.applied=dispatchRuntimeOfflineGameplayCommand(state,command,error);
                item.error=error;commands.push_back(item);return item.accepted;
            });
        }
        ~Witness(){state.multiplayerIntentRouter.setCaptureCallback({});}
        bool replay(std::string& error){
            if(!intercepted)return true;
            for(const auto& captured:commands){RuntimeMultiplayerGameplayCommand command;
                if(!decodeRuntimeMultiplayerGameplayCommand(captured.payload,command,error)
                    ||!dispatchRuntimeOfflineGameplayCommand(state,command,error))return false;}
            return true;
        }
    };
    std::size_t checks=0,oldCases=0,directCases=0,nativeCases=0,sourceReads=0,modalCases=0,guardCases=0,precedenceCases=0;
    std::size_t appliedCaptures=0,interceptedCaptures=0,bareDecimalCases=0,allianceCases=0,waypointCases=0;
    const auto expect=[&](bool ok,const std::string& why){++checks;if(!ok)output<<"Reference-communication integration failed: "<<why<<'\n';return ok;};
    if(!expect(stock.report.success&&stock.world.getSummary().loaded&&!stock.world.getTerrainCells().empty()
        &&stock.rulesLoaded&&stock.artLoaded&&stock.theaterLoaded,"actual stock resources"))return false;
    const char* requested=std::getenv("CONCORD_REFERENCE_COMMUNICATION_OUTPUT");
    const std::filesystem::path directory=requested&&*requested?std::filesystem::path(requested):std::filesystem::path("reference-communication-smoke-output");
    std::error_code ec;std::filesystem::create_directories(directory,ec);if(!expect(!ec,"isolated output directory"))return false;
    std::ofstream trace(directory/"reference-communication-trace.txt",std::ios::out|std::ios::trunc);if(!expect(static_cast<bool>(trace),"trace file"))return false;
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
    const auto make=[&](bool ready=false){
        auto storage=makeBase(ready?1:0,true);if(!storage)return storage;auto& state=*storage;
        RuntimeSnapshotActiveStateScope active(state);state.tick=7;state.playerCommunication={};
        state.runtimeSuppressPlayerAudioFeedback=false;state.playerCommunication.chatDraft=u"old draft";
        state.playerCommunication.diplomacyPressedControl=4;state.playerCommunication.diplomacyHoveredControl=7;
        state.playerLocalCommands.followTargeting=state.playerLocalCommands.rallyTargeting=true;
        state.playerCommunication.beaconTargeting=false;state.leftPointer.active=state.leftPointer.moved=true;
        const auto owner=runtimeCanonicalHumanHouseOwner(state.economy.owner);
        int nextId=1;for(const auto& object:state.world.getObjects())nextId=std::max(nextId,object.instanceId+1);
        WorldObjectInstance infantry;infantry.instanceId=nextId;infantry.mapId="ReferenceCommunicationInfantry";
        infantry.kind=WorldObjectKind::Infantry;infantry.type="E1";infantry.owner=state.economy.owner;
        infantry.x=state.economy.skirmishStartCell.x;infantry.y=state.economy.skirmishStartCell.y;
        infantry.cell=runtimeCellKey(infantry.x,infantry.y);infantry.level=terrainLevelAtRuntimeCell(state,infantry.x,infantry.y);
        infantry.alive=infantry.poweredOperational=true;infantry.health=runtimeMaxHealthForObject(state,infantry);infantry.mission="Guard";
        if(!expect(findRuntimeUnitType(state.rules,infantry)&&infantry.health>0,"actual stock E1 rule/health"))return std::unique_ptr<RuntimeWindowState>{};
        state.world.accessObjects().push_back(std::move(infantry));
        state.selectedObjectIds.clear();for(const auto& object:state.world.getObjects())
            if(object.alive&&runtimeCanonicalHumanHouseOwner(object.owner)!=owner){state.selectedObjectIds={object.instanceId};break;}
        if(!expect(!state.selectedObjectIds.empty(),"actual foreign current-root object"))return std::unique_ptr<RuntimeWindowState>{};
        RuntimeMultiplayerGameplayPlayerCommunicationCommand beacon;beacon.kind=RuntimePlayerCommunicationKind::PlaceBeacon;
        beacon.cell={state.economy.skirmishStartCell.x,state.economy.skirmishStartCell.y};
        if(!expect(findRuntimeTerrainCellAt(state,{beacon.cell.x,beacon.cell.y})
            &&applyRuntimePlayerCommunication(state,state.economy.owner,beacon),"real valid-cell beacon fixture"))return std::unique_ptr<RuntimeWindowState>{};
        state.playerCommunication.selectedBeacon=beacon.cell;state.playerCommunication.beacons.back().text="信标 Ω";
        state.playerCommunication.lines.clear();ensureRuntimeSuperWeaponsInitialized(state);
        state.runtimeSoundEvents=0;state.runtimeSoundIntentHistory.clear();state.runtimeSoundPlaybackHistory.clear();
        state.lastRuntimeSound.clear();state.interactionStatus="communication initial";state.repaintRequested=false;return storage;
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
    const char* actions[]={"Diplomacy","ToggleAlliance","ChatToAll","ChatToAllies","PlaceBeacon","DeleteBeacon","AllToCheer",
        "Taunt_1","Taunt_2","Taunt_3","Taunt_4","Taunt_5","Taunt_6","Taunt_7","Taunt_8","DeleteBeacon"};
    const auto configure=[&](RuntimeWindowState& state,int action,WPARAM raw=VK_F13){
        state.runtimeMenuKeyboardLoadAttempted=true;state.runtimeMenuKeyboardBindings.clear();setBinding(state,"Options",VK_ESCAPE);
        setBinding(state,actions[action],runtimePlayerCurrentKeyCode(raw));
    };
    // Independent original Windows codec; the old modal never calls the new codec.
    const auto oldWin32ChatUtf8=[](const std::wstring& text)->std::string {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0 || size > static_cast<int>(runtimeMultiplayerGameplayMaximumIdentifierBytes)) return {};
    std::string encoded(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        encoded.data(), size, nullptr, nullptr);
    return encoded;
};
    const auto runtimePlayerChatUtf8=[&](const std::u16string& units) {
        std::wstring oldUnits;oldUnits.reserve(units.size());
        for(char16_t unit:units)oldUnits.push_back(static_cast<wchar_t>(unit));
        return oldWin32ChatUtf8(oldUnits);
    };
    // Only the representation bridge changes: original flags-0 prefill remains native.
    const auto widenUtf8=[](const std::string& text) {
        const auto oldUnits=::widenUtf8(text);std::u16string units;units.reserve(oldUnits.size());
        for(wchar_t unit:oldUnits)units.push_back(static_cast<char16_t>(unit));
        return units;
    };
    const auto oldToggle=[&](RuntimeWindowState& state, const std::string& target) {
    if (target.empty()) return;
    const auto owner = runtimeCanonicalHumanHouseOwner(state.economy.owner);
    const auto offer = state.playerCommands.alliances.find({owner, target});
    const bool offered = offer == state.playerCommands.alliances.end()
        ? runtimeOwnersAreAllied(state, owner, target) : offer->second;
    RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
    command.kind = RuntimePlayerCommunicationKind::Alliance;
    command.text = target; command.value = offered ? 0 : 1;
    dispatchRuntimePlayerCommunicationFromUi(state, command);
};

    const auto oldModal=[&](RuntimeWindowState& state, int key, bool repeated) {
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
    if (!ui.diplomacyOpen) return false;
    const auto rows = runtimeDiplomacyRows(state);
    const int count=static_cast<int>(std::min<std::size_t>(rows.size(),8));
    if (key == VK_ESCAPE || key == VK_TAB || key == VK_RETURN
        || key==runtimeMenuKeyboardBindingCode(state,"Diplomacy",VK_TAB)) {
        if (!repeated) closeRuntimeDiplomacyPage(state);
    } else if (count>1) {
        ui.diplomacyRow = std::clamp(ui.diplomacyRow, 1, count-1);
        if (key == VK_UP) ui.diplomacyRow = ui.diplomacyRow==1?count-1:ui.diplomacyRow-1;
        else if (key == VK_DOWN) ui.diplomacyRow = ui.diplomacyRow==count-1?1:ui.diplomacyRow+1;
        else if ((key == 'A' || key == VK_SPACE) && !repeated
            && runtimeDiplomacyAllianceEnabled(state,rows[ui.diplomacyRow]))
            oldToggle(state,rows[ui.diplomacyRow].owner);
    }
    state.repaintRequested = true;
    return true;
};

    const auto oldReference=[&](RuntimeWindowState& state, const RECT& client, int code, bool repeated) {
    auto& local = state.playerLocalCommands; auto& ui = state.playerCommunication;
    const auto matches = [&](const std::string& id, int fallback) {
        return code == runtimeMenuKeyboardBindingCode(state, id, fallback);
    };
    RuntimeReferencePresentationUiHost presentationHost(state, client);
    if (handleRuntimeReferencePresentationKey({code, repeated}, local, presentationHost)) return true;
    if (matches("Diplomacy", VK_TAB)) {
        if (!repeated) openRuntimeDiplomacyPage(state);
        return true;
    }
    if (matches("ToggleAlliance", 'A')) {
        if (repeated) return true;
        std::string target;
        if (!state.selectedObjectIds.empty())
            if (const auto* object = findObjectById(state, state.selectedObjectIds.front()))
                if (runtimeCanonicalHumanHouseOwner(object->owner) != runtimeCanonicalHumanHouseOwner(state.economy.owner))
                    target = runtimeCanonicalHumanHouseOwner(object->owner);
        if (target.empty()) openRuntimeDiplomacyPage(state);
        else oldToggle(state, target);
        return true;
    }
    if (matches("ChatToAll", VK_RETURN) || matches("ChatToAllies", VK_HOME)) {
        if (!repeated) {
            ui.chatOpen = true; ui.chatAllies = matches("ChatToAllies", VK_HOME); ui.chatDraft.clear();
            ui.beaconEditing=false;
            if (!ui.chatAllies) if (const auto* beacon=runtimePlayerSelectedBeacon(state)) {
                ui.beaconEditing=true;ui.chatDraft=widenUtf8(beacon->text);
            }
            ui.beaconTargeting = local.followTargeting = local.rallyTargeting = false;
            state.leftPointer.active = state.leftPointer.moved = false;
        }
        return true;
    }
    if (matches("PlaceBeacon", 'B')) {
        if (!repeated) {
            if (!ui.beaconTargeting) {
                const auto owner=runtimePlayerCommunicationViewerOwner(state);
                const auto count=std::count_if(ui.beacons.begin(),ui.beacons.end(),[&](const auto& beacon) {
                    return beacon.owner==owner && runtimePlayerBeaconVisibleToLocal(state,beacon);
                });
                if (count>=3) {
                    state.interactionStatus="信标数量已达上限";
                    return true;
                }
                cancelRuntimeActiveSuperWeaponTargetSelection(state,"beacon");
                dismissRuntimePlacementFromUi(state);
            }
            ui.beaconTargeting = !ui.beaconTargeting;
            ui.selectedBeacon.reset();
            local.followTargeting = local.rallyTargeting = false;
            state.activeSidebarTool = RuntimeSidebarTool::None;
        }
        return true;
    }
    if (matches("DeleteBeacon", VK_DELETE) || code == VK_DECIMAL) {
        if (repeated) return true;
        if (local.planning && local.selectedWaypoint) {
            dispatchRuntimePlayerWaypointFromUi(state, RuntimePlayerWaypointOperation::DeletePoint, local.selectedWaypoint);
            return true;
        }
        if (const auto* beacon=runtimePlayerSelectedBeacon(state)) {
            RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
            command.kind = RuntimePlayerCommunicationKind::DeleteBeacon; command.cell = beacon->cell;
            dispatchRuntimePlayerCommunicationFromUi(state, command);
        }
        return true;
    }
    if (matches("AllToCheer", 'C')) {
        if (!repeated) {
            RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
            command.kind = RuntimePlayerCommunicationKind::Cheer;
            dispatchRuntimePlayerCommunicationFromUi(state, command);
        }
        return true;
    }
    for (int i = 1; i <= 8; ++i) if (matches("Taunt_" + std::to_string(i), VK_F5 + i - 1)) {
        if (!repeated) {
            RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
            command.kind = RuntimePlayerCommunicationKind::Taunt; command.value = static_cast<std::uint8_t>(i);
            dispatchRuntimePlayerCommunicationFromUi(state, command);
        }
        return true;
    }
    return false;
};

    const auto prepare=[&](RuntimeWindowState& state,int action,int mode){
        RuntimeSnapshotActiveStateScope active(state);const auto owner=runtimeCanonicalHumanHouseOwner(state.economy.owner);
        if(action==1){
            if(mode==1)state.selectedObjectIds.clear();
            if(mode==2)state.selectedObjectIds={999999};
            if(mode==3)for(const auto& object:state.world.getObjects())if(runtimeCanonicalHumanHouseOwner(object.owner)==owner){state.selectedObjectIds={object.instanceId};break;}
            if(mode==4)for(auto& object:state.world.accessObjects())if(object.instanceId==state.selectedObjectIds.front())object.alive=false;
            if(mode==5||mode==7){const auto* object=findObjectById(state,state.selectedObjectIds.front());if(!object)return false;
                const auto target=runtimeCanonicalHumanHouseOwner(object->owner);state.playerCommands.alliances[{owner,target}]=mode==7;
                state.playerCommands.alliances[{target,owner}]=true;}
            if(mode==6)state.config.gameMode="FreeForAll";
        }
        if(action==2||action==3||action==5){
            if(mode==1)state.playerCommunication.selectedBeacon=RuntimeMultiplayerGameplayCell{999,999};
            if(mode==2)state.playerCommunication.beacons.front().untilTick=state.tick;
            if(mode==3)state.playerCommunication.beacons.front().owner="skirmishplayer2";
        }
        if(action==4){
            if(mode>=1&&mode<=3){state.playerCommunication.beacons.clear();
                const int desired=mode==1?0:mode==2?2:3;int authored=0;
                for(const auto& cell:state.world.getTerrainCells()){
                    if(authored==desired)break;RuntimeMultiplayerGameplayPlayerCommunicationCommand beacon;
                    beacon.kind=RuntimePlayerCommunicationKind::PlaceBeacon;beacon.cell={cell.x,cell.y};
                    if(!applyRuntimePlayerCommunication(state,state.economy.owner,beacon))return false;++authored;
                }
                state.playerCommunication.lines.clear();if(authored!=desired)return false;
            }
            if(mode==4)state.playerCommunication.beacons.front().untilTick=state.tick;
            if(mode==5)state.playerCommunication.beaconTargeting=true;
            if(mode==6){std::string error;
                const auto placement=runtimeOldestReadyPlacement(state);
                if(!placement||!beginRuntimePlacementFromUi(state,*placement,true)
                    ||!state.multiplayerIntentRouter.beginSuperWeapon("ChronoSphereSpecial",1,error))return false;
                state.playerCommunication.beaconTargeting=false;state.leftPointer.moved=state.leftPointer.active=true;
            }
        }
        if(action==6&&mode==1)state.playerCommunication.nextFeedbackTick[{owner,RuntimePlayerCommunicationKind::Cheer}]=state.tick+1;
        if(action>=7&&action<=14){
            if(mode==1)state.playerCommunication.tauntsEnabled=false;
            if(mode==2)state.playerCommunication.nextFeedbackTick[{owner,RuntimePlayerCommunicationKind::Taunt}]=state.tick+1;
        }
        if(action==15){
            int actor=0;for(const auto& object:state.world.getObjects())if(object.alive&&object.kind==WorldObjectKind::Infantry
                &&runtimeCanonicalHumanHouseOwner(object.owner)==owner){actor=object.instanceId;break;}
            if(!actor)return false;RuntimeMultiplayerGameplayTargetOrderCommand order;order.objectIds={actor};
            order.target=RuntimeMultiplayerGameplayCellTarget{{state.economy.skirmishStartCell.x,state.economy.skirmishStartCell.y}};
            const auto point=state.playerCommands.waypoints.nextId;
            if(!applyRuntimePlayerWaypointCommand(state,owner,{RuntimePlayerWaypointOperation::Begin,0})
                ||!appendRuntimePlayerWaypointOrder(state,owner,order))return false;
            state.playerLocalCommands.planning=true;state.playerLocalCommands.selectedWaypoint=mode==1?0:point;
            state.interactionStatus="communication initial";state.repaintRequested=false;
        }
        return true;
    };
    const auto triple=[&](const std::string& label,int action,bool repeated,UINT message,bool intercepted,int mode=0,bool decimal=false){
        auto os=make(action==4&&mode==6),ds=make(action==4&&mode==6),ns=make(action==4&&mode==6);
        if(!expect(os&&ds&&ns,label+" independent real stock worlds"))return false;
        auto& old=*os;auto& direct=*ds;auto& native=*ns;const WPARAM raw=decimal?VK_DECIMAL:VK_F13;
        for(auto* state:{&old,&direct,&native}){configure(*state,action,raw);
            if(decimal)setBinding(*state,"DeleteBeacon",VK_F14);
            if(!expect(prepare(*state,action,mode),label+" actual consumer fixture"))return false;}
        Clock oc,dc,nc;ClockBinding ob(old,oc),db(direct,dc),nb(native,nc);
        Witness ow(old,intercepted),dw(direct,intercepted),nw(native,intercepted);
        RuntimePlayerDefeatNoticeSmokeWindow window(native,1024,768);if(!expect(window.window!=nullptr,label+" real hidden HWND"))return false;
        if(!expect(fullState(old)==fullState(direct)&&fullState(old)==fullState(native),label+" equal complete entry state"))return false;
        const auto before=fullState(old);const auto beforeWorld=old.world.deterministicSimulationStateHash();
        const auto beforeQueue=old.playerCommands.serialize();const auto code=runtimePlayerCurrentKeyCode(raw);
        bool oh=false,dh=false;
        {RuntimeSnapshotActiveStateScope active(old);(void)runtimeUiClockNow(old);oh=oldReference(old,client,code,repeated);}
        {RuntimeSnapshotActiveStateScope active(direct);(void)runtimeUiClockNow(direct);
            RuntimeReferencePresentationUiHost front(direct,client);
            if(handleRuntimeReferencePresentationKey({code,repeated},direct.playerLocalCommands,front))dh=true;
            else{RuntimeReferenceCommunicationUiHost host(direct);dh=handleRuntimeReferenceCommunicationKey({code,repeated},direct.playerLocalCommands,direct.playerCommunication,direct.selectedObjectIds,host);}}
        if(!expect(oh&&dh&&fullState(old)==fullState(direct)&&ow.commands==dw.commands,label+" frozen old and samecpp complete state/canonical capture"))return false;
        const bool controllerRepaint=old.repaintRequested;old.repaintRequested=direct.repaintRequested=true;
        LRESULT result=0;{RuntimeSnapshotActiveStateScope active(native);result=SendMessageW(window.window,message,raw,repeated?(static_cast<LPARAM>(1)<<30):0);}
        if(fullState(old)!=fullState(native))output<<"Reference-communication mismatch label="<<label<<" old="<<hexText(fullState(old))<<" native="<<hexText(fullState(native))<<'\n';
        if(!expect(result==0&&runtimePlayerCurrentKeyCode(raw)==code&&fullState(old)==fullState(native)&&ow.commands==nw.commands,
                label+" real native complete state and canonical capture including handled epilogue"))return false;
        if(!expect(oc.reads==1&&dc.reads==1&&nc.reads==1,label+" original key source once"))return false;
        const bool authorityAction=action==1||action==5||action==6||(action>=7&&action<=15);
        std::size_t expected=(!repeated&&authorityAction)?1:0;
        if(action==1&&(mode==1||mode==2||mode==3||mode==4||mode==6))expected=0;
        if(action==5&&mode!=0)expected=0;
        if(!expect(ow.commands.size()==expected,label+" exact accepted capture count"))return false;
        if(repeated&&!expect(old.world.deterministicSimulationStateHash()==beforeWorld&&old.playerCommands.serialize()==beforeQueue&&ow.commands.empty(),label+" repeat no authority changes"))return false;
        if(action==2&&!repeated&&mode==0&&!expect(old.playerCommunication.chatOpen&&old.playerCommunication.beaconEditing
            &&old.playerCommunication.chatDraft==u"信标 Ω"&&!old.playerLocalCommands.rallyTargeting&&!old.playerLocalCommands.followTargeting
            &&!old.playerCommunication.beaconTargeting&&!old.leftPointer.moved&&!old.leftPointer.active,label+" real UTF8 beacon edit and original target/drag clears"))return false;
        if(action==3&&!repeated&&!expect(old.playerCommunication.chatOpen&&old.playerCommunication.chatAllies&&!old.playerCommunication.beaconEditing
            &&old.playerCommunication.chatDraft.empty(),label+" allies avoids selected-beacon edit"))return false;
        if(action==4&&!repeated&&mode==3&&!expect(!old.playerCommunication.beaconTargeting&&old.interactionStatus=="信标数量已达上限"
            &&old.leftPointer.moved&&old.leftPointer.active,label+" exact three-limit early return"))return false;
        if(action==4&&!repeated&&mode==6&&!expect(old.playerCommunication.beaconTargeting&&!old.multiplayerIntentRouter.superWeapon()
            &&!runtimeSelectedPlacement(old)&&!old.multiplayerIntentRouter.placement(),label+" real ready placement and local SW intent canceled"))return false;
        for(const auto& captured:ow.commands){RuntimeMultiplayerGameplayCommand command;std::string error;
            if(!expect(captured.tick==old.tick&&captured.accepted&&captured.applied==!intercepted
                &&decodeRuntimeMultiplayerGameplayCommand(captured.payload,command,error),label+" canonical source/tick/application separation"))return false;
            if(const auto* c=std::get_if<RuntimeMultiplayerGameplayPlayerCommunicationCommand>(&command.body)){
                if(action==1&&!expect(c->kind==RuntimePlayerCommunicationKind::Alliance&&c->value==(mode==7?0:1),label+" exact alliance offer precedence payload"))return false;
                if(action==5&&!expect(c->kind==RuntimePlayerCommunicationKind::DeleteBeacon,label+" exact DeleteBeacon payload"))return false;
                if(action==6&&!expect(c->kind==RuntimePlayerCommunicationKind::Cheer,label+" exact Cheer payload"))return false;
                if(action>=7&&action<=14&&!expect(c->kind==RuntimePlayerCommunicationKind::Taunt&&c->value==action-6,label+" exact Taunt payload/value"))return false;
            }else if(action==15){const auto* c=std::get_if<RuntimeMultiplayerGameplayWaypointCommand>(&command.body);
                if(!expect(c&&c->operation==RuntimePlayerWaypointOperation::DeletePoint,label+" exact planning DeletePoint priority"))return false;}
        }
        const auto print=[&](const char* path,const RuntimeWindowState& state,const Witness& witness){trace<<"communication path="<<path<<" label="<<label<<" action="<<action<<" mode="<<mode
            <<" repeat="<<repeated<<" source="<<message<<" route="<<(intercepted?"offline-intercepted-capture":"offline-real-apply")
            <<" raw="<<raw<<" code="<<code<<" controllerRepaint="<<controllerRepaint<<" state="<<hexText(fullState(state))<<" capture="<<captureText(witness)<<'\n';};
        print("old",old,ow);print("direct",direct,dw);print("native",native,nw);
        if(intercepted){std::string error;if(!expect(ow.replay(error)&&dw.replay(error)&&nw.replay(error),label+" replay through actual prevalidation/application owner: "+error))return false;
            if(!expect(fullState(old)==fullState(direct)&&fullState(old)==fullState(native),label+" all real post-replay effects equal"))return false;
            trace<<"replay label="<<label<<" action="<<action<<" commandCount="<<ow.commands.size()<<" actualOfflineConsumer=1 sameTick="<<old.tick<<" state="<<hexText(fullState(old))<<'\n';}
        if(action==1&&!repeated&&expected){++allianceCases;const auto* object=findObjectById(old,old.selectedObjectIds.front());
            if(!expect(object&&old.playerCommands.alliances.at({runtimeCanonicalHumanHouseOwner(old.economy.owner),runtimeCanonicalHumanHouseOwner(object->owner)})==(mode!=7),label+" real alliance offer state"))return false;}
        if(action==5&&!repeated&&mode==0&&!expect(old.playerCommunication.beacons.empty()&&!old.playerCommunication.selectedBeacon,label+" actual Delete removes selected owned marker"))return false;
        if(action==6&&!repeated&&mode==0){const auto key=std::make_pair(runtimeCanonicalHumanHouseOwner(old.economy.owner),RuntimePlayerCommunicationKind::Cheer);
            if(!expect(!old.playerCommunication.cheerUntilTick.empty()&&old.playerCommunication.nextFeedbackTick.at(key)==old.tick+10*std::max(1,old.config.fixedTickHz),label+" original cheer consumer and10-second feedback tick"))return false;}
        if(action==15&&!repeated&&mode==0){++waypointCases;if(!expect(old.playerCommands.waypoints.routes.empty()&&old.playerLocalCommands.selectedWaypoint==0,label+" actual planned point removed"))return false;}
        sourceReads+=oc.reads+dc.reads+nc.reads;if(intercepted)interceptedCaptures+=ow.commands.size();else appliedCaptures+=ow.commands.size();
        ++oldCases;++directCases;++nativeCases;if(decimal)++bareDecimalCases;(void)before;return true;
    };
    for(int action=0;action<16;++action)for(bool repeated:{false,true})for(UINT message:{WM_KEYDOWN,WM_SYSKEYDOWN})
        if(!triple("reference-matrix",action,repeated,message,false))return false;
    for(int action=0;action<16;++action)if(!triple("intercepted-router",action,false,action%2?WM_KEYDOWN:WM_SYSKEYDOWN,true))return false;
    for(int mode=1;mode<=7;++mode)if(!triple("alliance-boundary",1,false,WM_KEYDOWN,mode%2!=0,mode))return false;
    for(int action:{2,3,5})for(int mode=1;mode<=3;++mode)if(!triple("selected-beacon-boundary",action,false,WM_SYSKEYDOWN,false,mode))return false;
    for(int mode=1;mode<=6;++mode)if(!triple("beacon-limit-mode-cancel",4,false,WM_KEYDOWN,mode%2!=0,mode))return false;
    if(!triple("cheer-rate-limit",6,false,WM_KEYDOWN,false,1)||!triple("taunt-disabled",7,false,WM_SYSKEYDOWN,false,1)
        ||!triple("taunt-rate-limit",14,false,WM_KEYDOWN,true,2)||!triple("planning-no-selected-point",15,false,WM_KEYDOWN,false,1))return false;
    if(runtimePlayerCurrentKeyCode(VK_DECIMAL)==VK_DECIMAL&&!triple("bare-decimal",5,false,WM_KEYDOWN,false,0,true))return false;    const auto modalTriple=[&](int key,bool repeated,int mode=0){
        auto os=make(),ds=make(),ns=make();if(!expect(os&&ds&&ns,"modal independent real worlds"))return false;
        auto& old=*os;auto& direct=*ds;auto& native=*ns;
        for(auto* state:{&old,&direct,&native}){configure(*state,0);setBinding(*state,"Diplomacy",VK_F13);state->playerCommunication.diplomacyOpen=true;
            state->playerCommunication.diplomacyRow=mode==1?-17:mode==2?999:1;
            if(mode==3)state->config.playerSlots.resize(1);
            if(mode==4){state->config.playerSlots.clear();state->world.accessObjects().clear();state->multiplayerLocalSpectator=true;}
            if(mode==5)state->config.gameMode="FreeForAll";
            if(mode==6)state->multiplayerDefeatedOwners.insert("skirmishplayer2");
            if(mode==7){state->playerCommunication.chatOpen=true;state->playerCommunication.chatDraft=u"chat owns this key";}
        }
        Clock oc,dc,nc;ClockBinding ob(old,oc),db(direct,dc),nb(native,nc);Witness ow(old,false),dw(direct,false),nw(native,false);
        RuntimePlayerDefeatNoticeSmokeWindow window(native,1024,768);if(!expect(window.window!=nullptr,"modal real hidden HWND"))return false;
        if(!expect(fullState(old)==fullState(direct)&&fullState(old)==fullState(native),"modal equal entry state"))return false;
        bool oh=false,dh=false;
        {RuntimeSnapshotActiveStateScope active(old);oh=oldModal(old,key,repeated);}
        {RuntimeSnapshotActiveStateScope active(direct);if(direct.playerCommunication.chatOpen)dh=handleRuntimePlayerModalKey(direct,key,repeated);
            else{RuntimeDiplomacyNavigationUiHost host(direct);dh=handleRuntimeDiplomacyNavigationKey({key,repeated},direct.playerCommunication,host);}}
        LRESULT result=0;{RuntimeSnapshotActiveStateScope active(native);result=SendMessageW(window.window,WM_KEYDOWN,static_cast<WPARAM>(key),repeated?(static_cast<LPARAM>(1)<<30):0);}
        if(!expect(oh&&dh&&result==0&&fullState(old)==fullState(direct)&&fullState(old)==fullState(native)
            &&ow.commands==dw.commands&&ow.commands==nw.commands,"old/direct/actual modal full state/canonical effects"))return false;
        if(!expect(oc.reads==0&&dc.reads==0&&nc.reads==0,"modal consumes before original key-clock sample"))return false;
        const bool alliance=(key=='A'||key==VK_SPACE)&&!repeated&&mode<3;
        if(!expect(ow.commands.size()==(alliance?1U:0U),"actual modal eligibility/repeat/close capture count"))return false;
        if(alliance){RuntimeMultiplayerGameplayCommand command;std::string error;if(!expect(decodeRuntimeMultiplayerGameplayCommand(ow.commands.front().payload,command,error),"modal canonical envelope"))return false;
            const auto* c=std::get_if<RuntimeMultiplayerGameplayPlayerCommunicationCommand>(&command.body);
            if(!expect(c&&c->kind==RuntimePlayerCommunicationKind::Alliance&&c->value==1&&old.playerCommands.alliances.at({runtimeCanonicalHumanHouseOwner(old.economy.owner),c->text}),"modal actual alliance payload/apply"))return false;++allianceCases;}
        if(mode<3&&key==VK_UP&&!expect(old.playerCommunication.diplomacyRow==(mode==2?1:2),"Up including repeat/clamp/wrap"))return false;
        if(mode<3&&key==VK_DOWN&&!expect(old.playerCommunication.diplomacyRow==(mode==2?1:2),"Down including repeat/clamp/wrap"))return false;
        if(mode==0&&(key==VK_ESCAPE||key==VK_TAB||key==VK_RETURN||key==VK_F13)&&!expect(old.playerCommunication.diplomacyOpen==repeated,"close keys consume repeat without closing"))return false;
        const auto print=[&](const char* path,const RuntimeWindowState& state,const Witness& witness){trace<<"modal path="<<path<<" key="<<key<<" repeat="<<repeated<<" mode="<<mode<<" state="<<hexText(fullState(state))<<" capture="<<captureText(witness)<<" sourceReads=0\n";};
        print("old",old,ow);print("direct",direct,dw);print("native",native,nw);appliedCaptures+=ow.commands.size();++oldCases;++directCases;++nativeCases;++modalCases;return true;
    };
    for(int key:std::array<int,8>{VK_UP,VK_DOWN,'A',VK_SPACE,VK_ESCAPE,VK_TAB,VK_RETURN,VK_F13})for(bool repeated:{false,true})if(!modalTriple(key,repeated))return false;
    for(const auto& test:std::array<std::pair<int,int>,7>{{{VK_UP,1},{VK_DOWN,2},{'A',3},{VK_DOWN,4},{'A',5},{'A',6},{VK_F13,7}}})if(!modalTriple(test.first,false,test.second))return false;
    for(int mode=0;mode<6;++mode){
        auto storage=make();if(!expect(static_cast<bool>(storage),"outer guard actual world"))return false;auto& state=*storage;configure(state,6);
        if(mode==0)state.runtimeMenuOpen=true;if(mode==1)state.playerCommunication.chatOpen=true;if(mode==2)state.playerCommunication.diplomacyOpen=true;
        if(mode==3)state.skirmishMatchFinished=true;if(mode==4){state.campaignMode=true;state.triggers.campaignInputLocked=true;}
        if(mode==5){state.runtimeMenuOpen=true;state.runtimeMenuPage=RuntimeMenuPage::Keyboard;state.runtimeMenuKeyCaptureIndex=0;}
        Clock clock;ClockBinding binding(state,clock);Witness witness(state,false);RuntimePlayerDefeatNoticeSmokeWindow window(state,1024,768);if(!expect(window.window!=nullptr,"outer guard real hidden HWND"))return false;
        const auto beforeWorld=state.world.deterministicSimulationStateHash();const auto beforeQueue=state.playerCommands.serialize();const int tick=state.tick;
        {RuntimeSnapshotActiveStateScope active(state);SendMessageW(window.window,WM_KEYDOWN,VK_F13,0);}
        if(!expect(state.world.deterministicSimulationStateHash()==beforeWorld&&state.playerCommands.serialize()==beforeQueue&&state.tick==tick&&clock.reads==0&&witness.commands.empty(),"original modal/menu/capture/campaign guards block Cheer before key sample"))return false;
        trace<<"guard mode="<<mode<<" state="<<hexText(fullState(state))<<" captures=0 sourceReads=0\n";++guardCases;
    }
    for(int mode=0;mode<7;++mode){
        auto storage=make();if(!expect(static_cast<bool>(storage),"priority actual world"))return false;auto& state=*storage;
        const WPARAM raw=mode==3?VK_NUMPAD1:VK_F13;configure(state,mode==3||mode==6?2:6,raw);const auto code=runtimePlayerCurrentKeyCode(raw);const auto selection=state.selectedObjectIds;
        if(mode==0)setBinding(state,"StructureTab",code);if(mode==1){setBinding(state,"ToggleAlliance",code);setBinding(state,"Taunt_1",code);}if(mode==2)setBinding(state,"StopObject",code);
        if(mode==3)setBinding(state,"TeamSelect_1",(code&~0xff)|'1');if(mode==4){state.campaignMode=true;state.triggers.campaignInputLocked=true;}if(mode==5)setBinding(state,"AllToCheer",runtimePlayerCurrentKeyCode(VK_F14));
        RuntimeMultiplayerRuntimeBridge bareBridge;if(mode==6){state.config.multiplayerActive=true;state.multiplayerLocalSpectator=true;state.multiplayerBridge=&bareBridge;}
        Clock clock;ClockBinding binding(state,clock);Witness witness(state,false);RuntimePlayerDefeatNoticeSmokeWindow window(state,1024,768);if(!expect(window.window!=nullptr,"priority hidden HWND"))return false;
        {RuntimeSnapshotActiveStateScope active(state);SendMessageW(window.window,mode==4||mode==5?WM_SYSKEYDOWN:WM_KEYDOWN,raw,0);}
        const bool authors=mode==1||mode==2||mode==4;if(!expect(witness.commands.size()==(authors?1U:0U)&&clock.reads==1,"original priority/campaignSYSKEY/unknown authority count"))return false;
        if(authors){RuntimeMultiplayerGameplayCommand command;std::string error;if(!expect(decodeRuntimeMultiplayerGameplayCommand(witness.commands.front().payload,command,error),"priority real canonical command"))return false;
            const auto* communication=std::get_if<RuntimeMultiplayerGameplayPlayerCommunicationCommand>(&command.body);if(!expect(communication&&communication->kind==(mode==1?RuntimePlayerCommunicationKind::Alliance:RuntimePlayerCommunicationKind::Cheer),"Reference wins lower action binding"))return false;}
        if(mode==0&&!expect(!state.playerCommunication.chatOpen&&state.activeBuildTab==RuntimeBuildKind::Building,"04 prefix consumes before communication"))return false;
        if(mode==3&&!expect(state.playerCommunication.chatOpen&&state.playerLocalCommands.lastSelectionKey=='?'&&state.selectedObjectIds==selection,"Reference precedes raw NumPad normalization"))return false;
        if(mode==6&&!expect(!runtimePlayerSelectionAllowed(state)&&state.selectedObjectIds.empty()&&state.playerCommunication.chatOpen,"actual MP spectator clears selection then allows local chat"))return false;
        trace<<"priority mode="<<mode<<" raw="<<raw<<" source="<<(mode==4||mode==5?WM_SYSKEYDOWN:WM_KEYDOWN)<<" state="<<hexText(fullState(state))<<" capture="<<captureText(witness)<<" sourceReads="<<clock.reads<<'\n';
        state.multiplayerBridge=nullptr;sourceReads+=clock.reads;++precedenceCases;
    }
    for(int mode=0;mode<2;++mode){
        auto storage=make();if(!expect(static_cast<bool>(storage),"bare bridge rejection world"))return false;auto& state=*storage;configure(state,6);
        RuntimeMultiplayerRuntimeBridge bridge;state.config.multiplayerActive=true;state.multiplayerBridge=&bridge;state.multiplayerLocalSpectator=mode==0;
        if(mode==1)for(auto& slot:state.config.playerSlots)slot.local=false;
        if(!expect(mode==0||std::none_of(state.config.playerSlots.begin(),state.config.playerSlots.end(),[](const RuntimeConfig::PlayerSlot& slot){return slot.enabled&&slot.human&&slot.local;}),"bare bridge fixture has no authenticated local human owner"))return false;
        Clock clock;ClockBinding binding(state,clock);Witness witness(state,false);RuntimePlayerDefeatNoticeSmokeWindow window(state,1024,768);if(!expect(window.window!=nullptr,"bare bridge rejection hidden HWND"))return false;
        const auto beforeWorld=state.world.deterministicSimulationStateHash();const auto beforeQueue=state.playerCommands.serialize();{RuntimeSnapshotActiveStateScope active(state);SendMessageW(window.window,WM_KEYDOWN,VK_F13,0);}
        const auto& authority=state.multiplayerIntentRouter.authorityState();if(!expect(witness.commands.empty()&&bridge.pendingGameplayCommandCount()==0&&state.world.deterministicSimulationStateHash()==beforeWorld&&state.playerCommands.serialize()==beforeQueue
            &&(mode==0?authority.spectator:authority.localDefeated),"unstarted real bridge cannot authenticate authority or accept Cheer"))return false;
        trace<<"bareBridgeRejected mode="<<mode<<" spectator="<<authority.spectator<<" localDefeated="<<authority.localDefeated<<" captures=0 pending=0 runningMPsession=not-run state="<<hexText(fullState(state))<<'\n';
        state.multiplayerBridge=nullptr;++guardCases;sourceReads+=clock.reads;
    }
    {
        auto storage=makeBase(1,true);if(!expect(static_cast<bool>(storage),"actual active three-house save world"))return false;auto& state=*storage;
        RuntimeSnapshotActiveStateScope active(state);state.config.shortGame=true;state.tick=21;(void)advanceRuntimeParticles(state);
        if(!expect(state.particlesInitialized&&state.particles.currentTick()==21&&state.particles.fixedTickHz()==std::max(1,state.config.fixedTickHz),"real active particle/save authority tick"))return false;
        Clock clock;ClockBinding binding(state,clock);const auto savePath=pathString(directory/"reference-communication-load.ini");std::string error;
        const bool saveSucceeded=saveRuntimeState(state,savePath,error);
        const bool loadSucceeded=saveSucceeded&&loadRuntimeState(state,savePath,error);
        if(!expect(saveSucceeded&&loadSucceeded&&state.tick==21&&state.uiClock==&clock,"actual ordinary load with borrowed clock preserved: "+error))return false;
        // No pre-load object, row, beacon, catalog or local-state reference survives here.
        configure(state,1);state.selectedObjectIds.clear();const auto owner=runtimeCanonicalHumanHouseOwner(state.economy.owner);
        for(const auto& object:state.world.getObjects())if(object.alive&&runtimeCanonicalHumanHouseOwner(object.owner)!=owner){state.selectedObjectIds={object.instanceId};break;}
        if(!expect(!state.selectedObjectIds.empty(),"fresh current-root foreign actor after load"))return false;
        const auto* current=findObjectById(state,state.selectedObjectIds.front());const std::string target=current?runtimeCanonicalHumanHouseOwner(current->owner):std::string{};
        if(!expect(!target.empty(),"fresh current-root target owner after load"))return false;
        Witness witness(state,false);RuntimePlayerDefeatNoticeSmokeWindow window(state,1024,768);if(!expect(window.window!=nullptr,"post-load hidden HWND"))return false;
        const auto reads=clock.reads;{RuntimeSnapshotActiveStateScope fresh(state);SendMessageW(window.window,WM_KEYDOWN,VK_F13,0);}
        if(!expect(state.tick==21&&state.uiClock==&clock&&clock.reads==reads+1&&witness.commands.size()==1&&witness.commands.front().applied&&state.playerCommands.alliances.at({owner,target}),"post-load next key uses fresh root and real alliance application"))return false;
        trace<<"load tick=21 particleTick="<<state.particles.currentTick()<<" sourceSame=1 freshRoot=1 actualAllianceApplied=1 state="<<hexText(fullState(state))<<" capture="<<captureText(witness)<<'\n';sourceReads+=clock.reads;
    }
    trace<<"checks="<<checks<<" oldCases="<<oldCases<<" directCases="<<directCases<<" nativeCases="<<nativeCases<<" modalCases="<<modalCases<<" guards="<<guardCases<<" precedenceCases="<<precedenceCases<<" appliedCaptures="<<appliedCaptures<<" interceptedCaptures="<<interceptedCaptures<<'\n';
    trace.flush();if(!expect(static_cast<bool>(trace),"trace flush"))return false;
    output<<"Reference-communication integration passed: checks="<<checks<<" oldCases="<<oldCases<<" directCases="<<directCases<<" nativeCases="<<nativeCases<<" sourceReads="<<sourceReads<<" modalCases="<<modalCases<<" guards="<<guardCases<<" precedenceCases="<<precedenceCases
        <<" appliedCaptures="<<appliedCaptures<<" interceptedCaptures="<<interceptedCaptures<<" allianceCases="<<allianceCases<<" waypointCases="<<waypointCases<<" bareDecimalCases="<<bareDecimalCases
        <<" sameProductionCpp=1 frozenOldTailModalAlliance=1 fullLocalState=1 canonicalPayloadTick=1 actualOfflineApply=1 interceptedRouterCapture=1 interceptedCaptureIsNotRunningMP=1 actualReplayConsumer=1 actualLoadThenAlliance=1 typedClockBorrow=1 oldOuterPriority=1"
        <<" nativeOrderedTrace=observable-command-sound-chat-effects standardOrderedQueries=separate-contract nativeModifierFullMatrix=not-run nativeDiplomacyRowsAbove8=not-run runningMPsession=not-run pendingWireApply=separate-real-bridge-contract naturalReadySuperWeapon=existing-beacon-regression"
        <<" fullMpSnapshot=not-run externalNetwork=not-run audioDeviceContinuity=not-run fullIME=not-covered geometryAlgorithmMigration=not-covered originalOverflowDomain=not-covered\n";return true;
}