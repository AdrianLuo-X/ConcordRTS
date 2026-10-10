static bool runRuntimeEmptyBuildingDepthContractCheck(std::string& error) {
    const auto fail = [&](const char* message) {
        error = std::string("empty building depth: ") + message;
        return false;
    };
    RuntimeEmptyBuildingDepthReferenceScope accelerated(false);
    GameWorld world;
    if (!runtimeSkipEmptyBuildingDepthScene(world)) return fail("empty world retained scene array");
    for (const auto kind : {WorldObjectKind::Unit, WorldObjectKind::Infantry}) {
        WorldObjectInstance object;
        object.kind = kind;
        object.alive = true;
        world.accessObjects().push_back(object);
    }
    if (!runtimeSkipEmptyBuildingDepthScene(world)) return fail("mobile-only world retained scene array");
    for (int variant = 0; variant != 4; ++variant) {
        WorldObjectInstance structure;
        structure.kind = WorldObjectKind::Structure;
        structure.type = variant == 1 ? "__NULL_RESOURCE__" : "GAPOWR";
        structure.alive = variant != 0;
        structure.health = variant == 0 ? 0 : 100;
        structure.x = structure.y = variant == 2 ? 1000000 : 3;
        structure.mission = variant == 3 ? "Selling" : "Guard";
        world.accessObjects().push_back(structure);
        if (runtimeSkipEmptyBuildingDepthScene(world)) return fail("structure variant bypassed established route");
        world.accessObjects().pop_back();
    }
    {
        RuntimeEmptyBuildingDepthReferenceScope reference(true);
        if (runtimeSkipEmptyBuildingDepthScene(world)) return fail("reference bypassed scene allocation");
        {
            RuntimeEmptyBuildingDepthReferenceScope nested(false);
            if (!runtimeSkipEmptyBuildingDepthScene(world)) return fail("nested optimized override failed");
        }
        if (runtimeSkipEmptyBuildingDepthScene(world)) return fail("reference scope not restored");
    }
    if (!runtimeSkipEmptyBuildingDepthScene(world)) return fail("optimized scope not restored");
    return true;
}

static bool runRuntimeEmptyBuildingDepthAssetContractCheck(
    const RuntimeMapRenderContext& source, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "empty building depth assets: " + message;
        return false;
    };
    IniFile ini;
    MapDatabase map;
    GameWorld world;
    if (!ini.loadFromString("[Map]\nTheater=TEMPERATE\nSize=0,0,32,32\nLocalSize=0,0,32,32\n")
        || !map.loadFromIni(ini) || !world.loadFromMap(map, source.rules)) return fail("fixture map");
    world.accessObjects().clear();
    for (const auto* type : {"MTNK", "DRED", "SHAD", "E1"}) {
        WorldObjectInstance object;
        object.type = type;
        object.instanceId = 930000 + static_cast<int>(world.getObjects().size());
        object.kind = object.type == "E1" ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        object.owner = "Americans";
        object.alive = true;
        object.health = 1000;
        object.mission = "Guard";
        object.x = 3 + static_cast<int>(world.getObjects().size()) * 2;
        object.y = 4;
        object.facing = static_cast<int>(world.getObjects().size()) * 64;
        object.turretFacing = 255 - object.facing;
        object.airborne = object.type == "SHAD";
        object.authoredAircraft = object.airborne;
        object.level = object.flightLevel = object.airborne ? 4 : 0;
        world.accessObjects().push_back(object);
    }
    RuntimeMapRenderContext context = source;
    context.vision = nullptr; context.visionObserverOwner.clear();
    context.seeHiddenObjects = true; context.stableRenderConfiguration = true;
    context.specialMechanismSnapshots = nullptr; context.gateSnapshots = nullptr;
    context.projectileActors = nullptr; context.impactActors = nullptr;
    context.wrecks = nullptr; context.buildingCollapses = nullptr;
    context.particleSnapshots = nullptr; context.particleBeamSnapshots = nullptr;
    context.localLightSnapshots = nullptr; context.selectedObjectIds = nullptr;
    context.playerCheerUntilTick = nullptr; context.uiResources = nullptr;
    context.lightingTint = nullptr; context.lightingProfile = nullptr;
    context.detailStatsEnabled = false; context.coarseStageTimingEnabled = false;
    context.useTileTextures = false; context.effectOnlyProbe = false;
    constexpr int width = 800, height = 600;
    const RECT viewports[] = {{0, 0, width, height}, {17, 11, 783, 589}, {0, 0, 320, 240}};
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* rawPixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &rawPixels, nullptr, 0);
    if (!dc || !bitmap || !rawPixels) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        return fail("surface allocation");
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    struct Cleanup {
        HDC dc; HBITMAP bitmap; HGDIOBJ old;
        ~Cleanup() { SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); }
    } cleanup{dc, bitmap, oldBitmap};
    auto* pixels = static_cast<std::uint32_t*>(rawPixels);
    constexpr std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    context.surfacePixels = pixels; context.surfaceWidth = width; context.surfaceHeight = height;
    RuntimeMapRenderCache accelerated, reference;
    struct BodyResult {
        bool resolved = false;
        RECT bounds{};
        RuntimeObjectWorldRenderPlacement placement;
    };
    std::vector<BodyResult> expectedBodies;
    std::vector<std::uint32_t> expectedPixels;
    bool expectedVisibleResult = false;
    for (std::size_t viewportIndex = 0; viewportIndex < sizeof(viewports) / sizeof(viewports[0]); ++viewportIndex) {
        const auto& viewport = viewports[viewportIndex];
        ++context.animationTick;
        for (auto& object : world.accessObjects()) {
            object.facing = (object.facing + 17) & 255;
            object.turretFacing = (object.turretFacing + 29) & 255;
            object.movementAnimationDistance += 0.125;
        }
        for (bool fast : {false, true}) {
            RuntimeEmptyBuildingDepthReferenceScope mode(!fast);
            RuntimeMapRenderCache& cache = fast ? accelerated : reference;
            RuntimeMapCamera camera;
            camera.initialized = true;
            camera.worldOffsetX = -(viewport.right - viewport.left) / 2;
            camera.worldOffsetY = -80;
            std::fill_n(pixels, pixelCount, 0xa1407199U);
            RuntimeGeometryQueryScope paint(world, viewport, context, camera, cache);
            drawRuntimeMap(dc, viewport, world, context, camera, cache);
            GdiFlush();
            if (!fast) expectedPixels.assign(pixels, pixels + pixelCount);
            else if (!std::equal(expectedPixels.begin(), expectedPixels.end(), pixels))
                return fail("map pixels differ at viewport " + std::to_string(viewportIndex));
            if (!fast) expectedBodies.clear();
            for (std::size_t index = 0; index < world.getObjects().size(); ++index) {
                const auto& object = world.getObjects()[index];
                BodyResult actual;
                actual.placement = runtimeObjectWorldRenderPlacement(object, world, viewport, context, camera, cache);
                actual.resolved = resolveRuntimeMobileObjectSpriteBounds(object, actual.placement.center,
                    context, camera, cache, actual.bounds);
                if (!fast) expectedBodies.push_back(actual);
                else {
                    const auto& expected = expectedBodies[index];
                    if (actual.resolved != expected.resolved || !EqualRect(&actual.bounds, &expected.bounds)
                        || actual.placement.center.x != expected.placement.center.x
                        || actual.placement.center.y != expected.placement.center.y
                        || actual.placement.sortPoint.x != expected.placement.sortPoint.x
                        || actual.placement.sortPoint.y != expected.placement.sortPoint.y
                        || actual.placement.airborneForeground != expected.placement.airborneForeground
                        || !EqualRect(&actual.placement.bodyViewport, &expected.placement.bodyViewport))
                        return fail("mobile/naval/air geometry differs: " + object.type);
                }
            }
            std::unordered_map<int, std::vector<RuntimeSpriteHitSpan>> visible;
            const bool visibleResult = resolveRuntimeBuildingVisibleHitSpans(world, viewport, context, camera, cache, visible);
            if (!visible.empty()) return fail("mobile-only scene produced building hit spans");
            if (!fast) expectedVisibleResult = visibleResult;
            else if (visibleResult != expectedVisibleResult) return fail("empty visible-span return contract differs");
        }
    }
    return true;
}
