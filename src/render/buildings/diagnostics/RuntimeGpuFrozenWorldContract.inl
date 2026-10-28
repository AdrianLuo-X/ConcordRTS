// G005-G004-01 minimum only. Complete-world software oracle vs real production GPU.
static bool runRuntimeGpuFrozenWorldContractCheck(const RuntimeMapRenderContext& assets,bool warp,std::string& error)try {
    const auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};
    require(assets.rules&&assets.art&&assets.theater,"original frozen world assets");
    IniFile ini;MapDatabase map;GameWorld world;require(ini.loadFromString("[Map]\nTheater=TEMPERATE\nSize=0,0,128,128\nLocalSize=0,0,128,128\n")&&map.loadFromIni(ini)&&world.loadFromMap(map,assets.rules),"original local frozen fixture map");
    world.accessTerrainCells().clear();world.accessObjects().clear();world.accessTerrainObjects().clear();world.accessSmudges().clear();
    for(int y=64;y<=96;++y)for(int x=64;x<=96;++x){TerrainCell c;c.x=x;c.y=y;c.cellIndex512=y*512+x;c.tileIndex=0;world.accessTerrainCells().push_back(c);}
    const auto* rules=assets.rules->findBuildingType("CANEWY06");require(rules!=nullptr,"original CANEWY06 rules");
    WorldObjectInstance building;building.instanceId=1;building.kind=WorldObjectKind::Structure;building.type="CANEWY06";building.owner="Americans";building.alive=true;building.health=rules->strength;building.x=building.y=80;building.cell=80080;building.mission="Guard";building.poweredOperational=true;
    auto backBuilding=building;backBuilding.instanceId=3;backBuilding.x=backBuilding.y=79;backBuilding.cell=79079;
    WorldObjectInstance unit;unit.instanceId=2;unit.kind=WorldObjectKind::Unit;unit.type="MTNK";unit.owner="Americans";unit.alive=true;unit.health=100;unit.x=79;unit.y=80;unit.cell=80079;unit.mission="Guard";unit.facing=64;unit.turretFacing=128;
    MapVisionGpuFixture gpu(warp);auto cpuCache=std::make_unique<RuntimeMapRenderCache>(),gpuCache=std::make_unique<RuntimeMapRenderCache>();
    std::vector<SpecialMechanismRenderSnapshot> snapshots;auto context=assets;context.mapFacts=nullptr;context.vision=nullptr;context.seeHiddenObjects=false;context.animationTick=12;context.fixedTickHz=30;context.specialMechanismSnapshots=&snapshots;context.captureBuildingDepthHitSpans=true;context.stableRenderConfiguration=true;
    int cases=0;std::uint64_t checked=0;const auto changed=[](const auto& a,const auto& b){std::size_t n=0;for(std::size_t i=0;i<a.size();++i)n+=(a[i]&0xffffffU)!=(b[i]&0xffffffU);return n;};
    const auto freeze=[&](std::initializer_list<int> ids){snapshots.clear();for(int id:ids){SpecialMechanismRenderSnapshot s;s.kind="temporal";s.targetObjectId=id;s.requiredProgress=10000;s.progress=100;snapshots.push_back(s);}};
    const auto render=[&](const std::string& name,int width,int height,int tileWidth,bool shadows,int requiredSaves,bool requireChain){
        auto input=context;input.shadowsEnabled=shadows;RuntimeMapCamera camera;camera.initialized=true;camera.tileWidth=tileWidth;camera.tileHeight=tileWidth/2;camera.worldOffsetX=-width/2;camera.worldOffsetY=80*tileWidth/2-height/2;
        const auto before=world.deterministicSimulationStateHash();const RECT viewport{0,0,width,height};
        const auto draw=[&](concord::ui::win32::SkirmishBackBufferWin32& surface,RuntimeMapRenderCache& cache){auto target=input;target.surfacePixels=reinterpret_cast<std::uint32_t*>(surface.writablePixels());target.surfaceWidth=width;target.surfaceHeight=height;target.surfaceTopDownOwned=true;auto pose=camera;const auto stats=drawRuntimeMap(surface.dc(),viewport,world,target,pose,cache);require(stats.rendered,"actual original world renderer");};
        const auto expected=gpu.softwareWorld(width,height,[&](auto& surface){draw(surface,*cpuCache);});
        const auto counts=gpu.worldFrame(width,height,expected,std::to_string(cases++)+'-'+name,[&](auto& surface){draw(surface,*gpuCache);});
        require(counts[0]>=static_cast<std::uint64_t>(requiredSaves)&&counts[1]==counts[0],"actual frozen whole body GPU save/restore");if(requireChain)require(counts[2]>0,"actual frozen foreign underlay/depth chain GPU");
        require(world.deterministicSimulationStateHash()==before,"original complete local World authority unchanged");checked+=expected.size();return expected;
    };
    world.accessObjects()={building};freeze({});const auto single=render("normal-building",640,480,60,false,0,false);
    freeze({1});const auto frozen=render("frozen-building",640,480,60,false,1,false);require(changed(single,frozen)>40,"nonempty visible actual frozen building");
    require(render("paused-frozen-building",640,480,60,false,1,false)==frozen,"paused original frozen pixels");freeze({});require(render("released-building",640,480,60,false,0,false)==single,"exact unfreeze original pixels");
    world.accessObjects()={backBuilding,building,unit};const auto normalOverlap=render("normal-overlap",640,480,60,true,0,false);
    freeze({1});const auto underlay=render("frozen-building-underlay",640,480,60,true,1,true);require(changed(normalOverlap,underlay)>40,"visible foreign underlay translucency");
    require(render("paused-frozen-underlay",640,480,60,true,1,true)==underlay,"paused original foreign underlay exact");freeze({});require(render("released-overlap",640,480,60,true,0,false)==normalOverlap,"release foreign underlay exact");
    unit.x=82;unit.y=83;unit.cell=83082;world.accessObjects()={backBuilding,building,unit};freeze({2});(void)render("frozen-mobile-front",640,480,60,true,1,false);
    unit.x=79;unit.y=80;unit.cell=80079;world.accessObjects()={backBuilding,building,unit};freeze({1,2});(void)render("both-frozen-underlay",640,480,60,true,2,true);
    freeze({1});(void)render("resized-frozen-underlay",401,301,60,true,1,true);(void)render("zoom72-frozen-underlay",640,480,72,true,1,true);
    unit.x=82;unit.y=83;unit.cell=83082;world.accessObjects()={backBuilding,building,unit};freeze({2});(void)render("zoom48-frozen-mobile",640,480,48,true,1,false);
    building.health=std::max(1,rules->strength/4);world.accessObjects()={backBuilding,building,unit};freeze({1});(void)render("damaged-frozen-building",640,480,60,true,1,false);
    const bool objects=std::getenv("CONCORD_S41_GPU_OBJECT_WORLD")!=nullptr;
    if(objects){freeze({});auto factory=building;factory.instanceId=4;factory.type="NAWEAP";factory.owner="Russians";
        const auto* factoryRules=assets.rules->findBuildingType(factory.type);require(factoryRules!=nullptr,"original NAWEAP rules");factory.health=factoryRules->strength;
        factory.factoryExitAirborne=true;factory.factoryExitAnimationStartTick=0;factory.factoryExitAnimationEndTick=2000;
        const auto* factoryArt=findArtTypeForObject(factory,context);require(factoryArt!=nullptr,"original factory art");
        context.animationTick=runtimeFactoryAirExitTransitionTicks(factoryArt,context.art,context.fixedTickHz)+1;
        world.accessObjects()={factory};const auto base=render("factory-host-open-base",640,480,60,true,0,false);
        auto air=unit;air.instanceId=5;air.type="ZEP";air.owner="Russians";const auto* airRules=assets.rules->findUnitType("vehicle",air.type);require(airRules!=nullptr,"original ZEP rules");air.health=airRules->strength;
        const auto foundation=runtimeObjectFoundationDimensions(factory,context);air.x=factory.x+(std::max(1,foundation.width)-1)/2;air.y=factory.y+(std::max(1,foundation.height)-1)/2;air.cell=air.y*1000+air.x;
        air.airborne=true;air.flightLevel=1;air.factoryExitHostId=factory.instanceId;
        world.accessObjects()={factory,air};const auto open=render("factory-exit-open",640,480,60,true,0,false);require(changed(base,open)>40,"actual visible emerging ZEP");
        factory.health=std::max(1,factoryRules->strength/4);world.accessObjects()={factory,air};(void)render("factory-exit-damaged-wall",640,480,60,true,0,false);
        const auto resized=render("factory-exit-odd-resize",401,301,60,true,0,false);require(render("factory-exit-paused",401,301,60,true,0,false)==resized,"factory paused same fullBGRA");
    }
    require(cases==(objects?19:14),"all original frozen and optional complete object scenes required");gpu.close();
    std::cout<<"S41 GPU frozen contract passed driver="<<(warp?"warp":"hardware")<<" cases="<<cases<<" checkedPixels="<<checked<<" fullBGRA=exact localWorldAuthority=unchanged sourceRGB=GPU-material normalReadbacks=none diagnosticReadbacks=explicit\n";return true;
}catch(const std::exception& e){error=e.what();return false;}
