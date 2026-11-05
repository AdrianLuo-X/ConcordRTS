#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <string>
namespace original_match_result_font {
inline HFONT create(std::int32_t pixelHeight) {
 return CreateFontW(pixelHeight,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
  OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Microsoft YaHei UI");
}
inline void draw(HDC dc,HFONT font,const std::wstring& text,RECT rect,COLORREF color,UINT format) {
 HGDIOBJ old=SelectObject(dc,font);
 SetTextColor(dc,color);
 DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&rect,format);
 SelectObject(dc,old);
}
}
