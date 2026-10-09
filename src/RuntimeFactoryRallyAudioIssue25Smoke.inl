// Issue25.14: genuine sidebar production, factory rally consumption and audio.
// Only the physical output device is disabled; production, commands, emit,
// resolution and captured playback records all use the actual runtime paths.
#pragma optimize("", off)
bool runRuntimeFactoryRallyAudioIssue25Smoke(const RuntimeConfig& config,std::string& error) {
    std::cout<<std::unitbuf;
    const char* output=std::getenv("CONCORD_FACTORY_RALLY_AUDIO_OUTPUT");
    const std::filesystem::path root=output&&*output?output:"build/smoke/feature-set-25/14-factory-rally-audio";
    std::filesystem::create_directories(root);std::string label,stage;
    const auto fail=[&](const std::string& why){error="Issue25 factory rally audio "+label+" ["+stage+"]: "+why;std::cout<<error<<'\n';return false;};
    const auto quote=[](const std::string& v){std::string r="\"";for(char c:v){if(c=='"')r+='"';r+=c;}return r+'"';};
    try {
        auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
        assets->config.playerSlots.clear();assets->config.gameModeRulesOverride.clear();
        assets->report=buildRuntimeStartupReport(assets->config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
        if(!assets->report.success)return fail("original startup: "+assets->report.errorMessage);
        assets->rulesLoaded=assets->artLoaded=assets->theaterLoaded=true;
        IniFile original;if(!original.loadFromFile(assets->report.rulesPath))return fail("original rules");
        std::ofstream sources(root/"sources.txt"),cases(root/"cases.csv"),events(root/"events.csv"),commands(root/"commands.csv"),
            phases(root/"production.csv"),checks(root/"checkpoints.csv"),metadataOut(root/"original-fields.csv");
        if(!sources||!cases||!events||!commands||!phases||!checks||!metadataOut)return fail("evidence streams");
        sources<<"rules="<<assets->report.rulesPath<<"\nart="<<assets->report.artPath
            <<"\nEight live original sidebar production scenes, not direct completion calls. 30Hz; existing noCooldown configuration accelerates the paid producer.\n"
            <<"No rule/HP/Speed/build-time/CreateSound/VoiceMove/MoveSound overrides. Real emit capture; runtimeSoundsEnabled=false only disables device playback.\n"
            <<"CreateSound is the original field, not VoiceCreated. Seven original fields are compared with loaded values; four named special units are actually produced.\n"
            <<"Manual same-rally Move, append, Stop and replacement use actual UI dispatch. Selection voices are captured separately from Move feedback.\n"
            <<"Two unique complete physical files, ordinary14 and canonical; canonical reopens the ordinary base before its extension (three reopen operations).\n"
            <<"Initial restore keeps exact queue/lease/paid/physical actor authority; canonical continues with exact raw World and full behavior each tick.\n"
            <<"Ordinary14 may re-plan its unsaved path, and independently must actually finish the same saved rally with original owner/HP/cost and no automatic voice. It is not claimed to be path-exact future replay.\n"
            <<"Copy-only ordinary World comparison ignores XYZ cache only when both hasRenderPosition and renderMoveActive are false; actual states and canonical raw hashes are unchanged.\n";
        cases<<"case,factory,type,owner,rally,produced_id,queue_tick,birth_tick,rally_consume_tick,finish_tick,credits_before,credits_after,original_cost,auto_voice,move_sound,create_sound,unit_ready,manual_voice,passed\n";
        events<<"case,replica,scope,tick,order,intent,section,sample,path,status,semantic,eva,playable,played,suppressed,spatial\n";
        commands<<"case,replica,scope,tick,dispatched,valid,verb,accepted,rejected,id,goal_x,goal_y,queue_count,voice_delta,move_sound_delta\n";
        phases<<"case,replica,tick,id,x,y,render_x,render_y,render_level,moving,mission,behavior_kind,behavior_state,goal_x,goal_y,path_size,exit_lease,queue_count,voice_count,create_count,credits\n";
        checks<<"case,format,tick,queue_equal,world_raw_equal,world_contract_equal,special_equal,pose_equal,behavior_equal,production_equal,file,passed\n";
        metadataOut<<"type,kind,raw_create_sound,loaded_create_sound,raw_voice_move,loaded_voice_move,raw_move_sound,loaded_move_sound\n";
        for(const auto& entry:std::array<std::pair<const char*,const char*>,7>{{
            {"TANY","InfantryType"},{"BORIS","InfantryType"},{"ZEP","UnitType"},{"DISK","UnitType"},
            {"YURIPR","InfantryType"},{"CCOMAND","InfantryType"},{"DTRUCK","UnitType"}}}) {
            const auto* u=assets->rules.findUnitType(entry.second,entry.first);
            if(!u||original.getValue(entry.first,"CreateSound").empty()||u->createSound!=original.getValue(entry.first,"CreateSound")
                ||!original.getValue(entry.first,"VoiceCreated").empty())return fail(std::string("original generic CreateSound field ")+entry.first);
            metadataOut<<entry.first<<','<<entry.second<<','<<quote(original.getValue(entry.first,"CreateSound"))<<','<<quote(u->createSound)
                <<','<<quote(original.getValue(entry.first,"VoiceMove"))<<','<<quote(u->voiceMove)
                <<','<<quote(original.getValue(entry.first,"MoveSound"))<<','<<quote(u->moveSound)<<'\n';
        }
        int landTile=-1,waterTile=-1;const auto metadata=runtimeTerrainMetadataContext(*assets);
        const int waterSet=assets->theater.getGeneralTileSetIndex("WaterSet");
        const auto* waterLand=assets->rules.findLandType("Water");
        if(waterSet<0||!waterLand||waterLand->floatSpeed!=100.0)return fail("native WaterSet/Float100");
        for(int tile=0;tile<int(assets->theater.getTotalTileSlots());++tile) {
            TerrainCell c;c.tileIndex=tile;c.subTileIndex=0;const auto info=runtimeTerrainInfoForCell(*assets,c);
            if(landTile<0&&info.passable&&!info.water&&!info.shore&&!info.cliff&&!info.bridge
                &&resolveRuntimeTerrainSubTileRampType(c,metadata,assets->renderCache)==0)landTile=tile;
            if(waterTile<0&&assets->theater.resolveTileIndex(tile).tileSetIndex==waterSet
                &&resolveRuntimeTerrainSubTileLandType(c,metadata,assets->renderCache)==RuntimeTerrainLandType::Water
                &&resolveRuntimeTerrainSubTileRampType(c,metadata,assets->renderCache)==0)waterTile=tile;
            if(landTile>=0&&waterTile>=0)break;
        }
        if(landTile<0||waterTile<0)return fail("native clear/water TMP land="+std::to_string(landTile)+" water="+std::to_string(waterTile));
        sources<<"landTile="<<landTile<<"\nwaterTile="<<waterTile<<"\nWaterSet="<<waterSet<<"\nNative map indexing is y*512+x; navigation lookup uses runtimeCellKey1000.\n";
        struct Case {const char* factory;const char* unit;const char* owner;bool rally;bool save;bool controls;};
        const std::array<Case,8> matrix{{
            {"GAPILE","E1","Americans",true,false,true},{"GAWEAP","MTNK","Americans",true,true,true},
            {"GAYARD","DEST","Americans",true,false,true},{"GAPILE","E1","Americans",false,false,false},
            {"GAPILE","TANY","Americans",true,false,false},{"NAHAND","BORIS","Russians",true,false,false},
            {"NAWEAP","ZEP","Russians",true,false,false},{"YAWEAP","DISK","YuriCountry",true,false,false}}};
        const auto add=[&](RuntimeWindowState& s,int id,const std::string& type,RuntimeCellPoint cell) {
            WorldObjectInstance o;o.instanceId=id;o.mapId="Issue14-"+std::to_string(id);o.type=type;o.kind=WorldObjectKind::Structure;
            o.owner=s.economy.owner;o.x=cell.x;o.y=cell.y;o.cell=runtimeCellKey(cell.x,cell.y);
            o.health=runtimeMaxHealthForObject(s,o);o.alive=o.poweredOperational=true;o.mission="Guard";
            o.buildAnimTicksTotal=o.buildAnimTicksRemaining=0;clearRuntimeRenderPosition(o);
            s.world.accessObjects().push_back(std::move(o));
        };
        const auto make=[&](const Case& c) {
            auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;seedRuntimeLoadScratch(s,*assets,false);
            s.config.fixedTickHz=30;s.config.playerCountry=c.owner;s.config.techLevel=10;s.config.startingUnits=0;
            s.config.playerSlots.clear();s.config.gameModeRulesOverride.clear();s.config.skirmishLoadSavePath.clear();
            s.config.multiplayerActive=s.config.computerAiEnabled=s.config.shortGame=s.config.crates=s.config.superweapons=false;
            s.config.gameMode="Battle";s.config.noCooldown=true;s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;
            s.world=assets->world;s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
            s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
            s.mapFacts={};s.mapFacts.theater=assets->mapFacts.theater;s.mapFacts.lighting=assets->mapFacts.lighting;s.mapFactsLoaded=true;
            s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=s.report.map.height=-1;
            s.behaviors.clear();s.projectiles.clear();s.recentExpiredProjectiles.clear();s.playerCommands={};
            s.economy={};s.economy.owner=c.owner;s.economy.credits=100000;s.economy.techLevel=10;s.economy.skirmishStartApplied=true;
            s.triggers={};s.triggers.bootstrapComplete=true;s.initialOreFieldsSeeded=true;
            s.computerAiByOwner.clear();s.computerMiningByOwner.clear();s.skirmishCountryByOwner.clear();s.skirmishTeamByOwner.clear();
            s.skirmishCountryByOwner[toLowerAsciiLocal(c.owner)]=c.owner;s.humanHouseAuthorities.clear();
            RuntimeHumanHouseAuthority house;house.owner=c.owner;house.canonicalOwner=toLowerAsciiLocal(c.owner);house.country=c.owner;house.localFacade=true;
            s.humanHouseAuthorities.push_back(house);s.visionEnabled=false;
            s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.runtimeSoundEffectsEnabled=true;s.runtimeSoundVolume=100;
            s.runtimeSuppressPlayerAudioFeedback=false;
            for(int y=40;y<=96;++y)for(int x=40;x<=108;++x) {
                const bool water=x<60;TerrainCell t;t.x=x;t.y=y;t.cellIndex512=y*512+x;t.tileIndex=water?waterTile:landTile;t.subTileIndex=0;
                s.world.accessTerrainCells().push_back(t);MapCellFacts f;f.x=x;f.y=y;f.cellIndex=t.cellIndex512;f.hasTerrain=true;
                f.terrain.x=x;f.terrain.y=y;f.terrain.tileIndex=t.tileIndex;f.terrain.subTileIndex=0;
                f.isWater=water;f.terrainClass=water?"water":"clear";f.passabilityHint="passable";
                s.mapFacts.cellIndexLookup.emplace(f.cellIndex,s.mapFacts.cells.size());s.mapFacts.cells.push_back(f);
            }
            const bool naval=std::string(c.factory)=="GAYARD";
            add(s,1,c.factory,naval?RuntimeCellPoint{46,64}:RuntimeCellPoint{68,64});
            std::vector<std::string> prerequisites;
            std::string power;
            if(std::string(c.owner)=="Americans"){prerequisites={"GACNST","GAREFN","GAPILE","GAWEAP","GAAIRC","GATECH"};power="GAPOWR";}
            else if(std::string(c.owner)=="Russians"){prerequisites={"NACNST","NAREFN","NAHAND","NAWEAP","NARADR","NATECH"};power="NAPOWR";}
            else {prerequisites={"YACNST","YAREFN","YABRCK","YAWEAP","YAPSIS","YATECH"};power="YAPOWR";}
            int id=10,pos=0;
            for(const auto& type:prerequisites)if(type!=c.factory){add(s,id++,type,{82+(pos%3)*8,48+(pos/3)*8});++pos;}
            for(int n=0;n<8;++n)add(s,100+n,power,{70+(n%4)*7,82+(n/4)*7});
            s.terrainByCell=buildRuntimeTerrainLookup(s.world);
            initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);initializeRuntimeTriggers(s);
            s.economy.owner=c.owner;s.economy.credits=100000;s.economy.techLevel=10;s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
            initializeRuntimeLocalProductionLanes(s.economy);ensureRuntimeSubcellsInitialized(s);ensureRuntimeSpecialMechanismsInitialized(s);
            ensureRuntimeRepairDepotsInitialized(s);ensureRuntimeLightingInitialized(s);ensureRuntimeParticlesInitialized(s);
            initializeRuntimeOfflineGameplayRouting(s);loadRuntimeSoundDatabase(s);
            {RuntimeSnapshotActiveStateScope active(s);advanceRuntimeSpecialMechanisms(s);completeRuntimeWorldRefresh(s);rebuildRuntimeEconomyPower(s);}
            return box;
        };
        struct Observation {
            std::vector<RuntimeSoundPlaybackRecord> records;std::size_t written=0;
            int loopWitnesses=0;int lastLoopOrdinal=0;
        };
        const auto capture=[&](RuntimeWindowState& s,Observation& o,const auto& body) {
            auto* previous=s.runtimeSoundPlaybackCapture;s.runtimeSoundPlaybackCapture=&o.records;
            struct Restore {RuntimeWindowState& s;std::vector<RuntimeSoundPlaybackRecord>* p;~Restore(){s.runtimeSoundPlaybackCapture=p;}} restore{s,previous};
            RuntimeSnapshotActiveStateScope active(s);body();
        };
        const auto flush=[&](Observation& o,const char* replica,const std::string& scope) {
            while(o.written<o.records.size()){const auto& r=o.records[o.written++];
                events<<label<<','<<replica<<','<<scope<<','<<r.tick<<','<<r.playbackOrder<<','<<quote(r.intent)<<','<<quote(r.section)
                    <<','<<quote(r.sample)<<','<<quote(r.path)<<','<<quote(r.status)<<','<<r.semantic<<','<<r.eva<<','<<r.playable
                    <<','<<r.played<<','<<r.suppressed<<','<<r.spatial<<'\n';}
            events.flush();
        };
        const auto voiceCount=[&](const Observation& o,const UnitType& u) {
            const auto tokens=splitRuntimeTokenList(u.voiceMove);int n=0;
            for(const auto& r:o.records)if(r.intent=="command move"
                ||std::find(tokens.begin(),tokens.end(),r.intent)!=tokens.end())++n;
            return n;
        };
        const auto soundCount=[](const Observation& o,const std::string& id) {
            return int(std::count_if(o.records.begin(),o.records.end(),[&](const auto& r){return r.intent=="sound:"+id;}));
        };
        const auto readyCount=[](const Observation& o) {
            return int(std::count_if(o.records.begin(),o.records.end(),[](const auto& r){return r.intent==runtimeSoundIntentUnitReady;}));
        };
        const auto playableSound=[&](const Observation& o,const std::string& id) {
            return std::any_of(o.records.begin(),o.records.end(),[&](const auto& r){
                return r.intent=="sound:"+id&&r.section==id&&r.playable&&!r.sample.empty()&&!r.path.empty();});
        };
        const auto playableVoice=[&](const Observation& o,const UnitType& u,std::size_t from) {
            const auto tokens=splitRuntimeTokenList(u.voiceMove);
            for(std::size_t i=from;i<o.records.size();++i){const auto& r=o.records[i];
                if(std::find(tokens.begin(),tokens.end(),r.intent)!=tokens.end()&&r.playable
                    &&!r.eva&&!r.sample.empty()&&!r.path.empty())return true;}
            return false;
        };
        const auto queued=[](const RuntimeWindowState& s,int id) {
            const auto q=s.playerCommands.queues.find(id);return q==s.playerCommands.queues.end()?0:int(q->second.orders.size());
        };
        const auto step=[&](RuntimeWindowState& s,Observation& o,int id,const UnitType& u,const char* replica) {
            capture(s,o,[&]{++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);
                if(runtimeDiskMoveSoundIsLoop(s,u))advanceRuntimeDiskMoveAudio(s,std::uint64_t(s.tick)*1000/std::max(1,s.config.fixedTickHz));});
            if(id>=0&&s.diskMoveAudio.sources.contains(id)&&s.diskMoveAudio.sources.at(id).active) {
                ++o.loopWitnesses;o.lastLoopOrdinal=int(s.diskMoveAudio.sources.at(id).ordinal);
            }
            flush(o,replica,"simulation");
        };
        const auto pose=[](const WorldObjectInstance& a) {
            std::ostringstream out;out<<std::setprecision(17)<<a.instanceId<<'|'<<a.type<<'|'<<a.owner<<'|'<<a.x<<'|'<<a.y
                <<'|'<<a.level<<'|'<<runtimeObjectRenderX(a)<<'|'<<runtimeObjectRenderY(a)<<'|'<<runtimeDrawLevelForObject(a)
                <<'|'<<a.renderMoveActive<<'|'<<a.facing<<'|'<<a.health<<'|'<<a.mission<<'|'<<a.subCell;
            return out.str();
        };
        const auto log=[&](const RuntimeWindowState& s,const Observation& o,int id,const UnitType& u,const char* replica) {
            const auto* a=findObjectById(s,id);if(!a)return;
            const auto b=s.behaviors.find(id);
            phases<<label<<','<<replica<<','<<s.tick<<','<<id<<','<<a->x<<','<<a->y<<','<<std::setprecision(17)
                <<runtimeObjectRenderX(*a)<<','<<runtimeObjectRenderY(*a)<<','<<runtimeDrawLevelForObject(*a)<<','<<a->renderMoveActive
                <<','<<quote(a->mission)<<','<<(b==s.behaviors.end()?-1:int(b->second.kind))<<','<<(b==s.behaviors.end()?-1:int(b->second.state))
                <<','<<(b==s.behaviors.end()?-1:b->second.moveTarget.x)<<','<<(b==s.behaviors.end()?-1:b->second.moveTarget.y)
                <<','<<(b==s.behaviors.end()?0:b->second.path.size())<<','<<runtimeFactoryDeliveryOwnsEgress(s,id)<<','<<queued(s,id)
                <<','<<voiceCount(o,u)<<','<<soundCount(o,u.createSound)<<','<<s.economy.credits<<'\n';phases.flush();
        };
        const auto diagnostic=[&](const RuntimeWindowState& s,int id) {
            std::ostringstream out;out<<"tick="<<s.tick<<" id="<<id<<" queue="<<queued(s,id)<<" credits="<<s.economy.credits;
            if(const auto* a=findObjectById(s,id))out<<" pose="<<pose(*a);
            if(const auto b=s.behaviors.find(id);b!=s.behaviors.end())out<<" behavior="<<runtimeMultiplayerSnapshotBehaviorRecord(id,b->second);
            return out.str();
        };
        const auto worldContract=[](const GameWorld& value) {
            GameWorld copy=value;for(auto& o:copy.accessObjects())if(!o.hasRenderPosition&&!o.renderMoveActive) {
                o.renderX=double(o.x);o.renderY=double(o.y);o.renderLevel=double(o.level);
            }
            return copy.deterministicSimulationStateHash();
        };
        const auto behaviorsEqual=[](const RuntimeWindowState& a,const RuntimeWindowState& b,bool exact) {
            if(a.behaviors.size()!=b.behaviors.size())return false;
            for(const auto& [id,m]:a.behaviors){const auto j=b.behaviors.find(id);if(j==b.behaviors.end())return false;const auto& n=j->second;
                if(exact){if(runtimeMultiplayerSnapshotBehaviorRecord(id,m)!=runtimeMultiplayerSnapshotBehaviorRecord(id,n))return false;}
                else if(m.kind!=n.kind||m.targetObjectId!=n.targetObjectId||m.moveTarget!=n.moveTarget
                    ||m.moveCooldownTicks!=n.moveCooldownTicks||m.weaponCooldownTicks!=n.weaponCooldownTicks
                    ||m.renderFrom!=n.renderFrom||m.renderTo!=n.renderTo||m.renderMoveTicksTotal!=n.renderMoveTicksTotal
                    ||m.renderMoveTicksRemaining!=n.renderMoveTicksRemaining||m.renderFromOffsetX!=n.renderFromOffsetX
                    ||m.renderFromOffsetY!=n.renderFromOffsetY||m.renderToOffsetX!=n.renderToOffsetX||m.renderToOffsetY!=n.renderToOffsetY
                    ||m.renderArrivalDistanceAfter!=n.renderArrivalDistanceAfter||m.factoryExitClearanceHostId!=n.factoryExitClearanceHostId)return false;
            }return true;
        };
        int scenes=0,produced=0,manualChecks=0,appendChecks=0,stopChecks=0,replaceChecks=0,ownerChecks=0,pairedTicks=0,physicalFiles=0,reopens=0,createChecks=0;
        const auto equal=[&](const RuntimeWindowState& s,const RuntimeWindowState& other,int id,bool exact) {
            const auto* a=findObjectById(s,id);const auto* b=findObjectById(other,id);
            return s.tick==other.tick&&a&&b&&pose(*a)==pose(*b)&&s.playerCommands.serialize()==other.playerCommands.serialize()
                &&s.economy.credits==other.economy.credits&&s.factoryDeliveries.stateHash()==other.factoryDeliveries.stateHash()
                &&runtimeProductionLanesStateHash(s.economy.localProductionLanes)==runtimeProductionLanesStateHash(other.economy.localProductionLanes)
                &&s.specialMechanisms.stateHash()==other.specialMechanisms.stateHash()&&behaviorsEqual(s,other,exact)
                &&(exact?s.world.deterministicSimulationStateHash()==other.world.deterministicSimulationStateHash():worldContract(s.world)==worldContract(other.world));
        };
        const auto checkpoint=[&](RuntimeWindowState& s,int id,bool exact,Observation& audio) {
            std::ostringstream baseText;RuntimeSnapshotActiveStateScope active(s);
            if(!writeRuntimeStateToStream(s,baseText,error))return std::unique_ptr<RuntimeWindowState>{};
            const auto basePath=root/(label+"-pending-rally-state14.ini");
            if(!exact){std::ofstream file(basePath,std::ios::binary);file<<baseText.str();file.close();++physicalFiles;}
            std::filesystem::path selected=basePath;
            if(exact){std::ostringstream all,canonical;all<<baseText.str();IniFile ini;
                if(!writeRuntimeMultiplayerSnapshotExtension(s,all,error)||!ini.loadFromString(all.str())
                    ||!writeRuntimeMultiplayerSnapshotCanonicalIni(ini,canonical,error))return std::unique_ptr<RuntimeWindowState>{};
                selected=root/(label+"-pending-rally-canonical.ini");std::ofstream file(selected,std::ios::binary);file<<canonical.str();file.close();++physicalFiles;}
            auto copy=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*copy,s,false);copy->runtimeSoundPlaybackCapture=&audio.records;
            IniFile base;if(!base.loadFromFile(pathString(basePath)))return std::unique_ptr<RuntimeWindowState>{};++reopens;
            if(!materializeRuntimeLoadStateFromIni(*copy,base,error))return std::unique_ptr<RuntimeWindowState>{};
            if(exact){IniFile ini;if(!ini.loadFromFile(pathString(selected)))return std::unique_ptr<RuntimeWindowState>{};++reopens;
                if(!materializeRuntimeMultiplayerSnapshotExtension(*copy,ini,error))return std::unique_ptr<RuntimeWindowState>{};}
            copy->runtimeSoundPlaybackCapture=nullptr;copy->runtimeSoundsEnabled=copy->runtimePreferencePersistenceEnabled=false;copy->runtimeSuppressPlayerAudioFeedback=false;
            initializeRuntimeOfflineGameplayRouting(*copy);
            const auto* a=findObjectById(s,id);const auto* b=findObjectById(*copy,id);
            const bool q=s.playerCommands.serialize()==copy->playerCommands.serialize(),raw=s.world.deterministicSimulationStateHash()==copy->world.deterministicSimulationStateHash();
            const bool contract=worldContract(s.world)==worldContract(copy->world),sp=s.specialMechanisms.stateHash()==copy->specialMechanisms.stateHash();
            const bool po=a&&b&&pose(*a)==pose(*b),be=behaviorsEqual(s,*copy,exact);
            const bool prod=s.factoryDeliveries.stateHash()==copy->factoryDeliveries.stateHash()
                &&runtimeProductionLanesStateHash(s.economy.localProductionLanes)==runtimeProductionLanesStateHash(copy->economy.localProductionLanes);
            const bool ok=equal(s,*copy,id,exact);
            checks<<label<<','<<(exact?"canonical":"ordinary14")<<','<<s.tick<<','<<q<<','<<raw<<','<<contract<<','<<sp<<','<<po<<','<<be<<','<<prod<<','<<quote(pathString(selected))<<','<<ok<<'\n';checks.flush();
            if(!ok){std::ostringstream source,peer;std::string ignored;writeRuntimeStateToStream(s,source,ignored);writeRuntimeStateToStream(*copy,peer,ignored);
                std::ofstream(root/(label+"-"+(exact?"canonical":"ordinary14")+"-source.txt"))<<source.str();
                std::ofstream(root/(label+"-"+(exact?"canonical":"ordinary14")+"-peer.txt"))<<peer.str();
                fail(std::string(exact?"canonical":"ordinary14")+" pending factory exit/rally restore changed authority: source "+diagnostic(s,id)+" peer "+diagnostic(*copy,id));
                return std::unique_ptr<RuntimeWindowState>{};}
            return copy;
        };
        const auto settled=[&](const RuntimeWindowState& s,int id,std::optional<RuntimeCellPoint> goal) {
            const auto* a=findObjectById(s,id);if(!a||a->renderMoveActive||runtimeFactoryDeliveryOwnsEgress(s,id)||queued(s,id))return false;
            const auto b=s.behaviors.find(id);
            if(b!=s.behaviors.end()&&b->second.state!=RuntimeBehaviorState::Complete&&b->second.state!=RuntimeBehaviorState::Idle&&b->second.kind!=RuntimeBehaviorKind::None)return false;
            return !goal||(a->x==goal->x&&a->y==goal->y);
        };
        const auto manualMove=[&](RuntimeWindowState& s,Observation& o,const UnitType& u,int id,RuntimeCellPoint goal,const std::string& scope,bool append) {
            capture(s,o,[&]{applySelection(s,{id},false,false);});flush(o,"live",scope+"-selection");
            const int before=voiceCount(o,u),soundBefore=soundCount(o,u.moveSound);const auto start=o.records.size();bool dispatched=false;
            capture(s,o,[&]{s.playerLocalCommands.planning=append;
                dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,goal);
                s.playerLocalCommands.planning=false;});
            const int delta=voiceCount(o,u)-before,sfx=soundCount(o,u.moveSound)-soundBefore;
            commands<<label<<",live,"<<scope<<','<<s.tick<<','<<dispatched<<','<<s.lastCommand.valid<<','<<quote(s.lastCommand.verb)
                <<','<<s.lastCommand.acceptedCount<<','<<s.lastCommand.rejectedCount<<','<<id<<','<<goal.x<<','<<goal.y
                <<','<<queued(s,id)<<','<<delta<<','<<sfx<<'\n';commands.flush();flush(o,"live",scope);
            if(!dispatched)return fail("actual manual Move rejected "+diagnostic(s,id));
            if(append){if(queued(s,id)!=1||delta!=0)return fail("actual append executed prematurely");++appendChecks;}
            else {
                if(!s.lastCommand.valid||s.lastCommand.acceptedCount!=1||s.lastCommand.rejectedCount!=0||delta!=1||!playableVoice(o,u,start))
                    return fail("actual manual Move did not retain one resolved original VoiceMove "+diagnostic(s,id));
                if(!u.moveSound.empty()&&!runtimeDiskMoveSoundIsLoop(s,u)&&sfx!=1)return fail("manual MoveSound changed");
                ++manualChecks;
            }
            return true;
        };
        for(const auto& c:matrix) {
            label=std::string(c.factory)+"-"+c.unit+(c.rally?"-rally":"-no-rally");stage="original fixture";
            auto box=make(c);auto& s=*box;Observation observed;
            const auto* u=s.rules.findUnitType(std::string(c.factory)=="GAPILE"||std::string(c.factory)=="NAHAND"?"InfantryType":"UnitType",c.unit);
            const auto* factory=s.rules.findBuildingType(c.factory);
            if(!u||!factory||u->strength!=std::stoi(original.getValue(c.unit,"Strength"))
                ||u->cost!=std::stoi(original.getValue(c.unit,"Cost"))||u->speed!=std::stoi(original.getValue(c.unit,"Speed"))
                ||u->voiceMove!=original.getValue(c.unit,"VoiceMove")||u->moveSound!=original.getValue(c.unit,"MoveSound")
                ||u->createSound!=original.getValue(c.unit,"CreateSound")||u->voiceMove.empty()
                ||!s.soundLoaded||!s.evaLoaded||!findObjectById(s,1)->poweredOperational)return fail("original loaded unit/audio/factory input");
            const RuntimeBuildKind kind=std::string(c.factory)=="GAPILE"||std::string(c.factory)=="NAHAND"?RuntimeBuildKind::Infantry:RuntimeBuildKind::Unit;
            const auto found=std::find_if(s.economy.catalog.begin(),s.economy.catalog.end(),[&](const auto& item){return item.typeId==c.unit&&item.kind==kind;});
            if(found==s.economy.catalog.end())return fail("real original sidebar catalog item absent");
            const RuntimeBuildCatalogItem item=*found;const auto availability=runtimeBuildAvailability(s,item);
            if(!availability.canQueue||item.cost!=u->cost)return fail("real original sidebar queue unavailable: "+availability.reason);
            const auto spawn=findRuntimeProductionSpawnForOwner(s,item,s.economy.owner,false);
            if(!spawn||!spawn->anchor||spawn->anchor->instanceId!=1)return fail("real original factory selected wrong anchor");
            const RuntimeCellPoint rally=factory->naval?RuntimeCellPoint{51,74}:RuntimeCellPoint{74,74};
            const auto* domain=runtimeMapQueryCell(s,rally);
            if(!domain||domain->isWater!=factory->naval)return fail("rally goal lacks original map domain");
            if(c.rally) {
                stage="real rally UI";bool dispatched=false;
                capture(s,observed,[&]{applySelection(s,{1},false,false);
                    dispatched=dispatchRuntimePlayerFactoryFromUi(s,1,RuntimeMultiplayerGameplayCell{rally.x,rally.y});});
                flush(observed,"live","factory-rally");
                const auto r=s.playerCommands.rallyPoints.find(1);
                if(!dispatched||r==s.playerCommands.rallyPoints.end()
                    ||r->second.cell!=RuntimeMultiplayerGameplayCell{rally.x,rally.y}
                    ||r->second.owner!=runtimeCanonicalHumanHouseOwner(c.owner))return fail("real factory rally UI not committed");
            } else if(!s.playerCommands.rallyPoints.empty())return fail("no-rally control inherited a factory rally");
            stage="real sidebar queue";const int queueTick=s.tick,creditsBefore=s.economy.credits;bool queuedBuild=false;
            capture(s,observed,[&]{queuedBuild=dispatchRuntimeQueueProductionFromUi(s,item);});flush(observed,"live","sidebar-queue");
            if(!queuedBuild)return fail("real paid sidebar queue rejected");
            int id=-1,birth=-1,consume=-1;bool moving=false,saved=false;int autoMoveSound=0;
            std::unique_ptr<RuntimeWindowState> ordinary,canonical;Observation ordinaryAudio,canonicalAudio;
            const auto row=[&](bool passed,int automaticVoice,int manualVoice) {
                cases<<label<<','<<c.factory<<','<<c.unit<<','<<c.owner<<','<<c.rally<<','<<id<<','<<queueTick<<','<<birth<<','<<consume<<','<<s.tick
                    <<','<<creditsBefore<<','<<s.economy.credits<<','<<item.cost<<','<<automaticVoice<<','<<autoMoveSound
                    <<','<<soundCount(observed,u->createSound)<<','<<readyCount(observed)<<','<<manualVoice<<','<<passed<<'\n';cases.flush();
            };
            stage="natural paid birth/egress/rally";
            for(int count=0;count<30*180;++count) {
                step(s,observed,id,*u,"live");
                if(id<0)for(const auto& o:s.world.getObjects())if(o.alive&&o.type==c.unit&&o.kind!=(WorldObjectKind::Structure)) {
                    if(id>=0)return fail("multiple unrequested produced units");id=o.instanceId;birth=s.tick;
                }
                if(id>=0) {
                    const auto* a=findObjectById(s,id);
                    if(!a||a->owner!=c.owner||a->health!=u->strength)return fail("actual production owner/original HP changed");
                    moving=moving||a->renderMoveActive;
                    if(birth==s.tick){++produced;++ownerChecks;
                        const std::string creation=!factory->createUnitSound.empty()?factory->createUnitSound:u->createSound;
                        if(s.economy.credits!=creditsBefore-item.cost||readyCount(observed)!=1)return fail("paid original birth cost/UnitReady changed");
                        if(!creation.empty()&&(soundCount(observed,creation)!=1||!playableSound(observed,creation)))
                            return fail("original special CreateSound was not resolved once at actual birth");
                        if(!u->createSound.empty())++createChecks;
                        if(c.rally) {
                            const auto q=s.playerCommands.queues.find(id);
                            if(q==s.playerCommands.queues.end()||q->second.orders.size()!=1
                                ||q->second.owner!=runtimeCanonicalHumanHouseOwner(c.owner)
                                ||q->second.orders.front().order!=RuntimeMultiplayerGameplayTargetOrderKind::Move)
                                return fail("actual birth lacks pending original rally queue");
                            const auto* goal=std::get_if<RuntimeMultiplayerGameplayCellTarget>(&q->second.orders.front().target);
                            if(!goal||goal->cell!=RuntimeMultiplayerGameplayCell{rally.x,rally.y})return fail("actual pending rally target changed");
                        }
                    }
                    log(s,observed,id,*u,"live");
                    if(c.rally&&consume<0&&queued(s,id)==0)consume=s.tick;
                    autoMoveSound=soundCount(observed,u->moveSound);
                    if(voiceCount(observed,*u)!=0) {
                        row(false,voiceCount(observed,*u),0);
                        if(scenes==0){RuntimeSnapshotActiveStateScope active(s);
                            drawRuntimeStateOffscreenAtCell(s,{a->x,a->y},800,600,error,pathString(root/(label+"-automatic-voice.bmp")),false);}
                        return fail("automatic factory egress/rally emitted original VoiceMove/command move: "+diagnostic(s,id));
                    }
                    if(c.save&&!saved&&birth==s.tick) {
                        stage="physical pending rally saves";
                        ordinary=checkpoint(s,id,false,ordinaryAudio);if(!ordinary)return false;flush(ordinaryAudio,"ordinary14","physical-load");
                        canonical=checkpoint(s,id,true,canonicalAudio);if(!canonical)return false;flush(canonicalAudio,"canonical","physical-load");
                        saved=true;stage="natural paid birth/egress/rally";
                    } else if(saved) {
                        for(auto& peer:std::array<std::pair<RuntimeWindowState*,Observation*>,2>{{{ordinary.get(),&ordinaryAudio},{canonical.get(),&canonicalAudio}}}) {
                            const bool exact=peer.first==canonical.get();const char* name=exact?"canonical":"ordinary14";
                            step(*peer.first,*peer.second,id,*u,name);log(*peer.first,*peer.second,id,*u,name);
                            if(exact&&!equal(s,*peer.first,id,true))return fail(std::string(name)+" real continuation changed pose/queue/production/lease: source "+diagnostic(s,id)+" peer "+diagnostic(*peer.first,id));
                            // Ordinary14 preserves the committed segment but re-plans
                            // its unsaved remaining path. It still must execute its
                            // actual preserved rally, never fabricate a continuation.
                            if(!exact) {
                                const auto* actor=findObjectById(*peer.first,id);
                                if(!actor||actor->owner!=c.owner||actor->health!=u->strength
                                    ||peer.first->economy.credits!=s.economy.credits)
                                    return fail("ordinary14 actual continuation changed original actor/economy");
                                const auto q=peer.first->playerCommands.queues.find(id);
                                if(q!=peer.first->playerCommands.queues.end()) {
                                    const auto* aim=std::get_if<RuntimeMultiplayerGameplayCellTarget>(&q->second.orders.front().target);
                                    if(q->second.orders.size()!=1||q->second.owner!=runtimeCanonicalHumanHouseOwner(c.owner)
                                        ||q->second.orders.front().order!=RuntimeMultiplayerGameplayTargetOrderKind::Move
                                        ||!aim||aim->cell!=RuntimeMultiplayerGameplayCell{rally.x,rally.y})
                                        return fail("ordinary14 pending rally identity changed during actual continuation");
                                }
                            }
                            if(voiceCount(*peer.second,*u)!=0||readyCount(*peer.second)!=0||soundCount(*peer.second,u->createSound)!=0)
                                return fail(std::string(name)+" reload duplicated birth or spoke automatic rally voice");
                            if(exact)++pairedTicks;
                        }
                    }
                    if(settled(s,id,c.rally?std::optional<RuntimeCellPoint>{rally}:std::nullopt)&&(!saved||settled(*ordinary,id,rally)))break;
                }
            }
            if(id<0||!moving||!settled(s,id,c.rally?std::optional<RuntimeCellPoint>{rally}:std::nullopt)
                ||(c.rally&&consume<0)||(c.save&&(!saved||!settled(*ordinary,id,rally))))return fail("real factory/rally did not finish: "+diagnostic(s,id));
            if(c.rally&&!u->moveSound.empty()&&!runtimeDiskMoveSoundIsLoop(s,*u)
                &&(autoMoveSound!=1||!playableSound(observed,u->moveSound)))return fail("automatic rally lost original one-shot MoveSound");
            if(c.rally&&runtimeDiskMoveSoundIsLoop(s,*u)&&observed.loopWitnesses==0)return fail("automatic flying movement lost original movement loop");
            if(c.save&&(voiceCount(ordinaryAudio,*u)!=0||voiceCount(canonicalAudio,*u)!=0))return fail("restored automatic voice leaked");
            const int finish=s.tick,automaticVoice=voiceCount(observed,*u);int manualBefore=voiceCount(observed,*u);
            if(c.controls) {
                stage="manual same-rally goal";
                if(!manualMove(s,observed,*u,id,rally,"manual-same-rally",false))return false;
                // The exact same coordinate is a player command here, so it must
                // speak. Origin cannot be inferred from the destination cell.
                stage="manual appended queue";const RuntimeCellPoint appended{rally.x,rally.y+2};
                const int before=voiceCount(observed,*u);const auto appendStart=observed.records.size();
                if(!manualMove(s,observed,*u,id,appended,"manual-append",true))return false;
                bool consumed=false;
                for(int tick=0;tick<30*90;++tick){step(s,observed,id,*u,"live");log(s,observed,id,*u,"live");
                    if(!queued(s,id)&&voiceCount(observed,*u)==before+1)consumed=true;
                    if(settled(s,id,appended))break;}
                if(!consumed||!settled(s,id,appended)||voiceCount(observed,*u)!=before+1||!playableVoice(observed,*u,appendStart))
                    return fail("manual queued Move inherited rally silence or failed actual route");
                ++manualChecks;
                stage="real Stop then manual replacement";
                bool stopped=false;capture(s,observed,[&]{applySelection(s,{id},false,false);
                    stopped=dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop);});
                flush(observed,"live","manual-Stop");
                if(!stopped||queued(s,id)!=0)return fail("actual Stop retained player queue");
                ++stopChecks;
                const RuntimeCellPoint first{rally.x,rally.y+3},replacement{rally.x+1,rally.y+3};
                if(!manualMove(s,observed,*u,id,first,"manual-after-Stop",false))return false;
                step(s,observed,id,*u,"live");
                if(!manualMove(s,observed,*u,id,replacement,"manual-replace",false)||queued(s,id)!=0)return false;
                ++replaceChecks;
                for(int tick=0;tick<30*90&&!settled(s,id,replacement);++tick)step(s,observed,id,*u,"live");
                if(!settled(s,id,replacement))return fail("manual replacement did not actually finish");
            }
            row(true,automaticVoice,voiceCount(observed,*u)-manualBefore);
            sources<<label<<" birth="<<birth<<" consume="<<consume<<" firstFinish="<<finish<<" loopWitnesses="<<observed.loopWitnesses
                <<" loopOrdinal="<<observed.lastLoopOrdinal<<" allCapturedRecords="<<observed.records.size()<<'\n';sources.flush();
            ++scenes;std::cout<<"Factory rally audio "<<label<<" PASS produced="<<id<<" birth="<<birth<<" consume="<<consume<<" finish="<<finish<<'\n';
        }
        if(scenes!=8||produced!=8||ownerChecks!=8||createChecks!=4||manualChecks!=12||appendChecks!=3||stopChecks!=3||replaceChecks!=3
            ||physicalFiles!=2||reopens!=3||pairedTicks<=0)return fail("terminal census mismatch");
        std::ofstream result(root/"result.txt");
        result<<"PASS\nscenes="<<scenes<<"\nliveProduced="<<produced<<"\noriginalCreateSoundMetadata=7\nactualSpecialCreates="<<createChecks
            <<"\nmanualVoiceControls="<<manualChecks<<"\nappendControls="<<appendChecks<<"\nStopControls="<<stopChecks<<"\nreplacementControls="<<replaceChecks
            <<"\nownershipChecks="<<ownerChecks<<"\nuniquePhysicalFiles="<<physicalFiles<<"\nfileReopens="<<reopens<<"\npairedTicks="<<pairedTicks<<'\n';
        std::cout<<"Factory rally audio PASS scenes="<<scenes<<" produced="<<produced<<" creates="<<createChecks
            <<" manual="<<manualChecks<<" physical="<<physicalFiles<<" reopens="<<reopens<<" paired="<<pairedTicks<<'\n';
        return true;
    } catch(const std::exception& ex){return fail(ex.what());}
}
#pragma optimize("", on)