// Exercise the real generic and specialized SHP/VXL compositor branches in
// one process. The pure row-kernel microtest uses the same production header.
bool runRuntimeIndexedOpaqueSpanContractCheck(std::string& error) {
    const auto fail = [&](const std::string& reason) {
        error = "indexed opaque spans: " + reason;
        return false;
    };
    constexpr int width = 79, height = 53;
    RuntimeMapCamera camera;
    camera.tileWidth = 60; camera.tileHeight = 30;
    std::vector<RuntimePaletteColor> palette(256);
    for (int index = 0; index < 256; ++index)
        palette[index] = {static_cast<unsigned char>(index * 37),
            static_cast<unsigned char>(index * 61), static_cast<unsigned char>(index * 97)};
    const std::array<RECT, 7> viewports{{{0, 0, width, height}, {-9, -7, width + 9, height + 11},
        {11, 7, width - 8, height - 9}, {0, 0, 1, 1}, {width - 1, height - 1, width, height},
        {-20, -20, -1, -1}, {width + 1, height + 1, width + 10, height + 10}}};
    const std::array<POINT, 4> centers{{{0, 0}, {width / 2, height / 2}, {width - 1, height - 1}, {19, 17}}};
    std::size_t cases = 0;
    std::size_t fastFrames = 0;
    for (int shape = 0; shape < 5; ++shape) for (int maskKind = 0; maskKind < 3; ++maskKind) {
        RuntimeShpFrameImage image;
        image.width = 37; image.height = 29;
        image.globalWidth = image.width; image.globalHeight = image.height;
        image.offsetX = shape == 4 ? -7 : 0; image.offsetY = shape == 4 ? 5 : 0;
        image.pixels.resize(static_cast<std::size_t>(image.width) * image.height);
        if (maskKind) image.ownerRemapMask.resize(image.pixels.size(), 0);
        if (maskKind == 2) image.ownerRemapMask.pop_back(); // Short masks use the authored index range.
        image.paletteRemapRangeFromHeader = shape % 2 != 0;
        image.paletteRemapStart = 80; image.paletteRemapEnd = 96;
        for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x) {
            const auto index = static_cast<std::size_t>(y) * image.width + x;
            const bool opaque = shape == 0 ? false : shape == 1 ? true
                : shape == 2 ? (x + y) % 2 != 0 : x > 11 && x < 25 && y > 6 && y < 22;
            image.pixels[index] = opaque ? static_cast<unsigned char>(1 + (index * 73) % 255) : 0;
            if (index < image.ownerRemapMask.size()) image.ownerRemapMask[index] = index % 3 ? 255 : 0;
        }
        for (const auto& viewport : viewports) for (bool owner : {false, true}) {
            std::vector<std::uint32_t> reference(width * height), optimized(width * height);
            for (std::size_t index = 0; index < reference.size(); ++index)
                reference[index] = optimized[index] = static_cast<std::uint32_t>(index * 2654435761ULL);
            RuntimeMapRenderContext context;
            context.surfaceWidth = width; context.surfaceHeight = height;
            for (std::size_t draw = 0; draw < centers.size(); ++draw) {
                // Owner/light changes force palette invalidation. Transparent
                // and tinted draws verify that the generic fallback remains.
                const int opacity = draw == 3 ? 129 : 255;
                context.chaosBodyTint = draw == 2 && shape == 4;
                context.airstrikeBodyColorAdd = draw == 2 && shape == 3 ? 0x392 : 0;
                const COLORREF remap = draw % 2 ? RGB(221, 40, 75) : RGB(35, 96, 205);
                bool oldResult, newResult;
                {
                    RuntimeFrameScalingReferenceScope oldPath(true);
                    context.surfacePixels = reference.data();
                    oldResult = drawRuntimeShpFrame(nullptr, viewport, centers[draw], context, camera,
                        image, palette, owner, remap, static_cast<int>(draw) * 3, shape % 2 != 0, false, opacity);
                }
                {
                    RuntimeFrameScalingReferenceScope newPath(false);
                    context.surfacePixels = optimized.data();
                    newResult = drawRuntimeShpFrame(nullptr, viewport, centers[draw], context, camera,
                        image, palette, owner, remap, static_cast<int>(draw) * 3, shape % 2 != 0, false, opacity);
                }
                if (oldResult != newResult || reference != optimized)
                    return fail("production pixels/return differ at " + std::to_string(cases));
                ++cases;
            }
        }
        if (image.spriteOpaqueSpansValid) ++fastFrames;
    }
    if (fastFrames == 0) return fail("fixture did not reach the production fast path");
    std::cout << "Indexed opaque spans: " << cases << " production draws; palette/remap/mask/clip/overlap/tint/opacity exact.\n";
    return true;
}
