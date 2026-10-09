// Stock UI orders, real movement/boarding and natural weapon/timer expiry.
// Expected bridge geometry and native pixels never call demolition helpers.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeBridgeHutDemolitionSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    std::string label="startup",stage;
    const auto fail=[&](const std::string& why){error="Bridge hut demolition "+label+" ["+stage+"]: "+why;return false;};
    const char* filter=std::getenv("CONCORD_BRIDGE_HUT_DEMOLITION_CASE");
    const char* output=std::getenv("CONCORD_BRIDGE_HUT_DEMOLITION_OUTPUT");
    const std::filesystem::path root=output&&*output?output:"build/issue22_05_bridge_hut_demolition";
    std::filesystem::create_directories(root/"native-original");
    std::ofstream records(root/"cases.csv"),topology(root/"topology.txt");
    records<<"case,replica,tick,stage,source,hut,anchors,collapsed,timer,sourceAlive,huts,spans\n";
    auto stock=std::make_unique<RuntimeWindowState>();stock->config=config;
    stock->config.playerSlots.clear();stock->config.gameModeRulesOverride.clear();
    stock->report=buildRuntimeStartupReport(stock->config,&stock->world,&stock->theater,&stock->art,&stock->rules,&stock->mapFacts);
    if(!stock->report.success)return fail("stock resources: "+stock->report.errorMessage);
    stock->rulesLoaded=stock->artLoaded=stock->theaterLoaded=true;
    IniFile raw;if(!raw.loadFromFile(stock->report.rulesPath)||!stock->rules.loadFromIni(raw))return fail("original rules");
    const auto rulesHash=stock->rules.getDetailRulesHash();
    // Freeze only autonomous resources through the existing mode overlay.
    // Long original Ivan timers/IFV boarding must not turn untouched ore
    // into a false bridge-damage difference. Native rules and terrain stay
    // exact, and simulation/bomb/movement clocks still advance normally.
    std::ostringstream resourceMode;
    for(const auto& t:stock->rules.getTiberiumTypes())resourceMode<<'['<<t.id<<"]\nGrowthPercentage=0\nSpreadPercentage=0\n";
    const auto resourceModeText=resourceMode.str();
    topology<<"resourceFixture=existingModeOverlay originalRules/terrain=preserved GrowthPercentage=0 SpreadPercentage=0 nativeTiberiumTypes="
        <<stock->rules.getTiberiumTypes().size()<<'\n';
    const auto resourceFixtureValid=[&](const RuntimeWindowState& s){
        if(!s.skirmishModeRulesLoaded||s.rules.getDetailRulesHash()!=rulesHash)return false;
        for(const auto& t:stock->rules.getTiberiumTypes())
            if(s.skirmishModeRules.getValue(t.id,"GrowthPercentage","")!="0"
                ||s.skirmishModeRules.getValue(t.id,"SpreadPercentage","")!="0"
                ||runtimeOreRuleProbability(s,t,false)!=0.0||runtimeOreRuleProbability(s,t,true)!=0.0)return false;
        return !stock->rules.getTiberiumTypes().empty();};
    const auto* hutRules=findRuntimeBuildingTypeById(stock->rules,"CABHUT");
    if(!hutRules||!hutRules->bridgeRepairHut||hutRules->strength!=2000||!hutRules->immune
        ||raw.getValue("CABHUT","BridgeRepairHut","")!="yes"||raw.getValue("CABHUT","Immune","")!="yes")
        return fail("original CABHUT contract");
    std::set<std::string> originalC4,originalIvan;
    for(const auto& unit:stock->rules.getInfantryTypes()) {
        if(raw.getValue(unit.id,"C4","")=="yes")originalC4.insert(unit.id);
        if(raw.getValue(unit.id,"Ivan","")=="yes")originalIvan.insert(unit.id);
    }
    if(originalC4!=std::set<std::string>{"GHOST","PTROOP","TANY"}
        ||originalIvan!=std::set<std::string>{"CIVAN","IVAN"}
        ||raw.getValue("GHOST","Image","")!="SEAL"||raw.getValue("CCOMAND","Secondary","")!="FakeC4"
        ||raw.getValue("TERROR","Primary","")!="TerrorBomb")return fail("raw stock demolition capabilities");
    const double c4Minutes=std::stod(raw.getValue("CombatDamage","C4Delay","0"));
    const int ivanFrames=std::stoi(raw.getValue("CombatDamage","IvanTimedDelay","0"));
    const int ivanDamage=std::stoi(raw.getValue("CombatDamage","IvanDamage","0"));
    if(c4Minutes!=.03||ivanFrames!=450||ivanDamage!=450)return fail("raw bomb timing/damage");
    struct Capability {const char* name;const char* type;const char* weapon;const char* passenger;bool c4=false,ivan=false,suicide=false;};
    const std::vector<Capability> capabilities{
        {"TANY","TANY","","",true},{"GHOST","GHOST","","",true},{"PTROOP","PTROOP","","",true},
        {"IVAN","IVAN","IvanBomber","",false,true},{"CIVAN","CIVAN","IvanBomber","",false,true},
        {"TERROR","TERROR","TerrorBomb","",false,false,true},{"CCOMAND","CCOMAND","FakeC4",""},
        {"DTRUCK","DTRUCK","Demobomb","",false,false,true},
        {"FV-mode7","FV","CRNuke","IVAN",false,false,true},
        {"FV-mode11","FV","CRTerrorBomb","TERROR",false,false,true}};
    for(const auto& c:capabilities) {
        const auto* unit=stock->rules.findUnitType(c.type==std::string("FV")||c.type==std::string("DTRUCK")?"Vehicle":"Infantry",c.type);
        const auto* weapon=*c.weapon?stock->rules.findWeaponType(c.weapon):nullptr;
        if(!unit||(c.c4&&!unit->c4)||(c.ivan&&!unit->ivan)||(*c.weapon&&!weapon)
            ||(weapon&&weapon->suicide!=c.suicide))return fail("native capability missing: "+std::string(c.name));
        if(weapon&&(weapon->damage!=std::stoi(raw.getValue(c.weapon,"Damage","0"))
            ||weapon->projectile!=raw.getValue(c.weapon,"Projectile","")||weapon->warhead!=raw.getValue(c.weapon,"Warhead","")))
            return fail("original weapon parse differs: "+std::string(c.weapon));
    }
    const auto ordinal=[&](const std::string& id){const auto& types=stock->rules.getOverlayTypes();
        for(std::size_t i=0;i<types.size();++i)if(types[i].id==id)return static_cast<int>(i);return -1;};
    if(ordinal("BRIDGE1")!=24||ordinal("BRIDGE2")!=25||ordinal("BRIDGEB1")!=237||ordinal("BRIDGEB2")!=238
        ||ordinal("LOBRDG01")!=74||ordinal("LOBRDB01")!=205)return fail("native OverlayPack ordinals");
    const auto lowId=[](bool wood,int offset){std::ostringstream v;v<<(wood?"LOBRDG":"LOBRDB")<<std::setw(2)<<std::setfill('0')<<offset+1;return v.str();};
    const auto bridge=[](const TerrainCell& c){return c.hasOverlay&&(c.overlayImage=="BRIDGE"||c.overlayImage=="BRIDGB"
        ||c.overlayType.rfind("LOBRDG",0)==0||c.overlayType.rfind("LOBRDB",0)==0);};
    const auto fields=[](const TerrainCell& a,const TerrainCell& b){return a.x==b.x&&a.y==b.y&&a.tileIndex==b.tileIndex
        &&a.subTileIndex==b.subTileIndex&&a.level==b.level&&a.iceGrowth==b.iceGrowth&&a.hasOverlay==b.hasOverlay
        &&a.overlayIndex==b.overlayIndex&&a.overlayFrame==b.overlayFrame&&a.overlayType==b.overlayType
        &&a.overlayImage==b.overlayImage&&a.overlayIsOre==b.overlayIsOre&&a.overlayIsGem==b.overlayIsGem;};
    const auto terrain=[](const RuntimeWindowState& s,RuntimeCellPoint p)->const TerrainCell*{
        const auto it=s.terrainByCell.find(runtimeCellKey(p.x,p.y));return it==s.terrainByCell.end()?nullptr:it->second;};
    const auto facade=[](RuntimeWindowState& s){s.economy.owner="Americans";s.config.playerCountry="Americans";
        s.humanHouseAuthorities.clear();RuntimeHumanHouseAuthority a;a.owner=a.country="Americans";
        a.canonicalOwner="americans";a.localFacade=true;s.humanHouseAuthorities.push_back(std::move(a));};
    const auto prepare=[&](RuntimeWindowState& s,int hz){s.config.fixedTickHz=hz;s.config.gameMode="Battle";
        s.config.skirmishLoadSavePath.clear();s.config.playerSlots.clear();s.config.gameModeRulesOverride.clear();
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=false;
        s.config.shortGame=s.config.crates=s.config.superweapons=false;s.campaignMode=false;
        s.visionEnabled=s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.rules=stock->rules;s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;
        s.tick=100;s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;facade(s);};
    const auto initialize=[&](RuntimeWindowState& s){initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);initializeRuntimeTriggers(s);
        s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;ensureRuntimeSubcellsInitialized(s);
        ensureRuntimeSpecialMechanismsInitialized(s);ensureRuntimeLightingInitialized(s);ensureRuntimeParticlesInitialized(s);
        initializeRuntimeOfflineGameplayRouting(s);facade(s);
        s.skirmishModeRulesLoaded=s.skirmishModeRules.loadFromString(resourceModeText);
        if(!resourceFixtureValid(s))fail("resource fixture mode failed or altered original rules");};
    const auto step=[](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);++s.tick;
        advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);};
    int ordinarySaves=0,canonicalSaves=0,pixelsChecked=0,passed=0,negativePassed=0;
    const auto checkpoint=[&](RuntimeWindowState& s,bool canonical){std::ostringstream payload,normalized;IniFile base,exact;
        const auto reject=[&](const std::string& reason){error=reason+(error.empty()?"":": "+error);return std::unique_ptr<RuntimeWindowState>{};};
        if(!writeRuntimeStateToStream(s,payload,error)||!base.loadFromString(payload.str()))return reject("ordinary checkpoint write/parse");
        {std::ofstream save(root/(label+"-"+stage+"-ordinary.ini"),std::ios::binary);save<<payload.str();}
        if(canonical) {
            if(!writeRuntimeMultiplayerSnapshotExtension(s,payload,error)||!exact.loadFromString(payload.str())
                ||!writeRuntimeMultiplayerSnapshotCanonicalIni(exact,normalized,error)||!exact.loadFromString(normalized.str()))
                return reject("canonical checkpoint write/parse");
            std::ofstream save(root/(label+"-"+stage+"-canonical.ini"),std::ios::binary);save<<normalized.str();
        }
        auto copy=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*copy,s,false);RuntimeSnapshotActiveStateScope active(*copy);
        if(!materializeRuntimeLoadStateFromIni(*copy,base,error)||(canonical&&!materializeRuntimeMultiplayerSnapshotExtension(*copy,exact,error)))
            return reject("strict checkpoint restore");
        if(!resourceFixtureValid(*copy))return reject("strict checkpoint lost zero-growth resource mode or original rules");
        copy->runtimeSoundsEnabled=copy->runtimePreferencePersistenceEnabled=copy->visionEnabled=false;facade(*copy);
        if(canonical)++canonicalSaves;else ++ordinarySaves;return copy;};
    std::set<std::string> nativeSources;std::map<std::string,std::vector<std::uint8_t>> nativeBytes;
    const auto extension=[](const RuntimeWindowState& s){auto v=s.theater.getTileExtension();if(!v.empty()&&v.front()=='.')v.erase(v.begin());return v;};
    const auto readNative=[&](RuntimeWindowState& s,const std::string& filename,std::vector<std::uint8_t>& bytes){
        const auto known=nativeBytes.find(filename);if(known!=nativeBytes.end()){bytes=known->second;return true;}
        const auto ext=extension(s);const std::string theaterName=ext=="des"?"desert":ext=="sno"?"snow":ext=="urb"?"urban":ext=="ubn"?"urbann":ext=="lun"?"lunar":"temperat";
        const std::string originalIso=ext=="tem"?"isotemp.mix":ext=="sno"?"isosnow.mix":"iso"+ext+".mix";
        for(const std::string directory:{"ra2md","ra2"})for(const std::string archiveName:{"iso"+ext+"md.mix",originalIso,theaterName+"md.mix",theaterName+".mix",ext+".mix"}) {
            const auto path=std::filesystem::path(runtimeAssetRootPath(s.config))/directory/archiveName;
            if(!std::filesystem::is_regular_file(path))continue;MixArchive archive;if(!archive.loadFromPath(path))continue;
            const auto* entry=archive.findEntryByName(filename);if(!entry)continue;
            if(!archive.readEntryData(*entry,bytes,error))return fail("original MIX read: "+error);
            nativeBytes.emplace(filename,bytes);nativeSources.insert(pathString(path)+"!"+filename);
            std::ofstream copy(root/"native-original"/filename,std::ios::binary);copy.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
            return static_cast<bool>(copy)||fail("original byte snapshot write");
        }return fail("original MIX resource unavailable: "+filename);};
    struct Scene {std::unique_ptr<RuntimeWindowState> state;bool high=true,wood=false,alongX=true,actual=false;
        RuntimeCellPoint first{31,32},last{39,32},spawn;int land=-1,hut=-1,source=-1,passenger=-1;
        std::vector<TerrainCell> original;std::vector<WorldObjectInstance> huts;std::unordered_map<int,int> originalHealth;};
    const auto findClear=[](const RuntimeWindowState& s,bool water){for(int i=0;i<static_cast<int>(s.theater.getTotalTileSlots());++i){
        TerrainCell c;c.tileIndex=i;c.subTileIndex=0;const auto t=runtimeTerrainInfoForCell(s,c);
        if((water?t.water:t.passable&&!t.water)&&!t.shore&&!t.cliff&&!t.bridge&&!t.ramp)return i;}return -1;};
    const auto overlay=[&](TerrainCell& cell,const std::string& id,int frame){const int i=ordinal(id);
        if(i<0)return fail("stock overlay missing: "+id);cell.hasOverlay=true;cell.overlayIndex=i;cell.overlayFrame=frame;
        cell.overlayType=id;cell.overlayImage=stock->rules.getOverlayTypes()[i].image;cell.overlayIsOre=cell.overlayIsGem=false;return true;};
    const auto synthetic=[&](bool high,bool wood,bool alongX,int hz){Scene scene;scene.high=high;scene.wood=wood;scene.alongX=alongX;
        scene.first=alongX?RuntimeCellPoint{31,32}:RuntimeCellPoint{32,31};scene.last=alongX?RuntimeCellPoint{39,32}:RuntimeCellPoint{32,39};
        scene.state=std::make_unique<RuntimeWindowState>();auto& s=*scene.state;seedRuntimeLoadScratch(s,*stock,false);prepare(s,hz);
        scene.land=findClear(s,false);const int water=findClear(s,true);if(scene.land<0||water<0){fail("native clear/water tiles");return scene;}
        s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=-1;s.mapFacts={};s.mapFactsLoaded=false;
        s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        for(int y=20;y<=68;++y)for(int x=20;x<=68;++x){TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;
            cell.tileIndex=scene.land;cell.subTileIndex=0;s.world.accessTerrainCells().push_back(cell);}
        const auto mutableAt=[&](RuntimeCellPoint p)->TerrainCell*{for(auto& c:s.world.accessTerrainCells())if(c.x==p.x&&c.y==p.y)return &c;return nullptr;};
        const auto stamp=[&](RuntimeCellPoint boundary,bool start){
            const int set=s.theater.getGeneralTileSetIndex(wood?"WoodBridgeSet":"BridgeSet");
            const int headOrdinal=s.theater.getGeneralTileSetIndex(alongX?(start?"BridgeTopLeft1":"BridgeBottomRight1"):(start?"BridgeTopRight1":"BridgeBottomLeft1"));
            const auto found=std::find_if(s.theater.getTileSets().begin(),s.theater.getTileSets().end(),[&](const TheaterTileSet& t){return t.index==set;});
            if(found==s.theater.getTileSets().end()||headOrdinal<=0||headOrdinal>found->tilesInSet)return fail("original high head tileset/ordinal");
            const int tile=found->startTileIndex+headOrdinal-1;const auto reference=s.theater.resolveTileIndex(tile);
            std::vector<std::uint8_t> bytes;if(!readNative(s,toLowerAsciiLocal(reference.expectedFileName),bytes)||bytes.size()<20)return false;
            const auto u32=[&](std::size_t p){return static_cast<unsigned>(bytes[p])|(static_cast<unsigned>(bytes[p+1])<<8)
                |(static_cast<unsigned>(bytes[p+2])<<16)|(static_cast<unsigned>(bytes[p+3])<<24);};
            const int w=static_cast<int>(u32(0)),h=static_cast<int>(u32(4));
            if(w<=0||h<=0||w>16||h>16||bytes.size()<16+static_cast<std::size_t>(w*h)*4)return fail("original head TMP grid");
            const RuntimeCellPoint origin{boundary.x-(alongX?(start?w-1:0):w/2),boundary.y-(alongX?h/2:(start?h-1:0))};
            for(int dy=0;dy<h;++dy)for(int dx=0;dx<w;++dx){auto* cell=mutableAt({origin.x+dx,origin.y+dy});
                if(!cell)return fail("head stamp outside fixture");cell->tileIndex=tile;cell->subTileIndex=dy*w+dx;cell->level=0;}
            topology<<label<<" head tile="<<tile<<" native="<<reference.expectedFileName<<" grid="<<w<<'x'<<h<<" origin="<<origin.x<<','<<origin.y<<'\n';return true;};
        for(int separate:{0,16}) {
            const RuntimeCellPoint begin{alongX?30:32+separate,alongX?32+separate:30},end{alongX?40:32+separate,alongX?32+separate:40};
            if(high&&(!stamp(begin,true)||!stamp(end,false)))return scene;
            for(int axial=30;axial<=40;++axial)for(int lane=0;lane<3;++lane){const RuntimeCellPoint p{
                alongX?axial:32+separate+lane,alongX?32+separate+lane:axial};auto* cell=mutableAt(p);if(!cell){fail("native bridge row missing");return scene;}
                if(axial>30&&axial<40){cell->tileIndex=water;if(!high||lane==0){const auto id=high?
                    (wood?(alongX?"BRIDGEB1":"BRIDGEB2"):(alongX?"BRIDGE1":"BRIDGE2")):lowId(wood,alongX?0:9);
                    if(!overlay(*cell,id,high?(alongX?0:9):lane))return scene;}}
                else if(!high&&!overlay(*cell,lowId(wood,alongX?(axial==30?20:18):(axial==30?22:24)),lane))return scene;
            }
            for(bool start:{true,false}){const auto boundary=start?begin:end;const RuntimeCellPoint point{
                boundary.x+(alongX?(start?-3:3):-1),boundary.y+(alongX?-1:(start?-3:3))};
                auto* hut=addRuntimeObjectInstanceForOwner(s,"Neutral",RuntimeBuildKind::Building,"CABHUT",point,std::nullopt,false);
                if(!hut){fail("stock CABHUT creation");return scene;}hut->mission="Sleep";if(!separate&&start)scene.hut=hut->instanceId;}
        }
        initialize(s);s.destroyableBridges=true;return scene;};
    const auto actual=[&](bool noHut,int hz){Scene scene;scene.actual=true;scene.high=!noHut;scene.alongX=!noHut;
        scene.state=std::make_unique<RuntimeWindowState>();auto& s=*scene.state;s.config=stock->config;
        const std::string map=noHut?"mapsmd03/sov04dmd.map":"mapsmd03/all02umd.map";
        s.config.defaultMapPath=pathString(std::filesystem::path(runtimeAssetRootPath(s.config))/map);
        s.report=buildRuntimeStartupReport(s.config,&s.world,&s.theater,&s.art,&s.rules,&s.mapFacts);
        if(!s.report.success){fail("original map: "+s.report.errorMessage);return scene;}s.mapFactsLoaded=!s.mapFacts.cells.empty();prepare(s,hz);
        std::erase_if(s.world.accessObjects(),[&](const WorldObjectInstance& o){const auto* b=findRuntimeBuildingTypeById(s.rules,o.type);return !b||!b->bridgeRepairHut;});
        initialize(s);const bool authored=s.destroyableBridges;s.destroyableBridges=true;scene.land=findClear(s,false);
        topology<<label<<" actual="<<map<<" loadedDestroyable="<<authored<<" fixtureDestroyable="<<s.destroyableBridges
            <<" originalHuts="<<s.world.getObjects().size()<<" registered="<<s.bridgeRepairSpansByHut.size()<<'\n';
        if(noHut){scene.wood=false;scene.alongX=false;scene.first={95,53};scene.last=scene.first;
            if(!s.world.getObjects().empty()){fail("original sov04 unexpectedly has CABHUT");return scene;}}
        else {scene.first={132,150};scene.last={158,150};
            for(const auto& hut:s.world.getObjects())if(hut.x==129&&hut.y==148)scene.hut=hut.instanceId;
            if(scene.hut<0||s.world.getObjects().size()!=12){fail("original all02 CABHUT inventory/upper hut");return scene;}
            for(int y:{150,155})for(int x=132;x<=158;++x){const auto found=std::find_if(s.mapFacts.cells.begin(),s.mapFacts.cells.end(),
                [&](const MapCellFacts& c){return c.x==x&&c.y==y;});const auto* loaded=terrain(s,{x,y});
                if(found==s.mapFacts.cells.end()||!found->hasOverlay||found->overlay.overlayIndex!=24||found->overlay.frameIndex!=0
                    ||!loaded||loaded->overlayIndex!=found->overlay.overlayIndex||loaded->overlayFrame!=found->overlay.frameIndex)
                    {fail("original two independent 27-anchor all02 strips");return scene;}}
        }return scene;};
    const auto captureOriginal=[&](Scene& scene){auto& s=*scene.state;scene.original=s.world.getTerrainCells();scene.originalHealth=s.bridgeHealthByCell;
        for(const auto& object:s.world.getObjects())if(object.type=="CABHUT")scene.huts.push_back(object);
        for(const auto& [id,span]:s.bridgeRepairSpansByHut){topology<<label<<" registered hut="<<id<<" heads="<<span.startHead.x<<','<<span.startHead.y
            <<"->"<<span.endHead.x<<','<<span.endHead.y<<" cells="<<span.cells.size()<<'\n';}topology.flush();};
    const auto source=[&](Scene& scene,const Capability& c){auto& s=*scene.state;
        const auto* hut=findObjectById(s,scene.hut);RuntimeCellPoint point=hut?RuntimeCellPoint{hut->x-5,hut->y-2}:RuntimeCellPoint{scene.first.x-4,scene.first.y-3};
        const auto kind=std::string(c.type)=="FV"||std::string(c.type)=="DTRUCK"?RuntimeBuildKind::Unit:RuntimeBuildKind::Infantry;
        const auto* type=s.rules.findUnitType(kind==RuntimeBuildKind::Unit?"Vehicle":"Infantry",c.type);if(!type)return fail("source stock type");
        const auto choose=[&](RuntimeCellPoint desired)->std::optional<RuntimeCellPoint>{for(int radius=0;radius<=8;++radius)for(int dy=-radius;dy<=radius;++dy)
            for(int dx=-radius;dx<=radius;++dx){if(std::max(std::abs(dx),std::abs(dy))!=radius)continue;RuntimeCellPoint p{desired.x+dx,desired.y+dy};
                const auto* cell=terrain(s,p);if(!cell||bridge(*cell))continue;const auto info=runtimeTerrainInfoForCell(s,*cell);
                if(info.passable&&!info.water&&!info.shore&&!info.cliff&&!info.ramp){bool occupied=false;
                    for(const auto& o:s.world.getObjects())if(o.alive&&o.x==p.x&&o.y==p.y)occupied=true;if(!occupied)return p;}}return std::nullopt;};
        const auto landing=choose(point);if(!landing)return fail("native land source placement unavailable");
        auto* unit=addRuntimeObjectInstance(s,kind,c.type,*landing);if(!unit||unit->health!=type->strength)return fail("original full-health source creation");
        scene.source=unit->instanceId;scene.spawn=*landing;unit->mission="Sleep";refreshRuntimeObjectOccupancyAfterLocalChange(s);
        if(*c.passenger){const auto p=choose({landing->x+1,landing->y});if(!p)return fail("native boarding placement");
            auto* passenger=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,c.passenger,*p);if(!passenger)return fail("original IFV passenger");
            scene.passenger=passenger->instanceId;passenger->mission="Sleep";s.selectedObjectIds={scene.passenger};
            const auto diagnoseBoarding=[&](const std::string& why){const auto* vehicle=findRuntimeObjectByIdIncludingInactive(s,scene.source);
                const auto* passenger=findRuntimeObjectByIdIncludingInactive(s,scene.passenger);
                const auto pending=s.pendingTransportTargets.find(scene.passenger);const auto behavior=s.behaviors.find(scene.passenger);
                std::ostringstream detail;detail<<why<<" FV="<<scene.source<<" owner="<<(vehicle?vehicle->owner:"missing")
                    <<" alive="<<(vehicle&&vehicle->alive)<<" health="<<(vehicle?vehicle->health:-1)<<" mode="<<(vehicle?vehicle->activeGunnerMode:-1)
                    <<" weaponIndex="<<(vehicle?vehicle->activeGunnerWeaponIndex:-1)<<" passenger="<<scene.passenger
                    <<" passengerOwner="<<(passenger?passenger->owner:"missing")<<" passengerAlive="<<(passenger&&passenger->alive)
                    <<" mission="<<(passenger?passenger->mission:"missing")<<" pending="<<(pending!=s.pendingTransportTargets.end())
                    <<" pendingHost="<<(pending==s.pendingTransportTargets.end()?-1:pending->second)
                    <<" behavior="<<(behavior==s.behaviors.end()?-1:static_cast<int>(behavior->second.kind))
                    <<" bombs="<<s.specialMechanisms.timedBombs().size()<<" passengers=";
                if(vehicle)for(int id:vehicle->transportPassengerIds)detail<<id<<':';
                detail<<" status="<<s.interactionStatus<<" economyStatus="<<s.economy.status;
                std::ofstream diagnostic(root/(label+"-boarding-failure.txt"));diagnostic<<detail.str()<<'\n';return fail(detail.str());};
            // Source preparation uses the existing native boarding queue from
            // the IFV contract. Contextual Ivan UI clicks intentionally prefer
            // planting a bomb on the friendly FV. Do not change that unrelated
            // priority, fake cargo relationships, or force immediate entry.
            const auto* vehicle=findObjectById(s,scene.source);
            if(!vehicle||!queueRuntimeTransportTarget(s,*vehicle)||!s.pendingTransportTargets.contains(scene.passenger)
                ||s.pendingTransportTargets.at(scene.passenger)!=scene.source)return diagnoseBoarding("native boarding queue rejected");
            bool boarded=false;for(int ticks=0;ticks<s.config.fixedTickHz*15&&!boarded;++ticks){step(s);vehicle=findObjectById(s,scene.source);
                boarded=vehicle&&vehicle->transportPassengerIds==std::vector<int>{scene.passenger};}
            vehicle=findObjectById(s,scene.source);if(!boarded||!vehicle||vehicle->activeGunnerMode!=(std::string(c.passenger)=="IVAN"?7:11))return diagnoseBoarding("natural original IFV mode not activated");
            const auto profile=runtimeWeaponProfileForObject(s,*vehicle,nullptr);if(!profile.weapon||profile.weapon->id!=c.weapon)return fail("original boarded IFV weapon differs");
        }return true;};
    const auto check=[&](const Scene& scene,RuntimeWindowState& s,bool collapsed){
        if(s.rules.getDetailRulesHash()!=rulesHash)return fail("stock rules changed");
        const int first=scene.alongX?scene.first.x:scene.first.y,last=scene.alongX?scene.last.x:scene.last.y;
        int bodies=0;
        for(const auto& before:scene.original){const auto* now=terrain(s,{before.x,before.y});if(!now)return fail("original terrain cell disappeared");
            const int axial=scene.alongX?before.x:before.y,lane=scene.alongX?before.y-scene.first.y:before.x-scene.first.x;
            const bool body=axial>=first&&axial<=last&&lane>=0&&lane<(scene.high?1:3)&&bridge(before);
            const bool head=!scene.high&&(axial==first-1||axial==last+1)&&lane>=0&&lane<3&&bridge(before);
            TerrainCell expected=before;
            if(collapsed&&body){++bodies;
                if(scene.high){if(axial==first||axial==last)expected.overlayFrame=axial==first?(scene.alongX?8:16):(scene.alongX?7:17);
                    else{expected.hasOverlay=false;expected.overlayIndex=-1;expected.overlayFrame=0;expected.overlayType.clear();expected.overlayImage.clear();expected.overlayIsOre=expected.overlayIsGem=false;}}
                else if(!overlay(expected,lowId(scene.wood,scene.alongX?26:27),before.overlayFrame))return false;
            }else if(collapsed&&head){const int base=scene.wood?74:205;int offset=before.overlayIndex-base;if(offset%2==0)++offset;
                if(!overlay(expected,lowId(scene.wood,offset),before.overlayFrame))return false;}
            if(!fields(*now,expected))return fail("whole/isolated native terrain differs at "+std::to_string(before.x)+","+std::to_string(before.y)
                +" expected="+expected.overlayType+"/"+std::to_string(expected.overlayFrame)+" actual="+now->overlayType+"/"+std::to_string(now->overlayFrame));
        }
        if(collapsed&&bodies!=(last-first+1)*(scene.high?1:3))return fail("independent whole span body count differs");
        for(const auto& before:scene.huts){const auto* hut=findRuntimeObjectByIdIncludingInactive(s,before.instanceId);
            if(!hut||!hut->alive||hut->health!=before.health||hut->health!=2000||hut->owner!=before.owner||hut->x!=before.x||hut->y!=before.y
                ||s.deathWeapons.hasTriggered(before.instanceId))return fail("original CABHUT life/owner/position changed");}
        if(collapsed)for(int axial=first;axial<=last;++axial){const RuntimeCellPoint p{scene.alongX?axial:scene.first.x,scene.alongX?scene.first.y:axial};
            const auto hp=s.bridgeHealthByCell.find(runtimeCellKey(p.x,p.y));if(hp==s.bridgeHealthByCell.end()||hp->second!=0)return fail("whole span HP anchor not zero");
            for(int lane=0;lane<3;++lane){const RuntimeCellPoint cell{p.x+(scene.alongX?0:lane),p.y+(scene.alongX?lane:0)};
                const auto info=s.terrainInfoByCell.find(runtimeCellKey(cell.x,cell.y));if(info==s.terrainInfoByCell.end()||info->second.bridge){
                    const auto* native=terrain(s,cell);std::ostringstream detail;
                    detail<<"collapsed whole physical lane retains bridge deck cell="<<cell.x<<','<<cell.y
                        <<" lane="<<lane<<" anchor="<<p.x<<','<<p.y<<" tile="<<(native?native->tileIndex:-1)
                        <<" subtile="<<(native?static_cast<int>(native->subTileIndex):-1)
                        <<" overlay="<<(native?native->overlayType:"missing")<<" overlayFrame="<<(native?native->overlayFrame:-1)
                        <<" nativeLandType="<<(info==s.terrainInfoByCell.end()?-1:static_cast<int>(info->second.movementLandType))
                        <<" kind="<<(info==s.terrainInfoByCell.end()?-1:static_cast<int>(info->second.kind))
                        <<" bridge="<<(info!=s.terrainInfoByCell.end()&&info->second.bridge);
                    return fail(detail.str());}}}
        for(const auto& [key,hp]:scene.originalHealth){const int x=key%1000,y=key/1000;
            const bool target=scene.alongX?y==scene.first.y&&x>=first&&x<=last:x==scene.first.x&&y>=first&&y<=last;
            if(collapsed&&target)continue;const auto it=s.bridgeHealthByCell.find(key);if(it==s.bridgeHealthByCell.end()||it->second!=hp)return fail("other bridge health changed");}
        for(const auto& [key,hp]:s.bridgeHealthByCell){const int x=key%1000,y=key/1000;
            const bool target=scene.alongX?y==scene.first.y&&x>=first&&x<=last:x==scene.first.x&&y>=first&&y<=last;
            if(collapsed&&target)continue;const auto it=scene.originalHealth.find(key);if(it==scene.originalHealth.end()||it->second!=hp)return fail("outside bridge acquired damage state");}
        return true;};
    std::map<std::string,RuntimeShpFrameImage> decoded;
    constexpr int width=1400,height=1000;const RECT viewport{0,0,width,height};
    HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=width;
    bi.bmiHeader.biHeight=-height;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;void* bits=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dc||!bitmap||!bits){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return fail("native pixel DIB");}
    const auto previous=SelectObject(dc,bitmap);struct Cleanup{HDC dc;HBITMAP bitmap;HGDIOBJ previous;
        ~Cleanup(){SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);}}cleanup{dc,bitmap,previous};
    auto* pixels=static_cast<std::uint32_t*>(bits);
    const auto nativePixels=[&](Scene& scene,RuntimeWindowState& s,const std::string& phase){RuntimeMapRenderContext context;
        context.rules=&s.rules;context.art=&s.art;context.theater=&s.theater;context.assetRootPath=runtimeAssetRootPath(s.config);
        context.palettePath=runtimePalettePath(s.config,s.report.map);context.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
        context.surfacePixels=pixels;context.surfaceWidth=width;context.surfaceHeight=height;context.shadowsEnabled=false;
        RuntimeMapCamera camera;camera.initialized=true;camera.tileWidth=60;camera.tileHeight=30;
        const RuntimeCellPoint center{(scene.first.x+scene.last.x)/2,(scene.first.y+scene.last.y)/2};
        camera.worldOffsetX=(center.x-center.y)*30-width/2;camera.worldOffsetY=(center.x+center.y)*15-height/2;
        GameWorld probe=s.world;probe.accessObjects().clear();probe.accessTerrainObjects().clear();probe.accessSmudges().clear();probe.accessTerrainCells().clear();
        const int length=std::max(std::abs(scene.first.x-scene.last.x),std::abs(scene.first.y-scene.last.y));
        for(int y=center.y-length/2-9;y<=center.y+length/2+9;++y)for(int x=center.x-length/2-9;x<=center.x+length/2+9;++x){
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;cell.tileIndex=scene.land;cell.subTileIndex=0;
            const auto* actualCell=terrain(s,{x,y});if(actualCell&&bridge(*actualCell)){cell=*actualCell;cell.tileIndex=scene.land;cell.subTileIndex=0;cell.level=0;}
            probe.accessTerrainCells().push_back(cell);}
        GameWorld bare=probe;for(auto& cell:bare.accessTerrainCells()){cell.hasOverlay=false;cell.overlayIndex=-1;cell.overlayType.clear();cell.overlayImage.clear();}
        RuntimeMapRenderCache bareCache,actualCache,referenceCache;std::fill(pixels,pixels+width*height,0x203040U);auto fixed=camera;
        const auto stats=drawRuntimeMap(dc,viewport,bare,context,fixed,bareCache);GdiFlush();if(!stats.rendered||!stats.texturedTerrainCells)return fail("bare native pixel reference");
        std::vector<std::uint32_t> expected(pixels,pixels+width*height);for(auto& color:expected)color&=0xffffffU;
        std::vector<RuntimePaletteColor> palette;std::string paletteSource;
        if(!loadRuntimePaletteResource(context,referenceCache,"iso"+extension(s)+".pal",palette,paletteSource,error)||palette.size()!=256)return fail("original ISO palette");
        for(const auto& cell:probe.getTerrainCells()){if(!bridge(cell))continue;
            if(cell.overlayType==lowId(scene.wood,26)||cell.overlayType==lowId(scene.wood,27))continue;
            const std::string filename=toLowerAsciiLocal(cell.overlayImage)+"."+extension(s),key=filename+":"+std::to_string(cell.overlayFrame);
            auto found=decoded.find(key);if(found==decoded.end()){std::vector<std::uint8_t> bytes;if(!readNative(s,filename,bytes))return false;
                RuntimeShpFrameImage image;if(!decodeRuntimeShpFrameFile(pathString(root/"native-original"/filename),cell.overlayFrame,image,error))return fail("raw native SHP decode: "+error);
                found=decoded.emplace(key,std::move(image)).first;}const auto& image=found->second;
            const bool high=cell.overlayImage=="BRIDGE"||cell.overlayImage=="BRIDGB";
            const int left=(cell.x-cell.y)*30-camera.worldOffsetX-image.globalWidth/2+image.offsetX-(high?30:0);
            const int top=(cell.x+cell.y)*15-camera.worldOffsetY-image.globalHeight/2+image.offsetY-(high?16:0);
            for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x){const auto index=image.pixels[static_cast<std::size_t>(y)*image.width+x];if(!index)continue;
                const int px=left+x,py=top+y;if(px<0||py<0||px>=width||py>=height)continue;const auto& c=palette[index];
                expected[static_cast<std::size_t>(py)*width+px]=(static_cast<std::uint32_t>(c.r)<<16)|(static_cast<std::uint32_t>(c.g)<<8)|c.b;}}
        for(int warm=0;warm<2;++warm){std::fill(pixels,pixels+width*height,0x203040U);fixed=camera;
            const auto actualStats=drawRuntimeMap(dc,viewport,probe,context,fixed,actualCache);GdiFlush();std::vector<std::uint32_t> actualPixels(pixels,pixels+width*height);
            for(auto& color:actualPixels)color&=0xffffffU;++pixelsChecked;
            if(!actualStats.rendered||actualPixels!=expected){writeRuntimeSmokeBmp(pathString(root/(label+"-"+phase+"-actual.bmp")),actualPixels.data(),width,height,error);
                writeRuntimeSmokeBmp(pathString(root/(label+"-"+phase+"-expected.bmp")),expected.data(),width,height,error);return fail("original whole-bridge SHP pixels "+phase+" warm="+std::to_string(warm));}
            if(!warm)writeRuntimeSmokeBmp(pathString(root/(label+"-"+phase+"-native.bmp")),actualPixels.data(),width,height,error);}
        return true;};
    const auto run=[&](Scene scene,const Capability& c,const std::string& name){label=name;
        if(filter&&*filter&&label.find(filter)==std::string::npos)return true;stage="native fixture";
        if(!error.empty()||!scene.state||scene.hut<0||scene.land<0)return fail("fixture: "+error);
        auto& s=*scene.state;RuntimeSnapshotActiveStateScope active(s);const auto span=s.bridgeRepairSpansByHut.find(scene.hut);
        if(span==s.bridgeRepairSpansByHut.end())return fail("actual CABHUT not naturally registered");
        std::set<int> required;for(const auto& cell:span->second.cells)if(cell.requiresOverlay)required.insert(runtimeCellKey(cell.point.x,cell.point.y));
        const int first=scene.alongX?scene.first.x:scene.first.y,last=scene.alongX?scene.last.x:scene.last.y;
        std::set<int> independentlyRequired;for(int a=first;a<=last;++a)for(int lane=0;lane<(scene.high?1:3);++lane)
            independentlyRequired.insert(runtimeCellKey(scene.alongX?a:scene.first.x+lane,scene.alongX?scene.first.y+lane:a));
        if(required!=independentlyRequired)return fail("natural registered span differs from independent original strip geometry");
        captureOriginal(scene);if(!source(scene,c)||!check(scene,s,false))return false;
        const bool render=s.config.fixedTickHz==30;if(render&&!nativePixels(scene,s,"intact"))return false;
        stage="approach";s.selectedObjectIds={scene.source};const auto* hut=findObjectById(s,scene.hut);if(!hut)return fail("original target hut missing");
        const auto cursorBefore=runtimeTargetCursorIntent(s,*hut);
        const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,{hut->x,hut->y},hut);
        if(!dispatched){const auto* actor=findRuntimeObjectByIdIncludingInactive(s,scene.source);
            const auto pending=s.pendingTimedBombIntents.find(scene.source);const auto behavior=s.behaviors.find(scene.source);
            const auto* target=findRuntimeObjectByIdIncludingInactive(s,scene.hut);
            const auto* building=target?findRuntimeBuildingTypeById(s.rules,target->type):nullptr;
            std::ostringstream detail;detail<<"real UI hut command rejected: dispatched="<<dispatched
                <<" lastValid="<<s.lastCommand.valid<<" accepted="<<s.lastCommand.acceptedCount<<" rejected="<<s.lastCommand.rejectedCount
                <<" verb="<<s.lastCommand.verb<<" selected="<<s.selectedObjectIds.size()<<" localOwner="<<s.economy.owner
                <<" source="<<scene.source<<" sourceOwner="<<(actor?actor->owner:"missing")<<" alive="<<(actor&&actor->alive)
                <<" sourceHealth="<<(actor?actor->health:-1)<<" sourceCell="<<(actor?actor->x:-1)<<','<<(actor?actor->y:-1)
                <<" c4="<<(actor&&runtimeObjectHasC4Capability(s.rules,*actor))
                <<" hut="<<scene.hut<<" hutAlive="<<(target&&target->alive)<<" hutHealth="<<(target?target->health:-1)
                <<" CanC4="<<(building&&building->canC4)<<" Bombable="<<(building&&building->bombable)
                <<" destroyable="<<s.destroyableBridges<<" registered="<<s.bridgeRepairSpansByHut.contains(scene.hut)
                <<" cursorBefore="<<static_cast<int>(cursorBefore)<<" pending="<<(pending!=s.pendingTimedBombIntents.end())
                <<" pendingTarget="<<(pending==s.pendingTimedBombIntents.end()?-1:pending->second.targetObjectId)
                <<" behavior="<<(behavior==s.behaviors.end()?-1:static_cast<int>(behavior->second.kind))
                <<" behaviorTarget="<<(behavior==s.behaviors.end()?-1:behavior->second.targetObjectId)
                <<" path="<<(behavior==s.behaviors.end()?0:behavior->second.path.size())
                <<" timer="<<s.specialMechanisms.timedBombs().contains(scene.hut)
                <<" status="<<s.interactionStatus<<" economyStatus="<<s.economy.status;
            std::ofstream diagnostic(root/(label+"-approach-failure.txt"));diagnostic<<detail.str()<<'\n';return fail(detail.str());}
        if(c.c4&&(!s.pendingTimedBombIntents.contains(scene.source)||s.specialMechanisms.timedBombs().contains(scene.hut)))return fail("C4 did not queue real move intent before planting");
        if(c.c4){step(s);step(s);
            if(!s.pendingTimedBombIntents.contains(scene.source)||!s.specialMechanisms.timedBombs().empty())return fail("C4 approach fixture reached hut before real movement checkpoint");}
        auto ordinary=checkpoint(s,false),canonical=checkpoint(s,true);if(!ordinary||!canonical)return fail(error);
        bool armedSaved=false,sawWeapon=false,collapsed=false;int deadline=-1,collapseTick=-1;
        const auto replicas=[&](){return std::vector<std::pair<std::string,RuntimeWindowState*>>{{"original",&s},{"ordinary",ordinary.get()},{"canonical",canonical.get()}};};
        const auto countCollapsed=[&](const RuntimeWindowState& q){int count=0;for(int a=first;a<=last;++a){const int key=runtimeCellKey(scene.alongX?a:scene.first.x,scene.alongX?scene.first.y:a);
            const auto hp=q.bridgeHealthByCell.find(key);if(hp!=q.bridgeHealthByCell.end()&&hp->second==0)++count;}return count;};
        for(int ticks=0;ticks<s.config.fixedTickHz*100&&!collapsed;++ticks){for(auto [replica,q]:replicas())step(*q);
            for(const auto& p:s.projectiles)if(p.attackerObjectId==scene.source&&p.weaponId==c.weapon)sawWeapon=true;
            for(const auto& p:s.recentExpiredProjectiles)if(p.attackerObjectId==scene.source&&p.weaponId==c.weapon)sawWeapon=true;
            const auto bomb=s.specialMechanisms.timedBombs().find(scene.hut);
            if(bomb!=s.specialMechanisms.timedBombs().end()){
                if(!c.c4&&!c.ivan)return fail("native immediate demolition unexpectedly planted a timer");
                const int delay=c.c4?std::max(1,static_cast<int>(std::floor(c4Minutes*60.0*s.config.fixedTickHz))):
                    std::max(1,static_cast<int>(std::lround(static_cast<double>(ivanFrames)*s.config.fixedTickHz/15.0)));
                if(bomb->second.sourceObjectId!=scene.source||bomb->second.detonateTick-bomb->second.attachedTick!=delay
                    ||bomb->second.kind!=(c.c4?TimedBombKind::C4:TimedBombKind::Ivan)
                    ||bomb->second.forceDestruction!=c.c4||bomb->second.damage!=(c.c4?std::numeric_limits<int>::max():ivanDamage))return fail("raw original timer/weapon identity differs");
                deadline=bomb->second.detonateTick;if(!armedSaved){stage="armed";
                    ordinary=checkpoint(s,false);canonical=checkpoint(s,true);if(!ordinary||!canonical)return fail(error);armedSaved=true;
                    // The original Ivan explosion can hurt its planter. Use a
                    // real move order after the armed checkpoint, never a pose
                    // assignment or immunity edit, to leave that blast radius.
                    if(c.ivan)for(auto [replica,q]:replicas()){q->selectedObjectIds={scene.source};
                        if(!dispatchRuntimeTargetOrderFromUi(*q,RuntimeMultiplayerGameplayTargetOrderKind::Move,scene.spawn)
                            ||q->lastCommand.acceptedCount!=1)return fail(replica+" real armed-bomb retreat rejected");}}
            }
            const int changed=countCollapsed(s);collapsed=changed!=0;
            for(auto [replica,q]:replicas()){
                if(countCollapsed(*q)!=changed||q->tick!=s.tick)return fail(replica+" restored natural demolition timing differs");
                const auto* actor=findRuntimeObjectByIdIncludingInactive(s,scene.source);
                const auto* restored=findRuntimeObjectByIdIncludingInactive(*q,scene.source);
                if(!actor||!restored||actor->alive!=restored->alive||actor->health!=restored->health
                    ||actor->x!=restored->x||actor->y!=restored->y||actor->level!=restored->level||actor->facing!=restored->facing
                    ||actor->hasRenderPosition!=restored->hasRenderPosition
                    ||(actor->hasRenderPosition&&(std::abs(actor->renderX-restored->renderX)>1e-9
                        ||std::abs(actor->renderY-restored->renderY)>1e-9||std::abs(actor->renderLevel-restored->renderLevel)>1e-9)))
                    return fail(replica+" real source movement/XYZ/health differs after checkpoint");
                const auto other=q->specialMechanisms.timedBombs().find(scene.hut);
                if((bomb==s.specialMechanisms.timedBombs().end())!=(other==q->specialMechanisms.timedBombs().end())
                    ||(bomb!=s.specialMechanisms.timedBombs().end()&&(other->second.detonateTick!=bomb->second.detonateTick
                        ||other->second.attachedTick!=bomb->second.attachedTick)))return fail(replica+" restored bomb lifetime differs");
            }
            if(collapsed){collapseTick=s.tick;if(changed!=last-first+1)return fail("natural blast collapsed only part of its independent whole span");}
        }
        stage="broken";if(!collapsed||((c.c4||c.ivan)&&(!armedSaved||deadline!=collapseTick))||(!c.c4&&!sawWeapon))
            return fail("natural demolition not proven: collapsed="+std::to_string(collapsed)+" armed="+std::to_string(armedSaved)
                +" weapon="+std::to_string(sawWeapon)+" deadline="+std::to_string(deadline)+" collapse="+std::to_string(collapseTick)
                +" status="+s.simulationStatus+" / "+s.interactionStatus);
        for(auto [replica,q]:replicas()){
            if(!check(scene,*q,true))return false;
            if(q->specialMechanisms.timedBombs().contains(scene.hut))return fail(replica+" expired bridge bomb remained attached");
            const auto* actor=findRuntimeObjectByIdIncludingInactive(*q,scene.source);
            if(!actor||(c.suicide?(actor->alive||actor->health>0):(!actor->alive||actor->health<=0)))return fail(replica+" original suicide/survival differs");
            if(c.suicide&&scene.passenger>=0){const auto* passenger=findRuntimeObjectByIdIncludingInactive(*q,scene.passenger);
                if(!passenger||passenger->alive||passenger->health>0)return fail("original suicide IFV passenger survived");}
            records<<label<<','<<replica<<','<<q->tick<<",broken,"<<scene.source<<','<<scene.hut<<','<<last-first+1<<','<<last-first+1
                <<",0,"<<actor->alive<<','<<scene.huts.size()<<','<<q->bridgeRepairSpansByHut.size()<<'\n';}
        if(render&&!nativePixels(scene,s,"broken"))return false;
        ordinary=checkpoint(s,false);canonical=checkpoint(s,true);if(!ordinary||!canonical)return fail(error);
        for(auto [replica,q]:replicas())if(!check(scene,*q,true))return false;
        if(render&&!nativePixels(scene,*canonical,"canonical-broken"))return false;
        stage="continued";for(int ticks=0;ticks<s.config.fixedTickHz*2;++ticks)for(auto [replica,q]:replicas())step(*q);
        for(auto [replica,q]:replicas())if(!check(scene,*q,true)||!runtimeBridgeRepairHutNeedsRepair(*q,scene.hut))return fail(replica+" destroyed bridge lost repair availability or changed again");
        ++passed;records.flush();std::cout<<"Bridge hut demolition "<<label<<" PASS natural="<<(c.c4?"C4":c.ivan?"Ivan":"weapon")
            <<" wholeAnchors="<<last-first+1<<" collapseTick="<<collapseTick<<" hutsPreserved="<<scene.huts.size()
            <<(*c.passenger?" FVsource=nativeBoardingQueue+naturalTicks":"")
            <<" otherBridges=exact ordinary/canonical/approach/armed/broken/nativePixels=checked\n";return true;};
    int geometry=0;
    for(int hz:{30,60}){geometry=0;for(bool high:{true,false})for(bool wood:{false,true})for(bool axis:{true,false}){
        const auto& c=capabilities[geometry++%7];const auto name=std::string(high?"high":"low")+"-"+(wood?"wood":"concrete")+"-"+(axis?"X":"Y")+"-"+c.name+"-"+std::to_string(hz);
        label=name;error.clear();if(filter&&*filter&&name.find(filter)==std::string::npos)continue;if(!run(synthetic(high,wood,axis,hz),c,name))return false;}
        for(std::size_t i=7;i<capabilities.size();++i){const auto& c=capabilities[i];const auto name=std::string("stock-")+c.name+"-"+std::to_string(hz);
            label=name;error.clear();if(filter&&*filter&&name.find(filter)==std::string::npos)continue;if(!run(synthetic(i!=8,i==9,i!=9,hz),c,name))return false;}}
    for(std::size_t index:{0U,3U}){const auto& c=capabilities[index];const auto name=std::string("actual-all02-upper-")+c.name+"-30";
        label=name;error.clear();if(filter&&*filter&&name.find(filter)==std::string::npos)continue;if(!run(actual(false,30),c,name))return false;}
    const auto negative=[&](const Capability& c,int mode){const auto name=std::string(mode==0?"actual-sov04-nohut-":mode==1?"ordinary-":mode==2?"NoMap-":"orphan-hut-")+c.name+"-30";
        label=name;if(filter&&*filter&&name.find(filter)==std::string::npos)return true;error.clear();stage="negative native fixture";
        Scene scene=mode==0?actual(true,30):synthetic(false,false,true,30);if(!scene.state||!error.empty())return fail(error);
        auto& s=*scene.state;RuntimeSnapshotActiveStateScope active(s);if(mode==2)s.destroyableBridges=false;
        if(mode==3){auto* isolated=addRuntimeObjectInstanceForOwner(s,"Neutral",RuntimeBuildKind::Building,"CABHUT",{62,62},std::nullopt,false);
            if(!isolated)return fail("original isolated CABHUT creation");scene.hut=isolated->instanceId;isolated->mission="Sleep";
            initializeRuntimeNavigation(s);s.destroyableBridges=true;
            if(s.bridgeRepairSpansByHut.contains(scene.hut))return fail("isolated original hut unexpectedly registered to far bridge");}
        captureOriginal(scene);if(!source(scene,c))return false;s.selectedObjectIds={scene.source};
        // A bare cell has no building identity with which to choose a
        // sabotage secondary weapon. Its ordinary gun may legally fire at
        // an artificial bridge target; only verify this empty-object Primary
        // move cannot plant a bomb. The flag comes from the original weapon.
        const bool bareMove=c.c4||(*c.weapon&&parseRuntimeBool(raw.getValue(c.weapon,"SabotageCursor","no")));
        if(mode==0){
            if(bareMove){dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,scene.first);
                if(!s.pendingTimedBombIntents.empty()||!s.specialMechanisms.timedBombs().empty())return fail("bare bridge received demolition intent/timer");}
            else{const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell,scene.first);
                if(s.lastCommand.acceptedCount!=0)return fail("bare bridge accepted stock bomb/suicide weapon; dispatched="+std::to_string(dispatched));}
        }else{const auto* hut=findObjectById(s,scene.hut);if(!hut)return fail("negative hut missing");
            if(mode>=2&&runtimeTargetCursorIntent(s,*hut)!=RuntimeTargetCursorIntent::NoAction)return fail("unavailable hut advertised demolition cursor");
            dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,{hut->x,hut->y},hut);
            if(mode>=2&&(!s.pendingTimedBombIntents.empty()||!s.specialMechanisms.timedBombs().empty()||s.lastCommand.acceptedCount>0))return fail("unavailable hut accepted demolition");}
        const std::string ordinaryWeapon=mode==1?raw.getValue(c.type,"Primary",""):"";
        const auto* nativeWeapon=mode==1?s.rules.findWeaponType(ordinaryWeapon):nullptr;
        if(mode==1&&(!nativeWeapon||ordinaryWeapon!=c.weapon||nativeWeapon->suicide
            ||nativeWeapon->damage!=std::stoi(raw.getValue(ordinaryWeapon,"Damage","0"))
            ||nativeWeapon->projectile!=raw.getValue(ordinaryWeapon,"Projectile","")
            ||nativeWeapon->warhead!=raw.getValue(ordinaryWeapon,"Warhead","")))return fail("raw ordinary source weapon differs");
        bool sawOrdinaryWeapon=false,sawOrdinaryImpact=false;int observedTicks=0;
        // Ordinary fire starts out of M60 range and must approach naturally.
        // Rejected explosives retain the original three-second observation.
        const int observationBudget=s.config.fixedTickHz*(mode==1?100:3);
        const auto ordinaryActor=[&](const RuntimeProjectileEffect& p){return mode==1&&p.attackerObjectId==scene.source
            &&p.targetObjectId==scene.hut&&p.weaponId==ordinaryWeapon&&p.projectileId==nativeWeapon->projectile
            &&p.warheadId==nativeWeapon->warhead&&p.damage==nativeWeapon->damage;};
        for(;observedTicks<observationBudget;++observedTicks){step(s);
            for(const auto& p:s.projectiles)if(ordinaryActor(p))sawOrdinaryWeapon=true;
            for(const auto& p:s.recentExpiredProjectiles)if(ordinaryActor(p)){
                sawOrdinaryWeapon=true;
                if(p.lifecycle==RuntimeProjectileLifecycleState::Expired&&p.remainingTicks==0
                    &&p.currentCell.x==p.toCell.x&&p.currentCell.y==p.toCell.y)sawOrdinaryImpact=true;}
            if(mode==1&&sawOrdinaryImpact){++observedTicks;break;}}
        if(mode==1&&!sawOrdinaryImpact){const auto* actor=findRuntimeObjectByIdIncludingInactive(s,scene.source);
            const auto* hut=findRuntimeObjectByIdIncludingInactive(s,scene.hut);const auto behavior=s.behaviors.find(scene.source);
            std::ostringstream detail;detail<<"ordinary stock UI weapon did not reach immune hut; ticks="<<observedTicks
                <<" budget="<<observationBudget<<" fired="<<sawOrdinaryWeapon<<" weapon="<<ordinaryWeapon
                <<" rawRange="<<raw.getValue(ordinaryWeapon,"Range","")<<" source="<<scene.source
                <<" owner="<<(actor?actor->owner:"missing")<<" alive="<<(actor&&actor->alive)
                <<" health="<<(actor?actor->health:-1)<<" spawn="<<scene.spawn.x<<','<<scene.spawn.y
                <<" cell="<<(actor?actor->x:-1)<<','<<(actor?actor->y:-1)<<" mission="<<(actor?actor->mission:"missing")
                <<" renderXYZ="<<(actor?actor->renderX:0)<<','<<(actor?actor->renderY:0)<<','<<(actor?actor->renderLevel:0)
                <<" hut="<<scene.hut<<" hutCell="<<(hut?hut->x:-1)<<','<<(hut?hut->y:-1)
                <<" distance="<<(actor&&hut&&nativeWeapon?runtimeWeaponDistanceToTarget(s,*actor,*hut,*nativeWeapon):-1)
                <<" behavior="<<(behavior==s.behaviors.end()?-1:static_cast<int>(behavior->second.kind))
                <<" state="<<(behavior==s.behaviors.end()?-1:static_cast<int>(behavior->second.state))
                <<" target="<<(behavior==s.behaviors.end()?-1:behavior->second.targetObjectId)
                <<" path="<<(behavior==s.behaviors.end()?0:behavior->second.path.size())
                <<" moveCooldown="<<(behavior==s.behaviors.end()?-1:behavior->second.moveCooldownTicks)
                <<" valid="<<s.lastCommand.valid<<" accepted="<<s.lastCommand.acceptedCount<<" rejected="<<s.lastCommand.rejectedCount
                <<" interactionStatus="<<s.interactionStatus<<" simulationStatus="<<s.simulationStatus<<" economyStatus="<<s.economy.status;
            const auto appendActor=[&](const RuntimeProjectileEffect& p){if(p.attackerObjectId==scene.source)
                detail<<" actor{"<<p.actorId<<','<<p.weaponId<<",target="<<p.targetObjectId<<",damage="<<p.damage
                    <<",lifecycle="<<static_cast<int>(p.lifecycle)<<",remaining="<<p.remainingTicks
                    <<",cell="<<p.currentCell.x<<','<<p.currentCell.y<<",to="<<p.toCell.x<<','<<p.toCell.y<<'}';};
            for(const auto& p:s.projectiles)appendActor(p);for(const auto& p:s.recentExpiredProjectiles)appendActor(p);
            std::ofstream diagnostic(root/(label+"-ordinary-fire-failure.txt"));diagnostic<<detail.str()<<'\n';return fail(detail.str());}
        if(!check(scene,s,false)||!s.specialMechanisms.timedBombs().empty()||!s.pendingTimedBombIntents.empty())return false;
        if(mode!=1){const auto* actor=findRuntimeObjectByIdIncludingInactive(s,scene.source);if(!actor||!actor->alive)return fail("rejected demolition consumed original source");
            for(const auto& p:s.projectiles)if(p.attackerObjectId==scene.source)return fail("rejected demolition launched projectile");
            for(const auto& p:s.recentExpiredProjectiles)if(p.attackerObjectId==scene.source)return fail("rejected demolition reached impact");}
        const char* evidence=mode==0?(bareMove?"bareBridgePrimaryMove/noBomb/noWeaponActor":"bareBridgeExplosiveRejected/noBomb/noWeaponActor")
            :mode==1?"actualOrdinaryWeaponImpact/immuneHut/noWholeDemolition":mode==2?"NoMapDemolitionRejected/noBomb/noWeaponActor"
            :"orphanHutDemolitionRejected/noBomb/noWeaponActor";
        ++negativePassed;std::cout<<"Bridge hut demolition "<<label<<" PASS "<<evidence<<" bridge/HP/huts=preserved"
            <<" observedTicks="<<observedTicks<<(mode==1?" weapon="+ordinaryWeapon:"")<<'\n';return true;};
    for(const auto& c:capabilities)if(!negative(c,0))return false;
    for(const Capability c:{Capability{"E1","E1","M60",""},Capability{"FV-empty","FV","HoverMissile",""}})if(!negative(c,1))return false;
    for(std::size_t i:{0U,3U,5U})if(!negative(capabilities[i],2))return false;
    for(std::size_t i:{0U,3U,5U})if(!negative(capabilities[i],3))return false;
    if(!passed&&!negativePassed)return fail("filter matched no scenes");
    std::ofstream manifest(root/"summary.txt");manifest<<"passed="<<passed<<" negatives="<<negativePassed<<" ordinary="<<ordinarySaves
        <<" canonical="<<canonicalSaves<<" nativePixelChecks="<<pixelsChecked<<" nativeSources="<<nativeSources.size()<<'\n';
    std::cout<<"Bridge hut demolition summary PASS scenes="<<passed<<" negatives="<<negativePassed<<" ordinary="<<ordinarySaves
        <<" canonical="<<canonicalSaves<<" nativePixelChecks="<<pixelsChecked<<" stock resources unchanged\n";return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
