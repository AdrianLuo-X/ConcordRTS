#pragma once

#include "assets/text/WestwoodFontCodec.h"
#include "ui/text/RuntimeResultBitmapText.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace concord::ui {

struct RuntimeResultBitmapFontResource {

    std::vector<unsigned char> bytes;
    unsigned stride = 0, rows = 0, height = 0, count = 0, recordSize = 0;


    bool load(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        concord::assets::WestwoodFontMetrics metrics{stride, rows, height, count, recordSize};
        const bool sane = concord::assets::decodeWestwoodFontHeader(bytes, metrics);
        stride = metrics.stride; rows = metrics.bitmapRows; height = metrics.lineHeight;
        count = metrics.glyphCount; recordSize = metrics.recordSize;
        if (!sane) { bytes.clear(); return false; }
        return true;
    }
    const unsigned char* glyph(wchar_t character) const {
        if (bytes.empty()) return nullptr;
        return concord::assets::westwoodFontGlyph(bytes,
            {stride, rows, height, count, recordSize}, static_cast<std::uint16_t>(character),
            concord::assets::WestwoodGlyphFallback::MissingReferenceOnly);
    }
    concord::ui::ResultBitmapFontView view() const {
        return {bytes,{stride,rows,height,count,recordSize}};
    }
    int measure(const std::wstring& text) const {
        return concord::ui::measureResultBitmapText(view(),std::wstring_view(text));
    }
};

template<class Font = RuntimeResultBitmapFontResource>
inline const Font& loadRuntimeResultBitmapFontResource(
    const std::string& modRoot, const std::string& resourceRoot) {
    static std::map<std::string, Font> fonts;
    const std::string key = modRoot + "|" + resourceRoot;
    const auto [entry, inserted] = fonts.try_emplace(key);
    if (inserted) {
        for (const auto& path : {
                std::filesystem::path(modRoot) / "game.fnt",
                std::filesystem::path(resourceRoot) / "GameMD" / "game.fnt",
                std::filesystem::path(resourceRoot) / "Game" / "game.fnt"}) {
            if (entry->second.load(path)) break;
        }
    }
    return entry->second;
}

} // namespace concord::ui
