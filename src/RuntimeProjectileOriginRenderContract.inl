bool runRuntimeIfvTrajectoryRenderCheck(const RuntimeMapRenderContext& context,
    const std::vector<std::vector<RuntimeProjectileRenderActor>>& phases,
    const std::string& outputPath, std::string& error) {
    auto cache=std::make_unique<RuntimeMapRenderCache>();
    std::vector<std::uint32_t> pixels(400*220,0x00305030U);
    auto probe=context;probe.surfacePixels=pixels.data();probe.surfaceWidth=400;probe.surfaceHeight=220;
    RuntimeMapCamera camera;camera.initialized=true;camera.worldOffsetX=-65;camera.worldOffsetY=64*30-145;
    const RECT viewport{0,0,400,220};int tick=0;
    for(const auto& actors:phases){
        probe.animationTick=++tick;probe.projectileActors=&actors;RuntimeMapRenderStats stats;
        drawRuntimeProjectileActors(nullptr,viewport,probe,camera,*cache,stats);
        if(stats.projectileActorTravelDrawn!=static_cast<int>(actors.size())||cache->projectileLineTrails.empty()){
            error="native DRAGON body/trail draw";return false;
        }
        for(const auto& actor:actors){
            const auto& head=cache->projectileLineTrails.at(actor.actorId).head;
            if(std::abs(head.x-actor.currentX)>1e-9||std::abs(head.z-actor.currentLevel)>1e-9){
                error="trail head separated from actual trajectory";return false;
            }
        }
    }
    if(!writeRuntimeBgraBmp(outputPath,400,220,pixels)){error="native trajectory image";return false;}
    return true;
}

bool runRuntimeTankArcRenderContractCheck(const RuntimeMapRenderContext& context,
    const std::vector<RuntimeProjectileRenderActor>& shots, std::string& error) {
    const auto fail=[&](const std::string& why){error="tank arc: "+why;return false;};
    if (shots.empty()) return fail("no native flight frames");
    auto cache=std::make_unique<RuntimeMapRenderCache>();
    constexpr int width=256,height=192;
    constexpr std::uint32_t background=0x00c0d0c0U;
    std::vector<std::uint32_t> pixels(width*height,background);
    auto probe=context; probe.vision=nullptr; probe.surfacePixels=pixels.data();
    probe.surfaceWidth=width; probe.surfaceHeight=height;
    const RECT viewport{0,0,width,height};
    std::vector<RuntimeProjectileRenderActor> one(1); probe.projectileActors=&one;
    int checks=0,draws=0,maxLift=0;
    for (int tileWidth:{30,60,120}) for (const auto& shot:shots) {
        RuntimeMapCamera camera; camera.initialized=true; camera.tileWidth=tileWidth; camera.tileHeight=tileWidth/2;
        camera.worldOffsetX=static_cast<int>(std::llround((shot.currentX-shot.currentY)*tileWidth/2.0))-width/2;
        camera.worldOffsetY=static_cast<int>(std::llround((shot.currentX+shot.currentY-shot.currentLevel)*camera.tileHeight/2.0))-height/2;
        const double weight=1.0-shot.progress;
        const POINT expected{
            static_cast<LONG>(std::llround((shot.currentX-shot.currentY)*tileWidth/2.0)-camera.worldOffsetX
                +std::lround(shot.sourceScreenOffsetX*weight)),
            static_cast<LONG>(std::llround((shot.currentX+shot.currentY-shot.currentLevel)*camera.tileHeight/2.0)-camera.worldOffsetY
                +std::lround(shot.sourceScreenOffsetY*weight))};
        const auto actual=runtimeProjectileActorScreenPoint(shot,viewport,camera);
        if (actual.x!=expected.x || actual.y!=expected.y) return fail("sprite separated from simulation position: "+shot.weaponId);
        if (tileWidth==60 && shot.arcing && !shot.high && !shot.veryHigh) {
            auto baseline=shot; baseline.currentLevel=shot.fromLevel+(shot.toLevel-shot.fromLevel)*shot.progress;
            const int lift=runtimeProjectileActorScreenPoint(baseline,viewport,camera).y-actual.y;
            if (lift<0 || lift>6) return fail("ordinary tank shell gained extra screen-space height");
            maxLift=std::max(maxLift,lift);
        }
        if (shot.remainingTicks>0) {
            std::fill(pixels.begin(),pixels.end(),background); one[0]=shot;
            RuntimeMapRenderStats stats; drawRuntimeProjectileActors(nullptr,viewport,probe,camera,*cache,stats);
            if (stats.projectileActorTravelDrawn!=1) return fail("native shell did not draw: "+shot.weaponId);
            const auto positioned=pixels;
            std::fill(pixels.begin(),pixels.end(),background);
            one[0].arcing=one[0].high=one[0].veryHigh=false;
            RuntimeMapRenderStats neutral; drawRuntimeProjectileActors(nullptr,viewport,probe,camera,*cache,neutral);
            if (pixels!=positioned) return fail("presentation flags bent an already-positioned shell twice");
            ++draws;
        }
        ++checks;
    }
    // Keep one actual sequence on a fixed camera to inspect the shallow arc.
    std::fill(pixels.begin(),pixels.end(),background);
    RuntimeMapCamera camera; camera.initialized=true;
    camera.worldOffsetX=static_cast<int>(std::llround((shots.front().fromX-shots.front().fromY)*30.0))-48;
    camera.worldOffsetY=static_cast<int>(std::llround((shots.front().fromX+shots.front().fromY-shots.front().fromLevel)*15.0))-80;
    for (const auto& shot:shots) {
        if (shot.actorId!=shots.front().actorId) break;
        one[0]=shot; RuntimeMapRenderStats stats;
        drawRuntimeProjectileActors(nullptr,viewport,probe,camera,*cache,stats);
    }
    if (const char* root=std::getenv("CONCORD_TANK_ARC_OUTPUT")) {
        if (!writeRuntimeBgraBmp(std::string(root)+"/tank-arc-"+std::to_string(context.fixedTickHz)+".bmp",width,height,pixels))
            return fail("trajectory image write");
    }
    std::cout << "Tank arc passed: hz=" << context.fixedTickHz << " frames=" << shots.size()
        << " projections=" << checks << " renderedPairs=" << draws << " standardScaleMaxLift=" << maxLift << "px\n";
    return true;
}

bool runRuntimeProjectileOriginRenderContractCheck(
    const RuntimeMapRenderContext& context, const GameWorld& world,
    const std::vector<RuntimeProjectileRenderActor>& actors, std::string& error
) {
    const auto fail = [&](const std::string& message) { error = "projectile origin: " + message; return false; };
    constexpr int width = 160, height = 120, columns = 8;
    constexpr std::uint32_t background = 0x00242c30U;
    std::vector<std::uint32_t> pixels(width * height, background);
    std::vector<std::uint32_t> sheet(width * height * actors.size(), background);
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    auto probe = context;
    probe.vision = nullptr;
    probe.surfacePixels = pixels.data(); probe.surfaceWidth = width; probe.surfaceHeight = height;
    RuntimeMapCamera camera;
    camera.initialized = true;
    const RECT viewport{0, 0, width, height};
    if (actors.size() != world.getObjects().size()) return fail("pose count mismatch");
    for (std::size_t i = 0; i < actors.size(); ++i) {
        std::fill(pixels.begin(), pixels.end(), background);
        const auto& object = world.getObjects()[i];
        const auto origin = runtimeObjectDrawPoint(object, {}, camera);
        camera.worldOffsetX = static_cast<int>(std::lround(origin.x)) - width / 2;
        camera.worldOffsetY = static_cast<int>(std::lround(origin.y)) - 82;
        const POINT center = screenPoint(origin, viewport, camera);
        RECT body{};
        const bool drawn = object.kind == WorldObjectKind::Infantry
            ? tryDrawObjectShpSprite(nullptr, viewport, object, center, probe, camera, *cache, nullptr, &body)
            : tryDrawVoxelSprite(nullptr, viewport, object, center, probe, camera, *cache);
        if (!drawn) return fail(object.type + " body unavailable");
        const POINT muzzle = runtimeProjectileActorScreenPoint(actors[i], viewport, camera);
        if (object.kind == WorldObjectKind::Infantry
            && (muzzle.y < body.top - 4 || muzzle.y >= body.bottom - 2))
            return fail(object.type + " muzzle missed firing body at facing " + std::to_string(object.facing));
        std::vector<RuntimeProjectileRenderActor> shot{actors[i]};
        probe.projectileActors = &shot;
        RuntimeMapRenderStats stats;
        drawRuntimeProjectileActors(nullptr, viewport, probe, camera, *cache, stats);
        // White cross marks the exact launch point, also for invisible bullets.
        for (int d = -2; d <= 2; ++d) {
            if (muzzle.x + d >= 0 && muzzle.x + d < width && muzzle.y >= 0 && muzzle.y < height)
                pixels[muzzle.y * width + muzzle.x + d] = 0x00ffffffU;
            if (muzzle.y + d >= 0 && muzzle.y + d < height && muzzle.x >= 0 && muzzle.x < width)
                pixels[(muzzle.y + d) * width + muzzle.x] = 0x00ffffffU;
        }
        const int col = static_cast<int>(i % columns), row = static_cast<int>(i / columns);
        for (int y = 0; y < height; ++y)
            std::copy_n(pixels.begin() + y * width, width,
                sheet.begin() + (row * height + y) * width * columns + col * width);
    }
    if (const char* path = std::getenv("CONCORD_MUZZLE_BMP")) {
        if (*path != '\0' && !writeRuntimeBgraBmp(path, width * columns,
                height * static_cast<int>(actors.size() / columns), sheet)) return fail("preview write failed");
    }

    camera.worldOffsetX = -width / 2; camera.worldOffsetY = -height / 2;
    std::vector<RuntimeProjectileRenderActor> shot(1);
    auto& actor = shot.front();
    actor.owner = "Russians"; actor.projectileId = "Cannon"; actor.projectileImageId = "120MM";
    actor.totalTicks = actor.remainingTicks = 10; actor.hasCurrentPosition = true;
    actor.trailerCadenceManaged = true;
    probe.projectileActors = &shot;
    std::fill(pixels.begin(), pixels.end(), background);
    RuntimeMapRenderStats shellStats;
    drawRuntimeProjectileActors(nullptr, viewport, probe, camera, *cache, shellStats);
    int minX = width, minY = height, maxX = -1, maxY = -1;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        if (pixels[y * width + x] != background) {
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);
        }
    if (shellStats.projectileActorTravelDrawn != 1 || maxX < 0
        || std::abs((minX + maxX) / 2 - width / 2) > 3
        || std::abs((minY + maxY) / 2 - height / 2) > 3)
        return fail("120MM pixels are displaced from the flight coordinate");

    const auto* cannon=probe.rules?probe.rules->findProjectileType("Cannon"):nullptr;
    const auto* nativePalette=runtimeNamedPaletteForRender(probe,*cache,"palette.pal");
    if(!cannon||!nativePalette)return fail("native cannon/default palette unavailable");
    std::string source;
    if(runtimePaletteForProjectileSprite(probe,*cache,cannon,"120MM",&source)!=nativePalette
        ||source!="ProjectileDefault")return fail("ordinary cannon did not use PALETTE.PAL");
    auto explicitPalette=*cannon;explicitPalette.animPalette=true;
    if(runtimePaletteForProjectileSprite(probe,*cache,&explicitPalette,"120MM")
            !=runtimeAnimPaletteForRender(probe,*cache))return fail("AnimPalette override changed");
    explicitPalette.animPalette=false;explicitPalette.paletteIsSet=true;explicitPalette.palette="anim.pal";
    if(runtimePaletteForProjectileSprite(probe,*cache,&explicitPalette,"120MM")
            !=runtimeNamedPaletteForRender(probe,*cache,"anim.pal"))return fail("explicit palette override changed");
    if(runtimePaletteForProjectileSprite(probe,*cache,nullptr,"DBRIS1SM",&source)
            !=runtimeAnimPaletteForRender(probe,*cache)||source!="AnimPalette")return fail("flying debris lost animation palette");
    const auto savedCamera=camera;const auto* savedColors=probe.ownerRemapColors;
    camera.tileWidth=480;camera.tileHeight=240;
    std::unordered_map<std::string,COLORREF> shellColors{{"russians",RGB(255,0,0)}};
    probe.ownerRemapColors=&shellColors;
    std::fill(pixels.begin(),pixels.end(),background);
    drawRuntimeProjectileActors(nullptr,viewport,probe,camera,*cache,shellStats);
    const auto nativePixels=pixels;
    int grayPixels=0,coloredPixels=0;
    for(auto p:pixels)if(p!=background) {
        const int r=(p>>16)&255,g=(p>>8)&255,b=p&255;
        if(std::max({r,g,b})-std::min({r,g,b})<=8)++grayPixels;else ++coloredPixels;
    }
    if(grayPixels==0||coloredPixels!=0)return fail("native shell is not gray/black");
    shellColors["russians"]=RGB(0,255,80);
    std::fill(pixels.begin(),pixels.end(),background);
    drawRuntimeProjectileActors(nullptr,viewport,probe,camera,*cache,shellStats);
    if(pixels!=nativePixels)return fail("ordinary cannon inherited its owner's color");
    std::fill(pixels.begin(),pixels.end(),background);
    const auto* oldPalette=runtimeAnimPaletteForRender(probe,*cache);
    if(!oldPalette||!drawRuntimeAuxiliaryShpSpriteWithPalette(nullptr,viewport,POINT{width/2,height/2},
            probe,camera,*cache,"120MM",0,*oldPalette,0,0,false,false,RGB(255,255,255),255,true)
        ||pixels==nativePixels)return fail("old animation-palette control did not differ");
    if(const char* root=std::getenv("CONCORD_PRISM_PRESENTATION_OUTPUT")) {
        std::vector<std::uint32_t> comparison(width*2*height,background);
        for(int y=0;y<height;++y) {
            std::copy_n(nativePixels.data()+y*width,width,comparison.data()+y*width*2);
            std::copy_n(pixels.data()+y*width,width,comparison.data()+y*width*2+width);
        }
        if(!writeRuntimeBgraBmp(std::string(root)+"/cannon-palette-native-vs-old.bmp",width*2,height,comparison))
            return fail("cannon color comparison write");
    }
    camera=savedCamera;probe.ownerRemapColors=savedColors;
    std::cout<<"Cannon palette: native gray/black pixels="<<grayPixels
        <<"; production render, owner-color independence, explicit overrides and debris passed\n";

    std::unordered_map<std::string, COLORREF> colors{{"russians", RGB(20, 80, 240)}};
    probe.ownerRemapColors = &colors;
    GameWorld missileWorld;
    WorldObjectInstance missile;
    missile.instanceId = 1; missile.alive = true; missile.owner = "Russians";
    missileWorld.accessObjects().push_back(missile);
    for (const std::string image : {"V3ROCKET", "DMISL", "BSUBMISL"}) {
        actor.projectileId.clear(); actor.projectileImageId = image; actor.facing = 64;
        actor.renderIntent = "spawnedMissile";
        // Both representations are in flight, including their attached flame.
        actor.remainingTicks = actor.totalTicks - 1;
        SpecialMechanismRenderSnapshot snapshot;
        snapshot.kind = "spawnedMissile"; snapshot.objectId = 1;
        snapshot.owner = actor.owner; snapshot.resourceId = image;
        snapshot.phase = "Flight"; snapshot.missileFacing = actor.facing;
        std::vector<SpecialMechanismRenderSnapshot> snapshots{snapshot};
        probe.specialMechanismSnapshots = &snapshots;
        colors["russians"] = RGB(20, 80, 240);
        std::fill(pixels.begin(), pixels.end(), background);
        RuntimeMapRenderStats ordinaryStats;
        drawRuntimeProjectileActors(nullptr, viewport, probe, camera, *cache, ordinaryStats);
        const auto ordinary = pixels;
        std::fill(pixels.begin(), pixels.end(), background);
        RuntimeMapRenderStats spawnedStats;
        drawRuntimeSpecialMechanismOverlays(nullptr, viewport, missileWorld, probe, camera, *cache, spawnedStats);
        if (ordinaryStats.projectileActorVoxelDrawn != 1 || spawnedStats.specialMechanismRenderActors != 1
            || ordinaryStats.missileExhaustHeadDrawn != 1 || spawnedStats.missileExhaustHeadDrawn != 1
            || pixels != ordinary) return fail(image + " projectile and spawned render origins disagree");
        colors["russians"] = RGB(240, 30, 120);
        std::fill(pixels.begin(), pixels.end(), background);
        drawRuntimeSpecialMechanismOverlays(nullptr, viewport, missileWorld, probe, camera, *cache, spawnedStats);
        int changed = 0;
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            if ((pixels[i] == background) != (ordinary[i] == background))
                return fail(image + " player color changed missile geometry");
            changed += pixels[i] != ordinary[i] ? 1 : 0;
        }
        if (changed < 3) return fail(image + " ignored the player's custom color");
    }
    return true;
}
