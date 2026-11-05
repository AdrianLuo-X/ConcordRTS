#include "ui/text/RuntimeMainBitmapText.h"
#include <algorithm>
#include <cstddef>

namespace concord::ui {
namespace {
const unsigned char* glyph(const MainBitmapFontView& font, std::uint16_t character) {
    if (!font.valid()) return nullptr;
    return concord::assets::westwoodFontGlyph(font.bytes, font.metrics, character,
        concord::assets::WestwoodGlyphFallback::AnyUnavailableRecord);
}
template<class Character>
std::uint16_t nextUnit(std::basic_string_view<Character> text, std::size_t& i) {
    const auto character = text[i];
    if (character >= 0xd800 && character <= 0xdbff && i + 1 < text.size()) {
        const auto following = text[i + 1];
        if (following >= 0xdc00 && following <= 0xdfff) { ++i; return u'?'; }
    }
    return static_cast<std::uint16_t>(character);
}
template<class Character>
std::int32_t measure(const MainBitmapFontView& font, std::basic_string_view<Character> text) {
    std::int32_t width = 0;
    bool hasGlyph = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto* record = glyph(font, nextUnit(text, i));
        width += record != nullptr ? record[0] + 1 : 5;
        hasGlyph = true;
    }
    return hasGlyph ? std::max<std::int32_t>(0, width - 1) : 0;
}
template<class Character>
void paint(const MainBitmapFontView& font, MenuPixelSurfaceView surface,
    std::basic_string_view<Character> text, const RuntimeInputPixelRect& rect,
    bool centered, bool verticallyCentered, const MainBitmapTextRamp& colors) {
    if (!font.valid() || !surface.valid() || text.empty()) return;
    const std::int32_t textWidth = measure(font, text);
    const std::int32_t startX = centered
        ? rect.left + std::max<std::int32_t>(0, rect.right - rect.left - textWidth) / 2
        : rect.left;
    const std::int32_t startY = verticallyCentered
        ? rect.top + std::max<std::int32_t>(0, rect.bottom - rect.top
            - static_cast<std::int32_t>(font.metrics.lineHeight)) / 2
        : rect.top;
    const auto setPixel = [&](std::int32_t x, std::int32_t y, RuntimeRgbColorValue value) {
        if (x < rect.left || x >= rect.right || y < rect.top || y >= rect.bottom
            || x < 0 || y < 0 || x >= surface.width || y >= surface.height) return;
        const auto at = static_cast<std::size_t>(y) * surface.rowStride
            + static_cast<std::size_t>(x) * 4U;
        surface.bytes[at] = value.blue(); surface.bytes[at + 1] = value.green();
        surface.bytes[at + 2] = value.red(); surface.bytes[at + 3] = 255;
    };
    const auto glyphPixel = [&](const unsigned char* record, std::int32_t glyphWidth,
        std::int32_t x, std::int32_t y) {
        if (x < 0 || x >= glyphWidth || y < 0
            || y >= static_cast<std::int32_t>(font.metrics.bitmapRows)) return false;
        const auto* row = record + 1U + static_cast<std::size_t>(y) * font.metrics.stride;
        return (row[x >> 3] & (0x80U >> (x & 7))) != 0;
    };
    const auto drawPass = [&](std::int32_t offsetX, std::int32_t offsetY,
        RuntimeRgbColorValue passColor, bool highlightsOnly) {
        std::int32_t cursorX = startX;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const auto* record = glyph(font, nextUnit(text, i));
            if (record == nullptr) { cursorX += 5; continue; }
            const std::int32_t glyphWidth = record[0];
            for (std::uint32_t y = 0; y < font.metrics.bitmapRows; ++y) {
                for (std::int32_t x = 0; x < glyphWidth; ++x) {
                    if (glyphPixel(record, glyphWidth, x, static_cast<std::int32_t>(y))
                        && (!highlightsOnly || !glyphPixel(record, glyphWidth, x,
                            static_cast<std::int32_t>(y) - 1))) {
                        setPixel(cursorX + x + offsetX,
                            startY + static_cast<std::int32_t>(y) + offsetY, passColor);
                    }
                }
            }
            cursorX += glyphWidth + 1;
        }
    };
    drawPass(1, 1, colors.dark, false);
    drawPass(0, 0, colors.middle, false);
    drawPass(0, 0, colors.bright, true);
}
}
std::int32_t measureMainBitmapText(const MainBitmapFontView& font, std::wstring_view text) { return measure(font, text); }
std::int32_t measureMainBitmapText(const MainBitmapFontView& font, std::u16string_view text) { return measure(font, text); }
void drawMainBitmapText(const MainBitmapFontView& font, MenuPixelSurfaceView surface,
    std::wstring_view text, const RuntimeInputPixelRect& rect, bool center,
    bool vertical, const MainBitmapTextRamp& colors) { paint(font, surface, text, rect, center, vertical, colors); }
void drawMainBitmapText(const MainBitmapFontView& font, MenuPixelSurfaceView surface,
    std::u16string_view text, const RuntimeInputPixelRect& rect, bool center,
    bool vertical, const MainBitmapTextRamp& colors) { paint(font, surface, text, rect, center, vertical, colors); }
} // namespace concord::ui
