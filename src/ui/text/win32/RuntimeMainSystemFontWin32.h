#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "ui/text/RuntimeMainSystemFont.h"
namespace concord::ui {
class MainSystemFontsWin32 {
public:
 MainSystemFontsWin32() noexcept = default;
 ~MainSystemFontsWin32();
 MainSystemFontsWin32(const MainSystemFontsWin32&)=delete;
 MainSystemFontsWin32& operator=(const MainSystemFontsWin32&)=delete;
 void initialize();
 void reset() noexcept;
 MainSystemFontCapability capability() const noexcept {return MainSystemFontCapability::NativeText;}
 bool available(MainSystemFontRole role) const noexcept {return nativeFont(role)!=nullptr;}
 // Borrow only inside this private native boundary. The owner retains deletion.
 HFONT nativeFont(MainSystemFontRole role) const noexcept;
 void draw(HDC,MainSystemFontRole,const MainSystemWideTextRequest&) const;
 void draw(HDC,MainSystemFontRole,const MainSystemTextRequest&) const;
private:
 HFONT buttonFont_=nullptr,titleFont_=nullptr,smallFont_=nullptr;
};
}
