// Include after the actual movement, simulation, entry and command helpers.
// The oracle reads authored rules/ART and cell-center/subcell geometry; it
// never asks a production footprint or sprite bounding box for its answer.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
namespace RuntimeIssue25FoundationOracle {
struct Rect {
    std::string id, image, authored;
    int width=0, height=0;
    bool invisible=false, wall=false, gate=false;
    bool cell(int x,int y,int bx=20,int by=20) const {
        return x>=bx && x<bx+width && y>=by && y<by+height;
    }
    bool foot(double x,double y,int bx=20,int by=20) const {
        // Integers represent cell centers. A boundary touch is not penetration.
        constexpr double epsilon=1e-9;
        return x>bx-0.5+epsilon && x<bx+width-0.5-epsilon
            && y>by-0.5+epsilon && y<by+height-0.5-epsilon;
    }
};
std::string upper(std::string value) {
    for(auto& c:value)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return value;
}
const IniFile::Section* section(const IniFile& ini,const std::string& name) {
    for(const auto& s:ini.getSectionNames())if(upper(s)==upper(name))return ini.getSection(s);
    return nullptr;
}
std::string value(const IniFile& ini,const std::string& name,const std::string& key,
    const std::string& fallback="") {
    const auto* s=section(ini,name);
    if(s)for(auto it=s->rbegin();it!=s->rend();++it)if(upper(it->first)==upper(key))return it->second;
    return fallback;
}
bool yes(std::string text) {
    text=upper(text);return text=="YES"||text=="TRUE"||text=="1";
}
bool dimensions(const std::string& source,int& width,int& height) {
    std::string compact;for(unsigned char c:source)if(!std::isspace(c))compact+=static_cast<char>(c);
    const auto x=compact.find_first_of("xX");
    if(x==std::string::npos||compact.find_first_of("xX",x+1)!=std::string::npos)return false;
    const auto number=[](const std::string& s,int& out){
        if(s.empty()||std::any_of(s.begin(),s.end(),[](unsigned char c){return !std::isdigit(c);}))return false;
        try{out=std::stoi(s);}catch(...){return false;}return out>0&&out<=32;
    };
    return number(compact.substr(0,x),width)&&number(compact.substr(x+1),height);
}
std::array<double,2> subcell(const WorldObjectInstance& actor) {
    if(actor.kind!=WorldObjectKind::Infantry||actor.airborne)return {0,0};
    switch(actor.subCell) {
        case 1:return {-0.25,-0.25};case 2:return {0.25,-0.25};
        case 3:return {-0.25,0.25};case 4:return {0.25,0.25};default:return {0,0};
    }
}
std::array<double,2> foot(const WorldObjectInstance& actor) {
    const auto slot=subcell(actor);
    return {(actor.hasRenderPosition?actor.renderX:double(actor.x))+slot[0],
        (actor.hasRenderPosition?actor.renderY:double(actor.y))+slot[1]};
}
std::string csv(const std::string& source) {
    std::string out="\"";for(char c:source){if(c=='"')out+='"';out+=c;}return out+'"';
}
std::uint64_t fingerprint(const std::string& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("actual authored INI source unavailable: "+path);
    std::uint64_t hash=14695981039346656037ULL;char c;
    while(input.get(c)){hash^=static_cast<unsigned char>(c);hash*=1099511628211ULL;}
    if(input.bad())throw std::runtime_error("actual authored INI read failed: "+path);
    return hash;
}
struct Mover {std::string id, family;WorldObjectKind kind=WorldObjectKind::Unit;};
struct Progress {
    std::ofstream& log;const std::vector<std::string>& failures;
    std::string stage,label;std::size_t before;
    Progress(std::ofstream& stream,const std::vector<std::string>& errors,
        std::string phase,std::string name):log(stream),failures(errors),
        stage(std::move(phase)),label(std::move(name)),before(errors.size()){}
    ~Progress(){log.flush();std::cout<<"Issue25 foundation "<<stage<<": "<<label<<' '
        <<(failures.size()==before?"passed":"failed")<<'\n';}
};
std::string family(const IniFile& rules,const std::string& id,bool infantry) {
    const auto locomotor=upper(value(rules,id,"Locomotor"));
    const auto zone=upper(value(rules,id,"MovementZone")),speed=upper(value(rules,id,"SpeedType"));
    if(yes(value(rules,id,"JumpJet"))||yes(value(rules,id,"ConsideredAircraft"))
        ||locomotor.find("92612C46")!=std::string::npos||locomotor.find("4A582743")!=std::string::npos
        ||zone.find("FLY")!=std::string::npos||speed.find("WINGED")!=std::string::npos)return "air";
    if(locomotor.find("4A582742")!=std::string::npos||zone.find("HOVER")!=std::string::npos
        ||speed.find("HOVER")!=std::string::npos)return "hover";
    if(yes(value(rules,id,"Naval"))||yes(value(rules,id,"WaterBound"))
        ||locomotor.find("4A582745")!=std::string::npos||speed.find("FLOAT")!=std::string::npos
        ||zone.find("WATER")!=std::string::npos)return "naval";
    if(locomotor.find("4A582747")!=std::string::npos)return infantry?"teleport-foot":"teleport-drive";
    if(infantry)return "foot";
    if(upper(value(rules,id,"Voxel"))=="NO")return "unit-shp";
    return speed=="TRACK"||yes(value(rules,id,"Crusher"))?"track":"wheel";
}
}
bool runRuntimeGroundFoundationIssue25Smoke(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimeIssue25FoundationOracle;
    std::cout<<std::unitbuf;
    const char* requested=std::getenv("CONCORD_ISSUE25_FOUNDATION_OUTPUT");
    const std::filesystem::path output=requested&&*requested?requested:"build/smoke/feature-set-25/02-ground-foundation";
    std::filesystem::create_directories(output);
    const char* entryFilter=std::getenv("CONCORD_ISSUE25_FOUNDATION_ENTRIES_ONLY");
    const bool entriesOnly=entryFilter&&std::strcmp(entryFilter,"1")==0;
    std::ofstream catalog(output/"catalog.csv"),staticLog(output/"static.csv"),
        moves(output/"moves.csv"),entries(output/"entries.csv"),sources(output/"sources.csv");
    if(!catalog||!staticLog||!moves||!entries||!sources){error="Issue25 foundation output unavailable";return false;}
    catalog<<"kind,type,image,foundation,width,height,family,status,original_index\n";
    staticLog<<"building,type,foundation,inside_checks,outside_checks,bad_inside,bad_outside\n";
    moves<<"case,event,tick,type,cell_x,cell_y,subcell,foot_x,foot_y,active,host,inside_cell,inside_foot,accepted_x,accepted_y,behavior_kind,behavior_target_x,behavior_target_y,cooldown,result\n";
    entries<<"case,event,tick,type,cell_x,cell_y,subcell,foot_x,foot_y,active,allowed_host,garrison_host,bunker_host,pending,phase,result\n";
    sources<<"kind,path,fnv64\n";
    std::vector<std::string> failures;
    const auto record=[&](const std::string& text){failures.push_back(text);std::cout<<"Issue25 foundation FAIL: "<<text<<'\n';};
    try {
        auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
        assets->config.gameModeRulesOverride.clear();assets->config.playerSlots.clear();
        assets->report=buildRuntimeStartupReport(assets->config,&assets->world,&assets->theater,
            &assets->art,&assets->rules,&assets->mapFacts);
        if(!assets->report.success)throw std::runtime_error("original startup resources unavailable");
        assets->rulesLoaded=assets->artLoaded=assets->theaterLoaded=true;
        // Resolve the loader's real backing files; archive display paths need
        // not themselves be filesystem paths.
        const auto rulesSource=resolveRuntimeIniSource(assets->config,RuntimeCampaignIniKind::Rules);
        const auto artSource=resolveRuntimeIniSource(assets->config,RuntimeCampaignIniKind::Art);
        IniFile rules,art;
        const auto rulesPath=pathString(rulesSource.backingPath),artPath=pathString(artSource.backingPath);
        if(!rules.loadFromFile(rulesPath)||!art.loadFromFile(artPath))
            throw std::runtime_error("actual original rules/ART source did not parse");
        sources<<"rules,"<<csv(rulesPath)<<','<<fingerprint(rulesPath)<<"\nart,"<<csv(artPath)<<','<<fingerprint(artPath)<<'\n';
        const auto metadata=runtimeTerrainMetadataContext(*assets);int clearTile=-1;
        const int clearSet=assets->theater.getGeneralTileSetIndex("ClearTile");
        for(int tile=0;tile<static_cast<int>(assets->theater.getTotalTileSlots());++tile){
            TerrainCell cell;cell.tileIndex=tile;cell.subTileIndex=0;
            if(assets->theater.resolveTileIndex(tile).tileSetIndex==clearSet
                &&resolveRuntimeTerrainSubTileLandType(cell,metadata,assets->renderCache)==RuntimeTerrainLandType::Clear
                &&resolveRuntimeTerrainSubTileRampType(cell,metadata,assets->renderCache)==0){clearTile=tile;break;}
        }
        if(clearTile<0)throw std::runtime_error("original flat Clear TMP unavailable");
        std::vector<Rect> rectangles;std::map<std::pair<int,int>,Rect> shapes;
        int authoredCount=0,rawAuthoredCount=0,rawLoadedCount=0,uniqueLoadedCount=0,rawFoundationCount=0;
        int missingRules=0,missingFoundation=0,invisibleCount=0;
        const auto* buildingList=section(rules,"BuildingTypes");
        if(!buildingList)throw std::runtime_error("original BuildingTypes missing");
        std::set<std::string> seen;
        for(const auto& [index,id]:*buildingList){
            if(index.empty()||!std::all_of(index.begin(),index.end(),[](unsigned char c){return std::isdigit(c);}))continue;
            ++rawAuthoredCount;
            Rect r;r.id=id;r.image=value(rules,id,"Image",id);
            r.authored=value(art,r.image,"Foundation");
            if(r.authored.empty())r.authored=value(art,id,"Foundation");
            r.invisible=yes(value(rules,id,"InvisibleInGame"));r.wall=yes(value(rules,id,"Wall"));r.gate=yes(value(rules,id,"Gate"));
            const bool originalSection=section(rules,id)!=nullptr;
            const bool originalFoundation=originalSection&&dimensions(r.authored,r.width,r.height);
            if(originalSection)++rawLoadedCount;
            if(originalFoundation)++rawFoundationCount;
            if(!seen.insert(upper(id)).second){
                catalog<<"building,"<<csv(id)<<','<<csv(r.image)<<','<<csv(r.authored)<<','<<r.width<<','<<r.height
                    <<",,duplicate-alias,"<<csv(index)<<'\n';continue;
            }
            ++authoredCount;if(originalSection)++uniqueLoadedCount;
            std::string status="authored";
            if(!section(rules,id)){status="missing-original-rules-section";++missingRules;}
            else if(!dimensions(r.authored,r.width,r.height)){status="missing-or-invalid-original-ART-foundation";++missingFoundation;}
            else if(!assets->rules.findBuildingType(id)){status="unresolved-stock-type";record(id+" original BuildingType did not load");}
            else{
                rectangles.push_back(r);if(r.invisible)++invisibleCount;
                if(!r.invisible&&!r.wall&&!r.gate){
                    const auto key=std::make_pair(r.width,r.height);
                    if(!shapes.contains(key))shapes.emplace(key,r);
                }
                if(r.invisible)status="invisible-map-object";
                else if(value(art,r.image,"Image").empty()&&value(art,r.image,"Height").empty()
                    &&value(art,r.image,"NewTheater").empty())status="foundation-only-original-art";
            }
            catalog<<"building,"<<csv(id)<<','<<csv(r.image)<<','<<csv(r.authored)<<','<<r.width<<','<<r.height<<",,"<<csv(status)<<','<<csv(index)<<'\n';
        }
        std::vector<Mover> groundTypes;
        for(const auto& list:std::array<std::pair<const char*,bool>,2>{{{"InfantryTypes",true},{"VehicleTypes",false}}}){
            const auto* entriesList=section(rules,list.first);if(!entriesList)throw std::runtime_error("original unit type list missing");
            std::set<std::string> unitSeen;
            for(const auto& [index,id]:*entriesList){
                if(index.empty()||!std::all_of(index.begin(),index.end(),[](unsigned char c){return std::isdigit(c);})
                    ||!unitSeen.insert(upper(id)).second)continue;
                auto f=family(rules,id,list.second);
                if(!list.second&&(f=="track"||f=="wheel")
                    &&!yes(value(art,value(rules,id,"Image",id),"Voxel")))f="unit-shp";
                std::string status=!section(rules,id)?"missing-original-rules-section":
                    f=="air"||f=="naval"?"outside-ground-scope":"ground-schema";
                if(status=="ground-schema")groundTypes.push_back({id,f,list.second?WorldObjectKind::Infantry:WorldObjectKind::Unit});
                catalog<<(list.second?"infantry":"vehicle")<<','<<csv(id)<<','<<csv(value(rules,id,"Image",id))
                    <<",,0,0,"<<csv(f)<<','<<csv(status)<<','<<csv(index)<<'\n';
            }
        }
        catalog.flush();sources.flush();
        std::cout<<"Issue25 foundation catalogue: rawEntries="<<rawAuthoredCount<<" uniqueTypes="<<authoredCount
            <<" rawLoaded="<<rawLoadedCount<<" uniqueLoaded="<<uniqueLoadedCount<<" rawFoundation="<<rawFoundationCount
            <<" uniqueFoundation="<<rectangles.size()
            <<" shapes="<<shapes.size()<<" groundTypes="<<groundTypes.size()<<" entriesOnly="<<entriesOnly<<'\n';
        const auto make=[&](const Rect& r,const Mover* mover=nullptr,RuntimeCellPoint origin=RuntimeCellPoint{17,24},
            const std::string& hostOwner="Russians"){
            auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
            seedRuntimeLoadScratch(s,*assets,false);s.config=assets->config;s.config.fixedTickHz=30;
            s.config.playerSlots.clear();s.config.playerCountry="Americans";
            s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=false;
            s.config.crates=s.config.superweapons=s.config.shortGame=false;
            s.visionEnabled=s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;
            s.runtimeSuppressPlayerAudioFeedback=true;s.economy.owner="Americans";s.economy.credits=10000;
            s.economy.skirmishStartApplied=true;s.triggers.loaded=false;s.triggers.bootstrapComplete=true;
            s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();
            s.world.accessSmudges().clear();s.mapFacts={};s.terrainByCell.clear();s.terrainInfoByCell.clear();
            s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=-1;
            for(int y=0;y<40;++y)for(int x=0;x<40;++x){
                TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);
                cell.tileIndex=clearTile;cell.subTileIndex=0;s.world.accessTerrainCells().push_back(cell);
                MapCellFacts fact;fact.x=x;fact.y=y;fact.cellIndex=cell.cellIndex512;fact.hasTerrain=true;
                fact.terrain.x=x;fact.terrain.y=y;fact.terrain.tileIndex=clearTile;fact.terrain.subTileIndex=0;
                fact.terrainClass="clear";fact.passabilityHint="passable";
                s.mapFacts.cellIndexLookup.emplace(fact.cellIndex,s.mapFacts.cells.size());s.mapFacts.cells.push_back(fact);
            }
            s.mapFacts.terrainCellCount=s.mapFacts.lookupIndexSize=s.mapFacts.cells.size();
            s.mapFacts.contentHash=0x25020001;s.mapFactsLoaded=true;
            const auto add=[&](int id,WorldObjectKind kind,const std::string& type,RuntimeCellPoint p,const std::string& owner){
                WorldObjectInstance object;object.instanceId=id;object.kind=kind;object.type=type;object.owner=owner;
                object.x=p.x;object.y=p.y;object.cell=runtimeCellKey(p.x,p.y);object.level=0;object.subCell=kind==WorldObjectKind::Infantry?2:-1;
                object.alive=object.poweredOperational=true;object.mission="Guard";object.health=runtimeMaxHealthForObject(s,object);
                object.weaponCooldownTicksRemaining=100000;s.world.accessObjects().push_back(object);
            };
            add(100,WorldObjectKind::Structure,r.id,{20,20},hostOwner);
            // A stock powered control center keeps ROBO's movement legal.
            add(110,WorldObjectKind::Structure,"NANRCT",{4,4},"Americans");
            add(111,WorldObjectKind::Structure,"GAROBO",{12,4},"Americans");
            if(mover)add(1,mover->kind,mover->id,origin,"Americans");
            initializeRuntimeNavigation(s);initializeRuntimeLocalProductionLanes(s.economy);
            rebuildRuntimeEconomyPower(s);ensureRuntimeGarrisonsInitialized(s);
            RuntimeHumanHouseAuthority authority;authority.owner="Americans";authority.canonicalOwner="americans";authority.localFacade=true;
            s.humanHouseAuthorities={authority};
            return storage;
        };
        const std::array<Mover,8> representatives{{
            {"E1","foot",WorldObjectKind::Infantry},{"MTNK","track",WorldObjectKind::Unit},
            {"FV","wheel",WorldObjectKind::Unit},{"DRON","unit-shp",WorldObjectKind::Unit},
            {"ROBO","hover",WorldObjectKind::Unit},{"LCRF","hover",WorldObjectKind::Unit},
            {"CMIN","teleport-drive",WorldObjectKind::Unit},{"CLEG","teleport-foot",WorldObjectKind::Infantry}}};
        std::size_t staticChecks=0,staticPairs=0,motionCases=0,entryCases=0,completedEntries=0,boundaryControls=0,serializedControls=0;
        const auto query=[&](RuntimeWindowState& s,const Rect& r,const Mover& kind){
            WorldObjectInstance actor;actor.instanceId=1;actor.kind=kind.kind;actor.type=kind.id;
            actor.owner="Americans";actor.x=17;actor.y=17;actor.cell=runtimeCellKey(17,17);
            actor.subCell=kind.kind==WorldObjectKind::Infantry?2:-1;actor.alive=actor.poweredOperational=true;actor.health=100;
            const auto occupied=buildRuntimeOccupancyLookup(s,1);
            int insideChecks=0,outsideChecks=0,badInside=0,badOutside=0;
            for(int y=19;y<=20+r.height;++y)for(int x=19;x<=20+r.width;++x){
                const bool inside=r.cell(x,y);
                const bool allowed=runtimeCellIsPassable(s,s.terrainByCell,occupied,{x,y},actor);
                const bool expected=!inside||r.invisible;
                ++staticChecks;if(inside)++insideChecks;else++outsideChecks;
                if(allowed!=expected){if(inside)++badInside;else++badOutside;}
            }
            staticLog<<csv(r.id)<<','<<csv(kind.id)<<','<<csv(r.authored)<<','<<insideChecks<<','<<outsideChecks<<','<<badInside<<','<<badOutside<<'\n';
            ++staticPairs;if(badInside||badOutside)record(r.id+"/"+kind.id+" authored cell gate mismatch inside="+
                std::to_string(badInside)+" outside="+std::to_string(badOutside));
        };
        if(!entriesOnly)for(const auto& r:rectangles){
            auto storage=make(r);auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);
            for(const auto& mover:representatives)query(s,r,mover);
        }
        const auto findRect=[&](const std::string& id)->Rect{
            const auto it=std::find_if(rectangles.begin(),rectangles.end(),[&](const auto& r){return upper(r.id)==upper(id);});
            if(it==rectangles.end())throw std::runtime_error("required original Foundation missing: "+id);return *it;
        };
        const Rect refinery=findRect("GAREFN"),reactor=findRect("YAPOWR");
        if(refinery.width!=4||refinery.height!=3||reactor.width!=2||reactor.height!=2)
            record("original photograph building Foundation differs");
        if(!entriesOnly){auto storage=make(refinery);RuntimeSnapshotActiveStateScope active(*storage);
            for(const auto& type:groundTypes)query(*storage,refinery,type);}
        staticLog.flush();
        std::cout<<"Issue25 foundation static completed: pairs="<<staticPairs<<" checks="<<staticChecks<<'\n';
        struct MotionCase {Rect rectangle;Mover type;int route;};
        std::vector<MotionCase> motionCasesToRun;
        // Every authored dimension gets a real near-corner foot and tank route.
        // All movement families and Stop get full routes at the two photographed
        // stock buildings. The independent synthetic swept regression covers
        // the other slot/clock dimensions without repeating every subsystem.
        if(!entriesOnly){
            for(const auto& [shape,r]:shapes)for(const auto& type:{representatives[0],representatives[1]})
                motionCasesToRun.push_back({r,type,0});
            for(const auto& r:{refinery,reactor})for(const auto& type:representatives)for(int route=0;route<4;++route)
                motionCasesToRun.push_back({r,type,route});
        }
        const auto step=[](RuntimeWindowState& s){++s.tick;advanceRuntimeSimulation(s,false);};
        for(const auto& scene:motionCasesToRun){
            const auto& r=scene.rectangle;const auto& type=scene.type;const int route=scene.route;
            const std::string label=r.id+"/"+type.id+"/route"+std::to_string(route);++motionCases;
            Progress progress(moves,failures,"move "+std::to_string(motionCases)+"/"+std::to_string(motionCasesToRun.size()),label);
            RuntimeCellPoint start,goal;
            if(route==0){start={19,20+r.height-1};goal={20+r.width,20+r.height};}
            else if(route==1){start={20+r.width,20};goal={19,19};}
            else if(route==2){start={18,20+r.height};goal={20+r.width/2,20+r.height/2};}
            else {start={20+r.width,20+r.height};goal={19,20+r.height-1};}
            auto storage=make(r,&type,start,"Americans");auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);
            s.selectedObjectIds={1};std::map<int,RuntimeCellPoint> accepted;
            issueRuntimeMoveCommand(s,goal,&accepted);bool bad=false,settled=false,stopped=false;
            const auto sample=[&](const char* event){
                const auto* actor=findRuntimeObjectByIdIncludingInactive(s,1);
                if(!actor||!actor->alive){record(label+" ordinary mover disappeared");bad=true;return;}
                const auto point=foot(*actor);
                const bool insideCell=r.cell(actor->x,actor->y),insideFoot=r.foot(point[0],point[1]);
                const auto behavior=s.behaviors.find(1);
                const int host=behavior==s.behaviors.end()?-1:behavior->second.pendingAllowedOccupiedObjectId;
                moves<<csv(label)<<','<<event<<','<<s.tick<<','<<csv(type.id)<<','<<actor->x<<','<<actor->y<<','<<actor->subCell
                    <<','<<std::setprecision(12)<<point[0]<<','<<point[1]<<','<<actor->renderMoveActive<<','<<host
                    <<','<<insideCell<<','<<insideFoot<<','<<(accepted.contains(1)?accepted.at(1).x:-1)
                    <<','<<(accepted.contains(1)?accepted.at(1).y:-1)
                    <<','<<(behavior==s.behaviors.end()?-1:int(behavior->second.kind))
                    <<','<<(behavior==s.behaviors.end()?-1:behavior->second.moveTarget.x)
                    <<','<<(behavior==s.behaviors.end()?-1:behavior->second.moveTarget.y)
                    <<','<<(behavior==s.behaviors.end()?0:behavior->second.moveCooldownTicks)
                    <<','<<(insideCell||insideFoot?"inside":"clear")<<'\n';
                if(insideCell||insideFoot){record(label+" ordinary actual pose entered original solid Foundation at "+
                    std::to_string(point[0])+","+std::to_string(point[1]));bad=true;}
            };
            sample("command");
            if(s.lastCommand.acceptedCount!=1||!accepted.contains(1)||r.cell(accepted[1].x,accepted[1].y)){
                record(label+" Move failed to choose a legal outside target");bad=true;
            }
            for(int t=0;t<1800&&!bad;++t){
                step(s);sample("tick");const auto* actor=findObjectById(s,1);
                if(route==3&&!stopped&&actor&&actor->renderMoveActive){
                    const auto it=s.behaviors.find(1);
                    if(it!=s.behaviors.end()&&it->second.renderMoveTicksTotal>0
                        &&it->second.renderMoveTicksRemaining*2<=it->second.renderMoveTicksTotal){
                        s.selectedObjectIds={1};issueRuntimeStopCommand(s);stopped=true;sample("stop");
                    }
                }
                if(actor&&!actor->renderMoveActive
                    &&(stopped||runtimeCellPointForObject(*actor)==accepted[1])){settled=true;break;}
            }
            if(!bad&&!settled){
                const auto* actor=findRuntimeObjectByIdIncludingInactive(s,1);
                const auto b=s.behaviors.find(1);
                record(label+" actual Move/Stop did not settle outside the Foundation; cell="+
                    std::to_string(actor?actor->x:-1)+","+std::to_string(actor?actor->y:-1)+" accepted="+
                    std::to_string(accepted.contains(1)?accepted.at(1).x:-1)+","+
                    std::to_string(accepted.contains(1)?accepted.at(1).y:-1)+" behavior="+
                    std::to_string(b==s.behaviors.end()?-1:int(b->second.kind)));
            }
        }
        moves.flush();
        std::cout<<"Issue25 foundation real boundary moves completed: "<<motionCases<<'\n';
        struct Entry {const char* host;const char* actor;int kind;const char* owner;};
        const std::array<Entry,5> entryKinds{{
            {"NABNKR","E1",0,"Americans"},{"CARUS07","E1",0,"Neutral"},
            {"GAREFN","ENGINEER",1,"Russians"},{"YAPOWR","ENGINEER",1,"Russians"},
            {"NATBNK","MTNK",2,"Americans"}}};
        for(const auto& entry:entryKinds)for(int command=0;command<4;++command)
            for(double requestedPhase:command==3?std::vector<double>{0.0}:std::vector<double>{0.0,0.5,0.75}){
            const auto r=findRect(entry.host);Mover mover{entry.actor,entry.kind==2?"track":"foot",
                entry.kind==2?WorldObjectKind::Unit:WorldObjectKind::Infantry};
            const std::string label=std::string(entry.host)+"/"+entry.actor+"/"+
                (command==0?"stop":command==1?"guard":command==2?"move":"complete")+"/phase"+std::to_string(requestedPhase);
            ++entryCases;
            Progress progress(entries,failures,"entry "+std::to_string(entryCases)+"/50",label);
            auto storage=make(r,&mover,{19,20},entry.owner);auto& s=*storage;
            RuntimeSnapshotActiveStateScope active(s);s.selectedObjectIds={1};
            if(entry.kind==1)findMutableObjectById(s,100)->health=10; // Legal original damaged enemy capture.
            const auto* host=findObjectById(s,100);
            const bool queued=entry.kind==0?queueRuntimeOccupyTarget(s,*host):
                entry.kind==1?queueRuntimeEngineerCapture(s,*host):queueRuntimeTargetAbility(s,*host);
            bool window=false,bad=false;
            const auto pending=[&]{return entry.kind==0?s.pendingOccupyTargets.contains(1):
                entry.kind==1?s.pendingEngineerCaptures.contains(1):s.pendingTransportTargets.contains(1);};
            const auto completed=[&]{
                const auto* actor=findRuntimeObjectByIdIncludingInactive(s,1);
                const auto* building=findObjectById(s,100);
                return actor&&building&&(entry.kind==0?actor->garrisonHostObjectId==100:
                    entry.kind==1?!actor->alive&&building->owner=="Americans":
                    actor->tankBunkerHostId==100&&building->tankBunkerOccupantId==1);
            };
            const auto sample=[&](const char* event){
                const auto* actor=findRuntimeObjectByIdIncludingInactive(s,1);if(!actor)return;
                const auto point=foot(*actor);const auto b=s.behaviors.find(1);
                const int allowed=b==s.behaviors.end()?-1:b->second.pendingAllowedOccupiedObjectId;
                const double phase=b==s.behaviors.end()||b->second.renderMoveTicksTotal<=0?-1:
                    1.0-double(b->second.renderMoveTicksRemaining)/b->second.renderMoveTicksTotal;
                entries<<csv(label)<<','<<event<<','<<s.tick<<','<<csv(mover.id)<<','<<actor->x<<','<<actor->y<<','<<actor->subCell
                    <<','<<std::setprecision(12)<<point[0]<<','<<point[1]<<','<<actor->renderMoveActive<<','<<allowed
                    <<','<<actor->garrisonHostObjectId<<','<<actor->tankBunkerHostId<<','<<pending()<<','<<phase<<','
                    <<(r.cell(actor->x,actor->y)||r.foot(point[0],point[1])?"inside":"clear")<<'\n';
            };
            if(!queued||!pending()){record(label+" actual stock entry was not queued");continue;}
            sample("queued");
            for(int t=0;t<900;++t){
                step(s);sample("approach");
                if(command==3&&completed()){++completedEntries;window=true;break;}
                const auto* actor=findObjectById(s,1);const auto b=s.behaviors.find(1);
                if(command!=3&&actor&&b!=s.behaviors.end()&&pending()
                    &&b->second.pendingAllowedOccupiedObjectId==100&&actor->renderMoveActive
                    &&r.cell(actor->x,actor->y)&&runtimeCellPointForObject(*actor)==b->second.moveTarget
                    &&b->second.renderMoveTicksTotal>0&&b->second.renderMoveTicksRemaining>0){
                    const double phase=1.0-double(b->second.renderMoveTicksRemaining)/b->second.renderMoveTicksTotal;
                    if(phase+1e-9>=requestedPhase){window=true;sample("final-entry-window");break;}
                }
                if(completed())break;
            }
            if(!window){record(label+(command==3?" authorized entry failed to complete":" real final active entry window unavailable"));continue;}
            if(command==3)continue;
            s.selectedObjectIds={1};
            if(command==0)issueRuntimeStopCommand(s);
            else if(command==1)issueRuntimeGuardCommand(s);
            else issueRuntimeMoveCommand(s,{17,24});
            sample("cancel-command");
            const auto safe=[&]{
                const auto* actor=findRuntimeObjectByIdIncludingInactive(s,1);
                if(!actor||!actor->alive||actor->garrisonHostObjectId>=0||actor->tankBunkerHostId>=0||actor->dockedToObjectId>=0){
                    record(label+" cancellation completed/consumed the authorized entrant");return false;
                }
                const auto point=foot(*actor);const bool inside=r.cell(actor->x,actor->y)||r.foot(point[0],point[1]);
                // An explicit active exit may be retained; an ordinary settled
                // actor may never borrow the cancelled entrance's solid cell.
                if(inside&&!actor->renderMoveActive){record(label+" cancellation stranded an ordinary idle pose inside original Foundation");return false;}
                return true;
            };
            if(pending()){record(label+" replacement retained cancelled entry intent");bad=true;}
            if(!safe())bad=true;
            bool outside=false;
            for(int t=0;t<900&&!bad;++t){
                step(s);sample("after-cancel");if(!safe()){bad=true;break;}
                const auto* actor=findObjectById(s,1);const auto point=foot(*actor);
                if(!r.cell(actor->x,actor->y)&&!r.foot(point[0],point[1])&&!actor->renderMoveActive
                    &&(command!=2||runtimeCellPointForObject(*actor)==RuntimeCellPoint{17,24})){outside=true;break;}
            }
            if(!bad&&!outside)record(label+" cancelled entrant did not settle on legal outside ground");
        }

        // Nine small controls exercise rejected cancellation, failed arrival,
        // and a real bunker departure. All geometry remains original ART data.
        std::ofstream boundary(output/"boundary-controls.csv");
        boundary<<"case,event,tick,cell_x,cell_y,foot_x,foot_y,active,behavior_kind,allowed_host,weapon_cooldown,pending,inside_cell,inside_foot,garrison_host,bunker_host\n";
        if(!boundary)throw std::runtime_error("boundary control output unavailable");
        const auto commandName=[](int command){return command==0?"stop":command==1?"guard":"move";};
        const auto replaceCommand=[&](RuntimeWindowState& s,int command){
            s.selectedObjectIds={1};std::map<int,RuntimeCellPoint> accepted;
            if(command==0)issueRuntimeStopCommand(s);
            else if(command==1)issueRuntimeGuardCommand(s);
            else issueRuntimeMoveCommand(s,{17,24},&accepted);
            return accepted.contains(1)?accepted.at(1):RuntimeCellPoint{-1,-1};
        };
        const auto boundarySample=[&](RuntimeWindowState& s,const Rect& r,const std::string& label,const char* event){
            const auto* actor=findRuntimeObjectByIdIncludingInactive(s,1);if(!actor)return;
            const auto p=foot(*actor);const auto b=s.behaviors.find(1);
            boundary<<csv(label)<<','<<event<<','<<s.tick<<','<<actor->x<<','<<actor->y
                <<','<<std::setprecision(17)<<p[0]<<','<<p[1]<<','<<actor->renderMoveActive
                <<','<<(b==s.behaviors.end()?-1:int(b->second.kind))
                <<','<<(b==s.behaviors.end()?-1:b->second.pendingAllowedOccupiedObjectId)
                <<','<<(b==s.behaviors.end()?0:b->second.weaponCooldownTicks)
                <<','<<s.pendingOccupyTargets.contains(1)<<','<<r.cell(actor->x,actor->y)<<','<<r.foot(p[0],p[1])
                <<','<<actor->garrisonHostObjectId<<','<<actor->tankBunkerHostId<<'\n';
        };
        const auto outside=[&](RuntimeWindowState& s,const Rect& r){
            const auto* actor=findRuntimeObjectByIdIncludingInactive(s,1);if(!actor||!actor->alive)return false;
            const auto p=foot(*actor);return !r.cell(actor->x,actor->y)&&!r.foot(p[0],p[1]);
        };
        const auto liveUnhosted=[&](RuntimeWindowState& s){
            const auto* actor=findRuntimeObjectByIdIncludingInactive(s,1);
            return actor&&actor->alive&&actor->garrisonHostObjectId<0&&actor->tankBunkerHostId<0&&actor->dockedToObjectId<0;
        };
        const auto prepareEntry=[&](const Rect& r,bool tank,bool finish,const std::string& label){
            const Mover mover{tank?"MTNK":"E1",tank?"track":"foot",tank?WorldObjectKind::Unit:WorldObjectKind::Infantry};
            auto storage=make(r,&mover,{19,20},"Americans");auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);
            s.selectedObjectIds={1};const auto* host=findObjectById(s,100);
            if(!(tank?queueRuntimeTargetAbility(s,*host):queueRuntimeOccupyTarget(s,*host))){
                record(label+" legal stock entry was not queued");return std::unique_ptr<RuntimeWindowState>{};
            }
            for(int tick=0;tick<900;++tick){
                step(s);
                const auto* actor=findObjectById(s,1);const auto b=s.behaviors.find(1);if(!actor)break;
                if(finish&&actor->tankBunkerHostId==100
                    &&findObjectById(s,100)->tankBunkerOccupantId==1){
                    boundarySample(s,r,label,"entry-completed");return storage;
                }
                const auto p=foot(*actor);
                if(!finish&&s.pendingOccupyTargets.contains(1)&&b!=s.behaviors.end()
                    &&b->second.kind==RuntimeBehaviorKind::Move&&b->second.pendingAllowedOccupiedObjectId==100
                    &&actor->renderMoveActive&&r.cell(actor->x,actor->y)&&r.foot(p[0],p[1])
                    &&runtimeCellPointForObject(*actor)==b->second.moveTarget
                    &&b->second.renderMoveTicksTotal>0&&b->second.renderMoveTicksRemaining==1){
                    boundarySample(s,r,label,"last-entry-tick");return storage;
                }
            }
            record(label+" required real entry window unavailable");return std::unique_ptr<RuntimeWindowState>{};
        };
        const auto savedEntry=[&](RuntimeWindowState& s,const std::string& label){
            std::ostringstream saved;IniFile ini;std::string loadError;
            auto restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,s,false);
            RuntimeSnapshotActiveStateScope active(s);
            if(!writeRuntimeStateToStream(s,saved,loadError)||!ini.loadFromString(saved.str())
                ||!materializeRuntimeLoadStateFromIni(*restored,ini,loadError)){
                record(label+" real active entry save/load failed: "+loadError);
                return std::unique_ptr<RuntimeWindowState>{};
            }
            std::ofstream saveEvidence(output/(label+"-active.ini"));saveEvidence<<saved.str();
            const auto* before=findObjectById(s,1);const auto* after=findObjectById(*restored,1);
            const auto p=before?foot(*before):std::array<double,2>{-1,-1};
            const auto q=after?foot(*after):std::array<double,2>{-2,-2};
            const auto b=restored->behaviors.find(1);
            if(!before||!after||before->x!=after->x||before->y!=after->y||before->subCell!=after->subCell
                ||p!=q||!after->renderMoveActive||!restored->pendingOccupyTargets.contains(1)
                ||b==restored->behaviors.end()||b->second.kind!=RuntimeBehaviorKind::Move
                ||b->second.pendingAllowedOccupiedObjectId!=100||b->second.weaponCooldownTicks<=0){
                record(label+" load lost actual pose, accepted entry or inherited positive reload");
                return std::unique_ptr<RuntimeWindowState>{};
            }
            ++serializedControls;return restored;
        };
        const auto actorAuthority=[&](RuntimeWindowState& s){
            std::ostringstream key;key<<std::setprecision(17);
            const auto* o=findRuntimeObjectByIdIncludingInactive(s,1);
            if(!o)return std::string("missing actor");
            key<<o->instanceId<<'|'<<int(o->kind)<<'|'<<o->type<<'|'<<o->owner<<'|'<<o->alive<<'|'<<o->health
                <<'|'<<o->x<<'|'<<o->y<<'|'<<o->cell<<'|'<<o->subCell<<'|'<<o->level<<'|'<<o->onBridge
                <<'|'<<o->mission<<'|'<<o->poweredOperational<<'|'<<o->hasRenderPosition<<'|'<<o->renderMoveActive
                <<'|'<<o->renderX<<'|'<<o->renderY<<'|'<<o->renderLevel<<'|'<<o->renderOnBridge
                <<'|'<<o->garrisonHostObjectId<<'|'<<o->tankBunkerHostId<<'|'<<o->dockedToObjectId<<'|'<<o->factoryExitHostId
                <<'|'<<o->weaponCooldownTicksRemaining;
            const auto b=s.behaviors.find(1);
            if(b==s.behaviors.end())key<<"|missing behavior";
            else{
                const auto& m=b->second;key<<'|'<<runtimeMultiplayerSnapshotBehaviorRecord(1,m)
                    <<'|'<<m.moveStartupDistanceQ16<<'|'<<m.renderStartupAccelerationDistance
                    <<'|'<<m.transportTransferHostId<<'|'<<int(m.transportTransferKind)<<'|'<<m.repackMoveGroup.has_value()
                    <<'|'<<m.renderFrom.x<<'|'<<m.renderFrom.y<<'|'<<m.renderTo.x<<'|'<<m.renderTo.y
                    <<'|'<<m.renderFromOffsetX<<'|'<<m.renderFromOffsetY<<'|'<<m.renderToOffsetX<<'|'<<m.renderToOffsetY
                    <<'|'<<m.renderFromLevel<<'|'<<m.renderToLevel<<'|'<<m.renderFromOnBridge<<'|'<<m.renderToOnBridge
                    <<'|'<<m.renderMoveTicksTotal<<'|'<<m.renderMoveTicksRemaining<<'|'<<m.renderArrivalDistanceAfter;
            }
            const auto pending=[&](const auto& map){
                const auto it=map.find(1);key<<'|'<<(it==map.end()?-1:it->second);
            };
            pending(s.pendingOccupyTargets);pending(s.pendingTransportTargets);pending(s.pendingEngineerCaptures);
            const auto reservation=s.subcells.reservations().find(1);
            if(reservation==s.subcells.reservations().end())key<<"|no reservation";
            else{const auto& r=reservation->second;key<<'|'<<r.destination.x<<'|'<<r.destination.y<<'|'<<r.destination.cell
                <<'|'<<r.destination.onBridge<<'|'<<r.subCell;}
            return key.str();
        };
        // Occupy every ordinary cell with a stationary stock vehicle. The host
        // and the two real power/control structures retain their authored cells.
        // No custom passability rule or helper radius supplies the expected result.
        const auto sealOutside=[&](RuntimeWindowState& s,const Rect& r){
            const Rect power=findRect("NANRCT"),control=findRect("GAROBO");
            auto& objects=s.world.accessObjects();
            for(int y=0;y<40;++y)for(int x=0;x<40;++x){
                if(r.cell(x,y)||power.cell(x,y,4,4)||control.cell(x,y,12,4))continue;
                WorldObjectInstance blocker;blocker.instanceId=2000+runtimeCellKey(x,y);
                blocker.kind=WorldObjectKind::Unit;blocker.type="MTNK";blocker.owner="Americans";
                blocker.x=x;blocker.y=y;blocker.cell=runtimeCellKey(x,y);blocker.subCell=-1;
                blocker.alive=blocker.poweredOperational=true;blocker.mission="Guard";
                blocker.health=runtimeMaxHealthForObject(s,blocker);blocker.weaponCooldownTicksRemaining=100000;
                objects.push_back(blocker);
            }
            refreshRuntimeObjectOccupancyAfterLocalChange(s);
        };
        const auto openOutside=[&](RuntimeWindowState& s){
            const int id=2000+runtimeCellKey(19,20);auto& objects=s.world.accessObjects();
            const auto blocked=std::find_if(objects.begin(),objects.end(),[&](const auto& o){return o.instanceId==id;});
            if(blocked==objects.end())throw std::runtime_error("sealed outside control cell missing");
            objects.erase(blocked);refreshRuntimeObjectOccupancyAfterLocalChange(s);
        };
        const Rect garrison=findRect("NABNKR"),bunker=findRect("NATBNK");
        for(int command=0;command<3;++command){
            const std::string label=std::string("no-exit-")+commandName(command);++boundaryControls;
            Progress progress(boundary,failures,"boundary "+std::to_string(boundaryControls)+"/9",label);
            auto storage=prepareEntry(garrison,false,false,label);if(!storage)continue;
            auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);sealOutside(s,garrison);
            boundarySample(s,garrison,label,"sealed");const auto original=actorAuthority(s);
            replaceCommand(s,command);boundarySample(s,garrison,label,"rejected-command");
            if(s.lastCommand.acceptedCount!=0||s.lastCommand.rejectedCount!=1||actorAuthority(s)!=original)
                record(label+" unavailable ordinary ground did not reject replacement with complete original Move/pose/entry authority intact");
            // The no-exit assertion is complete. Remove only fixture blockers
            // from memory so a replacement Move has its real ordinary route.
            auto& reopened=s.world.accessObjects();
            reopened.erase(std::remove_if(reopened.begin(),reopened.end(),[](const auto& o){return o.instanceId>=2000;}),reopened.end());
            refreshRuntimeObjectOccupancyAfterLocalChange(s);
            const auto accepted=replaceCommand(s,command);boundarySample(s,garrison,label,"opened-command");
            if(s.lastCommand.acceptedCount!=1||!liveUnhosted(s)||!outside(s,garrison)||s.pendingOccupyTargets.contains(1)
                ||(command==2&&(accepted!=RuntimeCellPoint{17,24}||garrison.cell(accepted.x,accepted.y))))
                record(label+" opening ordinary ground failed to revoke entry and recover actual outside pose");
            // Stop/Guard must remain ordinary; a new Move must settle at its
            // requested outside destination without borrowing the old host.
            bool settled=false;
            for(int tick=0;tick<1800;++tick){
                const auto* actor=findObjectById(s,1);
                if(!liveUnhosted(s)||!outside(s,garrison)){record(label+" post-replacement ordinary pose penetrated Foundation");break;}
                if(actor&&!actor->renderMoveActive&&(command!=2||runtimeCellPointForObject(*actor)==accepted)){
                    settled=true;break;
                }
                step(s);boundarySample(s,garrison,label,"ordinary-tick");
            }
            if(!settled)record(label+" opened cancellation did not settle on ordinary ground");
        }
        for(int blocked=0;blocked<2;++blocked){
            const std::string label=blocked?"failed-arrival-no-exit":"failed-arrival-free-exit";++boundaryControls;
            Progress progress(boundary,failures,"boundary "+std::to_string(boundaryControls)+"/9",label);
            auto sourceState=prepareEntry(garrison,false,false,label);if(!sourceState)continue;
            // The real save importer mirrors the paid independent weapon reload
            // into the active Move. A positive completion cooldown is legal.
            auto storage=savedEntry(*sourceState,label);if(!storage)continue;
            auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);
            if(blocked)sealOutside(s,garrison);
            auto* host=findMutableObjectById(s,100);host->owner="Russians";
            if(runtimeObjectCanOccupyTarget(s,*findObjectById(s,1),*host))
                record(label+" changed owner did not invalidate the accepted original garrison entry");
            boundarySample(s,garrison,label,"qualification-revoked");
            bool rejected=false;
            for(int tick=0;tick<20;++tick){
                step(s);boundarySample(s,garrison,label,"completion-tick");
                if(!s.pendingOccupyTargets.contains(1)){rejected=true;break;}
            }
            if(!rejected||!liveUnhosted(s)){
                record(label+" actual failed entry did not clear pending intent and retain live unhosted actor");continue;
            }
            if(blocked){
                const auto b=s.behaviors.find(1);const auto* actor=findObjectById(s,1);
                if(b==s.behaviors.end()||b->second.kind!=RuntimeBehaviorKind::Move
                    ||b->second.pendingAllowedOccupiedObjectId!=100||b->second.weaponCooldownTicks<=0
                    ||!actor||!garrison.cell(actor->x,actor->y)||outside(s,garrison))
                    record(label+" completed tail discarded blocked original Move/allowedHost before safe recovery");
                openOutside(s);step(s);boundarySample(s,garrison,label,"opened-completion");
            }
            const auto b=s.behaviors.find(1);
            if(!outside(s,garrison)||!liveUnhosted(s)
                ||(b!=s.behaviors.end()&&b->second.kind==RuntimeBehaviorKind::Move
                    &&b->second.pendingAllowedOccupiedObjectId==100))
                record(label+" completion/cooldown conversion retained an unauthorized solid Foundation pose");
        }
        for(int command=0;command<4;++command){
            const std::string label=std::string("bunker-unload-")+(command==3?"complete":commandName(command));++boundaryControls;
            Progress progress(boundary,failures,"boundary "+std::to_string(boundaryControls)+"/9",label);
            auto storage=prepareEntry(bunker,true,true,label);if(!storage)continue;
            auto& s=*storage;RuntimeSnapshotActiveStateScope active(s);
            if(!unloadRuntimeTankBunker(s,*findMutableObjectById(s,1))){
                record(label+" completed stock bunker could not accept normal unload");continue;
            }
            bool window=false;
            for(int tick=0;tick<900;++tick){
                step(s);boundarySample(s,bunker,label,"unload-tick");
                const auto* actor=findObjectById(s,1);const auto b=s.behaviors.find(1);if(!actor)break;
                const auto p=foot(*actor);
                if(liveUnhosted(s)&&actor->renderMoveActive&&!bunker.cell(actor->x,actor->y)
                    &&bunker.foot(p[0],p[1])&&b!=s.behaviors.end()&&b->second.kind==RuntimeBehaviorKind::Move
                    &&b->second.pendingAllowedOccupiedObjectId==100){
                    window=true;boundarySample(s,bunker,label,"logical-outside-body-inside");break;
                }
            }
            if(!window){record(label+" real unload did not expose logical-outside/actual-foot-inside window");continue;}
            if(command!=3){
                replaceCommand(s,command);boundarySample(s,bunker,label,"replacement-command");
                const auto b=s.behaviors.find(1);
                if(s.lastCommand.acceptedCount!=1||!outside(s,bunker)||!liveUnhosted(s)
                    ||(b!=s.behaviors.end()&&b->second.pendingAllowedOccupiedObjectId==100))
                    record(label+" replacement restored old inside body or retained temporary bunker authority");
            }
            bool settled=false;
            for(int tick=0;tick<1800;++tick){
                const auto* actor=findObjectById(s,1);if(!actor||!liveUnhosted(s)){
                    record(label+" unloader disappeared or became hosted");break;
                }
                if(!outside(s,bunker)){
                    const auto b=s.behaviors.find(1);
                    if(command!=3||!actor->renderMoveActive||b==s.behaviors.end()
                        ||b->second.kind!=RuntimeBehaviorKind::Move||b->second.pendingAllowedOccupiedObjectId!=100){
                        record(label+" ordinary settled unloader retained inside Foundation pose");break;
                    }
                }
                if(outside(s,bunker)&&!actor->renderMoveActive
                    &&(command!=2||runtimeCellPointForObject(*actor)==RuntimeCellPoint{17,24})){
                    settled=true;break;
                }
                step(s);boundarySample(s,bunker,label,"after-unload-tick");
            }
            if(!settled)record(label+" completed/replaced unload did not settle outside original Foundation");
        }
        boundary.flush();

        catalog.flush();staticLog.flush();moves.flush();entries.flush();sources.flush();
        std::cout<<"Issue25 foundation coverage: rawEntries="<<rawAuthoredCount<<" uniqueTypes="<<authoredCount
            <<" rawLoaded="<<rawLoadedCount<<" uniqueLoaded="<<uniqueLoadedCount<<" rawFoundation="<<rawFoundationCount
            <<" uniqueFoundation="<<rectangles.size()
            <<" shapes="<<shapes.size()<<" invisible="<<invisibleCount<<" missingRules="<<missingRules
            <<" missingFoundation="<<missingFoundation<<" groundTypes="<<groundTypes.size()
            <<" staticPairs="<<staticPairs<<" staticChecks="<<staticChecks<<" actualMoves="<<motionCases
            <<" entries="<<entryCases<<" legalCompletions="<<completedEntries
            <<" boundaryControls="<<boundaryControls<<" serializedControls="<<serializedControls<<" failures="<<failures.size()<<'\n';
        if(rawAuthoredCount!=403||authoredCount!=402||rawLoadedCount!=399||uniqueLoadedCount!=398
            ||rawFoundationCount!=398||rectangles.size()!=397||shapes.size()!=20||groundTypes.size()<50
            ||boundaryControls!=9||serializedControls!=2||completedEntries!=entryKinds.size()
            ||entryCases!=50||(!entriesOnly&&motionCases!=104))
            record("complete original catalogue/schema/authorized entry coverage missing");
    }catch(const std::exception& exception){record(exception.what());}
    if(!failures.empty()){
        error="Issue25 ground Foundation failed "+std::to_string(failures.size())+" checks; first: "+failures.front();return false;
    }
    std::cout<<"Issue25 ground Foundation passed: original catalogue, actual boundary poses and cancelled/complete legal entry controls\n";
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif

