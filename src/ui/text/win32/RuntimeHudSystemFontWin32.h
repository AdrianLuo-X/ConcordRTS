#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "ui/text/RuntimeHudSystemFont.h"
#include <string_view>

namespace concord::ui {
// UI-thread owner only. The canvas keeps its original SaveDC/select/restore
// policy, and explicitly resets at the original DeleteObject position.
class HudSystemFontWin32 {
public:
    HudSystemFontWin32() noexcept = default;
    ~HudSystemFontWin32();
    HudSystemFontWin32(const HudSystemFontWin32&) = delete;
    HudSystemFontWin32& operator=(const HudSystemFontWin32&) = delete;
    void initialize(HudSystemFontRole);
    void reset() noexcept;
    bool available() const noexcept { return font_ != nullptr; }
    HudSystemFontCapability capability() const noexcept { return HudSystemFontCapability::NativeText; }
    HFONT nativeFont() const noexcept { return font_; }
    std::int32_t drawText(HDC, const HudSystemWideTextRequest&) const;
    std::int32_t drawText(HDC, const HudSystemTextRequest&) const;
    bool textOut(HDC, std::int32_t x, std::int32_t y, std::wstring_view) const;
    static HudSystemTextExtent measure(HDC, std::wstring_view);
private:
    HFONT font_ = nullptr;
};
} // namespace concord::ui
