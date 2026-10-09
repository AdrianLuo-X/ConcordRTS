bool runRuntimeGarrisonPipRenderSmokeProbe(RuntimeWindowState& live, std::string& error) {
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& s = *storage;
    s.config = live.config; s.report = live.report;
    s.rules = live.rules; s.rulesLoaded = true; s.art = live.art; s.artLoaded = true;
    s.structureFootprints = buildRuntimeStructureFootprints(s);
    s.economy.owner = "Americans";
    constexpr int size = 256, height = 224, columns = 3;
    std::vector<std::uint32_t> sheet(size * columns * height, 0xff243039U);
    RuntimeMapRenderContext context;
    context.rules = &s.rules; context.art = &s.art;
    context.assetRootPath = runtimeAssetRootPath(s.config);
    context.palettePath = runtimePalettePath(s.config, s.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(s.config, s.report.map);
    std::unordered_map<std::string, COLORREF> colors{{"americans", RGB(255, 210, 25)}};
    context.ownerRemapColors = &colors;
    RuntimeMapCamera camera;
    camera.worldOffsetX = -size / 2; camera.worldOffsetY = -96;
    WorldObjectInstance host;
    host.instanceId = 100; host.type = "NABNKR"; host.kind = WorldObjectKind::Structure;
    host.owner = "Americans"; host.alive = true; host.health = s.rules.findBuildingType(host.type)->strength;
    host.x = host.y = host.cell = 0; host.mission = "Occupying";
    const auto foundation = runtimeStructureFootprintForObject(s, host);
    if (foundation.width != 2 || foundation.height != 2) {
        error = "garrison pip preview: original bunker foundation missing"; return false;
    }
    std::vector<POINT> centers;
    for (int y = 0; y < foundation.height; ++y) for (int x = 0; x < foundation.width; ++x)
        centers.push_back(POINT{(x - y) * camera.tileWidth / 2 - camera.worldOffsetX,
            (x + y) * camera.tileHeight / 2 - camera.worldOffsetY});
    const auto geometry = runtimeStructureSelectionFrameGeometry(centers,
        camera.tileWidth / 2, camera.tileHeight / 2, 0);
    const std::array<const char*, 5> units{"E1", "E2", "INIT", "E1", "E2"};
    for (int column = 0; column < columns; ++column) {
        s.world.accessObjects().clear(); host.garrisonOccupantIds.clear();
        const int count = column == 0 ? 0 : column == 1 ? 3 : 5;
        for (int i = 0; i < count; ++i) {
            WorldObjectInstance infantry;
            infantry.instanceId = 200 + i; infantry.kind = WorldObjectKind::Infantry;
            infantry.type = units[i]; infantry.owner = host.owner;
            infantry.health = 100; infantry.alive = false; infantry.garrisonHostObjectId = host.instanceId;
            host.garrisonOccupantIds.push_back(infantry.instanceId);
            s.world.accessObjects().push_back(infantry);
        }
        std::vector<std::uint32_t> pixels(size * height, 0xff243039U);
        context.surfacePixels = pixels.data(); context.surfaceWidth = size; context.surfaceHeight = height;
        if (!drawRuntimeObjectPreviewSprite(nullptr, RECT{0,0,size,height}, host, context, camera, s.renderCache)) {
            error = "garrison pip preview: original bunker did not render"; return false;
        }
        const auto withoutPips = pixels;
        s.backBuffer.pixels = pixels.data(); s.backBuffer.width = size; s.backBuffer.height = height;
        const int draws = drawRuntimeGarrisonPips(nullptr, s, host, geometry);
        s.backBuffer.pixels = nullptr; s.backBuffer.width = s.backBuffer.height = 0;
        if (draws != 5 || pixels == withoutPips) {
            error = "garrison pip preview: original capacity pips were not drawn"; return false;
        }
        for (int y = 0; y < height; ++y)
            std::copy_n(pixels.data() + y * size, size, sheet.data() + y * size * columns + column * size);
    }
    if (const char* output = std::getenv("CONCORD_GARRISON_PIPS_BMP"))
        if (!writeRuntimeSmokeBmp(output, sheet.data(), size * columns, height, error)) return false;
    return true;
}
