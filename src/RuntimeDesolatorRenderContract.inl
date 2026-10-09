bool runRuntimeDesolatorRenderContractCheck(const RuntimeMapRenderContext& context, std::string& error) {
    const auto fail = [&](const std::string& message) { error = "desolator render: " + message; return false; };
    constexpr int width = 960, height = 520;
    constexpr std::uint32_t background = 0x00536351U;
    std::vector<std::uint32_t> pixels(width * height, background);
    auto probe = context;
    probe.vision = nullptr;
    probe.surfacePixels = pixels.data(); probe.surfaceWidth = width; probe.surfaceHeight = height;
    RuntimeMapCamera camera;
    camera.initialized = true; camera.worldOffsetX = -width / 2; camera.worldOffsetY = 450 - height / 2;
    const RECT viewport{0, 0, width, height};
    GameWorld world;
    for (int y = 0; y <= 30; ++y) for (int x = 0; x <= 30; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        world.accessTerrainCells().push_back(cell);
    }
    SpecialMechanismRenderSnapshot site;
    site.kind = "radiation"; site.cell = {15, 15}; site.radius = 10;
    site.intensity = site.maximumIntensity = 500; site.expireTick = 500; site.color = "0,255,0";
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    RuntimeMapRenderStats stats;
    drawRuntimeRadiationCellTint(nullptr, viewport, world, probe, camera, *cache, stats, site);
    const auto sample = [&](double dx, double dy) {
        const auto point = screenPoint(projectCellXY(15.0 + dx, 15.0 + dy, 0, camera), viewport, camera);
        return pixels[point.y * width + point.x];
    };
    const auto green = [](std::uint32_t pixel) { return (pixel >> 8U) & 255U; };
    if (stats.radiationTintPixels <= 0 || sample(8, 8) != background || sample(10, 0) != background
        || sample(8, 0) == background || sample(6, 0) != sample(3.6, 4.8)
        || !(green(sample(0, 0)) > green(sample(3, 0)) && green(sample(3, 0)) > green(sample(6, 0))
            && green(sample(6, 0)) > green(sample(9, 0))))
        return fail("field was square, uneven at equal radii, or lacked a smooth fade");
    // Tile boundaries must not acquire a second layer of green.
    const auto seam = screenPoint(projectCellXY(17.5, 15.0, 0, camera), viewport, camera);
    const auto g0 = green(pixels[seam.y * width + seam.x - 1]);
    const auto g1 = green(pixels[seam.y * width + seam.x]);
    const auto g2 = green(pixels[seam.y * width + seam.x + 1]);
    if (std::abs(static_cast<int>(g0) - static_cast<int>(g1)) > 2
        || std::abs(static_cast<int>(g1) - static_cast<int>(g2)) > 2)
        return fail("shared terrain tile edge was tinted twice");
    const auto activePixels = pixels;
    std::fill(pixels.begin(), pixels.end(), background);
    probe.animationTick = 500;
    drawRuntimeRadiationCellTint(nullptr, viewport, world, probe, camera, *cache, stats, site);
    if (std::any_of(pixels.begin(), pixels.end(), [](auto pixel) { return pixel != background; }))
        return fail("expired field kept tinting the terrain");
    pixels = activePixels; probe.surfacePixels = pixels.data(); probe.animationTick = 0;
    // A visible TMP cliff pixel outside the projected flat diamond must still
    // receive light, while an unowned pixel beside it must remain untouched.
    std::fill(pixels.begin(), pixels.end(), background);
    auto& layer = cache->terrainLayer;
    layer.built = true; layer.width = width; layer.height = height;
    layer.originX = camera.worldOffsetX; layer.originY = camera.worldOffsetY;
    layer.pixels.assign(width * height, background);
    layer.terrainCellIndices.assign(width * height, -1);
    const auto cliff = screenPoint(projectCellXY(15, 15, 0, camera), viewport, camera);
    const int cliffIndex = (cliff.y + 80) * width + cliff.x;
    layer.terrainCellIndices[cliffIndex] = 15 * 512 + 15;
    drawRuntimeRadiationCellTint(nullptr, viewport, world, probe, camera, *cache, stats, site);
    if (pixels[cliffIndex] == background || pixels[cliffIndex + 1] != background)
        return fail("cliff ownership silhouette was ignored");
    const COLORREF dark = runtimeRadiationTerrainLight(RGB(20, 20, 20), RGB(0, 255, 0), 128);
    const COLORREF light = runtimeRadiationTerrainLight(RGB(100, 100, 100), RGB(0, 255, 0), 128);
    if (GetGValue(light) < GetGValue(dark) * 4) return fail("lighting flattened terrain texture contrast");
    pixels = activePixels; probe.surfacePixels = pixels.data();
    WorldObjectInstance deso;
    deso.kind = WorldObjectKind::Infantry; deso.type = "DESO"; deso.instanceId = 901;
    deso.owner = "Arabs"; deso.alive = true; deso.health = 150;
    deso.x = deso.y = 15; deso.cell = 15 * 512 + 15; deso.subCell = -1;
    deso.deployed = true; deso.mission = "Deployed"; deso.facing = 128;
    const auto center = runtimeObjectBodyAnchorForRender(deso, viewport, probe, camera);
    if (!tryDrawObjectShpSprite(nullptr, viewport, deso, center, probe, camera, *cache))
        return fail("original deployed Desolator SHP was unavailable");
    if (const char* path = std::getenv("CONCORD_DESOLATOR_BMP"))
        if (*path != '\0' && !writeRuntimeBgraBmp(path, width, height, pixels)) return fail("preview write failed");
    return true;
}
