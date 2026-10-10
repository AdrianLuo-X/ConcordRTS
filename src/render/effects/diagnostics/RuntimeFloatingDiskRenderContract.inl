bool runRuntimeFloatingDiskRenderContractCheck(const RuntimeMapRenderContext& context,
    const std::vector<WorldObjectInstance>& poses,
    const std::vector<RuntimeProjectileRenderActor>& actors, std::string& error) {
    const auto fail = [&](const std::string& text) { error = "floating disk render: " + text; return false; };
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    auto probe = context;
    probe.fixedTickHz = 30;
    probe.vision = nullptr;
    {
        constexpr int w = 128, h = 64;
        std::vector<std::uint32_t> background(w * h);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
            background[y * w + x] = packRuntimeDibColor(x < w / 2 ? RGB(20, 40, 60) : RGB(100, 120, 140));
        auto blended = background;
        auto surface = probe; surface.surfacePixels = blended.data();
        surface.surfaceWidth = w; surface.surfaceHeight = h;
        RuntimeMapCamera camera; camera.initialized = true;
        // screenPoint also adds the viewport's four-pixel inset.
        camera.worldOffsetX = -16; camera.worldOffsetY = -28;
        const RECT viewport{4, 4, 124, 60};
        RuntimeProjectileRenderActor beam;
        beam.weaponId = "DiskLaser"; beam.renderIntent = "diskLaserBeam";
        beam.toX = 4.0 / 3.0; beam.toY = -4.0 / 3.0;
        beam.totalTicks = beam.remainingTicks = 30;
        drawRuntimeDiskLaser(nullptr, viewport, beam, surface, camera);
        const auto dark = colorRefFromRuntimeDibColor(blended[32 * w + 40]);
        const auto light = colorRefFromRuntimeDibColor(blended[32 * w + 80]);
        if (GetGValue(dark) <= 40 || GetGValue(light) <= GetGValue(dark) + 5
            || blended[34 * w + 40] == background[34 * w + 40]
            || blended[35 * w + 40] != background[35 * w + 40])
            return fail("beam lost translucent centre or soft edge over different backgrounds");
        const auto bright = blended;
        std::copy(background.begin(), background.end(), blended.begin());
        beam.remainingTicks = 15;
        drawRuntimeDiskLaser(nullptr, viewport, beam, surface, camera);
        const auto fading = colorRefFromRuntimeDibColor(blended[32 * w + 40]);
        if (GetGValue(fading) <= 40 || GetGValue(fading) >= GetGValue(dark))
            return fail("soft beam did not retain its lifetime fade");
        std::copy(background.begin(), background.end(), blended.begin());
        beam.remainingTicks = 30;
        std::swap(beam.fromX, beam.toX); std::swap(beam.fromY, beam.toY);
        drawRuntimeDiskLaser(nullptr, viewport, beam, surface, camera);
        if (blended != bright) return fail("soft beam depends on direction");
        std::copy(background.begin(), background.end(), blended.begin());
        auto charge = beam; charge.renderIntent = "diskLaserCharge";
        charge.fromX = 2.0/3.0; charge.fromY = -2.0/3.0; charge.facing = 0;
        charge.frameIndex = 12; charge.totalTicks = 32; charge.remainingTicks = 20;
        drawRuntimeDiskLaser(nullptr, viewport, charge, surface, camera);
        int lightPixels = 0;
        for (std::size_t i=0; i<blended.size(); ++i) if (blended[i] != background[i]) {
            const auto before=colorRefFromRuntimeDibColor(background[i]);
            const auto after=colorRefFromRuntimeDibColor(blended[i]);
            if (GetGValue(after) < GetGValue(before)) return fail("charge arc retained a dark purple outline");
            if (GetGValue(after) > GetGValue(before)+20) ++lightPixels;
        }
        if (lightPixels < 10) return fail("charge arc lacks the beam's pale translucent light");
    }
    const auto& turret = loadRuntimeVoxelSourceFromCache("DISKTUR", probe, *cache, false);
    if (!turret.loaded || turret.metadata.hvaFrameCount != 1)
        return fail("original spinning turret VXL / single-frame HVA missing");
    constexpr int width = 240, height = 240, columns = 4;
    constexpr std::uint32_t background = 0x002c3438U;
    std::vector<std::uint32_t> pixels(width * height, background);
    std::vector<std::uint32_t> sheet(width * columns * height * 2, background);
    probe.surfacePixels = pixels.data(); probe.surfaceWidth = width; probe.surfaceHeight = height;
    RuntimeMapCamera camera;
    camera.initialized = true;
    const RECT viewport{0, 0, width, height};
    std::set<std::uint64_t> poseHashes;
    for (int row = 0; row < 2; ++row) for (int column = 0; column < columns; ++column) {
        std::fill(pixels.begin(), pixels.end(), background);
        // A Jumpjet may still be ascending during charge-up. Compare the
        // muzzle and the body captured in the same simulation frame.
        const auto& disk = poses[row == 0 ? column : 4 + column];
        const auto origin = runtimeObjectDrawPoint(disk, {}, camera);
        camera.worldOffsetX = static_cast<int>(std::lround(origin.x)) - 75;
        camera.worldOffsetY = static_cast<int>(std::lround(origin.y)) - 72
            - runtimeAircraftSpriteLiftPixels(disk, runtimeObjectLooksAircraftForRender(disk, probe));
        const POINT center = runtimeObjectBodyAnchorForRender(disk, viewport, probe, camera);
        if (!tryDrawVoxelSprite(nullptr, viewport, disk, center, probe, camera, *cache))
            return fail("original floating disk body could not be drawn");
        if (row == 0) {
            std::uint64_t hash = 1469598103934665603ULL;
            for (auto pixel : pixels) hash = (hash ^ pixel) * 1099511628211ULL;
            poseHashes.insert(hash);
        } else {
            const auto& actor = actors[column];
            if (actor.renderIntent == "diskLaserCharge") {
                POINT muzzle = screenPoint(projectCellXY(actor.fromX, actor.fromY, actor.fromLevel, camera),
                    viewport, camera);
                muzzle.x += actor.sourceScreenOffsetX;
                muzzle.y += actor.sourceScreenOffsetY;
                // Original FLH 0,0,75 projects eleven pixels above the body anchor.
                if (std::abs(muzzle.x - center.x) > 1 || std::abs(muzzle.y - center.y + 11) > 1)
                    return fail("charge ring detached from the airborne model muzzle");
            }
            WorldObjectInstance target;
            target.kind = WorldObjectKind::Unit; target.type = "MTNK";
            target.owner = "Americans"; target.alive = true; target.health = 200;
            target.x = static_cast<int>(std::lround(actor.toX));
            target.y = static_cast<int>(std::lround(actor.toY));
            target.level = static_cast<int>(std::lround(actor.toLevel));
            target.facing = target.turretFacing = 160;
            if (!tryDrawVoxelSprite(nullptr, viewport, target,
                    runtimeObjectBodyAnchorForRender(target, viewport, probe, camera), probe, camera, *cache))
                return fail("original target model missing");
            const auto before = pixels;
            std::vector<RuntimeProjectileRenderActor> shots{actors[column]};
            probe.projectileActors = &shots;
            RuntimeMapRenderStats stats;
            drawRuntimeProjectileActors(nullptr, viewport, probe, camera, *cache, stats);
            int purplePixels = 0;
            for (std::size_t i = 0; i < pixels.size(); ++i) {
                const auto color = colorRefFromRuntimeDibColor(pixels[i]);
                if (pixels[i] != before[i] && GetRValue(color) > GetGValue(color) + 30
                    && GetBValue(color) > GetGValue(color) + 30) ++purplePixels;
            }
            if (stats.projectileActorTravelDrawn != 1 || purplePixels < 5)
                return fail("authored purple arc / beam pixels missing in phase " + std::to_string(column));
            const auto stable = pixels;
            pixels = before;
            probe.surfacePixels = pixels.data();
            shots.front().frameIndex *= 2;
            shots.front().totalTicks *= 2;
            shots.front().remainingTicks *= 2;
            probe.fixedTickHz = 60;
            drawRuntimeProjectileActors(nullptr, viewport, probe, camera, *cache, stats);
            if (pixels != stable) return fail("render timing changed at 60 Hz");
            probe.fixedTickHz = 30;
            probe.projectileActors = nullptr;
        }
        for (int y = 0; y < height; ++y)
            std::copy_n(pixels.begin() + y * width, width,
                sheet.begin() + (row * height + y) * width * columns + column * width);
    }
    if (poseHashes.size() < 3U) return fail("turret facings reused a frozen model image");
    if (const char* path = std::getenv("CONCORD_FLOATING_DISK_BMP"))
        if (*path != '\0' && !writeRuntimeBgraBmp(path, width * columns, height * 2, sheet))
            return fail("preview write failed");
    return true;
}
