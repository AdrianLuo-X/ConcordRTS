// Issue25.11: real stock naval repair, original crane frames and complete saves.
// Include immediately after RuntimeShipyardDepthSmoke.inl.
#pragma optimize("", off)
bool runRuntimeShipyardRepairAnimationIssue25Smoke(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimeBuildingDepthOracle;
    std::cout<<std::unitbuf;
    const char* requested=std::getenv("CONCORD_SHIPYARD_REPAIR_ANIMATION_OUTPUT");
    const std::filesystem::path root=requested&&*requested?requested:
        "build/smoke/feature-set-25/11-shipyard-repair-animation";
    std::filesystem::create_directories(root);std::string label;
    const auto fail=[&](const std::string& why){error="Issue25 shipyard repair "+label+": "+why;
        std::cout<<error<<'\n';return false;};
    try {
        auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
        assets->config.playerSlots.clear();assets->config.gameModeRulesOverride.clear();
        assets->report=buildRuntimeStartupReport(assets->config,&assets->world,
            &assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
        if(!assets->report.success)return fail("original startup unavailable");
        assets->rulesLoaded=assets->artLoaded=assets->theaterLoaded=true;
        if(toLowerAsciiLocal(assets->report.map.theater)!="temperate")return fail("audited original temperate crane assets required");
        const auto cwd=std::filesystem::current_path(),sourcesRoot=cwd/"local_data/resources/mix_exports";
        const auto rawRules=readPinned(sourcesRoot/"ra2md/localmd/rulesmd.ini",
            "06761dd7f714e7d9400216ec3c06109ec5c1461f6a0727be7401eb9d8b0f6d05");
        const auto rawArt=readPinned(sourcesRoot/"ra2md/localmd/artmd.ini",
            "e1f0378394313c04ebbd5073f47785ee3e46f1b3c62d65724e8f3c310ee7ba31");
        const auto palette=readPinned(sourcesRoot/"extracted_palettes/cache_unittem.pal",
            "ed785e62eed291480f3198dd44f6b656ebe3a9b75e9f641944d710abc6bde3e3");
        IniFile ri,ai;
        if(!ri.loadFromString(std::string(rawRules.begin(),rawRules.end()))
            ||!ai.loadFromString(std::string(rawArt.begin(),rawArt.end()))
            ||palette.size()!=768||*std::max_element(palette.begin(),palette.end())>63)
            return fail("original INIs / six-bit palette");
        const auto generic=readPinned(sourcesRoot/"ra2/generic.mix",
            "dbe8d76687c1787e4f17396cdf6c88fe507b916031261492fa6375fe944f2b70");
        const auto genermd=readPinned(sourcesRoot/"ra2md/genermd.mix",
            "00ec7c6f020f2286533551ba216b653b5a721805e6f7cacfc3d52e05122ccb0e");
        const auto slice=[](const Bytes& archive,std::size_t offset,std::size_t count,const char* hash){
            if(offset>archive.size()||count>archive.size()-offset)throw std::runtime_error("original MIX range");
            Bytes bytes(archive.begin()+offset,archive.begin()+offset+count);
            if(sha256(bytes)!=hash)throw std::runtime_error("original crane slice SHA");return bytes;};
        struct Stock {std::string yard,ship,owner,power,image;Bytes bytes;
            int mainFrames=0,canvasW=0,canvasH=0;std::array<int,3> start{},end{},damagedStart{},damagedEnd{};};
        std::array<Stock,3> stock{{
            {"GAYARD","CARRIER","Americans","GAPOWR","GAYARD_S",
                slice(generic,5807934,471944,"719c1299e26eaef9bbea16067eec48b2d53e51f5dac9c88d3c3b2bb126564fa0"),36,432,304,{1,6,13},{6,13,18},{19,24,31},{24,31,36}},
            {"NAYARD","DRED","Russians","NAPOWR","NAYARD_S",
                slice(generic,8438990,625552,"0cddb9c6ccf4db487856cef54a2c4579239156c63e3f63cf1e26e8128517474f"),36,400,246,{1,6,13},{6,13,18},{19,24,31},{24,31,36}},
            {"YAYARD","AEGIS","YuriCountry","YAPOWR","YAYARD_S",
                slice(genermd,6377366,207336,"b1f2df049f2827c5fd6cb28e8db66212a09d6cbc4d010ebf4df2fc6222010fd9"),32,304,236,{0,0,0},{16,16,16},{17,17,17},{32,32,32}}
        }};
        const auto textYes=[](std::string value){return toLowerAsciiLocal(trimAsciiLocal(value))=="yes";};
        int authoredYards=0;
        if(const auto* registry=ri.getSection("BuildingTypes"))for(const auto& entry:*registry){
            const auto& id=entry.second;if(textYes(ri.getValue(id,"UnitRepair"))
                &&(textYes(ri.getValue(id,"Naval"))||textYes(ri.getValue(id,"WaterBound")))){
                if(std::none_of(stock.begin(),stock.end(),[&](const Stock& a){return a.yard==id;}))
                    return fail("unexpected original naval repair yard "+id);++authoredYards;}}
        if(authoredYards!=3)return fail("original three-yard census");
        for(const auto& item:stock){
            label=item.yard;const auto* b=assets->rules.findBuildingType(item.yard);
            const auto* a=assets->art.findArtType(item.yard);
            if(!b||!a||!b->unitRepair||!b->naval||!b->waterBound||b->powered
                ||b->numberOfDocks!=1||b->strength!=1500||a->foundation!="4x4"
                ||ri.getValue(item.yard,"NumberOfDocks")!="1"
                ||!textYes(ri.getValue(item.yard,"UnitRepair"))||textYes(ri.getValue(item.yard,"Powered"))
                ||ai.getValue(item.yard,"Foundation")!="4x4"
                ||ai.getValue(item.yard,"DockingOffset0")!="384,-128,0"
                ||int(little(item.bytes,6,2))!=item.mainFrames*2
                ||int(little(item.bytes,2,2))!=item.canvasW||int(little(item.bytes,4,2))!=item.canvasH)
                return fail("original yard / dock / original main-shadow halves");
            for(int phase=0;phase<3;++phase)for(bool damaged:{false,true}){
                const std::string key=phase==0?"SpecialAnim":phase==1?"SpecialAnimTwo":"SpecialAnimThree";
                const auto section=ai.getValue(item.yard,key+(damaged?"Damaged":""));
                if(section.empty()||ai.getValue(section,"Image")!=item.image
                    ||std::stoi(ai.getValue(section,"Start"))!=(damaged?item.damagedStart[phase]:item.start[phase])
                    ||std::stoi(ai.getValue(section,"LoopStart"))!=(damaged?item.damagedStart[phase]:item.start[phase])
                    ||std::stoi(ai.getValue(section,"LoopEnd"))!=(damaged?item.damagedEnd[phase]:item.end[phase])
                    ||ai.getValue(section,"LoopCount")!=(phase==1?"-1":"1")
                    ||ai.getValue(section,"Rate")!="200"||!textYes(ai.getValue(section,"Shadow"))
                    ||!textYes(ai.getValue(section,"Normalized"))||!ai.getValue(section,"End").empty())
                    return fail("original three-phase exclusive ART "+section);
            }
        }
        const double repairMinutes=std::stod(ri.getValue("General","URepairRate"));
        const int rawStep=std::stoi(ri.getValue("General","RepairStep"));
        const double rawPercent=std::stod(ri.getValue("General","RepairPercent").substr(0,2))/100.0;
        const auto& mechanism=assets->rules.getSpecialMechanismRules();
        if(repairMinutes!=.016||rawStep!=8||rawPercent!=.15
            ||mechanism.unitRepairRateMinutes!=repairMinutes||mechanism.repairStep!=rawStep
            ||mechanism.repairPercent!=rawPercent)return fail("original service rate/step/cost");
        const int waterSet=assets->theater.getGeneralTileSetIndex("WaterSet");int waterTile=-1,landTile=-1;
        const auto metadata=runtimeTerrainMetadataContext(*assets);const auto* waterRules=assets->rules.findLandType("Water");
        if(waterSet<0||!waterRules||waterRules->floatSpeed!=100.0)return fail("original WaterSet Float100");
        for(int tile=0;tile<int(assets->theater.getTotalTileSlots());++tile){
            TerrainCell c;c.tileIndex=tile;c.subTileIndex=0;const auto info=runtimeTerrainInfoForCell(*assets,c);
            if(!info.passable||info.shore||info.cliff||info.bridge)continue;
            if(waterTile<0&&assets->theater.resolveTileIndex(tile).tileSetIndex==waterSet
                &&resolveRuntimeTerrainSubTileLandType(c,metadata,assets->renderCache)==RuntimeTerrainLandType::Water
                &&resolveRuntimeTerrainSubTileRampType(c,metadata,assets->renderCache)==0)waterTile=tile;
            if(landTile<0&&!info.water)landTile=tile;
            if(waterTile>=0&&landTile>=0)break;
        }
        if(waterTile<0||landTile<0)return fail("native flat open-water and land");
        std::ofstream states(root/"states.csv"),frames(root/"frames.csv"),checks(root/"cases.csv"),
            saves(root/"checkpoints.csv"),resources(root/"sources.txt"),pulses(root/"repair-pulses.csv");
        if(!states||!frames||!checks||!saves||!resources||!pulses)return fail("evidence streams");
        states<<"case,replica,tick,phase,age,ship_health,credits,x,y,render_x,render_y,dock,slot,powered,power_produced,power_used,status\n";
        frames<<"case,stage,tick,phase,age,expected_frame,actual_frame,image,decoded_indices_equal,raw_nonremap_candidates,visible_original_pixels,changed_original_pixels,repeat_pixels_equal,world_unchanged\n";
        checks<<"case,kind,hz,damaged,real_ui,travel,dock,paid_pulses,seen_extend,seen_work,seen_retract,completed,passed\n";
        saves<<"case,stage,format,path,tick,phase,age,world_equal,world_contract_equal,repair_equal,special_equal,pose_equal,pixels_equal,passed\n";
        pulses<<"case,replica,tick,hp_before,hp_after,expected_heal,credits_before,credits_after,expected_cost,previous_pulse_tick,expected_interval,passed\n";
        resources<<"Original rules "<<sha256(rawRules)<<"\nART "<<sha256(rawArt)<<"\nunittem "<<sha256(palette)
            <<"\nLoopEnd exclusive: original AnimClass 4246FC..42470A / 42472A..424738 ends at relativeframe >= LoopEnd-Start; G/N main36 and Y main32, shadows excluded."
            <<"\nRate200 integer900/200=4 retail frames -> ceiling4*Hz/15 ticks. No production animation selector/duration constructs expected values."
            <<"\n14 scenes:8 core,3 Stop,3 actual economy power deficit (original yards Powered=no). No-ship/full-health/idling checks are core boundaries."
            <<"\n8 unique actual full files (4 ordinary14+4 canonical); real closed-file reopen and full materialization; work/retract in GAYARD healthy and YAYARD damaged."
            <<"\nRaw selected crane indices are compared in full. Actual full-world non-remap pixels and original adjacent-frame changes are positive witnesses, not an independent complete scene-Z oracle."
            <<"\nRepeated same-tick and same-tick restored whole DIBs must be exactly equal. Existing stock ShipyardDepth owns independent complete occlusion composition."
            <<"\nOrdinary14 world comparison canonicalizes only unused renderXYZ when hasRenderPosition=false AND renderMoveActive=false in private GameWorld copies, matching its original parser contract; raw hashes and full field diffs are retained. Canonical full world remains raw exact; active pose/flags/physics/repair/phase/pixels/HP/credits stay strict."
            <<"\nURepairRate="<<repairMinutes<<" RepairStep="<<rawStep<<" RepairPercent="<<rawPercent
            <<"\nWater tile="<<waterTile<<" set="<<waterSet<<" TMP Water ramp0 Float100; native cellIndex y*512+x, runtime lookup y*1000+x.\n";
        for(const auto& item:stock)resources<<item.yard<<" image="<<item.image<<" rawSHA="<<sha256(item.bytes)
            <<" main="<<item.mainFrames<<" canvas="<<item.canvasW<<'x'<<item.canvasH<<'\n';resources.flush();
        constexpr int yardId=101,shipId=102,powerId=103,width=1000,height=700;
        const RECT viewport{0,0,width,height};
        const auto frameAt=[](const Bytes& bytes,int index){if(index<0||unsigned(index)>=little(bytes,6,2))throw std::runtime_error("independent crane frame range");
            Bytes selected=bytes;const std::size_t at=8+std::size_t(index)*24;
            if(at+24>bytes.size())throw std::runtime_error("original frame header");
            std::copy_n(bytes.begin()+at,24,selected.begin()+8);return decodeFrameZero(selected);};
        const auto phase=[](const RuntimeWindowState& s){for(const auto& a:s.repairDepots.animations())if(a.hostId==yardId)
            return std::pair<int,int>{a.phase==RuntimeRepairDepotAnimationPhase::Extending?1:
                a.phase==RuntimeRepairDepotAnimationPhase::Repairing?2:3,a.ageTicks};
            return std::pair<int,int>{0,0};};
        const auto position=[](const WorldObjectInstance& o){return std::array<double,3>{runtimeObjectRenderX(o),runtimeObjectRenderY(o),runtimeObjectRenderLevel(o)};};
        const auto samePose=[&](const RuntimeWindowState& a,const RuntimeWindowState& b){const auto* x=findObjectById(a,shipId);const auto* y=findObjectById(b,shipId);
            return x&&y&&position(*x)==position(*y)&&x->x==y->x&&x->y==y->y&&x->level==y->level&&x->subCell==y->subCell
                &&x->health==y->health&&x->dockedToObjectId==y->dockedToObjectId&&x->dockIndex==y->dockIndex;};
        const auto step=[](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);++s.tick;advanceRuntimeSimulation(s,false);};
        const auto make=[&](const Stock& item,int hz,bool damaged,bool poorPower){
            auto p=std::make_unique<RuntimeWindowState>();auto& s=*p;seedRuntimeLoadScratch(s,*assets,false);
            s.config.fixedTickHz=hz;s.config.playerCountry=item.owner;s.config.playerSlots.clear();s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
            s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=s.config.crates=s.config.shortGame=s.config.superweapons=false;
            s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
            s.mapFacts={};s.mapFactsLoaded=true;s.terrainByCell.clear();s.terrainInfoByCell.clear();s.structureFootprints.clear();
            s.behaviors.clear();s.projectiles.clear();s.recentExpiredProjectiles.clear();s.impactEffects.clear();s.selectedObjectIds.clear();
            s.repairDepots.reset(hz);s.repairDepotsInitialized=false;s.specialMechanismsInitialized=s.superWeaponsInitialized=s.subcellsInitialized=false;
            s.economy={};s.economy.owner=item.owner;s.economy.credits=10000;s.economy.skirmishStartApplied=true;
            s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=s.visionEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
            s.computerAiByOwner.clear();s.computerMiningByOwner.clear();s.humanHouseAuthorities.clear();s.triggers={};s.triggers.bootstrapComplete=true;
            s.initialOreFieldsSeeded=true;s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=s.report.map.height=-1;
            s.ownerRemapColors.clear();s.ownerRemapColors[toLowerAsciiLocal(item.owner)]=RGB(235,210,45);
            RuntimeHumanHouseAuthority owner;owner.owner=owner.country=item.owner;owner.canonicalOwner=toLowerAsciiLocal(item.owner);owner.localFacade=true;
            s.humanHouseAuthorities.push_back(owner);s.lighting.reset({},RuntimeLightingConfig{hz,0,0});
            for(int y=10;y<=40;++y)for(int x=10;x<=40;++x){const bool isLand=x>=34&&y>=34;TerrainCell c;
                c.x=x;c.y=y;c.cellIndex512=y*512+x;c.tileIndex=isLand?landTile:waterTile;c.subTileIndex=0;
                s.world.accessTerrainCells().push_back(c);MapCellFacts v;v.x=x;v.y=y;v.cellIndex=c.cellIndex512;v.hasTerrain=true;
                v.terrain.x=x;v.terrain.y=y;v.terrain.tileIndex=c.tileIndex;v.terrain.subTileIndex=0;
                v.isWater=!isLand;v.terrainClass=isLand?"clear":"water";v.passabilityHint="passable";
                s.mapFacts.cellIndexLookup.emplace(v.cellIndex,s.mapFacts.cells.size());s.mapFacts.cells.push_back(v);
            }
            const auto add=[&](int id,const std::string& type,WorldObjectKind kind,int x,int y){WorldObjectInstance o;
                o.instanceId=id;o.type=type;o.kind=kind;o.owner=item.owner;o.mapId=label+"-"+std::to_string(id);o.alive=o.poweredOperational=true;
                o.x=x;o.y=y;o.cell=runtimeCellKey(x,y);o.health=runtimeMaxHealthForObject(s,o);o.mission="Guard";o.currentAmmo=-1;
                clearRuntimeRenderPosition(o);s.world.accessObjects().push_back(o);};
            add(yardId,item.yard,WorldObjectKind::Structure,20,20);add(shipId,item.ship,WorldObjectKind::Unit,27,21);
            if(!poorPower)add(powerId,item.power,WorldObjectKind::Structure,35,35);
            auto* host=findMutableObjectById(s,yardId);if(damaged)host->health=host->health/3;
            findMutableObjectById(s,shipId)->health-=128;
            s.terrainByCell=buildRuntimeTerrainLookup(s.world);s.structureFootprints=buildRuntimeStructureFootprints(s);
            initializeRuntimeNavigation(s);initializeRuntimeLocalProductionLanes(s.economy);rebuildRuntimeEconomyPower(s);ensureRuntimeRepairDepotsInitialized(s);
            initializeRuntimeOfflineGameplayRouting(s);step(s);return p;
        };
        int fixtures=0,cores=0,controls=0,ordinaryLoads=0,canonicalLoads=0,pairedTicks=0,bmps=0,pixelWitnesses=0,changedWitnesses=0,paidPulses=0;
        const auto ordinaryBehavior=[](const RuntimeObjectBehavior& v){return std::make_tuple(v.kind,v.targetObjectId,v.moveTarget.x,v.moveTarget.y,
            v.weaponCooldownTicks,v.moveCooldownTicks,v.attackBridge,v.attackBridgeCell.x,v.attackBridgeCell.y,v.pendingAllowedOccupiedObjectId,
            v.moveIntervalRemainderQ16,v.forceFire,v.attackGround,v.moveSegmentFrom.x,v.moveSegmentFrom.y,v.factoryExitClearanceHostId);};
        // Ordinary14 deliberately reconstructs unused pose caches from logical
        // cells (parser36268). R3 proved six differences, solely inactive XY of
        // Docked HORNET107..109. Work on private value copies, never live state:
        // only XYZ when hasRenderPosition=false AND renderMoveActive=false; all flags, active coordinates,
        // actual position, HP, mission, docking and clocks remain strict.
        const auto ordinaryWorldContractHash=[](const GameWorld& world){
            GameWorld copy=world;
            for(auto& o:copy.accessObjects())if(!o.hasRenderPosition&&!o.renderMoveActive){
                o.renderX=double(o.x);o.renderY=double(o.y);o.renderLevel=double(o.level);
            }
            return copy.deterministicSimulationStateHash();
        };
        const auto authorityEqual=[&](const RuntimeWindowState& a,const RuntimeWindowState& b,bool canonical){
            if(a.tick!=b.tick||(canonical?a.world.deterministicSimulationStateHash()!=b.world.deterministicSimulationStateHash():ordinaryWorldContractHash(a.world)!=ordinaryWorldContractHash(b.world))
                ||a.specialMechanisms.stateHash()!=b.specialMechanisms.stateHash()||a.repairDepots.stateHash()!=b.repairDepots.stateHash()
                ||a.lighting.stateHash()!=b.lighting.stateHash()||a.economy.credits!=b.economy.credits||!samePose(a,b)
                ||a.playerCommands.serialize()!=b.playerCommands.serialize()||a.behaviors.size()!=b.behaviors.size()
                ||a.projectiles.size()!=b.projectiles.size())return false;
            for(std::size_t n=0;n<a.projectiles.size();++n)if(serializeRuntimeProjectileEffect(a.projectiles[n])!=serializeRuntimeProjectileEffect(b.projectiles[n]))return false;
            for(const auto& [id,v]:a.behaviors){const auto i=b.behaviors.find(id);if(i==b.behaviors.end()||ordinaryBehavior(v)!=ordinaryBehavior(i->second)
                    ||(canonical&&runtimeMultiplayerSnapshotBehaviorRecord(id,v)!=runtimeMultiplayerSnapshotBehaviorRecord(id,i->second)))return false;}
            return true;
        };
        const auto cameraFor=[](){RuntimeMapCamera camera;camera.initialized=true;camera.tileWidth=60;camera.tileHeight=30;
            camera.worldOffsetX=-width/2;camera.worldOffsetY=44*15-height/2;return camera;};
        const auto draw=[&](RuntimeWindowState& s,const std::string& suffix,bool write,std::vector<std::uint32_t>& pixels){
            RuntimeSnapshotActiveStateScope active(s);const auto camera=cameraFor();const auto oldSelection=s.selectedObjectIds;s.selectedObjectIds.clear();
            const auto result=drawRuntimeStateOffscreenAtCell(s,{22,22},width,height,error,
                write?pathString(root/(label+"-"+suffix+".bmp")):"",false,false,-1,&pixels,false,&camera);
            s.selectedObjectIds=oldSelection;if(write&&result&&result->rendered)++bmps;return result;
        };
        const auto log=[&](const RuntimeWindowState& s,const char* replica){const auto* ship=findObjectById(s,shipId);const auto* yard=findObjectById(s,yardId);const auto a=phase(s);
            states<<label<<','<<replica<<','<<s.tick<<','<<a.first<<','<<a.second<<','<<(ship?ship->health:-1)<<','<<s.economy.credits
                <<','<<(ship?ship->x:-1)<<','<<(ship?ship->y:-1)<<','<<(ship?runtimeObjectRenderX(*ship):-1)
                <<','<<(ship?runtimeObjectRenderY(*ship):-1)<<','<<(ship?ship->dockedToObjectId:-1)<<','<<(ship?ship->dockIndex:-1)
                <<','<<(yard?yard->poweredOperational:false)<<','<<s.economy.powerProduced<<','<<s.economy.powerUsed
                <<','<<std::quoted(s.simulationStatus)<<'\n';
        };
        const auto inspect=[&](RuntimeWindowState& s,const Stock& item,bool damaged,int wantedPhase,int wantedAge,const std::string& stage,
                bool screenshot,bool requireChange){
            const int originalStart=wantedPhase?damaged?item.damagedStart[wantedPhase-1]:item.start[wantedPhase-1]:0;
            const int originalEnd=wantedPhase?damaged?item.damagedEnd[wantedPhase-1]:item.end[wantedPhase-1]:0;
            const int delay=(4*s.config.fixedTickHz+14)/15;
            const int stepIndex=wantedAge/delay;
            const int expected=wantedPhase?originalStart+(wantedPhase==2?stepIndex%(originalEnd-originalStart):
                std::min(originalEnd-originalStart-1,stepIndex)):-1;
            const auto worldBefore=s.world.deterministicSimulationStateHash(),repairBefore=s.repairDepots.stateHash();
            const auto specialBefore=s.specialMechanisms.stateHash();const int tickBefore=s.tick;
            std::vector<std::uint32_t> pixels,repeated;
            const auto actual=draw(s,stage,screenshot,pixels);
            if(!actual||!actual->rendered)return fail("actual crane draw "+error);
            const bool noMutation=s.tick==tickBefore&&s.world.deterministicSimulationStateHash()==worldBefore
                &&s.repairDepots.stateHash()==repairBefore&&s.specialMechanisms.stateHash()==specialBefore;
            const auto repeat=draw(s,stage+"-repeat",false,repeated);const bool repeatEqual=repeat&&pixels==repeated;
            bool indicesEqual=wantedPhase==0;int candidates=0,visible=0,changed=0;std::string selectedId;
            const std::string phaseKey=wantedPhase==1?"SpecialAnim":wantedPhase==2?"SpecialAnimTwo":"SpecialAnimThree";
            const std::string animationId=wantedPhase?ai.getValue(item.yard,phaseKey+(damaged?"Damaged":"")):"";
            if(wantedPhase&&actual->repairDepotAnimationLayers==1&&actual->repairDepotAnimationFrame==expected){
                const RuntimeObjectShpSpriteCacheEntry* selected=nullptr;const RuntimeAuxiliaryShpBytesCacheEntry* bytes=nullptr;
                for(const auto& [key,value]:s.renderCache.auxiliaryShpBytesByImage)if(value.loaded&&sha256(value.bytes)==sha256(item.bytes)){bytes=&value;break;}
                for(const auto& [key,value]:s.renderCache.auxiliaryShpSpritesByImage)
                    if(value.loaded&&value.frameIndex==expected&&key.rfind(toLowerAsciiLocal(animationId)+"|",0)==0){selected=&value;selectedId=value.resolvedImageId;break;}
                if(!bytes||!selected)return fail("actual selected crane does not use pinned original image/frame "+animationId);
                const auto original=frameAt(item.bytes,expected);
                indicesEqual=selected->image.globalWidth==original.canvasWidth&&selected->image.globalHeight==original.canvasHeight
                    &&selected->image.width==original.width&&selected->image.height==original.height
                    &&selected->image.offsetX==original.x&&selected->image.offsetY==original.y
                    &&selected->image.pixels==original.pixels;
                const auto previous=frameAt(item.bytes,std::max(originalStart,expected-1));
                const int cx=width/2,cy=20*30-cameraFor().worldOffsetY;
                const int left=cx-original.canvasWidth/2+original.x,top=cy-original.canvasHeight/2+original.y;
                for(int y=0;y<original.height;++y)for(int x=0;x<original.width;++x){
                    const auto index=original.pixels[std::size_t(y)*original.width+x];
                    if(!index||(index>=16&&index<=31))continue;
                    const int px=left+x,py=top+y;if(px<0||py<0||px>=width||py>=height)continue;++candidates;
                    const std::uint32_t color=(std::uint32_t(palette[index*3]*4)<<16)
                        |(std::uint32_t(palette[index*3+1]*4)<<8)|std::uint32_t(palette[index*3+2]*4);
                    if((pixels[std::size_t(py)*width+px]&0xffffffU)!=color)continue;++visible;
                    const int oldX=original.x+x-previous.x,oldY=original.y+y-previous.y;
                    const auto old=oldX>=0&&oldY>=0&&oldX<previous.width&&oldY<previous.height
                        ?previous.pixels[std::size_t(oldY)*previous.width+oldX]:0;
                    if(old!=index)++changed;
                }
                pixelWitnesses+=visible;changedWitnesses+=changed;
            }
            const bool correct=actual->repairDepotAnimationPhase==wantedPhase
                &&actual->repairDepotAnimationLayers==(wantedPhase?1:0)
                &&(!wantedPhase||(actual->repairDepotAnimationFrame==expected&&actual->repairDepotAnimationImage==animationId
                    &&actual->buildingSpecialAnimationLayers==1&&indicesEqual&&visible>0&&(!requireChange||changed>0)))
                &&repeatEqual&&noMutation;
            frames<<label<<','<<stage<<','<<s.tick<<','<<wantedPhase<<','<<wantedAge<<','<<expected<<','
                <<actual->repairDepotAnimationFrame<<','<<std::quoted(actual->repairDepotAnimationImage)<<','
                <<indicesEqual<<','<<candidates<<','<<visible<<','<<changed<<','<<repeatEqual<<','<<noMutation<<'\n';frames.flush();
            if(!correct)return fail("original crane phase/frame/pixels missing or mismatched phase="+std::to_string(wantedPhase)
                +" actualPhase="+std::to_string(actual->repairDepotAnimationPhase)+" expectedFrame="+std::to_string(expected)
                +" actualFrame="+std::to_string(actual->repairDepotAnimationFrame)+" layers="+std::to_string(actual->repairDepotAnimationLayers)
                +" rawEqual="+std::to_string(indicesEqual)+" originalVisible="+std::to_string(visible)+" originalChanged="+std::to_string(changed));
            return true;
        };
        const auto q=[](const std::string& value){std::ostringstream out;out<<std::quoted(value);return out.str();};
    const auto objectFields=[&](const WorldObjectInstance& o){std::map<std::string,std::string> fields;
        const auto put=[&](const char* key,const auto& v){std::ostringstream a;a<<std::setprecision(17)<<v;fields[key]=a.str();};
        const auto list=[](const auto& values){std::ostringstream a;for(const auto& v:values)a<<v<<',';return a.str();};
#define ISSUE25_HASH_OBJECT_FIELD(name) put(#name,o.name)
        ISSUE25_HASH_OBJECT_FIELD(instanceId);put("kind",int(o.kind));ISSUE25_HASH_OBJECT_FIELD(mapId);ISSUE25_HASH_OBJECT_FIELD(owner);ISSUE25_HASH_OBJECT_FIELD(type);
        ISSUE25_HASH_OBJECT_FIELD(health);ISSUE25_HASH_OBJECT_FIELD(x);ISSUE25_HASH_OBJECT_FIELD(y);ISSUE25_HASH_OBJECT_FIELD(cell);ISSUE25_HASH_OBJECT_FIELD(level);
        ISSUE25_HASH_OBJECT_FIELD(facing);ISSUE25_HASH_OBJECT_FIELD(turretFacing);ISSUE25_HASH_OBJECT_FIELD(subCell);ISSUE25_HASH_OBJECT_FIELD(mission);ISSUE25_HASH_OBJECT_FIELD(tag);
        if(o.bodyFacingClock.active()||o.turretFacingClock.active()){put("bodyFacingClock",runtimeSerializeFacingClock(o.bodyFacingClock));put("turretFacingClock",runtimeSerializeFacingClock(o.turretFacingClock));}
        ISSUE25_HASH_OBJECT_FIELD(veterancy);ISSUE25_HASH_OBJECT_FIELD(veterancyExperience);ISSUE25_HASH_OBJECT_FIELD(crateArmorMultiplier);ISSUE25_HASH_OBJECT_FIELD(crateFirepowerMultiplier);
        ISSUE25_HASH_OBJECT_FIELD(crateSpeedMultiplier);ISSUE25_HASH_OBJECT_FIELD(crateCloakable);if(o.modExtensions.initialized||!o.modExtensions.effects.empty())put("modExtensions",runtimeSerializeModExtensions(o.modExtensions));
        ISSUE25_HASH_OBJECT_FIELD(onBridge);ISSUE25_HASH_OBJECT_FIELD(authoredAircraft);ISSUE25_HASH_OBJECT_FIELD(alive);ISSUE25_HASH_OBJECT_FIELD(rubble);ISSUE25_HASH_OBJECT_FIELD(submerged);
        ISSUE25_HASH_OBJECT_FIELD(cloaked);ISSUE25_HASH_OBJECT_FIELD(cloakCooldownTicks);ISSUE25_HASH_OBJECT_FIELD(submergeCooldownTicks);ISSUE25_HASH_OBJECT_FIELD(deployed);
        ISSUE25_HASH_OBJECT_FIELD(deployStateTicksRemaining);ISSUE25_HASH_OBJECT_FIELD(prone);ISSUE25_HASH_OBJECT_FIELD(proneTicksRemaining);ISSUE25_HASH_OBJECT_FIELD(weaponCooldownTicksRemaining);
        ISSUE25_HASH_OBJECT_FIELD(radiationEruptionCooldownTicksRemaining);ISSUE25_HASH_OBJECT_FIELD(hasRenderPosition);ISSUE25_HASH_OBJECT_FIELD(renderX);ISSUE25_HASH_OBJECT_FIELD(renderY);
        ISSUE25_HASH_OBJECT_FIELD(renderMoveActive);ISSUE25_HASH_OBJECT_FIELD(renderLevel);ISSUE25_HASH_OBJECT_FIELD(renderOnBridge);ISSUE25_HASH_OBJECT_FIELD(buildAnimTicksTotal);
        ISSUE25_HASH_OBJECT_FIELD(buildAnimTicksRemaining);ISSUE25_HASH_OBJECT_FIELD(poweredOperational);ISSUE25_HASH_OBJECT_FIELD(factoryExitAnimationStartTick);
        ISSUE25_HASH_OBJECT_FIELD(factoryExitAnimationEndTick);ISSUE25_HASH_OBJECT_FIELD(factoryExitAirborne);ISSUE25_HASH_OBJECT_FIELD(factoryExitHostId);
        ISSUE25_HASH_OBJECT_FIELD(specialCashNextTick);ISSUE25_HASH_OBJECT_FIELD(specialCloneDeliveryObjectId);put("transportPassengerIds",list(o.transportPassengerIds));
        ISSUE25_HASH_OBJECT_FIELD(bioReactorAnimationStartTick);ISSUE25_HASH_OBJECT_FIELD(bioReactorAnimationPoweredStartTick);ISSUE25_HASH_OBJECT_FIELD(activeGunnerMode);
        ISSUE25_HASH_OBJECT_FIELD(activeGunnerWeaponIndex);ISSUE25_HASH_OBJECT_FIELD(activeGunnerTurretIndex);ISSUE25_HASH_OBJECT_FIELD(tankBunkerHostId);ISSUE25_HASH_OBJECT_FIELD(tankBunkerOccupantId);
        ISSUE25_HASH_OBJECT_FIELD(tankBunkerAnimationStartTick);ISSUE25_HASH_OBJECT_FIELD(tankBunkerLowering);ISSUE25_HASH_OBJECT_FIELD(dockedToObjectId);ISSUE25_HASH_OBJECT_FIELD(dockIndex);
        ISSUE25_HASH_OBJECT_FIELD(airborne);ISSUE25_HASH_OBJECT_FIELD(flightLevel);ISSUE25_HASH_OBJECT_FIELD(garrisonHostObjectId);put("garrisonOccupantIds",list(o.garrisonOccupantIds));
        ISSUE25_HASH_OBJECT_FIELD(garrisonFireCursor);ISSUE25_HASH_OBJECT_FIELD(currentAmmo);ISSUE25_HASH_OBJECT_FIELD(harvestAutoSearchSuspended);ISSUE25_HASH_OBJECT_FIELD(hasBeenCaptured);
#undef ISSUE25_HASH_OBJECT_FIELD
        return fields;};
    const auto terrainFields=[](const TerrainCell& o){std::map<std::string,std::string> fields;const auto put=[&](const char* key,const auto& v){std::ostringstream a;a<<std::setprecision(17)<<v;fields[key]=a.str();};
#define ISSUE25_HASH_TERRAIN_FIELD(name) put(#name,o.name)
        ISSUE25_HASH_TERRAIN_FIELD(x);ISSUE25_HASH_TERRAIN_FIELD(y);ISSUE25_HASH_TERRAIN_FIELD(cellIndex512);ISSUE25_HASH_TERRAIN_FIELD(tileIndex);ISSUE25_HASH_TERRAIN_FIELD(subTileIndex);
        ISSUE25_HASH_TERRAIN_FIELD(level);ISSUE25_HASH_TERRAIN_FIELD(iceGrowth);ISSUE25_HASH_TERRAIN_FIELD(hasOverlay);ISSUE25_HASH_TERRAIN_FIELD(overlayIndex);ISSUE25_HASH_TERRAIN_FIELD(overlayFrame);
        ISSUE25_HASH_TERRAIN_FIELD(overlayType);ISSUE25_HASH_TERRAIN_FIELD(overlayImage);ISSUE25_HASH_TERRAIN_FIELD(overlayIsOre);ISSUE25_HASH_TERRAIN_FIELD(overlayIsGem);
        put("wallOriginalIndex",o.overlayWall.originalIndex);put("wallOriginalFrame",o.overlayWall.originalFrame);put("wallOriginalType",o.overlayWall.originalType);put("wallOriginalImage",o.overlayWall.originalImage);put("wallHealth",o.overlayWall.health);
#undef ISSUE25_HASH_TERRAIN_FIELD
        return fields;};
        const auto dumpCheckpoint=[&](const RuntimeWindowState& a,const RuntimeWindowState& b,const std::string& stage,const char* format){
            const std::string prefix=label+"-"+stage+"-"+format;
            for(const auto& value:std::array<std::pair<const char*,const RuntimeWindowState*>,2>{{{"source",&a},{"peer",&b}}}){
                std::string detail;std::ostringstream text;
                if(writeRuntimeStateToStream(*value.second,text,detail)){
                    std::ofstream file(root/(prefix+"-"+value.first+"-state14.ini"),std::ios::binary);file<<text.str();}
                std::ostringstream exact;exact<<text.str();
                if(writeRuntimeMultiplayerSnapshotExtension(*value.second,exact,detail)){
                    std::ofstream file(root/(prefix+"-"+value.first+"-exact.txt"),std::ios::binary);file<<exact.str();}
            }
            std::ofstream diff(root/(prefix+"-field-diff.csv"));diff<<"kind,id,field,source,peer\n";
            const auto compareFields=[&](const char* kind,int id,const auto& l,const auto& r){
                std::set<std::string> names;for(const auto& [key,value]:l)names.insert(key);for(const auto& [key,value]:r)names.insert(key);
                for(const auto& key:names){const auto li=l.find(key),ri=r.find(key);
                    const std::string left=li==l.end()?"<missing>":li->second,right=ri==r.end()?"<missing>":ri->second;
                    if(left!=right)diff<<kind<<','<<id<<','<<q(key)<<','<<q(left)<<','<<q(right)<<'\n';}};
            std::map<int,std::map<std::string,std::string>> ao,bo,at,bt;
            for(const auto& o:a.world.getObjects())ao[o.instanceId]=objectFields(o);
            for(const auto& o:b.world.getObjects())bo[o.instanceId]=objectFields(o);
            for(const auto& o:a.world.getTerrainCells())at[runtimeCellKey(o.x,o.y)]=terrainFields(o);
            for(const auto& o:b.world.getTerrainCells())bt[runtimeCellKey(o.x,o.y)]=terrainFields(o);
            std::set<int> ids;for(const auto& [id,value]:ao)ids.insert(id);for(const auto& [id,value]:bo)ids.insert(id);
            for(int id:ids)compareFields("object",id,ao[id],bo[id]);
            ids.clear();for(const auto& [id,value]:at)ids.insert(id);for(const auto& [id,value]:bt)ids.insert(id);
            for(int id:ids)compareFields("terrain",id,at[id],bt[id]);
            std::ostringstream ar,br;a.repairDepots.save(ar);b.repairDepots.save(br);
            if(ar.str()!=br.str())diff<<"repairDepot,-1,fullSerializer,"<<q(ar.str())<<','<<q(br.str())<<'\n';
            ids.clear();for(const auto& [id,value]:a.behaviors)ids.insert(id);for(const auto& [id,value]:b.behaviors)ids.insert(id);
            for(int id:ids){const auto l=a.behaviors.find(id),r=b.behaviors.find(id);
                const std::string left=l==a.behaviors.end()?"<missing>":runtimeMultiplayerSnapshotBehaviorRecord(id,l->second);
                const std::string right=r==b.behaviors.end()?"<missing>":runtimeMultiplayerSnapshotBehaviorRecord(id,r->second);
                if(left!=right)diff<<"behavior,"<<id<<",canonicalRecord,"<<q(left)<<','<<q(right)<<'\n';}
            diff<<"summary,-1,tick,"<<a.tick<<','<<b.tick<<'\n'
                <<"summary,-1,worldHash,"<<a.world.deterministicSimulationStateHash()<<','<<b.world.deterministicSimulationStateHash()<<'\n'
                <<"summary,-1,repairHash,"<<a.repairDepots.stateHash()<<','<<b.repairDepots.stateHash()<<'\n'
                <<"summary,-1,specialHash,"<<a.specialMechanisms.stateHash()<<','<<b.specialMechanisms.stateHash()<<'\n';
            diff<<"summary,-1,ordinaryWorldContractHash,"<<ordinaryWorldContractHash(a.world)<<','<<ordinaryWorldContractHash(b.world)<<'\n';
        };
        const auto savePair=[&](RuntimeWindowState& s,const std::string& stage,
                std::unique_ptr<RuntimeWindowState>& ordinary,std::unique_ptr<RuntimeWindowState>& canonical){
            std::ostringstream baseText,extended,normalized;IniFile base,exact;
            if(!writeRuntimeStateToStream(s,baseText,error))return fail("complete14 writer "+error);
            const auto basePath=pathString(root/(label+"-"+stage+"-ordinary14.ini"));
            {std::ofstream f(basePath,std::ios::binary);f<<baseText.str();if(!f)return fail("ordinary physical write");}
            extended<<baseText.str();
            if(!writeRuntimeMultiplayerSnapshotExtension(s,extended,error)||!exact.loadFromString(extended.str())
                ||!writeRuntimeMultiplayerSnapshotCanonicalIni(exact,normalized,error))return fail("canonical writer "+error);
            const auto exactPath=pathString(root/(label+"-"+stage+"-canonical.ini"));
            {std::ofstream f(exactPath,std::ios::binary);f<<normalized.str();if(!f)return fail("canonical physical write");}
            bool allReadsOkay=true;std::string firstReadFailure;
            for(bool isCanonical:{false,true}){
                if(!base.loadFromFile(basePath)||(isCanonical&&!exact.loadFromFile(exactPath)))return fail("real full-file reopen");
                auto copy=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*copy,s,false);
                {RuntimeSnapshotActiveStateScope active(*copy);
                    if(!materializeRuntimeLoadStateFromIni(*copy,base,error)
                        ||(isCanonical&&!materializeRuntimeMultiplayerSnapshotExtension(*copy,exact,error)))return fail("full file materialization "+error);}
                copy->runtimeSoundsEnabled=copy->runtimePreferencePersistenceEnabled=copy->visionEnabled=false;
                initializeRuntimeOfflineGameplayRouting(*copy);
                const bool worldEqual=s.world.deterministicSimulationStateHash()==copy->world.deterministicSimulationStateHash();
                const bool worldContractEqual=isCanonical?worldEqual:ordinaryWorldContractHash(s.world)==ordinaryWorldContractHash(copy->world);
                const bool repairEqual=s.repairDepots.stateHash()==copy->repairDepots.stateHash();
                const bool specialEqual=s.specialMechanisms.stateHash()==copy->specialMechanisms.stateHash();
                const bool poseEqual=samePose(s,*copy);std::vector<std::uint32_t> a,b;
                const auto drawnA=draw(s,stage+"-source",false,a),drawnB=draw(*copy,stage+(isCanonical?"-canonical":"-ordinary14"),true,b);
                const bool pixelsEqual=drawnA&&drawnB&&a==b;const auto p=phase(*copy);
                const bool okay=authorityEqual(s,*copy,isCanonical)&&pixelsEqual;
                saves<<label<<','<<stage<<','<<(isCanonical?"canonical":"ordinary14")<<','<<std::quoted(isCanonical?exactPath:basePath)
                    <<','<<copy->tick<<','<<p.first<<','<<p.second<<','<<worldEqual<<','<<worldContractEqual<<','<<repairEqual<<','<<specialEqual<<','<<poseEqual<<','<<pixelsEqual<<','<<okay<<'\n';saves.flush();
                if(!worldEqual||!okay)dumpCheckpoint(s,*copy,stage,isCanonical?"canonical":"ordinary14");
                if(!okay){
                    allReadsOkay=false;
                    if(firstReadFailure.empty())firstReadFailure="physical restore changed world/service/animation/pose/pixels: "+stage+" "
                        +(isCanonical?"canonical":"ordinary14")+" world="+std::to_string(worldEqual)+" worldContract="+std::to_string(worldContractEqual)+" repair="+std::to_string(repairEqual)
                        +" special="+std::to_string(specialEqual)+" pose="+std::to_string(poseEqual)+" pixels="+std::to_string(pixelsEqual);
                }
                if(isCanonical){canonical=std::move(copy);++canonicalLoads;}else{ordinary=std::move(copy);++ordinaryLoads;}
            }
            return allReadsOkay || fail(firstReadFailure);
        };
        const auto run=[&](const Stock& item,int hz,bool damaged,const std::string& kind,bool save){
            label=item.yard+"-"+std::to_string(hz)+(damaged?"-damaged":"-healthy")+"-"+kind;
            auto box=make(item,hz,damaged,kind=="low-power");auto& s=*box;RuntimeSnapshotActiveStateScope live(s);
            const auto* originalUnit=assets->rules.findUnitType("UnitType",item.ship);
            if(!originalUnit||originalUnit->strength!=std::stoi(ri.getValue(item.ship,"Strength"))
                ||originalUnit->cost!=std::stoi(ri.getValue(item.ship,"Cost"))||!originalUnit->naval)
                return fail("original service vehicle Strength/Cost/Naval");
            const int maxHP=originalUnit->strength,initialHP=findObjectById(s,shipId)->health,initialCredits=s.economy.credits;
            const int interval=int(std::ceil(static_cast<long double>(repairMinutes)*60.0L*hz-1e-12L));
            const long double pricePerHP=static_cast<long double>(rawPercent)*originalUnit->cost/maxHP;
            const int stepCost=std::max(1,int(std::floor(pricePerHP*rawStep)));
            const int healedStep=std::min(rawStep,int(std::floor(static_cast<long double>(stepCost)/pricePerHP+1e-12L)));
            if(interval<=0||stepCost<=0||healedStep<=0)return fail("independent original paid step");
            resources<<label<<" inputShipHP="<<initialHP<<" maxHP="<<maxHP<<" rawCost="<<originalUnit->cost
                <<" pulseInterval="<<interval<<" pulseCost="<<stepCost<<" pulseHeal="<<healedStep
                <<" initialPower="<<s.economy.powerProduced<<'/'<<s.economy.powerUsed<<'\n';resources.flush();
            if(kind=="low-power"&&(s.economy.powerProduced>=s.economy.powerUsed
                ||findObjectById(s,yardId)->poweredOperational))return fail("actual deficit did not set the raw host power flag false");
            // This boundary is a constructed empty dock, before any service command.
            const auto freshShip=*findObjectById(s,shipId);
            std::erase_if(s.world.accessObjects(),[](const auto& object){return object.instanceId==shipId;});
            refreshRuntimeObjectOccupancyAfterLocalChange(s);
            for(int n=0;n<hz/2+1;++n)step(s);
            if(phase(s).first!=0||!s.repairDepots.relations().empty()
                ||!inspect(s,item,damaged,0,0,"no-ship",true,false))return fail("empty original yard emitted a repair crane");
            s.world.accessObjects().push_back(freshShip);refreshRuntimeObjectOccupancyAfterLocalChange(s);
            s.selectedObjectIds={shipId};const auto oldPosition=position(*findObjectById(s,shipId));
            const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                {20,20},findObjectById(s,yardId));
            if(!dispatched||!s.lastCommand.valid||s.lastCommand.verb!="unitRepair"
                ||s.lastCommand.acceptedCount!=1||s.lastCommand.rejectedCount!=0
                ||position(*findObjectById(s,shipId))!=oldPosition)return fail("actual Primary service command/acceptance/body discontinuity");
            std::unique_ptr<RuntimeWindowState> ordinary,canonical;
            std::set<std::string> rawReplayDumped;
            bool moved=false,docked=false,repaired=false,completed=false,stopIssued=false,workSaved=false,retractSaved=false;
            std::array<bool,4> seen{},sampled{};std::array<int,4> firstFrame{-1,-1,-1,-1};
            int previousPhase=0,phaseStart=-1,lastPulse=-1,scenePulses=0,stopHP=-1,stopCredits=-1;
            const auto replicas=[&](){std::vector<std::pair<const char*,RuntimeWindowState*>> list{{"live",&s}};
                if(ordinary)list.emplace_back("ordinary14",ordinary.get());if(canonical)list.emplace_back("canonical",canonical.get());return list;};
            for(int n=0;n<hz*180;++n){
                const auto beforePosition=position(*findObjectById(s,shipId));
                const int beforeHP=findObjectById(s,shipId)->health,beforeCredits=s.economy.credits;
                for(const auto& [name,replica]:replicas()){step(*replica);log(*replica,name);
                    if(replica!=&s){
                        if(s.world.deterministicSimulationStateHash()!=replica->world.deterministicSimulationStateHash()
                            &&rawReplayDumped.insert(name).second)dumpCheckpoint(s,*replica,"first-future-raw-world-difference",name);
                        if(!authorityEqual(s,*replica,std::string(name)=="canonical"))
                            return fail("actual continued full repair/pose/clock authority differs "+std::string(name)+" tick="+std::to_string(s.tick));
                        ++pairedTicks;}}
                const auto* ship=findObjectById(s,shipId);const auto current=phase(s);
                if(!ship||!ship->alive||ship->health<=0||ship->level!=0)return fail("original ship disappeared or was lifted during service");
                moved=moved||position(*ship)!=oldPosition;
                if(ship->dockedToObjectId==yardId){
                    docked=true;
                    if(ship->dockIndex!=0||ship->x!=23||ship->y!=21||!ship->hasRenderPosition||ship->renderMoveActive
                        ||std::abs(runtimeObjectRenderX(*ship)-23.5)>1e-8||std::abs(runtimeObjectRenderY(*ship)-21.5)>1e-8)
                        return fail("authored 384,-128,0 fractional dock pose");
                }
                if(ship->health!=beforeHP||s.economy.credits!=beforeCredits){
                    const int expectedHeal=std::min(maxHP-beforeHP,healedStep);
                    const bool okay=docked&&ship->health==beforeHP+expectedHeal&&s.economy.credits==beforeCredits-stepCost
                        &&(lastPulse<0||s.tick-lastPulse==interval)&&!stopIssued;
                    pulses<<label<<",live,"<<s.tick<<','<<beforeHP<<','<<ship->health<<','<<expectedHeal<<','
                        <<beforeCredits<<','<<s.economy.credits<<','<<stepCost<<','<<lastPulse<<','<<interval<<','<<okay<<'\n';pulses.flush();
                    if(!okay)return fail("original real paid service pulse/clock/cost changed");
                    lastPulse=s.tick;++scenePulses;++paidPulses;repaired=true;
                }
                if(kind=="low-power"&&(findObjectById(s,yardId)->poweredOperational
                    ||s.economy.powerProduced>=s.economy.powerUsed))return fail("actual deficit vanished or the raw host power flag became true");
                // Keep the unmodified baseline physically useful: first establish
                // actual repair/credits, then capture and report its missing layer.
                if(repaired&&!seen[1]&&current.first==0){
                    states.flush();std::vector<std::uint32_t> actual;draw(s,"paid-repair-missing-animation",true,actual);
                    return inspect(s,item,damaged,1,0,"paid-repair-expected-extend",false,false);
                }
                if(current.first!=previousPhase){
                    if(previousPhase==1&&current.first==2){const int span=damaged?item.damagedEnd[0]-item.damagedStart[0]:item.end[0]-item.start[0];
                        if(s.tick-phaseStart!=span*((4*hz+14)/15))return fail("original exclusive extend duration");}
                    if(previousPhase==3&&current.first==0){const int span=damaged?item.damagedEnd[2]-item.damagedStart[2]:item.end[2]-item.start[2];
                        if(s.tick-phaseStart!=span*((4*hz+14)/15))return fail("original exclusive retract duration");}
                    if(current.first>0){
                        if((!previousPhase&&current.first!=1)||(previousPhase==1&&current.first!=2)
                            ||(previousPhase==2&&current.first!=3))return fail("repair crane phase order");
                        phaseStart=s.tick;seen[current.first]=true;
                        if(current.second!=0||!inspect(s,item,damaged,current.first,0,"phase-"+std::to_string(current.first)+"-first",true,false))return false;
                        firstFrame[current.first]=damaged?item.damagedStart[current.first-1]:item.start[current.first-1];
                    }
                    previousPhase=current.first;
                }
                if(current.first>0){
                    if(current.second!=s.tick-phaseStart)return fail("original phase age differs from actual simulation elapsed ticks");
                    const int delay=(4*hz+14)/15;
                    if(!sampled[current.first]&&current.second>=delay*2){
                        if(!inspect(s,item,damaged,current.first,current.second,"phase-"+std::to_string(current.first)+"-moving",true,true))return false;
                        sampled[current.first]=true;
                        if(save&&current.first==2&&!workSaved){if(!savePair(s,"working",ordinary,canonical))return false;workSaved=true;}
                        if(save&&current.first==3&&!retractSaved){if(!savePair(s,"retracting",ordinary,canonical))return false;retractSaved=true;}
                    }
                }
                if(kind=="stop"&&!stopIssued&&repaired&&current.first==2&&sampled[2]){
                    const auto p=position(*ship);const int commandTick=s.tick;
                    s.selectedObjectIds={shipId};issueRuntimeStopCommand(s);
                    if(!s.lastCommand.valid||s.lastCommand.acceptedCount!=1||s.tick!=commandTick
                        ||position(*findObjectById(s,shipId))!=p
                        ||findRuntimeRepairDepotRelationForUnit(s.repairDepots,shipId))return fail("actual Stop failed to release service or moved the body same tick");
                    // releaseUnit changes the existing working crane immediately,
                    // before the next full tick. Observe that genuine command
                    // boundary, then retain the ordinary elapsed-age oracle.
                    const auto stoppedPhase=phase(s);log(s,"Stop-command-immediate");
                    if(stoppedPhase!=std::pair<int,int>{3,0})return fail("Stop did not immediately enter original retract age0: phase="
                        +std::to_string(stoppedPhase.first)+" age="+std::to_string(stoppedPhase.second));
                    if(!inspect(s,item,damaged,3,0,"Stop-immediate-retract",true,false))return false;
                    phaseStart=s.tick;previousPhase=3;seen[3]=true;
                    firstFrame[3]=damaged?item.damagedStart[2]:item.start[2];
                    stopIssued=true;stopHP=findObjectById(s,shipId)->health;stopCredits=s.economy.credits;
                }
                if(stopIssued&&(ship->health!=stopHP||s.economy.credits!=stopCredits))return fail("Stop left a paid repair task alive");
                if((stopIssued||(docked&&ship->health==maxHP))&&current.first==0
                    &&!findRuntimeRepairDepotRelationForUnit(s.repairDepots,shipId)&&ship->dockedToObjectId<0){
                    completed=true;break;
                }
                (void)beforePosition;
            }
            states.flush();
            if(!moved||!docked||!repaired||!completed||scenePulses<=0||!seen[1]||!seen[2]||!seen[3]
                ||!sampled[1]||!sampled[2]||!sampled[3]||(kind=="stop"&&!stopIssued)
                ||(save&&(!workSaved||!retractSaved)))return fail("actual route/paid repair/three moving original phases/complete save census incomplete");
            if(!inspect(s,item,damaged,0,0,"idle-after",true,false))return false;
            const int settledHealth=findObjectById(s,shipId)->health,settledCredits=s.economy.credits;
            if(kind!="stop"){
                s.selectedObjectIds={shipId};
                const bool accepted=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,{20,20},findObjectById(s,yardId));
                if(accepted||s.lastCommand.acceptedCount!=0||findRuntimeRepairDepotRelationForUnit(s.repairDepots,shipId))
                    return fail("full-health ship accepted a new paid repair service");
            }
            // Dispatch the same genuine full-health request on existing replicas
            // so no artificial difference in input history enters replay.
            for(auto* peer:{ordinary.get(),canonical.get()})if(peer&&kind!="stop"){
                RuntimeSnapshotActiveStateScope active(*peer);peer->selectedObjectIds={shipId};
                if(dispatchRuntimeTargetOrderFromUi(*peer,RuntimeMultiplayerGameplayTargetOrderKind::Primary,{20,20},findObjectById(*peer,yardId))
                    ||peer->lastCommand.acceptedCount!=0)return fail("restored full-health service acceptance differs");}
            for(int n=0;n<hz*2;++n)for(const auto& [name,replica]:replicas()){step(*replica);
                if(phase(*replica).first!=0||findObjectById(*replica,shipId)->health!=settledHealth
                    ||replica->economy.credits!=settledCredits||findRuntimeRepairDepotRelationForUnit(replica->repairDepots,shipId))
                    return fail("full/Stop idle restarted a crane or paid service");
                if(replica!=&s){if(!authorityEqual(s,*replica,std::string(name)=="canonical"))return fail("idle replay authority differs");++pairedTicks;}}
            if(!inspect(s,item,damaged,0,0,"idle-settled",false,false))return false;
            checks<<label<<','<<kind<<','<<hz<<','<<damaged<<",1,"<<moved<<','<<docked<<','<<scenePulses<<','<<seen[1]<<','
                <<seen[2]<<','<<seen[3]<<','<<completed<<",1\n";checks.flush();
            ++fixtures;if(kind=="core")++cores;else ++controls;
            std::cout<<"Shipyard original repair "<<label<<" real-ui/dock/paid-pulse/extend-work-retract/original-pixels/save=PASS\n";
            return true;
        };
        label="GAYARD-30-healthy-core";
        if(!run(stock[0],30,false,"core",true))return false;
        for(int index=0;index<3;++index)for(bool damaged:{false,true}){
            if(index==0&&!damaged)continue;
            if(!run(stock[index],30,damaged,"core",index==2&&damaged))return false;
        }
        for(int hz:{15,60})if(!run(stock[0],hz,false,"core",false))return false;
        for(const auto& item:stock)if(!run(item,30,false,"stop",false))return false;
        for(const auto& item:stock)if(!run(item,30,false,"low-power",false))return false;
        if(fixtures!=14||cores!=8||controls!=6||ordinaryLoads!=4||canonicalLoads!=4
            ||pairedTicks<=0||paidPulses<=0||pixelWitnesses<=0||changedWitnesses<=0||bmps<=0)return fail("final focused census");
        std::ofstream result(root/"result.txt");result<<"PASS\nscenes="<<fixtures<<"\ncore="<<cores<<"\ncontrols="<<controls
            <<"\nordinary14_unique_files="<<ordinaryLoads<<"\ncanonical_unique_files="<<canonicalLoads
            <<"\nfull_materializations="<<ordinaryLoads+canonicalLoads<<"\nfile_reopen_actions="<<ordinaryLoads+canonicalLoads*2
            <<"\npaired_ticks="<<pairedTicks<<"\npaid_pulses="<<paidPulses<<"\noriginal_visible_pixels="<<pixelWitnesses
            <<"\noriginal_changed_pixels="<<changedWitnesses<<"\nactual_BMPs="<<bmps<<"\n";
        std::cout<<"Shipyard repair animation: "<<fixtures<<" scenes / "<<cores<<" core / "<<controls
            <<" controls; "<<ordinaryLoads+canonicalLoads<<" complete physical loads, "<<pairedTicks<<" paired ticks; original crane PASS\n";
        return true;
    } catch(const std::exception& e){return fail(e.what());}
}
#pragma optimize("", on)