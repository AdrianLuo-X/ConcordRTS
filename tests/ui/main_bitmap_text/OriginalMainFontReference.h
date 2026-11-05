#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "assets/text/WestwoodFontCodec.h"
namespace main_original_reference {
namespace fs=std::filesystem;
std::string pathLabel(const fs::path& path) {
    const auto text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}
struct FontColorRamp {
    COLORREF bright = RGB(255, 190, 97);
    COLORREF middle = RGB(245, 172, 68);
    COLORREF dark = RGB(97, 59, 4);
};
struct WestwoodBitFont {

    std::vector<unsigned char> bytes;
    std::uint32_t stride = 0;
    std::uint32_t bitmapRows = 0;
    std::uint32_t lineHeight = 0;
    std::uint32_t glyphCount = 0;
    std::uint32_t recordSize = 0;
    std::string source;

    bool load(const fs::path& path) {
        *this = {};
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        bytes.assign(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>()
        );
        concord::assets::WestwoodFontMetrics metrics;
        const bool sane = concord::assets::decodeWestwoodFontHeader(bytes, metrics);
        if (!sane) { *this = {}; return false; }
        stride = metrics.stride;
        bitmapRows = metrics.bitmapRows;
        lineHeight = metrics.lineHeight;
        glyphCount = metrics.glyphCount;
        recordSize = metrics.recordSize;
        source = pathLabel(path);
        return true;
    }

    bool valid() const {
        return !bytes.empty() && recordSize > 1 && lineHeight > 0;
    }

    const unsigned char* glyph(wchar_t character) const {
        if (!valid()) return nullptr;
        return concord::assets::westwoodFontGlyph(bytes,
            {stride, bitmapRows, lineHeight, glyphCount, recordSize}, static_cast<std::uint16_t>(character),
            concord::assets::WestwoodGlyphFallback::AnyUnavailableRecord);
    }

    int measure(const std::wstring& text) const {
        int width = 0;
        int glyphs = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            wchar_t character = text[i];
            if (character >= 0xd800 && character <= 0xdbff
                && i + 1 < text.size()
                && text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff) {
                character = L'?';
                ++i;
            }
            const unsigned char* record = glyph(character);
            width += record != nullptr ? record[0] + 1 : 5;
            ++glyphs;
        }
        return glyphs > 0 ? std::max(0, width - 1) : 0;
    }

    void draw(
        unsigned char* pixels,
        int surfaceWidth,
        int surfaceHeight,
        const std::wstring& text,
        const RECT& rect,
        bool centered,
        bool verticallyCentered,
        const FontColorRamp& colors
    ) const {
        if (!valid() || pixels == nullptr || text.empty()) return;
        const int textWidth = measure(text);
        const int startX = centered
            ? rect.left + std::max(0L, rect.right - rect.left - textWidth) / 2
            : rect.left;
        const int startY = verticallyCentered
            ? rect.top + std::max(0L, rect.bottom - rect.top
                - static_cast<LONG>(lineHeight)) / 2
            : rect.top;
        const auto setPixel = [=](int x, int y, COLORREF value) {
            if (x < rect.left || x >= rect.right || y < rect.top || y >= rect.bottom
                || x < 0 || y < 0 || x >= surfaceWidth || y >= surfaceHeight) {
                return;
            }
            const std::size_t at = static_cast<std::size_t>(y * surfaceWidth + x) * 4U;
            pixels[at] = GetBValue(value);
            pixels[at + 1] = GetGValue(value);
            pixels[at + 2] = GetRValue(value);
            pixels[at + 3] = 255;
        };
        const auto glyphPixel = [this](
            const unsigned char* record,
            int glyphWidth,
            int x,
            int y
        ) {
            if (x < 0 || x >= glyphWidth
                || y < 0 || y >= static_cast<int>(bitmapRows)) {
                return false;
            }
            const unsigned char* row = record + 1U
                + static_cast<std::size_t>(y) * stride;
            return (row[x >> 3] & (0x80U >> (x & 7))) != 0;
        };
        const auto drawPass = [
            &
        ](int offsetX, int offsetY, COLORREF passColor, bool highlightsOnly) {
            int cursorX = startX;
            for (std::size_t i = 0; i < text.size(); ++i) {
                wchar_t character = text[i];
                if (character >= 0xd800 && character <= 0xdbff
                    && i + 1 < text.size()
                    && text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff) {
                    character = L'?';
                    ++i;
                }
                const unsigned char* record = glyph(character);
                if (record == nullptr) {
                    cursorX += 5;
                    continue;
                }
                const int glyphWidth = record[0];
                for (std::uint32_t y = 0; y < bitmapRows; ++y) {
                    for (int x = 0; x < glyphWidth; ++x) {
                        if (glyphPixel(
                                record,
                                glyphWidth,
                                x,
                                static_cast<int>(y))
                            && (!highlightsOnly || !glyphPixel(
                                record,
                                glyphWidth,
                                x,
                                static_cast<int>(y) - 1))) {
                            setPixel(
                                cursorX + x + offsetX,
                                startY + static_cast<int>(y) + offsetY,
                                passColor
                            );
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
};
}
