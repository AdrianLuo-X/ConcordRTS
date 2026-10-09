bool runRuntimeElectricTargetRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& error
) {
    const auto fail = [&](const std::string& message) {
        error = "electric target render: " + message;
        return false;
    };
    if (context.rules == nullptr || context.art == nullptr)
        return fail("original rules/art unavailable");
    constexpr int width = 240, height = 220, columns = 9, rows = 4;
    constexpr std::uint32_t background = 0x004c5850U;
    // Electric bolts use GDI. A nullptr HDC only exercises their counters and
    // cannot prove that their endpoint reaches the original SHP/VXL body.
    struct DibSurface {
        HDC dc = nullptr;
        HBITMAP bitmap = nullptr;
        HGDIOBJ previous = nullptr;
        std::uint32_t* pixels = nullptr;
        DibSurface() {
            dc = CreateCompatibleDC(nullptr);
            if (dc == nullptr) return;
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (bitmap == nullptr || bits == nullptr) return;
            pixels = static_cast<std::uint32_t*>(bits);
            previous = SelectObject(dc, bitmap);
            if (previous == nullptr || previous == HGDI_ERROR) pixels = nullptr;
        }
        ~DibSurface() {
            if (dc != nullptr && previous != nullptr && previous != HGDI_ERROR)
                SelectObject(dc, previous);
            if (bitmap != nullptr) DeleteObject(bitmap);
            if (dc != nullptr) DeleteDC(dc);
        }
    } surface;
    if (surface.pixels == nullptr) return fail("offscreen GDI DIB unavailable");
    std::vector<std::uint32_t> sheet(width * columns * height * rows, background);
    RuntimeMapRenderContext probe = context;
    probe.vision = nullptr;
    probe.visionObserverOwner.clear();
    probe.projectileActors = nullptr;
    probe.specialMechanismSnapshots = nullptr;
    probe.wrecks = nullptr;
    probe.particleSnapshots = nullptr;
    probe.particleBeamSnapshots = nullptr;
    probe.surfacePixels = surface.pixels;
    probe.surfaceWidth = width;
    probe.surfaceHeight = height;
    probe.surfaceTopDownOwned = true;
    RuntimeMapRenderCache cache;
    const RECT viewport{0, 0, width, height};
    struct TargetCase { const char* type; int subCell; int level; int tileWidth; bool aircraft; bool submerged = false; };
    const TargetCase cases[] = {
        {"SHK", 0, 0, 60, false}, {"SHK", 2, 0, 60, false},
        {"SHK", 4, 4, 60, false}, {"MTNK", -1, 0, 60, false},
        {"HTNK", -1, 3, 90, false}, {"ORCA", -1, 4, 60, true},
        {"SHK", 3, 3, 30, false}, {"MTNK", -1, 4, 120, false},
        {"DLPH", -1, 4, 60, false, true}
    };
    int verifiedDraws = 0;
    for (int column = 0; column < columns; ++column) {
        const auto scenario = cases[column];
        const std::string label = std::string(scenario.type) + " subcell="
            + std::to_string(scenario.subCell) + " level=" + std::to_string(scenario.level)
            + " tile=" + std::to_string(scenario.tileWidth);
        GameWorld world;
        WorldObjectInstance target;
        target.instanceId = 1;
        target.type = scenario.type;
        target.kind = std::string(scenario.type) == "SHK"
            ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        target.owner = "Americans";
        target.health = 100;
        target.x = target.y = 0;
        target.level = scenario.level;
        target.subCell = scenario.subCell;
        target.facing = target.turretFacing = 64;
        target.mission = "Guard";
        target.airborne = scenario.aircraft;
        target.submerged = scenario.submerged;
        target.flightLevel = scenario.aircraft ? scenario.level : 0;
        world.accessObjects() = {target};
        RuntimeMapCamera camera;
        camera.tileWidth = scenario.tileWidth;
        camera.tileHeight = scenario.tileWidth / 2;
        camera.worldOffsetX = -width / 2;
        camera.worldOffsetY = -180 - target.level * camera.tileHeight / 2;
        camera.initialized = true;
        RuntimeProjectileRenderActor beam;
        beam.actorId = 17;
        beam.targetObjectId = target.instanceId;
        beam.owner = "Russians";
        beam.weaponId = "TeslaBolt";
        beam.electricBolt = (column & 1) == 0;
        beam.renderIntent = "electricBolt";
        beam.alternateElectricColor = (column & 1) != 0;
        beam.electricColor = beam.alternateElectricColor ? "255,200,255" : "80,80,255";
        beam.fromX = -1.4;
        beam.fromY = 1.4;
        beam.fromLevel = target.level + 2;
        beam.sourceScreenOffsetX = 8;
        beam.sourceScreenOffsetY = -6;
        beam.toX = beam.toY = 0;
        beam.toLevel = drawLevelForObject(target);
        beam.lifecycleState = "InFlight";
        beam.totalTicks = beam.remainingTicks = 12;
        beam.frameIndex = 3;
        std::vector<RuntimeProjectileRenderActor> beams{beam};
        SpecialMechanismRenderSnapshot arc;
        arc.kind = column & 1 ? "electricAssault" : "electricArc";
        arc.instanceId = beam.actorId;
        arc.targetObjectId = target.instanceId;
        arc.owner = beam.owner;
        arc.sourceX = beam.fromX;
        arc.sourceY = beam.fromY;
        arc.sourceLevel = beam.fromLevel;
        arc.sourceScreenOffsetX = beam.sourceScreenOffsetX;
        arc.sourceScreenOffsetY = beam.sourceScreenOffsetY;
        arc.targetX = beam.toX;
        arc.targetY = beam.toY;
        arc.targetLevel = beam.toLevel;
        arc.hasBeamEndpoints = true;
        arc.color = beam.electricColor;
        arc.phase = beam.alternateElectricColor ? "Alternate" : "Normal";
        arc.progress = beam.frameIndex;
        arc.expireTick = probe.animationTick + 12;
        std::vector<SpecialMechanismRenderSnapshot> arcs{arc};
        POINT sourcePoint = screenPoint(projectCellXY(beam.fromX, beam.fromY,
            beam.fromLevel, camera), viewport, camera);
        sourcePoint.x += runtimeMuzzlePixelX(beam.sourceScreenOffsetX, camera, beam.sourceNativeFixedX);
        sourcePoint.y += runtimeMuzzlePixelY(beam.sourceScreenOffsetY, camera, beam.sourceNativeFixedY);
        const POINT savedPoint = runtimeProjectileActorTargetPoint(beam, viewport, camera);
        std::vector<std::uint32_t> body;
        POINT expectedPoint{};
        const auto drawBody = [&](const WorldObjectInstance& pose) {
            GdiFlush();
            std::fill_n(surface.pixels, width * height, background);
            probe.projectileActors = nullptr;
            probe.specialMechanismSnapshots = nullptr;
            const POINT origin = runtimeObjectBodyAnchorForRender(pose, viewport, probe, camera);
            RECT bounds{};
            bool drawn = false;
            if (objectUsesVoxelArt(pose, probe)) {
                drawn = tryDrawVoxelSprite(surface.dc, viewport, pose, origin, probe, camera, cache);
                if (const auto* sprite = loadRuntimeVoxelFromCache(pose, probe, cache))
                    bounds = runtimeShpFrameScreenRect(runtimeVoxelModelDrawCenter(origin, camera),
                        camera, sprite->image);
            } else {
                drawn = tryDrawObjectShpSprite(surface.dc, viewport, pose, origin, probe, camera, cache);
                if (!resolveRuntimeObjectShpSpriteBounds(pose, origin, probe, camera, cache, bounds))
                    return false;
            }
            GdiFlush();
            if (!drawn || !runtimeRectIsValid(bounds)) return false;
            body.assign(surface.pixels, surface.pixels + width * height);
            if (std::count_if(body.begin(), body.end(), [](std::uint32_t pixel) {
                    return (pixel & 0x00ffffffU) != background;
                }) < 6) return false;
            // Derive the oracle from the rendered body's sprite rectangle,
            // without invoking the electric target helper under test.
            expectedPoint = {origin.x, (bounds.top + bounds.bottom) / 2};
            return true;
        };
        const auto captureSheet = [&](int row) {
            for (int y = 0; y < height; ++y)
                std::copy_n(surface.pixels + y * width, width,
                    sheet.begin() + (row * height + y) * width * columns + column * width);
        };
        const auto checkBolt = [&](bool afterglow, const POINT& expected, bool requireOverlap,
                const std::string& stage, int previewRow) {
            GdiFlush();
            std::copy(body.begin(), body.end(), surface.pixels);
            drawRuntimeElectricBolt(surface.dc, sourcePoint, expected,
                beam.alternateElectricColor, beam.electricColor, beam.actorId, beam.frameIndex);
            GdiFlush();
            const std::vector<std::uint32_t> reference(surface.pixels, surface.pixels + width * height);
            std::copy(body.begin(), body.end(), surface.pixels);
            RuntimeMapRenderStats stats;
            if (afterglow) {
                probe.specialMechanismSnapshots = &arcs;
                probe.projectileActors = nullptr;
                drawRuntimeSpecialMechanismOverlays(surface.dc, viewport, world, probe, camera, cache, stats);
            } else {
                probe.specialMechanismSnapshots = nullptr;
                probe.projectileActors = &beams;
                drawRuntimeProjectileActors(surface.dc, viewport, probe, camera, cache, stats, &world);
            }
            GdiFlush();
            probe.specialMechanismSnapshots = nullptr;
            probe.projectileActors = nullptr;
            int changed = 0, overlap = 0, endpointPixels = 0, mismatch = 0;
            const int endpointRadius = 4;
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                const int index = y * width + x;
                if (surface.pixels[index] != reference[index]) ++mismatch;
                if ((surface.pixels[index] & 0x00ffffffU) == (body[index] & 0x00ffffffU)) continue;
                ++changed;
                if ((body[index] & 0x00ffffffU) != background) ++overlap;
                if (std::abs(x - expected.x) <= endpointRadius
                    && std::abs(y - expected.y) <= endpointRadius) ++endpointPixels;
            }
            const int actorsDrawn = afterglow ? stats.specialMechanismRenderActors : stats.projectileActorTravelDrawn;
            if (actorsDrawn != 1 || changed < 8 || endpointPixels < 1 || mismatch != 0
                || (requireOverlap && overlap < 1)) {
                return fail(label + " " + stage + " actors=" + std::to_string(actorsDrawn)
                    + " pixels=" + std::to_string(changed) + " overlap=" + std::to_string(overlap)
                    + " endpoint=" + std::to_string(endpointPixels) + " mismatch=" + std::to_string(mismatch));
            }
            if (previewRow >= 0) captureSheet(previewRow);
            ++verifiedDraws;
            return true;
        };
        if (!drawBody(target)) return fail(label + " original body unavailable");
        const auto originalBody = body;
        const POINT originalPoint = expectedPoint;
        if (!checkBolt(false, expectedPoint, true, "discharge", 0)
            || !checkBolt(true, expectedPoint, true, "afterglow", 1)) return false;
        auto& moved = world.accessObjects().front();
        moved.hasRenderPosition = true;
        moved.renderX = 0.65;
        moved.renderY = -0.35;
        moved.renderLevel = target.level + 0.5;
        if (!drawBody(moved)) return fail(label + " moving original body unavailable");
        if (std::abs(expectedPoint.x - originalPoint.x) < 3)
            return fail(label + " moving fixture did not change the rendered endpoint");
        if (!checkBolt(false, expectedPoint, true, "moving discharge", -1)
            || !checkBolt(true, expectedPoint, true, "moving afterglow", 2)) return false;
        moved.x = moved.y = 10;
        moved.renderX = moved.renderY = 10;
        moved.level = target.level + 7;
        moved.renderLevel = moved.level;
        moved.dockedToObjectId = 99;
        moved.health = 0;
        moved.alive = false;
        moved.mission = "Die";
        body = originalBody;
        if (!checkBolt(false, originalPoint, true, "dead victim discharge at saved hit", -1)
            || !checkBolt(true, originalPoint, true, "dead victim afterglow at saved hit", 3)) return false;
        if (column == 0) {
            beams.front().targetObjectId = arcs.front().targetObjectId = -1;
            if (!checkBolt(false, savedPoint, false, "missing victim discharge", -1)
                || !checkBolt(true, savedPoint, false, "missing victim afterglow", -1)) return false;
            beams.front().targetObjectId = arcs.front().targetObjectId = target.instanceId;
            moved = target;
            moved.type = "TESLA";
            moved.kind = WorldObjectKind::Structure;
            if (!checkBolt(false, savedPoint, false, "structure discharge", -1)
                || !checkBolt(true, savedPoint, false, "structure afterglow", -1)) return false;
        }
    }
    if (const char* path = std::getenv("CONCORD_ELECTRIC_TARGET_BMP"); path && *path)
        if (!writeRuntimeBgraBmp(path, width * columns, height * rows, sheet))
            return fail("preview write failed");
    std::cout << "Electric target rendering: SHK subcells, original tank/aircraft bodies, "
        "flat/elevated 0.5/1/1.5/2x views, discharge/afterglow pixels, moving/dead/missing victims; "
        << verifiedDraws << " GDI draws passed\n";
    error.clear();
    return true;
}
