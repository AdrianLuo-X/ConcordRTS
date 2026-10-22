#include "ui/input/RuntimeInputPixelValues.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(WINAPI)
#error Input pixel header leaked Windows before the native reference adapter
#endif
#if !defined(_WIN32)
#error This oracle requires actual Windows SDK MAKEPOINTS, POINT, SIZE and RECT
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>

static_assert(sizeof(LPARAM) == sizeof(std::uint64_t), "This reference exercises actual x64 LPARAM upper bits");
static_assert(sizeof(LONG) == 4 && std::numeric_limits<LONG>::is_signed);
static_assert(std::is_standard_layout_v<POINT> && std::is_standard_layout_v<SIZE>);
static_assert(sizeof(RuntimeInputPixelPoint) == sizeof(POINT));
static_assert(alignof(RuntimeInputPixelPoint) == alignof(POINT));
static_assert(offsetof(RuntimeInputPixelPoint, x) == offsetof(POINT, x));
static_assert(offsetof(RuntimeInputPixelPoint, y) == offsetof(POINT, y));
static_assert(sizeof(RuntimeInputPixelSize) == sizeof(SIZE));
static_assert(alignof(RuntimeInputPixelSize) == alignof(SIZE));
static_assert(offsetof(RuntimeInputPixelSize, width) == offsetof(SIZE, cx));
static_assert(offsetof(RuntimeInputPixelSize, height) == offsetof(SIZE, cy));

namespace {
std::size_t checks = 0;
bool expect(bool condition, const char* message) {
    ++checks;
    if (!condition) std::cerr << "Input pixel Win32 reference failed: " << message << '\n';
    return condition;
}
bool representable(std::int64_t value) {
    return value >= std::numeric_limits<std::int32_t>::min()
        && value <= std::numeric_limits<std::int32_t>::max();
}
bool definedBounds(const RECT& value) {
    return representable(static_cast<std::int64_t>(value.left) + value.right)
        && representable(static_cast<std::int64_t>(value.top) + value.bottom)
        && representable(static_cast<std::int64_t>(value.right) - value.left)
        && representable(static_cast<std::int64_t>(value.bottom) - value.top);
}
bool compareBounds(const RECT& rect) {
    // Frozen original Win32 expressions. Call only after definedBounds checks.
    const POINT oldMidpoint{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
    const SIZE oldSize{rect.right - rect.left, rect.bottom - rect.top};
    const RuntimeInputPixelPoint a{rect.left, rect.top};
    const RuntimeInputPixelPoint b{rect.right, rect.bottom};
    const auto midpoint = runtimeInputPixelBoundsMidpoint(a, b);
    const auto size = runtimeInputPixelBoundsSize(a, b);
    return expect(midpoint.x == oldMidpoint.x && midpoint.y == oldMidpoint.y,
            "original RECT midpoint differs")
        && expect(size.width == oldSize.cx && size.height == oldSize.cy,
            "original RECT size differs");
}
bool definedCamera(const POINT& projected, const SIZE& size) {
    return representable(static_cast<std::int64_t>(projected.x) - std::max(1L, size.cx) / 2)
        && representable(static_cast<std::int64_t>(projected.y) - std::max(1L, size.cy) / 2);
}
bool compareCamera(const POINT& projected, const SIZE& size) {
    // Frozen original clamp-before-half and signed 32-bit camera arithmetic.
    const POINT oldOffset{
        projected.x - std::max(1L, size.cx) / 2,
        projected.y - std::max(1L, size.cy) / 2
    };
    const auto offset = runtimeInputPixelCameraOffset(
        {projected.x, projected.y}, {size.cx, size.cy});
    return expect(offset.x == oldOffset.x && offset.y == oldOffset.y,
        "original camera offset differs");
}
} // namespace

int runInputPixelPointSizeWin32Reference() {
    std::size_t packedCases = 0, rectangleCases = 0, cameraCases = 0;
    std::size_t excludedOriginalOverflowCases = 0;
    constexpr std::array<std::uint64_t, 4> upperBits{0, 1, 0x12345678ULL, 0xffffffffULL};
    // Both x and y traverse every 16-bit pattern for every upper-32-bit variant.
    // XOR is a permutation, so the second component is independently exhaustive.
    for (const auto upper : upperBits) {
        for (std::uint32_t raw = 0; raw < 65536U; ++raw) {
            const std::uint32_t packed = raw | ((raw ^ 0x55aaU) << 16U);
            const std::uint64_t messageBits = (upper << 32U) | packed;
            LPARAM message = std::bit_cast<LPARAM>(messageBits);
            const POINTS oldPoint = MAKEPOINTS(message);
            const auto point = runtimeInputPixelPointFromPackedPosition(
                static_cast<std::uint32_t>(static_cast<std::uintptr_t>(message)));
            if (!expect(point.x == oldPoint.x && point.y == oldPoint.y,
                    "actual MAKEPOINTS signed components or upper bits differ")) {
                std::cerr << "raw=" << raw << " upper=" << upper << '\n';
                return 1;
            }
            ++packedCases;
        }
    }

    constexpr std::array<LONG, 13> dimensions{
        std::numeric_limits<LONG>::min(), -1000001, -5, -1, 0, 1, 2, 3, 5,
        168, 801, 1000001, std::numeric_limits<LONG>::max()
    };
    for (std::int32_t index = 0; index < 8192; ++index) {
        // Bounded construction keeps every original sum/difference representable.
        const LONG left = (index * 7919) % 2000001 - 1000000;
        const LONG top = (index * 1543) % 2000001 - 1000000;
        const LONG right = left + index % 2003 - 1001;
        const LONG bottom = top + index % 1999 - 999;
        const RECT rect{left, top, right, bottom};
        if (!expect(definedBounds(rect), "generated RECT escaped original arithmetic domain")
            || !compareBounds(rect)) return 1;
        ++rectangleCases;
        const POINT projected{top, left};
        const auto i = static_cast<std::size_t>(index);
        const SIZE size{dimensions[i % dimensions.size()], dimensions[(i * 5 + 3) % dimensions.size()]};
        if (!expect(definedCamera(projected, size), "generated camera escaped original arithmetic domain")
            || !compareCamera(projected, size)) return 1;
        ++cameraCases;
    }

    // Include signed extremes only where the original expressions are defined.
    // Int64 is a test-domain guard, never an alternative production calculation.
    constexpr std::array<LONG, 11> anchors{
        std::numeric_limits<LONG>::min(), std::numeric_limits<LONG>::min() + 1,
        -1073741824, -500001, -1, 0, 1, 500001, 1073741823,
        std::numeric_limits<LONG>::max() - 1, std::numeric_limits<LONG>::max()
    };
    for (const LONG a : anchors) {
        for (const LONG b : anchors) {
            const RECT rect{a, b, b, a};
            if (definedBounds(rect)) {
                if (!compareBounds(rect)) return 1;
                ++rectangleCases;
            } else {
                ++excludedOriginalOverflowCases;
            }
            const POINT projected{a, b};
            for (const LONG extent : dimensions) {
                const SIZE size{extent, extent};
                if (definedCamera(projected, size)) {
                    if (!compareCamera(projected, size)) return 1;
                    ++cameraCases;
                } else {
                    ++excludedOriginalOverflowCases;
                }
            }
        }
    }
    std::cout << "ABI point.size=" << sizeof(RuntimeInputPixelPoint)
        << " align=" << alignof(RuntimeInputPixelPoint)
        << " native.size=" << sizeof(POINT) << " native.align=" << alignof(POINT)
        << " x.offset=" << offsetof(RuntimeInputPixelPoint, x)
        << " native.x.offset=" << offsetof(POINT, x)
        << " y.offset=" << offsetof(RuntimeInputPixelPoint, y)
        << " native.y.offset=" << offsetof(POINT, y) << '\n';
    std::cout << "ABI size.size=" << sizeof(RuntimeInputPixelSize)
        << " align=" << alignof(RuntimeInputPixelSize)
        << " native.size=" << sizeof(SIZE) << " native.align=" << alignof(SIZE)
        << " width.offset=" << offsetof(RuntimeInputPixelSize, width)
        << " native.cx.offset=" << offsetof(SIZE, cx)
        << " height.offset=" << offsetof(RuntimeInputPixelSize, height)
        << " native.cy.offset=" << offsetof(SIZE, cy) << '\n';
    std::cout << "Input pixel Win32 reference passed: checks=" << checks
        << " packedCases=" << packedCases << " componentValues=65536 upper32Variants=" << upperBits.size()
        << " rectangleCases=" << rectangleCases << " cameraCases=" << cameraCases
        << " excludedOriginalOverflowCases=" << excludedOriginalOverflowCases
        << " originalOverflowDomain=not-covered\n";
    return 0;
}



static_assert(std::is_standard_layout_v<RECT>);
static_assert(sizeof(RuntimeInputPixelRect) == sizeof(RECT));
static_assert(alignof(RuntimeInputPixelRect) == alignof(RECT));
static_assert(offsetof(RuntimeInputPixelRect, left) == offsetof(RECT, left));
static_assert(offsetof(RuntimeInputPixelRect, top) == offsetof(RECT, top));
static_assert(offsetof(RuntimeInputPixelRect, right) == offsetof(RECT, right));
static_assert(offsetof(RuntimeInputPixelRect, bottom) == offsetof(RECT, bottom));

namespace {
std::size_t rectReferenceChecks = 0;
bool rectReferenceExpect(bool condition, const char* message) {
    ++rectReferenceChecks;
    if (!condition) std::cerr << "Input pixel rect Win32 reference failed: " << message << '\n';
    return condition;
}
RuntimeInputPixelRect inputRect(const RECT& r) { return {r.left, r.top, r.right, r.bottom}; }
bool sameRect(RuntimeInputPixelRect value, const RECT& expected) {
    return value.left == expected.left && value.top == expected.top
        && value.right == expected.right && value.bottom == expected.bottom;
}
// Frozen four production expressions from the prior Shell, with its actual 168.
// These are test oracles only. The production wrappers use the shared pure owner.
RECT frozenMapViewport(const RECT& clientRect) {
    RECT viewport = clientRect;
    viewport.right = std::max(viewport.left, viewport.right - 168);
    return viewport;
}
RECT frozenSidebarRect(const RECT& clientRect) {
    RECT sidebar = clientRect;
    sidebar.left = std::max(sidebar.left, sidebar.right - 168);
    return sidebar;
}
bool frozenPointInRectInclusive(const RECT& rect, POINT point) {
    return point.x >= rect.left
        && point.x <= rect.right
        && point.y >= rect.top
        && point.y <= rect.bottom;
}
bool frozenRectsIntersect(const RECT& left, const RECT& right) {
    return left.left < right.right
        && left.right > right.left
        && left.top < right.bottom
        && left.bottom > right.top;
}
bool viewportDefined(const RECT& rect) {
    // Int64 is a fixture filter only. Never execute old right-168 in its UB domain.
    return representable(static_cast<std::int64_t>(rect.right) - 168);
}
bool compareViewports(const RECT& rect) {
    return rectReferenceExpect(sameRect(runtimeInputPixelMapViewport(inputRect(rect), 168),
            frozenMapViewport(rect)), "four map viewport fields differ from original")
        && rectReferenceExpect(sameRect(runtimeInputPixelSidebarRect(inputRect(rect), 168),
            frozenSidebarRect(rect)), "four sidebar fields differ from original");
}
bool comparePointPredicate(const RECT& rect, POINT point) {
    return rectReferenceExpect(runtimeInputPixelPointInRectInclusive(inputRect(rect), {point.x, point.y})
        == frozenPointInRectInclusive(rect, point), "full-field inclusive predicate differs");
}
bool compareRectPredicate(const RECT& a, const RECT& b) {
    return rectReferenceExpect(runtimeInputPixelRectsIntersect(inputRect(a), inputRect(b))
        == frozenRectsIntersect(a, b), "full-field strict-overlap predicate differs");
}
int runInputPixelRectWin32Reference() {
    std::size_t viewportCases = 0, pointCases = 0, intersectionCases = 0;
    std::size_t excludedViewportOverflowCases = 0;
    std::uint32_t fixtureBits = 0x792a54edU;
    const auto nextField = [&fixtureBits]() {
        fixtureBits = fixtureBits * 1664525U + 1013904223U;
        return std::bit_cast<LONG>(fixtureBits);
    };
    // Full-width reproducible signed32 fields. No geometry normalization or
    // arithmetic validity filter is imposed on either comparison predicate.
    for (std::size_t i = 0; i < 8192; ++i) {
        const RECT a{nextField(), nextField(), nextField(), nextField()};
        const RECT b{nextField(), nextField(), nextField(), nextField()};
        const POINT p{nextField(), nextField()};
        if (!comparePointPredicate(a, p) || !compareRectPredicate(a, b)) return 1;
        ++pointCases; ++intersectionCases;
        if (viewportDefined(a)) {
            if (!compareViewports(a)) return 1;
            ++viewportCases;
        } else {
            ++excludedViewportOverflowCases;
        }
    }

    constexpr std::array<LONG, 5> anchors{
        std::numeric_limits<LONG>::min(), -1, 0, 1, std::numeric_limits<LONG>::max()
    };
    // Every combination of all eight RECT-pair fields over these extreme anchors.
    // Both predicates have only comparisons: all these fields are in the old domain.
    for (std::uint32_t code = 0; code < 390625U; ++code) {
        auto digits = code;
        std::array<LONG, 8> fields{};
        for (auto& field : fields) { field = anchors[digits % 5U]; digits /= 5U; }
        const RECT a{fields[0], fields[1], fields[2], fields[3]};
        const RECT b{fields[4], fields[5], fields[6], fields[7]};
        if (!compareRectPredicate(a, b)) return 1;
        ++intersectionCases;
    }
    // Every combination of the four RECT and two POINT fields over the same anchors.
    for (std::uint32_t code = 0; code < 15625U; ++code) {
        auto digits = code;
        std::array<LONG, 6> fields{};
        for (auto& field : fields) { field = anchors[digits % 5U]; digits /= 5U; }
        const RECT rect{fields[0], fields[1], fields[2], fields[3]};
        const POINT point{fields[4], fields[5]};
        if (!comparePointPredicate(rect, point)) return 1;
        ++pointCases;
    }
    constexpr auto minimum = std::numeric_limits<LONG>::min();
    constexpr auto maximum = std::numeric_limits<LONG>::max();
    constexpr std::array<LONG, 9> rightEdges{
        minimum, minimum + 1, minimum + 167, minimum + 168, minimum + 169,
        -1, 0, 168, maximum
    };
    for (const LONG left : anchors) {
        for (const LONG top : anchors) {
            for (const LONG bottom : anchors) {
                for (const LONG right : rightEdges) {
                    const RECT rect{left, top, right, bottom};
                    if (viewportDefined(rect)) {
                        if (!compareViewports(rect)) return 1;
                        ++viewportCases;
                    } else {
                        ++excludedViewportOverflowCases;
                    }
                }
            }
        }
    }
    // Authored real client shapes preserve exact boundaries and shared-edge behavior.
    constexpr std::array<RECT, 8> clients{{
        {0, 0, 800, 600}, {10, -20, 110, 80}, {10, -20, 178, 80},
        {10, 0, 0, 5}, {0, 0, 0, 0}, {-800, -600, -100, -200},
        {minimum, -20, minimum + 168, 80}, {maximum, minimum, minimum + 168, maximum}
    }};
    for (const auto& rect : clients) {
        if (!viewportDefined(rect) || !compareViewports(rect)) return 1;
        ++viewportCases;
    }

    std::cout << "ABI rect.size=" << sizeof(RuntimeInputPixelRect)
        << " align=" << alignof(RuntimeInputPixelRect)
        << " native.size=" << sizeof(RECT) << " native.align=" << alignof(RECT)
        << " left.offset=" << offsetof(RuntimeInputPixelRect, left)
        << " native.left.offset=" << offsetof(RECT, left)
        << " top.offset=" << offsetof(RuntimeInputPixelRect, top)
        << " native.top.offset=" << offsetof(RECT, top)
        << " right.offset=" << offsetof(RuntimeInputPixelRect, right)
        << " native.right.offset=" << offsetof(RECT, right)
        << " bottom.offset=" << offsetof(RuntimeInputPixelRect, bottom)
        << " native.bottom.offset=" << offsetof(RECT, bottom) << '\n';
    std::cout << "Input pixel rect Win32 reference passed: checks=" << rectReferenceChecks
        << " viewportCases=" << viewportCases << " pointCases=" << pointCases
        << " intersectionCases=" << intersectionCases
        << " generatedFullWidthCases=8192 rectAnchorCombinations=390625 pointAnchorCombinations=15625"
        << " excludedViewportOverflowCases=" << excludedViewportOverflowCases
        << " sidebarWidth=168 predicateFieldDomain=signed32 viewportOverflowDomain=not-covered\n";
    return 0;
}
} // namespace

int main() {
    const int original = runInputPixelPointSizeWin32Reference();
    return original == 0 ? runInputPixelRectWin32Reference() : original;
}

