// Real stock UI, native worker manager and original rule clocks. Saves use
// the ordinary format-14 writer plus its existing canonical extension.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeSlaveMinerLifecycleSmoke(RuntimeWindowState& live,std::string& error) {
    std::cout<<std::unitbuf;
    std::string label="startup",stage;
    const auto fail=[&](const std::string& why){error="Slave miner lifecycle "+label+" ["+stage+"]: "+why;return false;};
    const char* output=std::getenv("CONCORD_SLAVE_MINER_LIFECYCLE_OUTPUT");
    const std::filesystem::path root=output&&*output?output:"build/issue22_10_slave_miner_lifecycle";
    const char* selection=std::getenv("CONCORD_SLAVE_MINER_LIFECYCLE_CASE");
    const std::string filter=selection?selection:"";
    std::filesystem::create_directories(root);
    std::ofstream cases(root/"cases.csv"),events(root/"events.csv"),paths(root/"movement.csv"),saved(root/"checkpoints.csv");
    if(!cases||!events||!paths||!saved)return fail("output unavailable");
    cases<<"case,hz,initial_workers,replacements,deaths,mobile_gathers,manual_mobile_return,manual_ore,manual_building_return,auto_resume,regen_ticks,ordinary14,canonical_extension,result\n";
    events<<std::setprecision(17)<<"case,stage,tick,home,worker,type,alive,hp,mission,phase,ore_x,ore_y,cargo,value,work_ticks,x,y,z,body_x,body_y,body_z,subcell,credits,regen_remaining\n";
    paths<<std::setprecision(17)<<"case,stage,tick,object,type,mission,logical_x,logical_y,level,body_x,body_y,body_z,facing,moving,phase,cargo\n";
    saved<<"case,stage,format,tick,home,home_type,workers,stored,credits,regen_remaining\n";
    std::ofstream clocks(root/"regen_clocks.csv");if(!clocks)return fail("regen clock output unavailable");
    clocks<<"case,hz,raw_rule_frames,converted_ticks,first_awp_death_tick,all_deaths_observed_tick,observed_remaining,due_tick\n";
    std::ofstream lethal(root/"deaths.csv");if(!lethal)return fail("native death output unavailable");
    lethal<<"case,tick,attacker,victim,weapon,warhead,projectile_damage,hp,alive\n";
    std::ofstream otherImpacts(root/"native_impacts.csv");if(!otherImpacts)return fail("native impact diagnostic output unavailable");
    otherImpacts<<std::setprecision(17)<<"case,stage,tick,actor,attacker,target,target_type,weapon,warhead,projectile_damage,before_hp,after_hp,before_cargo,before_value,source_x,source_y,target_x,target_y,target_mission\n";
    IniFile raw;if(!raw.loadFromFile(live.report.rulesPath))return fail("original rules unavailable");
    const auto* minerType=live.rules.findUnitType("vehicle","SMIN");
    const auto* workerType=live.rules.findUnitType("infantry","SLAV");
    const auto* gunnerType=live.rules.findUnitType("infantry","SNIPE");
    const auto* buildingType=live.rules.findBuildingType("YAREFN");
    const auto* gun=live.rules.findWeaponType("AWP");
    const auto* gunWarhead=live.rules.findWarheadType("HollowPoint");
    const auto intRule=[&](const std::string& id,const char* key){return std::stoi(raw.getValue(id,key,"0"));};
    const int workerCount=intRule("YAREFN","SlavesNumber"),capacity=intRule("SLAV","Storage");
    const int harvestFrames=intRule("SLAV","HarvestRate"),regenFrames=intRule("YAREFN","SlaveRegenRate");
    const int oreValue=intRule("Riparius","Value");
    const auto verses=splitRuntimeTokenList(raw.getValue("HollowPoint","Verses",""));
    if(!minerType||!workerType||!gunnerType||!buildingType||!gun||!gunWarhead
        ||workerCount!=5||capacity!=4||harvestFrames!=150||regenFrames!=500||oreValue!=25
        ||minerType->strength!=2000||buildingType->strength!=2000||workerType->strength!=125
        ||minerType->speed!=3||workerType->speed!=3||workerType->storage!=capacity||workerType->harvestRate!=harvestFrames
        ||buildingType->slavesNumber!=workerCount||buildingType->slaveRegenRate!=regenFrames
        ||buildingType->slaveReloadRate!=25||minerType->slavesNumber!=workerCount
        ||minerType->deploysInto!="YAREFN"||buildingType->undeploysInto!="SMIN"||buildingType->enslaves!="SLAV"
        ||!workerType->slaved||gunnerType->primaryWeapon!="AWP"||gun->damage!=125||gun->range!=14||intRule("AWP","Range")!=14
        ||gun->projectile!="InvisibleLow"||gun->warhead!="HollowPoint"||verses.size()!=11||verses[0]!="200%"
        ||intRule("AWPE","Range")!=14||raw.getValue("InvisibleLow","SubjectToElevation","")!="yes"||intRule("InvisibleLow","ROT")!=0
        ||raw.getValue("SMIN","DeploysInto","")!="YAREFN"||raw.getValue("YAREFN","UndeploysInto","")!="SMIN"
        ||intRule("SMIN","Strength")!=2000||intRule("YAREFN","Strength")!=2000||intRule("SLAV","Strength")!=125)
        return fail("original registered miner/worker/gunner/count/cargo/time declarations differ");
    // Original scanning reserves the elevation projectile's potential bonus
    // even on this level-zero fixture; actual AWP firing remains range14.
    const double originalGuardScanRange=gun->range+intRule("ElevationModel","ElevationBonusCap")*intRule("ElevationModel","ElevationIncrementBonus");
    if(originalGuardScanRange!=18.0)return fail("original AWP potential elevation guard scan must be eighteen cells");
    const auto nativeGuardRadius=[&](const RuntimeWindowState& q,const WorldObjectInstance& shooter,const WorldObjectInstance& target){
        return std::max(runtimeCachedCapabilityProfileForObject(q,shooter).guardScanRange,runtimeEffectiveWeaponRange(q,shooter,target,*gun));};
    const std::uint64_t rulesHash=live.rules.getDetailRulesHash();
    std::ostringstream modeText;for(const auto& t:live.rules.getTiberiumTypes())modeText<<'['<<t.id<<"]\nGrowthPercentage=0\nSpreadPercentage=0\n";
    const auto resourceFixtureValid=[&](const RuntimeWindowState& s){
        if(!s.skirmishModeRulesLoaded||s.rules.getDetailRulesHash()!=rulesHash)return false;
        for(const auto& t:live.rules.getTiberiumTypes())if(runtimeOreRuleProbability(s,t,false)!=0.0||runtimeOreRuleProbability(s,t,true)!=0.0)return false;
        return true;};
    const auto facade=[](RuntimeWindowState& s,const std::string& owner){
        s.economy.owner=owner;s.config.playerCountry=owner;s.humanHouseAuthorities.clear();
        RuntimeHumanHouseAuthority a;a.owner=a.country=owner;a.canonicalOwner=toLowerAsciiLocal(owner);a.localFacade=true;
        s.humanHouseAuthorities.push_back(std::move(a));};
    const auto body=[](const WorldObjectInstance& o){return std::array<double,3>{
        o.hasRenderPosition?o.renderX:static_cast<double>(o.x),o.hasRenderPosition?o.renderY:static_cast<double>(o.y),
        o.hasRenderPosition?o.renderLevel:static_cast<double>(o.level)};};
    const auto describe=[&](const RuntimeWindowState& s,int id){std::ostringstream d;
        const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);if(!o)return std::string("missing#")+std::to_string(id);
        const auto p=body(*o);d<<std::setprecision(17)<<o->type<<'#'<<id<<" owner="<<o->owner<<" alive="<<o->alive<<" hp="<<o->health
            <<" logical="<<o->x<<','<<o->y<<','<<o->level<<" body="<<p[0]<<','<<p[1]<<','<<p[2]
            <<" move="<<o->renderMoveActive<<" mission="<<o->mission<<" subcell="<<o->subCell;
        const auto task=s.slaveMinerTasks.find(id);if(task!=s.slaveMinerTasks.end())d<<" task="<<serializeRuntimeSlaveMinerTask(task->second);
        if(const auto moving=s.behaviors.find(id);moving!=s.behaviors.end()){
            const auto& b=moving->second;d<<" behavior="<<static_cast<int>(b.kind)<<" state="<<static_cast<int>(b.state)
                <<" target="<<b.targetObjectId<<" goal="<<b.moveTarget.x<<','<<b.moveTarget.y<<" cooldown="<<b.moveCooldownTicks
                <<" pending="<<b.pathRequestPending<<" allowed="<<b.pendingAllowedOccupiedObjectId<<" path="<<b.path.size();
            if(!b.path.empty())d<<" first="<<b.path.front().x<<','<<b.path.front().y;
        }
        d<<" travelling="<<s.playerCommands.travelling.contains(id)<<" pendingQueue=";for(int queued:s.pendingPathObjectIds)d<<queued<<' ';
        return d.str();};
    const auto home=[](const RuntimeWindowState& s)->const WorldObjectInstance*{
        const WorldObjectInstance* result=nullptr;
        for(const auto& o:s.world.getObjects())if(o.alive&&o.health>0&&o.owner=="YuriCountry"&&(o.type=="SMIN"||o.type=="YAREFN")){
            if(result)return nullptr;result=&o;}
        return result;};
    const auto regenRemaining=[&](const RuntimeWindowState& s){const auto* h=home(s);if(!h)return -1;
        const auto found=s.slaveMinerRegenTicks.find(h->instanceId);return found==s.slaveMinerRegenTicks.end()?0:found->second;};
    const auto ids=[&](const RuntimeWindowState& s){std::set<int> result;const auto* h=home(s);if(!h)return result;
        for(const auto& [id,task]:s.slaveMinerTasks)if(task.refineryId==h->instanceId)result.insert(id);return result;};
    const auto storedCount=[&](const RuntimeWindowState& s){int count=0;for(int id:ids(s))if(s.slaveMinerTasks.at(id).phase==RuntimeSlaveMinerPhase::Stored)++count;return count;};
    const auto bails=[](const RuntimeWindowState& s){int count=0;for(const auto& c:s.world.getTerrainCells())
        if(c.x>=38&&c.x<=40&&c.y>=32&&c.y<=34)count+=runtimeResourceBailCountForTerrain(c);return count;};
    const auto orePoint=[](RuntimeCellPoint p){return p.x>=38&&p.x<=40&&p.y>=32&&p.y<=34;};
    const auto terrainFields=[](const TerrainCell& a,const TerrainCell& b){return a.x==b.x&&a.y==b.y&&a.tileIndex==b.tileIndex
        &&a.subTileIndex==b.subTileIndex&&a.level==b.level&&a.hasOverlay==b.hasOverlay&&a.overlayIndex==b.overlayIndex
        &&a.overlayFrame==b.overlayFrame&&a.overlayType==b.overlayType&&a.overlayImage==b.overlayImage
        &&a.overlayIsOre==b.overlayIsOre&&a.overlayIsGem==b.overlayIsGem&&a.iceGrowth==b.iceGrowth;};
    int totalCases=0,ordinarySaves=0,canonicalSaves=0,totalDeaths=0,totalReplacements=0;
    for(int hz:{30,60})for(bool batch:{false,true}){
        label=(batch?"batch-regeneration-":"lifecycle-")+std::to_string(hz);
        if(!filter.empty()&&label.find(filter)==std::string::npos)continue;stage="original fixture";error.clear();
        auto original=std::make_unique<RuntimeWindowState>();auto& s=*original;seedRuntimeLoadScratch(s,live,false);
        s.config.fixedTickHz=hz;s.config.gameMode="Battle";s.config.playerSlots.clear();s.config.skirmishLoadSavePath.clear();
        s.config.gameModeRulesOverride.clear();s.config.noCooldown=s.config.computerAiEnabled=s.config.multiplayerActive=false;
        s.config.superweapons=s.config.crates=s.config.shortGame=false;s.campaignMode=false;s.tick=200;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=s.visionEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();s.terrainByCell.clear();s.terrainInfoByCell.clear();
        s.mapFacts={};s.mapFactsLoaded=false;s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=s.report.map.height=-1;
        s.economy.credits=0;s.economy.skirmishStartApplied=true;s.triggers.loaded=s.triggers.bootstrapComplete=true;
        s.skirmishTeamByOwner={{"yuricountry","1"},{"british","2"}};s.skirmishModeAlliesAllowed=true;
        s.skirmishModeRulesLoaded=s.skirmishModeRules.loadFromString(modeText.str());facade(s,"YuriCountry");
        // Additional original flat rows provide a legal, reachable single
        // sniper parking cell beyond every native ore approach and home exit.
        for(int y=13;y<=48;++y)for(int x=20;x<=48;++x){TerrainCell c;c.x=x;c.y=y;c.cellIndex512=y*512+x;c.tileIndex=c.subTileIndex=0;
            if(orePoint({x,y}))setRuntimeResourceTerrainBails(c,"TIB",12);s.world.accessTerrainCells().push_back(c);}
        initializeRuntimeNavigation(s);ensureRuntimeSubcellsInitialized(s);ensureRuntimeSpecialMechanismsInitialized(s);
        ensureRuntimeSuperWeaponsInitialized(s);ensureRuntimeLightingInitialized(s);
        s.particles.reset(hz);s.particlesInitialized=s.particles.alignEmptyStateTick(s.tick);initializeRuntimeOfflineGameplayRouting(s);
        if(!s.particlesInitialized||!resourceFixtureValid(s)||bails(s)!=108||runtimeOwnersAreAllied(s,"YuriCountry","British"))return fail("native ore/context/particle clock");
        auto* miner=addRuntimeObjectInstanceForOwner(s,"YuriCountry",RuntimeBuildKind::Unit,"SMIN",{34,32});
        if(!miner||miner->health!=2000)return fail("original mobile miner health");const int initialMinerId=miner->instanceId;miner->mission="Guard";
        std::vector<int> gunners;for(int n=0;n<(batch?workerCount:1);++n){
            // Original AWP range14 covers the genuine first-out worker window,
            // while both native gunner rows are outside the home guard radius.
            const RuntimeCellPoint spawn=batch?RuntimeCellPoint{34,n<3?22:21}:RuntimeCellPoint{36,22};
            auto* gunner=addRuntimeObjectInstanceForOwner(s,"British",RuntimeBuildKind::Infantry,"SNIPE",spawn);
            if(!gunner||gunner->health!=gunnerType->strength)return fail("original gunner fixture");
            gunner->mission="Sleep";assignRuntimeInfantrySubcellAtCurrentLocation(s,*gunner);
            const auto* currentMiner=findObjectById(s,initialMinerId);
            if(!currentMiner||gunner->x!=spawn.x||gunner->y!=spawn.y||std::abs(nativeGuardRadius(s,*gunner,*currentMiner)-originalGuardScanRange)>1e-8)
                return fail("native gunner row or original eighteen-cell scan differs");
            gunners.push_back(gunner->instanceId);}
        refreshRuntimeObjectOccupancyAfterLocalChange(s);
        struct ObservedImpact {int health=0,cargo=0,value=0;};
        std::map<int,ObservedImpact> observedOtherImpactHp;
        struct SceneImpactScope {
            RuntimeWindowState& state;std::function<void(const RuntimeProjectileEffect&,bool)> previous;
            ~SceneImpactScope(){state.runtimeProjectileImpactObserver=std::move(previous);}
        } sceneImpactScope{s,std::move(s.runtimeProjectileImpactObserver)};
        s.runtimeProjectileImpactObserver=[&](const RuntimeProjectileEffect& projectile,bool after){
            const auto* target=findRuntimeObjectByIdIncludingInactive(s,projectile.targetObjectId);
            if(!target)return;
            // Observe every real impact, including unrelated guard shots at
            // cargo-bearing slaves. The explicit lethal counters below still
            // count only the workers selected for the original AWP command.
            if(!after){ObservedImpact captured;captured.health=target->health;
                if(const auto task=s.slaveMinerTasks.find(target->instanceId);task!=s.slaveMinerTasks.end()){
                    captured.cargo=task->second.cargoUnits;captured.value=task->second.cargoValue;}
                observedOtherImpactHp[projectile.actorId]=captured;return;}
            const auto before=observedOtherImpactHp.find(projectile.actorId);if(before==observedOtherImpactHp.end())return;
            const auto* attacker=findRuntimeObjectByIdIncludingInactive(s,projectile.attackerObjectId);
            const auto source=attacker?body(*attacker):std::array<double,3>{-1,-1,-1};const auto p=body(*target);
            otherImpacts<<label<<','<<stage<<','<<s.tick<<','<<projectile.actorId<<','<<projectile.attackerObjectId<<','
                <<projectile.targetObjectId<<','<<target->type<<','<<projectile.weaponId<<','<<projectile.warheadId<<','
                <<projectile.damage<<','<<before->second.health<<','<<target->health<<','<<before->second.cargo<<','<<before->second.value<<','<<source[0]<<','<<source[1]<<','
                <<p[0]<<','<<p[1]<<','<<target->mission<<'\n';otherImpacts.flush();observedOtherImpactHp.erase(before);
        };
        std::unique_ptr<RuntimeWindowState> ordinary,canonical;
        const auto replicas=[&](){std::vector<std::pair<std::string,RuntimeWindowState*>> result{{"original",&s}};
            if(ordinary)result.emplace_back("ordinary14",ordinary.get());if(canonical)result.emplace_back("canonical",canonical.get());return result;};
        const auto equal=[&](){
            for(auto [name,q]:replicas())if(q!=&s){
                if(q->tick!=s.tick||q->economy.credits!=s.economy.credits||q->economy.resourceTrips!=s.economy.resourceTrips
                    ||q->economy.slaveMinerLoads!=s.economy.slaveMinerLoads||q->slaveMinerTasks.size()!=s.slaveMinerTasks.size()
                    ||q->slaveMinerRegenTicks!=s.slaveMinerRegenTicks||q->specialMechanisms.stateHash()!=s.specialMechanisms.stateHash()
                    ||q->world.getObjects().size()!=s.world.getObjects().size())
                    return fail(name+" native tick/economy/controller mismatch tick="+std::to_string(s.tick)+"/"+std::to_string(q->tick)
                        +" regen="+std::to_string(regenRemaining(s))+"/"+std::to_string(regenRemaining(*q)));
                for(const auto& o:s.world.getObjects()){
                    const auto* after=findRuntimeObjectByIdIncludingInactive(*q,o.instanceId);const auto beforeBody=body(o);
                    if(!after)return fail(name+" missing "+describe(s,o.instanceId));const auto afterBody=body(*after);
                    if(after->type!=o.type||after->kind!=o.kind||after->owner!=o.owner||after->alive!=o.alive||after->health!=o.health
                        ||after->x!=o.x||after->y!=o.y||after->level!=o.level||after->onBridge!=o.onBridge||after->subCell!=o.subCell
                        ||after->mission!=o.mission||after->facing!=o.facing||after->veterancy!=o.veterancy||after->veterancyExperience!=o.veterancyExperience
                        ||after->buildAnimTicksRemaining!=o.buildAnimTicksRemaining
                        ||after->renderMoveActive!=o.renderMoveActive||std::abs(beforeBody[0]-afterBody[0])>1e-8
                        ||std::abs(beforeBody[1]-afterBody[1])>1e-8||std::abs(beforeBody[2]-afterBody[2])>1e-8
                        ||((o.renderMoveActive||worldObjectHasStoppedGroundPose(o))&&!after->hasRenderPosition))
                        return fail(name+" original body/identity/HP differs: "+describe(s,o.instanceId)+" / "+describe(*q,o.instanceId));
                }
                for(const auto& [id,task]:s.slaveMinerTasks){const auto found=q->slaveMinerTasks.find(id);
                    if(found==q->slaveMinerTasks.end()||serializeRuntimeSlaveMinerTask(found->second)!=serializeRuntimeSlaveMinerTask(task))
                        return fail(name+" worker task differs: "+describe(s,id)+" / "+describe(*q,id));}
                if(q->world.getTerrainCells().size()!=s.world.getTerrainCells().size())return fail(name+" terrain count differs");
                for(std::size_t n=0;n<s.world.getTerrainCells().size();++n)if(!terrainFields(s.world.getTerrainCells()[n],q->world.getTerrainCells()[n]))
                    return fail(name+" exact harvested terrain differs");
            }
            return true;};
        std::map<int,WorldObjectInstance> previousWorkers;std::map<int,RuntimeSlaveMinerTask> previousTasks;
        std::set<int> allWorkers;int deaths=0,replacements=0,mobileGathers=0,manualMobile=0,manualOre=0,manualBuilding=0,autoResume=0;
        int caseOrdinary=0,caseCanonical=0,firstAwpDeathTick=-1;bool gunnersParked=false;
        const auto inspect=[&](){const auto* h=home(s);if(!h||h->health!=2000||h->owner!="YuriCountry"){
                std::string actual=" tick="+std::to_string(s.tick)+" homeResolved="+std::to_string(h?h->instanceId:-1);
                for(const auto& o:s.world.getObjects())if(o.type=="SMIN"||o.type=="YAREFN")actual+=" / "+describe(s,o.instanceId);
                for(int id:gunners)actual+=" / "+describe(s,id);return fail("live original home identity/health"+actual);}
            const auto current=ids(s);if(current.size()>static_cast<std::size_t>(workerCount))return fail("worker manager exceeds original count five");
            int cargoValue=0;for(int id:current){const auto* worker=findRuntimeObjectByIdIncludingInactive(s,id);const auto& task=s.slaveMinerTasks.at(id);
                if(!worker||worker->type!="SLAV"||worker->kind!=WorldObjectKind::Infantry||worker->owner!="YuriCountry"||worker->health!=125
                    ||worker->level!=0||worker->onBridge||task.cargoUnits<0||task.cargoUnits>capacity||task.cargoGemUnits!=0
                    ||task.cargoValue!=task.cargoUnits*oreValue||task.workTicksRemaining<0||task.workTicksRemaining>harvestFrames*hz/15)
                    return fail("original worker/cargo/clock invalid: "+describe(s,id));
                const auto p=body(*worker);cargoValue+=task.cargoValue;
                if(gunnersParked)for(int gunnerId:gunners){const auto* gunner=findObjectById(s,gunnerId);
                    if(!gunner)return fail("parked native gunner disappeared");const auto source=body(*gunner);
                    const double radius=nativeGuardRadius(s,*gunner,*worker);const auto travel=s.playerCommands.travelling.find(gunnerId);
                    if(travel==s.playerCommands.travelling.end())return fail("parked original GuardDestination disappeared");
                    if(std::hypot(p[0]-source[0],p[1]-source[1])<=radius+0.001
                        ||std::hypot(double(travel->second.destination.x-worker->x),double(travel->second.destination.y-worker->y))<=radius+0.001)
                        return fail("native replacement body entered cleanup gunner range: "+describe(s,id)+" / "+describe(s,gunnerId));}
                if(task.phase==RuntimeSlaveMinerPhase::Stored){
                    // A real redeploy can retain Stored bodies during the
                    // building animation or while its legal exit is blocked.
                    if(worker->alive||worker->mission!="InsideSlaveMiner"||worker->subCell!=-1||task.cargoUnits!=0||task.cargoValue!=0
                        ||(h->type!="SMIN"&&h->type!="YAREFN")||worker->x!=h->x||worker->y!=h->y||worker->level!=h->level
                        ||worker->onBridge!=h->onBridge||worker->hasRenderPosition||worker->renderMoveActive||task.workTicksRemaining!=0)
                        return fail("stored worker must retain full HP, empty cargo, no independent render body and exact parent logical placement: "+describe(s,id));
                    const auto before=previousWorkers.find(id);
                    if(before!=previousWorkers.end()&&before->second.alive){
                        const auto old=body(before->second);
                        if(worker->x!=h->x||worker->y!=h->y||worker->level!=h->level||worker->renderMoveActive||h->renderMoveActive
                            ||std::hypot(old[0]-worker->x,old[1]-worker->y)>1.5)
                            return fail("mobile return stored remotely or before real body arrival: "+describe(s,id));
                    }
                }else if(!worker->alive)return fail("active slave task has hidden/dead body: "+describe(s,id));
                const auto before=previousWorkers.find(id);
                const auto beforeTask=previousTasks.find(id);
                if(before!=previousWorkers.end()&&before->second.alive&&worker->alive){const auto old=body(before->second);
                    if(std::abs(worker->x-before->second.x)>1||std::abs(worker->y-before->second.y)>1||std::hypot(p[0]-old[0],p[1]-old[1])>1.5)
                        return fail("worker teleported instead of naturally walking: "+describe(s,id));}
                if(beforeTask!=previousTasks.end()&&task.cargoUnits>beforeTask->second.cargoUnits){
                    if(task.cargoUnits!=beforeTask->second.cargoUnits+1||worker->renderMoveActive||!orePoint(task.targetOre)
                        ||std::max(std::abs(worker->x-task.targetOre.x),std::abs(worker->y-task.targetOre.y))>1)
                        return fail("cargo did not come from a settled native ore pickup: "+describe(s,id));
                    if(h->type=="SMIN")++mobileGathers;
                }
                const bool changed=before==previousWorkers.end()||beforeTask==previousTasks.end()||before->second.alive!=worker->alive
                    ||beforeTask->second.phase!=task.phase||beforeTask->second.cargoUnits!=task.cargoUnits||beforeTask->second.refineryId!=task.refineryId;
                if(changed)events<<label<<','<<stage<<','<<s.tick<<','<<h->instanceId<<','<<id<<",SLAV,"<<worker->alive<<','<<worker->health<<','<<worker->mission<<','
                    <<runtimeSlaveMinerPhaseName(task.phase)<<','<<task.targetOre.x<<','<<task.targetOre.y<<','<<task.cargoUnits<<','<<task.cargoValue<<','<<task.workTicksRemaining<<','
                    <<worker->x<<','<<worker->y<<','<<worker->level<<','<<p[0]<<','<<p[1]<<','<<p[2]<<','<<worker->subCell<<','<<s.economy.credits<<','<<regenRemaining(s)<<'\n';
                if(worker->renderMoveActive)paths<<label<<','<<stage<<','<<s.tick<<','<<id<<",SLAV,"<<worker->mission<<','<<worker->x<<','<<worker->y<<','<<worker->level
                    <<','<<p[0]<<','<<p[1]<<','<<p[2]<<','<<worker->facing<<",1,"<<runtimeSlaveMinerPhaseName(task.phase)<<','<<task.cargoUnits<<'\n';
                previousWorkers[id]=*worker;previousTasks[id]=task;allWorkers.insert(id);
            }
            if(s.economy.credits+cargoValue!=(108-bails(s))*oreValue){
                const int remainingBails=bails(s),expectedValue=(108-remainingBails)*oreValue;
                std::string actual=" tick="+std::to_string(s.tick)+" credits="+std::to_string(s.economy.credits)
                    +" retainedCargo="+std::to_string(cargoValue)+" remainingBails="+std::to_string(remainingBails)
                    +" expectedHarvestValue="+std::to_string(expectedValue)+" difference="+std::to_string(s.economy.credits+cargoValue-expectedValue)
                    +" regen="+std::to_string(regenRemaining(s))+" / "+describe(s,h->instanceId);
                for(const auto& o:s.world.getObjects())if(o.type=="SLAV")actual+=" / "+describe(s,o.instanceId);
                for(int id:gunners)actual+=" / "+describe(s,id);
                return fail("native ore/cargo/credits conservation differs"+actual);}
            if(h->renderMoveActive){const auto p=body(*h);paths<<label<<','<<stage<<','<<s.tick<<','<<h->instanceId<<','<<h->type<<','<<h->mission<<','<<h->x<<','<<h->y<<','<<h->level
                <<','<<p[0]<<','<<p[1]<<','<<p[2]<<','<<h->facing<<",1,home,0\n";}
            return true;};
        const auto step=[&](){for(auto [name,q]:replicas()){(void)name;RuntimeSnapshotActiveStateScope active(*q);++q->tick;advanceRuntimeSimulation(*q,false);completeRuntimeWorldRefresh(*q);}
            return equal()&&inspect();};
        const auto wait=[&](int budget,const auto& predicate,const std::string& why){
            for(int n=0;n<budget;++n){if(predicate())return true;if(!step())return false;}
            if(predicate())return true;const auto* h=home(s);return fail(why+" tick="+std::to_string(s.tick)+" home="+(h?describe(s,h->instanceId):"missing")
                +" workers="+std::to_string(ids(s).size())+" stored="+std::to_string(storedCount(s))+" status="+s.simulationStatus);};
        const auto moveHome=[&](RuntimeCellPoint point){for(auto [name,q]:replicas()){
            const auto* h=home(*q);if(!h)return fail(name+" home missing for UI move");q->selectedObjectIds={h->instanceId};
            if(!dispatchRuntimeTargetOrderFromUi(*q,RuntimeMultiplayerGameplayTargetOrderKind::Move,point))return fail(name+" native home ground move rejected");}
            return equal();};
        const auto toggle=[&](){for(auto [name,q]:replicas()){
            const auto* h=home(*q);if(!h)return fail(name+" home missing for DeployToggle");q->selectedObjectIds={h->instanceId};
            if(!dispatchRuntimeSecondaryActionFromUi(*q,h->instanceId))return fail(name+" real UI DeployToggle rejected");}
            return equal();};
        const auto manualOrder=[&](int worker,RuntimeCellPoint point,bool toHome){for(auto [name,q]:replicas()){
            const auto* h=home(*q);if(!h)return fail(name+" home missing for manual slave order");q->selectedObjectIds={worker};
            if(!dispatchRuntimeTargetOrderFromUi(*q,RuntimeMultiplayerGameplayTargetOrderKind::Primary,toHome?runtimeCellPointForObject(*h):point,toHome?h:nullptr))
                return fail(name+" real slave ore/home Primary rejected: "+describe(*q,worker));}
            return equal();};
        const auto checkpoint=[&](){
            const auto clone=[&](bool exact){std::ostringstream payload,normalized;IniFile base,extension;
                if(!writeRuntimeStateToStream(s,payload,error)||!base.loadFromString(payload.str())||base.getValue("RuntimeSave","Version","")!="14")return std::unique_ptr<RuntimeWindowState>{};
                {std::ofstream file(root/(label+"-"+stage+"-ordinary14.ini"),std::ios::binary);file<<payload.str();}
                if(exact){if(!writeRuntimeMultiplayerSnapshotExtension(s,payload,error)||!extension.loadFromString(payload.str())
                        ||!writeRuntimeMultiplayerSnapshotCanonicalIni(extension,normalized,error)||!extension.loadFromString(normalized.str()))return std::unique_ptr<RuntimeWindowState>{};
                    std::ofstream file(root/(label+"-"+stage+"-canonical-extension.ini"),std::ios::binary);file<<normalized.str();}
                auto copy=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*copy,s,false);RuntimeSnapshotActiveStateScope active(*copy);
                if(!materializeRuntimeLoadStateFromIni(*copy,base,error)||(exact&&!materializeRuntimeMultiplayerSnapshotExtension(*copy,extension,error)))return std::unique_ptr<RuntimeWindowState>{};
                if(!resourceFixtureValid(*copy)||copy->tick!=s.tick||copy->particles.fixedTickHz()!=hz)return std::unique_ptr<RuntimeWindowState>{};
                facade(*copy,"YuriCountry");initializeRuntimeOfflineGameplayRouting(*copy);
                copy->runtimeSoundsEnabled=copy->visionEnabled=copy->runtimePreferencePersistenceEnabled=false;
                exact?++canonicalSaves:++ordinarySaves;exact?++caseCanonical:++caseOrdinary;
                const auto* h=home(*copy);saved<<label<<','<<stage<<','<<(exact?"ordinary14+canonical-extension":"ordinary14")<<','<<copy->tick<<','
                    <<(h?h->instanceId:-1)<<','<<(h?h->type:"missing")<<','<<ids(*copy).size()<<','<<storedCount(*copy)<<','<<copy->economy.credits<<','<<regenRemaining(*copy)<<'\n';return copy;};
            ordinary=clone(false);canonical=clone(true);
            if(!ordinary||!canonical)return fail("strict native checkpoint rejected: "+error);
            return equal();};
        const auto saveWindow=[&](){return wait(hz*5,[&](){for(const auto& [id,b]:s.behaviors)if(b.kind==RuntimeBehaviorKind::Move){
                const auto* o=findObjectById(s,id);if(o&&(o->type=="SMIN"||o->type=="SLAV"||o->type=="SNIPE")
                    &&(!o->renderMoveActive||!o->hasRenderPosition||b.moveCooldownTicks<=1))return false;}return true;},"no native committed segment window for format14 saving");};
        const auto gunnerParkingCell=[&](std::size_t n){return batch?(n<3?RuntimeCellPoint{24,18}:RuntimeCellPoint{26,16}):RuntimeCellPoint{45,13};};
        const auto killWorkers=[&](const std::vector<int>& victims){
            if(victims.size()>gunners.size())return fail("original gunner count insufficient");
            std::map<int,int> actualImpactDeathTicks;
            // Observe the original Effects impact without altering its damage
            // or the later Economy countdown on this same simulation tick.
            struct ImpactScope {
                RuntimeWindowState& state;
                std::function<void(const RuntimeProjectileEffect&,bool)> previous;
                ~ImpactScope(){state.runtimeProjectileImpactObserver=std::move(previous);}
            } impactScope{s,std::move(s.runtimeProjectileImpactObserver)};
            s.runtimeProjectileImpactObserver=[&](const RuntimeProjectileEffect& projectile,bool after){
                if(impactScope.previous)impactScope.previous(projectile,after);
                if(!after||projectile.weaponId!="AWP")return;
                const int id=projectile.targetObjectId;
                if(std::find(victims.begin(),victims.end(),id)==victims.end())return;
                const auto* victim=findRuntimeObjectByIdIncludingInactive(s,id);
                if(victim&&victim->health<=0&&actualImpactDeathTicks.try_emplace(id,s.tick).second)
                    lethal<<label<<','<<s.tick<<','<<projectile.attackerObjectId<<','<<id<<','<<projectile.weaponId<<','<<projectile.warheadId
                        <<','<<projectile.damage<<','<<victim->health<<','<<victim->alive<<'\n';
            };
            for(auto [name,q]:replicas()){
                facade(*q,"British");for(std::size_t n=0;n<victims.size();++n){const auto* victim=findObjectById(*q,victims[n]);
                    if(!victim||victim->health!=125||q->slaveMinerTasks.at(victims[n]).cargoUnits!=0)return fail(name+" full-health empty worker required for native lethal shot");
                    q->selectedObjectIds={gunners[n]};if(!dispatchRuntimeTargetOrderFromUi(*q,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                            runtimeCellPointForObject(*victim),victim))return fail(name+" real original AWP command rejected");}
                facade(*q,"YuriCountry");
            }
            if(!wait(hz*5,[&](){for(int id:victims){const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);if(!o||o->health>0)return false;}return true;},"native AWP lethal impact never completed"))return false;
            for(auto [name,q]:replicas()){
                facade(*q,"British");q->selectedObjectIds.assign(gunners.begin(),gunners.begin()+static_cast<std::ptrdiff_t>(victims.size()));
                if(!dispatchRuntimeSelectionCommandFromUi(*q,RuntimeMultiplayerGameplayCommandType::Stop))return fail(name+" real gunner Stop rejected");
                // A blocked ordinary Move may finish into Guard before reaching
                // the requested parking cell. The original Ctrl+Alt ground
                // GuardDestination command retains its destination and excludes
                // acquisition until arrival; its native retries keep the route.
                // The batch uses two cells (three/two native infantry slots)
                // close enough to finish before the unchanged raw regen500.
                for(std::size_t n=0;n<victims.size();++n){const auto parking=gunnerParkingCell(n);
                    const auto* gunner=findObjectById(*q,gunners[n]);const auto* currentHome=home(*q);
                    if(!gunner||!currentHome)return fail(name+" native cleanup source/home missing");
                    const double radius=nativeGuardRadius(*q,*gunner,*currentHome);
                    for(int oy=31;oy<=35;++oy)for(int ox=37;ox<=41;++ox)
                        if(std::hypot(double(parking.x-ox),double(parking.y-oy))<=radius+0.001)
                            return fail("cleanup cell is within the original AWP eighteen-cell guard scan");
                    q->selectedObjectIds={gunners[n]};
                    if(!dispatchRuntimeTargetOrderFromUi(*q,RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination,parking)
                        ||q->lastCommand.acceptedCount!=1)return fail(name+" real individual gunner cleanup GuardDestination rejected: "+describe(*q,gunners[n]));
                    const auto travel=q->playerCommands.travelling.find(gunners[n]);
                    if(travel==q->playerCommands.travelling.end()||travel->second.kind!=RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination
                        ||travel->second.guarding||travel->second.destination.x!=parking.x||travel->second.destination.y!=parking.y)
                        return fail(name+" original GuardDestination cleanup intent differs: "+describe(*q,gunners[n]));}
                facade(*q,"YuriCountry");}
            for(int id:victims)if(s.slaveMinerTasks.contains(id))return fail("Destroyed slave retained its manager task");
            if(actualImpactDeathTicks.size()!=victims.size())return fail("worker death was not observed in the real original AWP Effects impact");
            for(const auto& [id,tick]:actualImpactDeathTicks){(void)id;if(firstAwpDeathTick<0||tick<firstAwpDeathTick)firstAwpDeathTick=tick;}lethal.flush();
            deaths+=static_cast<int>(victims.size());return equal();};
        stage="deploy and native initial generation";s.selectedObjectIds={initialMinerId};
        if(!toggle()||!wait(hz*30,[&](){const auto* h=home(s);return h&&h->type=="YAREFN"&&h->buildAnimTicksRemaining==0&&ids(s).size()==static_cast<std::size_t>(workerCount);},"real deploy did not generate original five workers"))return false;
        const auto initialIds=ids(s);
        if(batch){
            stage="batch real emergence before original AWP";
            // The first workers can finish a native harvest while later ones
            // are still approaching a crowded deposit. Observe their genuine
            // first outward journey instead of requiring five simultaneous
            // stationary, empty harvesting bodies.
            if(!wait(hz*20,[&](){const auto* h=home(s);if(!h)return false;std::size_t n=0;
                for(int id:initialIds){const auto* w=findObjectById(s,id);const auto task=s.slaveMinerTasks.find(id);
                    if(!w||w->health!=workerType->strength||runtimeGateObjectIsLimbo(*w)
                        ||task==s.slaveMinerTasks.end()||task->second.refineryId!=h->instanceId
                        ||!orePoint(task->second.targetOre)||task->second.cargoUnits!=0
                        ||(task->second.phase!=RuntimeSlaveMinerPhase::MovingToOre
                            &&task->second.phase!=RuntimeSlaveMinerPhase::Harvesting))return false;
                    const auto p=body(*w);const RuntimeCellPoint bodyCell{
                        static_cast<int>(std::lround(p[0])),static_cast<int>(std::lround(p[1]))};
                    const auto* shooter=findObjectById(s,gunners.at(n++));if(!shooter)return false;const auto source=body(*shooter);
                    if(runtimeCellDistanceToObjectFootprint(s,runtimeCellPointForObject(*w),*h)==0.0
                        ||runtimeCellDistanceToObjectFootprint(s,bodyCell,*h)==0.0
                        ||std::hypot(p[0]-source[0],p[1]-source[1])>gun->range)return false;
                }return true;},"five original full-health empty workers did not genuinely emerge toward their own ore within native AWP range"))return false;
            const std::vector<int> victims(initialIds.begin(),initialIds.end());
            if(!killWorkers(victims))return false;
        }else{
            stage="actual partial harvesting before repack";
            if(!wait(hz*90,[&](){for(int id:initialIds){const auto* w=findObjectById(s,id);const auto& t=s.slaveMinerTasks.at(id);
                    if(!w||w->renderMoveActive||t.cargoUnits<1||t.cargoUnits>=capacity||t.phase!=RuntimeSlaveMinerPhase::Harvesting||t.workTicksRemaining<=2)return false;}return true;},"all five did not genuinely harvest partial original cargo"))return false;
            if(!toggle()||!wait(hz*20,[&](){const auto* h=home(s);return h&&h->type=="SMIN"&&!runtimeMissionIsUndeploying(*h);},"real undeploy did not produce the mobile home"))return false;
            if(ids(s)!=initialIds)return fail("undeploy replaced worker identities");
            stage="mobile-inflight";
            if(!moveHome({30,32})||!wait(hz*5,[&](){const auto* h=home(s);const auto b=h?s.behaviors.find(h->instanceId):s.behaviors.end();
                    return h&&h->renderMoveActive&&h->hasRenderPosition&&b!=s.behaviors.end()&&b->second.moveCooldownTicks>1;},"mobile home never began a real committed ground segment")
                ||!saveWindow()||!checkpoint())return false;
            // Ordinary format14 rebuilds the rest of this genuinely saved
            // committed miner segment. Let that original queued work complete
            // naturally before adding a different infantry path batch. Every
            // warm-up tick still compares all three bodies/tasks/cargo/credits.
            stage="mobile-inflight restore path reconstruction";
            if(!wait(hz*2,[&](){for(auto [name,q]:replicas()){
                    (void)name;const auto* h=home(*q);if(!h)return false;
                    const auto b=q->behaviors.find(h->instanceId);
                    if(b==q->behaviors.end()||b->second.kind!=RuntimeBehaviorKind::Move
                        ||b->second.pathRequestPending||b->second.path.empty())return false;
                }return true;},"ordinary14 native parent path reconstruction did not complete"))return false;
            for(auto [name,q]:replicas()){
                const auto* h=home(*q);if(!h||h->type!="SMIN"||!h->renderMoveActive||!h->hasRenderPosition)
                    return fail(name+" manual partial Return lost the real moving-home window");
            }
            const int manualWorker=*initialIds.begin();const int partial=s.slaveMinerTasks.at(manualWorker).cargoUnits;
            if(partial<1||partial>=capacity)return fail("manual mobile delivery requires a genuine partial load");
            stage="manual partial return to moving home";
            if(!manualOrder(manualWorker,{},true))return false;
            if(s.slaveMinerTasks.at(manualWorker).phase!=RuntimeSlaveMinerPhase::Returning||s.slaveMinerTasks.at(manualWorker).cargoUnits!=partial)
                return fail("manual mobile Return discarded original cargo/intent");
            if(!wait(hz*150,[&](){return storedCount(s)==workerCount;},"mobile workers did not continue mining and physically return into storage"))return false;
            ++manualMobile;if(mobileGathers==0||ids(s)!=initialIds)return fail("mobile form did not preserve identities and ongoing native ore harvesting");
            // Allow the natural special-mechanism end of frame to settle the
            // newly inactive bodies before the strict stored checkpoint.
            if(!step()||!step())return false;
            stage="stored-mobile";if(!checkpoint())return false;
            stage="stored parent movement and hold";
            if(!moveHome({28,32})||!wait(hz*20,[&](){const auto* h=home(s);return h&&h->x==28&&h->y==32&&!h->renderMoveActive&&!s.behaviors.contains(h->instanceId);},"stored mobile parent failed its real ground move"))return false;
            for(int n=0;n<hz*3;++n){if(!step()||storedCount(s)!=workerCount)return fail("stored worker emerged while home remained mobile");}
            stage="redeploy reuses stored original workers";
            if(!toggle()||!wait(hz*30,[&](){const auto* h=home(s);return h&&h->type=="YAREFN"&&h->buildAnimTicksRemaining==0&&storedCount(s)==0&&ids(s)==initialIds;},"redeploy did not reuse the same five stored IDs"))return false;
            stage="manual chosen ore";const RuntimeCellPoint explicitOre{40,34};
            if(!manualOrder(manualWorker,explicitOre,false))return false;
            if(s.slaveMinerTasks.at(manualWorker).targetOre!=explicitOre)return fail("manual ore Primary did not preserve the clicked original deposit");
            if(!wait(hz*60,[&](){const auto& t=s.slaveMinerTasks.at(manualWorker);return t.cargoUnits>0&&t.cargoUnits<capacity
                    &&t.targetOre==explicitOre&&!findObjectById(s,manualWorker)->renderMoveActive;},"manual chosen ore never produced real partial cargo"))return false;
            ++manualOre;const int carried=s.slaveMinerTasks.at(manualWorker).cargoValue;
            stage="manual partial return to deployed home";
            if(!manualOrder(manualWorker,{},true)||s.slaveMinerTasks.at(manualWorker).cargoValue!=carried)return fail("manual deployed Return lost real partial cargo");
            if(!wait(hz*60,[&](){const auto& t=s.slaveMinerTasks.at(manualWorker);return t.cargoUnits==0&&t.phase!=RuntimeSlaveMinerPhase::Stored;},"manual deployed return never delivered the actual partial load"))return false;
            ++manualBuilding;stage="automatic resume after manual deposited load";
            if(!wait(hz*10,[&](){const auto& t=s.slaveMinerTasks.at(manualWorker);const auto* w=findObjectById(s,manualWorker);
                    return w&&w->renderMoveActive&&t.cargoUnits==0&&t.phase==RuntimeSlaveMinerPhase::MovingToOre&&orePoint(t.targetOre);},"deployed worker did not automatically resume after manual delivery"))return false;
            ++autoResume;stage="single native AWP death";if(!killWorkers({manualWorker}))return false;
        }
        const int regenTicks=regenFrames*hz/15,deathTick=firstAwpDeathTick,due=deathTick+regenTicks-1;
        // Effects precedes Economy: an AWP death starts the raw converted
        // interval, then Economy decrements once on its birth tick. Later
        // missing workers must not restart that first death's deadline.
        const int expectedRemaining=regenTicks-(s.tick-deathTick+1);
        if(deathTick<0||regenRemaining(s)!=expectedRemaining||ids(s).size()!=static_cast<std::size_t>(workerCount-(batch?workerCount:1)))
            return fail("Destroyed must start one original Effects/Economy regen countdown: firstDeath="+std::to_string(deathTick)
                +" now="+std::to_string(s.tick)+" converted="+std::to_string(regenTicks)+" remaining="+std::to_string(regenRemaining(s))
                +" expected="+std::to_string(expectedRemaining));
        clocks<<label<<','<<hz<<','<<regenFrames<<','<<regenTicks<<','<<deathTick<<','<<s.tick<<','<<regenRemaining(s)<<','<<due<<'\n';clocks.flush();
        if(batch){stage="batch pending regen repack";
            if(!toggle()||!wait(hz*20,[&](){const auto* h=home(s);return h&&h->type=="SMIN"&&!runtimeMissionIsUndeploying(*h);},"batch home did not repack during regen")
                ||!moveHome({36,35})||!wait(hz*20,[&](){const auto* h=home(s);return h&&h->x==36&&h->y==35&&!h->renderMoveActive&&!s.behaviors.contains(h->instanceId);},"batch mobile home failed its genuine relocation"))return false;
        }
        stage=batch?"batch native gunner cleanup before countdown save":"single native gunner cleanup before countdown save";
        // Format14 deliberately does not promise exact unrelated infantry
        // routes. In both cases finish the real sniper standing order before
        // saving; the original worker set and death deadline remain unchanged.
        const auto cleanupSurvivors=ids(s);
        for(;;){
            if(s.tick>due-4||ids(s)!=cleanupSurvivors||regenRemaining(s)!=due-s.tick){
                std::string remaining;for(int id:gunners)remaining+=" / "+describe(s,id);
                return fail("natural cleanup exhausted the unchanged regen deadline: tick="+std::to_string(s.tick)+" due="+std::to_string(due)+remaining);}
            bool settled=true;for(int id:gunners){const auto* gunner=findObjectById(s,id);
                if(!gunner||gunner->health!=gunnerType->strength||gunner->owner!="British")return fail("native cleanup gunner identity/HP differs");
                const auto travel=s.playerCommands.travelling.find(id);
                if(travel==s.playerCommands.travelling.end()||travel->second.kind!=RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination)
                    return fail("original cleanup standing order disappeared: "+describe(s,id));
                if(!travel->second.guarding||gunner->renderMoveActive||s.behaviors.contains(id)){settled=false;continue;}
                const auto p=body(*gunner);const auto* h=home(s);
                if(!h)return fail("native cleanup home missing");
                const double radius=nativeGuardRadius(s,*gunner,*h);
                if(std::abs(radius-originalGuardScanRange)>1e-8)return fail("native guard scan changed during original cleanup");
                const auto withinGuardScan=[&](int x,int y){return std::hypot(p[0]-x,p[1]-y)<=radius+0.001
                    ||std::hypot(double(travel->second.destination.x-x),double(travel->second.destination.y-y))<=radius+0.001
                    ||std::hypot(double(gunner->x-x),double(gunner->y-y))<=radius+0.001;};
                // The stock 2x2 future deployed foundation and its complete
                // radius-one exit ring remain beyond the original range.
                for(int y=h->y-1;y<=h->y+2;++y)for(int x=h->x-1;x<=h->x+2;++x)
                    if(withinGuardScan(x,y))return fail("settled gunner can target the native home/exit ring: "+describe(s,id));
                for(int y=31;y<=35;++y)for(int x=37;x<=41;++x)
                    if(withinGuardScan(x,y))return fail("settled gunner can target an original ore approach: "+describe(s,id));
            }
            if(settled){gunnersParked=true;break;}
            if(!step())return false;
        }
        stage=batch?"batch-regen":"single-regen";
        if(!step()||!step()||!saveWindow())return false;
        if(s.tick>=due||regenRemaining(s)!=due-s.tick)return fail("repack/natural ticks restarted or advanced the original regen deadline");
        if(s.tick>due-2)return fail("native snapshot must retain at least two original countdown ticks");
        if(!checkpoint())return false;
        const auto survivors=ids(s);stage="strict countdown before due";
        while(s.tick<due-1){if(!step())return false;
            if(ids(s)!=survivors||regenRemaining(s)!=due-s.tick)return fail("workers appeared before the exact original regen deadline");}
        stage="native full missing-batch generation at due";
        if(!step()||s.tick!=due||ids(s).size()!=static_cast<std::size_t>(workerCount)||regenRemaining(s)!=0)
            return fail("due tick did not restore the entire missing count to exactly five");
        const auto renewed=ids(s);for(int id:renewed)if(!survivors.contains(id)){
            if(initialIds.contains(id))return fail("dead worker identity was reused instead of a new native worker");++replacements;}
        if(replacements!=(batch?workerCount:1))return fail("native replacement count differs from the actual loss");
        stage="replacement hold and no duplicates";
        for(int n=0;n<hz*3;++n){if(!step()||ids(s)!=renewed)return fail("regen duplicated or replaced surviving workers");
            if(batch&&storedCount(s)!=workerCount)return fail("mobile regeneration released stored new workers before deployment");}
        if(batch){stage="new batch exits only after real redeploy";
            if(!toggle()||!wait(hz*30,[&](){const auto* h=home(s);return h&&h->type=="YAREFN"&&h->buildAnimTicksRemaining==0&&storedCount(s)==0&&ids(s)==renewed;},"new stored batch failed real redeployment"))return false;
        }
        stage="replacement automatic mining";bool newMined=false;
        if(!wait(hz*60,[&](){for(int id:renewed)if(!survivors.contains(id)&&s.slaveMinerTasks.at(id).cargoUnits>0)newMined=true;return newMined;},"new batch did not automatically walk out and genuinely mine for its own home"))return false;
        if(!equal()||!inspect())return false;
        const int expectedSaves=batch?1:3;if(caseOrdinary!=expectedSaves||caseCanonical!=expectedSaves)return fail("required in-flight/stored/regen checkpoints incomplete");
        cases<<label<<','<<hz<<','<<workerCount<<','<<replacements<<','<<deaths<<','<<mobileGathers<<','<<manualMobile<<','<<manualOre<<','<<manualBuilding<<','<<autoResume<<','
            <<regenTicks<<','<<caseOrdinary<<','<<caseCanonical<<",PASS\n";cases.flush();events.flush();paths.flush();saved.flush();
        ++totalCases;totalDeaths+=deaths;totalReplacements+=replacements;
        std::cout<<"Slave miner lifecycle "<<label<<" PASS initial="<<workerCount<<" replacements="<<replacements<<" deaths="<<deaths
            <<" mobileGathers="<<mobileGathers<<" stored/redeploy/manual/nativePath=checked ordinary14="<<caseOrdinary<<" canonical="<<caseCanonical<<'\n';
    }
    if(totalCases==0)return fail("filter matched no scene");
    if(filter.empty()&&(totalCases!=4||ordinarySaves!=8||canonicalSaves!=8||totalDeaths!=12||totalReplacements!=12))return fail("incomplete native lifecycle coverage");
    RuntimeMapRenderContext render;render.rules=&live.rules;render.art=&live.art;render.assetRootPath=runtimeAssetRootPath(live.config);
    render.palettePath=runtimePalettePath(live.config,live.report.map);render.structurePalettePath=runtimeStructurePalettePath(live.config,live.report.map);
    if(!runRuntimeSlaveMinerRenderContractCheck(render,error))return false;
    std::cout<<"Slave miner native lifecycle passed: cases="<<totalCases<<" deaths="<<totalDeaths<<" replacements="<<totalReplacements
        <<" ordinary14="<<ordinarySaves<<" canonicalExtensions="<<canonicalSaves<<" originalRulesHash="<<rulesHash<<'\n';
    error.clear();return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
