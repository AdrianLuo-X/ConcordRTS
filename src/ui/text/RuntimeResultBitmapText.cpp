#include "ui/text/RuntimeResultBitmapText.h"
#include <algorithm>
#include <utility>
namespace concord::ui {
namespace {
const unsigned char* glyph(const ResultBitmapFontView& font, std::uint16_t c) {
    if (font.bytes.empty()) return nullptr;
    return concord::assets::westwoodFontGlyph(font.bytes, font.metrics, c,
        concord::assets::WestwoodGlyphFallback::MissingReferenceOnly);
}
template<class Character>
std::int32_t measure(const ResultBitmapFontView& font, std::basic_string_view<Character> text) {
    std::int32_t width = 0;
    for (const auto c : text) {
        const auto* record = glyph(font, static_cast<std::uint16_t>(c));
        width += record != nullptr ? record[0] + 1 : 5;
    }
    return std::max<std::int32_t>(0, width - 1);
}
template<class Character>
void emit(const ResultBitmapFontView& font, const RuntimeInputPixelRect& base,
    std::basic_string<Character> text, bool centered, ResultBitmapTextSink& sink) {
    if (font.bytes.empty() || text.empty()) return;
    const auto width = [&](const std::basic_string<Character>& t) { return measure(font, std::basic_string_view<Character>(t)); };
    if (width(text) > base.right - base.left) {
        const std::basic_string<Character> suffix(3, static_cast<Character>('.'));
        while (!text.empty() && width(text + suffix) > base.right - base.left) text.pop_back();
        text += suffix;
    }
    const std::int32_t startX = base.left + (centered
        ? std::max<std::int32_t>(0, base.right - base.left - width(text)) / 2 : 0);
    const std::int32_t startY = base.top + std::max<std::int32_t>(0, base.bottom - base.top
        - static_cast<std::int32_t>(font.metrics.lineHeight)) / 2;
    sink.beginText(base);
    for (const std::int32_t pass : {1, 0}) {
        sink.beginPass(pass != 0);
        std::int32_t cursor = startX;
        for (const auto c : text) {
            const auto* record = glyph(font, static_cast<std::uint16_t>(c));
            if (record == nullptr) { cursor += 5; continue; }
            for (std::uint32_t y = 0; y < font.metrics.bitmapRows; ++y) {
                const auto* row = record + 1 + static_cast<std::size_t>(y) * font.metrics.stride;
                for (std::int32_t x = 0; x < record[0];) {
                    if (!(row[x / 8] & (0x80 >> (x % 8)))) { ++x; continue; }
                    const std::int32_t left = x++;
                    while (x < record[0] && (row[x / 8] & (0x80 >> (x % 8)))) ++x;
                    sink.fillRun({cursor + left + pass, startY + static_cast<std::int32_t>(y) + pass,
                        cursor + x + pass, startY + static_cast<std::int32_t>(y) + 1 + pass});
                }
            }
            cursor += record[0] + 1;
        }
        sink.endPass();
    }
    sink.endText();
}
}
std::int32_t measureResultBitmapText(const ResultBitmapFontView& font, std::wstring_view text) { return measure(font,text); }
std::int32_t measureResultBitmapText(const ResultBitmapFontView& font, std::u16string_view text) { return measure(font,text); }
void drawResultBitmapText(const ResultBitmapFontView& font, const RuntimeInputPixelRect& base,
    std::wstring text, bool centered, ResultBitmapTextSink& sink) { emit(font,base,std::move(text),centered,sink); }
void drawResultBitmapText(const ResultBitmapFontView& font, const RuntimeInputPixelRect& base,
    std::u16string text, bool centered, ResultBitmapTextSink& sink) { emit(font,base,std::move(text),centered,sink); }
} // namespace concord::ui
