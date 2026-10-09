bool runRuntimeIfvEffectsRenderContractCheck(const RuntimeMapRenderContext& context,
    const GameWorld& world, const std::vector<std::vector<RuntimeProjectileRenderActor>>& phases,
    const RuntimeImpactRenderActor& legacySmoke, std::string& error) {
    const auto fail=[&](const std::string& text){error="IFV render: "+text;return false;};
    constexpr int width=640,height=320;
    constexpr std::uint32_t background=0x001c2428;
    std::vector<std::uint32_t> pixels(width*height,background), preview;
    auto cache=std::make_unique<RuntimeMapRenderCache>();
    auto probe=context;probe.vision=nullptr;
    probe.surfacePixels=pixels.data();probe.surfaceWidth=width;probe.surfaceHeight=height;
    RuntimeMapCamera camera;camera.initialized=true;
    const RECT viewport{0,0,width,height};
    const auto middle=projectCellXY(12,10,0,camera);
    camera.worldOffsetX=middle.x-width/2;camera.worldOffsetY=middle.y-height/2;
    if(phases.size()<12 || phases.front().empty())return fail("flight samples missing");
    const auto* dragon=probe.art->findArtType("DRAGON");
    if(!dragon || !dragon->useLineTrail || !dragon->rotates
        || dragon->lineTrailColor!="216,216,255" || dragon->lineTrailColorDecrement!=16)
        return fail("retail DRAGON art flags");
    bool line=false,body=false;
    for(std::size_t tick=0;tick<phases.size();++tick){
        std::fill(pixels.begin(),pixels.end(),background);
        probe.projectileActors=&phases[tick];probe.animationTick=static_cast<int>(tick);
        RuntimeMapRenderStats stats;
        drawRuntimeProjectileActors(nullptr,viewport,probe,camera,*cache,stats,&world);
        if(stats.missileExhaustDrawn || stats.missileExhaustHeadDrawn || stats.projectileActorTravelMissing)
            return fail("line projectile drew legacy smoke or lost its body");
        line|=stats.projectileLineTrailSegments>0 && stats.projectileLineTrailPixels>0;
        body|=stats.projectileDirectionalSprites>0 && stats.projectileActorTravelDrawn>0;
        if(tick==12){
            const auto frozen=pixels;
            const auto nodes=cache->projectileLineTrails.begin()->second.points.size();
            std::fill(pixels.begin(),pixels.end(),background);
            RuntimeMapRenderStats repeated;
            drawRuntimeProjectileActors(nullptr,viewport,probe,camera,*cache,repeated,&world);
            if(pixels!=frozen || cache->projectileLineTrails.begin()->second.points.size()!=nodes)
                return fail("paused repaint changed path or pixels");
            for(const auto& object:world.getObjects()){
                const auto center=screenPoint(runtimeObjectDrawPoint(object,{},camera),viewport,camera);
                if(object.kind==WorldObjectKind::Unit)tryDrawVoxelSprite(nullptr,viewport,object,center,probe,camera,*cache);
            }
            preview=pixels;
        }
    }
    if(!line || !body)return fail("no visible line or directional missile");
    if(!runtimeImpactReplacedByLineTrail(legacySmoke,probe))return fail("legacy detached smoke not classified");
    std::fill(pixels.begin(),pixels.end(),background);RuntimeMapRenderStats smokeStats;
    drawRuntimeImpactActor(nullptr,viewport,world,probe,camera,*cache,legacySmoke,smokeStats);
    if(pixels!=std::vector<std::uint32_t>(width*height,background) || smokeStats.missileExhaustDrawn)
        return fail("legacy detached smoke remains visible");
    std::vector<RuntimeProjectileRenderActor> none;probe.projectileActors=&none;
    probe.animationTick+=context.fixedTickHz*3;
    RuntimeMapRenderStats expired;drawRuntimeProjectileActors(nullptr,viewport,probe,camera,*cache,expired,&world);
    if(!cache->projectileLineTrails.empty() || expired.projectileLineTrailSegments)return fail("trail did not expire");

    auto actor=phases.front().front();actor.hasCurrentPosition=true;actor.currentX=12;actor.currentY=10;actor.currentLevel=0;
    actor.fromX=actor.currentX;actor.fromY=actor.currentY;actor.fromLevel=0;
    actor.sourceScreenOffsetX=actor.sourceScreenOffsetY=0;
    const int frameCount=runtimeAuxiliaryShpFrameCount(probe,*cache,"DRAGON");
    if(frameCount<32)return fail("DRAGON directional resource has fewer than 32 frames");
    std::vector<std::uint32_t> directions(8*120*120,background);
    std::set<int> seen;
    for(int facing=0;facing<8;++facing){
        const double angle=facing*6.283185307179586/8;
        // Inverse isometric projection of a screen-space unit direction.
        const double sx=std::sin(angle),sy=-std::cos(angle);
        actor.toX=actor.currentX+sy+sx/2;actor.toY=actor.currentY+sy-sx/2;actor.toLevel=0;
        const int first=runtimeProjectileBodyFrame(actor,dragon,"DRAGON",probe,camera,*cache);
        if(first!=(32-facing*4)%32)return fail("retail SHP direction order");
        actor.frameIndex+=7;
        if(runtimeProjectileBodyFrame(actor,dragon,"DRAGON",probe,camera,*cache)!=first)
            return fail("direction changes when only time advances");
        seen.insert(first);
        std::vector<std::uint32_t> tile(120*120,background);
        auto tileContext=probe;tileContext.surfacePixels=tile.data();tileContext.surfaceWidth=tileContext.surfaceHeight=120;
        RuntimeMapCamera tileCamera=camera;tileCamera.tileWidth=120;tileCamera.tileHeight=60;
        const RECT tileBounds{0,0,120,120};
        if(!drawRuntimeAuxiliaryShpFrameInternal(nullptr,tileBounds,{60,60},tileContext,tileCamera,*cache,
            "DRAGON",first,0,0,false,false))return fail("directional sprite missing");
        std::vector<std::uint32_t> enlarged(120*120,background);
        for(int y=0;y<96;++y)for(int x=0;x<96;++x)
            enlarged[(y+12)*120+x+12]=tile[(48+y/4)*120+48+x/4];
        // A white guide points in the intended screen direction; the original
        // tiny sprite is enlarged with exact nearest-neighbor pixel copies.
        for(int distance=43;distance<56;++distance){int x=60+static_cast<int>(std::lround(sx*distance));
            int y=60+static_cast<int>(std::lround(sy*distance));enlarged[y*120+x]=0x00ffffff;}
        for(int y=0;y<120;++y)std::copy_n(enlarged.begin()+y*120,120,directions.begin()+y*960+facing*120);
    }
    if(seen.size()!=8)return fail("eight directions collapsed");
    if(const char* folder=std::getenv("CONCORD_IFV_EFFECTS_DIR")){
        if(!writeRuntimeBgraBmp(std::string(folder)+"/ifv-flight.bmp",width,height,preview)
            || !writeRuntimeBgraBmp(std::string(folder)+"/ifv-directions.bmp",960,120,directions))return fail("preview write");
    }
    std::cout<<"IFV render passed: authored blue trail, directional resource frames="<<frameCount
        <<", pause stable, smoke suppressed, trail expired\n";
    return true;
}
