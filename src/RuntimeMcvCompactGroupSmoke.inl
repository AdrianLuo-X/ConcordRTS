// Real deploy/repack and UI Move commands. The capacity oracle never supplies
// a destination, path, movement clock or final pose to the simulation.
bool runRuntimeMcvCompactGroupSmoke(const RuntimeConfig& config,std::string& error) {
    std::cout << std::unitbuf;
    const char* output=std::getenv("CONCORD_MCV_COMPACT_GROUP_OUTPUT");
    const std::filesystem::path root=output&&*output?output:
        "build/smoke/feature-set-19-complete/06-mcv-compact-groups/checks";
    const bool minimal=std::getenv("CONCORD_MCV_COMPACT_GROUP_MINIMAL")!=nullptr;
    const bool diagnostic=std::getenv("CONCORD_MCV_COMPACT_GROUP_DIAGNOSTIC")!=nullptr;
    std::filesystem::create_directories(root);
    std::ofstream groups(root/"groups.csv"),units(root/"units.csv");
    groups<<"scene,hz,layer,actors,minimum_cells,minimum_radius,target_cells,target_radius,target_area,final_cells,final_radius,final_area,settled_tick,stable_ticks,result\n";
    units<<"scene,hz,source_id,final_id,type,layer,target_x,target_y,final_x,final_y,render_x,render_y,subcell,result\n";
    std::string label;
    const auto fail=[&](const std::string& why){error="MCV compact "+label+": "+why;return false;};
    if(!groups||!units)return fail("evidence files");
    auto stock=std::make_unique<RuntimeWindowState>();stock->config=config;
    stock->config.playerSlots.clear();stock->config.gameModeRulesOverride.clear();
    stock->report=buildRuntimeStartupReport(stock->config,&stock->world,&stock->theater,&stock->art,&stock->rules,&stock->mapFacts);
    if(!stock->report.success)return fail("original startup");
    stock->rulesLoaded=stock->artLoaded=stock->theaterLoaded=true;
    const int clearSet=stock->theater.getGeneralTileSetIndex("ClearTile");int clearTile=-1;
    const auto terrainContext=runtimeTerrainMetadataContext(*stock);
    for(int tile=0;tile<static_cast<int>(stock->theater.getTotalTileSlots());++tile){
        TerrainCell cell;cell.tileIndex=tile;cell.subTileIndex=0;
        if(stock->theater.resolveTileIndex(tile).tileSetIndex==clearSet
            &&resolveRuntimeTerrainSubTileLandType(cell,terrainContext,stock->renderCache)==RuntimeTerrainLandType::Clear
            &&resolveRuntimeTerrainSubTileRampType(cell,terrainContext,stock->renderCache)==0){clearTile=tile;break;}
    }
    if(clearTile<0)return fail("original flat Clear TMP");
    struct Scenario {const char* name;const char* mcv;int hz;bool mixed;bool ordinaryFirst;};
    const std::array<Scenario,7> scenarios{{
        {"2AMCV", "AMCV",30,false,false}, {"2SMCV","SMCV",30,false,false},
        {"2PCV","PCV",30,false,false}, {"2AMCV15Hz","AMCV",15,false,false},
        {"2AMCV60Hz","AMCV",60,false,false},
        {"mixed_mcv_first","AMCV",30,true,false}, {"mixed_ordinary_first","AMCV",30,true,true}
    }};
    struct Bounds {
        std::set<std::pair<int,int>> cells;int actors=0,radius=0,x0=10000,y0=10000,x1=-1,y1=-1;
        void add(RuntimeCellPoint p,RuntimeCellPoint goal){++actors;cells.emplace(p.x,p.y);
            radius=std::max(radius,std::max(std::abs(p.x-goal.x),std::abs(p.y-goal.y)));
            x0=std::min(x0,p.x);y0=std::min(y0,p.y);x1=std::max(x1,p.x);y1=std::max(y1,p.y);}
        int area()const{return actors?(x1-x0+1)*(y1-y0+1):0;}
    };
    const auto minimumRadius=[](int cells){int r=0;while((2*r+1)*(2*r+1)<cells)++r;return r;};
    constexpr RuntimeCellPoint goal{32,30};
    int cases=0,failed=0,saves=0,pairedTicks=0,roundTrips=0,midSaves=0,badSaves=0,legacyLoads=0;
    int deathLoads=0,snapshots=0,snapshotTicks=0;std::string firstFailure;
    for(const auto& scene:scenarios){
        if(minimal&&cases)break;label=scene.name;
        auto owned=std::make_unique<RuntimeWindowState>();auto& s=*owned;
        s.config=stock->config;s.config.fixedTickHz=scene.hz;s.config.playerCountry="Americans";
        s.config.gameMode="Battle";s.config.mcvRepacks=true;
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=false;
        s.config.crates=s.config.superweapons=s.config.shortGame=false;
        s.rules=stock->rules;s.art=stock->art;s.theater=stock->theater;s.report=stock->report;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=s.mapFactsLoaded=true;
        s.world=stock->world;s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();s.mapFacts={};
        s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=-1;
        s.visionEnabled=s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.economy.owner="Americans";s.economy.skirmishStartApplied=true;
        s.triggers.loaded=s.triggers.bootstrapComplete=true;
        RuntimeHumanHouseAuthority authority;authority.owner="Americans";authority.canonicalOwner="americans";authority.localFacade=true;
        s.humanHouseAuthorities={authority};
        for(int y=0;y<52;++y)for(int x=0;x<52;++x){
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);cell.tileIndex=clearTile;cell.subTileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
            MapCellFacts fact;fact.x=x;fact.y=y;fact.cellIndex=cell.cellIndex512;fact.hasTerrain=true;
            fact.terrain.x=x;fact.terrain.y=y;fact.terrain.tileIndex=clearTile;fact.terrain.subTileIndex=0;
            fact.terrain.level=fact.heightLevel=0;fact.terrainClass="clear";
            s.mapFacts.cellIndexLookup.emplace(fact.cellIndex,s.mapFacts.cells.size());s.mapFacts.cells.push_back(fact);
        }
        s.mapFacts.terrainCellCount=s.mapFacts.lookupIndexSize=s.mapFacts.cells.size();s.mapFacts.contentHash=0x19060001;
        initializeRuntimeNavigation(s);RuntimeSnapshotActiveStateScope active(s);
        std::vector<int> mcvIds,ordinaryIds;std::set<int> airIds;
        const auto spawnOrdinary=[&](){
            if(!scene.mixed)return true;
            int index=0;
            for(const char* type:{"E1","E1","E1","E1","E1","E1","E1","MTNK","MTNK","SHAD","SHAD"}){
                auto* object=addRuntimeObjectInstance(s,runtimeBuildKindForUnitTypeId(s.rules,type),type,{5+4*(index%5),17+5*(index/5)});
                if(!object)return false;ordinaryIds.push_back(object->instanceId);
                if(std::string(type)=="SHAD")airIds.insert(object->instanceId);++index;
            }
            return true;
        };
        if(scene.ordinaryFirst&&!spawnOrdinary())return fail("ordinary stock fixture");
        for(const RuntimeCellPoint start:{RuntimeCellPoint{8,8},RuntimeCellPoint{15,8}}){
            auto* object=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,scene.mcv,start);
            if(!object)return fail("stock MCV fixture");mcvIds.push_back(object->instanceId);
        }
        applySelection(s,mcvIds,false,false);
        if(!dispatchRuntimeSecondaryActionForSelectionFromUi(s))return fail("real group deploy command");
        for(int tick=0;tick<scene.hz*6;++tick) {
            bool mobile=false;for(int id:mcvIds)mobile=mobile||findObjectById(s,id)!=nullptr;
            if(!mobile)break;
            ++s.tick;advanceRuntimeSimulation(s);
        }
        for(int id:mcvIds)if(findObjectById(s,id))return fail("group turn/deploy did not finish");
        const auto yardIds=s.selectedObjectIds;
        if(yardIds.size()!=2)return fail("group deployment lost selection");
        if(!scene.ordinaryFirst&&!spawnOrdinary())return fail("ordinary stock fixture");
        // A surviving hostile house prevents automatic battle settlement;
        // it remains far outside both the route and the destination region.
        if(!addRuntimeObjectInstanceForOwner(s,"Russians",RuntimeBuildKind::Building,"NAPOWR",{46,46}))
            return fail("surviving hostile fixture");
        s.structureFootprints=buildRuntimeStructureFootprints(s);rebuildRuntimeEconomyPower(s);
        const auto step=[](RuntimeWindowState& state){RuntimeSnapshotActiveStateScope scope(state);++state.tick;advanceRuntimeSimulation(state);};
        int buildTicks=0;
        for(;buildTicks<scene.hz*30;++buildTicks){
            bool complete=true;for(int id:yardIds){const auto* yard=findObjectById(s,id);
                if(!yard||yard->kind!=WorldObjectKind::Structure)return fail("deploy did not retain yard");
                complete=complete&&yard->buildAnimTicksRemaining<=0;}
            if(complete)break;step(s);
        }
        if(buildTicks>=scene.hz*30)return fail("real buildup did not complete");
        applySelection(s,yardIds,false,false);
        if(!dispatchRuntimeSecondaryActionForSelectionFromUi(s))return fail("real group repack command");
        step(s);
        std::map<int,int> foldingTicks;std::map<int,RuntimeCellPoint> starts;
        for(int id:yardIds){const auto* yard=findObjectById(s,id);
            if(!yard||!runtimeStructureIsRepacking(*yard)||yard->buildAnimTicksRemaining<=1
                ||yard->buildAnimTicksRemaining>=yard->buildAnimTicksTotal)return fail("no real in-progress folding window");
            foldingTicks[id]=yard->buildAnimTicksRemaining;starts[id]=runtimeCellPointForObject(*yard);}
        std::vector<int> ids=yardIds;ids.insert(ids.end(),ordinaryIds.begin(),ordinaryIds.end());
        applySelection(s,ids,false,false);
        // Both commands cross the real UI command dispatcher while the yards
        // are folding. The second updates the pending destination without
        // advancing simulation or modifying paths/targets by hand.
        for(const RuntimeCellPoint target:{RuntimeCellPoint{30,28},goal}){
            if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,target)
                ||s.lastCommand.acceptedCount!=static_cast<int>(ids.size())||s.lastCommand.rejectedCount)
                return fail("UI group Move rejected members: accepted="+std::to_string(s.lastCommand.acceptedCount)
                    +" rejected="+std::to_string(s.lastCommand.rejectedCount));
            for(const auto& [id,ticks]:foldingTicks){const auto* yard=findObjectById(s,id);
                if(!yard||yard->buildAnimTicksRemaining!=ticks)return fail("retarget restarted folding animation");}
        }
        std::vector<std::string> problems;
        const auto problem=[&](const std::string& value){if(std::find(problems.begin(),problems.end(),value)==problems.end())problems.push_back(value);};
        std::map<int,RuntimeCellPoint> targets;std::map<int,int> currentIds;std::map<int,std::string> expectedTypes;
        std::array<Bounds,2> allocated,arrived;
        std::map<int,std::pair<int,int>> groundTargets;
        for(int id:ids){const auto* object=findObjectById(s,id);const auto move=s.behaviors.find(id);
            if(!object||move==s.behaviors.end()||move->second.kind!=RuntimeBehaviorKind::Move)return fail("accepted member has no pending Move");
            targets[id]=move->second.moveTarget;currentIds[id]=id;
            expectedTypes[id]=foldingTicks.count(id)?scene.mcv:object->type;
            if(!starts.count(id))starts[id]=runtimeCellPointForObject(*object);
            const int layer=airIds.count(id)?1:0;allocated[layer].add(targets[id],goal);
            if(!layer){auto& occupancy=groundTargets[runtimeCellKey(targets[id].x,targets[id].y)];
                if(object->kind==WorldObjectKind::Infantry)++occupancy.first;else ++occupancy.second;
                if(occupancy.second>1||(occupancy.second&&occupancy.first)||occupancy.first>3)problem("assigned_ground_capacity_conflict");}
        }
        const std::array<int,2> capacity{{scene.mixed?7:2,scene.mixed?2:0}};
        for(int layer=0;layer<2;++layer)if(capacity[layer]){
            if(allocated[layer].radius>minimumRadius(capacity[layer]))problem(layer?"air_targets_spread":"ground_targets_spread");
            if(!allocated[layer].cells.count({goal.x,goal.y}))problem(layer?"air_center_unused":"ground_center_unused");
        }
        const std::string savePath=pathString(root/(label+"-pending.ini"));
        if(!saveRuntimeState(s,savePath,error))return fail("pending full save: "+error);
        auto resumed=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*resumed,s,false);
        {RuntimeSnapshotActiveStateScope destination(*resumed);
            if(!loadRuntimeState(*resumed,savePath,error))return fail("pending full load: "+error);}
        ++saves;
        for(int id:ids){const auto* restored=findObjectById(*resumed,id);const auto move=resumed->behaviors.find(id);
            if(!restored||move==resumed->behaviors.end()||move->second.kind!=RuntimeBehaviorKind::Move||move->second.moveTarget!=targets[id])
                return fail("pending save changed accepted destination");
            if(foldingTicks.count(id)&&restored->buildAnimTicksRemaining!=foldingTicks[id])return fail("pending save changed folding timer");}
        const auto groupRows=[&](const IniFile& ini){
            std::vector<std::string> values;const int count=parseRuntimeIntOrDefault(ini.getValue("RepackMoveGroups","Count","-1"),-1);
            if(ini.getValue("RepackMoveGroups","Schema","")!="1"||count<0)return values;
            for(int n=0;n<count;++n)values.push_back(ini.getValue("RepackMoveGroups",std::to_string(n),""));return values;
        };
        IniFile pendingIni;if(!pendingIni.loadFromFile(savePath))return fail("pending evidence read");
        const auto pendingRows=groupRows(pendingIni);
        if(pendingRows.size()!=yardIds.size())return fail("pending command lost narrow group records");
        const std::string roundPath=pathString(root/(label+"-pending-resaved.ini"));
        {RuntimeSnapshotActiveStateScope source(*resumed);if(!saveRuntimeState(*resumed,roundPath,error))return fail("pending resave: "+error);}
        IniFile roundIni;if(!roundIni.loadFromFile(roundPath)||groupRows(roundIni)!=pendingRows)return fail("pending resave changed group intent");
        auto reloaded=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*reloaded,*resumed,false);
        {RuntimeSnapshotActiveStateScope destination(*reloaded);if(!loadRuntimeState(*reloaded,roundPath,error))return fail("pending reload: "+error);}
        resumed=std::move(reloaded);++roundTrips;++saves;
        // Rewrite one real section instead of appending duplicate INI keys.
        // Historical positive member IDs may legally be dead or controlled by
        // another owner; malformed syntax/duplicates are distinct from that.
        const auto replaceGroups=[&](const std::vector<std::string>& rows,bool omit){
            std::ostringstream out;
            for(const auto& section:pendingIni.getSectionNames()){
                if(toUpperAsciiLocal(section)=="REPACKMOVEGROUPS"){
                    if(!omit){out<<"[RepackMoveGroups]\nSchema=1\nCount="<<rows.size()<<'\n';
                        for(std::size_t n=0;n<rows.size();++n)out<<n<<'='<<rows[n]<<'\n';}
                }else{out<<'['<<section<<"]\n";for(const auto& [key,value]:*pendingIni.getSection(section))out<<key<<'='<<value<<'\n';}
            }return out.str();
        };
        if(scene.mixed){
            for(int bad=0;bad<8;++bad){auto brokenRows=pendingRows;
                auto fields=splitRuntimeSaveFields(brokenRows[0]);
                if(fields.size()<6)return fail("mixed group record missing members");
                if(bad==0)brokenRows[1]=brokenRows[0];
                else if(bad==6)brokenRows.push_back("bad"); // A valid first row must not partly commit.
                else{
                    if(bad==1)fields[0]=std::to_string(ordinaryIds.front()); // Real E1, ordinary Move, not a MCV.
                    if(bad==2)fields[5]=fields[4];
                    if(bad==3)fields[4]="-1";
                    if(bad==4)fields[1]="512";
                    if(bad==5)fields[4]="999999999999999999999999999999";
                    if(bad==7)fields[4]="0";
                    std::ostringstream row;for(std::size_t n=0;n<fields.size();++n)row<<(n?"|":"")<<fields[n];brokenRows[0]=row.str();
                }
                const std::string badPath=pathString(root/(label+"-bad-group-"+std::to_string(bad)+".ini"));
                {std::ofstream file(badPath);file<<replaceGroups(brokenRows,false);if(!file)return fail("bad group evidence write");}
                RuntimeSnapshotActiveStateScope destination(*resumed);std::ostringstream before,after;std::string why;
                if(!writeRuntimeStateToStream(*resumed,before,error))return fail("bad group baseline serialization");
                if(loadRuntimeState(*resumed,badPath,why))return fail("malformed group accepted kind="+std::to_string(bad));
                if(why.empty()||!writeRuntimeStateToStream(*resumed,after,error)||before.str()!=after.str())
                    return fail("malformed group changed live authority/player commands kind="+std::to_string(bad));
                std::cout<<"MCV group rejection "<<label<<" kind="<<bad<<" reason="<<why<<'\n';++badSaves;
            }
            const std::string legacyPath=pathString(root/(label+"-legacy-no-group.ini"));
            {std::ofstream file(legacyPath);file<<replaceGroups({},true);if(!file)return fail("legacy evidence write");}
            auto legacy=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*legacy,s,false);
            {RuntimeSnapshotActiveStateScope destination(*legacy);if(!loadRuntimeState(*legacy,legacyPath,error))return fail("legacy optional section: "+error);}
            for(int id:yardIds){const auto move=legacy->behaviors.find(id);
                if(move==legacy->behaviors.end()||move->second.moveTarget!=targets[id]||move->second.pendingHasSharedGoal)
                    return fail("legacy load invented shared goal or lost target");}++legacyLoads;
        }
        if(scene.mixed&&!scene.ordinaryFirst){
            // Separate fixture: a genuine team member dies through the normal
            // object-exit lifecycle. Its historical group ID must remain a
            // legal self-produced save, including exact v15 reconstruction.
            auto mp=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*mp,s,false);
            {RuntimeSnapshotActiveStateScope destination(*mp);if(!loadRuntimeState(*mp,savePath,error))return fail("death fixture load: "+error);}
            RuntimeSnapshotActiveStateScope source(*mp);
            RuntimeObjectExitRequest death;death.objectId=ordinaryIds.front();death.kind=RuntimeObjectExitKind::Destroyed;
            death.tick=mp->tick;death.mission="Destroyed";death.reason="MCV group saved member death";death.suppressCrew=true;
            if(queueRuntimeObjectExit(*mp,death)!=RuntimeObjectExitQueueResult::Queued||!drainRuntimeObjectExitQueue(*mp))
                return fail("real member death lifecycle");
            const std::string deathPath=pathString(root/(label+"-member-death.ini"));
            if(!saveRuntimeState(*mp,deathPath,error))return fail("member death save: "+error);
            auto deathCopy=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*deathCopy,*mp,false);
            {RuntimeSnapshotActiveStateScope destination(*deathCopy);if(!loadRuntimeState(*deathCopy,deathPath,error))return fail("member death load: "+error);}
            IniFile deathIni;if(!deathIni.loadFromFile(deathPath)||groupRows(deathIni)!=pendingRows)
                return fail("member death changed historical group identity");++deathLoads;++saves;
            mp->config.multiplayerActive=true;mp->config.multiplayerSetupHash=1;
            mp->config.multiplayerCanonicalMapId="stock-mcv-group";mp->config.multiplayerMapContentFingerprint=2;
            mp->config.multiplayerTimelineEpoch=1;mp->config.simulationRandomSeed=1;
            // This isolated exact-snapshot fixture uses the normal multiplayer
            // slot/house identities. The ordinary density scenes above retain
            // their original country-named owners and real command history.
            mp->config.playerSlots.clear();mp->config.playerSlots.resize(2);
            for(std::size_t i=0;i<mp->config.playerSlots.size();++i){
                auto& slot=mp->config.playerSlots[i];slot.enabled=slot.human=true;slot.local=i==0;
                slot.startPosition=static_cast<int>(i)+1;slot.country=i==0?"Americans":"Russians";slot.team="-";
                slot.color=i==0?"Gold":"Red";slot.peerId=i==0?"mcv-group-local":"mcv-group-remote";
                slot.name=i==0?"MCV group snapshot":"Distant opponent";
            }
            initializeRuntimeSkirmishPlayerIdentities(*mp,"Americans");
            for(auto& object:mp->world.accessObjects()){
                if(object.owner=="Americans")object.owner=runtimeSkirmishOwnerForSlot(0);
                else if(object.owner=="Russians")object.owner=runtimeSkirmishOwnerForSlot(1);
            }
            initializeRuntimeHumanHouseAuthorities(*mp);
            completeRuntimeWorldRefresh(*mp);
            mp->particles.reset(mp->config.fixedTickHz);mp->particlesInitialized=true;
            mp->superWeapons.reset(std::vector<SuperWeaponRule>{});mp->superWeaponsInitialized=true;
            if(!mp->particles.alignEmptyStateTick(mp->tick)||!mp->superWeapons.alignEmptyStateTick(mp->tick))return fail("v15 clocks");
            const auto exactHash=[&](RuntimeWindowState& state){RuntimeSnapshotActiveStateScope scope(state);
                return runtimeMultiplayerAuthoritativeStateHash(state,error);};
            const auto hash=exactHash(*mp);
            RuntimeMultiplayerSnapshotIdentity identity;identity.proposalId=1;identity.sourceTimelineEpoch=1;
            identity.kind=RuntimeMultiplayerSnapshotKind::Load;identity.snapshotTick=identity.barrierTick=mp->tick;
            identity.snapshotToken=label+"-member-death";identity.authoritativeStateHash=hash;identity.gameplaySetupFingerprint=1;
            RuntimeMultiplayerSnapshotMemoryImage image;RuntimeMultiplayerSnapshotStagedState staged;
            if(!hash||!exportRuntimeMultiplayerSnapshotV15(*mp,identity,image,error)
                ||!stageRuntimeMultiplayerSnapshot(*mp,image.bytes,image.descriptor,staged,error)||!staged.scratch)
                return fail("real pending v15 stage: "+error);
            if(exactHash(*staged.scratch)!=hash)return fail("v15 pending group hash mismatch");
            {std::ofstream file(root/(label+"-member-death-v15.ini"),std::ios::binary);
                file.write(reinterpret_cast<const char*>(image.bytes.data()),static_cast<std::streamsize>(image.bytes.size()));
                if(!file)return fail("v15 evidence write");}
            auto exact=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*exact,*mp,false);
            {RuntimeSnapshotActiveStateScope destination(*exact);commitRuntimeMultiplayerSnapshotAuthority(*exact,std::move(staged));}
            if(exactHash(*exact)!=hash)return fail("v15 commit group hash mismatch");
            for(int n=0;n<12;++n){step(*mp);step(*exact);++snapshotTicks;
                const auto left=exactHash(*mp),right=exactHash(*exact);
                if(!left||left!=right)return fail("v15 exact continuation diverged tick="+std::to_string(n+1)+" "+error);}
            ++snapshots;
        }
        const auto ownMembers=[&](const RuntimeWindowState& state){std::vector<const WorldObjectInstance*> members;
            for(const auto& object:state.world.getObjects())if(object.alive&&object.owner=="Americans")members.push_back(&object);return members;};
        const auto inspect=[&](const RuntimeWindowState& state){
            std::map<std::tuple<int,int,bool,bool>,std::set<int>> occupied;
            const auto members=ownMembers(state);
            if(members.size()!=ids.size())problem("transform_lost_or_duplicated_member");
            for(const auto* object:members){
                if(object->kind==WorldObjectKind::Structure)continue;
                auto& slots=occupied[{object->x,object->y,object->onBridge,object->airborne}];
                if(object->kind==WorldObjectKind::Infantry&&!object->airborne){
                    if(!RuntimeSubcellSystemState::isActiveSubCell(object->subCell)||slots.count(-1)||!slots.insert(object->subCell).second)
                        problem("infantry_subcell_collision");
                }else{if(!slots.empty())problem("whole_cell_collision");slots.insert(-1);}
                if(!object->airborne)for(const auto* yard:members)if(yard->kind==WorldObjectKind::Structure)
                    for(const auto cell:runtimeStructureFootprintCellsForObject(state,*yard))
                        if(runtimeCellPointForObject(*object)==cell)problem("unit_entered_folding_foundation");
            }
        };
        const auto compare=[&](){
            for(const auto* object:ownMembers(s)){const auto* other=findObjectById(*resumed,object->instanceId);
                if(!other||!other->alive||other->type!=object->type||other->kind!=object->kind
                    ||other->x!=object->x||other->y!=object->y||other->subCell!=object->subCell
                    ||std::abs(runtimeObjectRenderX(*object)-runtimeObjectRenderX(*other))>1e-8
                    ||std::abs(runtimeObjectRenderY(*object)-runtimeObjectRenderY(*other))>1e-8
                    ||other->buildAnimTicksRemaining!=object->buildAnimTicksRemaining||other->renderMoveActive!=object->renderMoveActive)
                    return false;}
            return ownMembers(s).size()==ownMembers(*resumed).size();
        };
        std::unique_ptr<RuntimeWindowState> midway;
        const auto captureMidway=[&](){
            if(!scene.mixed||midway)return true;
            int mcvs=0;bool activeSegment=false;
            for(const auto* object:ownMembers(s)){
                if(object->kind==WorldObjectKind::Structure)return true;
                if(object->type!=scene.mcv)continue;++mcvs;
                const auto move=s.behaviors.find(object->instanceId);
                if(move!=s.behaviors.end()&&object->renderMoveActive&&move->second.renderMoveTicksRemaining>0
                    &&move->second.renderMoveTicksRemaining<move->second.renderMoveTicksTotal)activeSegment=true;
            }
            if(mcvs!=2||!activeSegment)return true;
            const std::string midPath=pathString(root/(label+"-midmove.ini"));
            if(!saveRuntimeState(s,midPath,error))return fail("converted MCV midmove save: "+error);
            midway=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*midway,s,false);
            {RuntimeSnapshotActiveStateScope destination(*midway);if(!loadRuntimeState(*midway,midPath,error))return fail("MCV midmove load: "+error);}
            IniFile saved;if(!saved.loadFromFile(midPath))return fail("midmove evidence read");
            const auto records=groupRows(saved);if(records.size()!=2)return fail("converted MCV lost group record");
            std::set<int> realMembers;for(const auto* object:ownMembers(s))realMembers.insert(object->instanceId);
            for(const auto& record:records){const auto fields=splitRuntimeSaveFields(record);
                if(fields.size()<5)return fail("converted group record shape");std::set<int> members;
                for(std::size_t n=4;n<fields.size();++n)members.insert(parseRuntimeIntOrDefault(fields[n],-1));
                if(members!=realMembers)return fail("converted group did not remap old yard IDs to real members");
            }
            for(const auto* object:ownMembers(s))if(object->type==scene.mcv){const auto* other=findObjectById(*midway,object->instanceId);
                const auto move=s.behaviors.find(object->instanceId),restored=midway->behaviors.find(object->instanceId);
                if(!other||move==s.behaviors.end()||restored==midway->behaviors.end()
                    ||move->second.moveTarget!=restored->second.moveTarget||other->renderMoveActive!=object->renderMoveActive
                    ||std::abs(runtimeObjectRenderX(*object)-runtimeObjectRenderX(*other))>1e-8
                    ||std::abs(runtimeObjectRenderY(*object)-runtimeObjectRenderY(*other))>1e-8)
                    return fail("midmove load changed MCV pose/target");
                if(object->renderMoveActive&&(move->second.renderFrom!=restored->second.renderFrom
                    ||move->second.renderTo!=restored->second.renderTo
                    ||move->second.renderMoveTicksTotal!=restored->second.renderMoveTicksTotal
                    ||move->second.renderMoveTicksRemaining!=restored->second.renderMoveTicksRemaining
                    ||move->second.moveCooldownTicks!=restored->second.moveCooldownTicks))
                    return fail("midmove load changed MCV committed segment");
            }
            ++midSaves;++saves;inspect(*midway);
            std::cout<<"MCV midmove public load "<<label<<" tick="<<s.tick<<" realMCVs="<<mcvs<<" groupRows="<<records.size()<<'\n';
            return true;
        };
        std::array<int,3> settledTick{{-1,-1,-1}},stableTicks{{0,0,0}};
        std::array<std::string,3> stablePose;bool settled=false;int poseDifferenceTicks=0;
        std::map<int,std::array<double,2>> previousMcv[3];
        inspect(s);inspect(*resumed);
        for(int n=0;n<scene.hz*240;++n){
            step(s);step(*resumed);if(midway){step(*midway);inspect(*midway);}++pairedTicks;inspect(s);inspect(*resumed);
            if(!captureMidway())return false;
            // Ordinary v14 reconstructs path scheduling. Count trajectory
            // differences as diagnostics; require both real outcomes to be
            // safe and compact instead of imposing v15 exact-path semantics.
            if(!compare())++poseDifferenceTicks;
            if(s.skirmishMatchFinished||resumed->skirmishMatchFinished){problem("fixture_battle_ended");break;}
            for(int branch=0;branch<(midway?3:2);++branch){const auto& state=branch==2?*midway:branch?*resumed:s;
                bool stopped=true;std::ostringstream pose;pose<<std::setprecision(17);
                for(const auto* object:ownMembers(state)){
                    const auto move=state.behaviors.find(object->instanceId);
                    if(object->kind==WorldObjectKind::Structure||object->renderMoveActive
                        ||(move!=state.behaviors.end()&&move->second.state!=RuntimeBehaviorState::Complete))stopped=false;
                    const double x=runtimeObjectRenderX(*object),y=runtimeObjectRenderY(*object);
                    pose<<object->instanceId<<'|'<<object->x<<'|'<<object->y<<'|'<<object->subCell<<'|'<<x<<'|'<<y<<';';
                    if(object->kind==WorldObjectKind::Unit&&object->type==scene.mcv){
                        const auto before=previousMcv[branch].find(object->instanceId);
                        if(before!=previousMcv[branch].end()&&std::hypot(x-before->second[0],y-before->second[1])>1.5)
                            problem("mcv_visible_position_jump");
                        previousMcv[branch][object->instanceId]={x,y};
                    }
                }
                if(!stopped){if(settledTick[branch]>=0)problem("movement_restarted");stableTicks[branch]=0;continue;}
                if(settledTick[branch]<0){settledTick[branch]=state.tick;stablePose[branch]=pose.str();}
                if(pose.str()!=stablePose[branch])problem("settled_pose_changed");++stableTicks[branch];
            }
            if(stableTicks[0]>=scene.hz*3&&stableTicks[1]>=scene.hz*3
                &&(!scene.mixed||(midway&&stableTicks[2]>=scene.hz*3))){settled=true;break;}
        }
        if(!settled)problem("arrival_timeout");
        if(scene.mixed&&!midway)problem("no_real_converted_MCV_midmove_save");
        std::set<int> matched(ordinaryIds.begin(),ordinaryIds.end());
        for(int id:yardIds){const WorldObjectInstance* candidate=nullptr;int best=100000;
            for(const auto* object:ownMembers(s))if(object->kind==WorldObjectKind::Unit&&object->type==scene.mcv&&!matched.count(object->instanceId)){
                // Identity is read after conversion; target matching labels
                // CSV rows only and never drives or alters the simulation.
                const int distance=std::abs(object->x-targets[id].x)+std::abs(object->y-targets[id].y);
                if(distance<best){best=distance;candidate=object;}}
            if(candidate){currentIds[id]=candidate->instanceId;matched.insert(candidate->instanceId);}else problem("repacked_mcv_missing");}
        for(int id:ids){const auto* object=findObjectById(s,currentIds[id]);if(!object)continue;
            const int layer=airIds.count(id)?1:0;arrived[layer].add(runtimeCellPointForObject(*object),goal);
            if(object->kind==WorldObjectKind::Structure)problem("yard_never_finished_repacking");
            if(std::abs(runtimeObjectRenderX(*object)-object->x)>1e-6||std::abs(runtimeObjectRenderY(*object)-object->y)>1e-6)
                problem("render_not_at_final_cell");
            if(runtimeCellPointForObject(*object)==starts[id])problem("member_never_left_start");}
        for(int layer=0;layer<2;++layer)if(capacity[layer]&&arrived[layer].radius>minimumRadius(capacity[layer]))
            problem(layer?"air_final_spread":"ground_final_spread");
        std::array<Bounds,2> restoredArrivals;
        for(const auto* object:ownMembers(*resumed)){
            const int layer=airIds.count(object->instanceId)?1:0;
            restoredArrivals[layer].add(runtimeCellPointForObject(*object),goal);
            if(object->kind==WorldObjectKind::Structure)problem("restored_yard_never_finished_repacking");
            if(std::abs(runtimeObjectRenderX(*object)-object->x)>1e-6||std::abs(runtimeObjectRenderY(*object)-object->y)>1e-6)
                problem("restored_render_not_at_final_cell");
        }
        for(int layer=0;layer<2;++layer)if(capacity[layer]&&restoredArrivals[layer].radius>minimumRadius(capacity[layer]))
            problem(layer?"restored_air_final_spread":"restored_ground_final_spread");
        std::array<Bounds,2> midwayArrivals;
        if(midway)for(const auto* object:ownMembers(*midway)){
            const int layer=airIds.count(object->instanceId)?1:0;midwayArrivals[layer].add(runtimeCellPointForObject(*object),goal);
            if(object->kind==WorldObjectKind::Structure)problem("midmove_yard_reappeared");
            if(std::abs(runtimeObjectRenderX(*object)-object->x)>1e-6||std::abs(runtimeObjectRenderY(*object)-object->y)>1e-6)
                problem("midmove_render_not_at_final_cell");
        }
        if(midway)for(int layer=0;layer<2;++layer)if(capacity[layer]&&midwayArrivals[layer].radius>minimumRadius(capacity[layer]))
            problem(layer?"midmove_air_final_spread":"midmove_ground_final_spread");
        std::string result="PASS";if(!problems.empty()){result.clear();for(const auto& p:problems){if(!result.empty())result+=';';result+=p;}
            ++failed;if(firstFailure.empty())firstFailure=label+": "+result;}
        for(int id:ids){const auto* object=findObjectById(s,currentIds[id]);if(!object)continue;
            units<<label<<','<<scene.hz<<','<<id<<','<<object->instanceId<<','<<expectedTypes[id]<<','<<(airIds.count(id)?"air":"ground")
                <<','<<targets[id].x<<','<<targets[id].y<<','<<object->x<<','<<object->y<<','<<std::setprecision(17)
                <<runtimeObjectRenderX(*object)<<','<<runtimeObjectRenderY(*object)<<','<<object->subCell<<','<<result<<'\n';}
        for(int branch=0;branch<(midway?3:2);++branch)for(int layer=0;layer<2;++layer)if(capacity[layer]){
            const auto& final=branch==2?midwayArrivals[layer]:branch?restoredArrivals[layer]:arrived[layer];
            groups<<label<<(branch==2?"-midmove-restored":branch?"-restored":"-original")<<','<<scene.hz<<','<<(layer?"air":"ground")
                <<','<<final.actors<<','<<capacity[layer]<<','<<minimumRadius(capacity[layer])<<','<<allocated[layer].cells.size()
                <<','<<allocated[layer].radius<<','<<allocated[layer].area()<<','<<final.cells.size()<<','<<final.radius
                <<','<<final.area()<<','<<settledTick[branch]<<','<<stableTicks[branch]<<','<<result<<'\n';}
        if(scene.hz==30){const auto picture=drawRuntimeStateOffscreenAtCell(s,goal,800,600,error,
                pathString(root/(label+"-settled.bmp")),false,true);
            if(!picture||!picture->rendered)return fail("settled picture: "+error);}
        ++cases;groups.flush();units.flush();
        std::cout<<"MCV compact "<<label<<" hz="<<scene.hz<<" targets="<<allocated[0].radius<<'/'<<allocated[1].radius
            <<" final="<<arrived[0].radius<<'/'<<arrived[1].radius<<" restored="<<restoredArrivals[0].radius<<'/'<<restoredArrivals[1].radius
            <<" settled="<<settledTick[0]<<'/'<<settledTick[1]<<" stable="<<stableTicks[0]<<'/'<<stableTicks[1]
            <<" ordinarySaveTrajectoryDifferenceTicks="<<poseDifferenceTicks<<" "<<result<<'\n';
        if(!problems.empty()&&!diagnostic)break;
    }
    std::cout<<"MCV compact: cases="<<cases<<" failed="<<failed<<" fullSaves="<<saves<<" pairedTicks="<<pairedTicks
        <<" pendingResaveReload="<<roundTrips<<" convertedMidmoveSaves="<<midSaves<<" atomicBadGroupSaves="<<badSaves
        <<" legacyLoads="<<legacyLoads<<" memberDeathLoads="<<deathLoads<<" exactV15="<<snapshots<<" exactTicks="<<snapshotTicks<<'\n';
    if(failed)return fail(firstFailure);error.clear();return true;
}
