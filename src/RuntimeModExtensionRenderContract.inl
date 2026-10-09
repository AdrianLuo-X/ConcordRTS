bool runRuntimeModExtensionRenderContractCheck(const RuntimeMapRenderContext& context, std::string& error) {
    const auto fail=[&](const std::string& message){error="Mod extension render: "+message;return false;};
    if(!context.art)return fail("ART unavailable");
    RuntimeMapRenderContext probe=context;probe.vision=nullptr;probe.specialMechanismSnapshots=nullptr;
    constexpr int width=128,height=96,columns=8;
    constexpr std::uint32_t background=0x00384838;
    std::vector<std::uint32_t> pixels(width*height,background),sheet(width*columns*height,background);
    probe.surfacePixels=pixels.data();probe.surfaceWidth=width;probe.surfaceHeight=height;
    RuntimeMapRenderCache cache;GameWorld world;
    RuntimeMapCamera camera;camera.worldOffsetX=-width/2;camera.worldOffsetY=-height/2;camera.initialized=true;
    const RECT viewport{0,0,width,height};
    RuntimeImpactRenderActor actor;actor.animId="MINDANIM";actor.renderIntent="attachEffect";
    actor.lifecycleState="AttachEffect";actor.x=actor.y=0;actor.remainingTicks=1;
    auto* first=loadRuntimeAuxiliaryShpSpriteFromCache("MINDANIM",0,probe,cache,false);
    if(!first || first->frameCount<2)return fail("original attached ring unavailable");
    const int count=first->frameCount;
    const auto* art=context.art->findArtType("MINDANIM");
    const int delay=runtimeRuleAnimationDelayFrames(art ? art->rate : 0);
    for(int hz:{15,30,60}){
        probe.fixedTickHz=hz;
        actor.totalTicks=1;
        if(runtimeImpactActorFrameIndex(actor,"MINDANIM",probe,cache)!=0)return fail("initial animation phase");
        actor.totalTicks=1+delay*hz/15;
        if(runtimeImpactActorFrameIndex(actor,"MINDANIM",probe,cache)!=1)return fail("animation cadence depends on tick rate");
        actor.totalTicks=1+count*delay*hz/15;
        if(runtimeImpactActorFrameIndex(actor,"MINDANIM",probe,cache)!=0)return fail("attached animation did not loop");
    }
    probe.fixedTickHz=30;int visiblePixels=0;
    for(int frame=0;frame<columns;++frame){
        std::fill(pixels.begin(),pixels.end(),background);
        actor.totalTicks=1+(frame%count)*delay*2;
        RuntimeMapRenderStats stats;
        drawRuntimeImpactActor(nullptr,viewport,world,probe,camera,cache,actor,stats);
        visiblePixels+=static_cast<int>(std::count_if(pixels.begin(),pixels.end(),[](auto value){return value!=background;}));
        for(int y=0;y<height;++y)std::copy_n(pixels.begin()+y*width,width,sheet.begin()+y*width*columns+frame*width);
    }
    if(visiblePixels==0)return fail("actual attached animation produced no pixels: "+cache.lastProjectileImageId+" / "+cache.lastProjectilePaletteSource);
    if(const char* path=std::getenv("CONCORD_MOD_EXTENSION_ANIMATION_BMP");path && *path)
        if(!writeRuntimeBgraBmp(path,width*columns,height,sheet))return fail("animation preview write");
    std::cout<<"Mod extension render: original pixels, 15/30/60Hz and repeated loop passed\n";
    return true;
}
