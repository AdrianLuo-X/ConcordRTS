bool runRuntimeOreDrillPaletteRenderContractCheck(
    const RuntimeMapRenderContext& input, const std::string& root, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "Ore drill palette: " + message; return false;
    };
    auto storage = std::make_unique<RuntimeMapRenderCache>();
    auto& cache = *storage;
    RuntimeMapRenderContext context = input;
    constexpr int width = 160, height = 144;
    constexpr RECT viewport{0, 0, width, height};
    const POINT center{80, 96};
    RuntimeMapCamera camera;
    const auto background = packRuntimeDibColor(RGB(48, 56, 40));
    std::vector<std::uint32_t> actual(width * height), expected(actual.size()), wrong(actual.size());
    std::vector<std::uint32_t> sheet(width * 3 * height * 3, background);
    context.surfaceWidth = width; context.surfaceHeight = height;
    int checked = 0, distinctPixels = 0;
    for (const char* image : {"TIBTRE01", "TREE01"}) {
        const bool drill = std::string(image) == "TIBTRE01";
        const auto* first = loadRuntimeAuxiliaryShpSpriteFromCache(image, 0, context, cache, true);
        if (!first || (drill && first->frameCount < 4)) return fail(std::string(image) + " stock SHP missing");
        const int frames = runtimeVisibleMainFrameCount(first->frameCount, true);
        const auto* palette = drill ? runtimeUnitPaletteForRender(context, cache, false)
            : runtimeOverlayPaletteForRender(context, cache);
        const auto* isoPalette = runtimeOverlayPaletteForRender(context, cache);
        if (!palette || !isoPalette) return fail("stock palette missing");
        for (int warm = 0; warm != 2; ++warm) for (int frame = 0; frame != frames; ++frame) {
            std::fill(actual.begin(), actual.end(), background);
            std::fill(expected.begin(), expected.end(), background);
            context.surfacePixels = actual.data();
            if (!drawRuntimeAuxiliaryShpSprite(nullptr, viewport, center, context, camera, cache,
                    image, frame, 0, 0, true, false, false, RGB(255,255,255), nullptr, true))
                return fail(std::string(image) + " runtime draw failed");
            context.surfacePixels = expected.data();
            if (!drawRuntimeAuxiliaryShpSpriteWithPalette(nullptr, viewport, center, context, camera,
                    cache, image, frame, *palette, 0, 0, true, false, RGB(255,255,255), 255, true)
                || expected != actual
                || std::count(actual.begin(), actual.end(), background) == actual.size())
                return fail(std::string(image) + " frame=" + std::to_string(frame) + " native palette mismatch");
            ++checked;
            if (!drill || warm != 0) continue;
            std::fill(wrong.begin(), wrong.end(), background);
            context.surfacePixels = wrong.data();
            if (!drawRuntimeAuxiliaryShpSpriteWithPalette(nullptr, viewport, center, context, camera,
                    cache, image, frame, *isoPalette, 0, 0, true, false, RGB(255,255,255), 255, true))
                return fail("control palette draw failed");
            for (std::size_t p = 0; p != actual.size(); ++p) distinctPixels += actual[p] != wrong[p];
            if (frame == 0 || frame == frames / 2 || frame == frames - 1) {
                const int row = frame == 0 ? 0 : frame == frames / 2 ? 1 : 2;
                const std::vector<std::uint32_t>* columns[] = {&actual, &expected, &wrong};
                for (int column = 0; column != 3; ++column) for (int y = 0; y != height; ++y)
                    std::copy_n(columns[column]->begin() + y * width, width,
                        sheet.begin() + (row * height + y) * width * 3 + column * width);
            }
        }
    }
    if (distinctPixels < 100) return fail("wrong terrain palette control was not distinguishable");
    if (!root.empty() && !writeRuntimeBgraBmp(root + "/ore-drill-palette.bmp", width * 3, height * 3, sheet))
        return fail("preview write failed");
    std::cout << "Ore drill palette: nativeFrames=" << checked << " isoMismatchPixels=" << distinctPixels
              << " cold/warm=passed ordinaryTreePalette=preserved\n";
    return true;
}
