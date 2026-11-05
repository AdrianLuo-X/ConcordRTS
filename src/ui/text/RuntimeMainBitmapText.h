#pragma once
#include "assets/text/WestwoodFontCodec.h"
#include "ui/surface/RuntimeMenuPixelSurface.h"
#include <span>
#include <string_view>

namespace concord::ui {
// Synchronous borrow: reload/reset of the native byte owner invalidates this view.
// Main menu folds a valid UTF16 surrogate pair into one '?'. Skirmish does not.
struct MainBitmapFontView {
    std::span<const unsigned char> bytes;
    concord::assets::WestwoodFontMetrics metrics;
    bool valid() const noexcept {
        return !bytes.empty() && metrics.recordSize > 1 && metrics.lineHeight > 0;
    }
};
struct MainBitmapTextRamp {
    RuntimeRgbColorValue bright = RuntimeRgbColorValue::fromChannels(255, 190, 97);
    RuntimeRgbColorValue middle = RuntimeRgbColorValue::fromChannels(245, 172, 68);
    RuntimeRgbColorValue dark = RuntimeRgbColorValue::fromChannels(97, 59, 4);
};
std::int32_t measureMainBitmapText(const MainBitmapFontView&, std::wstring_view);
std::int32_t measureMainBitmapText(const MainBitmapFontView&, std::u16string_view);
// Original representable signed32 layout/advance domain; half-open clip.
// Invalid font/surface or empty text is a no-op. No file/device/resource owner.
void drawMainBitmapText(const MainBitmapFontView&, MenuPixelSurfaceView,
    std::wstring_view, const RuntimeInputPixelRect&, bool centered,
    bool verticallyCentered, const MainBitmapTextRamp&);
void drawMainBitmapText(const MainBitmapFontView&, MenuPixelSurfaceView,
    std::u16string_view, const RuntimeInputPixelRect&, bool centered,
    bool verticallyCentered, const MainBitmapTextRamp&);
} // namespace concord::ui
