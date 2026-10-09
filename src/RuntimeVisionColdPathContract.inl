COLORREF runtimeBlendTint(COLORREF color, COLORREF tint, int alpha);

bool runRuntimeVisionColdPathContractCheck(std::string& error) {
    std::uint32_t randomState = 0x86a7b511U;
    const auto random = [&]() {
        randomState ^= randomState << 13; randomState ^= randomState >> 17;
        randomState ^= randomState << 5; return randomState;
    };
    int cases = 0;
    const auto check = [&](int width, int height, int pattern, int padding) {
        const int stride = width + padding * 2 + 1;
        const int left = padding, top = padding, right = left + width, bottom = top + height;
        std::vector<unsigned char> input(static_cast<std::size_t>(stride) * (height + padding * 2 + 1));
        for (auto& value : input) value = static_cast<unsigned char>(random());
        for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
            unsigned char marker = 0;
            switch (pattern) {
            case 0: break;
            case 1: marker = 255; break;
            case 2: marker = 254; break;
            case 3: marker = random() % 3 == 0 ? 0 : random() % 2 ? 254 : 255; break;
            case 4: marker = 255;
                if ((y == top || y == bottom - 1) && (x == left || x == right - 1)) marker = 254;
                break;
            case 5: marker = x == left + width / 2 ? 254 : random() % 2 ? 255 : 0; break;
            case 6: marker = y == top + height / 2 ? 254 : random() % 2 ? 255 : 0; break;
            default: marker = random() % 2 ? 255 : 0;
                if (x == left + width / 2 && y == top + height / 2) marker = 254;
                break;
            }
            input[static_cast<std::size_t>(y) * stride + x] = marker;
        }
        std::vector<unsigned char> referenceHorizontal(input.size(), 173), expected(input.size(), 173);
        runtimeReferenceHorizontalShroudBlur(input, referenceHorizontal, stride, left, top, right, bottom);
        runtimeReferenceVerticalShroudBlur(referenceHorizontal, expected, stride, left, top, right, bottom);
        // Empty/stale output buffers exercise the no-blur and resized paths.
        std::vector<unsigned char> horizontal, actual;
        runtimeSoftenGeometricShroud(input, horizontal, actual, stride, left, top, right, bottom);
        for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * stride + x;
            if (input[index] == 254 && (actual.size() <= index || actual[index] != expected[index])) return false;
        }
        ++cases;
        return true;
    };
    for (const auto [width, height] : std::array<std::pair<int, int>, 8>{{
            {1, 1}, {2, 5}, {5, 2}, {9, 9}, {17, 31}, {128, 63}, {320, 240}, {2256, 1504}}}) {
        for (int pattern = 0; pattern < 8; ++pattern) {
            if (!check(width, height, pattern, 3)) {
                error = "cold shroud sparse blur differs: " + std::to_string(width) + "x"
                    + std::to_string(height) + " pattern=" + std::to_string(pattern);
                return false;
            }
        }
    }
    const auto& colors = runtimeVisionShroudColors();
    for (int y = -129; y <= 129; ++y) for (int x = -65; x <= 65; ++x) {
        const int waveA = 16 - std::abs(((x + y * 2) & 63) - 32);
        const int waveB = 32 - std::abs(((x * 2 - y) & 127) - 64);
        const int mottle = waveA / 4 + waveB / 8;
        const int lift = std::clamp((mottle + 8) / 6, 0, 2);
        const COLORREF tint = RGB(3 + lift, 6 + lift, 10 + lift);
        const auto& actual = colors[static_cast<std::size_t>(y & 127) * 64 + (x & 63)];
        if (actual.mottle != mottle || actual.color != packRuntimeDibColor(tint)) {
            error = "cold shroud color period differs at signed camera coordinate"; return false;
        }
        const std::uint32_t pixel = random();
        for (int blurred : {0, 1, 17, 128, 211, 219, 220, 254, 255}) {
            const int opacity = std::clamp(blurred + mottle, 0, 250);
            const auto expected = packRuntimeDibColor(runtimeBlendTint(
                colorRefFromRuntimeDibColor(pixel), tint, opacity));
            if (runtimeBlendVisionShroud(pixel, actual.color, opacity) != expected) {
                error = "cold shroud direct DIB blend differs from original COLORREF blend"; return false;
            }
        }
    }
    std::cout << "Cold shroud: sparseBlurExactCases=" << cases
        << " opacityAndNegativeCamera=exact opaqueTMPBlurSkipped=yes\n";
    return true;
}
