bool runRuntimeCrateSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& reason) {
        error = "reward crates: " + reason;
        return false;
    };
    // Reuse the existing coverage of weighted rewards, money, owner-local
    // modifiers, movement domains, FreeMCV and authored campaign crates.
    std::string rewardSummary;
    if (!validateRuntimeSkirmishOptionSemantics(live.config, live, rewardSummary))
        return fail(rewardSummary);

    auto storage = std::make_unique<RuntimeWindowState>();
    auto& state = *storage;
    state.config = live.config;
    state.config.crates = true;
    state.rules = live.rules;
    state.rulesLoaded = true;
    state.art = live.art;
    state.artLoaded = true;
    state.theater = live.theater;
    state.theaterLoaded = live.theaterLoaded;
    state.report = live.report;
    state.world = live.world;
    state.world.accessObjects().clear();
    state.economy.owner = "SkirmishPlayer1";
    state.economy.credits = 1000;
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.runtimePreferencePersistenceEnabled = false;
    initializeRuntimeNavigation(state);
    const auto candidates = runtimeCratePlacementCandidates(state, {});
    if (candidates.empty()) return fail("no legal crate cells on the stock map");
    const std::vector<RuntimeCellPoint> starts{candidates.front().cell};
    initializeRuntimeSkirmishCrates(state, starts);
    if (state.skirmishCrates.empty() || state.initialSkirmishCrates.empty())
        return fail("enabled setup did not generate crates");
    const auto originalCount = state.skirmishCrates.size();

    state.config.crates = false;
    if (advanceRuntimeSkirmishCrates(state)) return fail("disabled crates kept advancing");
    initializeRuntimeSkirmishCrates(state, starts);
    if (!state.skirmishCrates.empty() || !state.initialSkirmishCrates.empty())
        return fail("disabled setup retained generated crates");
    state.config.crates = true;
    initializeRuntimeSkirmishCrates(state, starts);
    if (state.skirmishCrates.size() != originalCount)
        return fail("re-enabling did not restore the authored crate count");

    // Use an actual vehicle on a land crate and advance the real pickup loop.
    const auto land = std::find_if(candidates.begin(), candidates.end(),
        [](const RuntimeCratePlacement& cell) { return !cell.water; });
    if (land == candidates.end()) return fail("no land cell for pickup");
    auto& crate = state.skirmishCrates.front();
    crate.cell = land->cell;
    crate.water = false;
    crate.value = 2000;
    crate.powerupType = "Money";
    crate.powerupAnimation = "MONEY";
    crate.powerupData = 2000.0;
    crate.imageId = runtimeCrateArtImageId(state.rules, state.rules.getCrateRules().image);
    crate.respawnTick = 10000;
    WorldObjectInstance collector;
    collector.instanceId = 1;
    collector.kind = WorldObjectKind::Unit;
    collector.owner = state.economy.owner;
    collector.type = "MTNK";
    collector.alive = true;
    collector.health = 100;
    collector.x = crate.cell.x;
    collector.y = crate.cell.y;
    collector.cell = runtimeCellKey(collector.x, collector.y);
    state.world.accessObjects().push_back(collector);
    state.tick = 50;
    if (!advanceRuntimeSkirmishCrates(state) || crate.active
            || state.economy.credits != 3000 || crate.respawnTick != 51
            || state.impactEffects.empty() || state.impactEffects.back().animId != "MONEY")
        return fail("pickup did not grant money, play the original effect and consume the crate");
    const auto collectedCell = crate.cell;
    ++state.tick;
    if (!advanceRuntimeSkirmishCrates(state) || !crate.active
            || crate.cell == collectedCell || crate.respawnTick <= state.tick
            || state.skirmishCrates.size() != originalCount)
        return fail("collected crate did not respawn on an unoccupied cell");
    const int replacementLifetime = crate.respawnTick;
    state.tick = replacementLifetime;
    if (!advanceRuntimeSkirmishCrates(state) || crate.respawnTick <= state.tick)
        return fail("uncollected crate did not refresh after its lifetime");

    RuntimeMapRenderContext context;
    context.rules = &state.rules;
    context.art = &state.art;
    context.theater = &state.theater;
    context.assetRootPath = runtimeAssetRootPath(state.config);
    context.palettePath = runtimePalettePath(state.config, state.report.map);
    context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(state.config, state.report.map);
    const char* output = std::getenv("CONCORD_CRATE_OUTPUT");
    std::ofstream resources;
    if (output != nullptr) {
        std::filesystem::create_directories(output);
        resources.open(std::filesystem::path(output) / "resources.txt");
    }
    for (const char* imageId : {"CRATE", "WCRATE", "MONEY", "ARMOR", "FIREPOWR",
            "HEALALL", "REVEAL", "SPEED", "VETERAN"}) {
        RuntimeShpFrameImage image;
        std::string resolved, source;
        int frames = 0;
        if (!loadRuntimeAuxiliaryShpFrameImage(context, state.renderCache, imageId,
                0, image, resolved, source, error, true, &frames)
                || frames < 1)
            return fail(std::string("missing original resource ") + imageId + ": " + error);
        // The original award animations may begin with an empty frame. Decode
        // the authored sequence and require visible content, not a fake frame 0.
        bool visibleFrame = false;
        int firstVisibleFrame = -1;
        for (int frame = 0; frame < frames; ++frame) {
            if (!loadRuntimeAuxiliaryShpFrameImage(context, state.renderCache, imageId,
                    frame, image, resolved, source, error, true))
                return fail(std::string("invalid original frame ") + imageId + ": " + error);
            if (std::any_of(image.pixels.begin(), image.pixels.end(),
                    [](unsigned char pixel) { return pixel != 0; })) {
                visibleFrame = true;
                if (firstVisibleFrame < 0) firstVisibleFrame = frame;
            }
        }
        if (!visibleFrame) return fail(std::string("original sequence has no visible frames: ") + imageId);
        if (resources) resources << imageId << " | " << frames << " frames | first visible="
            << firstVisibleFrame << " | " << source << '\n';
    }

    // Crates are an interaction overlay, so validate the actual drawing call
    // on unexplored, revealed and re-shrouded terrain, with no fallback art.
    state.skirmishCrates.resize(1);
    state.skirmishCrates.front().cell = land->cell;
    state.skirmishCrates.front().active = true;
    state.skirmishCrates.front().imageId = "CRATE";
    state.world.accessObjects().clear();
    state.visionEnabled = true;
    state.vision.reset();
    state.vision.update(state.world, &state.rules, {{state.economy.owner, {}}}, state.tick);
    constexpr int width = 320, height = 240;
    const RECT viewport{0, 0, width, height};
    state.camera.initialized = true;
    const auto center = runtimeProjectCellXY(land->cell.x, land->cell.y,
        terrainLevelAtRuntimeCell(state, land->cell.x, land->cell.y), state.camera);
    state.camera.worldOffsetX = center.x - width / 2;
    state.camera.worldOffsetY = center.y - height / 2;
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (dc == nullptr || bitmap == nullptr) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        return fail("could not allocate native crate render surface");
    }
    const auto oldBitmap = SelectObject(dc, bitmap);
    const auto render = [&]() {
        GdiFlush();
        std::fill_n(static_cast<std::uint32_t*>(pixels), width * height, 0xff202820U);
        return drawRuntimeSkirmishCrates(dc, viewport, state, context);
    };
    const bool hidden = render() == 0;
    state.vision.revealArea(state.economy.owner, land->cell.x, land->cell.y, 1);
    const bool visible = render() == 1;
    bool imageWritten = true;
    if (output != nullptr) {
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        header.bfSize = header.bfOffBits + width * height * 4;
        std::ofstream screenshot(std::filesystem::path(output) / "original-crate.bmp", std::ios::binary);
        GdiFlush();
        screenshot.write(reinterpret_cast<const char*>(&header), sizeof(header));
        screenshot.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
        screenshot.write(static_cast<const char*>(pixels), width * height * 4);
        imageWritten = screenshot.good();
    }
    state.skirmishCrates.front().imageId = "WCRATE";
    const bool waterVisible = render() == 1;
    state.config.crates = false;
    const bool disabled = render() == 0;
    state.skirmishCrates.front().authored = true;
    const bool authoredVisibleWithRandomDisabled = render() == 1;
    state.skirmishCrates.front().authored = false;
    state.campaignMode = true;
    const bool campaignVisible = render() == 1;
    state.vision.resetShroud(state.economy.owner);
    const bool reshrouded = render() == 0;
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    if (!hidden || !visible || !waterVisible || !disabled || !authoredVisibleWithRandomDisabled
        || !campaignVisible || !reshrouded || !imageWritten)
        return fail("original sprites/shroud: hidden=" + std::to_string(hidden)
            + " visible=" + std::to_string(visible) + " water=" + std::to_string(waterVisible)
            + " disabled=" + std::to_string(disabled) + " campaign=" + std::to_string(campaignVisible)
            + " reshrouded=" + std::to_string(reshrouded) + " image=" + std::to_string(imageWritten));
    std::cout << "Reward crates: enabled/disabled generation, actual pickup, replacement, expiry, "
        "native land/water/effect assets and shroud rendering passed\n" << rewardSummary << '\n';
    return true;
}
