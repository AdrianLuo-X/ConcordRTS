bool runRuntimeChaosRenderContractCheck(const RuntimeMapRenderContext& context, const GameWorld& world,
    const std::vector<RuntimeImpactRenderActor>& gasFrames, std::string& error) {
    const auto fail=[&](const std::string& why){error="Chaos pixels: "+why; return false;};
    constexpr int width=400,height=280;
    constexpr std::uint32_t background=0x00667549;
    std::vector<std::uint32_t> pixels(width*height,background),sheet(width*height*2,background);
    auto cache=std::make_unique<RuntimeMapRenderCache>(); auto probe=context;
    probe.surfacePixels=pixels.data(); probe.surfaceWidth=width; probe.surfaceHeight=height;
    probe.vision=nullptr; probe.animationTick=0;
    std::unordered_map<std::string,COLORREF> colors{{"yuricountry",RGB(220,45,45)},{"americans",RGB(45,85,220)}};
    probe.ownerRemapColors=&colors;
    probe.fixedTickHz=30;
    for (const auto& gas : gasFrames) {
        const std::vector<RuntimeImpactRenderActor> impacts{gas}; probe.impactActors=&impacts;
        for (const auto& object : world.getObjects()) {
            if (runtimeVehicleBodyPoseForRender(object,probe).active())
                return fail("psychic gas rocked " + object.type + " frame=" + std::to_string(gas.frameIndex));
        }
    }
    probe.impactActors=nullptr;
    RuntimeMapCamera camera; camera.initialized=true;
    const auto anchor=projectCellXY(10,10,0,camera);
    camera.worldOffsetX=anchor.x-width/2; camera.worldOffsetY=anchor.y-130;
    const RECT viewport{0,0,width,height};
    std::vector<SpecialMechanismRenderSnapshot> statuses;
    const auto drawBody=[&](const WorldObjectInstance& object){
        const auto center=runtimeObjectBodyAnchorForRender(object,viewport,probe,camera);
        return object.kind==WorldObjectKind::Unit
            ? tryDrawVoxelSprite(nullptr,viewport,object,center,probe,camera,*cache)
            : tryDrawObjectShpSprite(nullptr,viewport,object,center,probe,camera,*cache);
    };
    for (const auto& target:world.getObjects()) {
        if (target.instanceId==1) continue;
        probe.specialMechanismSnapshots=nullptr; probe.animationTick=0;
        std::fill(pixels.begin(),pixels.end(),background);
        if (!drawBody(target)) return fail(target.type+" original body missing");
        const auto normal=pixels;
        SpecialMechanismRenderSnapshot status; status.kind="psychicStatus";
        status.targetObjectId=target.instanceId; status.phase="BerserkEnemy"; status.expireTick=60;
        statuses={status}; probe.specialMechanismSnapshots=&statuses;
        std::fill(pixels.begin(),pixels.end(),background); drawBody(target);
        int colored=0;
        for (std::size_t i=0;i<pixels.size();++i) {
            if (normal[i]==background) { if(pixels[i]!=background) return fail("status added a body-external marker"); continue; }
            const auto r=(pixels[i]>>16)&255U,g=(pixels[i]>>8)&255U,b=pixels[i]&255U;
            if (r<=g || r<=b) return fail(target.type+" retained non-red body pixels");
            ++colored;
        }
        if (colored<25) return fail("red body was invisible");
        const auto red=pixels;
        RuntimeMapRenderStats overlayStats;
        drawRuntimeSpecialMechanismOverlays(nullptr,viewport,world,probe,camera,*cache,overlayStats);
        if (pixels!=red || overlayStats.specialMechanismRenderActors!=0) return fail("purple marker remained");
        probe.animationTick=60; std::fill(pixels.begin(),pixels.end(),background); drawBody(target);
        if (pixels!=normal) return fail("status expiry did not restore the original cached palette");
        std::cout<<"Chaos pixels "<<target.type<<" red-body/expiry/no-marker=ok\n";
    }
    statuses.clear();
    for (const auto& target:world.getObjects()) if(target.instanceId!=1) {
        SpecialMechanismRenderSnapshot status; status.kind="psychicStatus"; status.targetObjectId=target.instanceId;
        status.phase="BerserkFriendly"; status.expireTick=90; statuses.push_back(status);
    }
    probe.animationTick=0; std::fill(pixels.begin(),pixels.end(),background);
    for(const auto& object:world.getObjects()) if(!drawBody(object)) return fail("native scene body missing");
    const auto bodies=pixels;
    std::copy(pixels.begin(),pixels.end(),sheet.begin());
    int peak=0,visibleFrames=0;
    for(const auto& actor:gasFrames) {
        pixels=bodies; probe.surfacePixels=pixels.data(); RuntimeMapRenderStats stats;
        drawRuntimeImpactActor(nullptr,viewport,world,probe,camera,*cache,actor,stats);
        if(stats.impactAnimMissing!=0) return fail("native CDGAS frame missing");
        int changed=0;
        for(std::size_t i=0;i<pixels.size();++i) if(pixels[i]!=bodies[i]) ++changed;
        if(changed>0) ++visibleFrames;
        if(changed>peak) {peak=changed; std::copy(pixels.begin(),pixels.end(),sheet.begin()+width*height);}
    }
    if(visibleFrames<8 || peak<500) return fail("native gas stayed sparse: frames="+std::to_string(visibleFrames)+" peak="+std::to_string(peak));
    if(const char* root=std::getenv("CONCORD_CHAOS_DIR"))
        if(*root && !writeRuntimeBgraBmp(std::string(root)+"/chaos-native.bmp",width,height*2,sheet)) return fail("preview write failed");
    std::cout<<"Chaos pixels native gas visibleFrames="<<visibleFrames<<" peak="<<peak<<"\n";
    return true;
}
