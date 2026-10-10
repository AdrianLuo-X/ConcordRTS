// Included after RuntimeShadowOpaqueSpan.h. This portable contract compares
// against the old row/pixel traversal and black-blend formula independently.
inline bool runRuntimeShadowOpaqueSpanContractCheck(std::string& error) {
    const auto fail = [&](const char* message) {
        error = message;
        return false;
    };
    const auto referencePixel = [](std::uint32_t pixel, unsigned char shadow,
        bool binaryMask) {
        if (shadow == 0) return pixel;
        const int alpha = binaryMask ? 128
            : std::max(1, (static_cast<int>(shadow) * 128 + 127) / 255);
        const int keep = 255 - alpha;
        std::uint32_t result = 0;
        for (unsigned int shift : {0U, 8U, 16U}) {
            const int value = static_cast<int>((pixel >> shift) & 255U);
            result |= static_cast<std::uint32_t>((value * keep + 127) / 255) << shift;
        }
        return result;
    };
    for (unsigned int shadow = 0; shadow < 256U; ++shadow) {
        for (unsigned int value = 0; value < 256U; ++value) {
            const auto pixel = 0xa7000000U | (value << 16U)
                | ((255U - value) << 8U) | ((value * 73U) & 255U);
            for (bool binary : {false, true}) {
                const auto expected = referencePixel(pixel, static_cast<unsigned char>(shadow), binary);
                if (runtimeBlendShadowSpanDib(pixel, static_cast<unsigned char>(shadow), binary, false) != expected
                    || runtimeBlendShadowSpanDib(pixel, static_cast<unsigned char>(shadow), binary, true) != expected) {
                    return fail("shadow blend channel/high-byte/binary contract mismatch");
                }
            }
        }
    }

    RuntimeShadowOpaqueSpanCache cache;
    std::vector<unsigned char> mask{0, 1, 127, 254, 255, 0, 9, 0};
    if (!cache.prepare(mask, 4, 2) || !cache.usable() || !cache.hasNonzero()
        || cache.spans().size() != 3) return fail("shadow span preparation mismatch");
    const auto initialBuilds = cache.rebuilds();
    if (!cache.prepare(mask, 4, 2) || cache.rebuilds() != initialBuilds)
        return fail("immutable shadow mask was rescanned");
    // Same address, dimensions and size with a different revision must rebuild.
    std::fill(mask.begin(), mask.end(), 0);
    if (!cache.prepare(mask, 4, 2, 1) || cache.hasNonzero()
        || cache.rebuilds() != initialBuilds + 1) return fail("shadow mask revision not invalidated");
    mask[2] = 255;
    cache.invalidate();
    if (!cache.prepare(mask, 4, 2, 1) || !cache.hasNonzero()
        || cache.spans().size() != 1) return fail("shadow in-place rewrite invalidate failed");
    const auto beforeShape = cache.rebuilds();
    if (!cache.prepare(mask, 2, 4, 1) || cache.rebuilds() != beforeShape + 1
        || cache.spans()[0].y != 1) return fail("shadow shape not invalidated");
    auto replacement = mask;
    if (!cache.prepare(replacement, 2, 4, 1) || cache.rebuilds() != beforeShape + 2)
        return fail("shadow storage replacement not invalidated");
    replacement.push_back(1);
    if (!cache.prepare(replacement, 2, 4, 1) || cache.rebuilds() != beforeShape + 3)
        return fail("shadow mask size not invalidated");
    if (cache.prepare(replacement, 0, 4) || cache.prepare(replacement, 4, -1)
        || cache.prepare(replacement, 8, 8) || cache.hasNonzero() || cache.usable())
        return fail("invalid shadow dimensions accepted");
    std::vector<unsigned char> tailOnly{0, 0, 0, 0, 1};
    if (!cache.prepare(tailOnly, 2, 2) || !cache.hasNonzero()
        || !cache.usable() || !cache.spans().empty())
        return fail("shadow surplus-tail legacy return mismatch");

    std::vector<unsigned char> fragmented(100 * 100);
    for (int y = 0; y < 100; ++y) for (int x = 0; x < 100; ++x)
        fragmented[static_cast<std::size_t>(y) * 100 + x] = (x & 1) ? 1 : 0;
    if (!cache.prepare(fragmented, 100, 100) || !cache.hasNonzero()
        || cache.usable() || !cache.spans().empty())
        return fail("fragmented shadow did not retain bounded fallback");
    const auto fragmentedBuilds = cache.rebuilds();
    if (!cache.prepare(fragmented, 100, 100) || cache.rebuilds() != fragmentedBuilds)
        return fail("fragmented shadow fallback was rescanned");

    std::uint32_t randomState = 0x861fc7a3U;
    const auto random = [&] {
        randomState ^= randomState << 13U;
        randomState ^= randomState >> 17U;
        randomState ^= randomState << 5U;
        return randomState;
    };
    struct ClipCase { int targetX, targetY, left, top, right, bottom; };
    const ClipCase cases[] = {
        {3, 4, 0, 0, 19, 13}, {-5, -3, 0, 0, 19, 13},
        {17, 11, 0, 0, 19, 13}, {3, 4, 6, 6, 12, 10},
        {-20, 1, 0, 0, 19, 13}, {1, -30, 0, 0, 19, 13},
        {2, 2, 4, 4, 4, 9}, {7, 4, -10, -10, 9, 9},
    };
    constexpr int surfaceWidth = 19, surfaceHeight = 13;
    constexpr int imageWidth = 11, imageHeight = 9;
    constexpr std::size_t guardCount = 8;
    constexpr std::uint32_t guard = 0xfbc2185dU;
    const auto surfacePixels = static_cast<std::size_t>(surfaceWidth) * surfaceHeight;
    for (int maskCase = 0; maskCase < 6; ++maskCase) {
        std::vector<unsigned char> pixels(imageWidth * imageHeight);
        for (std::size_t index = 0; index < pixels.size(); ++index) {
            const unsigned char values[] = {0, 1, 127, 254, 255};
            pixels[index] = maskCase == 0 ? 0 : maskCase == 1 ? 255
                : maskCase == 2 ? values[index % 5]
                : (index % 3 == 0 ? 0 : static_cast<unsigned char>(random()));
        }
        RuntimeShadowOpaqueSpanCache prepared;
        if (!prepared.prepare(pixels, imageWidth, imageHeight) || !prepared.usable())
            return fail("test shadow unexpectedly unavailable");
        for (const auto& clip : cases) for (bool binary : {false, true})
            for (bool halfBlack : {false, true}) {
                std::vector<std::uint32_t> fast(surfacePixels + 2 * guardCount, guard);
                for (std::size_t index = 0; index < surfacePixels; ++index)
                    fast[guardCount + index] = random();
                auto reference = fast;
                const int startX = std::max({0, clip.left - clip.targetX, -clip.targetX});
                const int endX = std::min({imageWidth, clip.right - clip.targetX, surfaceWidth - clip.targetX});
                const int startY = std::max({0, clip.top - clip.targetY, -clip.targetY});
                const int endY = std::min({imageHeight, clip.bottom - clip.targetY, surfaceHeight - clip.targetY});
                std::size_t expectedCount = 0;
                std::size_t actualCount = 0;
                // Repeated overlap verifies destination reads and write order;
                // rasterizing the same mask twice may never reuse output pixels.
                for (int repeat = 0; repeat < 3; ++repeat) {
                    for (int y = startY; y < endY; ++y) for (int x = startX; x < endX; ++x) {
                        const auto shadow = pixels[static_cast<std::size_t>(y) * imageWidth + x];
                        if (shadow == 0) continue;
                        auto& pixel = reference[guardCount
                            + static_cast<std::size_t>(clip.targetY + y) * surfaceWidth + clip.targetX + x];
                        pixel = referencePixel(pixel, shadow, binary);
                        ++expectedCount;
                    }
                    actualCount += runtimeDrawShadowOpaqueSpans(pixels.data(), imageWidth,
                        prepared.spans(), fast.data() + guardCount, surfaceWidth,
                        clip.targetX, clip.targetY, startX, endX, startY, endY, binary, halfBlack);
                }
                if (actualCount != expectedCount || fast != reference)
                    return fail("shadow span clipping/overlap/count/pixels mismatch");
                for (std::size_t index = 0; index < guardCount; ++index)
                    if (fast[index] != guard || fast[guardCount + surfacePixels + index] != guard)
                        return fail("shadow span wrote outside surface bounds");
            }
    }
    {
        RuntimeShadowSpanReferenceScope reference(true);
        if (!runtimeShadowSpanReferenceForRender()) return fail("shadow reference override missing");
        {
            RuntimeShadowSpanReferenceScope accelerated(false);
            if (runtimeShadowSpanReferenceForRender()) return fail("nested shadow fast override missing");
        }
        if (!runtimeShadowSpanReferenceForRender()) return fail("shadow reference scope not restored");
    }
    error.clear();
    return true;
}
