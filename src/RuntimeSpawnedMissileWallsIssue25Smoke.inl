// Issue25/10: original spawned payloads damage walls; mounted missiles are
// visible launch equipment, while released missiles remain real AA targets.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeSpawnedMissileWallsIssue25SmokeProbe(RuntimeWindowState& live,std::string& error) {
    std::cout<<std::unitbuf;
    std::string label="original-input",stage;
    const auto fail=[&](const std::string& why){error="Issue25 spawned missile walls: "+label+" ["+stage+"]: "+why;return false;};
    const char* output=std::getenv("CONCORD_SPAWNED_MISSILE_WALLS_ISSUE25_OUTPUT");
    const std::filesystem::path root=output&&*output?output:"build/smoke/feature-set-25/10-spawned-missile-walls";
    std::filesystem::create_directories(root);
    const char* requested=std::getenv("CONCORD_SPAWNED_MISSILE_WALLS_ISSUE25_CASE");
    const std::string subset=requested?requested:"";
    if(!subset.empty()&&subset!="walls"&&subset!="deck")return fail("CASE must be walls or deck; empty is the complete test");
    const auto quote=[](const std::string& v){std::string q="\"";for(char c:v){if(c=='"')q+='"';q+=c;}return q+'"';};
    const auto read=[](const std::string& p){std::ifstream f(p,std::ios::binary);return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};};
    const auto sha=[](std::span<const std::uint8_t> v){std::ostringstream s;s<<std::hex<<std::setfill('0');for(auto b:runtimeMultiplayerSnapshotSha256(v))s<<std::setw(2)<<unsigned(b);return s.str();};
    const auto asset=std::filesystem::path(runtimeAssetRootPath(live.config));
    const auto rulesPath=pathString(asset/"ra2md/localmd/rulesmd.ini"),artPath=pathString(asset/"ra2md/localmd/artmd.ini");
    IniFile raw,rawArt;
    if(sha(read(rulesPath))!="06761dd7f714e7d9400216ec3c06109ec5c1461f6a0727be7401eb9d8b0f6d05"
        ||!raw.loadFromFile(rulesPath)||!rawArt.loadFromFile(artPath))return fail("original stock rules/ART unavailable");
    const auto number=[&](const std::string& id,const char* key){return std::stod(raw.getValue(id,key,"-999"));};
    const auto yes=[&](const std::string& id,const char* key){return toLowerAsciiLocal(raw.getValue(id,key,"no"))=="yes";};
    const auto percent=[](const std::string& v){return std::stod(v)/100.0;};
    struct Stock {const char* parent;const char* child;const char* launcher;const char* prefix;const char* wh;const char* eliteWh;int slots,hp,regen,damage,eliteDamage,pause,tilt,speed;double minimum,range;};
    const std::array<Stock,2> stocks{{{"V3","V3ROCKET","V3Launcher","V3Rocket","V3WH","V3EWH",1,150,400,200,400,0,60,15,5,18},
        {"DRED","DMISL","DredLauncher","DMisl","DMISLWH","DMISLEWH",2,800,80,300,600,20,60,18,8,25}}};
    std::ofstream mapQueries(root/"map-query.csv");mapQueries<<"case,tick,id,type,x,y,native_index,query_exists,facts_class,facts_is_water,facts_is_bridge,expected_water,passed\n";
    std::ofstream poseInit(root/"fixture-pose-init.csv");poseInit<<std::setprecision(17)<<"case,stage,tick,id,type,old_has_pose,old_cache_x,old_cache_y,old_cache_level,logical_x,logical_y,logical_level,physical_before_x,physical_before_y,physical_before_level,new_has_pose,new_cache_x,new_cache_y,new_cache_level,physical_after_x,physical_after_y,physical_after_level,unchanged\n";
    if(!mapQueries||!poseInit)return fail("native map/fixture pose diagnostics output");
    std::ofstream originalInputs(root/"original-input.csv");originalInputs<<"case,stage,type,field,raw,loaded,expected,passed\n";if(!originalInputs)return fail("original input diagnostics output");
    const auto pin=[&](const RulesDatabase& r){
        for(const auto& p:stocks){const auto* u=r.findUnitType("UnitType",p.parent);const auto* child=r.findUnitType("AircraftType",p.child);
            const auto* w=r.findWeaponType(p.launcher);const auto* m=r.findSpawnedMissileProfile(p.child);const auto* life=r.findWeaponLifecycleProfile(p.launcher);
            if(!u||!child||!w||!m||!life){std::ostringstream actual;actual<<"unit="<<bool(u)<<" child="<<bool(child)<<" weapon="<<bool(w)<<" profile="<<bool(m)<<" lifecycle="<<bool(life);
                originalInputs<<quote(label)<<','<<quote(stage)<<','<<p.parent<<",presence,original-sections,"<<quote(actual.str())<<",all-present,0\n";originalInputs.flush();return fail(std::string("original pin failField=presence ")+p.parent+" "+actual.str());}
            bool fieldsOkay=true;std::string firstField;
            const auto check=[&](const char* id,const char* field,const std::string& authored,const auto& loaded,const auto& expected,bool rawOkay=true){const bool okay=loaded==expected&&rawOkay;
                std::ostringstream a,b;a<<std::setprecision(17)<<loaded;b<<std::setprecision(17)<<expected;
                originalInputs<<quote(label)<<','<<quote(stage)<<','<<id<<','<<field<<','<<quote(authored)<<','<<quote(a.str())<<','<<quote(b.str())<<','<<okay<<'\n';
                if(!okay&&firstField.empty())firstField=std::string(id)+"."+field+" raw="+authored+" loaded="+a.str()+" expected="+b.str();fieldsOkay=fieldsOkay&&okay;};
            check(p.parent,"Strength",raw.getValue(p.parent,"Strength"),u->strength,p.hp,number(p.parent,"Strength")==p.hp);
            check(p.parent,"Spawns",raw.getValue(p.parent,"Spawns"),u->spawns,std::string(p.child),raw.getValue(p.parent,"Spawns")==p.child);
            check(p.parent,"SpawnsNumber",raw.getValue(p.parent,"SpawnsNumber"),u->spawnsNumber,p.slots,number(p.parent,"SpawnsNumber")==p.slots);
            check(p.parent,"SpawnRegenRate",raw.getValue(p.parent,"SpawnRegenRate"),u->spawnRegenRate,p.regen,number(p.parent,"SpawnRegenRate")==p.regen);
            check(p.parent,"SpawnReloadRate",raw.getValue(p.parent,"SpawnReloadRate"),u->spawnReloadRate,0,number(p.parent,"SpawnReloadRate")==0);
            check(p.parent,"NoSpawnAlt",raw.getValue(p.parent,"NoSpawnAlt"),u->noSpawnAlt,true,yes(p.parent,"NoSpawnAlt"));
            check(p.child,"Strength",raw.getValue(p.child,"Strength"),child->strength,50,number(p.child,"Strength")==50);
            check(p.child,"Armor",raw.getValue(p.child,"Armor"),toLowerAsciiLocal(child->armor),std::string("special_2"),raw.getValue(p.child,"Armor")=="special_2");
            check(p.child,"Spawned",raw.getValue(p.child,"Spawned"),child->spawned,true,yes(p.child,"Spawned"));
            check(p.child,"MissileSpawn",raw.getValue(p.child,"MissileSpawn"),child->missileSpawn,true,yes(p.child,"MissileSpawn"));
            check(p.child,"Speed",raw.getValue(p.child,"Speed"),child->speed,p.speed,number(p.child,"Speed")==p.speed);
            check(p.launcher,"Range",raw.getValue(p.launcher,"Range"),w->range,p.range,number(p.launcher,"Range")==p.range);
            check(p.launcher,"MinimumRange",raw.getValue(p.launcher,"MinimumRange"),w->minimumRange,p.minimum,number(p.launcher,"MinimumRange")==p.minimum);
            check(p.launcher,"ROF",raw.getValue(p.launcher,"ROF"),w->rof,std::string(p.parent)=="V3"?150:50);
            // Original V3Launcher omits Burst. RulesDatabase stores zero;
            // ordinary firing interprets max(1,Burst), while DRED authors 2.
            const auto rawBurst=raw.getValue(p.launcher,"Burst");const int storedBurst=std::string(p.parent)=="V3"?0:2;
            check(p.launcher,"Burst-stored",rawBurst.empty()?"omitted":rawBurst,w->burst,storedBurst,std::string(p.parent)=="V3"?rawBurst.empty():rawBurst=="2");
            check(p.launcher,"Burst-effective",rawBurst.empty()?"omitted-default-one":rawBurst,std::max(1,w->burst),p.slots);
            check(p.launcher,"Spawner",raw.getValue(p.launcher,"Spawner"),w->spawner,true,yes(p.launcher,"Spawner"));
            check(p.launcher,"Warhead",raw.getValue(p.launcher,"Warhead"),w->warhead,std::string("Special"),raw.getValue(p.launcher,"Warhead")=="Special");
            check(p.launcher,"OmniFire",raw.getValue(p.launcher,"OmniFire","omitted-default-no"),life->omniFire,yes(p.launcher,"OmniFire"));
            check(p.launcher,"FireOnce",raw.getValue(p.launcher,"FireOnce","omitted-default-no"),life->fireOnce,yes(p.launcher,"FireOnce"));
            check(p.launcher,"DistributedFire",raw.getValue(p.launcher,"DistributedFire","omitted-default-no"),life->distributedFire,yes(p.launcher,"DistributedFire"));
            check(p.child,"profileType",raw.getValue("General",std::string(p.prefix)+"Type"),m->typeId,std::string(p.child));
            check(p.child,"profileDamage",raw.getValue("General",std::string(p.prefix)+"Damage"),m->damage,p.damage,number("General",(std::string(p.prefix)+"Damage").c_str())==p.damage);
            check(p.child,"profileEliteDamage",raw.getValue("General",std::string(p.prefix)+"EliteDamage"),m->eliteDamage,p.eliteDamage,number("General",(std::string(p.prefix)+"EliteDamage").c_str())==p.eliteDamage);
            const std::string whKey=std::string(p.parent)=="V3"?"V3Warhead":"DMislWarhead",eliteKey=std::string(p.parent)=="V3"?"V3EliteWarhead":"DMislEliteWarhead";
            check(p.child,"profileWarhead",raw.getValue("CombatDamage",whKey),m->warhead,std::string(p.wh));
            check(p.child,"profileEliteWarhead",raw.getValue("CombatDamage",eliteKey),m->eliteWarhead,std::string(p.eliteWh));
            check(p.child,"profilePauseFrames",raw.getValue("General",std::string(p.prefix)+"PauseFrames"),m->pauseFrames,p.pause);
            check(p.child,"profileTiltFrames",raw.getValue("General",std::string(p.prefix)+"TiltFrames"),m->tiltFrames,p.tilt);
            check(p.child,"profileAltitude",raw.getValue("General",std::string(p.prefix)+"Altitude"),m->altitudeLeptons,768);
            check(p.child,"ARTSpawnDelay",rawArt.getValue(p.child,"SpawnDelay"),rawArt.getValue(p.child,"SpawnDelay"),std::string("2"));
            check(p.child,"ARTVoxel",rawArt.getValue(p.child,"Voxel"),rawArt.getValue(p.child,"Voxel"),std::string("yes"));
            originalInputs.flush();if(!fieldsOkay)return fail("original pin failField="+firstField);
            if(!u||!child||!w||!m||!life||u->strength!=p.hp||u->spawns!=p.child||u->spawnsNumber!=p.slots
                ||u->spawnRegenRate!=p.regen||u->spawnReloadRate!=0||!u->noSpawnAlt
                ||child->strength!=50||!child->spawned||!child->missileSpawn||child->speed!=p.speed
                ||toLowerAsciiLocal(child->armor)!="special_2"||w->range!=p.range||w->minimumRange!=p.minimum||!w->spawner
                ||w->warhead!="Special"||w->rof!=(std::string(p.parent)=="V3"?150:50)||w->burst!=(std::string(p.parent)=="V3"?0:2)||std::max(1,w->burst)!=p.slots
                ||m->typeId!=p.child||m->damage!=p.damage||m->eliteDamage!=p.eliteDamage||m->warhead!=p.wh||m->eliteWarhead!=p.eliteWh
                ||m->pauseFrames!=p.pause||m->tiltFrames!=p.tilt||m->altitudeLeptons!=768
                ||life->omniFire!=yes(p.launcher,"OmniFire")||life->fireOnce!=yes(p.launcher,"FireOnce")||life->distributedFire!=yes(p.launcher,"DistributedFire")
                ||number(p.parent,"Strength")!=p.hp||raw.getValue(p.parent,"Spawns")!=p.child||number(p.parent,"SpawnsNumber")!=p.slots
                ||number(p.parent,"SpawnRegenRate")!=p.regen||number(p.parent,"SpawnReloadRate")!=0||!yes(p.parent,"NoSpawnAlt")
                ||number(p.child,"Strength")!=50||!yes(p.child,"Spawned")||!yes(p.child,"MissileSpawn")||yes(p.child,"Selectable")
                ||raw.getValue(p.child,"Armor")!="special_2"||number(p.child,"Speed")!=p.speed||raw.getValue(p.child,"Locomotor")!="{B7B49766-E576-11d3-9BD9-00104B972FE8}"
                ||number(p.launcher,"Range")!=p.range||number(p.launcher,"MinimumRange")!=p.minimum||number(p.launcher,"ROF")!=w->rof
                ||raw.getValue(p.launcher,"Warhead")!="Special"||!yes(p.launcher,"Spawner")
                ||number("General",(std::string(p.prefix)+"Damage").c_str())!=p.damage
                ||number("General",(std::string(p.prefix)+"EliteDamage").c_str())!=p.eliteDamage
                ||number("General",(std::string(p.prefix)+"PauseFrames").c_str())!=p.pause
                ||number("General",(std::string(p.prefix)+"TiltFrames").c_str())!=p.tilt
                ||rawArt.getValue(p.child,"SpawnDelay")!="2"||rawArt.getValue(p.child,"Voxel")!="yes")return fail(std::string("original pin failField=raw-contract-aggregate loaded fields matched; ")+p.parent+" Locomotor="+raw.getValue(p.child,"Locomotor")+" selectable="+raw.getValue(p.child,"Selectable"));
            for(const char* wh:{p.wh,p.eliteWh}){const auto* loaded=r.findWarheadType(wh);
                if(!yes(wh,"Wall")||!yes(wh,"Wood")||!loaded||!loaded->wall||!loaded->wood)return fail(std::string("original terminal wall permission ")+wh);}
        }
        for(const auto& w:std::array<std::pair<const char*,int>,2>{{{"GAWALL",300},{"GASAND",100}}}){const auto* b=r.findBuildingType(w.first);
            if(!b||!b->wall||b->strength!=w.second||number(w.first,"Strength")!=w.second||!yes(w.first,"Wall")
                ||rawArt.getValue(w.first,"Foundation")!="1x1"||rawArt.getValue(w.first,"ToOverlay")!=w.first)return fail("original wall foundation/strength");}
        const auto* cannon=r.findWeaponType("155mm");const auto* ball=r.findProjectileType("Ballistic");
        const auto* medusa=r.findWeaponType("Medusa");const auto* medusaP=r.findProjectileType("MedusaProjectile");
        if(!cannon||!ball||cannon->damage!=60||cannon->warhead!="ARTYHE"||cannon->projectile!="Ballistic"||!ball->ag||ball->aa
            ||number("155mm","Damage")!=60||number("ARTYHE","CellSpread")!=1||number("ARTYHE","PercentAtMax")!=.25
            ||raw.getValue("ARTYHE","Verses")!="100%,80%,60%,100%,60%,60%,100%,100%,60%,100%,100%"
            ||!medusa||!medusaP||medusa->damage!=100||medusa->warhead!="SAMWH"||!medusaP->aa||medusaP->ag
            ||number("General","VeteranCombat")!=1.1)return fail("original cannon/AA/armor inputs");
        return true;
    };
    if(!pin(live.rules))return false;
    std::ofstream cases(root/"cases.csv"),events(root/"events.csv"),commands(root/"commands.csv"),impacts(root/"impacts.csv"),checkpoints(root/"checkpoints.csv"),sources(root/"sources.txt");
    if(!cases||!events||!commands||!impacts||!checkpoints||!sources)return fail("private evidence output");
    for(auto* f:{&cases,&events,&commands,&impacts,&checkpoints,&sources})*f<<std::setprecision(17);
    cases<<"case,group,parent,rank,target,relation,children,shots,real_impacts,initial_parent_hp,final_parent_hp,initial_target_hp,final_target_hp,ordinary,canonical,paired_ticks,passed\n";
    events<<"case,stage,replica,tick,id,type,hp,alive,x,y,physical_x,physical_y,physical_z,airborne,docked_host,dock_slot,carrier_parent,carrier_slot,carrier_phase,missile_present,released,start_tick,release_tick,weapon,warhead,base_damage,multiplier,hidden,selectable,mission,behavior,target,status\n";
    commands<<"case,stage,tick,source,target,order,dispatched,valid,accepted,rejected,verb,cursor,before_x,before_y,after_x,after_y,point_x,point_y,picked_id\n";
    impacts<<"case,stage,replica,tick,actor,source,target,weapon,warhead,payload_base,damage_multiplier,spawned_payload,impact_x,impact_y,impact_cell_x,impact_cell_y,victim,victim_type,was_hp,now_hp,distance,raw_expected_damage,actual_hp_delta,was_airborne,was_docked,was_released,passed,declared_direct_force_target,affects_allies,affects_enemies\n";
    checkpoints<<"case,stage,format,tick,file,live_world_hash,world_hash,live_special_hash,special_hash,passed\n";
    sources<<"Original rules "<<rulesPath<<" SHA "<<sha(read(rulesPath))<<"\nOriginal ART "<<artPath<<" SHA "<<sha(read(artPath))
        <<"\nFull scope:16 realStructure walls and10 real deck/flight fixtures,4 physical full-file reads. CASE=walls/deck is a diagnostic subset and cannot claim full completion.\n"
        <<"Raw V3ROCKET/DMISL have Strength50,Armor special_2,Spawned/MissileSpawn yes,Selectable no. Selectable is not the damage oracle.\n"
        <<"Mounted expected protection applies to exact living registered parent/slot Docked relation, including natural unreleased Preparing; independent released world missile must remain targetable by AA.\n"
        <<"V3/DRED slots1/2; General base/elite payload200/400 and300/600. Launcher Special is a targeting marker, not the actual terminal V3WH/E or DMISLWH/E damage/Wall permission.\n"
        <<"Walls use original300 concrete/100 wood HP and1x1 foundations. Each real terminal impact independently checks raw payload,rank FIREPOWER1.1,Verses,CellSpread and actual HP delta.\n"
        <<"Mounted baseline is actual UI Attack of parent by original DEST155mm/ARTYHE60; real full simulation projectile impact must damage parent while every mounted50HP child survives. No damage,HP,speed,released flag or carrier relation is seeded.\n"
        <<"Mounted snapshots remain drawn but are not player selectable/hit-test targets. A real point query cannot be claimed as a child click when the registered child is already hidden for interaction.\n"
        <<"After release:real FV manual AA and powered NASAM automatic AA actual impacts; released parent sink uses original elite DRED volley,not an object-exit fabrication.\n"
        <<"Direct-force oracle permission is only the target of an actual accepted UI Ctrl order, with both declared target and projectile.targetObjectId matching. Allied bystanders still require original affectsAllies. Terminal WH/HP/payload/owner policy is unchanged.\n"
        <<"Fresh uncommanded fixture inputs only use standard clearRuntimeRenderPosition after actual pose==logical and no active/airborne segment guards. Ordinary14 reconstructs inactive caches; canonical preserves them. HP/dock/clock and actual pose are unchanged; no active Move/Attack/Preparing object is normalized. Both formats retain initial/full paired rawWorldHash exact.\n";sources.flush();
    auto metadata=runtimeTerrainMetadataContext(live);RuntimeMapRenderCache tileCache;std::optional<TerrainCell> land,water;
    const int waterSet=live.theater.getGeneralTileSetIndex("WaterSet");const auto* waterRule=live.rules.findLandType("Water");
    if(waterSet<0||!waterRule||waterRule->floatSpeed!=100.0||number("Water","Float")!=100.0)return fail("original WaterSet/Water.Float100 input");
    for(const auto& set:live.theater.getTileSets())for(int tile=set.startTileIndex;tile<set.startTileIndex+set.tilesInSet&&(!land||!water);++tile)
        for(int sub=0;sub<16&&(!land||!water);++sub){TerrainCell c;c.tileIndex=tile;c.subTileIndex=sub;const auto info=runtimeTerrainInfoForCell(live,c);
            if(info.passable&&!info.bridge&&!info.shore&&!info.cliff&&resolveRuntimeTerrainSubTileRampType(c,metadata,tileCache)==0){
                if(info.water&&!water&&live.theater.resolveTileIndex(tile).tileSetIndex==waterSet&&resolveRuntimeTerrainSubTileLandType(c,metadata,tileCache)==RuntimeTerrainLandType::Water)water=c;
                else if(!info.water&&!land)land=c;}}
    if(!land||!water)return fail("original flat TMP Clear and native WaterSet/Water/ramp0 unavailable");
    std::ofstream nativeWater(root/"native-water.csv"),moveCommands(root/"move-commands.csv");if(!nativeWater||!moveCommands)return fail("native water and real Move diagnostics output");
    nativeWater<<"tile,subtile,actual_set,original_water_set,raw_land,ramp,raw_float,loaded_float,passed\n";
    nativeWater<<water->tileIndex<<','<<water->subTileIndex<<','<<live.theater.resolveTileIndex(water->tileIndex).tileSetIndex<<','<<waterSet<<','<<int(resolveRuntimeTerrainSubTileLandType(*water,metadata,tileCache))<<','<<int(resolveRuntimeTerrainSubTileRampType(*water,metadata,tileCache))<<','<<quote(raw.getValue("Water","Float"))<<','<<waterRule->floatSpeed<<",1\n";nativeWater.flush();
    moveCommands<<std::setprecision(17)<<"case,stage,tick,id,type,source_x,source_y,goal_x,goal_y,can_receive,can_move,goal_query,goal_facts_class,goal_facts_water,goal_facts_bridge,native_index,tile,subtile,authored_land,raw_float,loaded_float,authored_permission,dispatched,valid,accepted,rejected,verb,interaction_status,simulation_status,behavior,state,accepted_goal_x,accepted_goal_y,path_size,path_pending,path\n";
    sources<<"Naval fixture source requires actual WaterSet, original TMP Water/ramp0 and original Water.Float100; no inferred large-ship shoreline radius. Water tile="<<water->tileIndex<<" sub="<<water->subTileIndex<<" set="<<waterSet<<"\n";sources.flush();
    struct Scene{std::unique_ptr<RuntimeWindowState> state;Stock stock;int parent=-1,target=-1,cannon=-1,aa=-1;int rank=0,realShots=0,realImpacts=0;std::set<int> originalChildren;};
    int total=0,wallCases=0,deckCases=0,ordinaryLoads=0,canonicalLoads=0,pairedTicks=0,bmps=0,observedImpacts=0,observedShots=0;
    const auto step=[](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);};
    // Ordinary14 intentionally reconstructs inactive pose caches at the
    // logical cell; canonical snapshots preserve even those unused bytes.
    // Only freshly created, uncommanded fixture inputs use the standard
    // initializer. Never normalize a combat, moving or Preparing object.
    const auto initializeInputPose=[&](RuntimeWindowState& s,WorldObjectInstance& o,const char* inputStage){
        const double x=runtimeObjectRenderX(o),y=runtimeObjectRenderY(o),z=runtimeObjectRenderLevel(o);
        if(o.renderMoveActive||o.airborne||x!=double(o.x)||y!=double(o.y)||z!=double(o.level))
            return fail("new fixture input has active or nonlogical actual pose: "+o.type+" #"+std::to_string(o.instanceId));
        const int hp=o.health,dock=o.dockedToObjectId,slot=o.dockIndex,cooldown=o.weaponCooldownTicksRemaining;
        poseInit<<quote(label)<<','<<inputStage<<','<<s.tick<<','<<o.instanceId<<','<<o.type<<','<<o.hasRenderPosition<<','<<o.renderX<<','<<o.renderY<<','<<o.renderLevel
            <<','<<o.x<<','<<o.y<<','<<o.level<<','<<x<<','<<y<<','<<z;
        clearRuntimeRenderPosition(o);
        const bool same=runtimeObjectRenderX(o)==x&&runtimeObjectRenderY(o)==y&&runtimeObjectRenderLevel(o)==z
            &&o.health==hp&&o.dockedToObjectId==dock&&o.dockIndex==slot&&o.weaponCooldownTicksRemaining==cooldown&&!o.renderMoveActive;
        poseInit<<','<<o.hasRenderPosition<<','<<o.renderX<<','<<o.renderY<<','<<o.renderLevel<<','<<runtimeObjectRenderX(o)<<','<<runtimeObjectRenderY(o)<<','<<runtimeObjectRenderLevel(o)<<','<<same<<'\n';poseInit.flush();
        return same||fail("standard fresh-input cache initialization moved body or changed HP/dock/clock");
    };
    const auto facade=[](RuntimeWindowState& s,const std::string& owner){s.economy.owner=owner;s.config.playerCountry=owner;s.humanHouseAuthorities.clear();RuntimeHumanHouseAuthority h;
        h.owner=h.country=owner;h.canonicalOwner=runtimeCanonicalHumanHouseOwner(owner);h.localFacade=true;s.humanHouseAuthorities.push_back(std::move(h));};
        const auto mapDomain=[&](RuntimeWindowState& s,int id,bool expectedWater){const auto* o=findObjectById(s,id);if(!o)return false;const auto* c=runtimeMapQueryCell(s,{o->x,o->y});
        const bool okay=c&&c->x==o->x&&c->y==o->y&&c->cellIndex==o->y*512+o->x&&toLowerAsciiLocal(c->terrainClass)==(expectedWater?"water":"clear")&&c->isWater==expectedWater&&!c->isBridge;
        mapQueries<<quote(label)<<','<<s.tick<<','<<id<<','<<o->type<<','<<o->x<<','<<o->y<<','<<o->y*512+o->x<<','<<bool(c)<<','<<(c?c->terrainClass:"missing")<<','<<(c&&c->isWater)<<','<<(c&&c->isBridge)<<','<<expectedWater<<','<<okay<<'\n';mapQueries.flush();
        if(!okay)fail("actual native512 MapFacts domain missing for "+o->type+" #"+std::to_string(id));return okay;};    const auto make=[&](Stock stock,int rank=0){Scene f;f.stock=stock;f.rank=rank;f.state=std::make_unique<RuntimeWindowState>();auto& s=*f.state;seedRuntimeLoadScratch(s,live,false);
        if(!pin(s.rules)){f.state.reset();return f;}
        s.tick=200;s.config.fixedTickHz=30;s.config.playerCountry="Russians";s.config.playerSlots.clear();s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=s.config.shortGame=s.config.crates=s.config.superweapons=false;
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=s.visionEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;s.campaignMode=s.skirmishMatchFinished=false;
        s.computerAiByOwner.clear();s.computerMiningByOwner.clear();s.humanHouseAuthorities.clear();s.skirmishTeamByOwner.clear();s.skirmishCountryByOwner.clear();s.skirmishDifficultyByOwner.clear();
        s.triggers={};s.triggers.bootstrapComplete=true;s.economy={};s.economy.owner="Russians";s.economy.credits=10000;s.economy.skirmishStartApplied=true;s.initialOreFieldsSeeded=true;
        s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();s.mapFacts={};s.mapFactsLoaded=true;
        s.terrainByCell.clear();s.terrainInfoByCell.clear();s.structureFootprints.clear();s.behaviors.clear();s.projectiles.clear();s.recentExpiredProjectiles.clear();s.impactEffects.clear();
        s.jumpjetFlights.clear();s.aircraftFlights.clear();s.deathWeapons.reset();s.pendingObjectExits.clear();s.pendingObjectExitIds.clear();s.selectedObjectIds.clear();s.playerCommands={};
        s.specialMechanismsInitialized=s.superWeaponsInitialized=s.subcellsInitialized=false;s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=s.report.map.height=-1;
        for(int y=44;y<=96;++y)for(int x=44;x<=96;++x){auto c=x<68?*water:*land;c.x=x;c.y=y;c.level=0;c.cellIndex512=y*512+x;s.world.accessTerrainCells().push_back(c);
            MapCellFacts v;v.x=x;v.y=y;v.cellIndex=c.cellIndex512;v.hasTerrain=true;v.terrain.x=x;v.terrain.y=y;v.terrain.tileIndex=c.tileIndex;v.terrain.subTileIndex=c.subTileIndex;
            v.terrainClass=x<68?"water":"clear";v.isWater=x<68;v.passabilityHint="passable";s.mapFacts.cellIndexLookup.emplace(v.cellIndex,s.mapFacts.cells.size());s.mapFacts.cells.push_back(v);}
        s.mapFacts.terrainCellCount=s.mapFacts.lookupIndexSize=s.mapFacts.cells.size();s.mapFacts.contentHash=0x25100000+rank+(std::string(stock.parent)=="DRED"?100:0);
        const RuntimeCellPoint p{std::string(stock.parent)=="V3"?68:64,64};
        auto* parent=addRuntimeObjectInstanceForOwner(s,"Russians",RuntimeBuildKind::Unit,stock.parent,p);if(!parent){f.state.reset();return f;}
        f.parent=parent->instanceId;if(!mapDomain(s,f.parent,std::string(stock.parent)=="DRED")){f.state.reset();return f;}parent->veterancy=rank;parent->facing=parent->turretFacing=runtimeFacingFromDelta(1,0);parent->mission="Sleep";
        for(const auto& owner:std::array<std::pair<const char*,RuntimeCellPoint>,2>{{{"Russians",{92,88}},{"Americans",{92,92}}}}){auto* b=addRuntimeObjectInstanceForOwner(s,owner.first,RuntimeBuildKind::Building,"GAPOWR",owner.second);
            if(!b){f.state.reset();return f;}b->mission="Sleep";b->buildAnimTicksRemaining=b->buildAnimTicksTotal=0;}
        initializeRuntimeNavigation(s);initializeRuntimeLocalProductionLanes(s.economy);rebuildRuntimeEconomyPower(s);ensureRuntimeSubcellsInitialized(s);ensureRuntimeSpecialMechanismsInitialized(s);
        facade(s,"Russians");initializeRuntimeOfflineGameplayRouting(s);s.particles.reset(30);s.particlesInitialized=s.particles.alignEmptyStateTick(s.tick);ensureRuntimeSuperWeaponsInitialized(s);ensureRuntimeLightingInitialized(s);
        step(s);
        // Initial docked equipment has just been naturally generated; no
        // player command or Preparing phase has started in this fixture.
        for(auto& o:s.world.accessObjects())if(!initializeInputPose(s,o,"make-uncommanded-idle")){f.state.reset();return f;}
        const auto pstate=s.specialMechanisms.carrierParents().find(f.parent);
        if(pstate==s.specialMechanisms.carrierParents().end()||pstate->second.capacity!=stock.slots||pstate->second.regenRate!=stock.regen||pstate->second.reloadRate!=0){f.state.reset();return f;}
        for(const auto& [slot,id]:pstate->second.childBySlot)if(id>=0)f.originalChildren.insert(id);
        if(f.originalChildren.size()!=std::size_t(stock.slots)){f.state.reset();return f;}
        return f;
    };
    const auto add=[&](Scene& f,const char* type,const char* owner,RuntimeBuildKind kind,RuntimeCellPoint cell){auto& s=*f.state;auto* b=addRuntimeObjectInstanceForOwner(s,owner,kind,type,cell);
        if(!b)return -1;const int id=b->instanceId;b->mission="Sleep";b->facing=b->turretFacing=runtimeFacingFromDelta(1,0);b->buildAnimTicksRemaining=b->buildAnimTicksTotal=0;
        if(!initializeInputPose(s,*b,"add-new-input"))return -1;
        refreshRuntimeObjectOccupancyAfterLocalChange(s);rebuildRuntimeEconomyPower(s);
        if(!mapDomain(s,id,std::string(type)=="DEST"||std::string(type)=="DRED"||std::string(type)=="AEGIS"))return -1;return id;};
    const auto row=[&](const RuntimeWindowState& s,int id,const char* replica){const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);if(!o)return;
        const auto c=s.specialMechanisms.carrierChildren().find(id);const auto m=s.specialMechanisms.spawnedMissiles().find(id);const auto b=s.behaviors.find(id);
        events<<quote(label)<<','<<quote(stage)<<','<<replica<<','<<s.tick<<','<<id<<','<<o->type<<','<<o->health<<','<<o->alive<<','<<o->x<<','<<o->y
            <<','<<runtimeObjectRenderX(*o)<<','<<runtimeObjectRenderY(*o)<<','<<runtimeObjectRenderLevel(*o)<<','<<o->airborne<<','<<o->dockedToObjectId<<','<<o->dockIndex
            <<','<<(c==s.specialMechanisms.carrierChildren().end()?-1:c->second.parentObjectId)<<','<<(c==s.specialMechanisms.carrierChildren().end()?-1:c->second.slot)
            <<','<<(c==s.specialMechanisms.carrierChildren().end()?-1:int(c->second.phase))<<','<<(m!=s.specialMechanisms.spawnedMissiles().end())
            <<','<<(m!=s.specialMechanisms.spawnedMissiles().end()&&m->second.released)<<','<<(m==s.specialMechanisms.spawnedMissiles().end()?-1:m->second.startTick)
            <<','<<(m==s.specialMechanisms.spawnedMissiles().end()?-1:m->second.releaseTick)<<','<<(m==s.specialMechanisms.spawnedMissiles().end()?"":m->second.weaponId)
            <<','<<(m==s.specialMechanisms.spawnedMissiles().end()?"":m->second.warheadId)<<','<<(m==s.specialMechanisms.spawnedMissiles().end()?0:m->second.damage)
            <<','<<(m==s.specialMechanisms.spawnedMissiles().end()?0:m->second.damageMultiplier)<<','<<runtimeSpecialObjectHiddenForInteraction(s,*o)
            <<','<<runtimeObjectAcceptsPlayerSelection(s,*o)<<','<<quote(o->mission)<<','<<(b==s.behaviors.end()?-1:int(b->second.kind))
            <<','<<(b==s.behaviors.end()?-1:b->second.targetObjectId)<<','<<quote(s.simulationStatus)<<'\n';};
    const auto capture=[&](Scene& f,const std::string& name){auto& s=*f.state;const auto* parent=findRuntimeObjectByIdIncludingInactive(s,f.parent);if(!parent)return fail("BMP parent missing");
        const auto wh=s.world.deterministicSimulationStateHash(),sp=s.specialMechanisms.stateHash();refreshRuntimeSpecialMechanismRenderSnapshots(s);
        const auto stats=drawRuntimeStateOffscreenAtCell(s,{parent->x,parent->y},900,600,error,pathString(root/(label+"-"+name+".bmp")),false);
        if(!stats||!stats->rendered)return fail("actual stock-material BMP: "+error);
        if(wh!=s.world.deterministicSimulationStateHash()||sp!=s.specialMechanisms.stateHash())return fail("drawing changed world/missile authority");++bmps;return true;};
    const auto order=[&](Scene& f,RuntimeWindowState& s,int source,int target,bool force=false){RuntimeSnapshotActiveStateScope active(s);const auto* a=findObjectById(s,source);const auto* t=findObjectById(s,target);
        if(!a||!t)return fail("actual UI actors missing");const double x=runtimeObjectRenderX(*a),y=runtimeObjectRenderY(*a);facade(s,a->owner);applySelection(s,{source},false,false);
        RECT client{0,0,1000,700};centerRuntimeCameraOnCell(s,client,{t->x,t->y});const RECT viewport=runtimeMapViewport(client);invalidateRuntimeObjectProjectionCache(s);POINT point{};bool hit=false;
        for(const auto& projected:runtimeProjectedObjectsForViewport(s,viewport)){if(!projected.object||projected.object->instanceId!=target)continue;const auto b=projected.bounds;
            for(int iy=1;iy<8&&!hit;++iy)for(int ix=1;ix<8&&!hit;++ix){POINT p{b.left+(b.right-b.left)*ix/8,b.top+(b.bottom-b.top)*iy/8};if(!PtInRect(&viewport,p))continue;
                const auto* resolved=runtimeCursorObjectAtPoint(s,viewport,p);if(resolved&&resolved->instanceId==target){point=p;hit=true;}}}
        if(!hit)return fail("actual point query did not resolve the real UI target");s.mouseClientPoint=point;s.mouseClientPointValid=true;
        const auto* picked=runtimeCursorObjectAtPoint(s,viewport,point);if(!picked||picked->instanceId!=target)return fail("actual UI target point changed");
        const auto intent=runtimeTargetCursorIntent(s,*picked);const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,force?RuntimeMultiplayerGameplayTargetOrderKind::Attack:RuntimeMultiplayerGameplayTargetOrderKind::Primary,{picked->x,picked->y},picked);
        const auto* after=findObjectById(s,source);commands<<quote(label)<<','<<quote(stage)<<','<<s.tick<<','<<source<<','<<target<<','<<(force?"force-attack":"Primary")<<','<<dispatched
            <<','<<s.lastCommand.valid<<','<<s.lastCommand.acceptedCount<<','<<s.lastCommand.rejectedCount<<','<<quote(s.lastCommand.verb)<<','<<int(intent)<<','<<x<<','<<y
            <<','<<(after?runtimeObjectRenderX(*after):-999)<<','<<(after?runtimeObjectRenderY(*after):-999)<<','<<point.x<<','<<point.y<<','<<picked->instanceId<<'\n';commands.flush();
        const auto b=s.behaviors.find(source);
        if(!dispatched||!s.lastCommand.valid||s.lastCommand.acceptedCount!=1||s.lastCommand.rejectedCount!=0||!after||s.selectedObjectIds!=std::vector<int>{source}
            ||b==s.behaviors.end()||b->second.kind!=RuntimeBehaviorKind::Attack||b->second.targetObjectId!=target||b->second.forceFire!=force
            ||std::abs(runtimeObjectRenderX(*after)-x)>1e-8||std::abs(runtimeObjectRenderY(*after)-y)>1e-8){(void)capture(f,"UI-failure");return fail("real UI attack was not accepted without a same-tick jump: "+s.interactionStatus);}
        if(!force&&intent!=RuntimeTargetCursorIntent::Attack&&intent!=RuntimeTargetCursorIntent::AttackOutOfRange)return fail("ordinary real target cursor not an attack");return true;};
    const auto stop=[&](RuntimeWindowState& s,int id){const auto* a=findObjectById(s,id);if(!a)return false;facade(s,a->owner);applySelection(s,{id},false,false);
        return dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop)&&s.lastCommand.valid&&s.lastCommand.acceptedCount==1&&s.lastCommand.rejectedCount==0;};
    const auto move=[&](RuntimeWindowState& s,int id,RuntimeCellPoint goal){const auto* a=findObjectById(s,id);if(!a)return false;facade(s,a->owner);applySelection(s,{id},false,false);
        const std::string type=a->type;const int x=a->x,y=a->y;const bool canReceive=runtimeObjectCanReceiveMoveCommand(s,*a),canMove=runtimeObjectCanMove(s,*a);
        const auto* facts=runtimeMapQueryCell(s,goal);const auto* terrain=findRuntimeTerrainCellAt(s,goal);const RuntimeTerrainInfo info=terrain?runtimeCachedTerrainInfoForCell(s,*terrain):RuntimeTerrainInfo{};
        const RuntimeTerrainLandType authored=info.authoredLandType;const char* nativeId=runtimeTerrainLandTypeRulesId(authored);const auto* nativeRule=nativeId?s.rules.findLandType(nativeId):nullptr;const bool permission=terrain&&runtimeAuthoredLandTypeAllowsMover(s,info,*a);
        const bool accepted=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,goal,nullptr);const auto b=s.behaviors.find(id);std::ostringstream path;if(b!=s.behaviors.end())for(const auto& p:b->second.path)path<<p.x<<':'<<p.y<<' ';
        moveCommands<<quote(label)<<','<<quote(stage)<<','<<s.tick<<','<<id<<','<<type<<','<<x<<','<<y<<','<<goal.x<<','<<goal.y<<','<<canReceive<<','<<canMove<<','<<bool(facts)<<','<<quote(facts?facts->terrainClass:"missing")<<','<<(facts&&facts->isWater)<<','<<(facts&&facts->isBridge)<<','<<(facts?facts->cellIndex:-1)<<','<<(terrain?terrain->tileIndex:-1)<<','<<(terrain?terrain->subTileIndex:-1)<<','<<int(authored)<<','<<quote(nativeId?raw.getValue(nativeId,"Float","omitted"):"unknown")<<','<<(nativeRule?nativeRule->floatSpeed:-1)<<','<<permission<<','<<accepted<<','<<s.lastCommand.valid<<','<<s.lastCommand.acceptedCount<<','<<s.lastCommand.rejectedCount<<','<<quote(s.lastCommand.verb)<<','<<quote(s.interactionStatus)<<','<<quote(s.simulationStatus)<<','<<(b==s.behaviors.end()?-1:int(b->second.kind))<<','<<(b==s.behaviors.end()?-1:int(b->second.state))<<','<<(b==s.behaviors.end()?-1:b->second.moveTarget.x)<<','<<(b==s.behaviors.end()?-1:b->second.moveTarget.y)<<','<<(b==s.behaviors.end()?0:b->second.path.size())<<','<<(b!=s.behaviors.end()&&b->second.pathRequestPending)<<','<<quote(path.str())<<'\n';moveCommands.flush();
        return accepted&&s.lastCommand.valid&&s.lastCommand.acceptedCount==1&&s.lastCommand.rejectedCount==0&&b!=s.behaviors.end()&&b->second.kind==RuntimeBehaviorKind::Move;};
    const auto mounted=[&](const Scene& f,const RuntimeWindowState& s){const auto* parent=findObjectById(s,f.parent);const auto p=s.specialMechanisms.carrierParents().find(f.parent);
        if(!parent||parent->health<=0||p==s.specialMechanisms.carrierParents().end())return false;
        for(int id:f.originalChildren){const auto* child=findObjectById(s,id);const auto c=s.specialMechanisms.carrierChildren().find(id);const auto m=s.specialMechanisms.spawnedMissiles().find(id);
            if(!child||child->health!=50||child->type!=f.stock.child||child->airborne||child->dockedToObjectId!=f.parent||child->dockIndex<0
                ||c==s.specialMechanisms.carrierChildren().end()||c->second.parentObjectId!=f.parent||c->second.slot!=child->dockIndex||c->second.phase!=CarrierChildPhase::Docked
                ||!p->second.childBySlot.contains(child->dockIndex)||p->second.childBySlot.at(child->dockIndex)!=id||(m!=s.specialMechanisms.spawnedMissiles().end()&&m->second.released))return false;
        }return true;};
        // All authoritative object fields below correspond to GameWorld.cpp's
    // WorldObjects/v3 hash record. Extra replay records are full serializers.
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
#undef ISSUE25_HASH_TERRAIN_FIELD
        return fields;};
    const auto compareDump=[&](const RuntimeWindowState& a,const RuntimeWindowState& b,const std::string& replica,const std::string& phase){
        const auto prefix=label+"-"+phase+"-"+replica;std::string e;
        for(const auto& v:std::array<std::pair<const char*,const RuntimeWindowState*>,2>{{{"live",&a},{"peer",&b}}}){std::ostringstream text;
            if(writeRuntimeStateToStream(*v.second,text,e)){std::ofstream file(root/(prefix+"-"+v.first+"-state14.ini"),std::ios::binary);file<<text.str();}}
        std::ofstream diff(root/(prefix+"-field-diff.csv"));diff<<"kind,id,field,live,peer\n";
        const auto diffFields=[&](const char* kind,int id,const auto& left,const auto& right){std::set<std::string> keys;for(const auto& [k,v]:left)keys.insert(k);for(const auto& [k,v]:right)keys.insert(k);
            for(const auto& k:keys){const auto l=left.find(k),r=right.find(k);const std::string lv=l==left.end()?"<missing>":l->second,rv=r==right.end()?"<missing>":r->second;
                if(lv!=rv)diff<<kind<<','<<id<<','<<quote(k)<<','<<quote(lv)<<','<<quote(rv)<<'\n';}};
        std::map<int,std::map<std::string,std::string>> ao,bo,at,bt;
        for(const auto& o:a.world.getObjects())ao[o.instanceId]=objectFields(o);for(const auto& o:b.world.getObjects())bo[o.instanceId]=objectFields(o);
        for(const auto& o:a.world.getTerrainCells())at[runtimeCellKey(o.x,o.y)]=terrainFields(o);for(const auto& o:b.world.getTerrainCells())bt[runtimeCellKey(o.x,o.y)]=terrainFields(o);
        std::set<int> ids;for(const auto& [id,v]:ao)ids.insert(id);for(const auto& [id,v]:bo)ids.insert(id);for(int id:ids)diffFields("object",id,ao[id],bo[id]);
        ids.clear();for(const auto& [id,v]:at)ids.insert(id);for(const auto& [id,v]:bt)ids.insert(id);for(int id:ids)diffFields("terrain",id,at[id],bt[id]);
        for(std::size_t i=0;i<std::max(a.projectiles.size(),b.projectiles.size());++i){const std::string l=i<a.projectiles.size()?serializeRuntimeProjectileEffect(a.projectiles[i]):"<missing>",r=i<b.projectiles.size()?serializeRuntimeProjectileEffect(b.projectiles[i]):"<missing>";
            if(l!=r)diff<<"projectile,"<<i<<",fullSerializer,"<<quote(l)<<','<<quote(r)<<'\n';}
        ids.clear();for(const auto& [id,v]:a.behaviors)ids.insert(id);for(const auto& [id,v]:b.behaviors)ids.insert(id);
        for(int id:ids){const auto l=a.behaviors.find(id),r=b.behaviors.find(id);const std::string lv=l==a.behaviors.end()?"<missing>":runtimeMultiplayerSnapshotBehaviorRecord(id,l->second),rv=r==b.behaviors.end()?"<missing>":runtimeMultiplayerSnapshotBehaviorRecord(id,r->second);
            if(lv!=rv)diff<<"behavior,"<<id<<",fullSerializer,"<<quote(lv)<<','<<quote(rv)<<'\n';}
        diff<<"summary,-1,worldHash,"<<a.world.deterministicSimulationStateHash()<<','<<b.world.deterministicSimulationStateHash()<<'\n'
            <<"summary,-1,specialHash,"<<a.specialMechanisms.stateHash()<<','<<b.specialMechanisms.stateHash()<<'\n';};    const auto checkpoint=[&](Scene& f,bool canonical){auto& s=*f.state;std::ostringstream ordinary,normalized;IniFile base,exact;
        if(!writeRuntimeStateToStream(s,ordinary,error)||!base.loadFromString(ordinary.str())||base.getValue("RuntimeSave","Version")!="14")return std::unique_ptr<RuntimeWindowState>{};
        const auto ordinaryPath=pathString(root/(label+"-"+stage+"-ordinary14.ini"));{std::ofstream file(ordinaryPath,std::ios::binary);file<<ordinary.str();if(!file)return std::unique_ptr<RuntimeWindowState>{};}
        if(!base.loadFromFile(ordinaryPath))return std::unique_ptr<RuntimeWindowState>{};std::string filePath=ordinaryPath;
        if(canonical){std::ostringstream expanded;expanded<<ordinary.str();if(!writeRuntimeMultiplayerSnapshotExtension(s,expanded,error)||!exact.loadFromString(expanded.str())
                ||!writeRuntimeMultiplayerSnapshotCanonicalIni(exact,normalized,error))return std::unique_ptr<RuntimeWindowState>{};filePath=pathString(root/(label+"-"+stage+"-canonical.ini"));
            {std::ofstream file(filePath,std::ios::binary);file<<normalized.str();if(!file)return std::unique_ptr<RuntimeWindowState>{};}if(!exact.loadFromFile(filePath))return std::unique_ptr<RuntimeWindowState>{};}
        auto copy=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*copy,s,false);if(!pin(copy->rules))return std::unique_ptr<RuntimeWindowState>{};
        {RuntimeSnapshotActiveStateScope active(*copy);if(!materializeRuntimeLoadStateFromIni(*copy,base,error)||(canonical&&!materializeRuntimeMultiplayerSnapshotExtension(*copy,exact,error)))return std::unique_ptr<RuntimeWindowState>{};}
        copy->runtimeSoundsEnabled=copy->runtimePreferencePersistenceEnabled=copy->visionEnabled=false;initializeRuntimeOfflineGameplayRouting(*copy);
        const auto liveHash=s.world.deterministicSimulationStateHash();compareDump(s,*copy,canonical?"canonical":"ordinary14","checkpoint-initial");
        bool okay=copy->world.deterministicSimulationStateHash()==liveHash&&copy->tick==s.tick&&copy->specialMechanisms.stateHash()==s.specialMechanisms.stateHash()&&copy->projectiles.size()==s.projectiles.size()&&mounted(f,*copy);
        for(const auto& a:s.world.getObjects()){const auto* b=findRuntimeObjectByIdIncludingInactive(*copy,a.instanceId);okay=okay&&b&&b->type==a.type&&b->health==a.health&&b->alive==a.alive&&b->airborne==a.airborne
            &&b->dockedToObjectId==a.dockedToObjectId&&b->dockIndex==a.dockIndex&&std::abs(runtimeObjectRenderX(*b)-runtimeObjectRenderX(a))<1e-8
            &&std::abs(runtimeObjectRenderY(*b)-runtimeObjectRenderY(a))<1e-8&&std::abs(runtimeObjectRenderLevel(*b)-runtimeObjectRenderLevel(a))<1e-8;}
        checkpoints<<quote(label)<<','<<quote(stage)<<','<<(canonical?"canonical":"ordinary14")<<','<<s.tick<<','<<quote(filePath)<<','<<liveHash<<','<<copy->world.deterministicSimulationStateHash()<<','<<s.specialMechanisms.stateHash()<<','<<copy->specialMechanisms.stateHash()<<','<<okay<<'\n';checkpoints.flush();
        if(!okay){error="physical full-file read changed world/parent/slots/attached pose/missile authority";return std::unique_ptr<RuntimeWindowState>{};}
        canonical?++canonicalLoads:++ordinaryLoads;return copy;};    const auto presented=[&](Scene& f){auto& s=*f.state;refreshRuntimeSpecialMechanismRenderSnapshots(s);
        const auto& snapshots=s.specialMechanismRenderSnapshots;
        RECT client{0,0,1000,700};centerRuntimeCameraOnCell(s,client,{findObjectById(s,f.parent)->x,findObjectById(s,f.parent)->y});const RECT viewport=runtimeMapViewport(client);
        invalidateRuntimeObjectProjectionCache(s);const auto& projected=runtimeProjectedObjectsForViewport(s,viewport);
        for(int id:f.originalChildren){const auto* c=findObjectById(s,id);if(!c||!runtimeSpecialObjectHiddenForInteraction(s,*c)||runtimeObjectAcceptsPlayerSelection(s,*c))return fail("mounted real child incorrectly interactive");
            if(std::none_of(snapshots.begin(),snapshots.end(),[&](const auto& v){return v.kind=="spawnedMissile"&&v.objectId==id&&v.phase=="Docked"&&!v.resourceId.empty();}))return fail("mounted real model snapshot absent");
            if(std::any_of(projected.begin(),projected.end(),[&](const auto& v){return v.object&&v.object->instanceId==id;}))return fail("mounted child published in real point-picking candidates");
        }return capture(f,"visible-mounted-not-pickable");};
    const auto rawArmor=[&](const WorldObjectInstance& o){return toLowerAsciiLocal(raw.getValue(o.type,"Armor","none"));};
    const std::array<std::string,11> armorNames{{"none","flak","plate","light","medium","heavy","wood","steel","concrete","special_1","special_2"}};
    struct Before {int hp=0;bool alive=false,air=false,mounted=false,released=false;int x=0,y=0;std::string type,owner,armor;};
    struct ImpactWatch {std::vector<int> victims;std::map<int,std::map<int,Before>> before;std::map<int,double> outgoing;std::set<int> seen,positiveVictims,lethalVictims;int actual=0,positive=0,main=0,declaredDirectForceTarget=-1;bool protect=false;std::string problem;};
    const auto attach=[&](Scene& f,RuntimeWindowState& s,const char* replica,std::vector<int> victims,bool protect,int declaredDirectForceTarget=-1){auto w=std::make_shared<ImpactWatch>();w->victims=std::move(victims);w->protect=protect;
        if(declaredDirectForceTarget>=0){
            const auto source=s.selectedObjectIds.size()==1?s.behaviors.find(s.selectedObjectIds.front()):s.behaviors.end();
            if(!s.lastCommand.valid||s.lastCommand.acceptedCount!=1||s.lastCommand.rejectedCount!=0||s.lastCommand.verb!="force-attack"
                ||s.lastCommand.targetObjectId!=declaredDirectForceTarget||source==s.behaviors.end()||source->second.kind!=RuntimeBehaviorKind::Attack
                ||!source->second.forceFire||source->second.targetObjectId!=declaredDirectForceTarget)
                w->problem="declared direct force target lacks actual accepted UI Ctrl authority";
            else w->declaredDirectForceTarget=declaredDirectForceTarget;
        }
        s.runtimeProjectileImpactObserver=[&,w,state=&s,replica,stock=f.stock,rank=f.rank,parentId=f.parent](const RuntimeProjectileEffect& p,bool after){
            const bool wanted=p.spawnedMissileDamage||p.weaponId=="155mm"||p.weaponId=="HoverMissile"||p.weaponId=="RedEye2"||p.weaponId=="Medusa"
                ||p.weaponId=="V3Airburst"||p.weaponId=="V3Cluster";
            if(!wanted)return;
            if(!after){const auto* attacker=findRuntimeObjectByIdIncludingInactive(*state,p.attackerObjectId);const int sourceRank=p.attackerObjectId==parentId?rank:(attacker?attacker->veterancy:0);
                w->outgoing[p.actorId]=sourceRank>=1&&raw.getValue(attacker?attacker->type:stock.parent,sourceRank>=2?"EliteAbilities":"VeteranAbilities").find("FIREPOWER")!=std::string::npos?1.1:1.0;
                auto& record=w->before[p.actorId];for(int id:w->victims){const auto* o=findRuntimeObjectByIdIncludingInactive(*state,id);if(!o)continue;
                    Before v;v.hp=o->health;v.alive=o->alive;v.air=o->airborne;v.x=o->x;v.y=o->y;v.type=o->type;v.owner=o->owner;v.armor=rawArmor(*o);
                    v.mounted=o->dockedToObjectId>=0&&state->specialMechanisms.carrierChildren().contains(id)&&state->specialMechanisms.carrierChildren().at(id).phase==CarrierChildPhase::Docked;
                    const auto m=state->specialMechanisms.spawnedMissiles().find(id);v.released=m!=state->specialMechanisms.spawnedMissiles().end()&&m->second.released;record[id]=v;}
                return;}
            ++w->actual;++observedImpacts;if(p.spawnedMissileDamage)++w->main;
            const auto records=w->before.find(p.actorId);if(records==w->before.end()){w->problem="real impact after without before observation";return;}
            for(const auto& [id,v]:records->second){const auto* o=findRuntimeObjectByIdIncludingInactive(*state,id);const int now=o?o->health:0;
                const double d=std::hypot(double(p.toCell.x-v.x),double(p.toCell.y-v.y));const double spread=std::stod(raw.getValue(p.warheadId,"CellSpread","0"));
                const double end=std::stod(raw.getValue(p.warheadId,"PercentAtMax","1"));const auto values=splitRuntimeTokenList(raw.getValue(p.warheadId,"Verses"));
                const auto ai=std::find(armorNames.begin(),armorNames.end(),v.armor);const int index=int(ai-armorNames.begin());
                int expected=0;double authored=0;
                if(p.spawnedMissileDamage){const bool dread=p.projectileImageId=="DMISL";const bool elite=p.warheadId==(dread?"DMISLEWH":"V3EWH");
                    const std::string terminal=dread?(elite?"DMISLEWH":"DMISLWH"):(elite?"V3EWH":"V3WH");authored=dread?(elite?600:300):(elite?400:200);
                    if((p.projectileImageId!="DMISL"&&p.projectileImageId!="V3ROCKET")||p.spawnedMissileBaseDamage!=authored||p.warheadId!=terminal||!yes(terminal,"Wall"))w->problem="original real missile payload identity changed";
                }else{authored=std::stod(raw.getValue(p.weaponId,"Damage","-999"));if(authored<0||raw.getValue(p.weaponId,"Warhead")!=p.warheadId)w->problem="real shot original raw weapon identity missing";}
                const bool domain=v.air?p.aa:p.ag;
                // Actual accepted Ctrl direct targets do not authorize allied splash.
                const bool explicitForceDirect=id==w->declaredDirectForceTarget&&p.targetObjectId==id;
                const bool relationship=v.owner!=p.owner||p.affectsAllies||explicitForceDirect;
                const bool inRadius=id==p.targetObjectId||(spread>0&&d<=spread);
                const double originalOutgoing=w->outgoing.at(p.actorId);

                if(std::abs(p.veteranDamageMultiplier-originalOutgoing)>1e-8)w->problem="original FIREPOWER snapshot multiplier changed";
                if(v.alive&&v.hp>0&&id!=p.attackerObjectId&&domain&&relationship&&inRadius&&!(w->protect&&v.mounted)){
                    if(index<0||index>=11||values.size()!=11){w->problem="original Verses/armor unavailable";continue;}
                    const double falloff=spread>0?1-(1-end)*std::clamp(d,0.0,spread)/std::max(.1,spread):1;
                    expected=std::min(v.hp,std::max(0,int(std::lround(authored*percent(values[index])*falloff*originalOutgoing))));}
                const int actual=v.hp-now;const bool okay=expected==actual;
                impacts<<quote(label)<<','<<quote(stage)<<','<<replica<<','<<state->tick<<','<<p.actorId<<','<<p.attackerObjectId<<','<<p.targetObjectId<<','<<p.weaponId<<','<<p.warheadId
                    <<','<<authored<<','<<p.veteranDamageMultiplier<<','<<p.spawnedMissileDamage<<','<<p.currentX<<','<<p.currentY<<','<<p.toCell.x<<','<<p.toCell.y<<','<<id<<','<<v.type
                    <<','<<v.hp<<','<<now<<','<<d<<','<<expected<<','<<actual<<','<<v.air<<','<<v.mounted<<','<<v.released<<','<<okay<<','<<w->declaredDirectForceTarget<<','<<p.affectsAllies<<','<<p.affectsEnemies<<'\n';
                if(!okay)w->problem="real original impact victim "+std::to_string(id)+" "+v.type+" expected="+std::to_string(expected)+" actual="+std::to_string(actual)+" weapon="+p.weaponId;
                if(actual>0){++w->positive;w->positiveVictims.insert(id);if(now==0)w->lethalVictims.insert(id);if(v.mounted&&w->protect)w->problem="mounted original missile received real splash damage";}
            }impacts.flush();
        };return w;};
    const auto missilePin=[&](const Scene& f,const RuntimeWindowState& s){for(const auto& [id,m]:s.specialMechanisms.spawnedMissiles()){if(m.parentObjectId!=f.parent)continue;
            const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);const int expectedStart=m.startTick;
            if(!o||o->type!=f.stock.child||m.typeId!=f.stock.child||m.weaponId!=f.stock.launcher||m.warheadId!=(f.rank>=2?f.stock.eliteWh:f.stock.wh)
                ||m.damage!=(f.rank>=2?f.stock.eliteDamage:f.stock.damage)||std::abs(m.damageMultiplier-(f.rank>=2?1.1:1.0))>1e-8
                ||m.releaseTick-expectedStart!=(f.stock.pause+f.stock.tilt)*2||m.slot<0||m.slot>=f.stock.slots)return false;
            if(m.released){if(!o->airborne||o->dockedToObjectId!=-1||s.specialMechanisms.carrierChildren().contains(id))return false;}
            else if(o->airborne||o->dockedToObjectId!=f.parent||o->health!=50)return false;
        }return true;};
    const auto preparing=[&](Scene& f){auto& s=*f.state;const auto* p=findObjectById(s,f.parent);if(!p)return false;
        f.target=add(f,"CAEAST01","Americans",RuntimeBuildKind::Building,{std::string(f.stock.parent)=="DRED"?88:80,64});
        if(f.target<0)return fail("actual distant original attack target unavailable");stage="launch-Primary";if(!order(f,s,f.parent,f.target))return false;
        for(int n=0;n<900;++n){step(s);if(!missilePin(f,s))return fail("natural preparing original phase/clock/payload");
            int count=0;for(const auto& [id,m]:s.specialMechanisms.spawnedMissiles())if(m.parentObjectId==f.parent&&!m.released)++count;
            // The Attack tail first creates missile records after this tick's
            // effects phase. Wait for the next genuine simulation frame to
            // publish Preparing bodies before taking an exact full-state save.
            // Never manufacture that pose or alter its original release clock.
            bool published=count==f.stock.slots&&mounted(f,s);
            for(int id:f.originalChildren){const auto* child=findObjectById(s,id);const auto m=s.specialMechanisms.spawnedMissiles().find(id);
                if(!child||m==s.specialMechanisms.spawnedMissiles().end()||m->second.released){published=false;continue;}
                const auto& missile=m->second;const int facing=runtimeFacingFromDelta(int(std::lround(std::cos(missile.heading)*100000)),int(std::lround(std::sin(missile.heading)*100000)));
                published=published&&missile.lastTick==s.tick&&child->mission=="MissilePreparing"&&child->hasRenderPosition&&child->renderMoveActive
                    &&child->facing==facing&&child->turretFacing==facing&&child->flightLevel==int(std::lround(missile.z))
                    &&std::abs(runtimeObjectRenderX(*child)-missile.x)<1e-8&&std::abs(runtimeObjectRenderY(*child)-missile.y)<1e-8&&std::abs(runtimeObjectRenderLevel(*child)-missile.z)<1e-8;
            }
            if(published){for(int id:f.originalChildren)row(s,id,"live");return true;}}
        return fail("normal Attack never reached original preparing slots");};
    const auto released=[&](Scene& f){auto& s=*f.state;if(!preparing(f))return false;
        for(int n=0;n<300;++n){step(s);if(!missilePin(f,s))return fail("release original payload/phase authority");
            int count=0;for(const auto& [id,m]:s.specialMechanisms.spawnedMissiles())if(m.parentObjectId==f.parent&&m.released&&f.originalChildren.contains(id))++count;
            if(count==f.stock.slots){for(int id:f.originalChildren)row(s,id,"live");return capture(f,"actual-release");}}
        return fail("natural preparing never released every original slot");};
    const auto paired=[&](RuntimeWindowState& a,const std::vector<std::unique_ptr<RuntimeWindowState>>& peers){for(const auto& b:peers){
            if(a.tick!=b->tick||a.world.deterministicSimulationStateHash()!=b->world.deterministicSimulationStateHash()||a.specialMechanisms.stateHash()!=b->specialMechanisms.stateHash()
                ||a.projectiles.size()!=b->projectiles.size())return false;
            for(std::size_t j=0;j<a.projectiles.size();++j)if(serializeRuntimeProjectileEffect(a.projectiles[j])!=serializeRuntimeProjectileEffect(b->projectiles[j]))return false;
        }return true;};
    const auto finish=[&](Scene& f,const char* group,const char* target,const char* relation,int shots,int realImpacts,int initialParent,int initialTarget,int firstOrd,int firstCan,int firstPair){
        const auto* p=findRuntimeObjectByIdIncludingInactive(*f.state,f.parent);const auto* t=findRuntimeObjectByIdIncludingInactive(*f.state,f.target);
        cases<<quote(label)<<','<<group<<','<<f.stock.parent<<','<<f.rank<<','<<target<<','<<relation<<','<<f.originalChildren.size()<<','<<shots<<','<<realImpacts
            <<','<<initialParent<<','<<(p?p->health:0)<<','<<initialTarget<<','<<(t?t->health:0)<<','<<ordinaryLoads-firstOrd<<','<<canonicalLoads-firstCan<<','<<pairedTicks-firstPair<<",1\n";cases.flush();
        ++total;std::string(group)=="walls"?++wallCases:++deckCases;std::cout<<"Issue25 missile "<<label<<" PASS\n";};
    if(subset.empty()||subset=="walls")for(const auto& stock:stocks)for(int rank:{0,2})for(bool own:{false,true})for(const char* wall:{"GAWALL","GASAND"}){
        label=std::string(stock.parent)+"-"+(rank?"elite":"normal")+"-"+(own?"own-Ctrl":"enemy-Primary")+"-"+wall;stage="structure-wall-fixture";auto f=make(stock,rank);
        if(!f.state)return fail("original fully loaded coast fixture "+error);auto& s=*f.state;const int initialParent=findObjectById(s,f.parent)->health;
        f.target=add(f,wall,own?"Russians":"Americans",RuntimeBuildKind::Building,{std::string(stock.parent)=="DRED"?80:78,64});
        if(f.target<0||!mounted(f,s))return fail("original whole wall/parent/slots unavailable");const int initialTarget=findObjectById(s,f.target)->health;
        if(initialParent!=stock.hp||initialTarget!=int(number(wall,"Strength")))return fail("original health was not preserved");
        if(!capture(f,"before-real-wall-click"))return false;stage="actual-wall-command";if(!order(f,s,f.parent,f.target,own))return false;
        auto watch=attach(f,s,"live",{f.target},false,own?f.target:-1);std::set<int> shotIds;int shots=0;bool dead=false;
        for(int n=0;n<3600;++n){step(s);if(!watch->problem.empty()){(void)capture(f,"impact-failure");return fail(watch->problem);}
            if(!missilePin(f,s))return fail("real wall launch violated original slot/clock/warhead contract");
            for(const auto& [id,m]:s.specialMechanisms.spawnedMissiles())if(m.parentObjectId==f.parent&&shotIds.insert(id).second){++shots;++observedShots;row(s,id,"live");}
            const auto* parent=findObjectById(s,f.parent);if(!parent||parent->health!=initialParent)return fail("real wall firing source died or lost health");
            const auto* t=findRuntimeObjectByIdIncludingInactive(s,f.target);if(t&&t->health==0&&!t->alive){dead=true;break;}
            if(n%120==0){row(s,f.parent,"live");row(s,f.target,"live");}}
        if(!dead||shots<1||watch->main<1||watch->positive<1)return fail("real original payload never destroyed selected whole wall");
        stage="actual-wall-destroyed";if(!capture(f,"wall-real-payload-destroyed"))return false;finish(f,"walls",wall,own?"own-force":"enemy",shots,watch->actual,initialParent,initialTarget,ordinaryLoads,canonicalLoads,pairedTicks);
    }
    const auto deckShell=[&](Scene& f,std::vector<std::unique_ptr<RuntimeWindowState>>& peers){auto& s=*f.state;const auto* p=findObjectById(s,f.parent);if(!p)return fail("cannon parent gone");
        if(f.cannon<0)f.cannon=add(f,"DEST",std::string(f.stock.parent)=="DRED"?"Russians":"Americans",RuntimeBuildKind::Unit,{std::string(f.stock.parent)=="V3"?67:63,64});if(f.cannon<0)return fail("original sea DEST cannon unavailable");
        std::vector<int> victims{f.parent};victims.insert(victims.end(),f.originalChildren.begin(),f.originalChildren.end());
        for(const auto& q:peers)if(!findObjectById(*q,f.cannon))return fail("checkpoint must include natural cannon before command");
        stage="real-parent-cannon-command";const bool ownCtrl=std::string(f.stock.parent)=="DRED";
        if(!order(f,s,f.cannon,f.parent,ownCtrl))return false;
        for(auto& q:peers)if(!order(f,*q,f.cannon,f.parent,ownCtrl))return false;
        // Bind the narrow oracle permission only after each real UI Ctrl
        // order was accepted; no simulation/impact can precede attachment.
        std::vector<std::shared_ptr<ImpactWatch>> watchers;watchers.push_back(attach(f,s,"live",victims,true,ownCtrl?f.parent:-1));
        for(std::size_t i=0;i<peers.size();++i)watchers.push_back(attach(f,*peers[i],i==0?"ordinary14":"canonical",victims,true,ownCtrl?f.parent:-1));
        bool emitted=false,landed=false;std::set<int> shotIds;const int hp=findObjectById(s,f.parent)->health;
        for(int n=0;n<240;++n){step(s);for(auto& q:peers){step(*q);++pairedTicks;}
            if(!paired(s,peers)){for(std::size_t i=0;i<peers.size();++i)compareDump(s,*peers[i],i==0?"ordinary14":"canonical","paired-failure-tick"+std::to_string(s.tick));return fail("physical restored mounted Preparing/world/projectile replay diverged; full state and authoritative field differences saved");}
            bool shot=false;for(const auto& p:s.projectiles)if(p.attackerObjectId==f.cannon&&p.weaponId=="155mm"&&shotIds.insert(p.actorId).second){shot=true;++observedShots;}
            if(shot&&!emitted){emitted=true;stage="cannon-shot-in-flight";if(!stop(s,f.cannon))return fail("normal cannon Stop failed");for(auto& q:peers)if(!stop(*q,f.cannon))return fail("restored normal cannon Stop failed");}
            for(const auto& w:watchers)if(!w->problem.empty()){stage="mounted-real-impact-failure";row(s,f.parent,"live");for(int id:f.originalChildren)row(s,id,"live");(void)capture(f,"mounted-real-splash-failure");return fail(w->problem);}
            if(!mounted(f,s))return fail("mounted original50HP child lost real parent/slot/phase during attack");
            if(watchers.front()->positive>0&&findObjectById(s,f.parent)->health<hp){landed=true;break;}}
        if(!emitted||!landed||watchers.front()->actual<1)return fail("real original155mm never hit/damaged mounted parent");
        f.realShots=int(shotIds.size());f.realImpacts=watchers.front()->actual;stage="real-parent-cannon-impact";row(s,f.parent,"live");for(int id:f.originalChildren)row(s,id,"live");return capture(f,"parent-real-hit-children50");};
    if(subset.empty()||subset=="deck"){
        for(const auto& stock:stocks)for(const char* state: {"parked","preparing","moving"}){
            label=std::string(stock.parent)+"-"+state+"-mounted-real-parent-splash";stage="deck-fixture";auto f=make(stock);if(!f.state)return fail("real original mounted fixture "+error);auto& s=*f.state;
            if(!mounted(f,s)||!presented(f))return false;const int hp=findObjectById(s,f.parent)->health;
            if(std::string(state)=="preparing"&&!preparing(f))return false;
            if(std::string(state)=="moving"){stage="parent-real-Move";const auto* p=findObjectById(s,f.parent);if(!move(s,f.parent,{p->x+2,p->y}))return fail("real parent Move rejected");
                bool motion=false;const double before=runtimeObjectRenderX(*findObjectById(s,f.parent));for(int n=0;n<180;++n){step(s);const auto* q=findObjectById(s,f.parent);
                    if(!q||!mounted(f,s))return fail("original child relation changed during real parent Move");if(q->renderMoveActive&&std::abs(runtimeObjectRenderX(*q)-before)>1e-7){motion=true;break;}}
                if(!motion)return fail("real original parent Move has no continuous motion");}
            std::vector<std::unique_ptr<RuntimeWindowState>> peers;const int firstOrd=ordinaryLoads,firstCan=canonicalLoads,firstPair=pairedTicks;
            const bool save=(std::string(stock.parent)=="V3"&&std::string(state)=="parked")||(std::string(stock.parent)=="DRED"&&std::string(state)=="preparing");
            if(save){const auto* p=findObjectById(s,f.parent);f.cannon=add(f,"DEST",std::string(stock.parent)=="DRED"?"Russians":"Americans",RuntimeBuildKind::Unit,{std::string(stock.parent)=="V3"?67:63,64});if(f.cannon<0)return fail("real checkpoint cannon input missing");
                stage=state;auto ordinary=checkpoint(f,false);const std::string ordinaryError=ordinary?"":error;
                // Both real physical formats are independently attempted and
                // logged, even if one initial exact-hash check fails.
                auto canonical=checkpoint(f,true);const std::string canonicalError=canonical?"":error;
                if(!ordinary||!canonical)return fail("full-file mounted reads ordinary14="+(ordinary?std::string("exact"):ordinaryError)+"; canonical="+(canonical?std::string("exact"):canonicalError));
                peers.push_back(std::move(ordinary));peers.push_back(std::move(canonical));}
            // deckShell reuses the real checkpoint cannon; non-save scenes add
            // it immediately before the same true command/damage pathway.
            if(!deckShell(f,peers))return false;finish(f,"deck",stock.child,state,f.realShots,f.realImpacts,hp,f.target>=0?int(number("CAEAST01","Strength")):0,firstOrd,firstCan,firstPair);
        }
        label="V3-real-Stop-cancels-preparing-then-parent-splash";stage="natural-preparing";auto stopped=make(stocks[0]);if(!stopped.state||!preparing(stopped))return false;
        const int stopHP=findObjectById(*stopped.state,stopped.parent)->health;stage="real-Stop";if(!stop(*stopped.state,stopped.parent))return fail("actual preparing Stop rejected");
        step(*stopped.state);if(!mounted(stopped,*stopped.state)||std::any_of(stopped.state->specialMechanisms.spawnedMissiles().begin(),stopped.state->specialMechanisms.spawnedMissiles().end(),[&](const auto& q){return q.second.parentObjectId==stopped.parent;}))return fail("real Stop did not return original preparing to mounted equipment");
        std::vector<std::unique_ptr<RuntimeWindowState>> none;if(!deckShell(stopped,none))return false;finish(stopped,"deck","V3ROCKET","real-Stop",stopped.realShots,stopped.realImpacts,stopHP,int(number("CAEAST01","Strength")),ordinaryLoads,canonicalLoads,pairedTicks);
        for(const auto& stock:stocks){label=std::string(stock.parent)+(std::string(stock.parent)=="V3"?"-released-real-FV-AA":"-released-real-powered-NASAM-auto-AA");stage="original-real-release";
            auto f=make(stock);if(!f.state||!released(f))return false;auto& s=*f.state;const int hp=findObjectById(s,f.parent)->health;
            const bool automatic=std::string(stock.parent)=="DRED";f.aa=add(f,automatic?"NASAM":"FV","Americans",automatic?RuntimeBuildKind::Building:RuntimeBuildKind::Unit,{69,66});
            if(f.aa<0)return fail("original AA input missing");if(automatic){auto* q=findMutableObjectById(s,f.aa);q->mission="Guard";rebuildRuntimeEconomyPower(s);if(!q->poweredOperational)return fail("natural original NASAM not powered");}
            auto w=attach(f,s,"live",std::vector<int>(f.originalChildren.begin(),f.originalChildren.end()),false);int shotCount=0;std::set<int> shots;bool destroyed=false;
            if(!automatic){stage="actual-released-AA-Primary";if(!order(f,s,f.aa,*f.originalChildren.begin()))return false;}
            stage=automatic?"actual-powered-auto-AA":"actual-manual-AA";
            for(int n=0;n<600;++n){step(s);for(const auto& p:s.projectiles)if(p.attackerObjectId==f.aa&&p.aa&&shots.insert(p.actorId).second){++shotCount;++observedShots;}
                if(!w->problem.empty()){(void)capture(f,"released-AA-failure");return fail(w->problem);}for(int id:f.originalChildren){const auto* c=findRuntimeObjectByIdIncludingInactive(s,id);
                    if(c&&c->health==0&&!c->alive&&w->lethalVictims.contains(id)&&!s.specialMechanisms.spawnedMissiles().contains(id)&&!s.specialMechanisms.carrierChildren().contains(id)){destroyed=true;row(s,id,"live");break;}}
                if(destroyed)break;}
            if(!destroyed||shotCount<1||w->actual<1)return fail("released independent original50HP missile was not destroyed by real AA shot");
            if(!capture(f,"released-real-AA-death"))return false;finish(f,"deck",stock.child,automatic?"auto-AA":"manual-AA",shotCount,w->actual,hp,int(number("CAEAST01","Strength")),ordinaryLoads,canonicalLoads,pairedTicks);
        }
        label="DRED-released-real-parent-sink-still-independent-AA";stage="natural-original-parent-sink-input";auto f=make(stocks[1]);if(!f.state)return fail("real sink original fixture");auto& s=*f.state;
        const int hp=findObjectById(s,f.parent)->health;f.cannon=add(f,"DRED","Americans",RuntimeBuildKind::Unit,{55,64});if(f.cannon<0)return fail("real enemy original DRED missing");
        findMutableObjectById(s,f.cannon)->veterancy=2;stage="real-parent-sink-Primary";if(!order(f,s,f.cannon,f.parent))return false;
        auto sink=attach(f,s,"live",{f.parent},false);bool sunk=false,survivingFlight=false;std::set<int> sinkShots;
        // Both original launch clocks run naturally together. The attacker
        // crosses nine cells; the own released volley crosses twenty-four.
        // No HP/delay/release edit creates the parent-death flight window.
        if(!released(f))return false;
        for(const auto& [id,m]:s.specialMechanisms.spawnedMissiles())if(m.parentObjectId==f.cannon)sinkShots.insert(id);
        for(int n=0;n<900;++n){step(s);if(!sink->problem.empty())return fail(sink->problem);for(const auto& [id,m]:s.specialMechanisms.spawnedMissiles())if(m.parentObjectId==f.cannon)sinkShots.insert(id);
            const auto* p=findRuntimeObjectByIdIncludingInactive(s,f.parent);if(p&&p->health==0&&!p->alive&&sink->lethalVictims.contains(f.parent)){sunk=true;
                for(int id:f.originalChildren){const auto* c=findObjectById(s,id);const auto m=s.specialMechanisms.spawnedMissiles().find(id);
                    if(c&&c->health==50&&c->airborne&&c->dockedToObjectId==-1&&m!=s.specialMechanisms.spawnedMissiles().end()&&m->second.released&&!s.specialMechanisms.carrierChildren().contains(id)){survivingFlight=true;row(s,id,"live");}}
                break;}}
        if(!sunk||!survivingFlight||sink->positive<1||sinkShots.empty())return fail("real original parent sinking did not leave a live released independent missile");
        stage="released-after-real-parent-sink";if(!capture(f,"released-child-survives-parent-sink"))return false;
        f.aa=add(f,"AEGIS","Americans",RuntimeBuildKind::Unit,{66,66});if(f.aa<0)return fail("real post-sink original Medusa AA missing");
        int target=-1;for(int id:f.originalChildren)if(findObjectById(s,id)&&s.specialMechanisms.spawnedMissiles().contains(id))target=id;
        auto aa=attach(f,s,"live",std::vector<int>(f.originalChildren.begin(),f.originalChildren.end()),false);stage="actual-after-parent-sink-AA-Primary";
        if(target<0||!order(f,s,f.aa,target))return false;std::set<int> aaShots;bool killed=false;
        for(int n=0;n<600;++n){step(s);if(!aa->problem.empty())return fail(aa->problem);for(const auto& p:s.projectiles)if(p.attackerObjectId==f.aa&&p.weaponId=="Medusa")aaShots.insert(p.actorId);
            const auto* c=findRuntimeObjectByIdIncludingInactive(s,target);if(c&&c->health==0&&!c->alive&&aa->lethalVictims.contains(target)&&!s.specialMechanisms.spawnedMissiles().contains(target)){killed=true;row(s,target,"live");break;}}
        if(!killed||aaShots.empty()){const auto* child=findRuntimeObjectByIdIncludingInactive(s,target);const auto* gun=findObjectById(s,f.aa);
            return fail("real post-sink AA did not kill independent missile; raw MedusaRange12 Float water x<68; child="+std::to_string(child?runtimeObjectRenderX(*child):-999)+","+std::to_string(child?runtimeObjectRenderY(*child):-999)+" gun="+std::to_string(gun?runtimeObjectRenderX(*gun):-999)+","+std::to_string(gun?runtimeObjectRenderY(*gun):-999));}
        finish(f,"deck","DMISL","real-parent-sink",int(sinkShots.size()+aaShots.size()),sink->actual+aa->actual,hp,int(number("CAEAST01","Strength")),ordinaryLoads,canonicalLoads,pairedTicks);
    }
    const bool complete=subset.empty();if((complete&&(total!=26||wallCases!=16||deckCases!=10||ordinaryLoads!=2||canonicalLoads!=2))
        ||(subset=="walls"&&(total!=16||wallCases!=16||deckCases!=0||ordinaryLoads||canonicalLoads))
        ||(subset=="deck"&&(total!=10||wallCases!=0||deckCases!=10||ordinaryLoads!=2||canonicalLoads!=2)))return fail("explicit authored/observed census mismatch");
    std::ofstream result(root/"result.txt");result<<"PASS "<<(complete?"complete-current-structure-wall-and-deck":"diagnostic-subset")<<"\nsubset="<<(complete?"all":subset)
        <<"\nscenes="<<total<<"\nstructureWalls="<<wallCases<<"\ndeck="<<deckCases<<"\nordinaryFullFileReads="<<ordinaryLoads<<"\ncanonicalFullFileReads="<<canonicalLoads
        <<"\npairedTicks="<<pairedTicks<<"\nactualImpactObservations="<<observedImpacts<<"\nactualShotObservations="<<observedShots<<"\nBMP="<<bmps
        <<"\nneutralOverlayWalls=not-in-this-first-runner;issue10-final-scope-remains-open\n";
    events.flush();impacts.flush();std::cout<<"Issue25 spawned missile walls "<<total<<" actual scenes, "<<ordinaryLoads+canonicalLoads<<" physical reads PASS "<<(complete?"current26":"diagnostic-subset")<<'\n';return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif