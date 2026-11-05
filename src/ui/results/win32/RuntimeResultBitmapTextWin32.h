#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "ui/text/RuntimeResultBitmapText.h"
namespace concord::ui {
// Existing R029 scaling owner is borrowed synchronously, never copied here.
using ResultBitmapScaleRectWin32 = RECT(*)(const RECT&,const RECT&);
void drawResultBitmapTextWin32(const ResultBitmapFontView&, HDC, const RECT& client,
    RECT base, std::wstring text, COLORREF, bool centered, ResultBitmapScaleRectWin32);
} // namespace concord::ui
