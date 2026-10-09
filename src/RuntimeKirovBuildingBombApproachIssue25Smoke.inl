// Issue25/9: original Kirov range, real building approach and physical release.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeKirovBuildingBombApproachIssue25SmokeProbe(RuntimeWindowState& live,std::string& error) {
    std::cout<<std::unitbuf;
    std::string label="original-input",stage;
    const auto fail=[&](const std::string& why){error="Issue25 Kirov building bomb: "+label+" ["+stage+"]: "+why;return false;};
    const char* out=std::getenv("CONCORD_KIROV_BUILDING_BOMB_APPROACH_ISSUE25_OUTPUT");
    const std::filesystem::path root=out&&*out?out:"build/smoke/feature-set-25/09-kirov-building-bomb-approach";
    std::filesystem::create_directories(root);
    const auto quote=[](const std::string& v){std::string q="\"";for(char c:v){if(c=='"')q+='"';q+=c;}return q+'"';};
    const auto read=[](const std::string& p){std::ifstream in(p,std::ios::binary);return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};};
    const auto sha=[](std::span<const std::uint8_t> v){const auto h=runtimeMultiplayerSnapshotSha256(v);std::ostringstream o;o<<std::hex<<std::setfill('0');for(auto b:h)o<<std::setw(2)<<unsigned(b);return o.str();};
    const auto asset=std::filesystem::path(runtimeAssetRootPath(live.config));
    const auto rulesPath=pathString(asset/"ra2md/localmd/rulesmd.ini"),artPath=pathString(asset/"ra2md/localmd/artmd.ini");
    IniFile raw,art;
    if(sha(read(rulesPath))!="06761dd7f714e7d9400216ec3c06109ec5c1461f6a0727be7401eb9d8b0f6d05"
        ||!raw.loadFromFile(rulesPath)||!art.loadFromFile(artPath))return fail("original stock rules/ART unavailable");
    const auto number=[&](const std::string& section,const char* key){return std::stod(raw.getValue(section,key,"-999"));};
    const auto yes=[&](const std::string& section,const char* key){return toLowerAsciiLocal(raw.getValue(section,key,"no"))=="yes";};
    std::ofstream lifecycleEvidence(root/"lifecycle.csv");
    if(!lifecycleEvidence)return fail("loaded original lifecycle evidence output");
    lifecycleEvidence<<"case,stage,weapon,profile_present,raw_omni,loaded_omni,raw_fire_once,loaded_fire_once,raw_distributed_fire,loaded_distributed_fire,passed\n";
    const auto lifecyclePin=[&](const RulesDatabase& database,const char* point){
        for(const char* id:{"BlimpBomb","BlimpBombE"}){
            const auto* profile=database.findWeaponLifecycleProfile(id);
            const bool rawOmni=yes(id,"OmniFire"),rawOnce=yes(id,"FireOnce"),rawDistributed=yes(id,"DistributedFire");
            const bool okay=profile&&profile->id==id&&profile->omniFire==rawOmni&&profile->fireOnce==rawOnce&&profile->distributedFire==rawDistributed;
            lifecycleEvidence<<quote(label)<<','<<point<<','<<id<<','<<(profile!=nullptr)<<','<<rawOmni<<','<<(profile?int(profile->omniFire):-1)
                <<','<<rawOnce<<','<<(profile?int(profile->fireOnce):-1)<<','<<rawDistributed<<','<<(profile?int(profile->distributedFire):-1)<<','<<okay<<'\n';lifecycleEvidence.flush();
            if(!okay)return fail(std::string("original WeaponLifecycle lost during ")+point+" weapon="+id+" present="+std::to_string(profile!=nullptr)
                +" rawOmni="+std::to_string(rawOmni)+" loadedOmni="+std::to_string(profile?int(profile->omniFire):-1)
                +" rawFireOnce="+std::to_string(rawOnce)+" loadedFireOnce="+std::to_string(profile?int(profile->fireOnce):-1)
                +" rawDistributed="+std::to_string(rawDistributed)+" loadedDistributed="+std::to_string(profile?int(profile->distributedFire):-1));
        }return true;
    };
    if(!lifecyclePin(live.rules,"initial-live-rules"))return false;
    const auto* psi=live.rules.findWeaponLifecycleProfile("PsiWave");
    if(!yes("PsiWave","FireOnce")||!psi||!psi->fireOnce
        ||psi->omniFire!=yes("PsiWave","OmniFire")||psi->distributedFire!=yes("PsiWave","DistributedFire"))
        return fail("original loaded PsiWave FireOnce=true and omitted lifecycle defaults");
    std::ofstream copyEvidence(root/"lifecycle-copy-contract.csv");
    if(!copyEvidence)return fail("loaded lifecycle copy/move evidence");
    copyEvidence<<"variant,weapon,source_present,destination_present,source_omni,destination_omni,source_fire_once,destination_fire_once,source_distributed,destination_distributed,passed\n";
    const std::size_t sourceWeaponCount=live.rules.getWeaponTypes().size();
    std::size_t lifecycleCopiesChecked=0;int lifecycleCopyVariants=0;
    const auto copyContract=[&](const RulesDatabase& destination,const char* variant){
        if(sourceWeaponCount==0||destination.getWeaponTypes().size()!=sourceWeaponCount)return fail(std::string("loaded weapon catalog size changed by ")+variant);
        for(const auto& weapon:live.rules.getWeaponTypes()){
            const auto* expected=live.rules.findWeaponLifecycleProfile(weapon.id);
            const auto* actual=destination.findWeaponLifecycleProfile(weapon.id);
            const bool okay=expected&&actual&&actual->id==expected->id&&actual->omniFire==expected->omniFire
                &&actual->fireOnce==expected->fireOnce&&actual->distributedFire==expected->distributedFire;
            copyEvidence<<variant<<','<<quote(weapon.id)<<','<<(expected!=nullptr)<<','<<(actual!=nullptr)
                <<','<<(expected?int(expected->omniFire):-1)<<','<<(actual?int(actual->omniFire):-1)
                <<','<<(expected?int(expected->fireOnce):-1)<<','<<(actual?int(actual->fireOnce):-1)
                <<','<<(expected?int(expected->distributedFire):-1)<<','<<(actual?int(actual->distributedFire):-1)<<','<<okay<<'\n';
            ++lifecycleCopiesChecked;
            if(!okay){copyEvidence.flush();return fail(std::string("loaded WeaponLifecycle copy/move lost fields: ")+variant+" weapon="+weapon.id);}
        }copyEvidence.flush();++lifecycleCopyVariants;return true;
    };
    {
        // The source is the real loaded database. No INI reload or flag
        // assignment can manufacture missing value-copy/move semantics.
        auto copied=std::make_unique<RulesDatabase>(live.rules);
        auto assigned=std::make_unique<RulesDatabase>();*assigned=live.rules;
        if(!copyContract(*copied,"copy-constructor")||!copyContract(*assigned,"copy-assignment"))return false;
        auto moved=std::make_unique<RulesDatabase>(std::move(*copied));
        auto moveAssigned=std::make_unique<RulesDatabase>();*moveAssigned=std::move(*assigned);
        if(!copyContract(*moved,"move-constructor")||!copyContract(*moveAssigned,"move-assignment"))return false;
    }
    const auto* native=live.rules.findUnitType("UnitType","ZEP");
    const auto* bombType=live.rules.findProjectileType("BlimpBombP");
    if(!native||!bombType||native->strength!=2000||native->jumpjetSpeed!=5||native->jumpjetClimb!=6||native->jumpjetCrash!=12
        ||native->jumpjetAccel!=10||native->jumpjetTurnRate!=2||native->airRangeBonus!=0
        ||number("ZEP","Strength")!=2000||number("ZEP","Speed")!=5||number("ZEP","JumpjetSpeed")!=5
        ||number("ZEP","JumpjetClimb")!=6||number("ZEP","JumpjetCrash")!=12||number("ZEP","JumpjetAccel")!=10
        ||number("ZEP","JumpjetTurnRate")!=2||number("ZEP","JumpjetHeight")!=750||!yes("ZEP","JumpjetNoWobbles")
        ||raw.getValue("ZEP","Locomotor")!="{92612C46-F71F-11d1-AC9F-006008055BB5}"
        ||raw.getValue("ZEP","AirRangeBonus","")!=""||raw.getValue("ZEP","Primary")!="BlimpBomb"
        ||raw.getValue("ZEP","ElitePrimary")!="BlimpBombE"||art.getValue("ZEP","PrimaryFireFLH")!="-50,0,-140"
        ||!yes("BlimpBombP","Vertical")||number("BlimpBombP","Acceleration")!=1||number("BlimpBombP","Arm")!=10
        ||number("BlimpBombP","DetonationAltitude")!=20000||raw.getValue("BlimpBombP","Image")!="ZBOMB")
        return fail("original ZEP/flight/FLH/BlimpBombP declaration mismatch");
    for(const auto* id:{"BlimpBomb","BlimpBombE"}){
        const auto* w=live.rules.findWeaponType(id);const auto wh=raw.getValue(id,"Warhead");
        if(!w||w->range!=1.5||number(id,"Range")!=1.5||number(id,"Damage")!=250||number(id,"Burst")!=1
            ||number(id,"ROF")!=50||number(id,"Speed")!=20||!yes(id,"CellRangefinding")||!yes(id,"OmniFire")
            ||raw.getValue(id,"Projectile")!="BlimpBombP"||number(wh,"CellSpread")!=2||number(wh,"PercentAtMax")!=.5
            ||raw.getValue(wh,"Verses")!="100%,100%,100%,70%,35%,35%,85%,75%,50%,100%,100%")
            return fail("original weapon range/spread/cadence differs");
    }
    const double range=number("BlimpBomb","Range"),cruise=number("ZEP","JumpjetHeight")/256.0;
    const double baseSpeed=number("ZEP","JumpjetSpeed"),veteranSpeed=number("General","VeteranSpeed"),veteranCombat=number("General","VeteranCombat");
    if(veteranSpeed!=1.2||veteranCombat!=1.1||raw.getValue("ZEP","VeteranAbilities")!="STRONGER,FIREPOWER,SIGHT,FASTER")return fail("original veteran speed/firepower");
    struct Spec{const char* id;int w,h,strength;double armor;};
    const std::array<Spec,3> specs{{{"GAPOWR",2,2,750,.85},{"YAGNTC",3,3,1000,.5},{"GAREFN",4,3,1000,.85}}};
    for(const auto& p:specs){const auto* b=live.rules.findBuildingType(p.id);
        if(!b||b->strength!=p.strength||number(p.id,"Strength")!=p.strength
            ||art.getValue(p.id,"Foundation")!=std::to_string(p.w)+"x"+std::to_string(p.h)
            ||raw.getValue(p.id,"Armor")!=(p.armor==.5?"concrete":"wood"))return fail("original building "+std::string(p.id));}
    std::ofstream states(root/"states.csv"),cases(root/"cases.csv"),shots(root/"shots.csv"),damage(root/"damage.csv"),saves(root/"checkpoints.csv"),sources(root/"sources.txt");
    if(!states||!cases||!shots||!damage||!saves||!sources)return fail("private evidence output");
    for(auto* p:{&states,&cases,&shots,&damage,&saves,&sources})*p<<std::setprecision(17);
    states<<"case,stage,replica,tick,source,navx,navy,physical_x,physical_y,physical_z,delta,max_delta,center_x,center_y,center_distance,in_foundation,physical_height,clearance_q16,active,stopped,speed,progress,length,mission,behavior,target,goalx,goaly,path,cooldown,weapon_cooldown,target_hp,cmd_valid,accepted,rejected,status\n";
    cases<<"case,type,direction,rank,hz,control,first_tick,first_distance,terminal_distance,terminal_limit,steady_ticks,shots,positive_impacts,ordinary,canonical,source_health,target_health,passed\n";
    shots<<"case,replica,tick,actor,spawn_tick,source,weapon,warhead,body_x,body_y,body_z,center_x,center_y,body_distance,source_x,source_y,source_z,target_x,target_y,current_x,current_y,current_z,vertical,unbound,first\n";
    damage<<"case,replica,tick,actor,target,release_cell_x,release_cell_y,original_foundation_distance,before_hp,after_hp,expected_delta,actual_delta,weapon,warhead\n";
    saves<<"case,stage,format,tick,source,physical_x,physical_y,physical_z,target_hp,file,passed\n";
    sources<<"Original rules "<<rulesPath<<" SHA "<<sha(read(rulesPath))<<"\nOriginal ART "<<artPath<<" SHA "<<sha(read(artPath))
        <<"\nZEP original JumpjetSpeed5/Accel10/TurnRate2/Climb6/Crash12/Height750/NoWobbles; cruise750/256.\n"
        <<"BlimpBomb/BlimpBombE Range1.5/CellRangefinding/OmniFire/Damage250/ROF50; BlimpBombP Vertical/ImageZBOMB/Acceleration1; original CellSpread2 differs from firing Range1.5.\n"
        <<"Original PrimaryFireFLH-50,0,-140 is transformed from actual source XY/facing, independently of any attack-range helper.\n"
        <<"Foundation center oracle is midpoint of original cell-center rectangle [x-.5,x+W-.5]x[y-.5,y+H-.5], i.e. x+(W-1)/2. This uses the established project coordinate contract; no new retail PE center proof is claimed.\n"
        <<"Existing jumpjet route endpoints are integer cells. Terminal tolerance is the independently computed nearest integer-center distance (2x2 sqrt(.5),3x3 0,4x3 .5), plus two lepton-Q16 units, always <=.707107 and inside true Foundation.\n"
        <<"Speed is original leptons/rule-frame at15Hz, confirmed by existing flight authority Q16 units; bound=rawSpeed*15/(256*fixedHz)+2/(256*65536). Elite FASTER reads raw VeteranSpeed1.2; no production movement/range/deadline helper generates expected values.\n"
        <<"Every actor/target is stock and full-health; natural full simulation reaches cruise before UI Primary. No rules, deadlines, speeds, health, positions or projectile records are changed during an attack.\n"
        <<"30 scenes:24 building/direction cores+6 command/clock controls. Two real cores checkpoint approach and falling bomb, each ordinary14 and canonical, physically close/write/reopen/read before full materialization and paired continuation (8 loads).\n";sources.flush();
    auto metadata=runtimeTerrainMetadataContext(live);RuntimeMapRenderCache tileCache;std::optional<TerrainCell> flat;
    for(const auto& set:live.theater.getTileSets())for(int i=set.startTileIndex;i<set.startTileIndex+set.tilesInSet&&!flat;++i)
        for(int sub=0;sub<16&&!flat;++sub){TerrainCell c;c.tileIndex=i;c.subTileIndex=sub;const auto info=runtimeTerrainInfoForCell(live,c);
            if(info.passable&&!info.water&&!info.bridge&&!info.shore&&!info.cliff&&resolveRuntimeTerrainSubTileRampType(c,metadata,tileCache)==0)flat=c;}
    if(!flat)return fail("original flat TMP land unavailable");
    struct Position{double x=0,y=0,z=0;};
    const auto body=[](const WorldObjectInstance& o){return Position{o.hasRenderPosition?o.renderX:double(o.x),o.hasRenderPosition?o.renderY:double(o.y),o.hasRenderPosition?o.renderLevel:double(o.level)};};
    const auto distance=[](Position a,Position b){return std::hypot(a.x-b.x,a.y-b.y);};
    const auto equal=[](double a,double b){return std::abs(a-b)<=1e-8;};
    constexpr double quantization=2.0/(256.0*65536.0);
    struct Scene{std::unique_ptr<RuntimeWindowState> state;Spec spec;int source=-1,target=-1,direction=0,rank=0,hz=30,tx=64,ty=64;};
    const auto center=[](const Scene& f){return Position{f.tx+(f.spec.w-1)*.5,f.ty+(f.spec.h-1)*.5,0};};
    const auto inside=[](const Scene& f,Position p){return p.x>f.tx-.5&&p.x<f.tx+f.spec.w-.5&&p.y>f.ty-.5&&p.y<f.ty+f.spec.h-.5;};
    const auto endpointLimit=[&](const Scene& f){const auto c=center(f);return std::hypot(c.x-std::round(c.x),c.y-std::round(c.y))+quantization;};
    const auto speedLimit=[&](const Scene& f){return baseSpeed*(f.rank>=2?veteranSpeed:1.0)*15.0/(256.0*f.hz)+quantization;};
    const std::array<RuntimeCellPoint,8> directions{{{-1,0},{-1,-1},{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1}}};
    const auto step=[](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);};
    const auto make=[&](Spec spec,int direction,int rank,int hz,bool initiallyOverhead=false){
        Scene f;f.spec=spec;f.direction=direction;f.rank=rank;f.hz=hz;f.state=std::make_unique<RuntimeWindowState>();auto& s=*f.state;
        seedRuntimeLoadScratch(s,live,false);
        if(!lifecyclePin(s.rules,"fresh-fixture-default-copy")){f.state.reset();return f;}
        s.tick=200;s.config.fixedTickHz=hz;s.config.playerCountry="Russians";s.config.playerSlots.clear();s.config.gameMode="Battle";s.config.gameModeRulesOverride.clear();
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=s.config.shortGame=s.config.crates=s.config.superweapons=false;
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=s.visionEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.campaignMode=s.skirmishMatchFinished=false;s.computerAiByOwner.clear();s.computerMiningByOwner.clear();s.humanHouseAuthorities.clear();
        s.skirmishTeamByOwner.clear();s.skirmishCountryByOwner.clear();s.skirmishDifficultyByOwner.clear();s.triggers={};s.triggers.bootstrapComplete=true;
        s.economy={};s.economy.owner="Russians";s.economy.credits=10000;s.economy.skirmishStartApplied=true;s.initialOreFieldsSeeded=true;
        s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.mapFacts={};s.mapFactsLoaded=true;s.terrainByCell.clear();s.terrainInfoByCell.clear();s.structureFootprints.clear();
        s.behaviors.clear();s.projectiles.clear();s.recentExpiredProjectiles.clear();s.impactEffects.clear();s.jumpjetFlights.clear();s.aircraftFlights.clear();s.deathWeapons.reset();
        s.pendingObjectExits.clear();s.pendingObjectExitIds.clear();s.selectedObjectIds.clear();s.playerCommands={};
        s.specialMechanismsInitialized=s.superWeaponsInitialized=s.subcellsInitialized=false;
        s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=s.report.map.height=-1;
        for(int y=44;y<=88;++y)for(int x=44;x<=88;++x){auto c=*flat;c.x=x;c.y=y;c.level=0;c.cellIndex512=runtimeCellKey(x,y);s.world.accessTerrainCells().push_back(c);
            MapCellFacts v;v.x=x;v.y=y;v.cellIndex=c.cellIndex512;v.hasTerrain=true;v.terrain.x=x;v.terrain.y=y;v.terrain.tileIndex=c.tileIndex;v.terrain.subTileIndex=c.subTileIndex;
            v.terrainClass="clear";v.passabilityHint="passable";s.mapFacts.cellIndexLookup.emplace(v.cellIndex,s.mapFacts.cells.size());s.mapFacts.cells.push_back(v);}
        s.mapFacts.terrainCellCount=s.mapFacts.lookupIndexSize=s.mapFacts.cells.size();s.mapFacts.contentHash=0x25090000+spec.w*100+spec.h+direction;
        const auto d=directions[direction];RuntimeCellPoint start{d.x<0?58:d.x>0?64+spec.w+5:64+spec.w/2,d.y<0?58:d.y>0?64+spec.h+5:64+spec.h/2};
        if(initiallyOverhead)start={65,65};
        auto* born=addRuntimeObjectInstanceForOwner(s,"Russians",RuntimeBuildKind::Unit,"ZEP",start);
        if(!born)return f;f.source=born->instanceId;born->airborne=true;born->level=born->flightLevel=0;born->veterancy=rank;born->mission="Guard";
        born->facing=born->turretFacing=runtimeFacingFromDelta(64+spec.w/2-start.x,64+spec.h/2-start.y);
        born->hasRenderPosition=true;born->renderX=born->x;born->renderY=born->y;born->renderLevel=0;
        initializeRuntimeNavigation(s);initializeRuntimeLocalProductionLanes(s.economy);rebuildRuntimeEconomyPower(s);ensureRuntimeSubcellsInitialized(s);ensureRuntimeSpecialMechanismsInitialized(s);
        RuntimeHumanHouseAuthority own;own.owner=own.country="Russians";own.canonicalOwner="russians";own.localFacade=true;s.humanHouseAuthorities.push_back(std::move(own));
        initializeRuntimeOfflineGameplayRouting(s);s.particles.reset(hz);s.particlesInitialized=s.particles.alignEmptyStateTick(s.tick);ensureRuntimeSuperWeaponsInitialized(s);ensureRuntimeLightingInitialized(s);
        // The actual stock aircraft climbs from zero through full simulation.
        // Add the target afterwards so the overhead control cannot auto-fire
        // while warming up; this is ordinary runtime building creation.
        for(int n=0;n<hz*20;++n){step(s);const auto* o=findObjectById(s,f.source);if(o&&equal(body(*o).z,cruise))break;}
        const auto* ready=findObjectById(s,f.source);
        if(!ready||ready->health!=2000||!equal(body(*ready).z,cruise))return f;
        auto* target=addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Building,spec.id,{f.tx,f.ty});
        if(!target)return f;f.target=target->instanceId;target->mission="Sleep";target->buildAnimTicksRemaining=target->buildAnimTicksTotal=0;
        // A distant original enemy building keeps the real outcome stage
        // independent of eventual bomb damage to the chosen target.
        auto* other=addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Building,"GAPOWR",{84,84});
        if(!other||!findObjectById(s,f.target)||findObjectById(s,f.target)->health!=spec.strength)return f;
        refreshRuntimeObjectOccupancyAfterLocalChange(s);rebuildRuntimeEconomyPower(s);s.selectedObjectIds={f.source};return f;
    };
    int fixtures=0,cores=0,controls=0,ordinaryLoads=0,canonicalLoads=0,bmps=0,pairedTicks=0,positiveImpacts=0,observedShots=0,movingAfterShot=0;
    double largestDelta=0;
    const auto sample=[&](const Scene& f,const RuntimeWindowState& s,const char* replica,double delta){
        const auto* o=findRuntimeObjectByIdIncludingInactive(s,f.source);if(!o)return;const auto p=body(*o),c=center(f);const auto b=s.behaviors.find(f.source);const auto j=s.jumpjetFlights.find(f.source);
        std::string path;if(b!=s.behaviors.end())for(auto cell:b->second.path)path+=std::to_string(cell.x)+":"+std::to_string(cell.y)+";";
        const auto* t=findRuntimeObjectByIdIncludingInactive(s,f.target);
        states<<quote(label)<<','<<quote(stage)<<','<<replica<<','<<s.tick<<','<<f.source<<','<<o->x<<','<<o->y<<','<<p.x<<','<<p.y<<','<<p.z<<','<<delta<<','<<speedLimit(f)
            <<','<<c.x<<','<<c.y<<','<<distance(p,c)<<','<<inside(f,p)<<','<<(j==s.jumpjetFlights.end()?0:j->second.height)<<','<<(j==s.jumpjetFlights.end()?0:j->second.clearanceLift)
            <<','<<(j!=s.jumpjetFlights.end()&&j->second.active)<<','<<(j!=s.jumpjetFlights.end()&&j->second.stopped)<<','<<(j==s.jumpjetFlights.end()?0:j->second.speed)
            <<','<<(j==s.jumpjetFlights.end()?0:j->second.progress)<<','<<(j==s.jumpjetFlights.end()?0:j->second.length)<<','<<quote(o->mission)
            <<','<<(b==s.behaviors.end()?-1:int(b->second.state))<<','<<(b==s.behaviors.end()?-1:b->second.targetObjectId)<<','<<(b==s.behaviors.end()?-1:b->second.moveTarget.x)
            <<','<<(b==s.behaviors.end()?-1:b->second.moveTarget.y)<<','<<quote(path)<<','<<(b==s.behaviors.end()?0:b->second.moveCooldownTicks)
            <<','<<o->weaponCooldownTicksRemaining<<','<<(t?t->health:-1)<<','<<s.lastCommand.valid<<','<<s.lastCommand.acceptedCount<<','<<s.lastCommand.rejectedCount
            <<','<<quote(s.interactionStatus+" / "+s.simulationStatus)<<'\n';
    };
    const auto capture=[&](Scene& f,const std::string& name){auto& s=*f.state;const int width=800,height=600;const RECT viewport{0,0,width,height};
        struct Surface{RuntimeBackBuffer buffer;HDC screen=GetDC(nullptr);~Surface(){releaseRuntimeBackBuffer(buffer);if(screen)ReleaseDC(nullptr,screen);}} surface;
        if(!surface.screen||!ensureRuntimeBackBuffer(surface.buffer,surface.screen,width,height))return fail("actual BMP DIB");
        const auto c=center(f);s.camera.initialized=true;s.camera.tileWidth=60;s.camera.tileHeight=30;s.camera.worldOffsetX=(c.x-c.y)*30-400;s.camera.worldOffsetY=(c.x+c.y)*15-320;
        auto view=runtimeTerrainMetadataContext(s);view.rules=&s.rules;view.art=&s.art;view.palettePath=runtimePalettePath(s.config,s.report.map);
        view.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);view.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
        view.animationTick=s.tick;view.fixedTickHz=f.hz;view.seeHiddenObjects=false;view.specialMechanismSnapshots=&s.specialMechanismRenderSnapshots;
        view.surfacePixels=static_cast<std::uint32_t*>(surface.buffer.pixels);view.surfaceWidth=width;view.surfaceHeight=height;
        const auto hash=s.world.deterministicSimulationStateHash();const auto flights=serializeRuntimeJumpjetFlights(s.jumpjetFlights);
        if(!drawRuntimeMap(surface.buffer.memoryDc,viewport,s.world,view,s.camera,s.renderCache).rendered)return fail("actual stock-material render");GdiFlush();
        if(hash!=s.world.deterministicSimulationStateHash()||flights!=serializeRuntimeJumpjetFlights(s.jumpjetFlights))return fail("draw changed physical flight");
        if(!writeRuntimeSmokeBmp(pathString(root/(label+"-"+name+".bmp")),view.surfacePixels,width,height,error))return false;++bmps;return true;
    };
    const auto physical=[&](Scene& f,RuntimeWindowState& s,Position previous,const char* replica){
        const auto* o=findObjectById(s,f.source);if(!o||o->health!=2000||!o->airborne)return fail("original source died/landed/health changed");
        const auto p=body(*o);const double delta=distance(previous,p);largestDelta=std::max(largestDelta,delta);sample(f,s,replica,delta);
        if(delta>speedLimit(f)||!equal(p.z,cruise)){states.flush();if(&s==f.state.get())(void)capture(f,"physical-failure");
            return fail("actual physical speed/height changed delta="+std::to_string(delta)+" limit="+std::to_string(speedLimit(f))+" z="+std::to_string(p.z));}
        return true;
    };
    const auto primary=[&](Scene& f){auto& s=*f.state;const auto* a=findObjectById(s,f.source);const auto* t=findObjectById(s,f.target);if(!a||!t)return fail("UI actors missing");
        const auto before=body(*a);s.selectedObjectIds={f.source};const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,{t->x,t->y},t);
        stage="UI-Primary";sample(f,s,"live",distance(before,body(*findObjectById(s,f.source))));
        const auto b=s.behaviors.find(f.source);
        if(!dispatched||!s.lastCommand.valid||s.lastCommand.acceptedCount!=1||s.lastCommand.rejectedCount!=0||b==s.behaviors.end()
            ||b->second.kind!=RuntimeBehaviorKind::Attack||b->second.targetObjectId!=f.target)return fail("actual UI Attack intent missing");
        if(distance(before,body(*findObjectById(s,f.source)))>1e-8)return fail("same-tick attack command moved physical body");return true;
    };
    const auto checkpoint=[&](Scene& f,const std::string& name,bool canonical){
        auto& s=*f.state;std::ostringstream ordinary,normalized;IniFile base,exact;
        if(!writeRuntimeStateToStream(s,ordinary,error)||!base.loadFromString(ordinary.str())||base.getValue("RuntimeSave","Version")!="14")return std::unique_ptr<RuntimeWindowState>{};
        const auto ordinaryPath=pathString(root/(label+"-"+name+"-ordinary14.ini"));
        {std::ofstream file(ordinaryPath,std::ios::binary);file<<ordinary.str();if(!file)return std::unique_ptr<RuntimeWindowState>{};}
        if(!base.loadFromFile(ordinaryPath))return std::unique_ptr<RuntimeWindowState>{};
        std::string savedPath=ordinaryPath;
        if(canonical){std::ostringstream extended;extended<<ordinary.str();
            if(!writeRuntimeMultiplayerSnapshotExtension(s,extended,error)||!exact.loadFromString(extended.str())
                ||!writeRuntimeMultiplayerSnapshotCanonicalIni(exact,normalized,error))return std::unique_ptr<RuntimeWindowState>{};
            savedPath=pathString(root/(label+"-"+name+"-canonical.ini"));
            {std::ofstream file(savedPath,std::ios::binary);file<<normalized.str();if(!file)return std::unique_ptr<RuntimeWindowState>{};}
            if(!exact.loadFromFile(savedPath))return std::unique_ptr<RuntimeWindowState>{};}
        auto copy=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*copy,s,false);
        if(!lifecyclePin(copy->rules,"checkpoint-default-copy-before-materialize"))return std::unique_ptr<RuntimeWindowState>{};
        {RuntimeSnapshotActiveStateScope active(*copy);if(!materializeRuntimeLoadStateFromIni(*copy,base,error)
            ||(canonical&&!materializeRuntimeMultiplayerSnapshotExtension(*copy,exact,error)))return std::unique_ptr<RuntimeWindowState>{};}
        if(!lifecyclePin(copy->rules,canonical?"canonical-after-materialize":"ordinary14-after-materialize"))return std::unique_ptr<RuntimeWindowState>{};
        copy->runtimeSoundsEnabled=copy->runtimePreferencePersistenceEnabled=copy->visionEnabled=false;
        initializeRuntimeOfflineGameplayRouting(*copy);
        const auto* a=findObjectById(s,f.source);const auto* b=findObjectById(*copy,f.source);
        const auto* at=findRuntimeObjectByIdIncludingInactive(s,f.target);const auto* bt=findRuntimeObjectByIdIncludingInactive(*copy,f.target);
        const auto p=a?body(*a):Position{},q=b?body(*b):Position{};
        const bool okay=a&&b&&at&&bt&&copy->tick==s.tick&&a->health==b->health&&at->health==bt->health&&at->alive==bt->alive
            &&distance(p,q)<1e-8&&equal(p.z,q.z)&&copy->rules.getDetailRulesHash()==s.rules.getDetailRulesHash()
            &&serializeRuntimeJumpjetFlights(copy->jumpjetFlights)==serializeRuntimeJumpjetFlights(s.jumpjetFlights)
            &&copy->projectiles.size()==s.projectiles.size()&&copy->behaviors.contains(f.source)
            &&copy->behaviors.at(f.source).kind==RuntimeBehaviorKind::Attack&&copy->behaviors.at(f.source).targetObjectId==f.target;
        saves<<quote(label)<<','<<name<<','<<(canonical?"canonical":"ordinary14")<<','<<s.tick<<','<<f.source<<','<<q.x<<','<<q.y<<','<<q.z<<','<<(bt?bt->health:-1)<<','<<quote(savedPath)<<','<<okay<<'\n';saves.flush();
        if(!okay){error="actual full checkpoint changed flight/position/target/clock";return std::unique_ptr<RuntimeWindowState>{};}
        canonical?++canonicalLoads:++ordinaryLoads;return copy;
    };
    const auto run=[&](Scene f,bool save,bool screenshots,const std::string& control="",bool alreadyCommanded=false){
        if(!f.state||f.source<0||f.target<0)return fail("original actor/building fixture creation or natural cruise"+(error.empty()?std::string{}:"; "+error));
        auto& s=*f.state;
        if(screenshots&&!capture(f,"before"))return false;
        if(!alreadyCommanded&&!primary(f))return false;
        std::unique_ptr<RuntimeWindowState> ordinary,canonical;
        const auto replicas=[&](){std::vector<std::pair<const char*,RuntimeWindowState*>> v{{"live",&s}};if(ordinary)v.emplace_back("ordinary14",ordinary.get());if(canonical)v.emplace_back("canonical",canonical.get());return v;};
        const auto same=[&](){const auto* a=findRuntimeObjectByIdIncludingInactive(s,f.source);const auto* at=findRuntimeObjectByIdIncludingInactive(s,f.target);if(!a||!at)return false;const auto p=body(*a);
            for(const auto& [name,q]:replicas()){(void)name;const auto* b=findRuntimeObjectByIdIncludingInactive(*q,f.source);const auto* bt=findRuntimeObjectByIdIncludingInactive(*q,f.target);
                if(!b||!bt||q->tick!=s.tick||b->health!=a->health||b->facing!=a->facing||b->mission!=a->mission||b->airborne!=a->airborne
                    ||bt->health!=at->health||bt->alive!=at->alive||distance(p,body(*b))>1e-8||!equal(p.z,body(*b).z)
                    ||serializeRuntimeJumpjetFlights(q->jumpjetFlights)!=serializeRuntimeJumpjetFlights(s.jumpjetFlights)||q->projectiles.size()!=s.projectiles.size())return false;
                for(std::size_t j=0;j<s.projectiles.size();++j)if(serializeRuntimeProjectileEffect(q->projectiles[j])!=serializeRuntimeProjectileEffect(s.projectiles[j]))return false;
            }return true;};
        std::map<std::string,std::map<int,int>> beforeImpact;
        std::map<std::string,int> sceneImpacts,sceneShots;
        std::map<std::string,std::set<int>> seen;
        std::map<std::string,std::map<int,RuntimeProjectileEffect>> previousBombs;
        std::string impactError;
        const auto attachObserver=[&](const char* name,RuntimeWindowState& q){q.runtimeProjectileImpactObserver=[&,name,state=&q](const RuntimeProjectileEffect& p,bool after){
            if(p.attackerObjectId!=f.source||(p.weaponId!="BlimpBomb"&&p.weaponId!="BlimpBombE"))return;
            const auto* t=findRuntimeObjectByIdIncludingInactive(*state,f.target);
            if(!after){beforeImpact[name][p.actorId]=t?t->health:0;return;}
            const int before=beforeImpact[name][p.actorId],afterHP=t?t->health:0;
            // Independent raw rectangular cell-center distance/falloff. The
            // observer reports a real impact; it never creates projectiles.
            const double d=std::hypot(p.toCell.x-std::clamp(p.toCell.x,f.tx,f.tx+f.spec.w-1),p.toCell.y-std::clamp(p.toCell.y,f.ty,f.ty+f.spec.h-1));
            const double outgoing=f.rank>=2?veteranCombat:1.0;
            const double falloff=1.0-.5*std::clamp(d,0.0,2.0)/2.0;
            const int expected=before<=0||d>2?0:std::min(before,int(std::lround(250*f.spec.armor*outgoing*falloff)));
            const int actual=before-afterHP;
            damage<<quote(label)<<','<<name<<','<<state->tick<<','<<p.actorId<<','<<f.target<<','<<p.toCell.x<<','<<p.toCell.y<<','<<d<<','<<before<<','<<afterHP<<','<<expected<<','<<actual<<','<<p.weaponId<<','<<p.warheadId<<'\n';
            if(actual!=expected)impactError="original bomb impact mismatch expected="+std::to_string(expected)+" actual="+std::to_string(actual)+" actor="+std::to_string(p.actorId);
            if(actual>0){++sceneImpacts[name];++positiveImpacts;}
        };};
        attachObserver("live",s);
        int firstTick=-1,steady=0,afterFirstMotion=0,sceneOrdinary=0,sceneCanonical=0;
        double firstDistance=-1,finalDistance=distance(body(*findObjectById(s,f.source)),center(f));
        bool approachSaved=false,fallingSaved=false,arrived=false;
        const auto observe=[&](const char* name,RuntimeWindowState& q){
            const auto* o=findObjectById(q,f.source);if(!o)return fail("source missing during bomb observation");const auto p=body(*o),c=center(f);
            for(const auto& b:q.projectiles){if(b.attackerObjectId!=f.source||(b.weaponId!="BlimpBomb"&&b.weaponId!="BlimpBombE"))continue;
                const auto previous=previousBombs[name].find(b.actorId);
                if(previous!=previousBombs[name].end()){
                    const auto& old=previous->second;
                    if(!equal(b.sourceX,old.sourceX)||!equal(b.sourceY,old.sourceY)||!equal(b.targetX,old.targetX)||!equal(b.targetY,old.targetY)
                        ||!equal(b.currentX,b.sourceX)||!equal(b.currentY,b.sourceY)||b.currentZ>old.currentZ+1e-8)return fail("released bomb retargeted or moved horizontally/upwards");
                }
                previousBombs[name][b.actorId]=b;
                if(!seen[name].insert(b.actorId).second)continue;
                const bool first=sceneShots[name]==0;++sceneShots[name];++observedShots;
                const double d=distance(p,c);
                shots<<quote(label)<<','<<name<<','<<q.tick<<','<<b.actorId<<','<<b.spawnTick<<','<<f.source<<','<<b.weaponId<<','<<b.warheadId<<','<<p.x<<','<<p.y<<','<<p.z
                    <<','<<c.x<<','<<c.y<<','<<d<<','<<b.sourceX<<','<<b.sourceY<<','<<b.sourceZ<<','<<b.targetX<<','<<b.targetY<<','<<b.currentX<<','<<b.currentY<<','<<b.currentZ<<','<<b.vertical<<','<<b.targetObjectId<<','<<first<<'\n';shots.flush();
                if(d>range+quantization){states.flush();damage.flush();if(&q==&s)(void)capture(f,"first-or-later-release-outside-range");
                    return fail("actual bomb released outside raw1.5 center range body="+std::to_string(p.x)+","+std::to_string(p.y)+" center="+std::to_string(c.x)+","+std::to_string(c.y)+" distance="+std::to_string(d));}
                // New projectiles must be observed in the actual spawn tick.
                // Prior saved bombs are already verified and mark seen on load.
                const double angle=(b.attackerFacing-96)*std::acos(-1.0)/128.0;
                const double expectedX=p.x-50.0*std::cos(angle)/256.0,expectedY=p.y-50.0*std::sin(angle)/256.0;
                const double expectedZ=runtimeDrawLevelForObject(*o)-140.0*std::sqrt(6.0)/256.0;
                if(b.spawnTick!=q.tick||b.weaponId!=(f.rank>=2?"BlimpBombE":"BlimpBomb")||b.warheadId!=(f.rank>=2?"KTSTLEXP":"BlimpHE")
                    ||!b.vertical||b.acceleration!=1||b.projectileImageId!="ZBOMB"||b.targetObjectId>=0
                    ||!equal(b.sourceX,expectedX)||!equal(b.sourceY,expectedY)||!equal(b.sourceZ,expectedZ)
                    ||!equal(b.targetX,b.sourceX)||!equal(b.targetY,b.sourceY)||!equal(b.currentX,b.sourceX)||!equal(b.currentY,b.sourceY))
                    return fail("actual original bomb/muzzle/fixed-release identity changed");
                if(first&&std::string(name)=="live"){
                    firstTick=q.tick;firstDistance=d;
                    if(control!="already-overhead"&&firstDistance<=.25)return fail("original1.5 early release was postponed until directly overhead");
                    if(control!="already-overhead"&&firstDistance<range-speedLimit(f)-quantization){
                        states.flush();if(&q==&s)(void)capture(f,"first-release-late-inside-original-range");
                        return fail("first actual bomb was delayed after entering raw range: distance="+std::to_string(firstDistance)
                            +" minimum="+std::to_string(range-speedLimit(f)-quantization));}
                    if(screenshots&&!capture(f,"first-real-bomb"))return false;
                }
            }return true;
        };
        const auto loadPair=[&](const std::string& name){ordinary=checkpoint(f,name,false);if(!ordinary)return fail("ordinary14 physical file restore "+error);
            canonical=checkpoint(f,name,true);if(!canonical)return fail("canonical physical file restore "+error);
            sceneOrdinary++;sceneCanonical++;
            for(const auto& [n,q]:replicas())if(q!=&s){seen[n]=seen["live"];sceneShots[n]=sceneShots["live"];previousBombs[n]=previousBombs["live"];attachObserver(n,*q);}
            return same()||fail("checkpoint flight/HP/projectile replicas differ");};
        for(int n=0;n<f.hz*180;++n){stage=firstTick<0?"approach":"range-and-overhead";
            for(const auto& [name,q]:replicas()){const auto before=body(*findObjectById(*q,f.source));step(*q);
                if(!physical(f,*q,before,name)||!observe(name,*q))return false;
                if(q==&s&&firstTick>=0&&distance(before,body(*findObjectById(*q,f.source)))>1e-10){++afterFirstMotion;++movingAfterShot;}
            }
            if(!same()){states.flush();return fail("paired physical flight/HP/bomb continuation diverged");}
            pairedTicks+=int(replicas().size())-1;
            if(!impactError.empty()){damage.flush();return fail(impactError);}
            const auto* source=findObjectById(s,f.source);const auto* target=findObjectById(s,f.target);
            if(!source||!target)return fail("original building died before overhead terminal witness");
            finalDistance=distance(body(*source),center(f));
            if(save&&!approachSaved&&source->renderMoveActive&&s.jumpjetFlights.contains(f.source)&&s.jumpjetFlights.at(f.source).progress>0&&firstTick<0&&finalDistance>range+.5){
                if(!loadPair("real-moving-approach"))return false;approachSaved=true;}
            if(save&&!fallingSaved&&firstTick>=0&&!s.projectiles.empty()){
                if(!loadPair("real-first-falling-bomb"))return false;fallingSaved=true;}
            const auto j=s.jumpjetFlights.find(f.source);
            const auto terminalBehavior=s.behaviors.find(f.source);
            const bool terminal=finalDistance<=endpointLimit(f)&&inside(f,body(*source))
                &&j!=s.jumpjetFlights.end()&&!j->second.active&&j->second.speed==0
                &&terminalBehavior!=s.behaviors.end()&&terminalBehavior->second.kind==RuntimeBehaviorKind::Attack
                &&terminalBehavior->second.targetObjectId==f.target&&terminalBehavior->second.path.empty()
                &&!terminalBehavior->second.pathRequestPending;
            if(terminal){if(!arrived){arrived=true;if(screenshots&&!capture(f,"above-original-foundation"))return false;}++steady;}else steady=0;
            if(firstTick>=0&&steady>=f.hz&&sceneImpacts["live"]>0){
                if(control!="already-overhead"&&(afterFirstMotion==0||finalDistance>=firstDistance-.25))return fail("in-range bombing never continued toward actual building center");
                if(save&&(!approachSaved||!fallingSaved||sceneOrdinary!=2||sceneCanonical!=2))return fail("both actual moving/falling checkpoints absent");
                cases<<quote(label)<<','<<f.spec.id<<','<<f.direction<<','<<f.rank<<','<<f.hz<<','<<quote(control)<<','<<firstTick<<','<<firstDistance<<','<<finalDistance
                    <<','<<endpointLimit(f)<<','<<steady<<','<<sceneShots["live"]<<','<<sceneImpacts["live"]<<','<<sceneOrdinary<<','<<sceneCanonical<<','<<source->health<<','<<target->health<<",1\n";
                cases.flush();states.flush();damage.flush();++fixtures;control.empty()?++cores:++controls;
                std::cout<<"Issue25 Kirov "<<label<<" PASS firstDistance="<<firstDistance<<" terminalDistance="<<finalDistance<<" impacts="<<sceneImpacts["live"]<<'\n';return true;
            }
        }
        states.flush();damage.flush();if(!capture(f,"approach-timeout"))return false;
        return fail("actual full-simulation approach/first-release/overhead timeout first="+std::to_string(firstTick)+" distance="+std::to_string(finalDistance)+" terminalLimit="+std::to_string(endpointLimit(f))+" impacts="+std::to_string(sceneImpacts["live"]));
    };
    const auto reachActive=[&](Scene& f){auto& s=*f.state;const auto* a=findObjectById(s,f.source);if(!a)return fail("pre-command original source missing");const auto from=body(*a);
        for(int n=0;n<f.hz*20;++n){const auto before=body(*findObjectById(s,f.source));step(s);stage="real-command-interruption-window";
            if(!physical(f,s,before,"live"))return false;const auto* o=findObjectById(s,f.source);const auto j=s.jumpjetFlights.find(f.source);
            if(o&&j!=s.jumpjetFlights.end()&&j->second.active&&j->second.progress>0&&distance(from,body(*o))>=.1
                &&distance(body(*o),center(f))>range+1&&!s.projectiles.size())return true;
        }return fail("natural fractional movement window before firing unavailable");};
    // First unchanged-production baseline: western GAREFN approach. Its raw
    // 4x3 center differs from the top-left anchor used by the former ring.
    // The real spawn row and actual BMP are emitted before range failure.
    const std::array<int,3> order{{2,0,1}};
    for(int index:order)for(int direction=0;direction<8;++direction){const int rank=index==2&&direction>=6?2:0;
        label=std::string(specs[index].id)+"-d"+std::to_string(direction)+"-rank"+std::to_string(rank)+"-30";
        stage="original-UI-core";auto f=make(specs[index],direction,rank,30);
        const bool save=index==2&&(direction==1||direction==4);
        if(!run(std::move(f),save,save||(index==2&&direction==0)))return false;
    }
    {
        label="Stop-real-fractional-approach-resume";stage="UI-approach";auto f=make(specs[2],0,0,30);
        if(f.source<0||f.target<0||!primary(f)||!reachActive(f))return false;auto& s=*f.state;
        const auto p=body(*findObjectById(s,f.source));s.selectedObjectIds={f.source};
        if(!dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop))return fail("real Stop dispatch refused");
        const auto j=s.jumpjetFlights.find(f.source);
        if(distance(p,body(*findObjectById(s,f.source)))>1e-8||j==s.jumpjetFlights.end()||!j->second.stopped||j->second.active)return fail("Stop did not freeze exact fractional body");
        for(int n=0;n<30;++n){const auto before=body(*findObjectById(s,f.source));step(s);stage="after-real-Stop";
            if(!physical(f,s,before,"live")||distance(p,body(*findObjectById(s,f.source)))>1e-8||!s.projectiles.empty())return fail("stopped exterior approach moved or fired before resume");}
        if(!run(std::move(f),false,true,"Stop-resume"))return false;
    }
    {
        label="newMove-cancels-building-approach";stage="UI-approach";auto f=make(specs[2],0,0,30);
        if(f.source<0||f.target<0||!primary(f)||!reachActive(f))return false;auto& s=*f.state;
        const auto p=body(*findObjectById(s,f.source));s.selectedObjectIds={f.source};const RuntimeCellPoint goal{52,52};
        const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,goal);
        const auto b=s.behaviors.find(f.source);
        stage="real-replacement-Move";sample(f,s,"live",distance(p,body(*findObjectById(s,f.source))));
        if(!dispatched||!s.lastCommand.valid||s.lastCommand.acceptedCount!=1||s.lastCommand.rejectedCount!=0
            ||b==s.behaviors.end()||b->second.kind!=RuntimeBehaviorKind::Move||b->second.moveTarget!=goal
            ||distance(p,body(*findObjectById(s,f.source)))>1e-8)return fail("new UI Move failed to replace Attack without a body jump");
        bool settled=false;
        for(int n=0;n<f.hz*180;++n){const auto before=body(*findObjectById(s,f.source));step(s);
            if(!physical(f,s,before,"live"))return false;const auto* o=findObjectById(s,f.source);const auto m=s.behaviors.find(f.source);const auto j=s.jumpjetFlights.find(f.source);
            if(!s.projectiles.empty()||findObjectById(s,f.target)->health!=f.spec.strength)return fail("cancelled old Attack still released/impacted a bomb");
            if(o&&equal(body(*o).x,goal.x)&&equal(body(*o).y,goal.y)&&j!=s.jumpjetFlights.end()&&!j->second.active
                &&(m==s.behaviors.end()||m->second.kind!=RuntimeBehaviorKind::Move||m->second.state==RuntimeBehaviorState::Complete)){settled=true;break;}
        }
        if(!settled)return fail("real replacement Move never settled at accepted destination");
        if(!capture(f,"new-Move-finished"))return false;
        const auto* o=findObjectById(s,f.source);cases<<quote(label)<<','<<f.spec.id<<','<<f.direction<<",0,30,newMove,-1,-1,"<<distance(body(*o),center(f))<<",-1,0,0,0,0,0,"<<o->health<<','<<findObjectById(s,f.target)->health<<",1\n";
        cases.flush();++fixtures;++controls;
    }
    {
        label="half-real-Move-to-building-Primary";stage="real-Move";auto f=make(specs[2],0,0,30);
        if(f.source<0||f.target<0)return fail("halfMove actors");auto& s=*f.state;s.selectedObjectIds={f.source};
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,{62,65})||s.lastCommand.acceptedCount!=1)return fail("actual initial Move not accepted");
        if(!reachActive(f))return false;const auto p=body(*findObjectById(s,f.source));
        if(!primary(f)||distance(p,body(*findObjectById(s,f.source)))>1e-8)return fail("halfMove Primary lost actual continuous body");
        if(!run(std::move(f),false,true,"halfMove-Primary",true))return false;
    }
    {
        label="half-approach-Primary-retarget";stage="old-UI-approach";auto f=make(specs[2],0,0,30);
        if(f.source<0||f.target<0||!primary(f)||!reachActive(f))return false;auto& s=*f.state;const int old=f.target;
        const auto p=body(*findObjectById(s,f.source));
        auto* newTarget=addRuntimeObjectInstanceForOwner(s,"Americans",RuntimeBuildKind::Building,"GAREFN",{72,70});
        if(!newTarget||newTarget->health!=1000)return fail("original second target creation");
        f.target=newTarget->instanceId;f.tx=72;f.ty=70;newTarget->mission="Sleep";newTarget->buildAnimTicksRemaining=newTarget->buildAnimTicksTotal=0;
        refreshRuntimeObjectOccupancyAfterLocalChange(s);
        if(!primary(f)||distance(p,body(*findObjectById(s,f.source)))>1e-8||findObjectById(s,old)->health!=1000||!s.projectiles.empty())return fail("new Primary changed physical body or fired at cancelled target");
        if(!run(std::move(f),false,true,"Primary-retarget",true))return false;
    }
    {
        label="already-overhead-real-Primary";stage="natural-cruise-before-building-creation";
        if(!run(make(specs[1],0,0,30,true),false,true,"already-overhead"))return false;
    }
    {
        label="GAREFN-d4-elite-60-clock-control";stage="original-60Hz-UI";
        if(!run(make(specs[2],4,2,60),false,true,"elite60"))return false;
    }
    if(fixtures!=30||cores!=24||controls!=6||ordinaryLoads!=4||canonicalLoads!=4||pairedTicks<=0||positiveImpacts<=0||movingAfterShot<=0||lifecycleCopyVariants!=4||lifecycleCopiesChecked!=4*sourceWeaponCount)
        return fail("permanent scene/checkpoint/actual impact/movement coverage incomplete");
    std::ofstream result(root/"result.txt");result<<"PASS\nfixtures="<<fixtures<<"\ncores="<<cores<<"\ncontrols="<<controls
        <<"\nordinary14_file_loads="<<ordinaryLoads<<"\ncanonical_file_loads="<<canonicalLoads<<"\npaired_ticks="<<pairedTicks
        <<"\npositive_impact_observations="<<positiveImpacts<<"\nprojectile_observations="<<observedShots<<"\nmoving_after_first_shot_live_ticks="<<movingAfterShot
        <<"\nmax_xy_delta="<<std::setprecision(17)<<largestDelta<<"\nbmps="<<bmps
        <<"\nlifecycle_loaded_weapons="<<sourceWeaponCount<<"\nlifecycle_copy_variants="<<lifecycleCopyVariants
        <<"\nlifecycle_copy_profile_comparisons="<<lifecycleCopiesChecked<<'\n';
    if(!result)return fail("terminal result write");
    std::cout<<"Issue25 Kirov building approach PASS scenes30/core24/controls6/read8 paired="<<pairedTicks<<" BMP="<<bmps<<'\n';return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif