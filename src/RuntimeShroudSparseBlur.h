#pragma once

#include "RuntimeShroudBlur.h"

#include <cstdint>

// TMP-owned shroud (255) is always opaque and uncovered pixels (0) are never
// shaded. Only geometric shroud (254) consumes a blurred value. Retain both
// nine-sample rounding steps and clamp against the original viewport edges.
inline void runtimeSoftenGeometricShroud(const std::vector<unsigned char>& mask,
    std::vector<unsigned char>& horizontal, std::vector<unsigned char>& softened,
    int stride, int left, int top, int right, int bottom) {
    if (left >= right || top >= bottom) return;
    struct Span { int left, right; };
    static thread_local std::vector<Span> targets, requiredRows;
    const int height = bottom - top;
    targets.assign(static_cast<std::size_t>(height), Span{right, left});
    std::size_t targetSpanPixels = 0;
    for (int y = top; y < bottom; ++y) {
        auto& span = targets[static_cast<std::size_t>(y - top)];
        const auto* row = mask.data() + static_cast<std::size_t>(y) * stride;
        for (int x = left; x < right; ++x) if (row[x] == 254) {
            span.left = std::min(span.left, x); span.right = x + 1;
        }
        if (span.left < span.right) targetSpanPixels += span.right - span.left;
    }
    if (targetSpanPixels == 0) return;
    const std::size_t area = static_cast<std::size_t>(right - left) * height;
    // Nine vertical reads per sparse output beat a full rolling pass only
    // when output spans are small. Dense/custom terrain keeps the old kernel.
    if (targetSpanPixels > area / 8) {
        horizontal.resize(mask.size()); softened.resize(mask.size());
        runtimeSoftenShroudHorizontally(mask, horizontal, stride, left, top, right, bottom);
        runtimeSoftenShroudVertically(horizontal, softened, stride, left, top, right, bottom);
        return;
    }
    horizontal.resize(mask.size()); softened.resize(mask.size());
    requiredRows.assign(static_cast<std::size_t>(height), Span{right, left});
    for (int row = 0; row < height; ++row) {
        const Span target = targets[static_cast<std::size_t>(row)];
        if (target.left >= target.right) continue;
        for (int dy = -4; dy <= 4; ++dy) {
            auto& required = requiredRows[static_cast<std::size_t>(std::clamp(row + dy, 0, height - 1))];
            required.left = std::min(required.left, target.left);
            required.right = std::max(required.right, target.right);
        }
    }
    for (int y = top; y < bottom; ++y) {
        const Span required = requiredRows[static_cast<std::size_t>(y - top)];
        if (required.left >= required.right) continue;
        const auto* input = mask.data() + static_cast<std::size_t>(y) * stride;
        auto* output = horizontal.data() + static_cast<std::size_t>(y) * stride;
        int sum = 0;
        for (int dx = -4; dx <= 4; ++dx) sum += input[std::clamp(required.left + dx, left, right - 1)];
        for (int x = required.left; x < required.right; ++x) {
            output[x] = static_cast<unsigned char>((sum + 4) / 9);
            sum += static_cast<int>(input[std::min(x + 5, right - 1)])
                - input[std::max(x - 4, left)];
        }
    }
    for (int y = top; y < bottom; ++y) {
        const Span target = targets[static_cast<std::size_t>(y - top)];
        auto* output = softened.data() + static_cast<std::size_t>(y) * stride;
        for (int x = target.left; x < target.right; ++x) {
            if (mask[static_cast<std::size_t>(y) * stride + x] != 254) continue;
            int sum = 0;
            for (int dy = -4; dy <= 4; ++dy)
                sum += horizontal[static_cast<std::size_t>(std::clamp(y + dy, top, bottom - 1)) * stride + x];
            output[x] = static_cast<unsigned char>((sum + 4) / 9);
        }
    }
}
