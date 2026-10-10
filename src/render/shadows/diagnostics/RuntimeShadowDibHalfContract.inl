// Included after RuntimeFrameScalingReferenceScope. These checks exercise the
// production shadow drawer with its original and optimized branches in one TU.
bool runRuntimeShadowDibHalfContractCheck(std::string& error) {
    const auto fail = [&](const std::string& detail) {
        error = "shadow DIB half: " + detail;
        return false;
    };
    std::uint64_t channelCases = 0;
    for (unsigned int high = 0; high < 256; ++high) {
        for (unsigned int channel = 0; channel < 256; ++channel) {
            for (unsigned int shift : {0U, 8U, 16U}) {
                const std::uint32_t seed = (high << 24U) | 0x0055aaedU;
                const std::uint32_t pixel = (seed & ~(255U << shift)) | (channel << shift);
                const std::uint32_t expected = packRuntimeDibColor(
                    runtimeBlendBlack(colorRefFromRuntimeDibColor(pixel), 128));
                if (runtimeHalfBlackShadowDib(pixel) != expected)
                    return fail("exhaustive channel/high-byte mismatch");
                ++channelCases;
            }
        }
    }
    constexpr int width = 37, height = 29;
    RuntimeMapCamera camera;
    camera.tileWidth = 48; camera.tileHeight = 24;
    const std::array<RECT, 8> viewports{{
        {0, 0, width, height}, {-9, -7, width + 11, height + 13},
        {2, 3, width - 5, height - 4}, {0, 0, 1, 1},
        {width - 1, height - 1, width, height}, {-12, -12, -1, -1},
        {width + 2, height + 2, width + 5, height + 8}, {5, 5, 5, 5}}};
    const std::array<POINT, 5> centers{{{0, 0}, {18, 15}, {19, 15},
        {width - 1, height - 1}, {18, 15}}};
    int drawingCases = 0;
    std::uint64_t darkenedTotal = 0;
    for (const auto& viewport : viewports) for (bool binary : {false, true}) {
        for (int pattern = 0; pattern < 5; ++pattern) {
            RuntimeShpFrameImage image;
            image.width = 16; image.height = 16;
            image.globalWidth = 16; image.globalHeight = 16;
            image.offsetX = pattern == 4 ? -3 : 0;
            image.offsetY = pattern == 4 ? 2 : 0;
            std::vector<unsigned char> mask(256);
            for (std::size_t i = 0; i < mask.size(); ++i) {
                // Includes every weighted alpha, empty masks and holes.
                mask[i] = pattern == 0 ? static_cast<unsigned char>(i)
                    : pattern == 1 ? 255 : pattern == 2 ? 0
                    : static_cast<unsigned char>((i % 3) ? (i * 73) & 255 : 0);
            }
            if (pattern == 4) mask.pop_back(); // Invalid, short source remains rejected.
            std::vector<std::uint32_t> reference(width * height), optimized(width * height);
            for (std::size_t i = 0; i < reference.size(); ++i)
                reference[i] = optimized[i] = static_cast<std::uint32_t>(i * 2654435761ULL);
            RuntimeMapRenderContext context;
            context.surfaceWidth = width; context.surfaceHeight = height;
            // Draw overlapping masks in identical order. Compare after each
            // draw to detect wrong clipping, skipped writes or altered overlap.
            for (const auto& center : centers) {
                bool referenceResult = false, optimizedResult = false;
                int referencePixels = -1, optimizedPixels = -1;
                {
                    RuntimeFrameScalingReferenceScope original(true);
                    context.surfacePixels = reference.data();
                    referenceResult = drawRuntimeVoxelShadowMask(nullptr, viewport, center,
                        context, camera, image, mask, binary, &referencePixels);
                }
                {
                    RuntimeFrameScalingReferenceScope fast(false);
                    context.surfacePixels = optimized.data();
                    optimizedResult = drawRuntimeVoxelShadowMask(nullptr, viewport, center,
                        context, camera, image, mask, binary, &optimizedPixels);
                }
                if (referenceResult != optimizedResult || referencePixels != optimizedPixels
                    || reference != optimized)
                    return fail("production mask pixels/return/count mismatch at case "
                        + std::to_string(drawingCases));
                darkenedTotal += static_cast<std::uint64_t>(referencePixels);
                ++drawingCases;
            }
        }
    }
    if (darkenedTotal == 0) return fail("fixture did not darken pixels");
    std::cout << "Shadow DIB half: channels=" << channelCases << " draws=" << drawingCases
        << " darkened=" << darkenedTotal << " masks/clips/overlaps/alphaBytes=exact\n";
    error.clear();
    return true;
}
