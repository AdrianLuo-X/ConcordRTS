// Original independent CPU lighting functions, complete channel/alpha matrices.
static bool runRuntimeGpuLightPrecisionContractCheck(const RuntimeMapRenderContext& assets,bool warp,std::string& error)try {
    RuntimeRendererEnvironmentScope environment;const auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};MapVisionGpuFixture gpu(warp);
    IniFile iniA,iniB,iniZero;auto rulesA=std::make_unique<RulesDatabase>(),rulesB=std::make_unique<RulesDatabase>(),rulesZero=std::make_unique<RulesDatabase>();
    require(iniA.loadFromString("[Radiation]\nRadTintFactor=4\nRadLightFactor=0\n")&&rulesA->loadFromIni(iniA),"original radiationfactor parser A");
    require(iniB.loadFromString("[Radiation]\nRadTintFactor=0\nRadLightFactor=4\n")&&rulesB->loadFromIni(iniB),"original radiationfactor parser B");
    require(iniZero.loadFromString("[Radiation]\nRadTintFactor=0\nRadLightFactor=0\n")&&rulesZero->loadFromIni(iniZero),"original radiationfactor parser zero");
    constexpr int width=256,height=256;const RECT viewport{0,0,width,height};RuntimeMapCamera camera;camera.initialized=true;camera.tileWidth=60;camera.tileHeight=30;camera.worldOffsetX=-width/2;camera.worldOffsetY=-height/2;
    const auto background=[](int x){return 0x9a000000U|((static_cast<unsigned>(x)*17U&255U)<<16U)|((static_cast<unsigned>(x)*23U&255U)<<8U)|(static_cast<unsigned>(x)*73U&255U);};
    int cases=0;std::uint64_t checked=0;for(int scene=0;scene<12;++scene){auto context=assets;context.vision=nullptr;context.mapFacts=nullptr;context.projectileActors=nullptr;context.impactActors=nullptr;context.particleSnapshots=nullptr;context.particleBeamSnapshots=nullptr;context.specialMechanismSnapshots=nullptr;context.localLightSnapshots=nullptr;context.lightingProfile=nullptr;
        RuntimeLightingTint tint=scene==1?RuntimeLightingTint{0,0,0}:RuntimeLightingTint{0.51,1.27,0.83};context.lightingTint=&tint;
        std::vector<RuntimeLocalLightSnapshot> lights;RuntimeLocalLightSnapshot light;light.stableId=90;light.worldX=light.worldY=light.worldLevel=0;light.owner="Americans";light.radiusPixels=scene==2?1:192;light.red=255;light.green=133;light.blue=21;light.intensity=255;light.revision=91;lights.push_back(light);
        if(scene==4){light.stableId=2;light.radiusPixels=137;light.red=13;light.green=91;light.blue=255;light.intensity=187;light.worldX=0.4;light.worldY=-0.1;lights.push_back(light);}context.localLightSnapshots=&lights;
        if(scene==5||scene==10||scene==11)context.rules=nullptr;else if(scene==7)context.rules=rulesA.get();else if(scene==8)context.rules=rulesB.get();else if(scene==9)context.rules=rulesZero.get();
        const COLORREF radiation=scene==6?RGB(193,23,211):scene==7?RGB(11,83,255):scene==8?RGB(255,0,0):scene==9?RGB(255,255,255):scene==10?RGB(0,0,0):RGB(0,255,0);
        std::vector<std::uint32_t> expected(width*height),input(width*height,0xa53917c3U);for(int y=0;y<height;++y)for(int x=0;x<width;++x)expected[static_cast<std::size_t>(y)*width+x]=background(x);
        auto cpu=context;cpu.surfacePixels=expected.data();cpu.surfaceWidth=width;cpu.surfaceHeight=height;RuntimeMapRenderStats cpuStats,gpuStats;
        const auto draw=[&](HDC dc,const RuntimeMapRenderContext& target,RuntimeMapRenderStats& stats)->int {
            if(scene<2)return applyRuntimeLightingTintToViewport(dc,viewport,target);
            if(scene<=4){(void)applyRuntimeLocalLightsToViewport(dc,viewport,target,camera,stats);return static_cast<int>(stats.localLightPixels);}
            std::optional<RuntimeGpuRadiationScope> scope;if(runtimeGpuWorldTarget(target))scope.emplace(viewport,target,radiation);int count=0;
            for(int y=0;y<height;++y)for(int x=0;x<width;++x)count+=tintRuntimeRadiationTerrainPixel(dc,target,x,y,radiation,scene==11?0:y);return count;
        };
        const int cpuCount=draw(nullptr,cpu,cpuStats);gpu.beginTerrain(width,height,input);auto target=cpu;target.surfacePixels=nullptr;int gpuCount=0;
        {RuntimeGpuWorldScope scope(gpu.drawDc());for(int x=0;x<width;++x)(void)runtimeGpuWorldFill(gpu.drawDc(),RECT{x,0,x+1,height},background(x));gpuCount=draw(gpu.drawDc(),target,gpuStats);}
        require(cpuCount==gpuCount&&cpuStats.localLightsApplied==gpuStats.localLightsApplied&&cpuStats.localLightPixels==gpuStats.localLightPixels,"original global/local/radiation counters exact");
        const auto kind=scene<2?concord::render::MapDrawKind::Lookup:scene<=4?concord::render::MapDrawKind::LocalLight:concord::render::MapDrawKind::ReceiverLookup;
        const auto operations=scene==11?0U:scene==4?2U:1U;const auto label=std::to_string(cases++)+"-light-precision-scene"+std::to_string(scene);gpu.finishOperations(input,expected,label,kind,operations);
        std::cout<<"G007 precision counts case="<<label<<" cpu="<<cpuCount<<" gpu="<<gpuCount<<" localApplied="<<gpuStats.localLightsApplied<<" alphaRange="<<(scene>=5&&scene<11?"0..255":"selected")<<'\n';checked+=expected.size();}
    require(cases==12,"all original light precision scenes required");gpu.close();std::cout<<"G007 GPU light precision passed driver="<<(warp?"warp":"hardware")<<" cases="<<cases<<" checkedPixels="<<checked<<" fullBGRA=exact originalCPU=unchanged counters=exact normalReadbacks=none\n";return true;
}catch(const std::exception& e){error=e.what();return false;}
