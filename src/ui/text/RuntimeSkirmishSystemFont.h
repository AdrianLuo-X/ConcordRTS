#pragma once
#include <cstdint>
namespace concord::ui {
enum class SkirmishSystemFontRole : std::uint8_t { Title, Caption, Normal, Small };
// The actual custom canvas requires the original bitmap font. This reports
// the canvas's missing system-text fallback. The original native EDIT uses
// the separately owned Normal font and is not a canvas fallback.
enum class SkirmishCanvasSystemTextSupport : std::uint8_t { BitmapOnly };
constexpr SkirmishCanvasSystemTextSupport skirmishCanvasSystemTextSupport() noexcept {
 return SkirmishCanvasSystemTextSupport::BitmapOnly;
}
}
