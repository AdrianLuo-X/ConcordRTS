// Call the actual viewport compositor in both modes, so the helper proof also
// covers clipping, row stride, high bytes, lookup invalidation and pixel counts.
bool runRuntimeLightingLookupRenderContractCheck(std::string& error) {
    const auto fail = [&](const std::string& reason) {
        error = "lighting lookup raster: " + reason;
        return false;
    };
    MapLightingProfile mapProfile;
    mapProfile.ambient = 1.0; mapProfile.ground = 0.05;
    mapProfile.red = 1.0; mapProfile.green = 0.9; mapProfile.blue = 0.9;
    const auto mapTint = runtimeLightingTintForProfile(mapProfile);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const std::array<RuntimeLightingTint, 18> tints{{
        mapTint, {0, 0, 0}, {1, 1, 1}, {0.5, 0.25, 0.75}, {1.5, 2, 16},
        {0.7, 0.9, 1.15}, {1.05, 0.945, 0.945}, {-1, -0.0, 17},
        {nan, infinity, 0.3}, {16, 15.9, 2}, {0.003, 0.001, 0.009},
        {std::nextafter(1.0, 0.0), std::nextafter(1.0, 2.0), 1.0},
        {std::nextafter(0.5, 0.0), std::nextafter(0.5, 1.0), 0.5},
        {1.000001, 0.999999, 1.000001}, {32, 64, 128},
        {0.85 * 1.5, 0.2 * 1.5, 0.3 * 1.5}, mapTint, {0.5, 0.25, 0.75}
    }};
    std::size_t cases = 0;
    bool mapQualified = false, roundingRejected = false;
    for (std::size_t tintIndex = 0; tintIndex < tints.size(); ++tintIndex) {
        std::array<std::uint8_t, 256> red{}, green{}, blue{};
        for (std::uint32_t value = 0; value < 256; ++value) {
            const auto color = runtimeApplyLightingTintToPackedDibColor(value * 0x010101U, tints[tintIndex]);
            red[value] = static_cast<std::uint8_t>(color >> 16U);
            green[value] = static_cast<std::uint8_t>(color >> 8U);
            blue[value] = static_cast<std::uint8_t>(color);
        }
        const auto raster = runtimeMakeLightingLookupRaster(red, green, blue);
        if (tintIndex == 0) mapQualified = raster.exact;
        if (tintIndex == 5) roundingRejected = !raster.exact;
        for (const int width : {1, 2, 3, 4, 5, 6, 7, 16, 39}) {
            constexpr int height = 13;
            const std::array<RECT, 7> viewports{{{0, 0, width, height},
                {-7, -9, width + 8, height + 9}, {1, 2, width - 1, height - 2},
                {width - 1, height - 1, width, height}, {1, 1, 1, height},
                {-9, -8, -1, -2}, {width + 1, 0, width + 9, height}}};
            for (const auto& viewport : viewports) for (std::size_t offset = 0; offset < 4; ++offset) {
                std::vector<std::uint32_t> original(static_cast<std::size_t>(width) * height + 8);
                for (std::size_t index = 0; index < original.size(); ++index)
                    original[index] = static_cast<std::uint32_t>(index * 2654435761ULL + tintIndex * 83197U);
                auto reference = original, optimized = original;
                RuntimeMapRenderContext context;
                context.surfaceWidth = width; context.surfaceHeight = height;
                context.lightingTint = &tints[tintIndex];
                for (int repeat = 0; repeat < 2; ++repeat) {
                    int oldCount, newCount;
                    {
                        RuntimeFrameScalingReferenceScope oldPath(true);
                        context.surfacePixels = reference.data() + offset;
                        oldCount = applyRuntimeLightingTintToViewport(nullptr, viewport, context);
                    }
                    {
                        RuntimeFrameScalingReferenceScope newPath(false);
                        context.surfacePixels = optimized.data() + offset;
                        newCount = applyRuntimeLightingTintToViewport(nullptr, viewport, context);
                    }
                    if (oldCount != newCount || reference != optimized)
                        return fail("actual viewport pixels/count differ at " + std::to_string(cases));
                    ++cases;
                }
                context.lightingTint = nullptr;
                context.surfacePixels = optimized.data() + offset;
                const auto beforeNull = optimized;
                if (applyRuntimeLightingTintToViewport(nullptr, viewport, context) != 0
                    || beforeNull != optimized) return fail("null tint changed pixels");
            }
        }
    }
    if (!mapQualified || !roundingRejected)
        return fail("fixture did not exercise both exact SIMD and rounding fallback");
    std::cout << "Lighting lookup raster: " << cases
        << " production calls; LUT/clip/stride/alpha/tails/cache changes/count exact.\n";
    return true;
}
