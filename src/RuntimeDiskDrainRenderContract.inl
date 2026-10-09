bool runRuntimeDiskDrainRenderContractCheck(const RuntimeMapRenderContext& context, const GameWorld& world,
    const RuntimeImpactRenderActor& initial, const std::string& path, std::string& error) {
    const auto fail = [&](const std::string& message) { error = "Disk drain render: " + message; return false; };
    const auto* source = findRuntimeSpecialRenderObject(world, initial.targetObjectId);
    if (!source || initial.animId != "DISKRAY") return fail("disc/animation snapshot missing");
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    auto probe = context; probe.vision = nullptr; probe.fixedTickHz = 30;
    auto* sprite = loadRuntimeAuxiliaryShpSpriteFromCache("DISKRAY", 0, probe, *cache, false);
    const auto* art = probe.art ? probe.art->findArtType("DISKRAY") : nullptr;
    if (!sprite || !sprite->loaded || sprite->frameCount < 4 || !art || art->animationLoopCount != -1
        || art->animationLoopStart != 0 || art->animationLoopEnd != 4 || art->startSound != "FloatingDiscStealLoop")
        return fail("original SHP/loop/sound definition missing");
    constexpr int width = 240, height = 240, columns = 4;
    constexpr std::uint32_t background = 0x002c3438U;
    std::vector<std::uint32_t> pixels(width * height, background), sheet(width * height * columns);
    probe.surfacePixels = pixels.data(); probe.surfaceWidth = width; probe.surfaceHeight = height;
    RuntimeMapCamera camera; camera.initialized = true;
    const RECT viewport{0,0,width,height};
    const auto origin = runtimeObjectDrawPoint(*source,{},camera);
    camera.worldOffsetX = int(std::lround(origin.x)) - width / 2;
    camera.worldOffsetY = int(std::lround(origin.y)) - height / 3
        - runtimeAircraftSpriteLiftPixels(*source,runtimeObjectLooksAircraftForRender(*source,probe));
    std::set<std::uint64_t> hashes;
    const int interval = runtimeAnimationFrameDelayTicks(art, probe.fixedTickHz);
    for (int column = 0; column < columns; ++column) {
        std::fill(pixels.begin(),pixels.end(),background);
        const auto center = runtimeObjectBodyAnchorForRender(*source,viewport,probe,camera);
        if (!tryDrawVoxelSprite(nullptr,viewport,*source,center,probe,camera,*cache)) return fail("original disc VXL missing");
        const auto before = pixels;
        auto actor = initial; actor.totalTicks = column * interval + 1; actor.remainingTicks = 1;
        if (runtimeImpactActorFrameIndex(actor,"DISKRAY",probe,*cache) != column) return fail("loop frame selection");
        RuntimeMapRenderStats stats;
        drawRuntimeImpactActor(nullptr,viewport,world,probe,camera,*cache,actor,stats);
        if (stats.impactActorDrawn != 1 || pixels == before) return fail("original drain frame did not draw");
        const auto expected = pixels;
        std::uint64_t hash = 1469598103934665603ULL;
        for (auto pixel : pixels) hash = (hash ^ pixel) * 1099511628211ULL;
        hashes.insert(hash);
        for (int y = 0; y < height; ++y)
            std::copy_n(pixels.begin()+y*width,width,sheet.begin()+y*width*columns+column*width);
        pixels = before; probe.surfacePixels = pixels.data(); probe.fixedTickHz = 60;
        actor.totalTicks = column * interval * 2 + 1;
        RuntimeMapRenderStats doubled;
        drawRuntimeImpactActor(nullptr,viewport,world,probe,camera,*cache,actor,doubled);
        if (pixels != expected) return fail("30/60 Hz frames differ at equal elapsed time");
        probe.fixedTickHz = 30;
    }
    auto wrapped = initial; wrapped.totalTicks = interval * columns + 1; wrapped.remainingTicks = 1;
    if (hashes.size() < 3 || runtimeImpactActorFrameIndex(wrapped,"DISKRAY",probe,*cache) != 0)
        return fail("drain animation froze or did not loop");
    if (!writeRuntimeBgraBmp(path,width*columns,height,sheet)) return fail("preview write failed");
    return true;
}
