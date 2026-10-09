bool runRuntimeGattlingRenderContractCheck(const RuntimeMapRenderContext& context,
    const WorldObjectInstance& source, const std::vector<RuntimeImpactRenderActor>& muzzles,
    std::string& error) {
    const auto fail = [&](const std::string& reason) { error = "Gattling pixels: " + reason; return false; };
    constexpr int width = 180, height = 160, columns = 5;
    constexpr std::uint32_t background = 0x005c6b49;
    std::vector<std::uint32_t> pixels(width * height, background), sheet(width * columns * height * 8, background);
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    auto probe = context;
    RulesDatabase localRules = *context.rules; probe.rules = &localRules;
    std::unordered_map<std::string, COLORREF> colors{{"yuricountry", RGB(55, 92, 208)}};
    probe.ownerRemapColors = &colors;
    probe.vision = nullptr; probe.specialMechanismSnapshots = nullptr;
    probe.surfacePixels = pixels.data(); probe.surfaceWidth = width; probe.surfaceHeight = height;
    RuntimeMapCamera camera; camera.initialized = true;
    const auto projected = projectCellXY(source.x, source.y, source.level, camera);
    camera.worldOffsetX = projected.x - width / 2; camera.worldOffsetY = projected.y - 120;
    const RECT viewport{0, 0, width, height};
    GameWorld world;
    auto object = source; object.mission = "Guard";
    auto* building = const_cast<BuildingType*>(localRules.findBuildingType(source.type));
    for (int row = 0; row < 8; ++row) {
        object.facing = object.turretFacing = row * 32;
        world.accessObjects() = {object};
        const auto center = runtimeObjectBodyAnchorForRender(object, viewport, probe, camera);
        const auto drawBody = [&]() {
            if (object.kind == WorldObjectKind::Unit)
                return tryDrawVoxelSprite(nullptr, viewport, object, center, probe, camera, *cache);
            if (!tryDrawObjectShpSprite(nullptr, viewport, object, center, probe, camera, *cache)) return false;
            return tryDrawRuntimeStructureTurretAnim(nullptr, viewport, object, center, 1, 1, probe, camera, *cache);
        };
        std::vector<std::uint32_t> corrected;
        for (int column = 0; column < columns; ++column) {
            std::fill(pixels.begin(), pixels.end(), background);
            // Reproduce the former low mount through a fixture-only Y offset.
            if (building && column == 0) building->turretAnimY += camera.tileHeight / 2;
            const bool bodyDrawn = drawBody();
            if (building && column == 0) building->turretAnimY -= camera.tileHeight / 2;
            if (!bodyDrawn) return fail("original body/turret missing");
            if (column == 1) corrected = pixels;
            if (column >= 2) for (int barrel = 0; barrel < 2; ++barrel) {
                auto flash = muzzles[column - 2 + barrel * 3];
                const auto* weapon = localRules.findWeaponType(flash.weaponId);
                if (!weapon) return fail("stage weapon missing");
                // Keep the preview at its captured firing angle; direction
                // rows separately validate the original turret registration.
                if (row == source.turretFacing / 32) {
                    const auto beforeFlash = pixels;
                    std::vector<std::uint32_t> peak;
                    int peakPixels = 0, drawnFrames = 0;
                    // This preview checks the resource's visible peak. Live
                    // native timing is checked by the continuity smoke. An
                    // explicit captured frame must not freeze this preview.
                    RuntimeShpFrameImage firstFrame;
                    std::string resolvedImage, sourcePath, loadError;
                    int frameCount = 0;
                    const auto* animation = probe.art->findArtType(flash.animId);
                    const bool mainFramesOnly = animation && animation->shadowSpecified && animation->shadow;
                    if (!loadRuntimeAuxiliaryShpFrameImage(probe, *cache, flash.animId, 0,
                        firstFrame, resolvedImage, sourcePath, loadError, mainFramesOnly, &frameCount)
                        || frameCount <= 0) return fail("original stage animation frames missing: " + flash.animId);
                    for (int frame = 0; frame < frameCount; ++frame) {
                        std::copy(beforeFlash.begin(), beforeFlash.end(), pixels.begin());
                        flash.frameIndex = frame;
                        RuntimeMapRenderStats stats;
                        drawRuntimeImpactActor(nullptr, viewport, world, probe, camera, *cache, flash, stats);
                        drawnFrames += stats.impactActorDrawn;
                        int changed = 0;
                        for (std::size_t i = 0; i < pixels.size(); ++i)
                            changed += pixels[i] != beforeFlash[i];
                        if (changed > peakPixels) { peakPixels = changed; peak = pixels; }
                    }
                    if (peakPixels == 0) {
                        const auto point = runtimeImpactActorScreenPoint(flash, viewport, camera);
                        if (const char* folder = std::getenv("CONCORD_GATTLING_DIR"))
                            if (*folder) writeRuntimeBgraBmp(std::string(folder) + "/muzzle-failed.bmp",
                                width, height, pixels);
                        return fail(source.type + " " + flash.animId + " invisible; frames="
                            + std::to_string(drawnFrames) + " point=" + std::to_string(point.x)
                            + "," + std::to_string(point.y) + " frame=" + std::to_string(flash.frameIndex));
                    }
                    std::copy(peak.begin(), peak.end(), pixels.begin());
                    std::cout << "Gattling pixels " << source.type << ' ' << flash.animId
                        << " peak=" << peakPixels << " drawnFrames=" << drawnFrames << '\n';
                }
            }
            for (int y = 0; y < height; ++y)
                std::copy_n(pixels.begin() + y * width, width,
                    sheet.begin() + (row * height + y) * width * columns + column * width);
        }
    }
    if (const char* folder = std::getenv("CONCORD_GATTLING_DIR")) {
        if (*folder && !writeRuntimeBgraBmp(std::string(folder) + "/" + source.type + "-stages.bmp",
                width * columns, height * 8, sheet)) return fail("preview write failed");
    }
    return true;
}
