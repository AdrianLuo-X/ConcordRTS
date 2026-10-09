// Issue12 original inputs only. This candidate does not replace authored terrain or actors.
static int runtimeOriginalMapCombatLimit() {
#if __has_include("new_features/SkirmishRoster.h")
    return concord::new_features::skirmishPlayerLimit;
#else
    return 8; // This edition's original RuntimeSkirmishMenu kPlayerSlots.
#endif
}
static std::string runtimeOriginalMapCombatColor(int slot) {
#if __has_include("new_features/SkirmishRoster.h")
    return concord::new_features::skirmishColorIds[static_cast<std::size_t>(slot)];
#else
    const std::array<const char*,8> colors{{"Gold","DarkRed","DarkBlue","DarkGreen","Orange","DarkSky","Purple","Magenta"}};
    return colors[static_cast<std::size_t>(slot)];
#endif
}
static std::string runtimeOriginalMapCsv(const std::string& value) {
    std::string out="\""; for(char c:value){ if(c=='\"') out+='\"'; out+=c; } return out+'\"';
}
static std::uint64_t runtimeOriginalMapByteFingerprint(const std::string& bytes) {
    std::uint64_t h=14695981039346656037ULL;
    for(unsigned char c:bytes){h^=c;h*=1099511628211ULL;} return h;
}
static bool runtimeOriginalMapReadBytes(const std::string& path, std::string& bytes) {
    std::ifstream in(path,std::ios::binary); if(!in) return false;
    bytes.assign(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
    return !in.bad();
}
static bool runtimeOriginalMapInsideLocal(const RuntimeWindowState& s,const RuntimeCellPoint& p) {
    const auto projection=buildRuntimeRadarProjection(s);
    return runtimeRadarProjectionContains(projection,
        runtimeRadarDxyPoint(p.x,p.y,projection.fullMapWidth));
}
static bool runtimeOriginalMapConfigureCase(const RuntimeConfig& base,
    const std::vector<std::string>& f,const CampaignDatabase& campaigns,
    RuntimeConfig& cfg,std::string& detail) {
    cfg=base; cfg.defaultMapPath=f[5]; cfg.campaignSourceId=f[2];
    cfg.gameMode=f[17];cfg.gameModeRulesOverride=f[18];
    cfg.playerSlots.clear();cfg.campaignId.clear();cfg.campaignMissionId.clear();
    cfg.campaignPlayerHouse.clear();cfg.campaignNextMissionId.clear();
    cfg.campaignLoadSavePath.clear();cfg.campaignSavePath.clear();cfg.skirmishLoadSavePath.clear();
    cfg.multiplayerActive=false;cfg.showMainMenu=false;cfg.showSkirmishMenu=false;
    if(f[1]=="campaign") {
        const CampaignMissionDefinition* found=nullptr;
        for(const auto& campaign:campaigns.campaigns) for(const auto& mission:campaign.missions)
            if(mission.sourceId==f[2] && toUpperAsciiLocal(mission.mapFileName)==toUpperAsciiLocal(f[22])) {
                if(found){detail="ambiguous actual campaign mission";return false;} found=&mission;
            }
        if(!found || found->mapPath.empty()){detail="actual campaign mission unavailable";return false;}
        std::string actual, staged;
        if(!runtimeOriginalMapReadBytes(found->mapPath,actual)
            ||!runtimeOriginalMapReadBytes(f[5],staged)||actual!=staged) {
            detail="actual campaign resolver bytes differ from frozen scene";return false;
        }
        cfg.campaignId=found->campaignId;cfg.campaignMissionId=found->id;
        cfg.campaignPlayerHouse=found->playerHouse;cfg.playerCountry=found->playerCountry;
        cfg.campaignNextMissionId=found->nextMissionId;cfg.startingCredits=found->startingCredits;
        cfg.startingUnits=0;cfg.shortGame=false;cfg.mcvRepacks=false;cfg.crates=false;
        detail=found->sourceMap; return true;
    }
    const bool originalDefault=f[1]=="default-config";
    if(f[1]!="menu"&&!originalDefault) {cfg.playerCountry.clear();detail="additional original payload, no advertised start authority";return true;}
    if(originalDefault){std::string actual,staged;
        if(!runtimeOriginalMapReadBytes(base.defaultMapPath,actual)||!runtimeOriginalMapReadBytes(f[5],staged)||actual!=staged){detail="actual default config map bytes differ";return false;}
    }
    // Match the original lobby default loader: its other options are not RuntimeConfig defaults.
    if(!originalDefault){cfg.startingUnits=10;cfg.shortGame=true;cfg.mcvRepacks=true;cfg.crates=true;
        cfg.superweapons=false;cfg.buildOffAlly=false;}
    const std::array<std::filesystem::path,4> originalDefaults{{
        std::filesystem::path(cfg.resourceRootPath)/"GameMD"/"RA2MD.INI",
        std::filesystem::current_path()/std::filesystem::path(cfg.resourceRootPath)/"GameMD"/"RA2MD.INI",
        std::filesystem::current_path()/"RedAlert2"/"GameMD"/"RA2MD.INI",
        std::filesystem::path(cfg.localDataRootPath)/"RA2MD.INI"}};
    if(!originalDefault)for(const auto& path:originalDefaults){IniFile ini;if(!ini.loadFromFile(path.generic_string()))continue;
        const auto* section=runtimeIniSectionNoCase(ini,"Skirmish");int count=10;
        const auto value=[&](const char* key,const char* fallback){return runtimeIniSectionValueNoCase(section,key,fallback);};
        if(parseRuntimeInt(value("UnitCount","10"),count))cfg.startingUnits=std::clamp(count,0,10);
        cfg.shortGame=parseRuntimeBool(value("ShortGame","yes"));cfg.mcvRepacks=parseRuntimeBool(value("MCVRepacks","yes"));
        cfg.crates=parseRuntimeBool(value("CratesAppear","yes"));cfg.superweapons=parseRuntimeBool(value("SuperWeaponsAllowed","no"));
        cfg.buildOffAlly=parseRuntimeBool(value("BuildOffAlly","no"));break;
    }
    const int minimum=std::stoi(f[19]), maximum=std::stoi(f[20]);
    int slots=std::max(2,minimum);
#if __has_include("new_features/SkirmishRoster.h")
    if(originalDefault)slots=base.defaultSkirmishPlayers;
#endif
    CooperativeScenarioDatabase cooperative;
    const CooperativeScenarioDefinition* coop=nullptr;
    if(toUpperAsciiLocal(cfg.gameMode)=="COOPERATIVE") {
        IniFile ini;const auto path=runtimeCooperativeScenarioPath(cfg);
        if(path.empty()||!ini.loadFromFile(path.generic_string())||!cooperative.loadFromIni(ini)
            ||!(coop=cooperative.findByMapId(f[3]))){detail="actual cooperative scenario unavailable";return false;}
        slots=4;
    }
    if(slots>maximum||slots>runtimeOriginalMapCombatLimit()||slots<2){detail="no legal advertised slot count";return false;}
    for(int i=0;i<slots;++i){RuntimeConfig::PlayerSlot slot;
        slot.enabled=true;slot.human=i==0;slot.local=i==0;slot.name=i==0?"Commander":"Computer "+std::to_string(i);
        slot.country=coop?coop->slots[static_cast<std::size_t>(i)].defaultCountry:"Americans";
        slot.color=runtimeOriginalMapCombatColor(i);
        slot.startPosition=(coop||originalDefault)?i+1:0;
        slot.team=toUpperAsciiLocal(cfg.gameMode)=="TEAMGAME"?(i<slots/2?"1":"2"):"-";
        if(coop&&!cooperativeSlotAllowsCountry(coop->slots[static_cast<std::size_t>(i)],slot.country)) {
            detail="cooperative authored default country is invalid";return false;
        }
        cfg.playerSlots.push_back(std::move(slot));
    }
    cfg.playerCountry=cfg.playerSlots.front().country;cfg.playerColor=cfg.playerSlots.front().color;
    if(!originalDefault)cfg.computerAiEnabled=false;
    detail=originalDefault?"actual default-config combat slots/options; original AI preserved":"advertised mode minimum legal slots; computer strategic AI explicitly paused";
    return true;
}
static int runtimeOriginalMapInitialActor(const RuntimeWindowState& s,
    std::ofstream& roster,const std::string& scene,int& ownedGround,int& unresolvedTypes,std::vector<int>& ids) {
    ids.clear();ownedGround=0;unresolvedTypes=0;
    for(const auto& object:s.world.getObjects()) {
        if(!object.alive||object.owner!=s.economy.owner||object.kind==WorldObjectKind::Structure) continue;
        const auto* unit=findRuntimeUnitType(s.rules,object);
        const auto mode=runtimeObjectMovementMode(s,object);
        const bool ground=mode==PathMovementMode::Ground||mode==PathMovementMode::Amphibious;
        if(ground)++ownedGround;if(!unit)++unresolvedTypes;
        const bool ordinary=unit&&unit->speed>0&&!(unit->teleporter&&!unit->harvester)
            && !runtimeUnitUsesJumpjetMovement(*unit)&&ground;
        const bool eligible=ordinary&&runtimeObjectCanReceiveMoveCommand(s,object);
        roster<<runtimeOriginalMapCsv(scene)<<','<<object.instanceId<<','<<runtimeOriginalMapCsv(object.type)
            <<','<<runtimeOriginalMapCsv(object.owner)<<','<<int(object.kind)<<','<<int(mode)<<','
            <<(unit?unit->speed:0)<<','<<object.x<<','<<object.y<<','<<object.level<<','<<object.subCell
            <<','<<object.health<<','<<eligible<<'\n';
        if(eligible)ids.push_back(object.instanceId);
    }
    std::sort(ids.begin(),ids.end());return ids.empty()?-1:ids.front();
}
static bool runtimeOriginalMapSelectGoal(RuntimeWindowState& s,int actorId,
    RuntimeCellPoint& goal,std::string& reason) {
    const auto* actor=findObjectById(s,actorId);if(!actor){reason="initial actor disappeared";return false;}
    const RuntimeCellPoint source=runtimeCellPointForObject(*actor);
    const auto occupied=buildRuntimeOccupancyLookup(s,actorId);
    const auto context=runtimeTerrainMetadataContext(s);
    bool sourceQualified=false;
    for(const auto& original:s.world.getTerrainCells())if(original.x==source.x&&original.y==source.y){
        if(original.tileIndex<0)break;TerrainCell descriptor=original;
        if(descriptor.tileIndex==65535){descriptor.tileIndex=0;descriptor.subTileIndex=0;}
        const auto land=resolveRuntimeTerrainSubTileLandType(descriptor,context,s.renderCache);
        const int ramp=resolveRuntimeTerrainSubTileRampType(descriptor,context,s.renderCache);
        // Original campaign authors may place initial units just outside LocalSize.
        // Keep the original source terrain/layer gates; the adjacent goal below must remain inside LocalSize.
        sourceQualified=(actor->onBridge?runtimeEffectiveTerrainLevel(original):original.level)==actor->level
            &&((actor->onBridge&&runtimeOverlayProvidesBridgeDeck(original))
                ||((land==RuntimeTerrainLandType::Clear||land==RuntimeTerrainLandType::Road||land==RuntimeTerrainLandType::Rough)&&ramp==0));break;
    }
    if(!sourceQualified){reason="actual initial unit has no independently flat source transition";return false;}
    std::vector<RuntimeCellPoint> candidates;
    for(const auto& cell:s.world.getTerrainCells()) {
        const int dx=cell.x-source.x,dy=cell.y-source.y;
        if(std::abs(dx)+std::abs(dy)!=1||cell.tileIndex<0)continue;
        const bool sameBridgeDeck=actor->onBridge&&runtimeOverlayProvidesBridgeDeck(cell);
        if((sameBridgeDeck?runtimeEffectiveTerrainLevel(cell):cell.level)!=actor->level)continue;
        if(cell.hasOverlay&&!sameBridgeDeck&&!cell.overlayIsOre&&!cell.overlayIsGem)continue;
        const RuntimeCellPoint p{cell.x,cell.y};if(!runtimeOriginalMapInsideLocal(s,p)
            ||occupied.contains(runtimeCellKey(p.x,p.y)))continue;
        TerrainCell descriptor=cell;
        if(descriptor.tileIndex==65535){descriptor.tileIndex=0;descriptor.subTileIndex=0;}
        const auto land=resolveRuntimeTerrainSubTileLandType(descriptor,context,s.renderCache);
        const int ramp=resolveRuntimeTerrainSubTileRampType(descriptor,context,s.renderCache);
        if((land!=RuntimeTerrainLandType::Clear&&land!=RuntimeTerrainLandType::Road
            &&land!=RuntimeTerrainLandType::Rough)&&!sameBridgeDeck)continue;
        if(ramp!=0&&!sameBridgeDeck)continue;
        const auto* facts=runtimeMapQueryCell(s,p);
        if(!facts||!facts->hasTerrain||((facts->isWater||facts->isCliff||facts->isBridge)&&!sameBridgeDeck)
            ||runtimeFoundationBlocksMovement(s,*actor,p,-1,false,RuntimeGatePassageMode::Commit))continue;
        candidates.push_back(p);
    }
    std::stable_sort(candidates.begin(),candidates.end(),[&](const auto& a,const auto& b){
        const int da=std::abs(a.x-source.x)+std::abs(a.y-source.y),db=std::abs(b.x-source.x)+std::abs(b.y-source.y);
        if(da!=db)return da<db;if(a.y!=b.y)return a.y<b.y;return a.x<b.x;
    });
    if(candidates.empty()){reason="no independent flat empty adjacent local target near actual initial unit";return false;}
    // Do not precompute/assign a path or discard a genuinely clear target solely because current navigation rejects it.
    goal=candidates.front();return true;
}
static bool runtimeOriginalMapMove(RuntimeWindowState& s,int actorId,const RuntimeCellPoint& goal,
    const std::string& scene,std::ofstream& motion,std::ofstream& simulation,int& ticks,std::string& reason) {
    const char* stateHashValue=std::getenv("CONCORD_ORIGINAL_MAPS_STATE_HASH");
    const bool stateHashEnabled=stateHashValue&&std::string(stateHashValue)=="1";
    const auto* initial=findObjectById(s,actorId);if(!initial){reason="initial actor unavailable";return false;}
    const RuntimeCellPoint source=runtimeCellPointForObject(*initial);
    const TerrainCell* targetTerrain=nullptr;for(const auto& cell:s.world.getTerrainCells())if(cell.x==goal.x&&cell.y==goal.y){targetTerrain=&cell;break;}
    if(!targetTerrain){reason="independently qualified original target terrain disappeared";return false;}
    const bool targetBridge=runtimeCellIsBridge(s,goal.x,goal.y);
    const int targetLevel=targetBridge?runtimeEffectiveTerrainLevel(*targetTerrain):targetTerrain->level;
    const double initialZ=initial->hasRenderPosition?initial->renderLevel:double(initial->level);
    const auto initialOffset=worldObjectSubCellOffset(*initial);
    const double startX=runtimeObjectRenderX(*initial)+initialOffset.x,startY=runtimeObjectRenderY(*initial)+initialOffset.y;
    const auto sample=[&](const char* event){const auto* object=findObjectById(s,actorId);if(!object||!object->alive)return false;
        const auto offset=worldObjectSubCellOffset(*object);const auto behavior=s.behaviors.find(actorId);
        const double x=runtimeObjectRenderX(*object)+offset.x,y=runtimeObjectRenderY(*object)+offset.y,
            z=object->hasRenderPosition?object->renderLevel:double(object->level);
        motion<<runtimeOriginalMapCsv(scene)<<','<<event<<','<<s.tick<<','<<actorId<<','<<runtimeOriginalMapCsv(object->type)
            <<','<<object->x<<','<<object->y<<','<<object->subCell<<','<<object->level<<','<<object->onBridge
            <<','<<x<<','<<y<<','<<z<<','<<object->renderMoveActive<<','<<object->health<<','
            <<(behavior!=s.behaviors.end())<<','<<(behavior==s.behaviors.end()?-1:int(behavior->second.kind))
            <<','<<(behavior==s.behaviors.end()?-1:int(behavior->second.state))
            <<','<<(behavior==s.behaviors.end()?-1:behavior->second.moveTarget.x)
            <<','<<(behavior==s.behaviors.end()?-1:behavior->second.moveTarget.y)
            <<','<<(behavior==s.behaviors.end()?0:behavior->second.path.size())
            <<','<<(behavior==s.behaviors.end()?0:behavior->second.moveCooldownTicks)
            <<','<<(behavior==s.behaviors.end()?0:behavior->second.renderMoveTicksRemaining)
            <<','<<(behavior==s.behaviors.end()?0:behavior->second.renderMoveTicksTotal)<<'\n';
        return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z);
    };
    const int hz=std::max(1,s.config.fixedTickHz);
    const double distance=std::hypot(double(goal.x-source.x),double(goal.y-source.y));
    const double clock=std::max(1,runtimeMovementIntervalTicks(s,*initial));
    const int budget=int(std::clamp(std::ceil(distance*clock*32.0+hz*5.0),double(hz*10),double(hz*120)));
    if(!sample("initial")){reason="initial nonfinite or dead actor";return false;}
    applySelection(s,{actorId},false,false);std::map<int,RuntimeCellPoint> accepted;
    issueRuntimeMoveCommand(s,goal,&accepted);
    const auto chosen=accepted.find(actorId);
    if(s.lastCommand.acceptedCount!=1||chosen==accepted.end()||chosen->second!=goal){reason="actual Move receipt/accepted target differs";sample("rejected");return false;}
    if(!sample("command")){reason="command lost actor";return false;}
    const auto* afterCommand=findObjectById(s,actorId);const auto commandOffset=worldObjectSubCellOffset(*afterCommand);
    if(std::hypot(runtimeObjectRenderX(*afterCommand)+commandOffset.x-startX,
        runtimeObjectRenderY(*afterCommand)+commandOffset.y-startY)>1e-9
        ||std::abs((afterCommand->hasRenderPosition?afterCommand->renderLevel:double(afterCommand->level))-initialZ)>1e-9){reason="Move command changed actual body immediately";return false;}
    bool travelled=false;
    for(ticks=1;ticks<=budget;++ticks){
        ++s.tick;const auto simulationStarted=std::chrono::steady_clock::now();
        advanceRuntimeSimulation(s,false);
        const double simulationElapsedMs=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-simulationStarted).count();
        std::string authorityHashError;
        const std::uint64_t authorityHash=stateHashEnabled
            ? runtimeStandaloneSimulationStateHash(s,authorityHashError):0;
        simulation<<runtimeOriginalMapCsv(scene)<<','<<s.tick<<','<<simulationElapsedMs
            <<','<<s.perf.lastSimEffectsMs<<','<<s.perf.lastSimEconomyMs<<','<<s.perf.lastSimTriggersMs
            <<','<<s.perf.lastSimBuildMs<<','<<s.perf.lastSimGuardMs<<','<<s.perf.lastSimMoveMs
            <<','<<s.perf.lastSimAttackMs<<','<<s.perf.lastSimPathMs<<','<<s.perf.lastSimVisionMs
            <<','<<s.perf.lastSimRepathCount<<','<<s.perf.lastSimPathRequests
            <<','<<s.visionEnabled<<','<<s.config.computerAiEnabled<<','<<s.campaignMode<<','<<s.triggers.loaded
            <<','<<s.config.playerSlots.size()<<','<<s.world.getObjects().size()<<','<<s.world.getTerrainCells().size()
            <<','<<stateHashEnabled<<',';
        if(stateHashEnabled)simulation<<authorityHash;
        simulation<<','<<runtimeOriginalMapCsv(authorityHashError)<<'\n';
        if(stateHashEnabled&&(!authorityHashError.empty()||authorityHash==0)){
            reason="actual post-simulation authority hash failed: "+authorityHashError;return false;
        }
        if(!sample("tick")){reason="native simulation lost actor or produced nonfinite body";return false;}
        const auto* object=findObjectById(s,actorId);const auto offset=worldObjectSubCellOffset(*object);
        travelled=travelled||std::hypot(runtimeObjectRenderX(*object)+offset.x-startX,runtimeObjectRenderY(*object)+offset.y-startY)>1e-6;
        const auto behavior=s.behaviors.find(actorId);
        const bool settled=!object->renderMoveActive&&(behavior==s.behaviors.end()
            ||(behavior->second.state==RuntimeBehaviorState::Complete&&behavior->second.moveCooldownTicks<=0));
        if(settled){if(travelled&&runtimeCellPointForObject(*object)==goal
            &&std::hypot(runtimeObjectRenderX(*object)-goal.x,runtimeObjectRenderY(*object)-goal.y)<=1e-9
            &&object->level==targetLevel&&object->onBridge==targetBridge
            &&std::abs((object->hasRenderPosition?object->renderLevel:double(object->level))-targetLevel)<=1e-9){return true;}
            reason="native move settled at a different logical/rendered endpoint";return false;}
    }
    ticks=budget;reason="actual initial Move exceeded declared clock-derived capped budget";return false;
}
bool runRuntimeOriginalMapReadNavigationSmoke(const RuntimeConfig& base,std::string& error) {
    const char* manifestValue=std::getenv("CONCORD_ORIGINAL_MAPS_MANIFEST");
    const char* outputValue=std::getenv("CONCORD_ORIGINAL_MAPS_OUTPUT");
    const char* phaseValue=std::getenv("CONCORD_ORIGINAL_MAPS_PHASE");
    const std::string phase=phaseValue?phaseValue:"";
    if(!manifestValue||!*manifestValue||!outputValue||!*outputValue
        ||(phase!="loaded"&&phase!="movement"&&phase!="all")){error="Original maps: strict manifest/output/loaded|movement|all phase required";return false;}
    const bool movePhase=phase!="loaded";
    const std::string header="scene_id\tgroup\tsource_id\tmap_id\tsha256\tmap_path\tbytes\tfnv1a64\ttheater\twidth\theight\tiso_tiles\toverlay_cells\tstructures\tunits\tinfantry\taircraft\tgame_mode\tmode_rules\tmin_players\tmax_players\toriginal_source\tcampaign_scenario\toriginal_basename_known";
    try {
        std::ifstream manifest(manifestValue);std::string line;
        if(!manifest||!std::getline(manifest,line)){error="Original maps: manifest unavailable";return false;}
        if(!line.empty()&&line.back()=='\r')line.pop_back();if(line!=header){error="Original maps: manifest schema mismatch";return false;}
        const std::filesystem::path directory(outputValue);std::filesystem::create_directories(directory);
        std::ofstream results(directory/"maps.csv"),cells(directory/"cells.csv"),motion(directory/"motion.csv"),simulation(directory/"simulation.csv"),roster(directory/"initial-units.csv"),houses(directory/"house-starts.csv"),failures(directory/"failures.csv");
        if(!results||!cells||!motion||!simulation||!roster||!houses||!failures){error="Original maps: private output unavailable";return false;}
        results<<"scene,group,sha256,actual_map,resource_source,mode,theater,terrain,overlay,objects,map_facts,load_ok,nav_ok,eligible_sentinel,nav_mismatch,move_status,actor,ticks,elapsed_ms,detail\n";
        cells<<"scene,role,x,y,tile,subtile,level,in_local,has_overlay,occupant,raw_land,raw_ramp,clear0_land,clear0_ramp,nav_kind,nav_passable,water,cliff,bridge,facts_water,facts_cliff,map_query_ground_allowed,eligible_clear0,nav_discrepancy\n";
        motion<<"scene,event,tick,actor,type,logical_x,logical_y,slot,level,on_bridge,body_x,body_y,body_z,active,health,has_behavior,kind,state,goal_x,goal_y,path_size,cooldown,remaining,total\n"<<std::setprecision(17);
        simulation<<"scene,tick,elapsed_ms,effects_ms,economy_ms,triggers_ms,build_ms,guard_ms,move_ms,attack_ms,path_ms,vision_ms,repath_count,path_requests,vision_enabled,computer_ai_enabled,campaign_mode,triggers_loaded,configured_slots,objects,terrain_cells,state_hash_enabled,authority_hash,authority_hash_error\n"<<std::setprecision(17);
        roster<<"scene,actor,type,owner,kind,movement_mode,loaded_speed,x,y,level,slot,health,ordinary_command_eligible\n";
        houses<<"scene,slot,owner,country,color,enabled,human,local,observer,ai_enabled,alive_objects,initial_ordinary_ground,remap_present,remap_rgb\n";
        failures<<"scene,stage,detail\n";
        CampaignDatabaseOptions campaignOptions;campaignOptions.resourceRootPath=base.resourceRootPath;
        campaignOptions.localDataRootPath=base.localDataRootPath;campaignOptions.modRootPath=base.modRootPath;campaignOptions.includeDebugOnlyMissions=true;
        const auto campaigns=loadCampaignDatabase(campaignOptions);
        int attempted=0,loaded=0,navPassed=0,moveAttempted=0,arrived=0,noGround=0,payloadOnly=0,failed=0;
        while(std::getline(manifest,line)) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
            ++attempted;std::vector<std::string> f;std::istringstream split(line);std::string field;
            while(std::getline(split,field,'\t'))f.push_back(field);if(!line.empty()&&line.back()=='\t')f.emplace_back();
            if(f.size()!=24){error="Original maps: malformed manifest row "+std::to_string(attempted);return false;}
            const auto privateDirectory=directory/"scene-data"/(std::to_string(attempted)+"-"+f[4].substr(0,16));
            std::filesystem::create_directories(privateDirectory);
            const auto started=std::chrono::steady_clock::now();bool loadOk=false,navOk=false,sceneFailed=false;
            int eligible=0,mismatch=0,actor=-1,ticks=0;std::string detail,moveStatus=movePhase?"not-reached":"not-requested";
            auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
            RuntimeSnapshotActiveStateScope active(s);
            const auto fail=[&](const char* stage,const std::string& why){sceneFailed=true;failures<<runtimeOriginalMapCsv(f[0])<<','<<stage<<','<<runtimeOriginalMapCsv(why)<<'\n';detail=why;};
            try {
                std::string bytes;if(!runtimeOriginalMapReadBytes(f[5],bytes)
                    ||bytes.size()!=std::stoull(f[6])||runtimeOriginalMapByteFingerprint(bytes)!=std::stoull(f[7])) {fail("source","private original bytes/size/fingerprint mismatch");}
                else if(!runtimeOriginalMapConfigureCase(base,f,campaigns,s.config,detail)){fail("context",detail);}
                else {
                    s.config.campaignSavePath=pathString(privateDirectory/"campaign-save.ini");
                    s.config.runtimeReplayEventOutputPath=pathString(privateDirectory/"replay-events.txt");
                    s.runtimeCampaignProgressRootPathOverride=pathString(privateDirectory);
                    s.runtimePreferencePersistenceEnabled=false;s.runtimeMusicEnabled=false;s.runtimeSoundsEnabled=false;
                    s.runtimeVoiceVolume=0;s.runtimeSoundVolume=0;s.runtimeSuppressPlayerAudioFeedback=true;
                    s.report=buildRuntimeStartupReport(s.config,&s.world,&s.theater,&s.art,&s.rules,&s.mapFacts);
                    s.theaterLoaded=s.report.theaterLoaded;s.artLoaded=s.report.artLoaded;s.rulesLoaded=s.report.rulesLoaded;
                    const auto rawCountsMatch=[&](std::string& why){
                        std::vector<int> dimensions;std::istringstream sizeTokens(s.report.map.size);std::string token;bool validSize=true;
                        while(std::getline(sizeTokens,token,',')){int value=0;
                            if(!parseRuntimeInt(trimAsciiLocal(token),value))validSize=false;dimensions.push_back(value);}
                        validSize=validSize&&dimensions.size()==4&&!s.report.map.size.empty()&&s.report.map.size.back()!=',';
                        const int nativeWidth=validSize?dimensions[2]:-1,nativeHeight=validSize?dimensions[3]:-1;
                        const auto nativeAircraft=std::count_if(s.world.getObjects().begin(),s.world.getObjects().end(),[](const auto& object){return object.authoredAircraft;});
                        std::ostringstream values;values<<"MapSize="<<s.report.map.size<<" validFourIntegers="<<validSize
                            <<" sizeWidth="<<nativeWidth<<'/'<<f[9]<<" sizeHeight="<<nativeHeight<<'/'<<f[10]
                            <<" HeaderWidth="<<s.report.map.width<<" HeaderHeight="<<s.report.map.height
                            <<" theater="<<s.report.map.theater<<'/'<<f[8]<<" isoTiles="<<s.report.map.isoTiles<<'/'<<f[11]
                            <<" terrain="<<s.world.getTerrainCells().size()<<'/'<<f[11]<<" overlay="<<s.report.map.overlayCells<<'/'<<f[12]
                            <<" structures="<<s.report.map.structures<<'/'<<f[13]<<" units="<<s.report.map.units<<'/'<<f[14]
                            <<" infantry="<<s.report.map.infantry<<'/'<<f[15]<<" aircraft="<<nativeAircraft<<'/'<<f[16];why=values.str();
                        return validSize&&nativeWidth==std::stoi(f[9])&&nativeHeight==std::stoi(f[10])
                            &&toUpperAsciiLocal(s.report.map.theater)==toUpperAsciiLocal(f[8])
                            &&s.report.map.isoTiles==std::stoi(f[11])&&s.world.getTerrainCells().size()==std::stoull(f[11])
                            &&s.report.map.overlayCells==std::stoi(f[12])&&s.report.map.structures==std::stoi(f[13])
                            &&s.report.map.units==std::stoi(f[14])&&s.report.map.infantry==std::stoi(f[15])&&nativeAircraft==std::stoi(f[16]);
                    };
                    std::string rawCountDetail;
                    if(!s.report.success||!s.report.map.loaded||!s.theaterLoaded||!s.artLoaded||!s.rulesLoaded
                        ||s.world.getTerrainCells().empty()){fail("startup",s.report.errorMessage.empty()?"map/catalog/theater/geometry startup failed":s.report.errorMessage);}
                    else if(!rawCountsMatch(rawCountDetail)){fail("raw-counts",rawCountDetail);}
                    else if(!initializeRuntimeCooperativeSession(s)){fail("cooperative",s.cooperativeSessionStatus);}
                    else {
                        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);loadOk=true;++loaded;
                        const auto context=runtimeTerrainMetadataContext(s);const auto occupied=buildRuntimeOccupancyLookup(s);
                        const WorldObjectInstance metadataQuery{}; // Query-only; never inserted into the original world.
                        bool waterWritten=false,cliffWritten=false,structureWritten=false;
                        for(const auto& cell:s.world.getTerrainCells()) {
                            const auto info=runtimeCachedTerrainInfoForCell(s,cell);const bool sentinel=cell.tileIndex==65535;
                            const RuntimeCellPoint p{cell.x,cell.y};const auto* facts=runtimeMapQueryCell(s,p);
                            const auto oi=occupied.find(runtimeCellKey(p.x,p.y));
                            const bool waterNegative=!waterWritten&&info.water,cliffNegative=!cliffWritten&&info.cliff,
                                structureNegative=!structureWritten&&facts&&facts->occupiedByStructure;
                            if(!sentinel&&!waterNegative&&!cliffNegative&&!structureNegative)continue;
                            TerrainCell canonical=cell;canonical.tileIndex=0;canonical.subTileIndex=0;
                            const auto rawLand=resolveRuntimeTerrainSubTileLandType(cell,context,s.renderCache);
                            const int rawRamp=resolveRuntimeTerrainSubTileRampType(cell,context,s.renderCache);
                            const auto clearLand=resolveRuntimeTerrainSubTileLandType(canonical,context,s.renderCache);
                            const int clearRamp=resolveRuntimeTerrainSubTileRampType(canonical,context,s.renderCache);
                            const bool local=runtimeOriginalMapInsideLocal(s,p);
                            const bool qualifies=sentinel&&local&&!cell.hasOverlay&&oi==occupied.end()
                                &&facts&&facts->hasTerrain&&!facts->occupiedByStructure&&!facts->isWater&&!facts->isCliff&&!facts->isBridge
                                &&clearLand==RuntimeTerrainLandType::Clear&&clearRamp==0;
                            const bool queryAllows=runtimeMapQueryPassabilityAllows(s,p,metadataQuery,PathMovementMode::Ground);
                            const bool discrepancy=qualifies&&(!info.passable||!queryAllows);if(qualifies)++eligible;if(discrepancy)++mismatch;
                            cells<<runtimeOriginalMapCsv(f[0])<<','<<(sentinel?"sentinel":waterNegative?"water-negative":cliffNegative?"cliff-negative":"foundation-negative")
                                <<','<<p.x<<','<<p.y<<','<<cell.tileIndex<<','<<cell.subTileIndex<<','<<cell.level<<','<<local<<','<<cell.hasOverlay
                                <<','<<(oi==occupied.end()?-1:oi->second)<<','<<int(rawLand)<<','<<rawRamp<<','<<int(clearLand)<<','<<clearRamp
                                <<','<<int(info.kind)<<','<<info.passable<<','<<info.water<<','<<info.cliff<<','<<info.bridge
                                <<','<<(facts&&facts->isWater)<<','<<(facts&&facts->isCliff)<<','<<queryAllows<<','<<qualifies<<','<<discrepancy<<'\n';
                            waterWritten=waterWritten||waterNegative;cliffWritten=cliffWritten||cliffNegative;structureWritten=structureWritten||structureNegative;
                        }
                        navOk=mismatch==0;if(navOk)++navPassed;else fail("clear0-nav","independently qualified original clear sentinel is blocked in actual navigation");
                        if(movePhase) {
                            if(f[1]!="menu"&&f[1]!="campaign"&&f[1]!="default-config"){moveStatus="additional-payload-no-advertised-start";++payloadOnly;}
                            else {
                                loadRuntimeSoundDatabase(s);ensureRuntimeLightingInitialized(s);ensureRuntimeParticlesInitialized(s);
                                bool startReady=true;if(!s.campaignMode&&!initializeRuntimeSkirmishStart(s)
                                    &&s.economy.skirmishStatus!="existing construction yard"){startReady=false;fail("formation",s.economy.skirmishStatus);}
                                if(startReady&&!initializeRuntimeTriggers(s)){startReady=false;fail("triggers",s.triggers.status);}
                                if(startReady){std::set<COLORREF> defaultColors;
                                    for(std::size_t slotIndex=0;slotIndex<s.config.playerSlots.size();++slotIndex){
                                        const auto& slot=s.config.playerSlots[slotIndex];const auto owner=runtimeSkirmishOwnerForSlot(slotIndex);
                                        const bool observer=runtimeObserverSlot(slot);int aliveObjects=0,ordinaryGround=0;
                                        for(const auto& object:s.world.getObjects())if(object.alive&&object.owner==owner){++aliveObjects;
                                            const auto* unit=findRuntimeUnitType(s.rules,object);const auto mode=runtimeObjectMovementMode(s,object);
                                            if(unit&&unit->speed>0&&!runtimeUnitUsesJumpjetMovement(*unit)
                                                &&!(unit->teleporter&&!unit->harvester)&&(mode==PathMovementMode::Ground||mode==PathMovementMode::Amphibious))++ordinaryGround;}
                                        const auto remap=s.ownerRemapColors.find(toLowerAsciiLocal(owner));
                                        houses<<runtimeOriginalMapCsv(f[0])<<','<<slotIndex<<','<<runtimeOriginalMapCsv(owner)<<','<<runtimeOriginalMapCsv(slot.country)
                                            <<','<<runtimeOriginalMapCsv(slot.color)<<','<<slot.enabled<<','<<slot.human<<','<<slot.local<<','<<observer
                                            <<','<<s.config.computerAiEnabled<<','<<aliveObjects<<','<<ordinaryGround<<','<<(remap!=s.ownerRemapColors.end())
                                            <<','<<(remap==s.ownerRemapColors.end()?0:remap->second)<<'\n';
                                        if(slot.enabled&&!observer&&aliveObjects==0){startReady=false;fail("house-start","enabled original combat house has no actual spawned objects");}
                                        if(f[1]=="default-config"&&slot.enabled&&!observer&&(ordinaryGround==0||remap==s.ownerRemapColors.end()
                                            ||!defaultColors.insert(remap->second).second)){startReady=false;fail("default-house-start","default combat house/ground actor/distinct original remap missing");}
                                    }
                                    if(startReady){int ownedGround=0,unresolved=0;std::vector<int> initialIds;actor=runtimeOriginalMapInitialActor(s,roster,f[0],ownedGround,unresolved,initialIds);
                                    if(actor<0){if(unresolved){moveStatus="initial-type-unresolved";fail("roster","owned original mobile type unresolved");}
                                        else {moveStatus=ownedGround?"no-commandable-ordinary-initial-ground-unit":"no-initial-ground-unit";++noGround;}}
                                    else {RuntimeCellPoint goal;std::string why;bool targetReady=false;
                                        for(int id:initialIds)if(runtimeOriginalMapSelectGoal(s,id,goal,why)){actor=id;targetReady=true;break;}
                                        if(!targetReady){moveStatus="target-qualification-failed";fail("target",why);}
                                        else {++moveAttempted;if(runtimeOriginalMapMove(s,actor,goal,f[0],motion,simulation,ticks,why)){moveStatus="arrived";++arrived;}
                                            else {moveStatus="actual-move-failed";fail("move",why);}}
                                    }
                                    }else moveStatus="startup-house-qualification-failed";
                                } else moveStatus="startup-formation-or-trigger-failed";
                            }
                        }
                    }
                }
            }catch(const std::exception& ex){fail("exception",ex.what());}
            if(sceneFailed)++failed;
            const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            results<<runtimeOriginalMapCsv(f[0])<<','<<runtimeOriginalMapCsv(f[1])<<','<<f[4]<<','<<runtimeOriginalMapCsv(s.config.defaultMapPath)
                <<','<<runtimeOriginalMapCsv(s.config.campaignSourceId)<<','<<runtimeOriginalMapCsv(s.config.gameMode)<<','<<runtimeOriginalMapCsv(s.report.map.theater)
                <<','<<s.world.getTerrainCells().size()<<','<<s.world.getOverlayCells().size()<<','<<s.world.getObjects().size()<<','<<s.mapFacts.cells.size()
                <<','<<loadOk<<','<<navOk<<','<<eligible<<','<<mismatch<<','<<moveStatus<<','<<actor<<','<<ticks<<','<<elapsed<<','<<runtimeOriginalMapCsv(detail)<<'\n';
            results.flush();cells.flush();motion.flush();simulation.flush();roster.flush();houses.flush();failures.flush();
            std::cout<<"Original maps scene: "<<f[0]<<" loaded="<<loadOk<<" nav="<<navOk<<" movement="<<moveStatus<<" failed="<<sceneFailed<<" ms="<<elapsed<<'\n';
        }
        if(!results||!cells||!motion||!simulation||!roster||!houses||!failures){error="Original maps: output stream failed";return false;}
        std::cout<<"Original maps summary: phase="<<phase<<" attempted="<<attempted<<" loaded="<<loaded<<" navPassed="<<navPassed
            <<" moveAttempted="<<moveAttempted<<" arrived="<<arrived<<" noGround="<<noGround<<" payloadOnly="<<payloadOnly<<" failed="<<failed<<'\n';
        if(attempted==0||failed){error="Original maps: actual scene failures="+std::to_string(failed);return false;}
        return true;
    }catch(const std::exception& ex){error=std::string("Original maps: ")+ex.what();return false;}
}
