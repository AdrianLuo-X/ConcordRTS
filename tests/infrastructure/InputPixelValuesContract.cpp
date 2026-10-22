#include "ui/input/RuntimeInputPixelValues.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(WINAPI)
#error Input pixel values must not transitively include windows.h
#endif

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>

static_assert(std::is_same_v<decltype(RuntimeInputPixelPoint::x), std::int32_t>);
static_assert(std::is_same_v<decltype(RuntimeInputPixelPoint::y), std::int32_t>);
static_assert(std::is_same_v<decltype(RuntimeInputPixelSize::width), std::int32_t>);
static_assert(std::is_same_v<decltype(RuntimeInputPixelSize::height), std::int32_t>);
static_assert(std::numeric_limits<std::int32_t>::is_signed
    && std::numeric_limits<std::int32_t>::digits == 31);
static_assert(std::is_standard_layout_v<RuntimeInputPixelPoint>
    && std::is_trivially_copyable_v<RuntimeInputPixelPoint>
    && std::is_aggregate_v<RuntimeInputPixelPoint>);
static_assert(std::is_standard_layout_v<RuntimeInputPixelSize>
    && std::is_trivially_copyable_v<RuntimeInputPixelSize>
    && std::is_aggregate_v<RuntimeInputPixelSize>);
static_assert(sizeof(RuntimeInputPixelPoint) == 8 && sizeof(RuntimeInputPixelSize) == 8);
static_assert(alignof(RuntimeInputPixelPoint) == alignof(std::int32_t)
    && alignof(RuntimeInputPixelSize) == alignof(std::int32_t));
static_assert(offsetof(RuntimeInputPixelPoint, x) == 0 && offsetof(RuntimeInputPixelPoint, y) == 4);
static_assert(offsetof(RuntimeInputPixelSize, width) == 0 && offsetof(RuntimeInputPixelSize, height) == 4);
static_assert(noexcept(runtimeInputPixelPointFromPackedPosition(0)));
static_assert(noexcept(runtimeInputPixelBoundsMidpoint({}, {})));
static_assert(noexcept(runtimeInputPixelBoundsSize({}, {})));
static_assert(noexcept(runtimeInputPixelCameraOffset({}, {})));
static_assert(runtimeInputPixelPointFromPackedPosition(0xffffffffU).x == -1);
static_assert(runtimeInputPixelBoundsMidpoint({-5, -1}, {0, 0}).x == -2);
static_assert(runtimeInputPixelBoundsMidpoint({-5, -1}, {0, 0}).y == 0);
static_assert(runtimeInputPixelBoundsSize({-3, -4}, {5, 7}).width == 8);
static_assert(runtimeInputPixelCameraOffset({3, 5}, {5, 7}).y == 2);

namespace {
std::size_t checks = 0;
bool expect(bool condition, const char* message) {
    ++checks;
    if (!condition) std::cerr << "Input pixel values contract failed: " << message << '\n';
    return condition;
}
bool point(RuntimeInputPixelPoint value, std::int32_t x, std::int32_t y, const char* message) {
    return expect(value.x == x && value.y == y, message);
}
bool size(RuntimeInputPixelSize value, std::int32_t width, std::int32_t height, const char* message) {
    return expect(value.width == width && value.height == height, message);
}
} // namespace

int runInputPixelPointSizeContract() {
    bool ok = true;
    constexpr auto minimum = std::numeric_limits<std::int32_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int32_t>::max();
    const RuntimeInputPixelPoint defaultPoint;
    const RuntimeInputPixelSize defaultSize;
    ok &= point(defaultPoint, 0, 0, "point default coordinates");
    ok &= size(defaultSize, 0, 0, "size default dimensions");

    // Expected values are explicit; these cases call only production functions.
    ok &= point(runtimeInputPixelPointFromPackedPosition(0), 0, 0, "packed zero");
    ok &= point(runtimeInputPixelPointFromPackedPosition(0x00010002U), 2, 1, "low/high component order");
    ok &= point(runtimeInputPixelPointFromPackedPosition(0x7fff7fffU), 32767, 32767, "signed16 maximum");
    ok &= point(runtimeInputPixelPointFromPackedPosition(0x80008000U), -32768, -32768, "signed16 minimum");
    ok &= point(runtimeInputPixelPointFromPackedPosition(0x80007fffU), 32767, -32768, "opposite signed16 extremes");
    ok &= point(runtimeInputPixelPointFromPackedPosition(0x7fff8000U), -32768, 32767, "reversed signed16 extremes");
    ok &= point(runtimeInputPixelPointFromPackedPosition(0xffffffffU), -1, -1, "all signed16 bits set");
    ok &= point(runtimeInputPixelPointFromPackedPosition(0xffff0000U), 0, -1, "negative high component");
    ok &= point(runtimeInputPixelPointFromPackedPosition(0x0000ffffU), -1, 0, "negative low component");

    ok &= point(runtimeInputPixelBoundsMidpoint({}, {}), 0, 0, "zero midpoint");
    ok &= point(runtimeInputPixelBoundsMidpoint({-5, -1}, {0, 0}), -2, 0, "negative odd sum truncates toward zero");
    ok &= point(runtimeInputPixelBoundsMidpoint({-7, -8}, {2, 3}), -2, -2, "mixed endpoints with negative odd sum");
    ok &= point(runtimeInputPixelBoundsMidpoint({10, -10}, {3, 3}), 6, -3, "positive and negative odd sums");
    ok &= point(runtimeInputPixelBoundsMidpoint({minimum, minimum + 1}, {}), -1073741824, -1073741823,
        "minimum representable sums retain original division");
    ok &= point(runtimeInputPixelBoundsMidpoint({maximum, maximum - 1}, {}), 1073741823, 1073741823,
        "maximum representable sums");
    ok &= point(runtimeInputPixelBoundsMidpoint({minimum, maximum}, {maximum, minimum}), 0, 0,
        "opposing extremes sum to negative one");
    ok &= point(runtimeInputPixelBoundsMidpoint({-3, 8}, {9, -2}), 3, 3, "reversed endpoint order stays literal");

    ok &= size(runtimeInputPixelBoundsSize({}, {}), 0, 0, "zero dimensions");
    ok &= size(runtimeInputPixelBoundsSize({-3, -4}, {5, 7}), 8, 11, "negative bounds origin");
    ok &= size(runtimeInputPixelBoundsSize({5, 7}, {-3, -4}), -8, -11, "reversed bounds keep negative size");
    ok &= size(runtimeInputPixelBoundsSize({}, {minimum, maximum}), minimum, maximum, "extreme representable dimensions");
    ok &= size(runtimeInputPixelBoundsSize({minimum, maximum}, {-1, 0}), maximum, -maximum, "large valid differences");
    ok &= size(runtimeInputPixelBoundsSize({maximum, minimum}, {0, -1}), -maximum, maximum, "opposite large valid differences");
    ok &= size(runtimeInputPixelBoundsSize({minimum, minimum}, {minimum, minimum}), 0, 0, "equal minimum endpoints");

    ok &= point(runtimeInputPixelCameraOffset({}, {}), 0, 0, "zero size clamps before dividing");
    ok &= point(runtimeInputPixelCameraOffset({3, 5}, {-1, minimum}), 3, 5, "negative dimensions clamp to one");
    ok &= point(runtimeInputPixelCameraOffset({3, 5}, {1, 1}), 3, 5, "one pixel divides to zero");
    ok &= point(runtimeInputPixelCameraOffset({3, 5}, {2, 3}), 2, 4, "even/odd positive dimensions");
    ok &= point(runtimeInputPixelCameraOffset({-3, -5}, {5, 7}), -5, -8, "negative projected position");
    ok &= point(runtimeInputPixelCameraOffset({maximum, minimum}, {1, 0}), maximum, minimum, "extreme unchanged projected position");
    ok &= point(runtimeInputPixelCameraOffset({maximum, 0}, {maximum, maximum}), 1073741824, -1073741823,
        "maximum size retains integer half");
    ok &= point(runtimeInputPixelCameraOffset({minimum + 1073741823, minimum + 1073741823},
        {maximum, maximum}), minimum, minimum, "minimum representable final offset");

    std::cout << "ABI point.size=" << sizeof(RuntimeInputPixelPoint)
        << " align=" << alignof(RuntimeInputPixelPoint)
        << " x.offset=" << offsetof(RuntimeInputPixelPoint, x)
        << " y.offset=" << offsetof(RuntimeInputPixelPoint, y) << '\n';
    std::cout << "ABI size.size=" << sizeof(RuntimeInputPixelSize)
        << " align=" << alignof(RuntimeInputPixelSize)
        << " width.offset=" << offsetof(RuntimeInputPixelSize, width)
        << " height.offset=" << offsetof(RuntimeInputPixelSize, height) << '\n';
    if (!ok) return 1;
    std::cout << "Input pixel values contract passed: checks=" << checks
        << " windowsHeader=absent originalOverflowDomain=not-covered\n";
    return 0;
}



static_assert(std::is_same_v<decltype(RuntimeInputPixelRect::left), std::int32_t>);
static_assert(std::is_same_v<decltype(RuntimeInputPixelRect::top), std::int32_t>);
static_assert(std::is_same_v<decltype(RuntimeInputPixelRect::right), std::int32_t>);
static_assert(std::is_same_v<decltype(RuntimeInputPixelRect::bottom), std::int32_t>);
static_assert(std::is_standard_layout_v<RuntimeInputPixelRect>
    && std::is_trivially_copyable_v<RuntimeInputPixelRect>
    && std::is_aggregate_v<RuntimeInputPixelRect>);
static_assert(sizeof(RuntimeInputPixelRect) == 16
    && alignof(RuntimeInputPixelRect) == alignof(std::int32_t));
static_assert(offsetof(RuntimeInputPixelRect, left) == 0
    && offsetof(RuntimeInputPixelRect, top) == 4
    && offsetof(RuntimeInputPixelRect, right) == 8
    && offsetof(RuntimeInputPixelRect, bottom) == 12);
static_assert(noexcept(runtimeInputPixelMapViewport({}, 168)));
static_assert(noexcept(runtimeInputPixelSidebarRect({}, 168)));
static_assert(noexcept(runtimeInputPixelPointInRectInclusive({}, {})));
static_assert(noexcept(runtimeInputPixelRectsIntersect({}, {})));
static_assert(runtimeInputPixelMapViewport({0, 0, 800, 600}, 168).right == 632);
static_assert(runtimeInputPixelSidebarRect({0, 0, 800, 600}, 168).left == 632);
static_assert(runtimeInputPixelPointInRectInclusive({}, {}));
static_assert(runtimeInputPixelRectsIntersect({5, 5, 5, 5}, {0, 0, 10, 10}));
static_assert(runtimeInputPixelRectsIntersect({7, 7, 3, 3}, {0, 0, 10, 10}));

namespace {
std::size_t rectChecks = 0;
bool rectExpect(bool condition, const char* message) {
    ++rectChecks;
    if (!condition) std::cerr << "Input pixel rect contract failed: " << message << '\n';
    return condition;
}
bool rectEquals(RuntimeInputPixelRect value, RuntimeInputPixelRect expected, const char* message) {
    return rectExpect(value.left == expected.left && value.top == expected.top
        && value.right == expected.right && value.bottom == expected.bottom, message);
}
int runInputPixelRectContract() {
    bool ok = true;
    constexpr auto minimum = std::numeric_limits<std::int32_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int32_t>::max();
    const RuntimeInputPixelRect zero;
    ok &= rectEquals(zero, {0, 0, 0, 0}, "default fields must all be zero");
    ok &= rectEquals(runtimeInputPixelMapViewport({0, 0, 800, 600}, 168),
        {0, 0, 632, 600}, "normal map viewport changes only right");
    ok &= rectEquals(runtimeInputPixelSidebarRect({0, 0, 800, 600}, 168),
        {632, 0, 800, 600}, "normal sidebar changes only left");
    const auto viewport = runtimeInputPixelMapViewport({0, 0, 800, 600}, 168);
    const auto sidebar = runtimeInputPixelSidebarRect({0, 0, 800, 600}, 168);
    ok &= rectExpect(runtimeInputPixelPointInRectInclusive(viewport, {632, 100}),
        "shared 632 boundary remains inclusive in map");
    ok &= rectExpect(runtimeInputPixelPointInRectInclusive(sidebar, {632, 100}),
        "shared 632 boundary remains inclusive in sidebar");
    ok &= rectEquals(runtimeInputPixelMapViewport({10, -20, 110, 80}, 168),
        {10, -20, 10, 80}, "client narrower than 168 collapses only map right");
    ok &= rectEquals(runtimeInputPixelSidebarRect({10, -20, 110, 80}, 168),
        {10, -20, 110, 80}, "narrow sidebar remains complete client");
    ok &= rectEquals(runtimeInputPixelMapViewport({10, -20, 178, 80}, 168),
        {10, -20, 10, 80}, "client width exactly 168");
    ok &= rectEquals(runtimeInputPixelSidebarRect({10, -20, 178, 80}, 168),
        {10, -20, 178, 80}, "exact-width sidebar");
    ok &= rectEquals(runtimeInputPixelMapViewport({10, 0, 0, 5}, 168),
        {10, 0, 10, 5}, "reversed client map is not normalized");
    ok &= rectEquals(runtimeInputPixelSidebarRect({10, 0, 0, 5}, 168),
        {10, 0, 0, 5}, "reversed client sidebar stays reversed");
    ok &= rectEquals(runtimeInputPixelMapViewport({}, 168), {}, "zero client map");
    ok &= rectEquals(runtimeInputPixelSidebarRect({}, 168), {}, "zero client sidebar");
    ok &= rectEquals(runtimeInputPixelMapViewport({-800, -600, -100, -200}, 168),
        {-800, -600, -268, -200}, "negative-origin map");
    ok &= rectEquals(runtimeInputPixelSidebarRect({-800, -600, -100, -200}, 168),
        {-268, -600, -100, -200}, "negative-origin sidebar");
    ok &= rectEquals(runtimeInputPixelMapViewport({minimum, -20, minimum + 168, 80}, 168),
        {minimum, -20, minimum, 80}, "first legal right-minus-168 map boundary");
    ok &= rectEquals(runtimeInputPixelSidebarRect({minimum, -20, minimum + 168, 80}, 168),
        {minimum, -20, minimum + 168, 80}, "first legal right-minus-168 sidebar boundary");
    ok &= rectEquals(runtimeInputPixelMapViewport({maximum, minimum, minimum + 168, maximum}, 168),
        {maximum, minimum, maximum, maximum}, "extreme reversed client map retains original max");
    ok &= rectEquals(runtimeInputPixelSidebarRect({maximum, minimum, minimum + 168, maximum}, 168),
        {maximum, minimum, minimum + 168, maximum}, "extreme reversed client sidebar fields");
    ok &= rectEquals(runtimeInputPixelMapViewport({-1, minimum, maximum, maximum}, 168),
        {-1, minimum, 2147483479, maximum}, "maximum right legal map");
    ok &= rectEquals(runtimeInputPixelSidebarRect({-1, minimum, maximum, maximum}, 168),
        {2147483479, minimum, maximum, maximum}, "maximum right legal sidebar");

    const RuntimeInputPixelRect box{0, 0, 10, 10};
    ok &= rectExpect(runtimeInputPixelPointInRectInclusive({}, {}), "zero point in zero rectangle");
    ok &= rectExpect(!runtimeInputPixelPointInRectInclusive({}, {1, 0}), "outside zero rectangle");
    ok &= rectExpect(runtimeInputPixelPointInRectInclusive(box, {0, 5}), "inclusive left edge");
    ok &= rectExpect(runtimeInputPixelPointInRectInclusive(box, {10, 5}), "inclusive right edge");
    ok &= rectExpect(runtimeInputPixelPointInRectInclusive(box, {5, 0}), "inclusive top edge");
    ok &= rectExpect(runtimeInputPixelPointInRectInclusive(box, {5, 10}), "inclusive bottom edge");
    ok &= rectExpect(runtimeInputPixelPointInRectInclusive(box, {0, 0}), "inclusive top-left corner");
    ok &= rectExpect(runtimeInputPixelPointInRectInclusive(box, {10, 10}), "inclusive bottom-right corner");
    ok &= rectExpect(!runtimeInputPixelPointInRectInclusive(box, {-1, 5}), "one pixel outside left");
    ok &= rectExpect(!runtimeInputPixelPointInRectInclusive(box, {11, 5}), "one pixel outside right");
    ok &= rectExpect(!runtimeInputPixelPointInRectInclusive(box, {5, -1}), "one pixel outside top");
    ok &= rectExpect(!runtimeInputPixelPointInRectInclusive(box, {5, 11}), "one pixel outside bottom");
    ok &= rectExpect(!runtimeInputPixelPointInRectInclusive({7, 7, 3, 3}, {5, 5}),
        "reversed point predicate must not normalize");
    ok &= rectExpect(runtimeInputPixelRectsIntersect({5, 5, 5, 5}, box),
        "contained zero-area rectangle is strictly intersecting in original expression");
    ok &= rectExpect(runtimeInputPixelRectsIntersect({7, 7, 3, 3}, box),
        "contained reversed rectangle retains original true result");
    ok &= rectExpect(!runtimeInputPixelRectsIntersect({}, {}), "equal zero rectangles do not intersect");
    ok &= rectExpect(!runtimeInputPixelRectsIntersect(box, {10, 0, 20, 10}), "right-edge contact is false");
    ok &= rectExpect(!runtimeInputPixelRectsIntersect(box, {-10, 0, 0, 10}), "left-edge contact is false");
    ok &= rectExpect(!runtimeInputPixelRectsIntersect(box, {0, 10, 10, 20}), "bottom-edge contact is false");
    ok &= rectExpect(!runtimeInputPixelRectsIntersect(box, {0, -10, 10, 0}), "top-edge contact is false");
    ok &= rectExpect(!runtimeInputPixelRectsIntersect(box, {10, 10, 20, 20}), "corner contact is false");
    ok &= rectExpect(runtimeInputPixelRectsIntersect(box, {1, 1, 9, 9}), "contained positive rectangle");
    const RuntimeInputPixelRect extremes{minimum, minimum, maximum, maximum};
    ok &= rectExpect(runtimeInputPixelPointInRectInclusive(extremes, {minimum, maximum}),
        "predicate compares full signed32 extremes without arithmetic");
    ok &= rectExpect(runtimeInputPixelPointInRectInclusive(extremes, {maximum, minimum}),
        "opposite extreme inclusive corner");
    ok &= rectExpect(runtimeInputPixelRectsIntersect(extremes, {}), "extreme box contains zero-area origin");
    ok &= rectExpect(!runtimeInputPixelRectsIntersect(extremes, {maximum, maximum, maximum, maximum}),
        "maximum zero-area corner only touches");
    ok &= rectExpect(!runtimeInputPixelRectsIntersect(extremes, {minimum, minimum, minimum, minimum}),
        "minimum zero-area corner only touches");

    std::cout << "ABI rect.size=" << sizeof(RuntimeInputPixelRect)
        << " align=" << alignof(RuntimeInputPixelRect)
        << " left.offset=" << offsetof(RuntimeInputPixelRect, left)
        << " top.offset=" << offsetof(RuntimeInputPixelRect, top)
        << " right.offset=" << offsetof(RuntimeInputPixelRect, right)
        << " bottom.offset=" << offsetof(RuntimeInputPixelRect, bottom) << '\n';
    if (!ok) return 1;
    std::cout << "Input pixel rect contract passed: checks=" << rectChecks
        << " sidebarWidth=168 predicateFieldDomain=signed32 viewportOverflowDomain=not-covered\n";
    return 0;
}
} // namespace

int main() {
    const int original = runInputPixelPointSizeContract();
    return original == 0 ? runInputPixelRectContract() : original;
}

