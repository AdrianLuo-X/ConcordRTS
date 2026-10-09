bool runRuntimeBruteDeathRenderContractCheck(const RuntimeMapRenderContext& context,
    std::string& error) {
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    constexpr int size = 120, columns = 6;
    constexpr std::uint32_t background = 0x005c6b49;
    std::vector<std::uint32_t> pixels(size * size, background), sheet(size * size * columns * 2, background);
    auto probe = context;
    probe.vision = nullptr; probe.surfacePixels = pixels.data();
    probe.surfaceWidth = probe.surfaceHeight = size;
    RuntimeMapCamera camera; camera.initialized = true;
    camera.worldOffsetX = -size / 2; camera.worldOffsetY = -size / 2;
    const RECT viewport{0, 0, size, size};
    GameWorld world;
    for (int row = 0; row < 2; ++row) {
        const std::string id = row == 0 ? "GENDEATH" : "BRUTDIE";
        const int frames = runtimeAuxiliaryShpFrameCount(probe, *cache, id);
        if (frames <= 5) { error = "Brute death original frames missing: " + id; return false; }
        int visibleFrames = 0;
        for (int frame = 0; frame < frames; ++frame) {
            std::fill(pixels.begin(), pixels.end(), background);
            RuntimeImpactRenderActor actor;
            actor.animId = id; actor.renderIntent = "unitDestruction";
            actor.lifecycleState = "UnitDestruction"; actor.centerOnAnchor = true;
            actor.frameIndex = frame; actor.totalTicks = frames; actor.remainingTicks = frames - frame;
            RuntimeMapRenderStats stats;
            drawRuntimeImpactActor(nullptr, viewport, world, probe, camera, *cache, actor, stats);
            visibleFrames += std::any_of(pixels.begin(), pixels.end(),
                [](auto pixel) { return pixel != background; });
            for (int column = 0; column < columns; ++column) if (frame == column * (frames - 1) / (columns - 1))
                for (int y = 0; y < size; ++y) std::copy_n(pixels.begin() + y * size, size,
                    sheet.begin() + (row * size + y) * size * columns + column * size);
        }
        if (visibleFrames < frames / 2) { error = "Brute death native animation invisible: " + id; return false; }
        std::cout << "Brute death pixels " << id << " frames=" << frames << " visible=" << visibleFrames << '\n';
    }
    if (const char* folder = std::getenv("CONCORD_QUESTION12_VISUAL_DIR"))
        if (*folder && !writeRuntimeBgraBmp(std::string(folder) + "/brute-death-before-after.bmp",
            size * columns, size * 2, sheet)) { error = "Brute death preview write failed"; return false; }
    return true;
}
