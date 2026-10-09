bool runRuntimeMutationPaletteRenderContractCheck(const RuntimeMapRenderContext& context,std::string& error){
    const auto fail=[&](const std::string& why){error="Mutation palette: "+why; return false;};
    const auto* art=context.art ? context.art->findArtType("GENDEATH") : nullptr;
    if(!art || art->makeInfantry!=0) return fail("original MakeInfantry=0 was not parsed");
    auto cache=std::make_unique<RuntimeMapRenderCache>();
    constexpr int size=120,columns=6;
    constexpr std::uint32_t background=0x005c6b49;
    std::vector<std::uint32_t> pixels(size*size,background),sheet(size*size*columns*2,background);
    auto probe=context; probe.vision=nullptr; probe.surfacePixels=pixels.data();
    probe.surfaceWidth=probe.surfaceHeight=size;
    RuntimeMapCamera camera; camera.initialized=true;
    camera.worldOffsetX=camera.worldOffsetY=-size/2;
    const RECT viewport{0,0,size,size}; const POINT center{size/2,size/2};
    const auto* unit=runtimeUnitPaletteForRender(probe,*cache,false);
    const auto* animation=runtimeAnimPaletteForRender(probe,*cache);
    if(!unit || !animation) return fail("original palettes missing");
    const int frames=runtimeAuxiliaryShpFrameCount(probe,*cache,"GENDEATH");
    if(frames<40) return fail("original mutation sequence missing");
    GameWorld world; int changedFrames=0,visibleFrames=0;
    const auto copy=[&](int row,int column){for(int y=0;y<size;++y)
        std::copy_n(pixels.begin()+y*size,size,sheet.begin()+(row*size+y)*size*columns+column*size);};
    for(int frame=0;frame<frames;++frame){
        std::fill(pixels.begin(),pixels.end(),background);
        if(!drawRuntimeAuxiliaryShpSpriteWithPalette(nullptr,viewport,center,probe,camera,*cache,"GENDEATH",frame,
            *animation,0,0,false,false,RGB(255,255,255),255,true)) return fail("old-palette comparison failed");
        const auto old=pixels;
        for(int column=0;column<columns;++column) if(frame==column*(frames-1)/(columns-1)) copy(0,column);
        std::fill(pixels.begin(),pixels.end(),background);
        RuntimeImpactRenderActor actor; actor.animId="GENDEATH"; actor.renderIntent="superWeaponEffect";
        actor.centerOnAnchor=true; actor.frameIndex=frame; actor.totalTicks=frames; actor.remainingTicks=frames-frame;
        RuntimeMapRenderStats stats;
        drawRuntimeImpactActor(nullptr,viewport,world,probe,camera,*cache,actor,stats);
        const auto actual=pixels;
        if(stats.impactActorDrawn!=1 || stats.impactAnimMissing!=0) return fail("real support-power visual did not draw");
        for(int column=0;column<columns;++column) if(frame==column*(frames-1)/(columns-1)) copy(1,column);
        std::fill(pixels.begin(),pixels.end(),background);
        if(!drawRuntimeAuxiliaryShpSpriteWithPalette(nullptr,viewport,center,probe,camera,*cache,"GENDEATH",frame,
            *unit,0,0,false,false,RGB(255,255,255),255,true) || pixels!=actual)
            return fail("mutation pixels did not match the original unit palette at frame "+std::to_string(frame));
        visibleFrames+=std::any_of(actual.begin(),actual.end(),[](auto c){return c!=background;});
        changedFrames+=old!=actual;
    }
    if(visibleFrames<40 || changedFrames<30) return fail("mutation color comparison was inconclusive");
    // Reuse the same cache: ordinary fire must retain its animation palette.
    std::string source;
    if(runtimePaletteForAuxiliaryEffectSprite(probe,*cache,"INITFIRE",RuntimeAuxiliaryEffectKind::Impact,&source)!=animation
        || source!="AnimPalette") return fail("mutation palette leaked into ordinary flame effects");
    int cloudFrames=0;
    for(const std::string id:{"WCCLOUD1","WCCLOUD2","WCCLOUD3"}) {
        const int count=runtimeAuxiliaryShpFrameCount(probe,*cache,id);
        if(count<=0) return fail("original lightning cloud missing: "+id);
        for(int frame=0;frame<count;++frame) {
            std::fill(pixels.begin(),pixels.end(),background);
            RuntimeImpactRenderActor actor; actor.animId=id; actor.renderIntent="superWeaponEffect";
            actor.centerOnAnchor=true; actor.frameIndex=frame; actor.totalTicks=count; actor.remainingTicks=count-frame;
            RuntimeMapRenderStats stats;
            drawRuntimeImpactActor(nullptr,viewport,world,probe,camera,*cache,actor,stats);
            const auto actual=pixels;
            std::fill(pixels.begin(),pixels.end(),background);
            if(stats.impactActorDrawn!=1 || !drawRuntimeAuxiliaryShpSpriteWithPalette(nullptr,viewport,center,
                probe,camera,*cache,id,frame,*unit,0,0,false,false,RGB(255,255,255),255,true) || pixels!=actual)
                return fail("lightning cloud unit palette/cache mismatch: "+id+" frame="+std::to_string(frame));
            ++cloudFrames;
        }
    }
    WorldObjectInstance sensor; sensor.type="NAPSIS"; sensor.kind=WorldObjectKind::Structure;
    sensor.owner="Observer"; sensor.alive=true; sensor.health=100;
    sensor.poweredOperational=true; sensor.x=sensor.y=0; sensor.instanceId=1;
    world.accessObjects().push_back(sensor); probe.visionObserverOwner="Observer";
    RuntimeImpactRenderActor warning; warning.animId="PSIWARN"; warning.owner="Enemy";
    warning.lifecycleState="NuclearTargetWarning"; warning.renderIntent="superWeaponEffect";
    warning.centerOnAnchor=true; warning.frameIndex=0; warning.totalTicks=warning.remainingTicks=20;
    for(int test=0;test<6;++test) {
        auto& detector=world.accessObjects().front(); detector=sensor;
        warning.owner="Enemy"; warning.x=0;
        if(test==1)detector.poweredOperational=false;
        if(test==2)detector.alive=false;
        if(test==3)warning.x=100;
        if(test==4)warning.owner="Observer";
        if(test==5)detector.owner="Enemy";
        RuntimeMapRenderStats stats;
        drawRuntimeImpactActor(nullptr,viewport,world,probe,camera,*cache,warning,stats);
        if((stats.impactActorDrawn>0)!=(test==0)) return fail("nuclear warning observer/sensor case "+std::to_string(test));
    }
    std::cout<<"Superweapon visuals passed: "<<cloudFrames<<" original lightning cloud frames, unit palette/cache; six nuclear sensor visibility cases\n";
    if(const char* folder=std::getenv("CONCORD_MUTATION_DIR"))
        if(*folder && !writeRuntimeBgraBmp(std::string(folder)+"/mutation-palette.bmp",size*columns,size*2,sheet)) return fail("preview write failed");
    std::cout<<"Mutation palette passed: "<<frames<<" original frames, "<<changedFrames
        <<" corrected frames, unit palette and fire cache isolation\n";
    return true;
}
