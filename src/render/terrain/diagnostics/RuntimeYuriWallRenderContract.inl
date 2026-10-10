// Stock assets are intentional here: the regression was an omitted YR MIX
// archive, which a synthetic SHP fixture or a fallback counter cannot detect.
bool runRuntimeYuriWallRenderContractCheck(
    const RuntimeMapRenderContext& context,
    const std::string& outputDirectory,
    std::string& errorMessage
) {
    const auto fail = [&](const std::string& reason) {
        errorMessage = "wall art: " + reason;
        return false;
    };
    if (context.art == nullptr || context.rules == nullptr)
        return fail("stock rules/art missing");
    const std::string exportsRoot = runtimeMixExportsRootForContext(context);
    if (exportsRoot.empty()) return fail("MIX export root missing");
    struct TheaterCase { const char* ini; const char* palette; };
    const TheaterCase theaters[] = {
        {"temperatmd.ini", "cache_unittem.pal"},
        {"snowmd.ini", "cache_unitsno.pal"},
        {"urbanmd.ini", "cache_uniturb.pal"},
        {"desertmd.ini", "cachemd_unitdes.pal"},
        {"urbann.ini", "cachemd_unitubn.pal"},
        {"lunarmd.ini", "cachemd_unitlun.pal"}
    };
    constexpr int size = 160;
    constexpr std::uint32_t background = 0x00607048U;
    const RECT viewport{0, 0, size, size};
    const POINT center{size / 2, size / 2};
    RuntimeMapCamera camera;
    camera.tileWidth = 60;
    camera.tileHeight = 30;
    std::vector<std::uint32_t> pixels(size * size, background);
    std::vector<std::uint32_t> expected(size * size, background);
    std::unordered_map<std::string, COLORREF> colors{
        {"wall-probe", RGB(192, 128, 32)}
    };
    const std::unordered_set<std::string> neutralOwners;
    int framesChecked = 0;
    int rendersChecked = 0;
    for (const auto& theaterCase : theaters) {
        IniFile ini;
        ini.loadFromString("[TileSet0000]\nFileName=Clear01\nTilesInSet=1\n");
        TheaterDatabase theater;
        if (!theater.loadFromIni(ini, theaterCase.ini))
            return fail("theater fixture failed");
        auto cache = std::make_unique<RuntimeMapRenderCache>();
        auto referenceCache = std::make_unique<RuntimeMapRenderCache>();
        RuntimeMapRenderContext probe = context;
        probe.theater = &theater;
        probe.vision = nullptr;
        probe.lightingTint = nullptr;
        probe.lightingProfile = nullptr;
        probe.ownerRemapColors = &colors;
        probe.neutralRemapOwners = &neutralOwners;
        probe.surfacePixels = pixels.data();
        probe.surfaceWidth = size;
        probe.surfaceHeight = size;
        probe.structurePalettePath = exportsRoot + "/extracted_palettes/"
            + theaterCase.palette;
        std::vector<RuntimePaletteColor> originalPalette;
        if (!loadRuntimePalette(probe.structurePalettePath, originalPalette, errorMessage))
            return false;
        const std::string label = std::filesystem::path(theaterCase.ini).stem().string();
        std::vector<std::uint32_t> atlas(size * 16 * size * 9, background);
        int typeIndex = 0;
        for (const char* typeId : {"GAFWLL", "GAWALL", "NAWALL"}) {
            const auto* type = context.rules->findBuildingType(typeId);
            const auto* art = context.art->findArtType(typeId);
            if (type == nullptr || !type->wall || art == nullptr
                || art->toOverlay != typeId || art->damageLevels != 3)
                return fail(std::string(typeId) + " stock wall metadata");
            WorldObjectInstance wall;
            wall.instanceId = 1;
            wall.kind = WorldObjectKind::Structure;
            wall.type = typeId;
            wall.owner = "wall-probe";
            for (int damage = 0; damage < 3; ++damage) {
                wall.health = damage == 0 ? type->strength
                    : (damage == 1 ? type->strength / 2 : type->strength / 4);
                for (int mask = 0; mask < 16; ++mask) {
                    wall.wallConnectionMask = mask;
                    const int frame = mask + damage * 16;
                    auto* sprite = loadRuntimeAuxiliaryShpSpriteFromCache(
                        typeId, frame, probe, *cache, false);
                    if (sprite == nullptr || sprite->frameIndex != frame)
                        return fail(std::string(typeId) + " missing frame " + std::to_string(frame));
                    if (typeIndex == 0) {
                        const bool snow = theater.getTileExtension() == ".sno";
                        const std::string originalMix = exportsRoot + "/ra2md/"
                            + (snow ? "snowmd.mix" : "genermd.mix");
                        const std::string originalImage = snow ? "gafwll" : "ggfwll";
                        const std::string expectedSource = normalizedLowerPath(
                            originalMix + "!" + originalImage + ".shp");
                        if (normalizedLowerPath(sprite->sourcePath) != expectedSource
                            || toLowerAscii(sprite->resolvedImageId) != originalImage
                            || sprite->image.globalWidth != 102
                            || sprite->image.globalHeight != 74)
                            return fail(label + " loaded obsolete Yuri wall: " + sprite->sourcePath);
                        RuntimeShpFrameImage original;
                        std::string source;
                        if (!loadRuntimeShpFrameFromMixArchive(originalMix,
                                *referenceCache, originalImage, frame, original,
                                source, errorMessage)) return false;
                        if (sprite->image.pixels != original.pixels
                            || sprite->image.offsetX != original.offsetX
                            || sprite->image.offsetY != original.offsetY)
                            return fail(label + " Yuri wall differs from original frame");
                    } else if (toLowerAscii(sprite->resolvedImageId).find("wall")
                            == std::string::npos) {
                        return fail(std::string(typeId) + " changed to another wall family");
                    }
                    ++framesChecked;
                    RuntimeMapRenderContext reference = probe;
                    reference.surfacePixels = expected.data();
                    std::fill(expected.begin(), expected.end(), background);
                    const POINT anchor = runtimeAuxiliaryShpAdjustedCenter(
                        center, camera, sprite->image, 0, 0, true);
                    if (!drawRuntimeShpFrame(nullptr, viewport, anchor, reference,
                            camera, sprite->image, originalPalette, true,
                            colors.at("wall-probe"))) return fail("reference render failed");
                    for (int repeat = 0; repeat < 2; ++repeat) {
                        std::fill(pixels.begin(), pixels.end(), background);
                        RuntimeMapRenderStats stats;
                        if (!tryDrawRuntimeStructureOverlaySprite(nullptr, viewport,
                                wall, center, probe, camera, *cache, stats)
                            || stats.structureWallOverlaySprites != 1
                            || stats.structureWallOverlayFallbacks != 0
                            || stats.lastStructureWallOverlayFrame != frame
                            || pixels != expected)
                            return fail(label + " " + typeId + " frame="
                                + std::to_string(frame) + " palette/connection mismatch");
                        ++rendersChecked;
                    }
                    if (std::all_of(pixels.begin(), pixels.end(),
                            [](std::uint32_t color) { return color == background; }))
                        return fail(std::string(typeId) + " empty frame");
                    for (int y = 0; y < size; ++y)
                        std::copy_n(pixels.data() + y * size, size,
                            atlas.data() + ((typeIndex * 3 + damage) * size + y)
                                * size * 16 + mask * size);
                }
            }
            ++typeIndex;
        }
        if (!outputDirectory.empty()
            && !writeRuntimeBgraBmp(std::filesystem::path(outputDirectory)
                    / (label + "-wall-frames.bmp"), size * 16, size * 9, atlas))
            return fail("atlas write failed");
        if (!outputDirectory.empty()) {
            // Match the user's intersecting wall reference at the actual
            // 60x30 cell spacing, in back-to-front order.
            constexpr int width = 420;
            constexpr int height = 260;
            std::vector<std::uint32_t> connected(width * height, background);
            probe.surfacePixels = connected.data();
            probe.surfaceWidth = width;
            probe.surfaceHeight = height;
            const RECT crossViewport{0, 0, width, height};
            WorldObjectInstance wall;
            wall.instanceId = 1;
            wall.kind = WorldObjectKind::Structure;
            wall.type = "GAFWLL";
            wall.owner = "wall-probe";
            wall.health = context.rules->findBuildingType("GAFWLL")->strength;
            for (int position = -3; position <= 3; ++position) {
                for (int axis = 0; axis < 2; ++axis) {
                    if (position == 0 && axis == 1) continue;
                    wall.x = axis == 0 ? position : 0;
                    wall.y = axis == 0 ? 0 : position;
                    wall.wallConnectionMask = position == 0 ? 15
                        : (axis == 0 ? (position == -3 ? 2 : (position == 3 ? 8 : 10))
                            : (position == -3 ? 4 : (position == 3 ? 1 : 5)));
                    const POINT point{width / 2 + (wall.x - wall.y) * 30,
                        height / 2 + (wall.x + wall.y) * 15};
                    RuntimeMapRenderStats stats;
                    if (!tryDrawRuntimeStructureOverlaySprite(nullptr, crossViewport,
                            wall, point, probe, camera, *cache, stats)
                        || stats.structureWallOverlaySprites != 1)
                        return fail("connected wall preview failed");
                }
            }
            if (!writeRuntimeBgraBmp(std::filesystem::path(outputDirectory)
                    / (label + "-yuri-wall-cross.bmp"), width, height, connected))
                return fail("connected preview write failed");
        }
    }
    std::cout << "Wall art contract: theaters=6 types=3 frames=" << framesChecked
        << " renders=" << rendersChecked << " YuriSource=genermd/snowmd"
        << " palettes=original-unit cached=yes\n";
    return true;
}
