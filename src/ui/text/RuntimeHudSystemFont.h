#pragma once

#include "ui/input/RuntimeInputPixelValues.h"
#include <cstdint>

namespace concord::ui {
enum class HudSystemFontRole : std::uint8_t {
    NoticeAndHotkeys, WaypointLabels, DiplomacyFallback
};
enum class HudSystemFontCapability : std::uint8_t { Unsupported, NativeText };

// A synchronous text borrow. Count -1 preserves the original NUL-terminated
// calls; nonnegative counts preserve embedded NULs and UTF16 code units.
// Format is transported unchanged to the selected text provider.
template<class Character> struct BasicHudSystemTextRequest {
    const Character* text = nullptr;
    std::int32_t count = -1;
    RuntimeInputPixelRect rect;
    std::uint32_t format = 0;
};
using HudSystemTextRequest = BasicHudSystemTextRequest<char16_t>;
using HudSystemWideTextRequest = BasicHudSystemTextRequest<wchar_t>;

struct HudSystemTextExtent {
    bool measured = false;
    std::int32_t width = 0, height = 0;
};
} // namespace concord::ui
