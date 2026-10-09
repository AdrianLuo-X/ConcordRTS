// Issue25.12: original dolphins, real commands and observed sonic damage.
// Include after the other Issue25 smoke probes and TypeImmune implementation.
#pragma optimize("", off)
bool runRuntimeDolphinDamageIssue25Smoke(const RuntimeConfig& config,std::string& error) {
    std::cout<<std::unitbuf;
    const char* requested=std::getenv("CONCORD_DOLPHIN_DAMAGE_OUTPUT");
    const std::filesystem::path root=requested&&*requested?requested:"build/smoke/feature-set-25/12-dolphin-damage";
    std::filesystem::create_directories(root);std::string label;
    const auto fail=[&](const std::string& why){error="Issue25 dolphin "+label+": "+why;std::cout<<error<<'\n';return false;};
    try {
        auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
        assets->config.playerSlots.clear();assets->config.gameModeRulesOverride.clear();
        assets->report=buildRuntimeStartupReport(assets->config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
        if(!assets->report.success)return fail("original startup: "+assets->report.errorMessage);
        assets->rulesLoaded=assets->artLoaded=assets->theaterLoaded=true;
        IniFile native;if(!native.loadFromFile(assets->report.rulesPath))return fail("original rules unavailable");
        const auto yes=[](const std::string& v){const auto s=toLowerAsciiLocal(trimAsciiLocal(v));return s=="yes"||s=="true";};
        const auto* dolphin=assets->rules.findUnitType("UnitType","DLPH");
        if(!dolphin||dolphin->strength!=200||dolphin->armor!="light"||!dolphin->naval||!dolphin->typeImmune
            ||dolphin->primaryWeapon!="SonicZap"||dolphin->elitePrimaryWeapon!="SonicZapE"
            ||native.getValue("DLPH","Strength")!="200"||native.getValue("DLPH","Armor")!="light"
            ||!yes(native.getValue("DLPH","TypeImmune"))||!yes(native.getValue("DLPH","Underwater"))
            ||native.getValue("DLPH","NavalTargeting")!="5"||native.getValue("DLPH","LandTargeting")!="1")
            return fail("original DLPH identity/house immunity/domain");
        const auto* normal=assets->rules.findWeaponType("SonicZap");const auto* elite=assets->rules.findWeaponType("SonicZapE");
        const auto* wh=assets->rules.findWarheadType("SonicWarhead");const auto* proj=assets->rules.findProjectileType("Sonic");
        if(!normal||!elite||!wh||!proj||normal->damage!=4||normal->ambientDamage!=10||normal->rof!=120||normal->range!=6
            ||elite->damage!=8||elite->ambientDamage!=15||elite->rof!=80||elite->range!=6||elite->burst!=2
            ||!normal->sonic||!elite->sonic||normal->warhead!="SonicWarhead"||elite->warhead!="SonicWarhead"
            ||normal->projectile!="Sonic"||elite->projectile!="Sonic"||wh->cellSpread!=.1||wh->percentAtMax!=1)
            return fail("original normal/elite sonic fields");
        const auto* destroyer=assets->rules.findUnitType("UnitType","DEST");
        if(!destroyer||destroyer->armor!="heavy"||destroyer->strength!=600||!destroyer->naval
            ||native.getValue("DEST","Armor")!="heavy"||native.getValue("DEST","Strength")!="600")
            return fail("original DEST heavy armor/Strength control");
        const auto tokens=splitRuntimeTokenList(native.getValue("SonicWarhead","Verses"));
        if(tokens.size()!=11||tokens[3]!="100%"||tokens[5]!="80%"
            ||native.getValue("SonicZap","Damage")!="4"||native.getValue("SonicZap","AmbientDamage")!="10"
            ||native.getValue("SonicZapE","Damage")!="8"||native.getValue("SonicZapE","AmbientDamage")!="15"
            ||native.getValue("SonicZapE","Burst")!="2")return fail("original raw sonic damage and light/heavy multipliers");
        int landTile=-1,waterTile=-1;const auto metadata=runtimeTerrainMetadataContext(*assets);
        const int waterSet=assets->theater.getGeneralTileSetIndex("WaterSet");
        const auto* waterLand=assets->rules.findLandType("Water");
        if(waterSet<0||!waterLand||waterLand->floatSpeed!=100.0)return fail("original WaterSet/Float100");
        for(int tile=0;tile<int(assets->theater.getTotalTileSlots());++tile){TerrainCell c;c.tileIndex=tile;c.subTileIndex=0;
            const auto info=runtimeTerrainInfoForCell(*assets,c);
            const auto image=assets->theater.resolveTileIndex(tile);
            if(landTile<0&&info.passable&&!info.water&&!info.shore&&!info.cliff&&!info.bridge&&!info.ramp)landTile=tile;
            if(waterTile<0&&image.tileSetIndex==waterSet&&info.water&&!info.bridge&&!info.shore&&!info.ramp
                &&resolveRuntimeTerrainSubTileLandType(c,metadata,assets->renderCache)==RuntimeTerrainLandType::Water
                &&resolveRuntimeTerrainSubTileRampType(c,metadata,assets->renderCache)==0)waterTile=tile;
        }
        if(landTile<0||waterTile<0)return fail("original navigable clear/water TMP: landTile="+std::to_string(landTile)+" waterTile="+std::to_string(waterTile)+" WaterSet="+std::to_string(waterSet));
        TerrainCell selectedWater;selectedWater.tileIndex=waterTile;selectedWater.subTileIndex=0;
        const int rawWaterLand=int(resolveRuntimeTerrainSubTileLandType(selectedWater,metadata,assets->renderCache));
        const int rawWaterRamp=resolveRuntimeTerrainSubTileRampType(selectedWater,metadata,assets->renderCache);
        std::cout<<"Dolphin original terrain waterTile="<<waterTile<<" landTile="<<landTile<<" WaterSet="<<waterSet
            <<" rawWaterLand="<<rawWaterLand<<" rawWaterRamp="<<rawWaterRamp<<'\n';
        std::ofstream sources(root/"sources.txt"),cases(root/"cases.csv"),states(root/"states.csv"),commands(root/"commands.csv"),
            impacts(root/"impacts.csv"),shots(root/"shots.csv"),checkpoints(root/"checkpoints.csv");
        if(!sources||!cases||!states||!commands||!impacts||!shots||!checkpoints)return fail("evidence streams");
        sources<<"rules="<<assets->report.rulesPath<<"\nDLPH rawHP200/light; TypeImmune same House AND same type only.\n"
            <<"Normal4/direct+10/Ambient; Elite8/15/Burst2 pinned but this focused matrix uses normal stock units.\n"
            <<"SonicWarhead light100% heavy80% CellSpread.1 PercentAtMax1. No rule, HP, speed or firepower overrides.\n"
            <<"Visual terrain info does not populate movementLandType; selected water uses original TMP land type Water and ramp0. rawWaterLand="<<rawWaterLand<<" rawWaterRamp="<<rawWaterRamp<<"\n"
            <<"Native terrain index y*512+x; navigation runtimeCellKey uses1000. WaterSet="<<waterSet<<" waterTile="<<waterTile<<" landTile="<<landTile<<'\n'
            <<"Enemy reciprocity uses actual local Primary plus existing enemy AI Attack entry. Same/ally House controls use actual Ctrl Attack.\n"
            <<"Two unique physical files: full ordinary14 plus canonical extension; three reopen actions (canonical also reopens its base).\n";
        cases<<"case,hz,source_type,target_type,source_owner,target_owner,mutual,immune,accepted,rejected,source_hp,target_hp,shots,waves,damage_events,passed\n";
        states<<std::setprecision(17)<<"case,replica,tick,id,type,owner,hp,alive,x,y,z,body_x,body_y,body_z,airborne,mission,shots,waves\n";
        commands<<"case,channel,tick,dispatched,valid,verb,accepted,rejected,target,source\n";
        impacts<<std::setprecision(17)<<"case,replica,tick,actor,source,target,render,weapon,warhead,authored_damage,cell_x,cell_y,victim,owner,armor,before_hp,after_hp,expected_damage,actual_damage,passed\n";
        shots<<"case,replica,tick,actor,source,target,render,weapon,damage,spawn_tick,elapsed,remaining,progress\n";
        checkpoints<<"case,format,tick,world_equal,special_equal,sonic_equal,objects_equal,file,passed\n";
        const auto step=[](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);};
        const auto make=[&](const std::string& targetType,const std::string& targetOwner,bool allied){
            auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;seedRuntimeLoadScratch(s,*assets,false);
            s.world=assets->world;s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
            s.mapFacts={};s.mapFacts.theater=assets->mapFacts.theater;s.mapFacts.lighting=assets->mapFacts.lighting;s.mapFactsLoaded=true;
            s.config.fixedTickHz=30;s.config.playerSlots.clear();s.config.playerCountry="Americans";s.config.gameMode="Battle";
            s.config.gameModeRulesOverride.clear();s.config.noCooldown=s.config.computerAiEnabled=s.config.multiplayerActive=s.config.crates=s.config.superweapons=s.config.shortGame=false;
            s.config.skirmishLoadSavePath.clear();s.behaviors.clear();s.projectiles.clear();s.recentExpiredProjectiles.clear();
            s.economy={};s.economy.owner="Americans";s.economy.credits=10000;s.economy.skirmishStartApplied=true;
            s.triggers={};s.triggers.bootstrapComplete=true;s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=s.visionEnabled=false;
            s.runtimeSuppressPlayerAudioFeedback=true;s.initialOreFieldsSeeded=true;s.computerAiByOwner.clear();s.computerMiningByOwner.clear();
            s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=s.report.map.height=-1;
            s.skirmishTeamByOwner.clear();if(allied){s.skirmishTeamByOwner["americans"]="team1";s.skirmishTeamByOwner["french"]="team1";}
            for(int y=64;y<=90;++y)for(int x=64;x<=90;++x){const bool water=x<84;TerrainCell c;c.x=x;c.y=y;c.cellIndex512=y*512+x;
                c.tileIndex=water?waterTile:landTile;c.subTileIndex=0;s.world.accessTerrainCells().push_back(c);
                MapCellFacts f;f.x=x;f.y=y;f.cellIndex=c.cellIndex512;f.hasTerrain=true;f.terrain.x=x;f.terrain.y=y;
                f.terrain.tileIndex=c.tileIndex;f.terrain.subTileIndex=0;f.isWater=water;f.terrainClass=water?"water":"clear";f.passabilityHint="passable";
                s.mapFacts.cellIndexLookup.emplace(f.cellIndex,s.mapFacts.cells.size());s.mapFacts.cells.push_back(f);}
            s.terrainByCell=buildRuntimeTerrainLookup(s.world);initializeRuntimeNavigation(s);initializeRuntimeLocalProductionLanes(s.economy);
            ensureRuntimeSubcellsInitialized(s);ensureRuntimeSpecialMechanismsInitialized(s);ensureRuntimeLightingInitialized(s);ensureRuntimeParticlesInitialized(s);
            loadRuntimeSoundDatabase(s);initializeRuntimeOfflineGameplayRouting(s);
            const auto add=[&](int id,const std::string& type,const std::string& owner,int x){WorldObjectInstance o;o.instanceId=id;
                o.type=type;o.owner=owner;o.kind=WorldObjectKind::Unit;o.mapId=label+"-"+std::to_string(id);o.x=x;o.y=75;o.cell=runtimeCellKey(o.x,o.y);
                o.alive=o.poweredOperational=true;o.health=runtimeMaxHealthForObject(s,o);o.currentAmmo=-1;o.mission="Sleep";
                o.facing=o.turretFacing=runtimeFacingFromDelta(id==1?1:-1,0);clearRuntimeRenderPosition(o);s.world.accessObjects().push_back(o);};
            add(1,"DLPH","Americans",75);add(2,targetType,targetOwner,targetType=="MTNK"?85:80);refreshRuntimeObjectOccupancyAfterLocalChange(s);
            for(int id:{1,2}){const auto* o=findObjectById(s,id);const auto* f=runtimeMapQueryCell(s,runtimeCellPointForObject(*o));
                if(!f||f->isWater!=(o->type!="MTNK"))throw std::runtime_error("native map domain fixture");}
            step(s);return box;
        };
        struct Probe {std::map<int,std::array<int,2>> before;std::set<std::pair<int,int>> seen;
            std::set<int> shots,waves,damaged,firingSources;int damageEvents=0;std::string failure;};
        int fixtures=0,ordinaryLoads=0,canonicalLoads=0,reopens=0,paired=0,captures=0,totalEvents=0;
        const auto attach=[&](RuntimeWindowState& s,Probe& p,const char* replica){s.runtimeProjectileImpactObserver=[&,state=&s,probe=&p,replica](const RuntimeProjectileEffect& effect,bool after){
            if(effect.weaponId!="SonicZap")return;
            if(!after){std::array<int,2> hp{};for(int id:{1,2}){const auto* o=findRuntimeObjectByIdIncludingInactive(*state,id);hp[id-1]=o?o->health:-999;}
                probe->before[effect.actorId]=hp;return;}
            const auto old=probe->before.find(effect.actorId);if(old==probe->before.end()){probe->failure="impact has no before snapshot";return;}
            const auto* source=findRuntimeObjectByIdIncludingInactive(*state,effect.attackerObjectId);
            for(int id:{1,2}){const auto* victim=findRuntimeObjectByIdIncludingInactive(*state,id);if(!victim)continue;
                const bool reaches=id!=effect.attackerObjectId&&victim->x==effect.toCell.x&&victim->y==effect.toCell.y;
                const bool immune=source&&source->alive&&source->health>0&&source->type==victim->type&&source->owner==victim->owner&&victim->type=="DLPH";
                const int base=effect.renderIntent=="sonicAmbientPulse"?10:4;
                const double armor=victim->type=="DEST"?.8:1.0;
                const int expected=reaches&&!immune?std::min(old->second[id-1],int(std::lround(base*armor))):0;
                const int actual=old->second[id-1]-victim->health;const bool okay=actual==expected;
                impacts<<label<<','<<replica<<','<<state->tick<<','<<effect.actorId<<','<<effect.attackerObjectId<<','<<effect.targetObjectId<<','
                    <<effect.renderIntent<<','<<effect.weaponId<<','<<effect.warheadId<<','<<effect.damage<<','<<effect.toCell.x<<','<<effect.toCell.y<<','
                    <<id<<','<<victim->owner<<','<<(victim->type=="DEST"?"heavy":"light")<<','<<old->second[id-1]<<','<<victim->health<<','<<expected<<','<<actual<<','<<okay<<'\n';
                if(actual>0){probe->damaged.insert(id);++probe->damageEvents;}if(!okay&&probe->failure.empty())probe->failure="independent original per-impact damage victim="+std::to_string(id)
                    +" actual="+std::to_string(actual)+" expected="+std::to_string(expected)+" cell="+std::to_string(effect.toCell.x)+","+std::to_string(effect.toCell.y);
            }probe->before.erase(old);impacts.flush();};};        const auto sample=[&](RuntimeWindowState& s,Probe& probe,const char* replica){
            for(const auto& p:s.projectiles)if(p.weaponId=="SonicZap"&&probe.seen.emplace(p.actorId,p.spawnTick).second){
                if(p.renderIntent=="sonicWave")probe.waves.insert(p.actorId);else{probe.shots.insert(p.actorId);probe.firingSources.insert(p.attackerObjectId);}
                shots<<label<<','<<replica<<','<<s.tick<<','<<p.actorId<<','<<p.attackerObjectId<<','<<p.targetObjectId<<','<<p.renderIntent
                    <<','<<p.weaponId<<','<<p.damage<<','<<p.spawnTick<<','<<p.elapsedTicks<<','<<p.remainingTicks<<','<<p.progress<<'\n';}
            for(int id:{1,2}){const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);if(!o)continue;
                states<<label<<','<<replica<<','<<s.tick<<','<<id<<','<<o->type<<','<<o->owner<<','<<o->health<<','<<o->alive<<','
                    <<o->x<<','<<o->y<<','<<o->level<<','<<runtimeObjectRenderX(*o)<<','<<runtimeObjectRenderY(*o)<<','<<runtimeObjectRenderLevel(*o)
                    <<','<<o->airborne<<','<<o->mission<<','<<probe.shots.size()<<','<<probe.waves.size()<<'\n';}
            states.flush();shots.flush();};
        const auto sonicAuthority=[](const RuntimeWindowState& s){std::vector<std::string> list;
            for(auto p:s.projectiles)if(p.weaponId=="SonicZap"){
                // Full projectile authority includes all damage, physics and wave clocks.
                // Four UI-only launch pixel coordinates are not in ordinary14 authority.
                p.sourceScreenOffsetX=p.sourceScreenOffsetY=0;p.sourceNativeFixedX=p.sourceNativeFixedY=0;
                list.push_back(serializeRuntimeProjectileEffect(p));}return list;};
        const auto objectAuthority=[](const WorldObjectInstance& o){return std::make_tuple(o.instanceId,o.type,o.owner,o.alive,o.health,
            o.x,o.y,o.level,o.onBridge,o.airborne,o.facing,o.turretFacing,o.mission,o.veterancy,o.cloaked,
            runtimeObjectRenderX(o),runtimeObjectRenderY(o),runtimeObjectRenderLevel(o),o.weaponCooldownTicksRemaining);};
        const auto equal=[&](const RuntimeWindowState& a,const RuntimeWindowState& b,bool canonical){
            if(a.tick!=b.tick||a.specialMechanisms.stateHash()!=b.specialMechanisms.stateHash()||sonicAuthority(a)!=sonicAuthority(b))return false;
            for(int id:{1,2}){const auto* x=findRuntimeObjectByIdIncludingInactive(a,id);const auto* y=findRuntimeObjectByIdIncludingInactive(b,id);
                if(!x||!y||objectAuthority(*x)!=objectAuthority(*y))return false;}
            if(canonical){if(a.world.deterministicSimulationStateHash()!=b.world.deterministicSimulationStateHash()||a.behaviors.size()!=b.behaviors.size())return false;
                for(const auto& [id,v]:a.behaviors){const auto q=b.behaviors.find(id);if(q==b.behaviors.end()
                    ||runtimeMultiplayerSnapshotBehaviorRecord(id,v)!=runtimeMultiplayerSnapshotBehaviorRecord(id,q->second))return false;}}
            return true;};
        const auto capture=[&](RuntimeWindowState& s,const std::string& stage,bool wave){
            const auto stats=drawRuntimeStateOffscreenAtCell(s,{78,75},960,600,error,(root/(label+"-"+stage+".bmp")).string(),false);
            if(!stats||!stats->rendered||(wave&&(stats->sonicWavesDrawn<=0||stats->sonicWavePixels<=0)))return fail("actual original scene/wave rendering "+stage);
            ++captures;return true;};
        const auto savePair=[&](RuntimeWindowState& s,std::unique_ptr<RuntimeWindowState>& ordinary,std::unique_ptr<RuntimeWindowState>& canonical){
            const auto basePath=(root/(label+"-ongoing-ordinary14.ini")).string(),exactPath=(root/(label+"-ongoing-canonical.ini")).string();
            std::ostringstream text,extended,normalized;IniFile base,exact;
            if(!writeRuntimeStateToStream(s,text,error))return fail("complete save writer "+error);
            {std::ofstream f(basePath,std::ios::binary);f<<text.str();if(!f)return fail("physical ordinary write");}
            extended<<text.str();if(!writeRuntimeMultiplayerSnapshotExtension(s,extended,error)||!exact.loadFromString(extended.str())
                ||!writeRuntimeMultiplayerSnapshotCanonicalIni(exact,normalized,error))return fail("canonical writer "+error);
            {std::ofstream f(exactPath,std::ios::binary);f<<normalized.str();if(!f)return fail("physical canonical write");}
            for(bool isCanonical:{false,true}){if(!base.loadFromFile(basePath))return fail("physical base reopen");++reopens;
                if(isCanonical){if(!exact.loadFromFile(exactPath))return fail("physical extension reopen");++reopens;}
                auto copy=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*copy,s,false);
                {RuntimeSnapshotActiveStateScope active(*copy);if(!materializeRuntimeLoadStateFromIni(*copy,base,error)
                    ||(isCanonical&&!materializeRuntimeMultiplayerSnapshotExtension(*copy,exact,error)))return fail("complete restore "+error);}
                copy->runtimeSoundsEnabled=copy->runtimePreferencePersistenceEnabled=copy->visionEnabled=false;initializeRuntimeOfflineGameplayRouting(*copy);
                const bool okay=equal(s,*copy,isCanonical);checkpoints<<label<<','<<(isCanonical?"canonical":"ordinary14")<<','<<s.tick<<','
                    <<(s.world.deterministicSimulationStateHash()==copy->world.deterministicSimulationStateHash())<<','
                    <<(s.specialMechanisms.stateHash()==copy->specialMechanisms.stateHash())<<','<<(sonicAuthority(s)==sonicAuthority(*copy))<<',';
                bool objects=true;for(int id:{1,2})objects=objects&&objectAuthority(*findRuntimeObjectByIdIncludingInactive(s,id))==objectAuthority(*findRuntimeObjectByIdIncludingInactive(*copy,id));
                checkpoints<<objects<<','<<(isCanonical?exactPath:basePath)<<','<<okay<<'\n';checkpoints.flush();
                if(!okay)return fail("initial physical restore changed dolphin health/pose/wave clock "+std::string(isCanonical?"canonical":"ordinary14"));
                if(isCanonical){canonical=std::move(copy);++canonicalLoads;}else{ordinary=std::move(copy);++ordinaryLoads;}}
            return true;};
        struct Spec {const char* name;const char* target;const char* owner;bool mutual,force,immune,reject,save,allied;};
        const std::array<Spec,5> matrix{{
            {"enemy-reciprocal-original-DLPH","DLPH","Russians",true,false,false,false,true,false},
            {"same-House-Ctrl-original-immune","DLPH","Americans",false,true,true,false,false,false},
            {"other-allied-House-Ctrl-original-damage","DLPH","French",false,true,false,false,false,true},
            {"naval-DEST-original-heavy-damage","DEST","Russians",false,false,false,false,false,false},
            {"land-MTNK-original-rejected","MTNK","Russians",false,false,false,true,false,false}
        }};
        for(const auto& spec:matrix){label=spec.name;auto box=make(spec.target,spec.owner,spec.allied);auto& s=*box;RuntimeSnapshotActiveStateScope active(s);
            Probe live,oldProbe,exactProbe;attach(s,live,"live");std::unique_ptr<RuntimeWindowState> ordinary,canonical;
            const int beforeSource=findObjectById(s,1)->health,beforeTarget=findObjectById(s,2)->health;
            if(findObjectById(s,1)->veterancy!=0||findObjectById(s,2)->veterancy!=0||beforeSource!=200||beforeTarget!=std::stoi(native.getValue(spec.target,"Strength")))return fail("fixture changed original Strength");
            if(!runtimeObjectVisibleToOwner(s,"Americans",*findObjectById(s,2)))return fail("real Primary target is not visible through original sonar/sensors");
            if(!capture(s,"before",false))return false;s.selectedObjectIds={1};
            bool dispatched=false;
            if(spec.force){issueRuntimeAttackCommand(s,*findObjectById(s,2),true);dispatched=s.lastCommand.valid&&s.lastCommand.acceptedCount>0;}
            else dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                runtimeCellPointForObject(*findObjectById(s,2)),findObjectById(s,2));
            commands<<label<<','<<(spec.force?"actual-Ctrl-attack":"actual-Primary")<<','<<s.tick<<','<<dispatched<<','<<s.lastCommand.valid<<','
                <<s.lastCommand.verb<<','<<s.lastCommand.acceptedCount<<','<<s.lastCommand.rejectedCount<<",2,1\n";commands.flush();
            const int accepted=s.lastCommand.acceptedCount,rejected=s.lastCommand.rejectedCount;
            if(!s.lastCommand.valid||accepted!=(spec.reject?0:1)||rejected!=(spec.reject?1:0)||dispatched==spec.reject)
                return fail("real command result accepted="+std::to_string(accepted)+" rejected="+std::to_string(rejected)+" verb="+s.lastCommand.verb);
            if(spec.mutual){auto* enemy=findMutableObjectById(s,2);const auto* target=findObjectById(s,1);
                if(!enemy||!target||!runtimeComputerAiIssueAttack(s,*enemy,*target,false,0))return fail("real enemy reciprocal Attack unavailable");
                const auto it=s.behaviors.find(2);if(it==s.behaviors.end()||it->second.kind!=RuntimeBehaviorKind::Attack||it->second.targetObjectId!=1)
                    return fail("enemy Attack did not keep actual reciprocal target");
                commands<<label<<",actual-enemy-AI-Attack,"<<s.tick<<",1,1,Attack,1,0,1,2\n";commands.flush();}
            bool saved=false,middle=false;int quiet=0;sample(s,live,"live");
            for(int n=0;n<120;++n){step(s);sample(s,live,"live");
                if(!live.failure.empty()){capture(s,"damage-failure",false);return fail(live.failure);}
                if(ordinary){step(*ordinary);sample(*ordinary,oldProbe,"ordinary14");++paired;
                    if(!oldProbe.failure.empty()||!equal(s,*ordinary,false))return fail("ordinary full sonic continuation differs tick="+std::to_string(s.tick)+" "+oldProbe.failure);}
                if(canonical){step(*canonical);sample(*canonical,exactProbe,"canonical");++paired;
                    if(!exactProbe.failure.empty()||!equal(s,*canonical,true))return fail("canonical full sonic continuation differs tick="+std::to_string(s.tick)+" "+exactProbe.failure);}
                const auto moving=std::find_if(s.projectiles.begin(),s.projectiles.end(),[](const auto& p){return p.renderIntent=="sonicWave"&&p.progress>.1&&p.progress<.5;});
                if(!middle&&moving!=s.projectiles.end()){if(!capture(s,"ongoing-wave",true))return false;middle=true;}
                if(spec.save&&!saved&&moving!=s.projectiles.end()){
                    if(!savePair(s,ordinary,canonical))return false;attach(*ordinary,oldProbe,"ordinary14");attach(*canonical,exactProbe,"canonical");saved=true;}
                const bool hasWave=std::any_of(s.projectiles.begin(),s.projectiles.end(),[](const auto& p){return p.renderIntent=="sonicWave";});
                quiet=hasWave?0:quiet+1;
                if(spec.reject&&n>=29)break;
                if(!live.waves.empty()&&quiet>=15)break;
            }
            const auto* source=findRuntimeObjectByIdIncludingInactive(s,1);const auto* target=findRuntimeObjectByIdIncludingInactive(s,2);
            if(!source||!target)return fail("original object identity vanished");
            if(spec.reject){if(!live.shots.empty()||!live.waves.empty()||source->health!=beforeSource||target->health!=beforeTarget)
                return fail("rejected land order fired or damaged original units");}
            else {if(live.shots.empty()||live.waves.empty()||!middle||(spec.save&&!saved))return fail("actual original shot/advancing wave missing");
                if(spec.immune){if(target->health!=200||source->health!=200||live.damageEvents!=0)return fail("original same House immunity bypassed");}
                else if(!live.damaged.contains(2)||target->health>=beforeTarget)return fail("other House/naval original target never damaged");
                if(spec.mutual){if(!live.firingSources.contains(1)||!live.firingSources.contains(2)||!live.damaged.contains(1)||source->health>=200)return fail("enemy dolphins did not actually exchange sonic damage");}}
            if(!capture(s,"after",false))return false;totalEvents+=live.damageEvents;++fixtures;
            cases<<label<<",30,DLPH,"<<spec.target<<",Americans,"<<spec.owner<<','<<spec.mutual<<','<<spec.immune<<','<<accepted<<','<<rejected<<','
                <<source->health<<','<<target->health<<','<<live.shots.size()<<','<<live.waves.size()<<','<<live.damageEvents<<",1\n";cases.flush();
            std::cout<<"Dolphin "<<label<<" PASS HP="<<source->health<<'/'<<target->health<<" shots="<<live.shots.size()<<" waves="<<live.waves.size()<<" actualDamageEvents="<<live.damageEvents<<'\n';
        }
        if(fixtures!=5||ordinaryLoads!=1||canonicalLoads!=1||reopens!=3||paired<=0)return fail("exact focused census");
        std::ofstream result(root/"result.txt");result<<"PASS cases="<<fixtures<<" ordinary="<<ordinaryLoads<<" canonical="<<canonicalLoads
            <<" uniquePhysicalFiles=2 fileReopens="<<reopens<<" pairedTicks="<<paired<<" captures="<<captures<<" liveDamageEvents="<<totalEvents<<'\n';
        std::cout<<"Issue25 dolphin PASS cases="<<fixtures<<" physicalReads="<<ordinaryLoads+canonicalLoads<<" pairedTicks="<<paired<<" captures="<<captures<<'\n';return true;
    }catch(const std::exception& e){return fail(e.what());}
}
#pragma optimize("", on)