bool runRuntimeGroundImpactRenderContractCheck(
    const RuntimeMapRenderContext& context,
    const std::vector<RuntimeProjectileRenderActor>& shots,
    const std::vector<RuntimeImpactRenderActor>& impacts,
    std::string& error
) {
    const auto fail = [&](const std::string& text) { error = "ground impact alignment: " + text; return false; };
    if (shots.empty() || shots.size() != impacts.size()) return fail("paired shots and impacts missing");
    constexpr int width = 160, height = 180, columns = 4;
    constexpr std::uint32_t background = 0x004c5850U;
    std::vector<std::uint32_t> pixels(width * height, background), sheet;
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    RuntimeMapRenderContext probe = context;
    probe.vision = nullptr; probe.specialMechanismSnapshots = nullptr;
    probe.surfacePixels = pixels.data(); probe.surfaceWidth = width; probe.surfaceHeight = height;
    const RECT viewport{0, 0, width, height};
    GameWorld world;
    std::set<std::string> pictured;
    int framesChecked = 0;
    for (std::size_t index = 0; index < shots.size(); ++index) {
        auto actor = impacts[index];
        RuntimeMapCamera camera;
        camera.initialized = true;
        const auto projected = projectCellXY(actor.x, actor.y, actor.level, camera);
        camera.worldOffsetX = projected.x - width / 2;
        camera.worldOffsetY = projected.y - 110;
        const POINT hit = runtimeImpactActorScreenPoint(actor, viewport, camera);
        const POINT bullet = runtimeProjectileActorScreenPoint(shots[index], viewport, camera);
        if (hit.x != bullet.x || hit.y != bullet.y || !actor.centerOnAnchor)
            return fail("shell endpoint and impact origin differ: " + actor.weaponId);
        const auto* first = loadRuntimeAuxiliaryShpSpriteFromCache(actor.animId, 0, probe, *cache, false);
        if (first == nullptr || first->frameCount <= 0) return fail("original animation missing: " + actor.animId);
        const int frameCount = first->frameCount;
        const bool picture = pictured.insert(actor.animId).second;
        const std::size_t row = pictured.size() - 1;
        if (picture) sheet.resize(pictured.size() * width * columns * height, background);
        for (int frame = 0; frame < frameCount; ++frame) {
            actor.frameIndex = frame;
            std::fill(pixels.begin(), pixels.end(), background);
            const bool expectedDrawn = drawRuntimeClassifiedAuxiliaryEffect(nullptr, viewport, hit, probe, camera,
                *cache, actor.animId, frame, 0, 0, false, RuntimeAuxiliaryEffectKind::Impact, false, true);
            if (!expectedDrawn) {
                const auto* source = loadRuntimeAuxiliaryShpSpriteFromCache(actor.animId, frame, probe, *cache, false);
                if (source == nullptr || std::any_of(source->image.pixels.begin(), source->image.pixels.end(),
                        [](unsigned char pixel) { return pixel != 0; })) return fail("original frame missing: " + actor.animId);
            }
            const auto expected = pixels;
            std::fill(pixels.begin(), pixels.end(), background);
            RuntimeMapRenderStats stats;
            drawRuntimeImpactActor(nullptr, viewport, world, probe, camera, *cache, actor, stats);
            if (stats.impactActorDrawn != (expectedDrawn ? 1 : 0) || pixels != expected)
                return fail("flash/flame frame moved off the hit point: " + actor.animId
                    + " frame=" + std::to_string(frame));
            ++framesChecked;
            if (picture) {
                const int selected[] = {0, std::min(5, frameCount - 1), std::min(11, frameCount - 1)};
                for (int phase = 0; phase < 3; ++phase) if (frame == selected[phase]) {
                    // The white cross is the unchanged shell hit coordinate.
                    for (int d = -2; d <= 2; ++d) {
                        pixels[hit.y * width + hit.x + d] = 0x00ffffffU;
                        pixels[(hit.y + d) * width + hit.x] = 0x00ffffffU;
                    }
                    for (int y = 0; y < height; ++y)
                        std::copy_n(pixels.begin() + y * width, width,
                            sheet.begin() + (row * height + y) * width * columns + (phase + 1) * width);
                }
            }
        }
        if (picture) {
            std::fill(pixels.begin(), pixels.end(), background);
            std::vector<RuntimeProjectileRenderActor> terminal{shots[index]};
            terminal[0].remainingTicks = 1;
            terminal[0].trailerCadenceManaged = true;
            probe.projectileActors = &terminal;
            RuntimeMapRenderStats stats;
            drawRuntimeProjectileActors(nullptr, viewport, probe, camera, *cache, stats);
            probe.projectileActors = nullptr;
            if (stats.projectileActorTravelDrawn != 1) return fail("terminal shell pixels missing");
            for (int y = 0; y < height; ++y)
                std::copy_n(pixels.begin() + y * width, width,
                    sheet.begin() + (row * height + y) * width * columns);
        }
    }
    if (const char* path = std::getenv("CONCORD_GROUND_IMPACT_BMP"))
        if (*path && !writeRuntimeBgraBmp(path, width * columns,
                height * static_cast<int>(pictured.size()), sheet)) return fail("preview write failed");
    std::cout << "Ground impact pixels: weapons=" << shots.size() << " animations=" << pictured.size()
        << " frames=" << framesChecked << " shell/flash/flame=one-world-point\n";
    return true;
}
