#include "ui/text/RuntimeSkirmishBitmapText.h"
#include "ui/drawing/MenuDrawCommands.h"
#include <algorithm>
#include <cstddef>
#include <limits>

namespace concord::ui {
namespace {
const unsigned char* glyph(const SkirmishBitmapFontView& font, std::uint16_t unit) {
    if (!font.valid()) return nullptr;
    return concord::assets::westwoodFontGlyph(font.bytes, font.metrics, unit,
        concord::assets::WestwoodGlyphFallback::MissingReferenceOnly);
}
template<class Character>
std::int32_t measure(const SkirmishBitmapFontView& font, std::basic_string_view<Character> text) {
    std::int32_t width = 0;
    for (const Character character : text) {
        const auto* record = glyph(font, static_cast<std::uint16_t>(character));
        width += record != nullptr ? record[0] + 1 : 5;
    }
    return text.empty() ? 0 : std::max<std::int32_t>(0, width - 1);
}
template<class Character>
void paint(const SkirmishBitmapFontView& font, std::span<unsigned char> pixels,
    std::int32_t width, std::int32_t height, std::basic_string_view<Character> text,
    const RuntimeInputPixelRect& rect, bool centered, bool verticallyCentered,
    RuntimeRgbColorValue color,MenuDrawList* commands=nullptr) {
    if (!font.valid() || text.empty() || width <= 0 || height <= 0) return;
    const auto w = static_cast<std::size_t>(width), h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / 4) return;
    const auto stride = w * 4;
    if (h > std::numeric_limits<std::size_t>::max() / stride || (!commands&&pixels.size() < h * stride)) return;
    const std::int32_t startX = centered
        ? rect.left + std::max<std::int32_t>(0, rect.right - rect.left - measure(font, text)) / 2
        : rect.left;
    const std::int32_t startY = verticallyCentered
        ? rect.top + std::max<std::int32_t>(0, rect.bottom - rect.top
            - static_cast<std::int32_t>(font.metrics.lineHeight)) / 2
        : rect.top;
    const auto drawPass = [&](std::int32_t offsetX, std::int32_t offsetY, RuntimeRgbColorValue passColor) {
        std::int32_t cursorX = startX;
        for (const Character character : text) {
            const auto* record = glyph(font, static_cast<std::uint16_t>(character));
            if (record == nullptr) { cursorX += 5; continue; }
            const std::int32_t glyphWidth = record[0];
            if(commands) {
                MenuDrawCommand c;c.kind=MenuDrawKind::Glyph;
                c.originX=cursorX+offsetX;c.originY=startY+offsetY;
                c.left=std::max({c.originX,rect.left,0});c.top=std::max({c.originY,rect.top,0});
                c.right=std::min({c.originX+glyphWidth,rect.right,width});
                c.bottom=std::min({c.originY+static_cast<std::int32_t>(font.metrics.bitmapRows),rect.bottom,height});
                c.offset=commands->intern({record+1,font.metrics.recordSize-1});c.stride=font.metrics.stride;
                c.color=passColor.blue()|(static_cast<std::uint32_t>(passColor.green())<<8)
                    |(static_cast<std::uint32_t>(passColor.red())<<16)|0xff000000U;
                commands->push(c);cursorX+=glyphWidth+1;continue;
            }
            for (std::uint32_t y = 0; y < font.metrics.bitmapRows; ++y) {
                const auto* row = record + 1U + y * font.metrics.stride;
                for (std::int32_t x = 0; x < glyphWidth; ++x) {
                    if ((row[x >> 3] & (0x80U >> (x & 7))) == 0) continue;
                    const std::int32_t px = cursorX + x + offsetX;
                    const std::int32_t py = startY + static_cast<std::int32_t>(y) + offsetY;
                    if (px < rect.left || px >= rect.right || py < rect.top || py >= rect.bottom
                        || px < 0 || py < 0 || px >= width || py >= height) continue;
                    const auto at = (static_cast<std::size_t>(py) * w + static_cast<std::size_t>(px)) * 4;
                    pixels[at] = passColor.blue(); pixels[at + 1] = passColor.green();
                    pixels[at + 2] = passColor.red(); pixels[at + 3] = 255;
                }
            }
            cursorX += glyphWidth + 1;
        }
    };
    drawPass(1, 1, RuntimeRgbColorValue::fromChannels(20, 12, 0));
    drawPass(0, 0, color);
}
}
std::int32_t measureSkirmishBitmapText(const SkirmishBitmapFontView& font, std::wstring_view text) { return measure(font, text); }
std::int32_t measureSkirmishBitmapText(const SkirmishBitmapFontView& font, std::u16string_view text) { return measure(font, text); }
void recordSkirmishBitmapText(const SkirmishBitmapFontView& font,MenuDrawList& list,
    std::int32_t width,std::int32_t height,std::wstring_view text,const RuntimeInputPixelRect& rect,
    bool center,bool vertical,RuntimeRgbColorValue color) {
    paint(font,{},width,height,text,rect,center,vertical,color,&list);
}
void drawSkirmishBitmapText(const SkirmishBitmapFontView& font, std::span<unsigned char> pixels,
    std::int32_t width, std::int32_t height, std::wstring_view text,
    const RuntimeInputPixelRect& rect, bool center, bool vertical, RuntimeRgbColorValue color) {
    paint(font, pixels, width, height, text, rect, center, vertical, color);
}
void drawSkirmishBitmapText(const SkirmishBitmapFontView& font, std::span<unsigned char> pixels,
    std::int32_t width, std::int32_t height, std::u16string_view text,
    const RuntimeInputPixelRect& rect, bool center, bool vertical, RuntimeRgbColorValue color) {
    paint(font, pixels, width, height, text, rect, center, vertical, color);
}
} // namespace concord::ui
