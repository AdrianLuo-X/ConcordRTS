bool runRuntimeIfvEffectsSmoke(const RuntimeConfig& config,std::string& error){
    std::cout<<std::unitbuf;
    const auto fail=[&](const std::string& text){error="IFV effects: "+text;return false;};
    auto live=std::make_unique<RuntimeWindowState>();live->config=config;
    live->report=buildRuntimeStartupReport(config,&live->world,&live->theater,&live->art,&live->rules,&live->mapFacts);
    if(!live->report.success)return fail("stock startup");
    auto s=std::make_unique<RuntimeWindowState>();s->config=config;s->report=live->report;
    s->rules=live->rules;s->rulesLoaded=true;s->art=live->art;s->artLoaded=true;
    s->theater=live->theater;s->theaterLoaded=true;s->visionEnabled=false;
    s->runtimeSoundsEnabled=false;s->runtimeSuppressPlayerAudioFeedback=true;s->runtimePreferencePersistenceEnabled=false;
    s->economy.owner="Americans";
    for(int y=0;y<30;++y)for(int x=0;x<30;++x){TerrainCell c;c.x=x;c.y=y;c.tileIndex=0;c.cellIndex512=y*512+x;
        s->world.accessTerrainCells().push_back(c);s->terrainInfoByCell.emplace(runtimeCellKey(x,y),RuntimeTerrainInfo{});}
    WorldObjectInstance vehicle;vehicle.instanceId=1;vehicle.kind=WorldObjectKind::Unit;vehicle.type="FV";
    vehicle.owner="Americans";vehicle.x=10;vehicle.y=10;vehicle.cell=runtimeCellKey(10,10);vehicle.health=200;
    vehicle.alive=true;vehicle.facing=vehicle.turretFacing=64;vehicle.mission="Guard";
    auto target=vehicle;target.instanceId=2;target.type="HTNK";target.owner="Russians";target.x=15;target.cell=runtimeCellKey(15,10);target.health=10000;
    s->world.accessObjects()={vehicle,target};s->terrainByCell=buildRuntimeTerrainLookup(s->world);initializeRuntimeNavigation(*s);
    const auto profile=runtimeWeaponProfileForObject(*s,*findObjectById(*s,1),findObjectById(*s,2));
    if(!profile.weapon || profile.weapon->id!="HoverMissile" || profile.weapon->burst!=2)return fail("normal IFV weapon selection");
    spawnRuntimeProjectile(*s,*findObjectById(*s,1),*findObjectById(*s,2),*profile.weapon,profile,25);
    std::vector<std::vector<RuntimeProjectileRenderActor>> phases;
    RuntimeImpactRenderActor legacySmoke;bool hadSmoke=false;bool hadImpact=false;
    for(int tick=0;tick<70;++tick){
        std::vector<RuntimeProjectileRenderActor> actors;
        for(const auto& shot:s->projectiles)actors.push_back(makeRuntimeProjectileRenderActor(shot,&s->rules,&s->art));
        phases.push_back(std::move(actors));
        ++s->tick;advanceRuntimeEffects(*s);
        for(const auto& effect:s->impactEffects){
            if(!hadSmoke && effect.renderIntent=="missileExhaust"){
                legacySmoke=makeRuntimeImpactRenderActor(effect);hadSmoke=true;
            }
            if(effect.renderIntent=="warheadImpact" && !effect.animId.empty())hadImpact=true;
        }
    }
    if(!hadSmoke || !hadImpact || findObjectById(*s,2)->health>=target.health || !s->projectiles.empty())return fail("real missile impact/lifecycle");
    std::string hashError;
    const auto beforeHash=runtimeStandaloneSimulationStateHash(*s,hashError);
    if(!hashError.empty())return fail(hashError);
    RuntimeMapRenderContext context;context.rules=&s->rules;context.art=&s->art;context.theater=&s->theater;
    context.assetRootPath=runtimeAssetRootPath(s->config);context.tileRootPath=runtimeTileRootPath(s->config,s->report.map);
    context.palettePath=runtimePalettePath(s->config,s->report.map);
    context.structureRootPath=runtimeStructureRootPath(s->config,s->report.map);
    context.structurePalettePath=runtimeStructurePalettePath(s->config,s->report.map);context.fixedTickHz=config.fixedTickHz;
    if(!runRuntimeIfvEffectsRenderContractCheck(context,s->world,phases,legacySmoke,error))return false;
    if(beforeHash!=runtimeStandaloneSimulationStateHash(*s,hashError) || !hashError.empty())return fail("render changed authoritative state");
    // Every stock passenger mode and its save/load path have an existing end-to-end scenario.
    live->rulesLoaded=live->artLoaded=live->theaterLoaded=true;
    live->runtimeSoundsEnabled=false;live->runtimeSuppressPlayerAudioFeedback=true;
    if(runRuntimeIfvCommandSmokeScenario(*live)!=0)return fail("passenger modes / combat / save-load regression");
    std::cout<<"IFV effects: passed; real flight+impact, presentation preserves authority, 17 passenger modes+save/load\n";
    return true;
}
