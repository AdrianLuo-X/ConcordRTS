#pragma once
#include "assets/text/WestwoodFontCodec.h"
#include "ui/input/RuntimeInputPixelValues.h"
#include "ui/values/RuntimeColorValues.h"
#include <cstdint>
#include <span>
#include <string_view>

namespace concord::ui {
// Borrowed only for a measure/paint call, invalidated by the byte owner's reload.
// Metrics come from the unique R002 byte codec. No file/resource/device owner.
struct SkirmishBitmapFontView {
    std::span<const unsigned char> bytes;
    concord::assets::WestwoodFontMetrics metrics;
    bool valid() const noexcept {
        return !bytes.empty() && metrics.recordSize > 1 && metrics.lineHeight > 0;
    }
};
// The native wchar path preserves the existing uint16 code-unit projection.
// Portable callers can supply u16 directly, with no copy or aliasing conversion.
// Skirmish intentionally treats each surrogate code unit separately.
std::int32_t measureSkirmishBitmapText(const SkirmishBitmapFontView&, std::wstring_view);
std::int32_t measureSkirmishBitmapText(const SkirmishBitmapFontView&, std::u16string_view);
// Contiguous top-down BGRA borrow for this call. Actual menu passes800x600.
// Invalid/short surface or invalid font is a no-op. Existing rectangular
// arithmetic domain (representable signed32 sums/differences/advance) is retained.
void drawSkirmishBitmapText(const SkirmishBitmapFontView&, std::span<unsigned char>,
    std::int32_t width, std::int32_t height, std::wstring_view,
    const RuntimeInputPixelRect&, bool centered, bool verticallyCentered, RuntimeRgbColorValue);
void drawSkirmishBitmapText(const SkirmishBitmapFontView&, std::span<unsigned char>,
    std::int32_t width, std::int32_t height, std::u16string_view,
    const RuntimeInputPixelRect&, bool centered, bool verticallyCentered, RuntimeRgbColorValue);
} // namespace concord::ui
