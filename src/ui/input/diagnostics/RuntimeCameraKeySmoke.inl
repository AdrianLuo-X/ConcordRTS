// Included after the real camera adapter, existing hidden WindowProc fixture and ordinary loader.
bool runRuntimeCameraKeySmokeProbe(RuntimeWindowState& stock,std::ostream& output) {
    struct Clock final : RuntimeUiClock {
        RuntimeUiTimeMs value=1000;mutable std::size_t reads=0;
        RuntimeUiTimeMs nowMs() const noexcept override {++reads;return value;}
    };
    struct ClockBinding {
        RuntimeWindowState& state;const RuntimeUiClock* previous;
        ClockBinding(RuntimeWindowState& root,const RuntimeUiClock& clock):state(root),previous(root.uiClock){state.uiClock=&clock;}
        ~ClockBinding(){state.uiClock=previous;}
    };
    struct Witness {
        RuntimeWindowState& state;std::size_t captures=0;
        explicit Witness(RuntimeWindowState& root):state(root) {
            initializeRuntimeOfflineGameplayRouting(state);
            state.multiplayerIntentRouter.setCaptureCallback([this](const RuntimeMultiplayerGameplayCommand&,std::string& error){
                ++captures;error="camera diagnostic forbids authority capture";return false;
            });
        }
        ~Witness(){state.multiplayerIntentRouter.setCaptureCallback({});}
    };
    std::size_t checks=0,triples=0,sourceReads=0,guards=0,priorities=0,projectionQueries=0,resizeCases=0,heightCases=0;
    const auto expect=[&](bool condition,const std::string& reason){
        ++checks;if(!condition)output<<"Camera-key integration failed: "<<reason<<'\n';return condition;
    };
    if(!expect(stock.report.success&&stock.world.getSummary().loaded&&!stock.world.getTerrainCells().empty()
        &&stock.rulesLoaded&&stock.artLoaded&&stock.theaterLoaded,"actual stock terrain and render resources"))return false;
    char* requested=nullptr;std::size_t requestedLength=0;
    const auto environmentResult=_dupenv_s(&requested,&requestedLength,"CONCORD_CAMERA_KEY_OUTPUT");
    const std::filesystem::path directory=requested&&*requested?std::filesystem::path(requested):std::filesystem::path("camera-key-smoke-output");
    std::free(requested);if(!expect(environmentResult==0,"isolated output environment"))return false;
    std::error_code ec;std::filesystem::create_directories(directory,ec);if(!expect(!ec,"isolated output directory"))return false;
    std::ofstream trace(directory/"camera-key-trace.txt",std::ios::out|std::ios::trunc);
    if(!expect(static_cast<bool>(trace),"trace file"))return false;
    const auto hexText=[](const std::string& value){std::ostringstream s;for(unsigned char b:value)
        s<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(b);return s.str();};
    const auto rectText=[](const RECT& r){std::ostringstream s;s<<r.left<<','<<r.top<<','<<r.right<<','<<r.bottom;return s.str();};
    const auto pointText=[](const POINT& p){return std::to_string(p.x)+","+std::to_string(p.y);};
    const auto projectionState=[&](const RuntimeWindowState& state){
        std::ostringstream s;s<<std::setprecision(17);
        s<<"valid="<<state.projectedObjectsCacheValid<<";viewport="<<rectText(state.projectedObjectsCacheViewport)
            <<";camera="<<state.projectedObjectsCacheCameraX<<','<<state.projectedObjectsCacheCameraY
            <<";tile="<<state.projectedObjectsCacheTileWidth<<','<<state.projectedObjectsCacheTileHeight
            <<";signature="<<state.projectedObjectsCacheSignature;
        for(const auto& p:state.projectedObjectsCache){
            s<<";object="<<(p.object?p.object->instanceId:-1)<<','<<hexText(p.observerType)<<','<<hexText(p.observerOwner)
                <<','<<static_cast<int>(p.observerKind)<<','<<p.spyDisguiseProjected
                <<";center="<<pointText(p.center)<<";renderCenter="<<pointText(p.renderCenter)
                <<";bounds="<<rectText(p.bounds)<<";sprite="<<rectText(p.spriteBounds)<<','<<p.hasMobileSpriteBounds
                <<";selection="<<rectText(p.selectionBounds)<<";selectionBase="<<rectText(p.selectionBaseBounds)
                <<";projected="<<p.healthPixelOffsetY<<','<<p.drawLevel<<','<<p.projectedX<<','<<p.projectedY
                <<','<<p.usesFootprintHit<<','<<p.hasStructureSpriteGeometry<<','<<p.airborneForeground
                <<','<<p.hasBodyHitClip<<";body="<<rectText(p.bodyHitClip)<<";factory="<<p.factoryForegroundHostId
                <<";footprint="<<rectText(p.footprintBounds)<<','<<p.hasBuildingDepthVisibility
                <<','<<p.hasHostedSortOverride<<','<<p.hostedSortCellSum<<','<<p.footprintHalfWidth<<','<<p.footprintHalfHeight;
            const auto spans=[&](const char* name,const std::vector<RuntimeSpriteHitSpan>& values){
                s<<';'<<name<<'=';for(const auto& v:values)s<<v.y<<','<<v.left<<','<<v.right<<'/';
            };
            spans("structureSpans",p.structureSpriteHitSpans);spans("foregroundSpans",p.structureForegroundHitSpans);
            spans("visibleSpans",p.visibleBuildingHitSpans);s<<";footprintCenters=";
            for(const auto& point:p.footprintHitCenters)s<<pointText(point)<<'/';
        }
        return s.str();
    };
    const auto cameraState=[&](const RuntimeWindowState& state){
        std::ostringstream s;s<<"camera="<<state.camera.worldOffsetX<<','<<state.camera.worldOffsetY
            <<','<<state.camera.tileWidth<<','<<state.camera.tileHeight<<','<<state.camera.initialized
            <<";clamp="<<rectText(state.cameraClampViewport)<<";projection="<<projectionState(state);
        return s.str();
    };
    const auto authorityState=[&](const RuntimeWindowState& state){
        std::ostringstream s;s<<"tick="<<state.tick<<";world="<<state.world.deterministicSimulationStateHash()
            <<";commands="<<hexText(state.playerCommands.serialize())<<";commandHash="<<state.playerCommands.stateHash()
            <<";credits="<<state.economy.credits<<";lanes="<<hexText(runtimeSerializeProductionLanes(state.economy.localProductionLanes))
            <<";superWeapons="<<state.superWeapons.stateHash()
            <<";lastCommand="<<state.lastCommand.valid<<','<<hexText(state.lastCommand.verb)<<','<<state.lastCommand.tick
            <<','<<state.lastCommand.selectedCount<<','<<state.lastCommand.acceptedCount<<','<<state.lastCommand.rejectedCount
            <<','<<state.lastCommand.targetX<<','<<state.lastCommand.targetY<<','<<state.lastCommand.targetObjectId
            <<','<<hexText(state.lastCommand.targetObjectType)<<','<<state.lastCommand.inputAckImmediate<<','<<state.lastCommand.feedbackLatencyTicks;
        if(state.multiplayerBridge)s<<";bridge="<<state.multiplayerBridge->pendingGameplayCommandCount()
            <<','<<state.multiplayerBridge->orderedTransportPumpSessionView().authoritativeTick()
            <<','<<state.multiplayerBridge->lockstep().currentBarrierTick()<<','<<state.multiplayerBridge->lockstep().config().inputDelayTicks;
        else s<<";bridge=none-offline";
        return s.str();
    };
    const auto fullState=[&](const RuntimeWindowState& state){
        std::ostringstream s;s<<cameraState(state)<<';'<<authorityState(state);const auto& local=state.playerLocalCommands;
        s<<";local="<<local.planning<<','<<local.selectedWaypoint<<','<<local.followTargeting<<','<<local.rallyTargeting
            <<','<<local.cameraFollow<<','<<local.showCoordinates<<','<<local.lastSelectionKey<<','<<local.lastSelectionAtMs
            <<','<<local.veterancyRank<<','<<local.healthRank<<";selection=";
        for(int id:state.selectedObjectIds)s<<id<<',';
        for(std::size_t i=0;i<local.teams.size();++i){s<<";team="<<i<<':';for(int id:local.teams[i])s<<id<<',';}
        for(std::size_t i=0;i<local.views.size();++i){s<<";view="<<i<<':';if(local.views[i])s<<local.views[i]->x<<','<<local.views[i]->y;else s<<"none";}
        s<<";start="<<state.visionEnabled<<','<<state.economy.skirmishStartApplied<<','<<state.economy.skirmishStartCell.x<<','<<state.economy.skirmishStartCell.y
            <<";rate="<<state.runtimeScrollRate<<";tool="<<static_cast<int>(state.activeSidebarTool)
            <<";beaconTarget="<<state.playerCommunication.beaconTargeting<<";selectedBeacon=";
        if(state.playerCommunication.selectedBeacon)s<<state.playerCommunication.selectedBeacon->x<<','<<state.playerCommunication.selectedBeacon->y;else s<<"none";
        s<<";drag="<<state.leftPointer.active<<','<<state.leftPointer.moved<<','<<state.rightPointer.active<<','<<state.rightPointer.moved
            <<";status="<<hexText(state.interactionStatus)<<";repaint="<<state.repaintRequested<<";keys="<<state.runtimeMenuKeyboardLoadAttempted;
        for(const auto& [id,code]:state.runtimeMenuKeyboardBindings)s<<','<<hexText(id)<<':'<<code;
        return s.str();
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
    // One real three-house world provides all matrix objects. Copies keep their legal occupancy;
    // no manually pushed unit is ever serialized or used to claim ordinary load acceptance.
    auto prototype=makeBase(0,true);if(!expect(static_cast<bool>(prototype),"legal skirmish camera prototype"))return false;
    const auto make=[&](){
        auto storage=std::make_unique<RuntimeWindowState>();auto& state=*storage;
        RuntimeSnapshotActiveStateScope active(state);
        state.config=prototype->config;state.report=prototype->report;state.world=prototype->world;
        state.rules=prototype->rules;state.art=prototype->art;state.theater=prototype->theater;state.mapFacts=prototype->mapFacts;
        state.rulesLoaded=state.artLoaded=state.theaterLoaded=true;state.mapFactsLoaded=prototype->mapFactsLoaded;
        state.terrainInfoByCell=prototype->terrainInfoByCell;state.terrainByCell=buildRuntimeTerrainLookup(state.world);
        state.skirmishCountryByOwner=prototype->skirmishCountryByOwner;state.skirmishTeamByOwner=prototype->skirmishTeamByOwner;
        state.economy=prototype->economy;state.selectedObjectIds=prototype->selectedObjectIds;state.triggers.bootstrapComplete=true;
        state.runtimeSoundsEnabled=state.runtimeMusicEnabled=false;state.runtimeSuppressPlayerAudioFeedback=true;
        state.runtimePreferencePersistenceEnabled=false;state.runtimeVoiceVolume=state.runtimeSoundVolume=0;
        state.runtimeMenuOpen=false;state.skirmishMatchFinished=false;state.tick=7;state.visionEnabled=false;
        initializeRuntimeNavigation(state);initializeRuntimeOfflineGameplayRouting(state);
        centerRuntimeCameraOnCell(state,client,state.economy.skirmishStartCell);
        state.playerLocalCommands.cameraFollow=true;state.playerLocalCommands.followTargeting=state.playerLocalCommands.rallyTargeting=false;
        state.playerLocalCommands.lastSelectionKey='?';state.playerLocalCommands.lastSelectionAtMs=777;
        state.playerCommunication.beaconTargeting=false;state.activeSidebarTool=RuntimeSidebarTool::None;
        state.runtimeMenuKeyboardLoadAttempted=true;state.runtimeMenuKeyboardBindings={{"Options",VK_ESCAPE}};
        state.interactionStatus="camera initial";state.repaintRequested=false;state.runtimeScrollRate=3;
        return storage;
    };
    const auto setBinding=[](RuntimeWindowState& state,const std::string& action,int code){
        for(auto& entry:state.runtimeMenuKeyboardBindings)if(entry.first==action){entry.second=code;return;}
        state.runtimeMenuKeyboardBindings.emplace_back(action,code);
    };
    // Exact old complete camera block, including its original redraw/default epilogue.
    const auto oldCamera=[](RuntimeWindowState* state,HWND window,UINT message,WPARAM wParam,LPARAM lParam)->LRESULT {
        const int scrollStep = runtimeCameraScrollStep(*state);
        const int centerViewKey = runtimeMenuKeyboardBindingCode(
            *state,
            "CenterView",
            VK_CLEAR
        );
        bool handled = true;
        if (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP || wParam == VK_DOWN)
            state->playerLocalCommands.cameraFollow = false;
        if (runtimePlayerCurrentKeyCode(wParam) == centerViewKey) {
            if (state->visionEnabled
                && state->economy.skirmishStartApplied
                && state->economy.skirmishStartCell.x >= 0
                && state->economy.skirmishStartCell.y >= 0) {
                RECT homeClientRect{};
                GetClientRect(window, &homeClientRect);
                centerRuntimeCameraOnCell(
                    *state,
                    homeClientRect,
                    state->economy.skirmishStartCell
                );
            } else {
                state->camera.initialized = false;
            }
            invalidateRuntimeObjectProjectionCache(*state);
        } else {
            switch (wParam) {
            case VK_LEFT:
                scrollRuntimeMapCameraClamped(*state, -scrollStep, 0);
                invalidateRuntimeObjectProjectionCache(*state);
                break;
            case VK_RIGHT:
                scrollRuntimeMapCameraClamped(*state, scrollStep, 0);
                invalidateRuntimeObjectProjectionCache(*state);
                break;
            case VK_UP:
                scrollRuntimeMapCameraClamped(*state, 0, -scrollStep);
                invalidateRuntimeObjectProjectionCache(*state);
                break;
            case VK_DOWN:
                scrollRuntimeMapCameraClamped(*state, 0, scrollStep);
                invalidateRuntimeObjectProjectionCache(*state);
                break;
            default:
                handled = false;
                break;
            }
        }
        if (handled) {
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    };
    // This unmodified upstream consumer is deliberately shared by old/direct paths. It is
    // not the old camera oracle: it establishes the same existing lazy bindings, clock sample,
    // observer selection gate and earlier-command priority reached by the actual WindowProc.
    const auto prefix=[](RuntimeWindowState& state,HWND window,UINT message,WPARAM key,LPARAM flags){
        (void)runtimeMenuKeyboardBindingCode(state,"Options",VK_ESCAPE);
        if(handleRuntimePlayerCommandKey(state,window,key,flags))return true;
        RuntimeSelectionKeyWin32Host host(state,key);
        const RuntimeSelectionKeyInput input{static_cast<std::int32_t>(key),(flags&(static_cast<LPARAM>(1)<<30))!=0,
            message==WM_SYSKEYDOWN?RuntimeKeyDownSource::SystemKeyDown:RuntimeKeyDownSource::KeyDown};
        return handleRuntimeSelectionKey(input,host)!=RuntimeSelectionKeyResult::Unhandled;
    };
    const auto directCamera=[](RuntimeWindowState& state,HWND window,UINT message,WPARAM key,LPARAM flags)->LRESULT {
        RuntimeCameraKeyWin32Host host(state,window);
        if(handleRuntimeCameraKey({static_cast<std::uint64_t>(key),(flags&(static_cast<LPARAM>(1)<<30))!=0},host)){
            state.repaintRequested=true;RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_NOERASE);return 0;
        }
        return DefWindowProcW(window,message,key,flags);
    };
    const auto triple=[&](const std::string& label,WPARAM raw,int rate,bool repeated,int mode=0,bool warm=false,bool resize=false){
        auto os=make(),ds=make(),ns=make();if(!expect(os&&ds&&ns,label+" independent real states"))return false;
        auto& old=*os;auto& direct=*ds;auto& native=*ns;
        RuntimePlayerDefeatNoticeSmokeWindow ow(old,1024,768),dw(direct,1024,768),nw(native,1024,768);
        if(!expect(ow.window&&dw.window&&nw.window,label+" three real hidden windows"))return false;
        const auto initialViewport=old.cameraClampViewport;
        for(auto* state:{&old,&direct,&native}){
            state->runtimeScrollRate=rate;state->visionEnabled=true;
            setBinding(*state,"CenterView",runtimePlayerCurrentKeyCode(mode==6?VK_LEFT:VK_CLEAR));
            if(mode==1)state->visionEnabled=false;
            if(mode==2)state->economy.skirmishStartApplied=false;
            if(mode==3)state->economy.skirmishStartCell.x=-1;
            if(mode==4)state->economy.skirmishStartCell.y=-1;
            if(mode==5)state->cameraClampViewport={0,0,0,0};
            if(mode==7){state->camera.worldOffsetX=100000;state->camera.worldOffsetY=-100000;}
            if(mode==8){state->camera.tileWidth=90;state->camera.tileHeight=45;}
            if(mode==9){state->campaignMode=true;state->triggers.campaignInputLocked=true;}
            if(mode==10){state->runtimeMenuKeyboardLoadAttempted=false;state->runtimeMenuKeyboardBindings.clear();}
            if(mode==11){
                auto& cells=state->world.accessTerrainCells();
                const auto start=state->economy.skirmishStartCell;
                auto found=std::find_if(cells.begin(),cells.end(),[&](const TerrainCell& c){return c.x==start.x&&c.y==start.y;});
                if(found==cells.end())return false;
                found->level=4;state->terrainByCell=buildRuntimeTerrainLookup(state->world);
            }
            if(warm){RuntimeSnapshotActiveStateScope active(*state);(void)runtimeProjectedObjectsForViewport(*state,viewport);}
        }
        if(resize){
            for(HWND window:{ow.window,dw.window,nw.window})if(!SetWindowPos(window,nullptr,0,0,800,600,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE))return false;
            if(!expect(EqualRect(&old.cameraClampViewport,&initialViewport)&&EqualRect(&direct.cameraClampViewport,&initialViewport)
                &&EqualRect(&native.cameraClampViewport,&initialViewport),label+" resize alone does not establish new clamp viewport"))return false;
            ++resizeCases;
        }
        if(!expect(fullState(old)==fullState(direct)&&fullState(old)==fullState(native),label+" full entry state"))return false;
        Clock oc,dc,nc;ClockBinding ob(old,oc),db(direct,dc),nb(native,nc);Witness a(old),b(direct),c(native);
        const auto authority=authorityState(old);const auto beforeProjection=projectionState(old);
        const int beforeX=old.camera.worldOffsetX,beforeY=old.camera.worldOffsetY;
        const auto signature=old.projectedObjectsCacheSignature;const auto storedCount=old.projectedObjectsCache.size();
        const UINT message=mode==9||raw==VK_F14||triples%2?WM_SYSKEYDOWN:WM_KEYDOWN;
        const LPARAM flags=repeated?(static_cast<LPARAM>(1)<<30):0;
        LRESULT oldResult=0,directResult=0,nativeResult=0;
        {RuntimeSnapshotActiveStateScope active(old);if(!expect(!prefix(old,ow.window,message,raw,flags),label+" old reaches final camera consumer"))return false;
            oldResult=oldCamera(&old,ow.window,message,raw,flags);}
        {RuntimeSnapshotActiveStateScope active(direct);if(!expect(!prefix(direct,dw.window,message,raw,flags),label+" direct reaches final camera consumer"))return false;
            directResult=directCamera(direct,dw.window,message,raw,flags);}
        {RuntimeSnapshotActiveStateScope active(native);nativeResult=SendMessageW(nw.window,message,raw,flags);}
        if(fullState(old)!=fullState(native))output<<"Camera mismatch label="<<label<<" old="<<hexText(fullState(old))<<" native="<<hexText(fullState(native))<<'\n';
        if(!expect(oldResult==directResult&&oldResult==nativeResult&&fullState(old)==fullState(direct)&&fullState(old)==fullState(native),
                label+" frozen old/same cpp/actual WindowProc full state and return"))return false;
        if(!expect(authorityState(old)==authority&&authorityState(direct)==authority&&authorityState(native)==authority
            &&a.captures==0&&b.captures==0&&c.captures==0,label+" no world/tick/hash/command/capture authority changes"))return false;
        if(!expect(oc.reads==1&&dc.reads==1&&nc.reads==1,label+" unchanged upstream clock sample once"))return false;
        const bool center=runtimePlayerCurrentKeyCode(raw)==runtimeMenuKeyboardBindingCode(old,"CenterView",VK_CLEAR);
        const bool arrow=raw==VK_LEFT||raw==VK_RIGHT||raw==VK_UP||raw==VK_DOWN;
        const bool handled=center||arrow;
        if(handled){
            if(!expect(!old.projectedObjectsCacheValid&&old.projectedObjectsCacheSignature==signature
                &&old.projectedObjectsCache.size()==storedCount&&old.repaintRequested,
                label+" invalidation retains stored projection/signature and requests repaint even if no movement"))return false;
        }else if(!expect(projectionState(old)==beforeProjection&&!old.repaintRequested,label+" unhandled preserves cache and caller default"))return false;
        if(arrow&&!expect(!old.playerLocalCommands.cameraFollow,label+" raw arrow clears follow independently of modified match"))return false;
        if(mode==5&&!expect(old.camera.worldOffsetX==beforeX&&old.camera.worldOffsetY==beforeY&&!old.projectedObjectsCacheValid,
            label+" pre-paint invalid viewport defers scroll but invalidates"))return false;
        if(center&&mode>=1&&mode<=4&&!expect(!old.camera.initialized&&old.playerLocalCommands.cameraFollow,
            label+" failed center guard resets initialized only; non-arrow retains follow"))return false;
        if(resize){
            RECT actual{};GetClientRect(nw.window,&actual);const auto expected=runtimeMapViewport(actual);
            if(!expect(EqualRect(&native.cameraClampViewport,&expected),label+" CenterView uses late resized client"))return false;
        }
        if(mode==11){if(!expect(terrainLevelAtRuntimeCell(native,native.economy.skirmishStartCell.x,native.economy.skirmishStartCell.y)==4,
                label+" real native terrain-height query sees authored level4"))return false;++heightCases;}
        if(!expect(old.backBuffer.pixels==nullptr&&direct.backBuffer.pixels==nullptr&&native.backBuffer.pixels==nullptr,
            label+" no WM_PAINT required"))return false;
        const auto print=[&](const char* path,const RuntimeWindowState& state,const Witness& witness){
            trace<<"camera path="<<path<<" label="<<label<<" raw="<<static_cast<std::uint64_t>(raw)<<" rate="<<rate
                <<" repeat="<<repeated<<" mode="<<mode<<" warm="<<warm<<" message="<<message<<" captures="<<witness.captures
                <<" state="<<hexText(fullState(state))<<'\n';
        };
        print("old",old,a);print("direct",direct,b);print("native",native,c);
        if(mode==0&&handled&&!resize){
            for(auto* state:{&old,&direct,&native}){RuntimeSnapshotActiveStateScope active(*state);(void)runtimeProjectedObjectsForViewport(*state,viewport);++projectionQueries;}
            if(!expect(!old.projectedObjectsCache.empty()&&old.projectedObjectsCacheValid&&fullState(old)==fullState(direct)&&fullState(old)==fullState(native),
                label+" real sprite/voxel projection rebuild without paint and complete owned geometry equality"))return false;
            trace<<"projection label="<<label<<" state="<<hexText(fullState(native))<<" captures=0\n";
        }
        sourceReads+=oc.reads+dc.reads+nc.reads;++triples;return true;
    };
    for(int rate:{0,3,6})for(WPARAM raw:{static_cast<WPARAM>(VK_LEFT),static_cast<WPARAM>(VK_RIGHT),static_cast<WPARAM>(VK_UP),static_cast<WPARAM>(VK_DOWN)})
        for(bool repeated:{false,true})if(!triple("arrows",raw,rate,repeated,0,repeated))return false;
    for(int mode=0;mode<=4;++mode)for(bool repeated:{false,true})
        if(!triple("center-guards",VK_CLEAR,3,repeated,mode,repeated))return false;
    for(int rate:{std::numeric_limits<int>::min(),std::numeric_limits<int>::max()})
        if(!triple("rate-extremes",VK_RIGHT,rate,false))return false;
    for(bool repeated:{false,true})if(!triple("raw-arrow-center-collision",VK_LEFT,3,repeated,6,true))return false;
    for(bool repeated:{false,true})if(!triple("invalid-prepaint-viewport",VK_DOWN,3,repeated,5))return false;
    if(!triple("clamp-overshoot",VK_LEFT,6,false,7))return false;
    if(!triple("resized-center",VK_CLEAR,3,false,0,false,true))return false;
    if(!triple("camera-scale",VK_CLEAR,3,false,8))return false;
    if(!triple("campaign-system-key",VK_RIGHT,3,true,9))return false;
    if(!triple("lazy-default-bindings",VK_RIGHT,3,false,10))return false;
    if(!triple("real-terrain-height",VK_CLEAR,3,false,11))return false;
    if(!triple("unknown-system-key",VK_F14,3,false))return false;
    if(sizeof(WPARAM)>=8&&!triple("raw-high-word-is-not-arrow",static_cast<WPARAM>((std::uint64_t{1}<<32)|VK_LEFT),3,true))return false;
    for(int mode=0;mode<6;++mode){
        auto storage=make();auto& state=*storage;setBinding(state,"CenterView",runtimePlayerCurrentKeyCode(VK_LEFT));
        if(mode==0)state.runtimeMenuOpen=true;
        if(mode==1)state.playerCommunication.chatOpen=true;
        if(mode==2)state.playerCommunication.diplomacyOpen=true;
        if(mode==3)state.skirmishMatchFinished=true;
        if(mode==4){state.campaignMode=true;state.triggers.campaignInputLocked=true;}
        if(mode==5){state.runtimeMenuOpen=true;state.runtimeMenuPage=RuntimeMenuPage::Keyboard;state.runtimeMenuKeyCaptureIndex=0;}
        Clock clock;ClockBinding binding(state,clock);Witness witness(state);RuntimePlayerDefeatNoticeSmokeWindow window(state,1024,768);
        if(!expect(window.window!=nullptr,"outer guard actual HWND"))return false;
        const auto camera=cameraState(state),authority=authorityState(state);
        {RuntimeSnapshotActiveStateScope active(state);SendMessageW(window.window,WM_KEYDOWN,VK_LEFT,0);}
        if(!expect(cameraState(state)==camera&&authorityState(state)==authority&&clock.reads==0&&witness.captures==0,
            "earlier modal/menu/terminal/campaign/capture gate prevents final camera"))return false;
        trace<<"guard mode="<<mode<<" sourceReads="<<clock.reads<<" captures="<<witness.captures<<" state="<<hexText(fullState(state))<<'\n';++guards;
    }
    for(int mode=0;mode<4;++mode){
        auto storage=make();auto& state=*storage;const auto code=runtimePlayerCurrentKeyCode(VK_LEFT);
        setBinding(state,"CenterView",code);bool repeated=false;
        if(mode==0)setBinding(state,"StructureTab",code);
        if(mode==1){setBinding(state,"StopObject",code);repeated=true; if(!expect(runtimeMenuKeyboardBindingCode(state,"StopObject",'S')==code,"priority StopObject real binding precondition"))return false;}
        if(mode==2){setBinding(state,"TeamCreate_1",code);repeated=true;}
        if(mode==3){state.config.playerSlots.clear();state.config.playerCountry="Observer";state.visionEnabled=false;}
        Clock clock;ClockBinding binding(state,clock);Witness witness(state);RuntimePlayerDefeatNoticeSmokeWindow window(state,1024,768);
        if(!expect(window.window!=nullptr,"priority actual HWND"))return false;
        const auto camera=cameraState(state),authority=authorityState(state);
        {RuntimeSnapshotActiveStateScope active(state);SendMessageW(window.window,WM_KEYDOWN,VK_LEFT,repeated?(static_cast<LPARAM>(1)<<30):0);}
        if(!expect(authorityState(state)==authority&&clock.reads==1&&witness.captures==0,"upstream priority never creates authority"))return false;
        if(mode<3&&!expect(cameraState(state)==camera&&state.playerLocalCommands.cameraFollow,"upstream matching consumer wins CenterView collision"))return false;
        if(mode==0&&!expect(state.activeBuildTab==RuntimeBuildKind::Building,"Reference tab wins camera"))return false;
        if(mode==3&&!expect(state.selectedObjectIds.empty()&&!state.playerLocalCommands.cameraFollow&&!state.camera.initialized,
            "observer clears selection then camera raw-arrow/invalid-center still executes"))return false;
        trace<<"priority mode="<<mode<<" sourceReads="<<clock.reads<<" captures="<<witness.captures<<" state="<<hexText(fullState(state))<<'\n';
        sourceReads+=clock.reads;++priorities;
    }
    {
        auto storage=makeBase(1,true);if(!expect(static_cast<bool>(storage),"proven legal active three-house save fixture"))return false;auto& state=*storage;
        RuntimeSnapshotActiveStateScope active(state);state.config.shortGame=true;state.tick=21;(void)advanceRuntimeParticles(state);
        if(!expect(state.particlesInitialized&&state.particles.currentTick()==21,"real particles at active tick21"))return false;
        Clock clock;ClockBinding binding(state,clock);const auto savePath=pathString(directory/"camera-key-load.ini");std::string error;
        const bool saved=saveRuntimeState(state,savePath,error);const bool loaded=saved&&loadRuntimeState(state,savePath,error);
        if(!expect(saved&&loaded&&state.tick==21&&state.uiClock==&clock,"ordinary load preserves clock borrow/tick: "+error))return false;
        // Every child reference is acquired after scratch commit. No fixture object is injected into this save.
        state.runtimeMenuKeyboardLoadAttempted=true;state.runtimeMenuKeyboardBindings={{"Options",VK_ESCAPE}};
        state.playerCommunication.beaconTargeting=false;state.playerLocalCommands.followTargeting=state.playerLocalCommands.rallyTargeting=false;
        state.visionEnabled=true;state.playerLocalCommands.cameraFollow=true;setBinding(state,"CenterView",runtimePlayerCurrentKeyCode(VK_F13));
        Witness witness(state);RuntimePlayerDefeatNoticeSmokeWindow window(state,800,600);
        if(!expect(window.window!=nullptr,"post-load real HWND"))return false;
        const auto before=authorityState(state);const auto reads=clock.reads;
        {RuntimeSnapshotActiveStateScope fresh(state);SendMessageW(window.window,WM_KEYDOWN,VK_F13,0);}
        RECT actual{};GetClientRect(window.window,&actual);const RECT expected=runtimeMapViewport(actual);
        if(!expect(state.uiClock==&clock&&state.tick==21&&clock.reads==reads+1&&authorityState(state)==before&&witness.captures==0
            &&!state.playerLocalCommands.cameraFollow&&state.camera.initialized&&!state.projectedObjectsCacheValid
            &&EqualRect(&state.cameraClampViewport,&expected),"first camera key after load uses fresh root and late current client with zero authority"))return false;
        trace<<"load tick=21 particleTick="<<state.particles.currentTick()<<" sourceSame=1 freshRoot=1 captures=0 state="<<hexText(fullState(state))<<'\n';
        sourceReads+=clock.reads;
    }
    trace<<"checks="<<checks<<" triples="<<triples<<" guards="<<guards<<" priorities="<<priorities<<" projectionQueries="<<projectionQueries
        <<" resizeCases="<<resizeCases<<" heightCases="<<heightCases<<'\n';
    trace.flush();if(!expect(static_cast<bool>(trace),"trace flush"))return false;
    output<<"Camera-key integration passed: checks="<<checks<<" oldCases="<<triples<<" directCases="<<triples<<" nativeCases="<<triples
        <<" sourceReads="<<sourceReads<<" guards="<<guards<<" precedenceCases="<<priorities<<" projectionQueries="<<projectionQueries
        <<" resizeCases="<<resizeCases<<" heightCases="<<heightCases<<" actualLoad=1 sameProductionCpp=1 frozenOldCompleteCameraBlock=1"
        <<" fullCameraLocalProjectionCacheState=1 worldTickHashCommandsCaptureUnchanged=1 coldNoPaint=1 realGeometryOwner=1"
        <<" nativeOrderedTrace=observable-complete-state standardOrderedQueries=separate-contract nativeModifierFullMatrix=not-run"
        <<" runningMPsession=not-run fullMpSnapshot=not-run externalNetwork=not-run geometryAlgorithmMigration=not-covered"
        <<" nativeCaptureReentryTargetMutation=not-forced originalOverflowDomain=not-covered originalObserverEndSaveFailure=preserved\n";return true;
}