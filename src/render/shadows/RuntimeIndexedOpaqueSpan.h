#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

// The caller supplies immutable, nonzero source runs and already transformed
// palettes. Opaque writes deliberately replace all 32 bits, matching the
// generic indexed compositor. No destination reads or pixel-effect branches
// are needed in this specialization.
template<bool MaskedRemap, class Spans>
inline void runtimeDrawIndexedOpaqueSpansImpl(
    const unsigned char* indices, const unsigned char* remapMask,
    int imageWidth, const Spans& spans,
    std::uint32_t* destination, int destinationWidth,
    int targetX, int targetY, int startX, int endX, int startY, int endY,
    const std::uint32_t* palette, const std::uint32_t* remapPalette
) {
    if (startX >= endX || startY >= endY) return;
    for (const auto& span : spans) {
        if (span.y < startY) continue;
        if (span.y >= endY) break;
        const int left = std::max(startX, static_cast<int>(span.left));
        const int right = std::min(endX, static_cast<int>(span.right));
        if (left >= right) continue;
        const auto sourceOffset = static_cast<std::size_t>(span.y) * imageWidth + left;
        const unsigned char* source = indices + sourceOffset;
        std::uint32_t* target = destination
            + (static_cast<std::size_t>(targetY + span.y) * destinationWidth
                + static_cast<std::size_t>(targetX + left));
        const int count = right - left;
        if constexpr (MaskedRemap) {
            const unsigned char* mask = remapMask + sourceOffset;
            for (int index = 0; index < count; ++index)
                target[index] = mask[index] ? remapPalette[source[index]] : palette[source[index]];
        } else {
            for (int index = 0; index < count; ++index) target[index] = palette[source[index]];
        }
    }
}

template<class Spans>
inline void runtimeDrawIndexedOpaqueSpans(
    const unsigned char* indices, const unsigned char* remapMask,
    int imageWidth, const Spans& spans,
    std::uint32_t* destination, int destinationWidth,
    int targetX, int targetY, int startX, int endX, int startY, int endY,
    const std::uint32_t* palette, const std::uint32_t* remapPalette
) {
    if (remapMask) {
        runtimeDrawIndexedOpaqueSpansImpl<true>(indices, remapMask, imageWidth, spans,
            destination, destinationWidth, targetX, targetY, startX, endX, startY, endY, palette, remapPalette);
    } else {
        runtimeDrawIndexedOpaqueSpansImpl<false>(indices, remapMask, imageWidth, spans,
            destination, destinationWidth, targetX, targetY, startX, endX, startY, endY, palette, remapPalette);
    }
}
