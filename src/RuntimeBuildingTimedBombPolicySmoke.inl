// Stock building inventory and actual UI planting. Original non-clickable
// logic buildings exercise native C4 queue / authorized Ivan attachment.
// Ordinary saves remain format 14; canonical extensions are tested separately.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeBuildingTimedBombPolicySmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    std::string label="startup", stage;
    const auto fail=[&](const std::string& why){error="Building timed bomb "+label+" ["+stage+"]: "+why;return false;};
    const char* selected=std::getenv("CONCORD_BUILDING_TIMED_BOMB_POLICY_CASE");
    const std::string filter=selected?selected:"";
    int plantBudgetSeconds=5;
    const char* budgetText=std::getenv("CONCORD_BUILDING_TIMED_BOMB_POLICY_PLANT_BUDGET_SECONDS");
    if(budgetText){
        if(!*budgetText)return fail("PLANT_BUDGET_SECONDS must be an unsigned integer in 5..120");
        int parsed=0;
        for(const char* p=budgetText;*p;++p){
            if(*p<'0'||*p>'9')return fail("PLANT_BUDGET_SECONDS must be an unsigned integer in 5..120");
            parsed=parsed*10+(*p-'0');
            if(parsed>120)return fail("PLANT_BUDGET_SECONDS must be an unsigned integer in 5..120");
        }
        if(parsed<5)return fail("PLANT_BUDGET_SECONDS must be an unsigned integer in 5..120");
        plantBudgetSeconds=parsed;
    }
    const char* shardText=std::getenv("CONCORD_BUILDING_TIMED_BOMB_POLICY_SHARD");
    int shard=-1;if(shardText&&*shardText){
        if(shardText[1]!='\0'||shardText[0]<'0'||shardText[0]>'2')return fail("SHARD must be 0, 1 or 2");
        shard=shardText[0]-'0';}
    const auto caseShard=[](const std::string& name){
        std::uint64_t hash=14695981039346656037ULL;
        for(unsigned char byte:name){hash^=byte;hash*=1099511628211ULL;}
        return static_cast<int>(hash%3ULL);};
    const auto matches=[&](const std::string& name){return (filter.empty()||name.find(filter)!=std::string::npos)
        &&(shard<0||caseShard(name)==shard);};
    const char* output=std::getenv("CONCORD_BUILDING_TIMED_BOMB_POLICY_OUTPUT");
    const std::filesystem::path root=output&&*output?output:"build/issue22_09_building_timed_bomb";
    std::filesystem::create_directories(root);
    std::ofstream csv(root/"cases.csv"), inventory(root/"inventory.csv"), checkpoints(root/"checkpoints.csv"),catalogCsv(root/"expected_catalog.csv");
    if(!csv||!inventory||!checkpoints||!catalogCsv)return fail("output unavailable");
    csv<<std::setprecision(17)<<"case,type,source,relation,order,hz,target,source_id,accepted,armed,attached,deadline,steps,target_hp,source_hp,route,UIAccepted,result,natural_damage_check,expected_direct_ivan_damage,bridge_destroyable,expected_bridge_broken,ordinary_immune_control\n";
    inventory<<"type,raw_section,registered,strength,CanC4,Bombable,Immune,Wall,bridge_hut,coverage,InvisibleInGame,image,route\n";
    checkpoints<<"case,stage,format,tick,bombs,pending,source,target,source_hp,target_hp\n";
    IniFile raw;if(!raw.loadFromFile(live.report.rulesPath))return fail("original rules unavailable");
    const auto rulesHash=live.rules.getDetailRulesHash();
    const auto* list=raw.getSection("BuildingTypes");if(!list)return fail("original BuildingTypes list");
    std::set<std::string> listed, configured, missing, registered;
    for(const auto& [key,id]:*list){(void)key;listed.insert(id);(raw.getSection(id)?configured:missing).insert(id);}
    for(const auto& b:live.rules.getBuildingTypes())registered.insert(b.id);
    if(listed.size()!=402||configured.size()!=398||configured!=registered
        ||missing!=std::set<std::string>{"CALA02","CALOND02","CASYDN01","CATIME"})
        return fail("stock registered/configured inventory differs");
    std::set<std::string> noC4, c4Sources, ivanSources, invisibleTypes, nullImageTypes, nonClickableTypes;
    std::set<std::string> inventorySeen;
    for(const auto& b:live.rules.getBuildingTypes()){
        // NAPSYA is authored twice in BuildingTypes (185 and 241). Preserve
        // the original rules, but test each of its 398 configured IDs once.
        if(!inventorySeen.insert(b.id).second)continue;
        if(raw.getValue(b.id,"CanC4","")=="no")noC4.insert(b.id);
        const bool invisible=toLowerAsciiLocal(raw.getValue(b.id,"InvisibleInGame","no"))=="yes";
        const std::string image=raw.getValue(b.id,"Image",b.id);
        const bool nullImage=toLowerAsciiLocal(image)=="null";
        if(invisible)invisibleTypes.insert(b.id);if(nullImage)nullImageTypes.insert(b.id);
        if(invisible||nullImage)nonClickableTypes.insert(b.id);
        if(invisible!=b.invisibleInGame||nullImage!=(toLowerAsciiLocal(b.image)=="null"))
            return fail("original invisible/image-null rule parsing differs: "+b.id);
        if(b.strength!=std::stoi(raw.getValue(b.id,"Strength","0"))||b.strength<=0)
            return fail("original building health: "+b.id);
        inventory<<b.id<<",1,1,"<<b.strength<<','<<raw.getValue(b.id,"CanC4","default-yes")<<','
            <<raw.getValue(b.id,"Bombable","default-yes")<<','<<raw.getValue(b.id,"Immune","default-no")<<','
            <<raw.getValue(b.id,"Wall","default-no")<<','<<b.bridgeRepairHut<<','
            <<(b.bridgeRepairHut?"real-all02-matrix":"actual-stock-planting")<<','
            <<raw.getValue(b.id,"InvisibleInGame","default-no")<<','<<image<<','
            <<((invisible||nullImage)?"hidden-c4-queue+ivan-attachment-capability":"UI")<<'\n';
    }
    if(invisibleTypes.size()!=16||nullImageTypes!=std::set<std::string>{"GAGREEN"}||nonClickableTypes.size()!=17)
        return fail("original non-clickable inventory must retain 16 invisible lights and GAGREEN null image");
    for(const auto& id:missing)inventory<<id<<",0,0,0,,,,,,excluded-missing-original-section,,,excluded\n";
    for(const auto& unit:live.rules.getInfantryTypes()){
        if(raw.getValue(unit.id,"C4","")=="yes")c4Sources.insert(unit.id);
        if(raw.getValue(unit.id,"Ivan","")=="yes")ivanSources.insert(unit.id);
    }
    if(noC4!=std::set<std::string>{"AMMOCRAT","CAMISC01","CAMISC02","CAMISC06"}
        ||c4Sources!=std::set<std::string>{"GHOST","PTROOP","TANY"}
        ||ivanSources!=std::set<std::string>{"CIVAN","IVAN"})return fail("original demolition capability inventory");
    struct ExpectedCase {bool matrix=false,hidden=false,positive=false;int saves=0;bool natural=false;};
    struct CoverageCounts {int matrix=0,ui=0,backend=0,representatives=0,positive=0,negative=0,ordinary=0,canonical=0,natural=0;};
    std::map<std::string,ExpectedCase> expectedCatalog;
    const auto addExpected=[&](const std::string& name,bool matrix,bool hidden,bool positive,int saves,bool natural=false){
        if(!expectedCatalog.emplace(name,ExpectedCase{matrix,hidden,positive,saves,natural}).second)return fail("duplicate expected case: "+name);
        return true;};
    // This catalog comes from unique original sections, not the parsed typed
    // vector or the actual execution loop. Every shard writes this same file.
    for(const auto& id:configured){
        const bool hut=toLowerAsciiLocal(raw.getValue(id,"BridgeRepairHut","no"))=="yes";
        for(const std::string source:{"TANY","IVAN"})for(const std::string relation:{"own","ally","enemy"})for(bool force:{false,true}){
            const std::string name=(hut?"bridge-":"stock-")+source+"-"+id+"-"+relation+"-"+(force?"force":"Primary")+"-30";
            if(!addExpected(name,true,nonClickableTypes.contains(id),force||relation=="enemy",0,
                    hut&&source=="IVAN"&&relation=="enemy"&&!force))return false;
        }
    }
    const std::array<std::array<std::string,4>,7> rawRepresentatives{{
        {"GAPOWR","TANY","own","force"},{"CAMISC01","GHOST","ally","force"},{"CAMISC02","PTROOP","enemy","Primary"},
        {"CAMISC06","IVAN","own","force"},{"AMMOCRAT","CIVAN","ally","force"},{"GAWALL","GHOST","enemy","Primary"},{"CAPOWR","CIVAN","enemy","Primary"}}};
    for(int hz:{30,60})for(const auto& c:rawRepresentatives){
        const std::string name="timer-"+c[1]+"-"+c[0]+"-"+c[2]+"-"+c[3]+"-"+std::to_string(hz);
        const int saves=hz==30&&(c[0]=="GAPOWR"||c[0]=="CAMISC06")?3:0;
        if(!addExpected(name,false,false,true,saves,true))return false;
    }
    if(!addExpected("unit-IVAN-MTNK-own-Primary-30",false,false,true,0)
        ||!addExpected("persistence-IVAN-GAPOWR-own-force-Stop-30",false,false,true,0)
        ||!addExpected("persistence-TANY-GAPOWR-enemy-to-ally-Primary-30",false,false,false,0)
        ||!addExpected("context-TANY-GAPOWR-same-team-no-allies-Primary-30",false,false,true,1)
        ||!addExpected("bridge-TANY-CABHUT-own-force-60",false,false,true,3,true)
        ||!addExpected("orphan-TANY-CABHUT-enemy-force-30",false,false,false,0)
        ||!addExpected("orphan-IVAN-CABHUT-enemy-force-30",false,false,true,3,true))return false;
    for(int hz:{30,60}){
        if(!addExpected("timer-IVAN-GAWALL-enemy-Primary-"+std::to_string(hz),false,false,true,0,true)
            ||!addExpected("bridge-IVAN-CABHUT-indestructible-enemy-Primary-"+std::to_string(hz),false,false,true,hz==30?3:0,true))return false;
    }
    for(const std::string source:{"IVAN","CIVAN"})
        if(!addExpected("neutral-"+source+"-GAPOWR-Primary-30",false,false,true,0,true))return false;
    if(!addExpected("timer-CIVAN-GAPOWR-own-Primary-30",false,false,false,0)
        ||!addExpected("timer-CIVAN-GAPOWR-own-force-30",false,false,true,0,true))return false;
    if(expectedCatalog.size()!=4805)return fail("independent raw catalog must contain 4805 unique cases");
    const auto addCounts=[](CoverageCounts& c,const ExpectedCase& e){
        if(e.matrix){++c.matrix;e.hidden?++c.backend:++c.ui;}else ++c.representatives;
        e.positive?++c.positive:++c.negative;c.ordinary+=e.saves;c.canonical+=e.saves;c.natural+=e.natural;};
    CoverageCounts expectedCounts;std::array<CoverageCounts,3> shardCounts;std::set<std::string> selectedExpectedNames;
    int expectedOrdinaryImmuneControls=0;
    catalogCsv<<"case,shard,matrix,route,positive,ordinary14,canonical_extension,natural_damage_check,ordinary_immune_control\n";
    for(const auto& [name,e]:expectedCatalog){
        const int bucket=caseShard(name);addCounts(shardCounts[static_cast<std::size_t>(bucket)],e);
        const std::string route=e.hidden?(name.starts_with("stock-IVAN-")?"hidden-ivan-attachment-capability":"hidden-native-executor"):"UI";
        const bool ordinaryImmuneControl=e.natural&&name.find("-IVAN-")!=std::string::npos&&name.find("-CABHUT-")!=std::string::npos;
        catalogCsv<<name<<','<<bucket<<','<<e.matrix<<','<<route<<','<<e.positive<<','<<e.saves<<','<<e.saves<<','<<e.natural<<','<<ordinaryImmuneControl<<'\n';
        if(matches(name)){selectedExpectedNames.insert(name);addCounts(expectedCounts,e);expectedOrdinaryImmuneControls+=ordinaryImmuneControl;}
    }
    catalogCsv.flush();
    for(int bucket=0;bucket<3;++bucket){const auto& c=shardCounts[static_cast<std::size_t>(bucket)];
        std::cout<<"Building timed bomb expected shard "<<bucket<<": matrix="<<c.matrix<<" UI="<<c.ui<<" hidden="<<c.backend
            <<" representatives="<<c.representatives<<" positive="<<c.positive<<" negative="<<c.negative
            <<" ordinary14="<<c.ordinary<<" canonicalExtensions="<<c.canonical<<" naturalDamage="<<c.natural<<'\n';}
    const double c4Minutes=std::stod(raw.getValue("CombatDamage","C4Delay","0"));
    const int ivanFrames=std::stoi(raw.getValue("CombatDamage","IvanTimedDelay","0"));
    const int ivanDamage=std::stoi(raw.getValue("CombatDamage","IvanDamage","0"));
    const std::string c4Warhead=raw.getValue("CombatDamage","C4Warhead",""), ivanWarhead=raw.getValue("CombatDamage","IvanWarhead","");
    if(c4Minutes!=.03||ivanFrames!=450||ivanDamage!=450||c4Warhead.empty()||ivanWarhead.empty())return fail("original timer declarations");
    // Independent retail armor/Verses reference for the two repaired direct
    // targets. Do not ask the production damage helper for its expected result.
    const auto originalIvanDirectDamage=[&](const std::string& type){
        const std::array<std::string,11> armorIds{"none","flak","plate","light","medium","heavy","wood","steel","concrete","special_1","special_2"};
        const auto armor=toLowerAsciiLocal(raw.getValue(type,"Armor","none"));
        const auto found=std::find(armorIds.begin(),armorIds.end(),armor);
        const auto verses=splitRuntimeTokenList(raw.getValue(ivanWarhead,"Verses",""));
        if(found==armorIds.end()||verses.size()!=armorIds.size())return -1;
        const auto& percent=verses[static_cast<std::size_t>(found-armorIds.begin())];
        if(percent.empty()||percent.back()!='%')return -1;
        return static_cast<int>(std::lround(ivanDamage*std::stod(percent)/100.0));
    };
    if(originalIvanDirectDamage("GAWALL")!=90||originalIvanDirectDamage("CABHUT")!=450)
        return fail("original Ivan wall/hut armor and Verses reference differs");
    std::ostringstream resourceMode;
    for(const auto& t:live.rules.getTiberiumTypes())resourceMode<<'['<<t.id<<"]\nGrowthPercentage=0\nSpreadPercentage=0\n";
    const auto resourceModeText=resourceMode.str();
    const auto resourceValid=[&](const RuntimeWindowState& s){
        if(!s.skirmishModeRulesLoaded||s.rules.getDetailRulesHash()!=rulesHash)return false;
        for(const auto& t:live.rules.getTiberiumTypes())if(runtimeOreRuleProbability(s,t,false)!=0.0||runtimeOreRuleProbability(s,t,true)!=0.0)return false;
        return true;};
    const auto facade=[](RuntimeWindowState& s){
        s.economy.owner="Americans";s.config.playerCountry="Americans";s.humanHouseAuthorities.clear();
        RuntimeHumanHouseAuthority a;a.owner=a.country="Americans";a.canonicalOwner="americans";a.localFacade=true;
        s.humanHouseAuthorities.push_back(std::move(a));};
    const auto prepare=[&](RuntimeWindowState& s,int hz){
        s.config.fixedTickHz=hz;s.config.gameMode="Battle";s.config.playerSlots.clear();s.config.skirmishLoadSavePath.clear();
        s.config.gameModeRulesOverride.clear();s.config.multiplayerActive=s.config.computerAiEnabled=s.config.noCooldown=false;
        s.config.shortGame=s.config.crates=s.config.superweapons=false;s.campaignMode=false;s.tick=200;
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=s.visionEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.economy.skirmishStartApplied=true;s.triggers.loaded=s.triggers.bootstrapComplete=true;
        s.skirmishTeamByOwner={{"americans","1"},{"french","1"},{"russians","2"}};
        s.skirmishModeAlliesAllowed=true;s.skirmishModeRulesLoaded=s.skirmishModeRules.loadFromString(resourceModeText);
        facade(s);};
    const auto initialize=[&](RuntimeWindowState& s){
        initializeRuntimeNavigation(s);ensureRuntimeSubcellsInitialized(s);ensureRuntimeSpecialMechanismsInitialized(s);
        ensureRuntimeSuperWeaponsInitialized(s);ensureRuntimeLightingInitialized(s);
        s.particles.reset(s.config.fixedTickHz);s.particlesInitialized=s.particles.alignEmptyStateTick(s.tick);
        initializeRuntimeOfflineGameplayRouting(s);return s.particlesInitialized&&resourceValid(s);};
    const auto step=[](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);++s.tick;
        advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);};
    const auto hp=[](const RuntimeWindowState& s,int id){const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);return o?o->health:-1;};
    const auto describe=[](const RuntimeWindowState& s,int id){const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);
        if(!o)return std::string("missing#")+std::to_string(id);std::ostringstream d;
        d<<std::setprecision(17)<<o->type<<'#'<<id<<" owner="<<o->owner<<" hp="<<o->health<<" alive="<<o->alive<<" XYZ="<<o->x<<','<<o->y<<','<<o->level
            <<" mission="<<o->mission<<" subcell="<<o->subCell<<" render="<<o->hasRenderPosition<<':'<<o->renderMoveActive
            <<':'<<o->renderX<<','<<o->renderY<<','<<o->renderLevel;return d.str();};
    const auto fields=[](const TerrainCell& a,const TerrainCell& b){return a.x==b.x&&a.y==b.y&&a.tileIndex==b.tileIndex
        &&a.subTileIndex==b.subTileIndex&&a.level==b.level&&a.iceGrowth==b.iceGrowth&&a.hasOverlay==b.hasOverlay
        &&a.overlayIndex==b.overlayIndex&&a.overlayFrame==b.overlayFrame&&a.overlayType==b.overlayType&&a.overlayImage==b.overlayImage
        &&a.overlayIsOre==b.overlayIsOre&&a.overlayIsGem==b.overlayIsGem;};
    auto flat=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*flat,live,false);prepare(*flat,30);
    flat->world.accessObjects().clear();flat->world.accessTerrainCells().clear();flat->world.accessTerrainObjects().clear();flat->world.accessSmudges().clear();
    flat->mapFacts={};flat->mapFactsLoaded=false;flat->terrainByCell.clear();flat->terrainInfoByCell.clear();
    flat->bridgeRepairSpansByHut.clear();flat->bridgeHealthByCell.clear();flat->report.map.size.clear();flat->report.map.localSize.clear();
    flat->report.map.width=flat->report.map.height=-1;
    for(int y=48;y<=88;++y)for(int x=48;x<=88;++x){TerrainCell c;c.x=x;c.y=y;c.cellIndex512=runtimeCellKey(x,y);c.tileIndex=c.subTileIndex=0;
        flat->world.accessTerrainCells().push_back(c);}
    if(!initialize(*flat))return fail("flat stock fixture clock/mode");
    int waterTile=-1;
    for(int i=0;i<static_cast<int>(flat->theater.getTotalTileSlots());++i){TerrainCell c;c.tileIndex=i;
        if(runtimeTerrainInfoForCell(*flat,c).water){waterTile=i;break;}}
    if(waterTile<0)return fail("original water tile for naval structures");
    struct Scene {std::unique_ptr<RuntimeWindowState> state;int source=-1,target=-1;bool bridge=false;
        std::string type,sourceType,relation,owner;RuntimeCellPoint retreat{50,50};int originalSourceHP=0,originalTargetHP=0;
        std::vector<TerrainCell> originalTerrain;std::vector<WorldObjectInstance> huts;};
    const auto addSource=[&](Scene& scene,RuntimeCellPoint point){auto& s=*scene.state;
        const auto* type=s.rules.findUnitType("infantry",scene.sourceType);if(!type)return fail("registered source "+scene.sourceType);
        auto* source=addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Infantry,scene.sourceType,point);
        if(!source||source->health!=type->strength)return fail("source original full health");scene.source=source->instanceId;
        scene.originalSourceHP=source->health;source->mission="Sleep";clearRuntimeRenderPosition(*source);
        source->facing=runtimeFacingFromDelta(1,0);refreshRuntimeObjectOccupancyAfterLocalChange(s);return true;};
    const auto makeScene=[&](const std::string& type,const std::string& source,const std::string& relation,int hz,bool unit=false,bool farApproach=false){
        Scene scene;scene.type=type;scene.sourceType=source;scene.relation=relation;
        scene.owner=relation=="own"?"Americans":relation=="ally"?"French":relation=="neutral"?"Neutral":"Russians";
        scene.state=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*scene.state,*flat,false);auto& s=*scene.state;prepare(s,hz);
        auto* target=addRuntimeObjectInstanceForOwner(s,scene.owner,unit?RuntimeBuildKind::Unit:RuntimeBuildKind::Building,type,{64,64});
        if(!target){fail("target creation "+type);return scene;}scene.target=target->instanceId;scene.originalTargetHP=target->health;
        target->mission="Sleep";target->buildAnimTicksRemaining=target->buildAnimTicksTotal=0;
        const auto* rule=unit?nullptr:s.rules.findBuildingType(type);
        if(rule&&(rule->naval||rule->waterBound)){
            // Original shipyard foundations are water; infantry approach from
            // adjacent original clear land, without changing their rule flags.
            const auto cells=runtimeStructureFootprintCellsForObject(s,*target);
            for(auto& c:s.world.accessTerrainCells())if(std::any_of(cells.begin(),cells.end(),[&](RuntimeCellPoint p){return p.x==c.x&&p.y==c.y;}))c.tileIndex=waterTile;
        }
        // Leave a genuine committed approach segment before C4 can reach the
        // building. Format 14 restores that segment while rebuilding its path.
        if(!initialize(s)||!addSource(scene,{farApproach?60:62,64}))return scene;
        scene.originalTerrain=s.world.getTerrainCells();return scene;};
    int ordinarySaves=0,canonicalSaves=0,matrix=0,matrixUi=0,matrixBackend=0,representatives=0,positive=0,negative=0,naturalDamageCases=0,ordinaryImmuneControls=0;
    std::set<std::string> completedNames;std::map<std::string,int> ordinaryByCase,canonicalByCase;
    const auto recordCompleted=[&](bool isMatrix,bool hidden,bool isPositive,bool natural=false){
        const auto found=expectedCatalog.find(label);
        if(found==expectedCatalog.end()||!selectedExpectedNames.contains(label)||!completedNames.insert(label).second)
            return fail("unexpected or duplicate completed case: "+label);
        const auto& e=found->second;
        if(e.matrix!=isMatrix||e.hidden!=hidden||e.positive!=isPositive||e.natural!=natural
            ||ordinaryByCase[label]!=e.saves||canonicalByCase[label]!=e.saves)
            return fail("completed case differs from independent raw metadata: "+label);
        if(isMatrix){++matrix;hidden?++matrixBackend:++matrixUi;}else ++representatives;
        isPositive?++positive:++negative;naturalDamageCases+=natural;return true;};
    const auto checkpoint=[&](RuntimeWindowState& s,const Scene& scene,bool canonical){
        std::ostringstream out,normalized;IniFile base,exact;
        if(!writeRuntimeStateToStream(s,out,error)||!base.loadFromString(out.str())
            ||base.getValue("RuntimeSave","Version","")!="14")return std::unique_ptr<RuntimeWindowState>{};
        {std::ofstream file(root/(label+"-"+stage+"-ordinary14.ini"),std::ios::binary);file<<out.str();}
        if(canonical){if(!writeRuntimeMultiplayerSnapshotExtension(s,out,error)||!exact.loadFromString(out.str())
                ||!writeRuntimeMultiplayerSnapshotCanonicalIni(exact,normalized,error)||!exact.loadFromString(normalized.str()))return std::unique_ptr<RuntimeWindowState>{};
            std::ofstream file(root/(label+"-"+stage+"-canonical-extension.ini"),std::ios::binary);file<<normalized.str();}
        auto copy=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*copy,s,false);
        // Only the canonical extension stores BridgeFlags. Format14 inherits
        // this immutable map flag; the canonical checkpoint proves restoration
        // from a deliberately different seed without changing that map contract.
        if(canonical&&scene.bridge&&!s.destroyableBridges)copy->destroyableBridges=true;
        RuntimeSnapshotActiveStateScope active(*copy);
        if(!materializeRuntimeLoadStateFromIni(*copy,base,error)||(canonical&&!materializeRuntimeMultiplayerSnapshotExtension(*copy,exact,error)))return std::unique_ptr<RuntimeWindowState>{};
        if(copy->tick!=s.tick||copy->destroyableBridges!=s.destroyableBridges
            ||!resourceValid(*copy)||copy->skirmishModeRulesLoaded!=s.skirmishModeRulesLoaded
            ||copy->skirmishModeAlliesAllowed!=s.skirmishModeAlliesAllowed
            ||runtimeOwnersAreAllied(*copy,"Americans","French")!=runtimeOwnersAreAllied(s,"Americans","French")
            ||hp(*copy,scene.source)!=hp(s,scene.source)||hp(*copy,scene.target)!=hp(s,scene.target)
            ||copy->specialMechanisms.stateHash()!=s.specialMechanisms.stateHash()){
            std::ostringstream d;d<<"strict checkpoint changed original health/tick/bomb authority: format="<<canonical
                <<" tick="<<s.tick<<'/'<<copy->tick<<" hash="<<s.specialMechanisms.stateHash()<<'/'<<copy->specialMechanisms.stateHash()
                <<" mode="<<s.skirmishModeRulesLoaded<<'/'<<copy->skirmishModeRulesLoaded
                <<" alliesAllowed="<<s.skirmishModeAlliesAllowed<<'/'<<copy->skirmishModeAlliesAllowed
                <<" destroyableBridges="<<s.destroyableBridges<<'/'<<copy->destroyableBridges
                <<" file="<<pathString(root/(label+"-"+stage+(canonical?"-canonical-extension.ini":"-ordinary14.ini")))
                <<" source="<<describe(s,scene.source)<<" / "<<describe(*copy,scene.source)
                <<" target="<<describe(s,scene.target)<<" / "<<describe(*copy,scene.target);
            error=d.str();return std::unique_ptr<RuntimeWindowState>{};}
        if(s.pendingTimedBombIntents.contains(scene.source)){
            const auto before=s.behaviors.find(scene.source),after=copy->behaviors.find(scene.source);
            if(!copy->pendingTimedBombIntents.contains(scene.source)||before==s.behaviors.end()||after==copy->behaviors.end()
                ||after->second.kind!=RuntimeBehaviorKind::Move||after->second.forceFire!=before->second.forceFire){
                error="strict format14 checkpoint lost pending C4 Move/force authorization";return std::unique_ptr<RuntimeWindowState>{};}}
        facade(*copy);initializeRuntimeOfflineGameplayRouting(*copy);copy->runtimeSoundsEnabled=copy->visionEnabled=copy->runtimePreferencePersistenceEnabled=false;
        canonical?++canonicalSaves:++ordinarySaves;
        canonical?++canonicalByCase[label]:++ordinaryByCase[label];
        checkpoints<<label<<','<<stage<<','<<(canonical?"ordinary14+canonical-extension":"ordinary14")<<','<<s.tick<<','
            <<s.specialMechanisms.timedBombs().size()<<','<<s.pendingTimedBombIntents.size()<<','<<scene.source<<','<<scene.target<<','
            <<hp(s,scene.source)<<','<<hp(s,scene.target)<<'\n';return copy;};
    const auto checkBridge=[&](const Scene& scene,const RuntimeWindowState& s,bool broken,bool directIvanDamage=false){
        if(!scene.bridge)return true;
        for(const auto& original:scene.huts){const auto* hut=findRuntimeObjectByIdIncludingInactive(s,original.instanceId);
            const int expectedHealth=directIvanDamage&&original.instanceId==scene.target
                ?std::max(0,original.health-originalIvanDirectDamage("CABHUT")):original.health;
            if(!hut||hut->health!=expectedHealth||hut->owner!=original.owner
                ||hut->alive!=(expectedHealth>0))return fail("original repair hut lifetime/owner or direct Ivan damage changed");}
        for(const auto& before:scene.originalTerrain){const auto found=s.terrainByCell.find(runtimeCellKey(before.x,before.y));
            if(found==s.terrainByCell.end()||!found->second)return fail("original actual terrain disappeared");const auto& after=*found->second;
            const bool target=before.y==150&&before.x>=132&&before.x<=158;
            if(!broken||!target){if(!fields(before,after))return fail("untargeted original bridge/terrain changed at "+std::to_string(before.x)+","+std::to_string(before.y));continue;}
            const int frame=before.x==132?8:before.x==158?7:-1;
            if(before.tileIndex!=after.tileIndex||before.subTileIndex!=after.subTileIndex||before.level!=after.level
                ||(frame<0?after.hasOverlay:!after.hasOverlay||after.overlayIndex!=24||after.overlayFrame!=frame))return fail("independent original whole-span cap/clear differs");
            const auto health=s.bridgeHealthByCell.find(runtimeCellKey(before.x,before.y));
            if(health==s.bridgeHealthByCell.end()||health->second!=0)return fail("whole-span bridge HP not zero");
        }
        if(broken)for(int x=132;x<=158;++x)for(int lane=0;lane<3;++lane){const auto found=s.terrainInfoByCell.find(runtimeCellKey(x,150+lane));
            if(found==s.terrainInfoByCell.end()||found->second.bridge)
                return fail("whole-span physical bridge deck remains");}
        return true;};
    const auto run=[&](Scene scene,bool force,bool full,bool save,bool isMatrix){
        if(!scene.state||scene.source<0||scene.target<0||!error.empty())return fail("fixture: "+error);
        auto& s=*scene.state;RuntimeSnapshotActiveStateScope active(s);
        const bool ivan=ivanSources.contains(scene.sourceType), expected=force||scene.relation=="enemy"||scene.relation=="neutral"||scene.type=="MTNK";
        const bool exactDirectIvanDamage=ivan&&(scene.type=="GAWALL"||scene.type=="CABHUT");
        const int expectedDirectDamage=exactDirectIvanDamage?originalIvanDirectDamage(scene.type):-1;
        const bool expectedBridgeBroken=scene.bridge&&s.destroyableBridges;
        const int delay=ivan?std::max(1,static_cast<int>(std::lround(static_cast<double>(ivanFrames)*s.config.fixedTickHz/15.0))):
            std::max(1,static_cast<int>(std::floor(c4Minutes*60.0*s.config.fixedTickHz)));
        if(!resourceValid(s)||runtimeOwnersAreAllied(s,"Americans","French")!=s.skirmishModeAlliesAllowed
            ||runtimeOwnersAreAllied(s,"Americans","Russians"))return fail("original fixture rules/alliance context");
        const auto* target=findObjectById(s,scene.target);if(!target)return fail("live target missing");
        const bool nonClickable=nonClickableTypes.contains(scene.type);
        const auto* building=s.rules.findBuildingType(scene.type);
        if(nonClickable!=(runtimeInvisibleMapBuilding(&s.rules,*target)||(building&&toLowerAsciiLocal(building->image)=="null")))
            return fail("independent raw non-clickable census differs from native target classification");
        const std::string route=nonClickable?(ivan?"hidden-ivan-attachment-capability":"hidden-native-executor"):"UI";
        s.selectedObjectIds={scene.source};stage="UI order";
        const bool uiAccepted=dispatchRuntimeTargetOrderFromUi(s,force?RuntimeMultiplayerGameplayTargetOrderKind::Attack:RuntimeMultiplayerGameplayTargetOrderKind::Primary,
            {target->x,target->y},target);
        bool accepted=uiAccepted;
        int observedPlantTicks=0;
        const auto plantingState=[&](){std::ostringstream d;
            d<<std::setprecision(17)<<" tick="<<s.tick<<" plantingTicks="<<observedPlantTicks<<" hz="<<s.config.fixedTickHz;
            const auto move=s.behaviors.find(scene.source);
            if(move!=s.behaviors.end()){const auto& b=move->second;
                d<<" behavior="<<static_cast<int>(b.kind)<<':'<<static_cast<int>(b.state)
                    <<" goal="<<b.moveTarget.x<<','<<b.moveTarget.y
                    <<" next="<<(b.path.empty()?-1:b.path.front().x)<<','<<(b.path.empty()?-1:b.path.front().y)
                    <<" path="<<b.path.size()<<" cooldown="<<b.moveCooldownTicks<<" blocked="<<b.blockedTicks
                    <<" request="<<b.pathRequestPending<<" force="<<b.forceFire
                    <<" allowedHost="<<b.pendingAllowedOccupiedObjectId
                    <<" renderFromTo="<<b.renderFrom.x<<','<<b.renderFrom.y<<"->"<<b.renderTo.x<<','<<b.renderTo.y
                    <<" renderTicks="<<b.renderMoveTicksRemaining<<'/'<<b.renderMoveTicksTotal<<" route=[";
                for(const auto& point:b.path)d<<point.x<<':'<<point.y<<';';d<<']';
            }else d<<" behavior=none";
            const auto* source=findRuntimeObjectByIdIncludingInactive(s,scene.source);
            const auto* currentTarget=findRuntimeObjectByIdIncludingInactive(s,scene.target);
            if(source){const auto offset=worldObjectSubCellOffset(*source);
                d<<" sourceBody="<<runtimeObjectRenderX(*source)+offset.x<<','<<runtimeObjectRenderY(*source)+offset.y;}
            for(const auto& worker:s.world.getObjects()){
                if(toUpperAsciiLocal(worker.type)!="SLAV"
                    ||(!source&&!currentTarget)
                    ||((!source||std::max(std::abs(worker.x-source->x),std::abs(worker.y-source->y))>8)
                        &&(!currentTarget||std::max(std::abs(worker.x-currentTarget->x),std::abs(worker.y-currentTarget->y))>8)))continue;
                const auto offset=worldObjectSubCellOffset(worker);const auto* unit=findRuntimeUnitType(s.rules,worker);
                d<<" [nearSLAV "<<describe(s,worker.instanceId)
                    <<" body="<<runtimeObjectRenderX(worker)+offset.x<<','<<runtimeObjectRenderY(worker)+offset.y
                    <<" workerRole="<<(unit&&unit->slaved);
                const auto task=s.slaveMinerTasks.find(worker.instanceId);
                if(task!=s.slaveMinerTasks.end())d<<" refinery="<<task->second.refineryId
                    <<" phase="<<static_cast<int>(task->second.phase)<<" task="<<task->second.status;
                d<<']';
            }return d.str();};
        const auto diagnose=[&](const std::string& why){std::ostringstream d;
            d<<why<<" route="<<route<<" UIAccepted="<<uiAccepted<<" executorAccepted="<<accepted<<" valid="<<s.lastCommand.valid<<" accepted="<<s.lastCommand.acceptedCount<<" rejected="<<s.lastCommand.rejectedCount
                <<" verb="<<s.lastCommand.verb<<" pending="<<s.pendingTimedBombIntents.size()<<" bombs="<<s.specialMechanisms.timedBombs().size()
                <<" source="<<describe(s,scene.source)<<" target="<<describe(s,scene.target)<<" status="<<s.interactionStatus<<" / "<<s.simulationStatus
                <<plantingState();
            return fail(d.str());};
        if(nonClickable){
            // InvisibleInGame must reject UI. A NULL-image logic object is
            // also non-clickable; record its actual dispatcher result without
            // interpreting a supplied object pointer as a real mouse click.
            if(invisibleTypes.contains(scene.type)&&uiAccepted)
                return diagnose("original invisible target was accepted by UI");
            if(!uiAccepted&&(!s.pendingTimedBombIntents.empty()||!s.specialMechanisms.timedBombs().empty()
                ||!s.projectiles.empty()||!s.recentExpiredProjectiles.empty()
                ||hp(s,scene.source)!=scene.originalSourceHP||hp(s,scene.target)!=scene.originalTargetHP))
                return diagnose("original non-clickable target did not reject UI without explosive authority");
            // Preserve the real command's relation/force authorization. The
            // attack loop intentionally cannot project an invisible target;
            // Ivan therefore tests the typed attachment called by impact,
            // after authorization, and does not claim a UI/projectile proof.
            stage="hidden native executor";
            if(!uiAccepted){
                if(ivan){issueRuntimeAttackCommand(s,*target,force);accepted=s.lastCommand.acceptedCount>0;}
                else {const bool queued=queueRuntimeTimedBombTarget(s,*target,force);accepted=queued&&s.lastCommand.acceptedCount>0;}
            }
            if(accepted!=expected)return diagnose("hidden building native executor relation/force gate differs");
            if(ivan&&expected){
                std::string reason;
                if(!s.specialMechanisms.tryAttachIvanBomb(s.world.getObjects(),s.rules,scene.source,scene.target,s.tick,
                        s.config.fixedTickHz,runtimeSpecialEventSink(s),reason))
                    return diagnose("authorized hidden Ivan typed attachment failed: "+reason);
            }
        }
        if(expected&&!accepted)return diagnose("required object planting command rejected");
        if(expected&&!ivan){const auto move=s.behaviors.find(scene.source);
            if(!s.pendingTimedBombIntents.contains(scene.source)||move==s.behaviors.end()
                ||move->second.kind!=RuntimeBehaviorKind::Move||move->second.forceFire!=force)
                return diagnose("C4 object order did not retain its original Move/force authorization");}
        std::unique_ptr<RuntimeWindowState> ordinary,canonical;
        if(save&&expected){
            if(!ivan){
                // A fresh queued path is intentionally absent from ordinary
                // format 14. Save during the first actual native segment so
                // both formats exercise its existing in-flight reconstruction.
                stage="native approach before save";
                const auto* mover=findObjectById(s,scene.source);
                int approachTicks=0;
                while(approachTicks<3&&mover&&!mover->renderMoveActive){step(s);++approachTicks;mover=findObjectById(s,scene.source);}
                const auto move=s.behaviors.find(scene.source);
                if(!mover||!mover->renderMoveActive||!mover->hasRenderPosition||!s.pendingTimedBombIntents.contains(scene.source)
                    ||s.specialMechanisms.timedBombs().contains(scene.target)||move==s.behaviors.end()
                    ||move->second.kind!=RuntimeBehaviorKind::Move||move->second.moveCooldownTicks<=1)
                    return diagnose("no genuine pending C4 approach segment available for strict saving");
            }
            stage="approach-save";ordinary=checkpoint(s,scene,false);
            if(!ordinary)return diagnose("strict pending approach ordinary14 checkpoint: "+error);
            canonical=checkpoint(s,scene,true);
            if(!canonical)return diagnose("strict pending approach canonical checkpoint: "+error);}
        const auto replicas=[&](){std::vector<RuntimeWindowState*> v{&s};if(ordinary)v.push_back(ordinary.get());if(canonical)v.push_back(canonical.get());return v;};
        std::string replicaDifference;
        const auto same=[&](){
            bool equal=true;for(auto* q:replicas())if(q->tick!=s.tick||hp(*q,scene.target)!=hp(s,scene.target)
                ||hp(*q,scene.source)!=hp(s,scene.source)||q->specialMechanisms.stateHash()!=s.specialMechanisms.stateHash())equal=false;
            if(equal)return true;
            std::ostringstream d;for(auto* q:replicas()){
                const std::string name=q==&s?"original":q==ordinary.get()?"ordinary14":"canonical";
                d<<" ["<<name<<" tick="<<q->tick<<" hash="<<q->specialMechanisms.stateHash()<<" pending="<<q->pendingTimedBombIntents.size()
                    <<" bombs="<<q->specialMechanisms.timedBombs().size()<<" source="<<describe(*q,scene.source)<<" target="<<describe(*q,scene.target);
                const auto move=q->behaviors.find(scene.source);if(move!=q->behaviors.end())d<<" behavior="<<static_cast<int>(move->second.kind)
                    <<" goal="<<move->second.moveTarget.x<<','<<move->second.moveTarget.y<<" cooldown="<<move->second.moveCooldownTicks
                    <<" path="<<move->second.path.size()<<" requesting="<<move->second.pathRequestPending<<" force="<<move->second.forceFire;
                for(const auto& [id,bomb]:q->specialMechanisms.timedBombs())d<<" bomb="<<id<<':'<<bomb.attachedTick<<':'<<bomb.detonateTick;
                d<<']';std::ostringstream stateDump;std::string dumpError;
                const bool written=writeRuntimeStateToStream(*q,stateDump,dumpError);
                if(written)(void)writeRuntimeMultiplayerSnapshotExtension(*q,stateDump,dumpError);
                std::ofstream file(root/(label+"-"+stage+"-difference-"+name+".ini"),std::ios::binary);
                file<<stateDump.str()<<"\n[Diagnostic]\nWriterSucceeded="<<written<<"\nWriterError="<<dumpError<<'\n';
            }
            replicaDifference=d.str();return false;};
        int ticks=0,attached=-1,deadline=-1;
        stage="native planting";
        const int budget=expected?s.config.fixedTickHz*(scene.bridge?100:plantBudgetSeconds):6;
        for(;ticks<budget;++ticks){for(auto* q:replicas())step(*q);++observedPlantTicks;if(!same())return diagnose("restored native approach differs"+replicaDifference);
            const auto bomb=s.specialMechanisms.timedBombs().find(scene.target);
            if(bomb!=s.specialMechanisms.timedBombs().end()){
                if(!expected)return diagnose("ordinary own/allied building received an unauthorized bomb");
                const auto& b=bomb->second;attached=b.attachedTick;deadline=b.detonateTick;
                if(b.sourceObjectId!=scene.source||b.owner!="Americans"||b.kind!=(ivan?TimedBombKind::Ivan:TimedBombKind::C4)
                    ||deadline-attached!=delay||b.forceDestruction==ivan||b.damage!=(ivan?ivanDamage:std::numeric_limits<int>::max())
                    ||b.warheadId!=(ivan?ivanWarhead:c4Warhead)||hp(s,scene.target)!=scene.originalTargetHP||hp(s,scene.source)!=scene.originalSourceHP)
                    return diagnose("raw original armed timer/source/HP differs");
                break;
            }
            if(hp(s,scene.target)!=scene.originalTargetHP)return diagnose("ordinary damage was mistaken for planting");
            if(!filter.empty()&&expected&&observedPlantTicks%(s.config.fixedTickHz*5)==0)
                std::cout<<"Building timed bomb planting progress "<<label<<plantingState()<<'\n';
        }
        if(expected&&deadline<0)return diagnose("actual native armed timer never appeared");
        if(!expected){
            if(!s.pendingTimedBombIntents.empty()||!s.specialMechanisms.timedBombs().empty()
                ||std::any_of(s.projectiles.begin(),s.projectiles.end(),[](const auto& p){return p.weaponId=="IvanBomber";})
                ||std::any_of(s.recentExpiredProjectiles.begin(),s.recentExpiredProjectiles.end(),[](const auto& p){return p.weaponId=="IvanBomber";}))
                return diagnose("ordinary allied/own building leaked explosive authority");
        }
        if(!checkBridge(scene,s,false))return false;
        if(full&&expected){
            stage="armed-save";if(save){ordinary=checkpoint(s,scene,false);
                if(!ordinary)return diagnose("strict armed ordinary14 checkpoint: "+error);
                canonical=checkpoint(s,scene,true);
                if(!canonical)return diagnose("strict armed canonical checkpoint: "+error);}
            if(!ivan&&force)for(auto* q:replicas()){
                const auto before=q->specialMechanisms.stateHash();q->selectedObjectIds={scene.source};const auto* t=findObjectById(*q,scene.target);
                if(!t)return diagnose("armed forced C4 target disappeared");
                (void)dispatchRuntimeTargetOrderFromUi(*q,RuntimeMultiplayerGameplayTargetOrderKind::Attack,{t->x,t->y},t);
                const auto move=q->behaviors.find(scene.source);
                if(q->specialMechanisms.stateHash()!=before||q->specialMechanisms.timedBombs().at(scene.target).detonateTick!=deadline
                    ||q->pendingTimedBombIntents.contains(scene.source)||(move!=q->behaviors.end()&&move->second.kind==RuntimeBehaviorKind::Attack))
                    return diagnose("armed C4 retry restarted timer or fell through to ordinary gun fire");}
            if(ivan)for(auto* q:replicas()){
                q->selectedObjectIds={scene.source};if(!dispatchRuntimeTargetOrderFromUi(*q,RuntimeMultiplayerGameplayTargetOrderKind::Move,scene.retreat))return diagnose("real Ivan blast retreat rejected");}
            stage="natural expiry";
            for(;s.tick<deadline;){for(auto* q:replicas())step(*q);if(!same())return diagnose("restored original expiry/health differs"+replicaDifference);
                if(s.tick<deadline){if(!s.specialMechanisms.timedBombs().contains(scene.target)||hp(s,scene.target)!=scene.originalTargetHP||!checkBridge(scene,s,false))return diagnose("original timer detonated early");}}
            const bool wrongTargetDamage=ivan
                ?(exactDirectIvanDamage?hp(s,scene.target)!=std::max(0,scene.originalTargetHP-expectedDirectDamage)
                    :hp(s,scene.target)>=scene.originalTargetHP)
                :(scene.bridge?hp(s,scene.target)!=scene.originalTargetHP:hp(s,scene.target)!=0);
            if(s.tick!=deadline||s.specialMechanisms.timedBombs().contains(scene.target)||s.pendingTimedBombIntents.contains(scene.source)
                ||wrongTargetDamage)
                return diagnose("natural deadline did not consume the bomb or deliver original direct target damage="+std::to_string(expectedDirectDamage));
            for(auto* q:replicas())if(!checkBridge(scene,*q,expectedBridgeBroken,ivan))return false;
            stage="post-expiry-save";if(save){ordinary=checkpoint(s,scene,false);
                if(!ordinary)return diagnose("strict post-expiry ordinary14 checkpoint: "+error);
                canonical=checkpoint(s,scene,true);
                if(!canonical)return diagnose("strict post-expiry canonical checkpoint: "+error);}
            const auto nextRequest=s.deathWeapons.nextRequestId();const int targetHealth=hp(s,scene.target);
            stage="once";for(int repeat=0;repeat<3;++repeat)for(auto* q:replicas())step(*q);
            if(!same()||s.specialMechanisms.timedBombs().contains(scene.target)||hp(s,scene.target)!=targetHealth||s.deathWeapons.nextRequestId()!=nextRequest)
                return diagnose("expired timer replayed damage/death authority"+replicaDifference);
            for(auto* q:replicas())if(!checkBridge(scene,*q,expectedBridgeBroken,ivan))return false;
            if(ivan&&scene.type=="CABHUT"){
                stage="ordinary immune impact after attached Ivan damage";
                const auto* ordinaryWarhead=s.rules.findWarheadType(ivanWarhead);
                if(!ordinaryWarhead)return diagnose("ordinary original IvanWH control missing");
                for(auto* q:replicas()){
                    const auto* victim=findObjectById(*q,scene.target);
                    if(!victim)return diagnose("damaged living hut missing before ordinary control");
                    const int beforeHP=victim->health;
                    const auto beforeBombHash=q->specialMechanisms.stateHash();
                    const auto beforeDeathRequest=q->deathWeapons.nextRequestId();
                    RuntimeProjectileEffect ordinaryImpact;
                    ordinaryImpact.actorId=nextRuntimeProjectileActorId(*q);
                    ordinaryImpact.attackerObjectId=scene.source;ordinaryImpact.targetObjectId=scene.target;
                    ordinaryImpact.owner="Americans";
                    ordinaryImpact.fromCell=ordinaryImpact.toCell=ordinaryImpact.currentCell=runtimeCellPointForObject(*victim);
                    ordinaryImpact.fromLevel=ordinaryImpact.toLevel=ordinaryImpact.currentLevel=victim->level;
                    ordinaryImpact.hasTargetPosition=true;
                    ordinaryImpact.targetX=victim->x;ordinaryImpact.targetY=victim->y;ordinaryImpact.targetZ=victim->level;
                    ordinaryImpact.spawnTick=ordinaryImpact.impactTick=q->tick;
                    ordinaryImpact.damage=ordinaryImpact.spawnedMissileBaseDamage=ivanDamage;
                    ordinaryImpact.spawnedMissileDamage=true;
                    ordinaryImpact.warheadId=ordinaryImpact.warheadImpactIntentId=ivanWarhead;
                    ordinaryImpact.cellSpread=ordinaryWarhead->cellSpread;ordinaryImpact.percentAtMax=ordinaryWarhead->percentAtMax;
                    ordinaryImpact.aa=ordinaryImpact.ag=true;
                    ordinaryImpact.affectsAllies=ordinaryWarhead->affectsAllies;
                    ordinaryImpact.affectsEnemies=ordinaryWarhead->affectsEnemies;
                    ordinaryImpact.totalTicks=ordinaryImpact.remainingTicks=1;
                    ordinaryImpact.lifecycle=RuntimeProjectileLifecycleState::Impacting;ordinaryImpact.lifecycleState="Impacting";
                    ordinaryImpact.renderIntent="warheadImpact";
                    // The ordinary overload grants no attached-bomb permission.
                    (void)applyRuntimeProjectileImpact(*q,ordinaryImpact);
                    if(hp(*q,scene.target)!=beforeHP||q->specialMechanisms.stateHash()!=beforeBombHash
                        ||q->deathWeapons.nextRequestId()!=beforeDeathRequest
                        ||!checkBridge(scene,*q,expectedBridgeBroken,true))
                        return diagnose("ordinary impact damaged or healed an immune hut, or changed bridge authority");
                }
                if(!same())return diagnose("ordinary immune control differs after strict restore"+replicaDifference);
                ++ordinaryImmuneControls;
            }
        }
        if(!recordCompleted(isMatrix,nonClickable,expected,full&&expected))return false;
        csv<<label<<','<<scene.type<<','<<scene.sourceType<<','<<scene.relation<<','<<(force?"force-object":"Primary")<<','<<s.config.fixedTickHz<<','
            <<scene.target<<','<<scene.source<<','<<accepted<<','<<expected<<','<<attached<<','<<deadline<<','<<ticks<<','<<hp(s,scene.target)<<','<<hp(s,scene.source)
            <<','<<route<<','<<uiAccepted<<",PASS,"<<(full&&expected)<<','<<expectedDirectDamage<<','
            <<(scene.bridge&&s.destroyableBridges)<<','<<(full&&expected&&expectedBridgeBroken)<<','
            <<(full&&expected&&ivan&&scene.type=="CABHUT")<<'\n';
        csv.flush();
        if(!filter.empty()||!isMatrix)std::cout<<"Building timed bomb "<<label<<" PASS armed="<<expected<<" rawTimer="<<delay
            <<" full="<<full<<" saves="<<save<<" targetHP="<<hp(s,scene.target)<<'\n';return true;};
    // All configured ordinary structures receive an actual command and must
    // either reach a real timer or prove that no explosive authority exists.
    std::set<std::string> stockSeen;
    for(const auto& b:live.rules.getBuildingTypes())if(!b.bridgeRepairHut&&stockSeen.insert(b.id).second){
        for(const std::string source:{"TANY","IVAN"})for(const std::string relation:{"own","ally","enemy"})for(bool force:{false,true}){
            label="stock-"+source+"-"+b.id+"-"+relation+"-"+(force?"force":"Primary")+"-30";
            if(!matches(label))continue;stage="fixture";error.clear();if(!run(makeScene(b.id,source,relation,30),force,false,false,true))return false;}
        if(filter.empty()&&matrix%300==0)std::cout<<"Building timed bomb inventory progress: "<<matrix<<" commands verified\n";
    }
    struct Representative {const char* type;const char* source;const char* relation;bool force;};
    const std::array<Representative,7> examples{{
        {"GAPOWR","TANY","own",true},{"CAMISC01","GHOST","ally",true},{"CAMISC02","PTROOP","enemy",false},
        {"CAMISC06","IVAN","own",true},{"AMMOCRAT","CIVAN","ally",true},{"GAWALL","GHOST","enemy",false},{"CAPOWR","CIVAN","enemy",false}}};
    for(int hz:{30,60})for(const auto& c:examples){label="timer-"+std::string(c.source)+"-"+c.type+"-"+c.relation+"-"+(c.force?"force":"Primary")+"-"+std::to_string(hz);
        if(!matches(label))continue;stage="fixture";error.clear();const bool save=hz==30&&(std::string(c.type)=="GAPOWR"||std::string(c.type)=="CAMISC06");
        if(!run(makeScene(c.type,c.source,c.relation,hz,false,save&&c4Sources.contains(c.source)),c.force,true,save,false))return false;}
    for(int hz:{30,60}){
        label="timer-IVAN-GAWALL-enemy-Primary-"+std::to_string(hz);
        if(matches(label)){stage="original wall natural expiry";error.clear();
            if(!run(makeScene("GAWALL","IVAN","enemy",hz),false,true,false,false))return false;}
    }
    for(const std::string source:{"IVAN","CIVAN"}){
        label="neutral-"+source+"-GAPOWR-Primary-30";
        if(matches(label)){stage="neutral ordinary natural expiry";error.clear();
            if(!run(makeScene("GAPOWR",source,"neutral",30),false,true,false,false))return false;}
    }
    for(bool force:{false,true}){
        label="timer-CIVAN-GAPOWR-own-"+std::string(force?"force":"Primary")+"-30";
        if(matches(label)){stage="original chrono Ivan own order";error.clear();
            if(!run(makeScene("GAPOWR","CIVAN","own",30),force,force,false,false))return false;}
    }
    label="unit-IVAN-MTNK-own-Primary-30";
    if(matches(label)){error.clear();if(!run(makeScene("MTNK","IVAN","own",30,true),false,false,false,false))return false;}
    label="persistence-IVAN-GAPOWR-own-force-Stop-30";
    if(matches(label)){
        stage="launch-before-Stop";error.clear();auto scene=makeScene("GAPOWR","IVAN","own",30);
        if(!scene.state||scene.source<0||scene.target<0||!error.empty())return fail("forced Ivan persistence fixture");auto& s=*scene.state;
        s.selectedObjectIds={scene.source};const auto* t=findObjectById(s,scene.target);
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Attack,{t->x,t->y},t))return fail("real forced Ivan launch command");
        int actor=-1;for(int i=0;i<150&&actor<0;++i){step(s);for(const auto& p:s.projectiles)if(p.weaponId=="IvanBomber"&&p.attackerObjectId==scene.source){
            if(!p.affectsAllies||p.targetObjectId!=scene.target)return fail("forced building Ivan actor lost immutable authorization");actor=p.actorId;}}
        if(actor<0||s.specialMechanisms.timedBombs().contains(scene.target))return fail("natural live Ivan actor never observed before attachment");
        s.selectedObjectIds={scene.source};if(!dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop))return fail("real Stop after Ivan launch");
        for(int i=0;i<90&&!s.specialMechanisms.timedBombs().contains(scene.target);++i)step(s);
        const auto armed=s.specialMechanisms.timedBombs().find(scene.target);
        if(armed==s.specialMechanisms.timedBombs().end()||armed->second.kind!=TimedBombKind::Ivan||armed->second.sourceObjectId!=scene.source
            ||armed->second.detonateTick-armed->second.attachedTick!=900||hp(s,scene.target)!=scene.originalTargetHP)
            return fail("Stop revoked the already launched forced Ivan actor or dealt ordinary damage");
        if(!recordCompleted(false,false,true))return false;
        csv<<label<<",GAPOWR,IVAN,own,force-object+Stop,30,"<<scene.target<<','<<scene.source<<",1,1,"<<armed->second.attachedTick<<','
            <<armed->second.detonateTick<<",0,"<<hp(s,scene.target)<<','<<hp(s,scene.source)<<",UI,1,PASS,0,-1,0,0,0\n";
    }
    label="persistence-TANY-GAPOWR-enemy-to-ally-Primary-30";
    if(matches(label)){
        stage="pending target owner transition";error.clear();auto scene=makeScene("GAPOWR","TANY","enemy",30);
        if(!scene.state||scene.source<0||scene.target<0||!error.empty())return fail("normal C4 relation transition fixture");auto& s=*scene.state;
        s.selectedObjectIds={scene.source};const auto* t=findObjectById(s,scene.target);
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,{t->x,t->y},t)
            ||!s.pendingTimedBombIntents.contains(scene.source)||s.behaviors.at(scene.source).forceFire)return fail("native nonforced enemy C4 approach");
        const auto changed=applyRuntimeOwnerTransitionBatch(s,{{scene.target,"Russians","French"}},RuntimeOwnerTransitionRepairPolicy::CaptureDockedUnits,"timedBombPolicyNativeCapture");
        if(!changed.applied||!runtimeOwnersAreAllied(s,"Americans",findObjectById(s,scene.target)->owner))return fail("actual capture into allied house: "+changed.error);
        for(int i=0;i<60;++i)step(s);
        if(!s.pendingTimedBombIntents.empty()||!s.specialMechanisms.timedBombs().empty()||hp(s,scene.target)!=scene.originalTargetHP)
            return fail("nonforced enemy approach planted on its now-allied building");
        if(!recordCompleted(false,false,false))return false;
        csv<<label<<",GAPOWR,TANY,enemy-to-ally,Primary,30,"<<scene.target<<','<<scene.source<<",1,0,-1,-1,60,"<<hp(s,scene.target)<<','
            <<hp(s,scene.source)<<",UI,1,PASS,0,-1,0,0,0\n";
    }
    label="context-TANY-GAPOWR-same-team-no-allies-Primary-30";
    if(matches(label)){
        stage="same team with Battle alliances disabled";error.clear();auto scene=makeScene("GAPOWR","TANY","ally",30,false,true);
        if(!scene.state||scene.source<0||scene.target<0||!error.empty())return fail("mode-context fixture");auto& s=*scene.state;
        s.skirmishModeAlliesAllowed=false;
        if(s.skirmishTeamByOwner.at("americans")!="1"||s.skirmishTeamByOwner.at("french")!="1"
            ||runtimeOwnersAreAllied(s,"Americans","French"))return fail("same-team Battle fixture must have no active alliance");
        // The authored team is shared, but current Battle mode makes French
        // an enemy. Ordinary C4 and both strict saved intents remain non-force.
        scene.relation="enemy";
        if(!run(std::move(scene),false,false,true,false))return false;
    }
    // CABHUT keeps its typed bridge service and ordinary immunity. An attached
    // Ivan bomb damages only its direct hut; registered bridge destruction is
    // independently controlled by the original map flag.
    auto bridgeBase=std::make_unique<RuntimeWindowState>();bridgeBase->config=live.config;bridgeBase->config.playerSlots.clear();
    label="bridge-base";stage="original all02 initialization";
    bridgeBase->config.defaultMapPath=pathString(std::filesystem::path(runtimeAssetRootPath(live.config))/"mapsmd03/all02umd.map");
    bridgeBase->report=buildRuntimeStartupReport(bridgeBase->config,&bridgeBase->world,&bridgeBase->theater,&bridgeBase->art,&bridgeBase->rules,&bridgeBase->mapFacts);
    if(!bridgeBase->report.success)return fail("original all02: "+bridgeBase->report.errorMessage);
    // Startup legitimately applies campaign map rule overrides. This stock
    // weapon fixture restores the already verified original rules, retaining
    // all original map geometry and typed huts exactly as in the earlier test.
    bridgeBase->rules=live.rules;
    bridgeBase->mapFactsLoaded=!bridgeBase->mapFacts.cells.empty();prepare(*bridgeBase,30);
    std::erase_if(bridgeBase->world.accessObjects(),[&](const auto& o){const auto* b=bridgeBase->rules.findBuildingType(o.type);return !b||!b->bridgeRepairHut;});
    const bool bridgeInitialized=initialize(*bridgeBase);
    if(!bridgeInitialized||bridgeBase->world.getObjects().size()!=12){std::ostringstream d;
        d<<"original all02 native huts/clock initialized="<<bridgeInitialized<<" huts="<<bridgeBase->world.getObjects().size()
            <<" rulesHash="<<bridgeBase->rules.getDetailRulesHash()<<" expected="<<rulesHash
            <<" rulesLoaded="<<bridgeBase->rulesLoaded<<" modeLoaded="<<bridgeBase->skirmishModeRulesLoaded
            <<" particlesInitialized="<<bridgeBase->particlesInitialized<<" hz="<<bridgeBase->config.fixedTickHz
            <<" tick="<<bridgeBase->tick<<" registeredSpans="<<bridgeBase->bridgeRepairSpansByHut.size();return fail(d.str());}
    bridgeBase->destroyableBridges=true;
    for(int y:{150,155})for(int x=132;x<=158;++x){const auto found=std::find_if(bridgeBase->mapFacts.cells.begin(),bridgeBase->mapFacts.cells.end(),
            [&](const auto& c){return c.x==x&&c.y==y;});if(found==bridgeBase->mapFacts.cells.end()||!found->hasOverlay
            ||found->overlay.overlayIndex!=24||found->overlay.frameIndex!=0)return fail("raw original independent 27-anchor all02 strips");}
    const auto makeBridge=[&](const std::string& source,const std::string& relation,int hz){Scene scene;scene.type="CABHUT";scene.sourceType=source;scene.relation=relation;scene.bridge=true;
        scene.owner=relation=="own"?"Americans":relation=="ally"?"French":"Russians";scene.retreat={120,140};
        scene.state=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*scene.state,*bridgeBase,false);auto& s=*scene.state;prepare(s,hz);
        if(!initialize(s)){fail("actual bridge fixture initialization");return scene;}s.destroyableBridges=true;
        for(auto& hut:s.world.accessObjects())if(hut.x==129&&hut.y==148){scene.target=hut.instanceId;hut.owner=scene.owner;hut.mission="Sleep";scene.originalTargetHP=hut.health;}
        if(scene.target<0){fail("original upper CABHUT missing");return scene;}
        const auto span=s.bridgeRepairSpansByHut.find(scene.target);std::set<int> actual,expected;
        if(span!=s.bridgeRepairSpansByHut.end())for(const auto& c:span->second.cells)if(c.requiresOverlay)actual.insert(runtimeCellKey(c.point.x,c.point.y));
        for(int x=132;x<=158;++x)expected.insert(runtimeCellKey(x,150));
        if(actual!=expected){fail("original bridge hut registration differs from independent upper strip");return scene;}
        RuntimeCellPoint spawn{124,146};bool chosen=false;
        for(int radius=0;radius<=8&&!chosen;++radius)for(int dy=-radius;dy<=radius&&!chosen;++dy)for(int dx=-radius;dx<=radius&&!chosen;++dx){
            if(std::max(std::abs(dx),std::abs(dy))!=radius)continue;const RuntimeCellPoint point{124+dx,146+dy};const auto cell=s.terrainByCell.find(runtimeCellKey(point.x,point.y));
            if(cell==s.terrainByCell.end())continue;const auto t=runtimeTerrainInfoForCell(s,*cell->second);
            if(t.passable&&!t.water&&!t.shore&&!t.cliff&&!t.ramp&&!runtimeOverlayProvidesBridgeDeck(*cell->second)){spawn=point;chosen=true;}}
        if(!chosen||!addSource(scene,spawn)){fail("actual original land approach for bridge source");return scene;}
        scene.originalTerrain=s.world.getTerrainCells();for(const auto& o:s.world.getObjects())if(o.type=="CABHUT")scene.huts.push_back(o);
        return scene;};
    for(const std::string source:{"TANY","IVAN"})for(const std::string relation:{"own","ally","enemy"})for(bool force:{false,true}){
        label="bridge-"+source+"-CABHUT-"+relation+"-"+(force?"force":"Primary")+"-30";
        if(!matches(label))continue;stage="fixture";error.clear();const bool full=source=="IVAN"&&relation=="enemy"&&!force;
        if(!run(makeBridge(source,relation,30),force,full,false,true))return false;}
    label="bridge-TANY-CABHUT-own-force-60";
    if(matches(label)){error.clear();if(!run(makeBridge("TANY","own",60),true,true,true,false))return false;}
    for(int hz:{30,60}){
        label="bridge-IVAN-CABHUT-indestructible-enemy-Primary-"+std::to_string(hz);
        if(!matches(label))continue;stage="original registered indestructible bridge";error.clear();
        auto scene=makeBridge("IVAN","enemy",hz);
        if(!scene.state||scene.target<0||scene.source<0)return fail("indestructible bridge fixture");
        scene.state->destroyableBridges=false;
        if(!run(std::move(scene),false,true,hz==30,false))return false;
    }
    // C4 still requires an actual registered bridge to demolish a repair hut.
    label="orphan-TANY-CABHUT-enemy-force-30";
    if(matches(label)){error.clear();auto scene=makeScene("CABHUT","TANY","enemy",30);
        if(!scene.state||scene.target<0||scene.source<0)return fail("orphan C4 fixture");auto& s=*scene.state;
        if(s.bridgeRepairSpansByHut.contains(scene.target))return fail("orphan C4 fixture unexpectedly registered a bridge");
        s.selectedObjectIds={scene.source};const auto* target=findObjectById(s,scene.target);const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Attack,{target->x,target->y},target);
        for(int i=0;i<6;++i)step(s);
        if(dispatched||!s.pendingTimedBombIntents.empty()||!s.specialMechanisms.timedBombs().empty()||hp(s,scene.target)!=2000)return fail("forced orphan C4 repair hut bypassed original registered-span contract");
        if(!recordCompleted(false,false,false))return false;
        csv<<label<<",CABHUT,TANY,enemy,force-object,30,"<<scene.target<<','<<scene.source<<",0,0,-1,-1,6,2000,"<<hp(s,scene.source)<<",UI,0,PASS,0,-1,0,0,0\n";
    }
    label="orphan-IVAN-CABHUT-enemy-force-30";
    if(matches(label)){stage="orphan Ivan hut natural expiry";error.clear();auto scene=makeScene("CABHUT","IVAN","enemy",30);
        if(!scene.state||scene.target<0||scene.source<0)return fail("orphan Ivan fixture");
        if(scene.state->bridgeRepairSpansByHut.contains(scene.target))return fail("orphan Ivan fixture unexpectedly registered a bridge");
        if(!run(std::move(scene),true,true,true,false))return false;
    }
    if(matrix+representatives==0)return fail("filter matched no native case");
    if(completedNames!=selectedExpectedNames||matrix!=expectedCounts.matrix||matrixUi!=expectedCounts.ui||matrixBackend!=expectedCounts.backend
        ||representatives!=expectedCounts.representatives||positive!=expectedCounts.positive||negative!=expectedCounts.negative
        ||ordinarySaves!=expectedCounts.ordinary||canonicalSaves!=expectedCounts.canonical
        ||naturalDamageCases!=expectedCounts.natural||ordinaryImmuneControls!=expectedOrdinaryImmuneControls)
        return fail("incomplete independent catalog/route/positive/negative/save/natural-damage/immune-control coverage");
    if(filter.empty()&&shard<0&&(matrix!=4776||matrixUi!=4572||matrixBackend!=204||representatives!=29||ordinarySaves!=16||canonicalSaves!=16||naturalDamageCases!=24||ordinaryImmuneControls!=4))return fail("incomplete inventory/route/representative/save coverage");
    std::cout<<"Building timed bomb policy passed: configured="<<configured.size()<<" missingSections="<<missing.size()<<" matrix="<<matrix
        <<" matrixUI="<<matrixUi<<" matrixHiddenExecutor="<<matrixBackend
        <<" representatives="<<representatives<<" positive="<<positive<<" negative="<<negative<<" ordinary14="<<ordinarySaves
        <<" canonicalExtensions="<<canonicalSaves<<" naturalDamage="<<naturalDamageCases
        <<" ordinaryImmuneControls="<<ordinaryImmuneControls<<" originalRulesHash="<<rulesHash<<'\n';
    std::cout<<"Building timed bomb catalog exact: shard="<<shard<<" selected="<<selectedExpectedNames.size()<<" completed="<<completedNames.size()
        <<" catalog="<<expectedCatalog.size()<<'\n';
    error.clear();return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
