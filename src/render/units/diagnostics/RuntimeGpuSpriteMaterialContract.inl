// Direct original drawRuntimeShpFrame CPU oracle, actual production GPU material emitter.
static bool runRuntimeGpuSpriteMaterialContractCheck(const RuntimeMapRenderContext& assets,bool warp,std::string& error)try {
    RuntimeRendererEnvironmentScope environment;const auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};MapVisionGpuFixture gpu(warp);
    RuntimeShpFrameImage original;original.width=original.globalWidth=29;original.height=original.globalHeight=19;original.paletteRemapRangeFromHeader=true;original.paletteRemapStart=8;original.paletteRemapEnd=47;
    original.pixels.resize(551);original.ownerRemapMask.resize(551);original.lightLevels.resize(551);
    for(std::size_t i=0;i<551;++i){original.pixels[i]=static_cast<unsigned char>(i*73U);original.ownerRemapMask[i]=i%3!=0;original.lightLevels[i]=static_cast<unsigned char>(i*11U);}
    std::vector<RuntimePaletteColor> palette(256);for(unsigned i=0;i<256;++i)palette[i]={static_cast<unsigned char>(i*19U),static_cast<unsigned char>(i*23U),static_cast<unsigned char>(i*3U)};palette[255]={0,0,0};
    struct Scene{const char* name;bool owner,mask,grade,vpl,chaos;int alpha,lift,x,y,width,height;std::uint16_t add;};
    const std::array<Scene,20> scenes{{
        {"raw-index0-index255",false,false,false,false,false,255,0,64,48,128,96,0},
        {"owner-index-range",true,false,false,false,false,255,0,64,48,128,96,0},
        {"owner-mask",true,true,false,false,false,255,0,64,48,128,96,0},
        {"alpha0-highbyte",true,true,false,false,false,0,0,64,48,128,96,0},
        {"alpha1",true,true,false,false,false,1,0,64,48,128,96,0},
        {"alpha128",true,true,false,false,false,128,0,64,48,128,96,0},
        {"alpha254",true,true,false,false,false,254,0,64,48,128,96,0},
        {"vpl-light-remap-soften",true,true,true,true,false,255,21,64,48,128,96,0},
        {"vxl-light-remap-alpha",true,true,true,false,false,128,15,64,48,128,96,0},
        {"left-top-clip-light",true,false,true,true,false,254,-13,1,2,128,96,0},
        {"chaos-after-soften",true,true,true,true,true,255,7,64,48,128,96,0},
        {"rgb565-or",true,true,true,true,false,255,3,64,48,128,96,0x9a73},
        {"chaos-then-rgb565",true,true,true,true,true,128,31,64,48,128,96,0xffff},
        {"negative-lift",true,false,true,false,false,255,-41,64,48,128,96,0},
        {"transparent0",false,false,false,false,false,255,0,64,48,128,96,0},
        {"opaque-black255",false,false,false,false,false,255,0,64,48,128,96,0},
        {"right-bottom-clip",true,true,true,true,false,1,27,127,94,128,96,0x1841},
        {"odd-resize-clip",true,true,true,true,true,254,-4,52,36,53,37,0x4321},
        {"single-pixel-neighbour-boundary",true,true,true,true,false,255,8,64,48,128,96,0},
        {"owned-after-source-mutation",true,true,true,true,true,128,14,64,48,128,96,0xa519}}};
    std::uint64_t checked=0;int cases=0;for(const auto& s:scenes){auto image=original;auto colors=palette;if(!s.mask)image.ownerRemapMask.clear();
        if(cases==14)std::fill(image.pixels.begin(),image.pixels.end(),0);if(cases==15)std::fill(image.pixels.begin(),image.pixels.end(),255);
        if(cases==18){image.width=image.height=image.globalWidth=image.globalHeight=1;image.pixels={27};image.ownerRemapMask={1};image.lightLevels={187};}
        auto cpu=assets;cpu.vision=nullptr;cpu.mapFacts=nullptr;cpu.chaosBodyTint=s.chaos;cpu.airstrikeBodyColorAdd=s.add;
        std::vector<std::uint32_t> expected(static_cast<std::size_t>(s.width)*s.height,0x9a112233U),input(expected.size(),0x573f2189U);cpu.surfacePixels=expected.data();cpu.surfaceWidth=s.width;cpu.surfaceHeight=s.height;
        RuntimeMapCamera camera;camera.initialized=true;camera.tileWidth=60;camera.tileHeight=30;const RECT viewport{0,0,s.width,s.height};const POINT center{s.x,s.y};
        require(drawRuntimeShpFrame(nullptr,viewport,center,cpu,camera,image,colors,s.owner,RGB(32,96,201),s.lift,s.grade,s.vpl,s.alpha),"unchanged original CPU material oracle");
        if(cases==16)require(drawRuntimeTemporalBeam(nullptr,viewport,cpu,POINT{3,8},POINT{s.width-6,s.height-5},RGB(120,201,255),0.75),"original CPU temporal beam");
        if(cases==17)drawRuntimeMindControlArc(nullptr,viewport,cpu,POINT{2,s.height-6},POINT{s.width-3,s.height-7},RGB(214,52,196));
        gpu.beginTerrain(s.width,s.height,input);auto target=cpu;target.surfacePixels=nullptr;const auto captures=runtimeGpuSpriteMaterialCaptures;
        {RuntimeGpuWorldScope scope(gpu.drawDc());require(runtimeGpuWorldFill(gpu.drawDc(),viewport,0x9a112233U),"sprite constant GPU background");
            require(drawRuntimeShpFrame(gpu.drawDc(),viewport,center,target,camera,image,colors,s.owner,RGB(32,96,201),s.lift,s.grade,s.vpl,s.alpha),"actual production GPU material emitter");
            if(cases==16)require(drawRuntimeTemporalBeam(gpu.drawDc(),viewport,target,POINT{3,8},POINT{s.width-6,s.height-5},RGB(120,201,255),0.75),"actual GPU original temporal geometry/alpha");
            if(cases==17)drawRuntimeMindControlArc(gpu.drawDc(),viewport,target,POINT{2,s.height-6},POINT{s.width-3,s.height-7},RGB(214,52,196));}
        require(runtimeGpuSpriteMaterialCaptures>captures,"actual original table capture exits before CPU RGB loop");
        if(cases==19){std::fill(image.pixels.begin(),image.pixels.end(),0);std::fill(image.ownerRemapMask.begin(),image.ownerRemapMask.end(),0);std::fill(image.lightLevels.begin(),image.lightLevels.end(),0);colors.assign(256,{0,0,0});image.packedBaseColors.clear();image.packedRemapColors.clear();}
        gpu.finishSprites(input,expected,std::to_string(cases++)+'-'+s.name);checked+=expected.size();}
    require(cases==20,"all material scenes required");gpu.close();std::cout<<"S41 GPU sprite contract passed driver="<<(warp?"warp":"hardware")<<" profile="<<runtimeVxlRenderProfileToken()<<" cases="<<cases<<" checkedPixels="<<checked<<" fullBGRA=exact invalidHeaders=10 sourceRGB=GPU-material normalReadbacks=none\n";return true;
}catch(const std::exception& e){error=e.what();return false;}
