// Included in the private runtime owner after the real navigation adapter and loader.
// No UI message, input injection or private desktop is needed for this read-only seam.
namespace RuntimeNavigationTerrainSmokeDetail {
inline thread_local std::vector<std::string>* observations=nullptr;
inline std::string info(const RuntimeNavigationTerrainInfo& v){std::ostringstream s;s<<static_cast<int>(v.movementLandType)<<','<<static_cast<int>(v.movementRampType)<<','<<static_cast<int>(v.kind)<<','<<static_cast<int>(v.authoredLandType)<<','<<v.passable<<','<<v.water<<','<<v.shore<<','<<v.cliff<<','<<v.ramp<<','<<v.bridge<<','<<v.cost;return s.str();}
inline std::string cell(const TerrainCell& c){std::ostringstream s;s<<c.x<<','<<c.y<<','<<c.cellIndex512<<','<<c.tileIndex<<','<<c.subTileIndex<<','<<c.level<<','<<c.iceGrowth<<','<<c.hasOverlay<<','<<c.overlayIndex<<','<<c.overlayFrame<<','<<c.overlayType<<','<<c.overlayImage<<','<<c.overlayIsOre<<','<<c.overlayIsGem<<','<<c.overlayWall.originalIndex<<','<<c.overlayWall.originalFrame<<','<<c.overlayWall.originalType<<','<<c.overlayWall.originalImage<<','<<c.overlayWall.health;return s.str();}
inline void note(const char* name,const TerrainCell& c,const std::string& value){if(observations)observations->push_back(std::string(name)+':'+cell(c)+"=>"+value);}
struct ObservationBinding {std::vector<std::string>* previous=observations;explicit ObservationBinding(std::vector<std::string>& v){observations=&v;}~ObservationBinding(){observations=previous;}};
struct Old {
 static bool runtimeOverlayWallRuleForCell(RuntimeWindowState& s,const TerrainCell& c){const bool v=static_cast<bool>(::runtimeOverlayWallRuleForCell(s,c));note("wall-rule",c,v?"1":"0");return v;}
 static bool runtimeOverlayWallUsesCurrentOverlay(const TerrainCell& c){const bool v=::runtimeOverlayWallUsesCurrentOverlay(c);note("wall-current",c,v?"1":"0");return v;}
 static bool runtimeOverlayProvidesBridgeDeck(const TerrainCell& c){const bool v=::runtimeOverlayProvidesBridgeDeck(c);note("deck",c,v?"1":"0");return v;}
 static bool runtimeHighBridgeFrameIsDisconnected(const TerrainCell& c){const bool v=::runtimeHighBridgeFrameIsDisconnected(c);note("broken",c,v?"1":"0");return v;}
 static bool runtimeOverlayIsBrokenLowBridgePlaceholder(const TerrainCell& c){const bool v=::runtimeOverlayIsBrokenLowBridgePlaceholder(c);note("placeholder",c,v?"1":"0");return v;}
 static RuntimeTerrainInfo runtimeTerrainInfoForCell(RuntimeWindowState& s,const TerrainCell& c){const auto v=::runtimeTerrainInfoForCell(s,c);note("base",c,info(runtimeOwnedNavigationTerrainInfo(v)));return v;}
 static RuntimeTerrainLandType resolveRuntimeTerrainSubTileLandType(const TerrainCell& c,const RuntimeMapRenderContext& context,RuntimeMapRenderCache& cache){const auto v=::resolveRuntimeTerrainSubTileLandType(c,context,cache);note("land",c,std::to_string(static_cast<int>(v)));return v;}
 static unsigned char resolveRuntimeTerrainSubTileRampType(const TerrainCell& c,const RuntimeMapRenderContext& context,RuntimeMapRenderCache& cache){const auto v=::resolveRuntimeTerrainSubTileRampType(c,context,cache);note("ramp",c,std::to_string(static_cast<int>(v)));return v;}
 static bool runtimeCellHasDisconnectedHighBridgeDeck(RuntimeWindowState& s,const TerrainCell& c){const bool v=::runtimeCellHasDisconnectedHighBridgeDeck(s,c);note("disconnected",c,v?"1":"0");return v;}
 static
RuntimeTerrainInfo runtimeNavigationTerrainInfoForCell(
    RuntimeWindowState& state,
    const TerrainCell& terrain,
    const RuntimeMapRenderContext& metadataContext
) {
    if (terrain.overlayWall.health > 0 && runtimeOverlayWallRuleForCell(state, terrain)
        && runtimeOverlayWallUsesCurrentOverlay(terrain)) {
        RuntimeTerrainInfo blocked;
        blocked.kind = RuntimeTerrainKind::Blocked;
        blocked.passable = false;
        blocked.cost = std::numeric_limits<int>::max() / 4;
        blocked.movementLandType = blocked.authoredLandType = RuntimeTerrainLandType::Wall;
        return blocked;
    }
    // Resolve only ordinary sentinel ground through canonical metadata. Intact
    // bridge overlays retain their previous underlying movement-surface policy.
    if (terrain.tileIndex == 65535 && state.theaterLoaded
        && !runtimeOverlayProvidesBridgeDeck(terrain)
        && !runtimeHighBridgeFrameIsDisconnected(terrain)
        && !runtimeOverlayIsBrokenLowBridgePlaceholder(terrain)) {
        TerrainCell clear = terrain;
        clear.tileIndex = 0;
        clear.subTileIndex = 0;
        return runtimeNavigationTerrainInfoForCell(state, clear, metadataContext);
    }
    RuntimeTerrainInfo info = runtimeTerrainInfoForCell(state, terrain);
    info.movementLandType = resolveRuntimeTerrainSubTileLandType(terrain, metadataContext, state.renderCache);
    info.movementRampType = resolveRuntimeTerrainSubTileRampType(terrain, metadataContext, state.renderCache);
    if (info.bridge && !runtimeOverlayProvidesBridgeDeck(terrain)
        && runtimeCellHasDisconnectedHighBridgeDeck(state, terrain)) {
        info.bridge = false;
        info.authoredLandType = info.movementLandType;
        info.water = info.movementLandType == RuntimeTerrainLandType::Water;
        info.kind = info.water ? RuntimeTerrainKind::Water : RuntimeTerrainKind::Land;
        info.cost = info.water ? 18
            : (info.movementLandType == RuntimeTerrainLandType::Road ? 8 : 10);
        if (info.movementLandType == RuntimeTerrainLandType::Rock
            || info.movementLandType == RuntimeTerrainLandType::Wall) {
            info.kind = RuntimeTerrainKind::Blocked;
            info.passable = false;
            info.cost = std::numeric_limits<int>::max() / 4;
        }
    }
    if (info.kind != RuntimeTerrainKind::Shore) {
        return info;
    }

    info.authoredLandType = resolveRuntimeTerrainSubTileLandType(
        terrain,
        metadataContext,
        state.renderCache
    );
    if (info.authoredLandType == RuntimeTerrainLandType::Water) {
        info.kind = RuntimeTerrainKind::Water;
        info.water = true;
        info.shore = false;
        info.cost = 18;
    } else if (info.authoredLandType == RuntimeTerrainLandType::Rough
               || info.authoredLandType == RuntimeTerrainLandType::Clear) {
        // Landward shoreline cells remain ordinary traversable land.
        info.kind = RuntimeTerrainKind::Land;
        info.water = false;
        info.shore = false;
        info.cost = 10;
    }
    return info;
}

};
struct Port final : RuntimeNavigationTerrainQueryPort {
 RuntimeNavigationTerrainRendererAdapter source;
 Port(RuntimeWindowState& s,const RuntimeMapRenderContext& c):source(s,c){}
 bool theaterLoaded()const override{return source.theaterLoaded();} // Direct old state-field read is observed by the standard contract.
 bool overlayWallRuleForCell(const TerrainCell& c)const override{const bool v=source.overlayWallRuleForCell(c);note("wall-rule",c,v?"1":"0");return v;}
 bool overlayWallUsesCurrentOverlay(const TerrainCell& c)const override{const bool v=source.overlayWallUsesCurrentOverlay(c);note("wall-current",c,v?"1":"0");return v;}
 bool overlayProvidesBridgeDeck(const TerrainCell& c)const override{const bool v=source.overlayProvidesBridgeDeck(c);note("deck",c,v?"1":"0");return v;}
 bool highBridgeFrameIsDisconnected(const TerrainCell& c)const override{const bool v=source.highBridgeFrameIsDisconnected(c);note("broken",c,v?"1":"0");return v;}
 bool overlayIsBrokenLowBridgePlaceholder(const TerrainCell& c)const override{const bool v=source.overlayIsBrokenLowBridgePlaceholder(c);note("placeholder",c,v?"1":"0");return v;}
 RuntimeNavigationTerrainInfo baseTerrainInfoForCell(const TerrainCell& c)const override{const auto v=source.baseTerrainInfoForCell(c);note("base",c,info(v));return v;}
 RuntimeTerrainLandType subTileLandType(const TerrainCell& c)override{const auto v=source.subTileLandType(c);note("land",c,std::to_string(static_cast<int>(v)));return v;}
 unsigned char subTileRampType(const TerrainCell& c)override{const auto v=source.subTileRampType(c);note("ramp",c,std::to_string(static_cast<int>(v)));return v;}
 bool cellHasDisconnectedHighBridgeDeck(const TerrainCell& c)const override{const bool v=source.cellHasDisconnectedHighBridgeDeck(c);note("disconnected",c,v?"1":"0");return v;}
};
inline std::string cacheValue(const RuntimeMapRenderCache& cache){std::vector<std::string> rows;
 for(const auto& [key,tile]:cache.tmpTilesByPath){std::ostringstream s;s<<key<<':'<<tile.loadAttempted<<','<<tile.loaded<<','<<tile.errorMessage;rows.push_back(s.str());}
 for(const auto& [index,path]:cache.terrainTilePathByIndex)rows.push_back("path:"+std::to_string(index)+':'+path);
 for(const auto index:cache.missingTerrainTilePathByIndex)rows.push_back("missing:"+std::to_string(index));
 std::sort(rows.begin(),rows.end());std::ostringstream out;for(const auto& row:rows)out<<row<<'\n';out<<"error:"<<cache.lastTmpTileLoadError;return out.str();}
}

bool runRuntimeNavigationTerrainQuerySmokeProbe(RuntimeWindowState& stock,std::ostream& output){
 using namespace RuntimeNavigationTerrainSmokeDetail;
 std::size_t checks=0,cases=0,queryRows=0,rebuildCells=0,loadCases=0,nestedCases=0;
 const auto expect=[&](bool value,const std::string& why){++checks;if(!value)output<<"Navigation terrain integration failed: "<<why<<'\n';return value;};
 if(!expect(stock.report.success&&stock.world.getSummary().loaded&&stock.rulesLoaded&&stock.artLoaded&&stock.theaterLoaded,"real startup resources"))return false;
 char* requested=nullptr;std::size_t length=0;const auto envResult=_dupenv_s(&requested,&length,"CONCORD_NAVIGATION_TERRAIN_QUERY_OUTPUT");
 const std::filesystem::path directory=requested&&*requested?std::filesystem::path(requested):std::filesystem::path("navigation-terrain-query-output");std::free(requested);
 if(!expect(envResult==0,"owned output environment"))return false;
 std::error_code ec;std::filesystem::create_directories(directory,ec);if(!expect(!ec,"private output directory"))return false;
 std::ofstream trace(directory/"navigation-terrain-query-trace.txt",std::ios::binary|std::ios::trunc);if(!expect(static_cast<bool>(trace),"trace open"))return false;
 const auto make=[&](){auto ptr=std::make_unique<RuntimeWindowState>();auto& s=*ptr;
  s.config=stock.config;s.report=stock.report;s.world=stock.world;s.rules=stock.rules;s.art=stock.art;s.theater=stock.theater;s.mapFacts=stock.mapFacts;
  s.rulesLoaded=stock.rulesLoaded;s.artLoaded=stock.artLoaded;s.theaterLoaded=stock.theaterLoaded;s.mapFactsLoaded=stock.mapFactsLoaded;
  s.terrainByCell=buildRuntimeTerrainLookup(s.world);s.terrainInfoByCell=stock.terrainInfoByCell;
  s.runtimePreferencePersistenceEnabled=false;s.runtimeSoundsEnabled=s.runtimeMusicEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;return ptr;};
 const auto authority=[](const RuntimeWindowState& s){return std::to_string(s.tick)+':'+std::to_string(s.world.deterministicSimulationStateHash())+':'+std::to_string(s.playerCommands.stateHash())+':'+std::to_string(s.superWeapons.stateHash());};
 auto oldStorage=make(),directStorage=make(),consumerStorage=make();auto& old=*oldStorage;auto& direct=*directStorage;auto& consumer=*consumerStorage;
 auto oldContext=runtimeTerrainMetadataContext(old),directContext=runtimeTerrainMetadataContext(direct);
 const auto oldAuthority=authority(old),directAuthority=authority(direct),consumerAuthority=authority(consumer);
 const auto pair=[&](const TerrainCell& terrain,const char* phase,const RuntimeMapRenderContext& a,const RuntimeMapRenderContext& b){
  std::vector<std::string> oq,dq;RuntimeNavigationTerrainInfo expected,actual;
  {RuntimeSnapshotActiveStateScope active(old);ObservationBinding observing(oq);expected=runtimeOwnedNavigationTerrainInfo(Old::runtimeNavigationTerrainInfoForCell(old,terrain,a));}
  {RuntimeSnapshotActiveStateScope active(direct);ObservationBinding observing(dq);Port port(direct,b);actual=queryRuntimeNavigationTerrainInfo(port,terrain);}
  if(!expect(info(expected)==info(actual),std::string(phase)+" all eleven fields")||!expect(oq==dq,std::string(phase)+" ordered primitive calls")||!expect(cacheValue(old.renderCache)==cacheValue(direct.renderCache),std::string(phase)+" complete cache/error outcomes"))return false;
  trace<<"query phase="<<phase<<" cell="<<cell(terrain)<<" value="<<info(actual)<<" calls="<<dq.size()<<'\n';for(const auto& row:dq)trace<<"call "<<row<<'\n';
  queryRows+=dq.size();++cases;return true;};
 std::vector<TerrainCell> cells;for(const auto& terrain:stock.world.getTerrainCells())cells.push_back(terrain);
 if(!expect(!cells.empty(),"actual map terrain"))return false;
 for(const auto& terrain:cells)if(!pair(terrain,"cold-outside",oldContext,directContext))return false;
 for(const auto& terrain:cells)if(!pair(terrain,"warm-outside",oldContext,directContext))return false;
 constexpr RECT client{0,0,1024,768};const RECT viewport=runtimeMapViewport(client);
 oldContext.stableRenderConfiguration=directContext.stableRenderConfiguration=true;
 {RuntimeGeometryQueryScope a(old.world,viewport,oldContext,old.camera,old.renderCache);RuntimeGeometryQueryScope b(direct.world,viewport,directContext,direct.camera,direct.renderCache);
  // Each separate world's scope is entered only around its own query below; simultaneous frames cannot be cross-borrowed.
  if(!pair(cells.front(),"foreign-active-scope",oldContext,directContext))return false;
 }
 for(int mode=0;mode!=3;++mode){auto a=oldContext,b=directContext;
  if(mode==1){
   const auto missing=std::filesystem::absolute(directory/"deliberately-absent-terrain-root");
   if(!expect(!std::filesystem::exists(missing),"missing-resource fixture path must not exist"))return false;
   for(auto parent=missing;!parent.empty();parent=parent.parent_path()){
    if(!expect(toLowerAsciiLocal(parent.filename().string())!="mix_exports","missing-resource fixture cannot inherit a MIX export root"))return false;
    if(parent==parent.parent_path())break;
   }
   a.tileRootPath=b.tileRootPath=pathString(missing);a.assetRootPath.clear();b.assetRootPath.clear();a.structureRootPath.clear();b.structureRootPath.clear();
   auto primitive=cells.front();primitive.tileIndex=0;primitive.subTileIndex=0;
   const auto oldLand=resolveRuntimeTerrainSubTileLandType(primitive,a,old.renderCache),newLand=resolveRuntimeTerrainSubTileLandType(primitive,b,direct.renderCache);
   const auto oldRamp=resolveRuntimeTerrainSubTileRampType(primitive,a,old.renderCache),newRamp=resolveRuntimeTerrainSubTileRampType(primitive,b,direct.renderCache);
   if(!expect(oldLand==RuntimeTerrainLandType::Unknown&&newLand==RuntimeTerrainLandType::Unknown&&oldRamp==0&&newRamp==0,"actual missing decoder primitive must yield Unknown land and zero ramp"))return false;
  }
  if(mode==2){a.theater=nullptr;b.theater=nullptr;}
  for(std::size_t index=0;index<std::min<std::size_t>(cells.size(),32);++index){
   std::vector<std::string> oq,dq;RuntimeNavigationTerrainInfo expected,actual;
   {RuntimeSnapshotActiveStateScope active(old);RuntimeGeometryQueryScope outer(old.world,viewport,a,old.camera,old.renderCache);RuntimeGeometryQueryScope nested(old.world,viewport,a,old.camera,old.renderCache);ObservationBinding observing(oq);expected=runtimeOwnedNavigationTerrainInfo(Old::runtimeNavigationTerrainInfoForCell(old,cells[index],a));}
   {RuntimeSnapshotActiveStateScope active(direct);RuntimeGeometryQueryScope outer(direct.world,viewport,b,direct.camera,direct.renderCache);RuntimeGeometryQueryScope nested(direct.world,viewport,b,direct.camera,direct.renderCache);ObservationBinding observing(dq);Port port(direct,b);actual=queryRuntimeNavigationTerrainInfo(port,cells[index]);}
   if(!expect(info(expected)==info(actual)&&oq==dq,"nested/missing/null context fields and ordered calls")||!expect(cacheValue(old.renderCache)==cacheValue(direct.renderCache),"nested/missing/null cache and errors"))return false;
   trace<<"nested mode="<<mode<<" cell="<<cell(cells[index])<<" value="<<info(actual)<<" calls="<<dq.size()<<'\n';queryRows+=dq.size();++nestedCases;
  }
 }
 // Sentinel, bridge-frame and wall metadata use copies; no synthetic cell is saved into the world.
 for(const int tile:std::array<int,4>{-1,0,65535,std::numeric_limits<int>::max()})for(const int health:std::array<int,3>{-1,0,1}){
  auto terrain=cells.front();terrain.tileIndex=tile;terrain.subTileIndex=-1;terrain.overlayWall.health=health;
  if(!pair(terrain,"copied-boundary",oldContext,directContext))return false;
 }
 // The real consumer reborrows a fresh context and rebuilds every map cell.
 std::map<int,std::string> expectedMap;
 {RuntimeSnapshotActiveStateScope active(old);const auto context=runtimeTerrainMetadataContext(old);
  for(const auto& terrain:cells)expectedMap[runtimeCellKey(terrain.x,terrain.y)]=info(runtimeOwnedNavigationTerrainInfo(Old::runtimeNavigationTerrainInfoForCell(old,terrain,context)));}
 {RuntimeSnapshotActiveStateScope active(consumer);rebuildRuntimeTerrainInfoCache(consumer);}
 if(!expect(consumer.terrainInfoByCell.size()==expectedMap.size(),"actual rebuild full cardinality"))return false;
 for(const auto& [key,value]:expectedMap){const auto found=consumer.terrainInfoByCell.find(key);if(!expect(found!=consumer.terrainInfoByCell.end()&&info(runtimeOwnedNavigationTerrainInfo(found->second))==value,"actual rebuild eleven-field map equality"))return false;++rebuildCells;}
 if(!expect(authority(old)==oldAuthority&&authority(direct)==directAuthority&&authority(consumer)==consumerAuthority,"terrain reads and real metadata rebuild preserve authority"))return false;
 // Reuse the validated ordinary active-session constructor, never the synthetic copied-boundary cells.
 char* skipRequested=nullptr;std::size_t skipLength=0;
 const auto skipResult=_dupenv_s(&skipRequested,&skipLength,"CONCORD_NAVIGATION_TERRAIN_QUERY_SKIP_LOAD");
 const bool skipPresent=skipRequested!=nullptr;const std::string skipValue=skipRequested?skipRequested:"";std::free(skipRequested);
 if(!expect(skipResult==0&&(!skipPresent||skipValue=="1"),"SKIP_LOAD must be absent or exactly 1"))return false;
 const bool skipLoad=skipPresent;
 if(!skipLoad){
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
 struct Clock final:RuntimeUiClock{mutable std::size_t reads=0;RuntimeUiTimeMs nowMs()const noexcept override{++reads;return 123456;}};
 struct ClockBinding{RuntimeWindowState& state;const RuntimeUiClock* prior;ClockBinding(RuntimeWindowState& s,const RuntimeUiClock& clock):state(s),prior(s.uiClock){s.uiClock=&clock;}~ClockBinding(){state.uiClock=prior;}};
 auto loadOld=makeBase(0,false),loadDirect=makeBase(0,false),loadConsumer=makeBase(0,false);
 if(!expect(loadOld&&loadDirect&&loadConsumer,"three independent ordinary active two-house roots"))return false;
 Clock oldClock,directClock,consumerClock;ClockBinding oa(*loadOld,oldClock),da(*loadDirect,directClock),ca(*loadConsumer,consumerClock);
 std::size_t route=0;
 for(auto* state:std::array<RuntimeWindowState*,3>{loadOld.get(),loadDirect.get(),loadConsumer.get()}){
  RuntimeSnapshotActiveStateScope active(*state);state->config.shortGame=true;state->tick=21;static_cast<void>(advanceRuntimeParticles(*state));
  const auto privateRoot=directory/("load-route-"+std::to_string(route));std::filesystem::create_directories(privateRoot,ec);
  if(!expect(!ec,"private legal save root"))return false;
  state->config.localDataRootPath=pathString(privateRoot);state->config.configPath=pathString(privateRoot/"preferences.ini");
  const auto path=runtimeDefaultSavePath(*state);const RuntimeUiClock* borrowed=state->uiClock;std::string error;
  if(!expect(state->particlesInitialized&&state->particles.currentTick()==21,"real fixed tick particle state"))return false;
  const bool saved=saveRuntimeState(*state,path,error);const bool loaded=saved&&loadRuntimeState(*state,path,error);
  if(!expect(saved&&loaded&&state->tick==21&&state->particles.currentTick()==21&&state->uiClock==borrowed,"ordinary active save/load retains borrowed clock: "+error))return false;
  trace<<"save route="<<route<<" tick="<<state->tick<<" particles="<<state->particles.currentTick()<<" borrowedClock=1 path="<<path<<'\n';++route;
 }
 const auto loadedOldAuthority=authority(*loadOld),loadedDirectAuthority=authority(*loadDirect),loadedConsumerAuthority=authority(*loadConsumer);
 std::map<int,std::string> loadedExpected;const auto loadedOldContext=runtimeTerrainMetadataContext(*loadOld),loadedDirectContext=runtimeTerrainMetadataContext(*loadDirect);
 for(const auto& terrain:loadOld->world.getTerrainCells()){
  RuntimeNavigationTerrainInfo expected,actual;std::vector<std::string> oq,dq;
  {RuntimeSnapshotActiveStateScope active(*loadOld);ObservationBinding observing(oq);expected=runtimeOwnedNavigationTerrainInfo(Old::runtimeNavigationTerrainInfoForCell(*loadOld,terrain,loadedOldContext));}
  const auto found=loadDirect->terrainByCell.find(runtimeCellKey(terrain.x,terrain.y));
  if(!expect(found!=loadDirect->terrainByCell.end(),"fresh loaded world has matching current cell"))return false;
  {RuntimeSnapshotActiveStateScope active(*loadDirect);ObservationBinding observing(dq);Port port(*loadDirect,loadedDirectContext);actual=queryRuntimeNavigationTerrainInfo(port,*found->second);}
  if(!expect(info(expected)==info(actual)&&oq==dq,"first post-load reads borrow current world/context/cache"))return false;
  loadedExpected.emplace(runtimeCellKey(terrain.x,terrain.y),info(expected));
 }
 {RuntimeSnapshotActiveStateScope active(*loadConsumer);rebuildRuntimeTerrainInfoCache(*loadConsumer);}
 if(!expect(loadConsumer->terrainInfoByCell.size()==loadedExpected.size(),"post-load actual consumer cardinality"))return false;
 for(const auto& [key,value]:loadedExpected){const auto found=loadConsumer->terrainInfoByCell.find(key);if(!expect(found!=loadConsumer->terrainInfoByCell.end()&&info(runtimeOwnedNavigationTerrainInfo(found->second))==value,"post-load consumer all eleven fields"))return false;}
 if(!expect(authority(*loadOld)==loadedOldAuthority&&authority(*loadDirect)==loadedDirectAuthority&&authority(*loadConsumer)==loadedConsumerAuthority&&loadOld->uiClock==&oldClock&&loadDirect->uiClock==&directClock&&loadConsumer->uiClock==&consumerClock,"post-load read-only authority and current borrowed clock"))return false;
 ++loadCases;
 trace<<"load cells="<<loadedExpected.size()<<" oldReads="<<oldClock.reads<<" directReads="<<directClock.reads<<" consumerReads="<<consumerClock.reads<<" freshBorrow=1\n";
 }else{
  trace<<"load skipped=1 reason=explicit-query-only-role\n";
 }
 trace<<"checks="<<checks<<" differentialCases="<<cases<<" orderedPrimitiveRows="<<queryRows<<" rebuildCells="<<rebuildCells<<" nestedCases="<<nestedCases<<" loadCases="<<loadCases<<" fields=11\n";
 trace.flush();if(!expect(static_cast<bool>(trace),"complete diagnostic trace write"))return false;
 output<<"Navigation terrain integration passed: checks="<<checks<<"; differentialCases="<<cases<<"; orderedPrimitiveRows="<<queryRows<<"; rebuildCells="<<rebuildCells<<"; nestedCases="<<nestedCases<<"; loadCases="<<loadCases<<"; fields=11; thirdRoute=actual-rebuild; nativeWindow=not-required\n";
 return true;
}