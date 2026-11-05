#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
namespace original_campaign_system_font {
struct Fonts {
 HFONT body=nullptr,smallFont=nullptr;
 void initialize(){
    body = CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    smallFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
 }
 ~Fonts(){DeleteObject(body);DeleteObject(smallFont);}
 HFONT font(unsigned role) const {return role==0?body:smallFont;}
};
void drawText(
    HDC dc,
    HFONT font,
    const std::wstring& text,
    RECT rect,
    COLORREF color,
    UINT format = DT_LEFT | DT_TOP | DT_NOPREFIX
) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, format);
    SelectObject(dc, oldFont);
}
}
