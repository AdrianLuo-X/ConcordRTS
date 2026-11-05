#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "ui/text/RuntimeMatchResultSystemFont.h"
namespace concord::ui {
class MatchResultSystemFontWin32 {
public:
 MatchResultSystemFontWin32() noexcept=default;
 ~MatchResultSystemFontWin32();
 MatchResultSystemFontWin32(const MatchResultSystemFontWin32&)=delete;
 MatchResultSystemFontWin32& operator=(const MatchResultSystemFontWin32&)=delete;
 void initialize(std::int32_t pixelHeight);
 void reset() noexcept;
 MatchResultSystemFontCapability capability() const noexcept {return MatchResultSystemFontCapability::NativeText;}
 bool available() const noexcept {return font_!=nullptr;}
 HFONT nativeFont() const noexcept {return font_;}
 void draw(HDC,const MatchResultSystemWideTextRequest&) const;
 void draw(HDC,const MatchResultSystemTextRequest&) const;
private:
 HFONT font_=nullptr;
};
}
