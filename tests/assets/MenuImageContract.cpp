#include "assets/menu/MenuImage.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
int checks = 0;
int failures = 0;
void expect(bool condition, const char* message) {
    ++checks;
    if (!condition) { ++failures; std::cerr << message << '\n'; }
}
std::uint32_t pixel(const concord::assets::MenuImage& image, std::size_t at) {
    const auto* p = image.bgra.data() + at * 4;
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8)
        | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
}
int main() {
    using namespace concord::assets;
    std::vector<RuntimePaletteColor> palette(256);
    palette[0] = {1, 2, 3}; palette[1] = {10, 20, 30};
    palette[2] = {40, 50, 60}; palette[255] = {70, 80, 90};
    std::vector<unsigned char> indices{0, 1, 2, 255};
    MenuIndexedFrameView frame{1, 1, 4, 3, 2, 2, indices};
    MenuImage image;
    expect(makeMenuImage(frame, palette, "archive.mix!frame.shp", true, image), "valid frame");
    expect(image.valid() && image.width == 4 && image.height == 3, "global canvas and offsets");
    expect(image.source == "archive.mix!frame.shp", "exact source");
    const std::array<std::uint32_t, 12> expected{
        0, 0, 0, 0, 0, 0x00010203, 0xff0a141e, 0, 0, 0xff28323c, 0xff46505a, 0};
    for (std::size_t i = 0; i < expected.size(); ++i) expect(pixel(image, i) == expected[i], "top-down BGRA clip/zero padding");
    indices.assign(4, 17);
    expect(pixel(image, 10) == 0xff46505a, "output owns pixels beyond borrowed input change");
    indices = {0, 1, 2, 255}; frame.pixels = indices;
    expect(makeMenuImage(frame, palette, "opaque", false, image), "opaque frame");
    expect(pixel(image, 5) == 0xff010203, "index zero opaque retains palette color");
    frame.offsetX = -1; frame.offsetY = -1; frame.globalWidth = 3; frame.globalHeight = 3;
    expect(makeMenuImage(frame, palette, "clip", true, image), "negative offset clip");
    expect(pixel(image, 0) == 0xff46505a, "clipped source lands at correct pixel");
    for (std::size_t i = 1; i < 9; ++i) expect(pixel(image, i) == 0, "clipped canvas background");
    frame.offsetX = 1; frame.offsetY = 2; frame.globalWidth = 1; frame.globalHeight = 1;
    expect(makeMenuImage(frame, palette, "grow", true, image) && image.width == 3 && image.height == 4, "canvas grows to positive offset bounds");
    palette.resize(255);
    expect(!makeMenuImage(frame, palette, "bad", true, image), "short palette rejected");
    expect(image.width == 0 && image.height == 0 && image.bgra.empty() && image.source.empty(), "rejection resets output before validation");
    palette.resize(256); frame.pixels = std::span(indices).first(3);
    expect(!makeMenuImage(frame, palette, "short", true, image), "short source rejected");
    frame.pixels = indices; frame.width = 0;
    expect(!makeMenuImage(frame, palette, "zero", true, image) && image.bgra.empty(), "zero source rejected");
    frame.width = 2; frame.offsetX = -9; frame.offsetY = -8; frame.globalWidth = -10; frame.globalHeight = -10;
    expect(!makeMenuImage(frame, palette, "offcanvas", true, image), "nonpositive canvas rejected");
    expect(image.width == -7 && image.height == -6 && image.bgra.empty() && image.source.empty(), "old partial dimensions preserved after canvas rejection");
    std::cout << "Menu image pixels: checks=" << checks << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
