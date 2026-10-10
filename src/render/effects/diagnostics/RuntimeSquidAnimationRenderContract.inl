#include "render/effects/diagnostics/RuntimeSquidShipRenderContract.inl"

bool runRuntimeSquidAnimationRenderContractCheck(const RuntimeMapRenderContext& context, std::string& error) {
    const auto fail=[&](const std::string& why){error="squid animation: "+why;return false;};
    constexpr int width=208,height=240,columns=8;
    constexpr std::uint32_t background=0x00242c30U;
    std::vector<std::uint32_t> pixels(width*height,background),sheet(width*columns*height*6,background);
    auto cache=std::make_unique<RuntimeMapRenderCache>();
    auto probe=context;probe.vision=nullptr;probe.surfacePixels=pixels.data();
    probe.surfaceWidth=width;probe.surfaceHeight=height;
    std::unordered_map<std::string,COLORREF> colors{{"russians",RGB(210,25,35)}};
    probe.ownerRemapColors=&colors;
    RuntimeMapCamera camera;camera.initialized=true;
    camera.worldOffsetX=-width/2;camera.worldOffsetY=-height/2;
    const RECT viewport{0,0,width,height};
    WorldObjectInstance squid;squid.instanceId=1;squid.kind=WorldObjectKind::Unit;
    squid.type="SQD";squid.owner="Russians";squid.alive=true;squid.health=200;squid.mission="Attack";
    auto host=squid;host.instanceId=2;host.type="DEST";
    SpecialMechanismRenderSnapshot link;link.kind="parasite";link.objectId=1;link.targetObjectId=2;
    const auto* palette=runtimeUnitPaletteForRender(probe,*cache,true);
    const auto* art=probe.art?probe.art->findArtType("SQD"):nullptr;
    const auto* shape=loadRuntimeAuxiliaryShpSpriteFromCache("SQD",0,probe,*cache,false);
    if(!palette||!art||art->walkFrames!=20||art->firingFrames!=16||!shape||shape->frameCount!=296)
        return fail("native SQD walking/standing/firing layout missing");
    const auto storePanel=[&](int row,int col){for(int y=0;y<height;++y)
        std::copy_n(pixels.data()+y*width,width,sheet.data()+(row*height+y)*width*columns+col*width);};
    int grapplePoses=0,punchFrames=0,punchStateChecks=0;
    for(int hz:{15,30,60}) {
        probe.fixedTickHz=hz;
        for(bool submerged:{false,true})for(int direction=0;direction<8;++direction)
            for(int stage=0;stage<3;++stage) {
                host.type=submerged?"SUB":"DEST";host.submerged=submerged;
                host.facing=worldObjectFacingFromMapFacing(direction*32);
                const int nativeAge=stage==0?15:stage==1?50:90;
                link.progress=nativeAge*hz/15;
                const int expected=stage*80+direction*10+5;
                std::fill(pixels.begin(),pixels.end(),background);RuntimeMapRenderStats stats;
                const int actual=drawRuntimeSquidGrapple(nullptr,viewport,squid,host,link,probe,camera,*cache,stats);
                if(actual!=expected||stats.squidGrappleDrawn!=1||stats.squidGrappleMissing)
                    return fail("native grapple direction expected="+std::to_string(expected)
                        +" actual="+std::to_string(actual)+" runtimeFacing="+std::to_string(host.facing));
                const auto production=pixels;
                std::fill(pixels.begin(),pixels.end(),background);
                const POINT center=screenPoint(runtimeObjectDrawPoint(host,{},camera),viewport,camera);
                if(!drawRuntimeAuxiliaryShpSpriteWithPalette(nullptr,viewport,center,probe,camera,*cache,
                        "SQDG",expected,*palette,0,0,false,true,colors.at("russians"),submerged?144:255,true)
                    ||pixels!=production)return fail("native palette, submerged depth or player remap mismatch");
                if(hz==30)storePanel((submerged?3:0)+stage,direction);
                ++grapplePoses;
            }
        for(int direction=0;direction<8;++direction)for(int frame=0;frame<16;++frame) {
            squid.facing=direction*32;
            squid.spriteFiringStartTick=100;
            probe.animationTick=100+frame*2*hz/15;
            squid.weaponCooldownTicksRemaining=7; // Deliberately unrelated ROF.
            if(selectRuntimeObjectShpFrame(squid,probe,shape->frameCount,cache.get())!=168+direction*16+15-frame)
                return fail("SQD punch strip/direction/rate mismatch");
            for (const char* mission : {"Guard", "Move", "Attack"}) for (int reload : {0, 1, 200}) {
                auto interrupted = squid;
                interrupted.mission = mission;
                interrupted.weaponCooldownTicksRemaining = reload;
                interrupted.renderMoveActive = interrupted.hasRenderPosition = true;
                if (selectRuntimeObjectShpFrame(interrupted,probe,shape->frameCount,cache.get())
                    !=168+direction*16+15-frame)
                    return fail("native firing counter was gated by mission, movement or reload");
                ++punchStateChecks;
            }
            ++punchFrames;
        }
    }
    if(const char* root=std::getenv("CONCORD_SQUID_GRAPPLE_OUTPUT")) {
        if(!writeRuntimeBgraBmp(std::string(root)+"/native-grapple-actions.bmp",width*columns,height*6,sheet))
            return fail("grapple sheet write");
        std::fill(sheet.begin(),sheet.end(),background);
        probe.fixedTickHz=30;
        for(int direction=0;direction<8;++direction)for(int stage=0;stage<4;++stage) {
            squid.facing=direction*32;squid.spriteFiringStartTick=100;
            probe.animationTick=100+stage*16;
            std::fill(pixels.begin(),pixels.end(),background);
            if(!tryDrawObjectShpSprite(nullptr,viewport,squid,POINT{width/2,height-12},probe,camera,*cache))
                return fail("actual SQD punch draw");
            storePanel(stage,direction);
        }
        sheet.resize(width*columns*height*4);
        if(!writeRuntimeBgraBmp(std::string(root)+"/native-organic-punch.bmp",width*columns,height*4,sheet))
            return fail("punch sheet write");
    }
    squid.spriteFiringStartTick=-1;squid.weaponCooldownTicksRemaining=0;
    if(selectRuntimeObjectShpFrame(squid,probe,shape->frameCount,cache.get())>=168)
        return fail("idle squid retained punching pose");
    std::cout<<"Squid punch state: "<<punchStateChecks<<" movement/mission/reload combinations passed\n";
    std::cout<<"Squid animation: "<<grapplePoses<<" native surface/submerged grapple poses and "
        <<punchFrames<<" organic punch frames; unit palette, depth, opacity, player band, 15/30/60 Hz passed\n";
    return runRuntimeSquidShipRenderContractCheck(context,error);
}
