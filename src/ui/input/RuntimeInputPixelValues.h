#pragma once

#include <cstdint>

struct RuntimeInputPixelPoint {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

struct RuntimeInputPixelSize {
    std::int32_t width = 0;
    std::int32_t height = 0;
};

// Win32 mouse messages supply two signed 16-bit coordinates in the low 32 bits.
// Decode with representable arithmetic, without implementation-defined narrowing.
constexpr RuntimeInputPixelPoint runtimeInputPixelPointFromPackedPosition(
    std::uint32_t packed) noexcept {
    const auto x = static_cast<std::int32_t>(packed & 0xffffU);
    const auto y = static_cast<std::int32_t>((packed >> 16U) & 0xffffU);
    return {x >= 32768 ? x - 65536 : x, y >= 32768 ? y - 65536 : y};
}

// These retain the original signed 32-bit arithmetic and truncation toward zero.
// The caller's original domain requires each sum, difference, and final camera
// offset to be representable in int32_t. No saturation, exception, or widened
// midpoint algorithm is introduced; the original overflow/UB boundary is unchanged.
constexpr RuntimeInputPixelPoint runtimeInputPixelBoundsMidpoint(
    RuntimeInputPixelPoint a, RuntimeInputPixelPoint b) noexcept {
    return {(a.x + b.x) / 2, (a.y + b.y) / 2};
}

constexpr RuntimeInputPixelSize runtimeInputPixelBoundsSize(
    RuntimeInputPixelPoint a, RuntimeInputPixelPoint b) noexcept {
    return {b.x - a.x, b.y - a.y};
}

constexpr RuntimeInputPixelPoint runtimeInputPixelCameraOffset(
    RuntimeInputPixelPoint projected, RuntimeInputPixelSize size) noexcept {
    const std::int32_t width = size.width > 1 ? size.width : 1;
    const std::int32_t height = size.height > 1 ? size.height : 1;
    return {projected.x - width / 2, projected.y - height / 2};
}


// W002-02-input: existing pixel comparisons and viewport/sidebar expressions.
// These values have no normalization, ownership, native adapter, or world access.
struct RuntimeInputPixelRect {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
};

// Production passes the single existing sidebar width, 168. The original signed
// right-sidebarWidth subtraction must be representable before the max comparison.
// No saturation/exception/empty guard or broader width-domain claim is introduced.
constexpr RuntimeInputPixelRect runtimeInputPixelMapViewport(
    RuntimeInputPixelRect client, std::int32_t sidebarWidth) noexcept {
    const std::int32_t clippedRight = client.right - sidebarWidth;
    client.right = client.left > clippedRight ? client.left : clippedRight;
    return client;
}

constexpr RuntimeInputPixelRect runtimeInputPixelSidebarRect(
    RuntimeInputPixelRect client, std::int32_t sidebarWidth) noexcept {
    const std::int32_t clippedLeft = client.right - sidebarWidth;
    client.left = client.left > clippedLeft ? client.left : clippedLeft;
    return client;
}

// Comparisons alone retain their original full signed32 field domain, including
// zero-area and reversed rectangles. Do not add an empty-rectangle precondition.
constexpr bool runtimeInputPixelPointInRectInclusive(
    RuntimeInputPixelRect rect, RuntimeInputPixelPoint point) noexcept {
    return point.x >= rect.left
        && point.x <= rect.right
        && point.y >= rect.top
        && point.y <= rect.bottom;
}

constexpr bool runtimeInputPixelRectsIntersect(
    RuntimeInputPixelRect left, RuntimeInputPixelRect right) noexcept {
    return left.left < right.right
        && left.right > right.left
        && left.top < right.bottom
        && left.bottom > right.top;
}

