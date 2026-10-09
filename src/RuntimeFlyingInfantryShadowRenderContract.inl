bool runRuntimeFlyingInfantryShadowRenderContractCheck(
    const GameWorld& sourceWorld,
    const RuntimeMapRenderContext& sourceContext,
    std::string& errorMessage
) {
    constexpr int width = 360;
    constexpr int height = 280;
    const RECT viewport{0, 0, width, height};
    GameWorld world = sourceWorld;
    world.accessObjects().clear();
    world.accessTerrainObjects().clear();
    world.accessSmudges().clear();
    world.accessTerrainCells().clear();
    for (int y = 8; y <= 34; ++y) for (int x = 8; x <= 34; ++x) {
        TerrainCell cell;
        cell.x = x;
        cell.y = y;
        cell.cellIndex512 = y * 512 + x;
        cell.level = 2;
        world.accessTerrainCells().push_back(cell);
    }
    std::array<POINT, 3> expectedCenters{{{90, 190}, {180, 205}, {270, 228}}};
    for (int index = 0; index < 3; ++index) {
        WorldObjectInstance object;
        object.instanceId = 930000 + index;
        object.kind = WorldObjectKind::Infantry;
        object.type = "JUMPJET";
        object.owner = "Americans";
        object.health = 125;
        object.x = 20 + index * 2;
        object.y = 20 - index;
        object.cell = object.y * 512 + object.x;
        object.subCell = 0;
        object.airborne = true;
        object.level = object.flightLevel = 4 + index * 2;
        object.facing = index * 96;
        object.mission = index == 2 ? "Move" : "Guard";
        if (index == 2) {
            object.hasRenderPosition = object.renderMoveActive = true;
            object.renderX = object.x + 0.25;
            object.renderY = object.y + 0.25;
            object.renderLevel = object.flightLevel;
        }
        world.accessObjects().push_back(object);
    }
    RuntimeMapRenderContext context = sourceContext;
    context.mapFacts = nullptr;
    context.vision = nullptr;
    context.projectileActors = nullptr;
    context.impactActors = nullptr;
    context.particleSnapshots = nullptr;
    context.particleBeamSnapshots = nullptr;
    context.localLightSnapshots = nullptr;
    context.specialMechanismSnapshots = nullptr;
    context.selectedObjectIds = nullptr;
    context.gateSnapshots = nullptr;
    context.uiResources = nullptr;
    context.useTileTextures = false;
    context.effectOnlyProbe = false;
    context.surfaceWidth = width;
    context.surfaceHeight = height;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    HDC dc = CreateCompatibleDC(nullptr);
    void* rawPixels = nullptr;
    HBITMAP bitmap = dc == nullptr ? nullptr
        : CreateDIBSection(dc, &info, DIB_RGB_COLORS, &rawPixels, nullptr, 0);
    if (!dc || !bitmap || !rawPixels) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        errorMessage = "flying infantry shadow surface unavailable";
        return false;
    }
    const HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
    context.surfacePixels = static_cast<std::uint32_t*>(rawPixels);
    const auto render = [&](bool shadows) {
        context.shadowsEnabled = shadows;
        RuntimeMapCamera camera;
        camera.worldOffsetX = -90;
        camera.worldOffsetY = 380;
        camera.initialized = true;
        RuntimeMapRenderCache cache;
        drawRuntimeMap(dc, viewport, world, context, camera, cache);
        GdiFlush();
        return std::vector<std::uint32_t>(context.surfacePixels,
            context.surfacePixels + width * height);
    };
    const auto withoutShadows = render(false);
    const auto withShadows = render(true);
    SelectObject(dc, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);

    std::array<int, 3> shadowPixels{};
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const int offset = y * width + x;
        if (withShadows[offset] == withoutShadows[offset]) continue;
        bool onGroundDisc = false;
        for (int index = 0; index < 3; ++index) {
            const POINT center = expectedCenters[index];
            if (std::abs(x - center.x) <= 8 && std::abs(y - (center.y - 1)) <= 4) {
                ++shadowPixels[index];
                onGroundDisc = true;
            }
        }
        if (!onGroundDisc) {
            errorMessage = "flying infantry shadow escaped its ground footprint";
            return false;
        }
    }
    if (shadowPixels[0] < 40 || shadowPixels[0] > 100
        || shadowPixels[1] != shadowPixels[0] || shadowPixels[2] != shadowPixels[0]) {
        errorMessage = "flying infantry ground discs changed with facing, altitude or motion";
        return false;
    }
    if (const char* path = std::getenv("CONCORD_FLYING_INFANTRY_SHADOW_BMP")) {
        if (*path && !writeRuntimeBgraBmp(std::filesystem::u8path(path), width, height, withShadows)) {
            errorMessage = "could not write flying infantry shadow preview";
            return false;
        }
    }
    std::cout << "Flying infantry shadows: ground discs, facing/altitude/motion stable, "
                 "global toggle respected\n";
    errorMessage.clear();
    return true;
}
