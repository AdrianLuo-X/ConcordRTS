// Include after RuntimeHealthPipStrip.inl and drawRuntimeIndexedImage.
bool runRuntimeHealthPipStripPixelContract(std::string& error) {
    const auto fail = [&](const std::string& reason) {
        error = "health pip strip: " + reason;
        return false;
    };
    constexpr int width = 43;
    constexpr int height = 7;
    std::vector<std::uint32_t> backdrop(static_cast<std::size_t>(width) * height);
    for (std::size_t index = 0; index < backdrop.size(); ++index)
        backdrop[index] = 0x9b624831U ^ static_cast<std::uint32_t>(index * 17777U);

    const std::array<std::vector<unsigned char>, 6> pixelCases{{
        {1, 2, 3, 4}, {0, 1, 2, 0}, {0, 0, 0, 0},
        {4, 3, 2, 1}, {255, 1, 254, 0}, {2, 2, 1, 1}
    }};
    const std::array<std::vector<RuntimePaletteColor>, 3> palettes{{
        {{71, 37, 53}, {0, 255, 0}, {0, 176, 0}, {255, 255, 0}, {184, 32, 32}},
        {{255, 255, 255}, {255, 0, 255}, {240, 32, 240}, {239, 32, 255}, {255, 33, 255}},
        {{255, 0, 255}, {13, 97, 211}}
    }};
    constexpr std::array<int, 9> lefts{-40, -3, -1, 0, 1, 12, 41, 42, 47};
    constexpr std::array<int, 6> tops{-3, -1, 0, 5, 6, 8};
    std::size_t cases = 0;
    for (const auto& pixels : pixelCases) {
        RuntimeShpFrameImage image;
        image.width = image.height = 2;
        image.pixels = pixels;
        for (const auto& palette : palettes) {
            for (bool chroma : {false, true}) {
                for (int left : lefts) for (int top : tops) {
                    for (int fill = 0; fill <= 34; ++fill) {
                        auto expected = backdrop;
                        auto actual = backdrop;
                        int expectedPips = 0;
                        // Invoke the old general renderer, including its odd
                        // final pip's 2-to-1 source sampling and CPU clipping.
                        for (int x = 0; x < fill; x += 2) {
                            const RECT rect{left + x, top,
                                left + std::min(fill, x + 2), top + 2};
                            if (!drawRuntimeIndexedImage(nullptr, rect, image, palette,
                                    true, true, true, expected.data(), width, height,
                                    false, runtimeIndexedOpaqueAlpha, chroma, chroma))
                                return fail("reference rejected a supported pip");
                            ++expectedPips;
                        }
                        if (!drawRuntimeHealthPipStripPixels(image, palette, chroma,
                                {actual.data(), width, height}, left, top, fill))
                            return fail("strip rejected a supported pip");
                        if (actual != expected || expectedPips != (fill + 1) / 2)
                            return fail("pixels/count differ at case " + std::to_string(cases));
                        ++cases;
                    }
                }
            }
        }
    }

    // Repeated and overlapping strips retain caller order and transparency.
    RuntimeShpFrameImage image;
    image.width = image.height = 2;
    auto expected = backdrop;
    auto actual = backdrop;
    for (int index = 0; index < 128; ++index) {
        image.pixels = pixelCases[static_cast<std::size_t>(index) % pixelCases.size()];
        const auto& palette = palettes[static_cast<std::size_t>(index) % palettes.size()];
        const int left = (index * 7) % 48 - 4;
        const int top = (index * 3) % 9 - 1;
        const int fill = index % 35;
        const bool chroma = (index & 1) != 0;
        for (int x = 0; x < fill; x += 2) {
            const RECT rect{left + x, top, left + std::min(fill, x + 2), top + 2};
            drawRuntimeIndexedImage(nullptr, rect, image, palette, true, true, true,
                expected.data(), width, height, false, runtimeIndexedOpaqueAlpha, chroma, chroma);
        }
        if (!drawRuntimeHealthPipStripPixels(image, palette, chroma,
                {actual.data(), width, height}, left, top, fill) || actual != expected)
            return fail("overlapping strips differ at unit " + std::to_string(index));
    }

    // Unsupported resources/targets must leave the old caller fallback intact.
    image.pixels = pixelCases[0];
    const auto& palette = palettes[0];
    actual = backdrop;
    if (drawRuntimeHealthPipStripPixels(image, palette, false, {}, 0, 0, 5)
        || drawRuntimeHealthPipStripPixels(image, palette, false,
            {actual.data(), 0, height}, 0, 0, 5)
        || drawRuntimeHealthPipStripPixels(image, {}, false,
            {actual.data(), width, height}, 0, 0, 5)
        || drawRuntimeHealthPipStripPixels(image, palette, false,
            {actual.data(), width, height}, 0, 0, -1))
        return fail("unsupported input accepted");
    image.pixels.resize(3);
    if (drawRuntimeHealthPipStripPixels(image, palette, false,
            {actual.data(), width, height}, 0, 0, 5))
        return fail("partial source accepted");
    image.pixels = pixelCases[0];
    image.width = 1;
    if (drawRuntimeHealthPipStripPixels(image, palette, false,
            {actual.data(), width, height}, 0, 0, 5) || actual != backdrop)
        return fail("fallback guard altered pixels");
    {
        RuntimeHealthPipStripReferenceScope reference(true);
        if (!runtimeHealthPipStripReference()) return fail("reference override ignored");
        {
            RuntimeHealthPipStripReferenceScope optimized(false);
            if (runtimeHealthPipStripReference()) return fail("optimized override ignored");
        }
        if (!runtimeHealthPipStripReference()) return fail("nested override not restored");
    }
    return true;
}
