#pragma once

#include "RuntimeRendererEnvironment.h"
#include "RuntimeShadowDibHalf.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

struct RuntimeShadowOpaqueSpan {
    int y = 0;
    int left = 0;
    int right = 0;
};

// A shadow mask is independent of the body image's opaque spans. Call
// invalidate(), or advance sourceRevision, whenever the mask is rewritten in
// place. This includes reload/raster assignment with retained vector capacity.
// An immutable raster needs no content hash or mask scan after preparation.
class RuntimeShadowOpaqueSpanCache {
public:
    static constexpr std::size_t MaximumSpans = 4096;

    void invalidate() noexcept { valid_ = false; }

    bool prepare(std::span<const unsigned char> mask, int width, int height,
        std::uint64_t sourceRevision = 0) {
        if (valid_ && source_ == mask.data() && sourceSize_ == mask.size()
            && width_ == width && height_ == height && revision_ == sourceRevision) {
            return shapeValid_;
        }
        valid_ = false;
        source_ = mask.data();
        sourceSize_ = mask.size();
        width_ = width;
        height_ = height;
        revision_ = sourceRevision;
        shapeValid_ = false;
        hasNonzero_ = false;
        usable_ = false;
        spans_.clear();
        ++rebuilds_;
        if (width <= 0 || height <= 0
            || static_cast<std::size_t>(height)
                > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(width)) {
            valid_ = true;
            return false;
        }
        const auto pixelCount = static_cast<std::size_t>(width) * height;
        if (mask.size() < pixelCount) {
            valid_ = true;
            return false;
        }
        shapeValid_ = true;
        usable_ = true;
        for (int y = 0; y < height; ++y) {
            const auto row = static_cast<std::size_t>(y) * width;
            int x = 0;
            while (x < width) {
                while (x < width && mask[row + x] == 0) ++x;
                const int left = x;
                while (x < width && mask[row + x] != 0) ++x;
                if (left == x) continue;
                hasNonzero_ = true;
                if (spans_.size() == MaximumSpans) {
                    // Unusually fragmented masks retain the original pixel
                    // path; their cached nonempty result still avoids none_of.
                    spans_.clear();
                    usable_ = false;
                    valid_ = true;
                    return true;
                }
                spans_.push_back({y, left, x});
            }
        }
        // The legacy mask contract tests the whole vector, including bytes
        // beyond width*height. Preserve its return value for these inputs.
        if (!hasNonzero_) {
            hasNonzero_ = std::any_of(mask.begin() + pixelCount, mask.end(),
                [](unsigned char value) { return value != 0; });
        }
        valid_ = true;
        return true;
    }

    bool hasNonzero() const noexcept { return shapeValid_ && hasNonzero_; }
    bool usable() const noexcept { return shapeValid_ && usable_; }
    std::span<const RuntimeShadowOpaqueSpan> spans() const noexcept { return spans_; }
    std::uint64_t rebuilds() const noexcept { return rebuilds_; }

private:
    const unsigned char* source_ = nullptr;
    std::size_t sourceSize_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::uint64_t revision_ = 0;
    std::uint64_t rebuilds_ = 0;
    bool valid_ = false;
    bool shapeValid_ = false;
    bool hasNonzero_ = false;
    bool usable_ = false;
    std::vector<RuntimeShadowOpaqueSpan> spans_;
};

inline thread_local int runtimeShadowSpanReferenceOverride = -1;

class RuntimeShadowSpanReferenceScope {
public:
    explicit RuntimeShadowSpanReferenceScope(bool enabled) noexcept
        : previous_(runtimeShadowSpanReferenceOverride) {
        runtimeShadowSpanReferenceOverride = enabled ? 1 : 0;
    }
    ~RuntimeShadowSpanReferenceScope() { runtimeShadowSpanReferenceOverride = previous_; }
    RuntimeShadowSpanReferenceScope(const RuntimeShadowSpanReferenceScope&) = delete;
    RuntimeShadowSpanReferenceScope& operator=(const RuntimeShadowSpanReferenceScope&) = delete;
private:
    int previous_;
};

inline bool runtimeShadowSpanReferenceForRender() {
    if (runtimeShadowSpanReferenceOverride >= 0) return runtimeShadowSpanReferenceOverride != 0;
    const char* value = runtimeRendererEnvironmentValue("CONCORD_SHADOW_SPAN_REFERENCE");
    return value != nullptr && value[0] == '1' && value[1] == '\0';
}

inline std::uint32_t runtimeBlendShadowSpanDib(std::uint32_t pixel,
    unsigned char shadow, bool binaryMask, bool halfBlackFastPath) noexcept {
    if (shadow == 0) return pixel;
    const unsigned int alpha = binaryMask ? 128U
        : std::max(1U, (static_cast<unsigned int>(shadow) * 128U + 127U) / 255U);
    if (halfBlackFastPath && alpha == 128U) return runtimeHalfBlackShadowDib(pixel);
    const unsigned int keep = 255U - alpha;
    const auto channel = [keep](unsigned int value) noexcept {
        return (value * keep + 127U) / 255U;
    };
    // Legacy COLORREF -> DIB packing discards the high byte for every written
    // pixel. Zero-mask pixels remain completely unchanged, including that byte.
    return (channel((pixel >> 16U) & 255U) << 16U)
        | (channel((pixel >> 8U) & 255U) << 8U) | channel(pixel & 255U);
}

// start/end are source-space coordinates, already clipped against viewport
// and destination dimensions. This mirrors the existing indexed-span kernel;
// the caller retains allocation-failure/HDC and nonusable-cache fallbacks.
inline std::size_t runtimeDrawShadowOpaqueSpans(
    const unsigned char* mask, int imageWidth,
    std::span<const RuntimeShadowOpaqueSpan> spans,
    std::uint32_t* destination, int destinationWidth,
    int targetX, int targetY, int startX, int endX, int startY, int endY,
    bool binaryMask, bool halfBlackFastPath
) noexcept {
    if (mask == nullptr || destination == nullptr || imageWidth <= 0
        || destinationWidth <= 0 || startX >= endX || startY >= endY) return 0;
    std::size_t darkened = 0;
    for (const auto& span : spans) {
        if (span.y < startY) continue;
        if (span.y >= endY) break;
        const int left = std::max(startX, span.left);
        const int right = std::min(endX, span.right);
        if (left >= right) continue;
        const auto sourceOffset = static_cast<std::size_t>(span.y) * imageWidth + left;
        const auto targetOffset = static_cast<std::size_t>(targetY + span.y) * destinationWidth
            + static_cast<std::size_t>(targetX + left);
        const auto* source = mask + sourceOffset;
        auto* target = destination + targetOffset;
        for (int x = 0; x < right - left; ++x) {
            target[x] = runtimeBlendShadowSpanDib(target[x], source[x],
                binaryMask, halfBlackFastPath);
        }
        darkened += static_cast<std::size_t>(right - left);
    }
    return darkened;
}
