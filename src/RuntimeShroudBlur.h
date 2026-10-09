#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

// Split the clamped edge pixels from the interior. The interior keeps the
// same rolling sum while avoiding two clamps and vector metadata reads per pixel.
inline void runtimeSoftenShroudHorizontally(
    const std::vector<unsigned char>& shroudMask,
    std::vector<unsigned char>& horizontalBlur,
    int surfaceWidth,
    int clipLeft,
    int clipTop,
    int clipRight,
    int clipBottom
) {
    constexpr int featherRadius = 4;
    constexpr int featherSampleCount = (featherRadius * 2) + 1;
    if (clipLeft >= clipRight || clipTop >= clipBottom) {
        return;
    }
    const int width = clipRight - clipLeft;
    const int lastX = width - 1;
    const int leftEdgeEnd = std::min(featherRadius, width);
    const int middleEnd = std::max(leftEdgeEnd, width - featherRadius - 1);
    for (int y = clipTop; y < clipBottom; ++y) {
        const unsigned char* input = shroudMask.data()
            + static_cast<std::size_t>(y) * surfaceWidth + clipLeft;
        unsigned char* output = horizontalBlur.data()
            + static_cast<std::size_t>(y) * surfaceWidth + clipLeft;
        int sum = 0;
        for (int sample = -featherRadius; sample <= featherRadius; ++sample) {
            sum += input[std::clamp(sample, 0, lastX)];
        }
        int x = 0;
        for (; x < leftEdgeEnd; ++x) {
            output[x] = static_cast<unsigned char>(
                (sum + featherSampleCount / 2) / featherSampleCount
            );
            sum += static_cast<int>(input[std::min(x + featherRadius + 1, lastX)])
                - input[0];
        }
        for (; x < middleEnd; ++x) {
            output[x] = static_cast<unsigned char>(
                (sum + featherSampleCount / 2) / featherSampleCount
            );
            sum += static_cast<int>(input[x + featherRadius + 1])
                - input[x - featherRadius];
        }
        for (; x < width; ++x) {
            output[x] = static_cast<unsigned char>(
                (sum + featherSampleCount / 2) / featherSampleCount
            );
            sum += static_cast<int>(input[lastX]) - input[x - featherRadius];
        }
    }
}

// Keep the nine-sample vertical shroud blur identical to the original
// column walk, but access source and destination pixels in contiguous rows.
inline void runtimeSoftenShroudVertically(
    const std::vector<unsigned char>& horizontalBlur,
    std::vector<unsigned char>& softenedShroud,
    int surfaceWidth,
    int clipLeft,
    int clipTop,
    int clipRight,
    int clipBottom
) {
    if (clipLeft >= clipRight || clipTop >= clipBottom) {
        return;
    }
    constexpr int featherRadius = 4;
    constexpr int featherSampleCount = (featherRadius * 2) + 1;
    static thread_local std::vector<int> columnSums;
    const std::size_t columnCount =
        static_cast<std::size_t>(clipRight - clipLeft);
    columnSums.assign(columnCount, 0);
    int* const sums = columnSums.data();
    for (int sample = -featherRadius; sample <= featherRadius; ++sample) {
        const int sampleY = std::clamp(
            clipTop + sample, clipTop, clipBottom - 1
        );
        const unsigned char* row = horizontalBlur.data()
            + static_cast<std::size_t>(sampleY) * surfaceWidth + clipLeft;
        for (std::size_t x = 0; x < columnCount; ++x) {
            sums[x] += row[x];
        }
    }
    for (int y = clipTop; y < clipBottom; ++y) {
        unsigned char* output = softenedShroud.data()
            + static_cast<std::size_t>(y) * surfaceWidth + clipLeft;
        const int removeY = std::clamp(
            y - featherRadius, clipTop, clipBottom - 1
        );
        const int addY = std::clamp(
            y + featherRadius + 1, clipTop, clipBottom - 1
        );
        const unsigned char* remove = horizontalBlur.data()
            + static_cast<std::size_t>(removeY) * surfaceWidth + clipLeft;
        const unsigned char* add = horizontalBlur.data()
            + static_cast<std::size_t>(addY) * surfaceWidth + clipLeft;
        for (std::size_t x = 0; x < columnCount; ++x) {
            output[x] = static_cast<unsigned char>(
                (sums[x] + featherSampleCount / 2) / featherSampleCount
            );
            sums[x] += static_cast<int>(add[x]) - remove[x];
        }
    }
}
