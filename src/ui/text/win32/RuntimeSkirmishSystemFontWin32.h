#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "ui/text/RuntimeSkirmishSystemFont.h"
namespace concord::ui::win32 {
struct SkirmishGdiReleaseTrace;
// Resource ownership only: preserve the actual optional nativeFonts/prewarm
// condition and original creation/use threads. Current shell text is bitmap.
class SkirmishSystemFontsWin32 {
public:
 SkirmishSystemFontsWin32() noexcept = default;
 ~SkirmishSystemFontsWin32();
 SkirmishSystemFontsWin32(const SkirmishSystemFontsWin32&)=delete;
 SkirmishSystemFontsWin32& operator=(const SkirmishSystemFontsWin32&)=delete;
 void initialize();
 void bindNativeControl(HWND,SkirmishSystemFontRole) const noexcept;
 void reset(SkirmishGdiReleaseTrace* trace=nullptr) noexcept;
 SkirmishCanvasSystemTextSupport canvasTextSupport() const noexcept {return skirmishCanvasSystemTextSupport();}
 bool available(SkirmishSystemFontRole role) const noexcept {return nativeFont(role)!=nullptr;}
 // Private legacy/diagnostic borrow only; owner retains lifetime and deletion.
 HFONT nativeFont(SkirmishSystemFontRole role) const noexcept;
private:
 HFONT titleFont_=nullptr,captionFont_=nullptr,normalFont_=nullptr,smallFont_=nullptr;
};
}
