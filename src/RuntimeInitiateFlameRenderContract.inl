bool runRuntimeInitiateFlameRenderContractCheck(
    const RuntimeMapRenderContext& context, const GameWorld& world,
    const std::vector<RuntimeImpactRenderActor>& phases,
    const std::string& previewPath, std::string& error
) {
    const auto fail = [&](const std::string& reason) {
        error = "Initiate flame pixels: " + reason;
        return false;
    };
    if (phases.size() != 30) return fail("complete live flame sequence missing");
    constexpr int width = 160, height = 180, columns = 5;
    constexpr std::uint32_t background = 0x004c5850U;
    std::vector<std::uint32_t> pixels(width * height, background);
    std::vector<std::uint32_t> sheet(width * columns * height, background);
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    auto probe = context;
    probe.vision = nullptr; probe.specialMechanismSnapshots = nullptr;
    probe.surfacePixels = pixels.data(); probe.surfaceWidth = width; probe.surfaceHeight = height;
    RuntimeMapCamera camera; camera.initialized = true;
    const auto point = projectCellXY(phases.front().x, phases.front().y, phases.front().level, camera);
    camera.worldOffsetX = point.x - width / 2;
    camera.worldOffsetY = point.y - 135;
    const RECT viewport{0, 0, width, height};
    const auto* target = findRuntimeSpecialRenderObject(world, phases.front().targetObjectId);
    if (target) {
        const auto center = runtimeObjectBodyAnchorForRender(*target, viewport, probe, camera);
        const bool drawn = target->kind == WorldObjectKind::Unit
            ? tryDrawVoxelSprite(nullptr, viewport, *target, center, probe, camera, *cache)
            : tryDrawObjectShpSprite(nullptr, viewport, *target, center, probe, camera, *cache);
        if (!drawn) return fail("original target missing: " + target->type);
    }
    const auto body = pixels;
    const auto copyColumn = [&](int column) {
        for (int y = 0; y < height; ++y)
            std::copy_n(pixels.begin() + y * width, width,
                sheet.begin() + y * width * columns + column * width);
    };
    copyColumn(0);
    int earlyPeak = 0, fullPeak = 0, warmFrames = 0;
    std::set<int> frames;
    for (const auto& actor : phases) {
        pixels = body;
        // Repeated assignments keep the buffer size/capacity, but refresh the pointer explicitly.
        probe.surfacePixels = pixels.data();
        RuntimeMapRenderStats stats;
        drawRuntimeImpactActor(nullptr, viewport, world, probe, camera, *cache, actor, stats);
        if (stats.impactActorDrawn != 1 || stats.impactAnimMissing != 0)
            return fail("native FIRE01 frame missing: " + std::to_string(actor.frameIndex));
        int warm = 0;
        for (std::size_t i = 0; i < pixels.size(); ++i) if (pixels[i] != body[i]) {
            const unsigned red = (pixels[i] >> 16U) & 255U;
            const unsigned green = (pixels[i] >> 8U) & 255U;
            const unsigned blue = pixels[i] & 255U;
            if (red > 100 && red > blue + 40 && green > blue) ++warm;
        }
        if (warm > 0) ++warmFrames;
        if (actor.frameIndex < 4) earlyPeak = std::max(earlyPeak, warm);
        fullPeak = std::max(fullPeak, warm);
        frames.insert(actor.frameIndex);
        // Before, old last visible spark, then the full flame rising and fading.
        const int selected[] = {3, 10, 20, 29};
        for (int column = 0; column < 4; ++column)
            if (actor.frameIndex == selected[column]) copyColumn(column + 1);
    }
    if (frames.size() != 30 || warmFrames < 24 || fullPeak < 80 || fullPeak < earlyPeak * 3)
        return fail("flame never grew visibly beyond the clipped sparks: early="
            + std::to_string(earlyPeak) + " peak=" + std::to_string(fullPeak)
            + " warmFrames=" + std::to_string(warmFrames));
    if (!previewPath.empty()
        && !writeRuntimeBgraBmp(previewPath, width * columns, height, sheet)) return fail("preview write failed");
    if (!previewPath.empty()) std::cout << "Initiate flame pixels: "
        << (target ? target->type : "ground") << " early=" << earlyPeak << " full=" << fullPeak
        << " original animation frames=" << frames.size() << '\n';
    return true;
}
