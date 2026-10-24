#pragma once
#include <cstdint>
#include <span>

namespace concord::assets {
struct WestwoodFontMetrics {
    std::uint32_t stride = 0;
    std::uint32_t bitmapRows = 0;
    std::uint32_t lineHeight = 0;
    std::uint32_t glyphCount = 0;
    std::uint32_t recordSize = 0;
};
// Short/bad-magic input preserves metrics. A complete recognizable header assigns
// its raw fields even when semantic validation fails (the result-page contract).
// No file I/O, resource selection, ownership, layout, encoding, or color policy.
bool decodeWestwoodFontHeader(std::span<const unsigned char> bytes,
    WestwoodFontMetrics& metrics) noexcept;

enum class WestwoodGlyphFallback {
    MissingReferenceOnly,   // Skirmish/result: invalid width is not a missing mapping.
    AnyUnavailableRecord  // Main menu: an invalid-width glyph also falls back to '?'.
};
// One BMP map code unit. Surrogate pairing/advance/layout stay with the caller.
// Returned record is borrowed until the byte owner resets/reloads its buffer.
const unsigned char* westwoodFontGlyph(std::span<const unsigned char> bytes,
    const WestwoodFontMetrics& metrics, std::uint16_t codeUnit,
    WestwoodGlyphFallback fallback) noexcept;
} // namespace concord::assets
