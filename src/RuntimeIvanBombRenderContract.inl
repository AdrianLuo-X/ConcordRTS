bool runRuntimeIvanBombRenderContractCheck(const RuntimeMapRenderContext& context, std::string& error) {
    const auto fail=[&](const std::string& reason){error="Ivan icon render: "+reason;return false;};
    auto cache=std::make_unique<RuntimeMapRenderCache>();
    auto probe=context;probe.vision=nullptr;probe.specialMechanismSnapshots=nullptr;
    auto* first=loadRuntimeAuxiliaryShpSpriteFromCache("BOMBCURS",0,probe,*cache,false);
    if (!first || !first->loaded || first->frameCount!=13) return fail("original 13-frame BOMBCURS unavailable");
    for(int n=0;n<13;++n) {
        auto* frame=loadRuntimeAuxiliaryShpSpriteFromCache("BOMBCURS",n,probe,*cache,false);
        if(!frame || !frame->loaded || frame->image.pixels.empty()) return fail("native frame decode "+std::to_string(n));
    }
    const std::pair<int,int> authoredCases[]={{0,0},{7,0},{8,1},{15,1},{16,0},
        {37,1},{40,2},{74,3},{440,11},{443,11},{444,12},{448,11},{449,11}};
    for(int hz:{15,30,60}) for(const auto [time,expected]:authoredCases) {
        if(runtimeIvanBombIconFrame(time*hz/15,450*hz/15,time*hz/15,hz,8,13)!=expected)
            return fail("native phase/rate differs at authored frame "+std::to_string(time)+" / "+std::to_string(hz));
    }
    if(runtimeIvanBombIconFrame(450,450,450,15,8,13)!=-1
        || runtimeIvanBombIconFrame(0,1,0,15,0,13)!=0
        || runtimeIvanBombIconFrame(0,10,0,30,8,1)!=0
        || runtimeIvanBombIconFrame(0,10,0,30,8,0)!=-1) return fail("short delay / zero-rate / malformed resource boundaries");
    const auto base=packRuntimeDibColor(RGB(36,40,48));
    constexpr int width=128,height=100;
    const RECT viewport{0,0,width,height};
    std::vector<std::uint32_t> pixels(width*height,base);
    probe.surfacePixels=pixels.data();probe.surfaceWidth=width;probe.surfaceHeight=height;
    probe.fixedTickHz=30;
    RuntimeMapCamera camera;camera.tileWidth=60;camera.tileHeight=30;
    camera.worldOffsetX=-64;camera.worldOffsetY=-65;camera.initialized=true;
    WorldObjectInstance target;target.instanceId=2;target.kind=WorldObjectKind::Unit;
    target.x=target.y=target.level=0;
    target.type="MTNK";target.owner="Enemy";target.alive=true;target.health=100;
    SpecialMechanismRenderSnapshot snapshot;snapshot.kind="timedBomb";snapshot.phase="Ivan";
    snapshot.targetObjectId=2;snapshot.objectId=1;snapshot.owner="Owner";
    snapshot.requiredProgress=900;snapshot.expireTick=900;
    const auto draw=[&]() {
        std::fill(pixels.begin(),pixels.end(),base);
        return drawRuntimeIvanBombIcon(nullptr,viewport,target,snapshot,probe,camera,*cache);
    };
    snapshot.progress=16;
    if(draw()!=1 || std::all_of(pixels.begin(),pixels.end(),[&](auto p){return p==base;}))
        return fail("real native sprite did not draw");
    const auto paused=pixels;
    target.owner="Blue";snapshot.owner="Green";
    if(draw()!=1 || pixels!=paused) return fail("house colors remapped the red bomb");
    target.owner="Enemy";snapshot.owner="Owner";
    for(int time:{0,100,9999}) {
        probe.animationTick=time;
        if(draw()!=1 || pixels!=paused) return fail("UI animation advanced a paused bomb");
    }
    snapshot.progress=32;
    if(draw()!=0 || pixels==paused) return fail("configured flicker did not change pixels");
    auto* rates=context.rules ? const_cast<SpecialMechanismRules*>(&context.rules->getSpecialMechanismRules()) : nullptr;
    if(!rates) return fail("rules missing");
    const int originalRate=rates->ivanIconFlickerRateFrames;
    rates->ivanIconFlickerRateFrames=4;
    snapshot.progress=8;
    const int changedRate=draw();
    rates->ivanIconFlickerRateFrames=originalRate;
    if(changedRate!=1 || draw()!=0) return fail("IvanIconFlickerRate not consumed by production draw");
    target.hasRenderPosition=true;target.renderMoveActive=true;target.renderX=0.25;target.renderY=0;
    draw();const auto moving=pixels;target.x=9;target.y=9;
    draw();if(pixels!=moving) return fail("icon jumped to logical destination before its host");
    target.renderX=0.75;draw();if(pixels==moving) return fail("icon did not follow host interpolation");

    // Exercise the complete overlay entry with current vision and a removed
    // bomb snapshot; real BombSight/owner filtering is checked in the host.
    GameWorld world;target.hasRenderPosition=target.renderMoveActive=false;target.x=target.y=0;
    world.accessObjects().push_back(target);
    TerrainCell terrain;terrain.x=terrain.y=0;terrain.tileIndex=0;terrain.cellIndex512=0;
    world.accessTerrainCells().push_back(terrain);
    RuntimeVisionSystem vision;
    vision.update(world,context.rules,{{"Viewer",{}},{"Enemy",{}}},0);
    probe.vision=&vision;probe.visionObserverOwner="Viewer";
    std::vector<SpecialMechanismRenderSnapshot> snapshots{snapshot};probe.specialMechanismSnapshots=&snapshots;
    RuntimeMapRenderStats hiddenStats;std::fill(pixels.begin(),pixels.end(),base);
    drawRuntimeSpecialMechanismOverlays(nullptr,viewport,world,probe,camera,*cache,hiddenStats);
    if(hiddenStats.specialMechanismRenderActors!=0 || std::any_of(pixels.begin(),pixels.end(),[&](auto p){return p!=base;}))
        return fail("icon leaked target through shroud");
    probe.vision=nullptr;snapshots.clear();RuntimeMapRenderStats removedStats;
    drawRuntimeSpecialMechanismOverlays(nullptr,viewport,world,probe,camera,*cache,removedStats);
    if(removedStats.specialMechanismRenderActors!=0 || std::any_of(pixels.begin(),pixels.end(),[&](auto p){return p!=base;}))
        return fail("removed/disarmed icon remained visible");

    constexpr int columns=13;
    std::vector<std::uint32_t> strip(columns*64*64,base);
    const auto* palette=runtimeNamedPaletteForRender(context,*cache,"mousepal.pal");
    const auto* oldPalette=runtimeAnimPaletteForRender(context,*cache);
    if(!palette || !oldPalette) return fail("native mouse/animation palette unavailable");
    probe.surfacePixels=strip.data();probe.surfaceWidth=columns*64;probe.surfaceHeight=64;
    const RECT stripViewport{0,0,columns*64,64};
    for(int n=0;n<columns;++n) if(!drawRuntimeAuxiliaryShpSpriteWithPalette(nullptr,stripViewport,
        POINT{32+n*64,32},probe,camera,*cache,"BOMBCURS",n,*palette,0,0,false,false,RGB(255,255,255),255,true))
        return fail("native contact sheet draw failed");
    int redPixels=0;
    for(int n=0;n<columns;++n) {
        int frameRed=0;
        for(int y=0;y<64;++y)for(int x=n*64;x<(n+1)*64;++x) {
            const auto p=strip[y*columns*64+x];
            const int r=(p>>16)&255,g=(p>>8)&255,b=p&255;
            if(r>=100 && r>g*3/2 && r>b*3/2)++frameRed;
        }
        if(frameRed<10)return fail("bomb lost its native red sticks in frame "+std::to_string(n));
        redPixels+=frameRed;
    }
    const char* output=std::getenv("CONCORD_PRISM_PRESENTATION_OUTPUT");
    const std::string root=output&&*output?output:"build/smoke/feature-set-11/08-ivan-icon";
    std::filesystem::create_directories(root);
    if(!writeRuntimeBgraBmp(root+"/native-bombcurs.bmp",columns*64,64,strip))
        return fail("native contact sheet write failed");
    const auto nativeStrip=strip;
    std::fill(strip.begin(),strip.end(),base);
    for(int n=0;n<columns;++n) if(!drawRuntimeAuxiliaryShpSpriteWithPalette(nullptr,stripViewport,
        POINT{32+n*64,32},probe,camera,*cache,"BOMBCURS",n,*oldPalette,0,0,false,false,RGB(255,255,255),255,true))
        return fail("old-palette control draw failed");
    if(strip==nativeStrip || !writeRuntimeBgraBmp(root+"/old-animation-palette-control.bmp",columns*64,64,strip))
        return fail("old-palette control did not differ");
    // Compare the production icon with the same native palette, including
    // its world anchor; a correct contact sheet alone cannot verify the fix.
    probe.surfacePixels=pixels.data();probe.surfaceWidth=width;probe.surfaceHeight=height;
    target.hasRenderPosition=target.renderMoveActive=false;target.x=target.y=0;
    snapshot.progress=16;
    if(draw()!=1)return fail("production comparison phase");
    const auto production=pixels;std::fill(pixels.begin(),pixels.end(),base);
    POINT anchor=screenPoint(runtimeSpecialMechanismObjectRenderPoint(target,camera),viewport,camera);
    anchor.y-=24;
    if(!drawRuntimeAuxiliaryShpSpriteWithPalette(nullptr,viewport,anchor,probe,camera,*cache,
            "BOMBCURS",1,*palette,0,0,false,false,RGB(255,255,255),255,true) || pixels!=production)
        return fail("production icon differs from original mouse palette");
    std::cout<<"Ivan bomb palette: 13 native red frames, "<<redPixels
        <<" red pixels; production/owner-color/old-palette comparison passed\n";
    if(!runRuntimeParticleRenderContractCheck(error)) return false;
    return true;
}
