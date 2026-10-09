bool runRuntimeUnitDebrisSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "UnitDebris: " + why; return false; };
    const char* requested = std::getenv("CONCORD_UNIT_DEBRIS_DIR");
    const std::string root = requested && *requested ? requested
        : "build/smoke/feature-checks/14-unit-debris/checks";
    std::filesystem::create_directories(root);
    auto stock = std::make_unique<RuntimeWindowState>(); stock->config = config;
    stock->config.defaultMapPath = "local_data/resources/mix_exports/multimd/2peaks.map";
    stock->report = buildRuntimeStartupReport(stock->config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("stock resources unavailable");
    IniFile source;
    if (!source.loadFromFile("local_data/rulesmd.ini")) return fail("original rules unavailable");
    const auto make = [&](int hz, int level) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = stock->config; s.config.fixedTickHz = hz; s.config.playerSlots.clear();
        s.config.computerAiEnabled = s.config.noCooldown = s.config.multiplayerActive = s.config.crates = false;
        s.config.shortGame = s.config.superweapons = false; s.config.gameMode = "Battle";
        s.config.gameModeRulesOverride.clear();
        s.report = stock->report; s.rules = stock->rules; s.art = stock->art; s.theater = stock->theater;
        // The renderer needs the loaded map summary even with synthetic cells.
        s.world = stock->world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        s.economy.owner = "Americans"; s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        s.economy.credits = 10000; s.economy.techLevel = 10; initializeRuntimeLocalProductionLanes(s.economy);
        for (int y = 1; y <= 32; ++y) for (int x = 1; x <= 32; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x,y);
            cell.tileIndex = cell.subTileIndex = 0; cell.level = level;
            s.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(s); return box;
    };
    const auto same = [](const auto& a, const auto& b) {
        if (a.projectiles.size()!=b.projectiles.size()) return false;
        for(std::size_t i=0;i<a.projectiles.size();++i)
            if(serializeRuntimeProjectileEffect(a.projectiles[i])!=serializeRuntimeProjectileEffect(b.projectiles[i]))return false;
        return true;
    };
    int rows=0,draws=0,fragments=0,records=0,deaths=0,saves=0,boundaries=0,renders=0;
    auto probeBox=make(30,0); auto& probe=*probeBox;
    if(probe.rules.getVoxelAnimTypes().size()!=10)return fail("ten authored VoxelAnims missing");
    const auto* tire=probe.rules.findVoxelAnimType("tire");
    if(!tire || tire->image!="TIRE" || tire->duration!=150 || tire->elasticity!=0.8
        || tire->minZVelocity!=28 || tire->maxZVelocity!=32 || tire->maxXYVelocity!=10
        || tire->damage!=0 || !tire->expireAnim.empty() || !tire->warhead.empty())return fail("stock TIRE rules");
    RuntimeProjectileEffect cause;
    WorldObjectInstance destroyed; destroyed.instanceId=1000; destroyed.kind=WorldObjectKind::Unit;
    destroyed.owner="Americans"; destroyed.x=destroyed.y=16; destroyed.cell=runtimeCellKey(16,16);
    std::ofstream counts(root+"/unit-lists.csv"); counts<<"category,id,min,max,voxel_types,samples,smallest,largest\n";
    for(const auto& category:std::vector<std::pair<std::string,const std::vector<UnitType>*>>{
        {"InfantryTypes",&probe.rules.getInfantryTypes()},{"VehicleTypes",&probe.rules.getVehicleTypes()},
        {"AircraftTypes",&probe.rules.getAircraftTypes()}}) {
        int expectedRows=0;
        if(const auto* list=source.getSection(category.first))for(const auto& entry:*list)
            if(!entry.second.empty() && source.getSection(entry.second))++expectedRows;
        if(expectedRows!=static_cast<int>(category.second->size()))return fail(category.first+" registry coverage");
        for(const auto& type:*category.second) {
            const auto read = [&](const char* key) { return source.getValue(type.id,key,""); };
            int minimum=0,maximum=0;
            if(!read("MinDebris").empty())minimum=std::stoi(read("MinDebris"));
            if(!read("MaxDebris").empty())maximum=std::stoi(read("MaxDebris"));
            minimum=std::max(0,minimum);maximum=std::max(minimum,maximum);
            if(type.minDebris!=minimum || type.maxDebris!=maximum || type.debrisTypes!=read("DebrisTypes")
                || type.debrisMaximums!=read("DebrisMaximums") || type.debrisAnims!=read("DebrisAnims"))return fail(type.id+" source rules differ");
            destroyed.type=type.id; destroyed.kind=category.first=="InfantryTypes"?WorldObjectKind::Infantry:WorldObjectKind::Unit;
            destroyed.authoredAircraft=category.first=="AircraftTypes";
            const int low=maximum<=0?0:std::min(minimum,maximum-1), high=maximum<=0?0:std::max(minimum,maximum-1);
            int smallest=INT_MAX,largest=0;
            for(int sample=0;sample<128;++sample) {
                probe.tick=sample;probe.projectiles.clear();
                const int count=spawnRuntimeUnitDebris(probe,destroyed,type,cause);
                if(count<low || count>high || count!=static_cast<int>(probe.projectiles.size()))return fail(type.id+" budget range");
                smallest=std::min(smallest,count);largest=std::max(largest,count);
                for(const auto& p:probe.projectiles) {
                    const auto event=std::find_if(probe.recentReplayEvents.rbegin(),probe.recentReplayEvents.rend(),[&](const auto& entry) {
                        return entry.kind=="ProjectileSpawned" && entry.projectileActorId==p.actorId;
                    });
                    if(event==probe.recentReplayEvents.rend() || event->weaponId!=p.weaponId
                        || event->renderIntentId!=runtimeProjectileReplayRenderIntentId(p,"ProjectileSpawned")
                        || event->warheadImpactIntentId!=p.warheadImpactIntentId)return fail(type.id+" spawn replay identity");
                    if(type.debrisTypes.empty()) {
                        const auto defaults=splitRuntimeTokenList(probe.rules.getCombatDetailRules().metallicDebris);
                        if(p.voxelDebris.enabled || std::find(defaults.begin(),defaults.end(),p.projectileId)==defaults.end())return fail(type.id+" default SHP branch");
                    } else if(!p.voxelDebris.enabled || p.projectileId!="TIRE" || p.damage!=0 || !p.impactAnimId.empty())return fail(type.id+" original TIRE branch");
                    RuntimeProjectileEffect copy;
                    if(!parseRuntimeProjectileEffect(serializeRuntimeProjectileEffect(p),copy)
                        || serializeRuntimeProjectileEffect(p)!=serializeRuntimeProjectileEffect(copy))return fail(type.id+" projectile record");
                    ++records;++fragments;
                }
                ++draws;
            }
            if(smallest!=low || largest!=high)return fail(type.id+" budget endpoints not exercised");
            counts<<category.first<<','<<type.id<<','<<minimum<<','<<maximum<<','<<type.debrisTypes<<",128,"<<smallest<<','<<largest<<'\n';
            ++rows;
        }
    }
    std::cout<<"Stock unit branches passed: rows="<<rows<<" draws="<<draws<<" fragments="<<fragments<<'\n';
    const auto parse = [](const std::string& fields,const std::string& tireFields="") {
        IniFile ini;ini.loadFromString("[VehicleTypes]\n0=TEST\n[VoxelAnims]\n0=TIRE\n[General]\nMetallicDebris=DBRIS1SM\n[TEST]\n"+fields
            +"\n[TIRE]\nElasticity=0.8\nMinAngularVelocity=12\nMaxAngularVelocity=24\nMinZVel=28\nMaxZVel=32\nMaxXYVel=10\nDuration=150\n"+tireFields);
        RulesDatabase rules;rules.loadFromIni(ini);return rules;
    };
    const std::string base="MinDebris=0\nMaxDebris=6\nDebrisTypes=TIRE\nDebrisMaximums=4";
    const auto baseline=parse(base);
    for(const auto& change:std::vector<std::string>{"MinDebris=1","MaxDebris=7","DebrisTypes=OTHER","DebrisMaximums=3","DebrisAnims=DBRIS2SM"}) {
        if(baseline.getDetailRulesHash()==parse(base+"\n"+change).getDetailRulesHash())return fail("unit field missing from hash");++boundaries;
    }
    for(const auto& change:std::vector<std::string>{"Duration=151","Elasticity=0.7","MinAngularVelocity=13","MaxAngularVelocity=25",
        "MinZVel=29","MaxZVel=33","MaxXYVel=11","Image=OTHER","BounceAnim=BOOM","ExpireAnim=BOOM","TrailerAnim=BOOM","Damage=1","DamageRadius=2","Warhead=HE"}) {
        if(baseline.getDetailRulesHash()==parse(base,change).getDetailRulesHash())return fail("voxel field missing from hash");++boundaries;
    }
    for(const auto& invalid:std::vector<std::string>{"DebrisMaximums=0","DebrisMaximums=-1","DebrisTypes=UNKNOWN","DebrisMaximums="}) {
        auto box=make(30,0); box->rules=parse(base+"\n"+invalid); destroyed.type="TEST";
        if(spawnRuntimeUnitDebris(*box,destroyed,box->rules.getVehicleTypes().front(),cause)!=0)return fail("invalid voxel list must not emit a substitute");++boundaries;
    }
    for(const char* id:{"MTNK","HARV","CMIN","ORCA","SHAD","ZEP","DISK","DEST","HTNK","CARRIER"})
        for(int hz:{30,60})for(int level:{0,2}) {
        auto box=make(hz,level);auto& s=*box;
        const auto* rule=s.rules.findUnitType("UnitType",id);bool aircraft=false;
        if(!rule){rule=s.rules.findUnitType("AircraftType",id);aircraft=true;}
        if(!rule)return fail(std::string(id)+" missing unit");
        WorldObjectInstance unit=destroyed;unit.type=id;unit.kind=WorldObjectKind::Unit;unit.authoredAircraft=aircraft;
        unit.instanceId=1000;unit.health=rule->strength;unit.alive=true;unit.mission="Guard";
        unit.currentAmmo=rule->ammo<0?-1:std::clamp(rule->initialAmmo<0?rule->ammo:rule->initialAmmo,0,rule->ammo);
        unit.airborne=aircraft || rule->crashable;unit.level=level+(unit.airborne?6:0);unit.flightLevel=unit.level;
        s.world.accessObjects().push_back(unit);invalidateRuntimeObjectProjectionCache(s);
        // Use a deterministic nonzero sample when the original range allows it.
        for(int tick=0;tick<128;++tick) {
            auto selector=make(hz,level);selector->tick=tick;
            if(spawnRuntimeUnitDebris(*selector,unit,*rule,cause)>0 || rule->maxDebris<=0){s.tick=tick;break;}
        }
        RuntimeObjectExitRequest request;request.objectId=1000;request.tick=s.tick;
        request.kind=RuntimeObjectExitKind::Destroyed;request.suppressCrew=request.suppressDeathWeapon=true;
        if(!finalizeRuntimeObjectExit(s,request).accepted)return fail(std::string(id)+" death route");
        const auto initialCount=s.projectiles.size();const auto nextId=s.nextProjectileActorId;
        if(rule->maxDebris>1 && initialCount==0)return fail(std::string(id)+" death omitted configured fragments");
        if(rule->maxDebris<=0 && initialCount!=0)return fail(std::string(id)+" invented debris budget");
        if(unit.airborne && s.wrecks.empty())return fail(std::string(id)+" lost crashing body");
        if(finalizeRuntimeObjectExit(s,request).accepted || s.projectiles.size()!=initialCount || s.nextProjectileActorId!=nextId)return fail("repeated death duplicated debris");
        for(int tick=0;tick<7;++tick){++s.tick;advanceRuntimeEffects(s);}
        const auto path=root+"/unit-"+id+'-'+std::to_string(hz)+'-'+std::to_string(level)+".ini";
        if(!saveRuntimeState(s,path,error))return fail("save "+error);
        auto restored=make(hz,level);
        if(!loadRuntimeState(*restored,path,error) || !same(s,*restored))return fail("restore "+error);
        ++saves;
        const bool images=hz==30 && level==0 && (std::string(id)=="HARV" || std::string(id)=="MTNK" || std::string(id)=="DISK");
        for(int tick=0;tick<hz*12;++tick) {
            if(images && (tick==0 || tick==30 || tick==74 || tick==128)) {
                const auto picture=root+"/"+id+"-phase-"+std::to_string(tick)+".bmp";
                const auto rendered=drawRuntimeStateOffscreenAtCell(s,{16,16},640,480,error,picture,false,false,-1,nullptr,false,nullptr,false,true);
                if(!rendered)return fail("render "+error);
                if(std::string(id)=="HARV" && !s.projectiles.empty() && rendered->projectileActorVoxelDrawn!=static_cast<int>(s.projectiles.size()))return fail("TIRE VXL not drawn");
                ++renders;
            }
            ++s.tick;advanceRuntimeEffects(s);++restored->tick;advanceRuntimeEffects(*restored);
            if(!same(s,*restored))return fail(std::string(id)+" saved trajectory diverged");
        }
        if(!s.projectiles.empty() || s.nextProjectileActorId!=nextId)return fail(std::string(id)+" expiration or repeated landing debris");
        ++deaths;
        std::cout<<"Unit death/save passed: "<<id<<" hz="<<hz<<" level="<<level<<" initial="<<initialCount<<'\n';
    }
    // Identical simulated elapsed time must produce identical physics at 15/30/60 Hz.
    std::string reference;
    for(int hz:{15,30,60}) {
        auto box=make(hz,0);auto& s=*box;
        auto rule=*s.rules.findUnitType("UnitType","HARV");rule.minDebris=rule.maxDebris=2;
        destroyed.type="HARV";destroyed.kind=WorldObjectKind::Unit;destroyed.authoredAircraft=false;
        spawnRuntimeUnitDebris(s,destroyed,rule,cause);
        if(s.projectiles.empty())return fail("timebase sample empty");
        for(int tick=0;tick<hz*3;++tick){++s.tick;advanceRuntimeEffects(s);}
        if(s.projectiles.empty())return fail("TIRE expired before three seconds");
        const auto record=serializeRuntimeVoxelDebris(s.projectiles.front().voxelDebris);
        if(hz==15)reference=record;else if(record!=reference)return fail("physics changes with host tick rate");++boundaries;
    }
    // Old schema-three ordinary projectiles remain readable; missing/forged
    // motion records must never become an alternative damage path.
    auto legacyBox=make(30,0);auto& legacy=*legacyBox;
    destroyed.type="MTNK";auto rule=*legacy.rules.findUnitType("UnitType","MTNK");rule.minDebris=rule.maxDebris=2;
    spawnRuntimeUnitDebris(legacy,destroyed,rule,cause);
    const auto newText=serializeRuntimeProjectileEffect(legacy.projectiles.front());
    const auto oldText=newText.substr(0,newText.rfind('|'));
    RuntimeProjectileEffect parsed;
    if(!parseRuntimeProjectileEffect(oldText,parsed,runtimeProjectileSaveSchemaV3) || parsed.voxelDebris.enabled)return fail("legacy schema 3");++boundaries;
    if(parseRuntimeProjectileEffect(newText+"|extra",parsed))return fail("surplus projectile field");++boundaries;
    auto forged=legacy.projectiles.front();forged.renderIntent="unitVoxelDebris";
    if(parseRuntimeProjectileEffect(serializeRuntimeProjectileEffect(forged),parsed))return fail("voxel intent without motion");++boundaries;
    std::cout<<"UnitDebris passed: rows="<<rows<<" draws="<<draws<<" fragments="<<fragments<<" records="<<records
        <<" deaths="<<deaths<<" saves="<<saves<<" boundaries="<<boundaries<<" renders="<<renders
        <<"; original lists/budgets, actual TIRE VXL, rotating bounces, death/crash, no duplicate landing, persistence and tick-rate equivalence.\n";
    return true;
}
