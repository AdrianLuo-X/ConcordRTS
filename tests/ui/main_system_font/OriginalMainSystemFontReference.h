#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
namespace original_main_system_font {
struct FontColorRamp {
    COLORREF bright = RGB(255, 190, 97);
    COLORREF middle = RGB(245, 172, 68);
    COLORREF dark = RGB(97, 59, 4);
};
struct MenuState {
 HFONT buttonFont=nullptr,titleFont=nullptr,smallFont=nullptr;
 ~MenuState(){
  if(buttonFont!=nullptr)DeleteObject(buttonFont);
  if(titleFont!=nullptr)DeleteObject(titleFont);
  if(smallFont!=nullptr)DeleteObject(smallFont);
 }
};
void initializeFonts(MenuState& state) {
    state.buttonFont = CreateFontW(
        -17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, FF_DONTCARE, L"Arial Narrow"
    );
    state.titleFont = CreateFontW(
        -16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, FF_DONTCARE, L"Arial Narrow"
    );
    state.smallFont = CreateFontW(
        -14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, FF_DONTCARE, L"Arial Narrow"
    );
}
void drawShadowText(
    HDC dc,
    HFONT font,
    const std::wstring& text,
    RECT rect,
    UINT format,
    const FontColorRamp& colors
) {
    if (text.empty()) return;
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    RECT shadow = rect;
    OffsetRect(&shadow, 1, 1);
    SetTextColor(dc, colors.dark);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &shadow, format);
    SetTextColor(dc, colors.middle);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, format);
    SelectObject(dc, oldFont);
}
}
