void runtimeReferenceHorizontalShroudBlur(const std::vector<unsigned char>& shroudMask,
              std::vector<unsigned char>& horizontalBlur,
              int surfaceWidth, int clipLeft, int clipTop,
              int clipRight, int clipBottom) {
    constexpr int featherRadius = 4;
    constexpr int featherSampleCount = 9;
    for (int y = clipTop; y < clipBottom; ++y) {
        const std::size_t rowOffset =
            static_cast<std::size_t>(y)
            * static_cast<std::size_t>(surfaceWidth);
        int sum = 0;
        for (int sample = -featherRadius;
            sample <= featherRadius;
            ++sample) {
            const int sampleX = std::clamp(
                clipLeft + sample,
                clipLeft,
                clipRight - 1
            );
            sum += shroudMask[rowOffset + static_cast<std::size_t>(sampleX)];
        }
        for (int x = clipLeft; x < clipRight; ++x) {
            horizontalBlur[rowOffset + static_cast<std::size_t>(x)] =
                static_cast<unsigned char>(
                    (sum + (featherSampleCount / 2)) / featherSampleCount
                );
            const int removeX = std::clamp(
                x - featherRadius,
                clipLeft,
                clipRight - 1
            );
            const int addX = std::clamp(
                x + featherRadius + 1,
                clipLeft,
                clipRight - 1
            );
            sum += static_cast<int>(
                shroudMask[rowOffset + static_cast<std::size_t>(addX)]
            ) - static_cast<int>(
                shroudMask[rowOffset + static_cast<std::size_t>(removeX)]
            );
        }
    }


}

// Exact comparison against the former column-wise compositor, including
// clipped surfaces and dimensions smaller than the nine-pixel filter.
void runtimeReferenceVerticalShroudBlur(const std::vector<unsigned char>& horizontalBlur,
              std::vector<unsigned char>& softenedShroud,
              int surfaceWidth, int clipLeft, int clipTop,
              int clipRight, int clipBottom) {
    constexpr int featherRadius = 4;
    constexpr int featherSampleCount = 9;
    for (int x = clipLeft; x < clipRight; ++x) {
        int sum = 0;
        for (int sample = -featherRadius;
            sample <= featherRadius;
            ++sample) {
            const int sampleY = std::clamp(
                clipTop + sample,
                clipTop,
                clipBottom - 1
            );
            const std::size_t offset =
                (static_cast<std::size_t>(sampleY)
                    * static_cast<std::size_t>(surfaceWidth))
                + static_cast<std::size_t>(x);
            sum += horizontalBlur[offset];
        }
        for (int y = clipTop; y < clipBottom; ++y) {
            const std::size_t offset =
                (static_cast<std::size_t>(y)
                    * static_cast<std::size_t>(surfaceWidth))
                + static_cast<std::size_t>(x);
            softenedShroud[offset] = static_cast<unsigned char>(
                (sum + (featherSampleCount / 2)) / featherSampleCount
            );
            const int removeY = std::clamp(
                y - featherRadius,
                clipTop,
                clipBottom - 1
            );
            const int addY = std::clamp(
                y + featherRadius + 1,
                clipTop,
                clipBottom - 1
            );
            const std::size_t removeOffset =
                (static_cast<std::size_t>(removeY)
                    * static_cast<std::size_t>(surfaceWidth))
                + static_cast<std::size_t>(x);
            const std::size_t addOffset =
                (static_cast<std::size_t>(addY)
                    * static_cast<std::size_t>(surfaceWidth))
                + static_cast<std::size_t>(x);
            sum += static_cast<int>(horizontalBlur[addOffset])
                - static_cast<int>(horizontalBlur[removeOffset]);
        }
    }

}
bool runRuntimeShroudBlurContractCheck(std::string& error) {
    std::uint32_t randomState = 0x18227361U;
    const auto random = [&]() {
        randomState ^= randomState << 13U;
        randomState ^= randomState >> 17U;
        randomState ^= randomState << 5U;
        return randomState;
    };
    int cases = 0;
    const auto check = [&](int width, int height, int paddingX, int paddingY) {
        const int stride = width + paddingX * 2 + 1;
        std::vector<unsigned char> input(
            static_cast<std::size_t>(stride) * (height + paddingY * 2 + 1)
        );
        for (unsigned char& value : input) {
            value = static_cast<unsigned char>(random());
        }
        std::vector<unsigned char> expected(input.size(), 173);
        std::vector<unsigned char> actual(input.size(), 173);
        runtimeReferenceVerticalShroudBlur(
            input, expected, stride, paddingX, paddingY,
            paddingX + width, paddingY + height
        );
        runtimeSoftenShroudVertically(
            input, actual, stride, paddingX, paddingY,
            paddingX + width, paddingY + height
        );
        if (actual != expected) {
            return false;
        }
        std::vector<unsigned char> expectedHorizontal(input.size(), 173);
        std::vector<unsigned char> actualHorizontal(input.size(), 173);
        runtimeReferenceHorizontalShroudBlur(
            input, expectedHorizontal, stride, paddingX, paddingY,
            paddingX + width, paddingY + height
        );
        runtimeSoftenShroudHorizontally(
            input, actualHorizontal, stride, paddingX, paddingY,
            paddingX + width, paddingY + height
        );
        if (actualHorizontal != expectedHorizontal) {
            return false;
        }
        // Check the complete two-pass blur, including untouched padding,
        // rather than only comparing each independently supplied stage.
        std::fill(expected.begin(), expected.end(), static_cast<unsigned char>(173));
        std::fill(actual.begin(), actual.end(), static_cast<unsigned char>(173));
        runtimeReferenceVerticalShroudBlur(
            expectedHorizontal, expected, stride, paddingX, paddingY,
            paddingX + width, paddingY + height
        );
        runtimeSoftenShroudVertically(
            actualHorizontal, actual, stride, paddingX, paddingY,
            paddingX + width, paddingY + height
        );
        ++cases;
        return actual == expected;
    };
    for (int height : {1, 2, 5, 9, 31, 127}) {
        for (int width : {1, 2, 5, 9, 63, 128}) {
            if (!check(width, height, 3, 2)) {
                error = "shroud blur changed clipped or narrow-surface pixels";
                return false;
            }
        }
    }
    const std::array<std::pair<int, int>, 5> displaySizes{{
        {1280, 720}, {1920, 1080}, {2256, 1504},
        {2560, 1440}, {3840, 2160}
    }};
    for (const auto [width, height] : displaySizes) {
        if (!check(width, height, 0, 0)) {
            error = "shroud blur changed full-display pixels";
            return false;
        }
    }
    // Resize the reused rolling buffer back down after the largest display.
    if (!check(2, 1, 1, 1)) {
        error = "shroud blur retained stale values after resizing";
        return false;
    }
    std::cout << "Shroud blur: exactCases=" << cases
              << " native2256x1504=yes twoPass=exact cropAndPadding=preserved resize=exact\n";
    return true;
}
