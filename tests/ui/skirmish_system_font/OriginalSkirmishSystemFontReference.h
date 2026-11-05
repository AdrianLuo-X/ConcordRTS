#pragma once
#include "ui/skirmish/win32/RuntimeSkirmishBackBufferWin32.h"
namespace original_skirmish_system_font {
using concord::ui::win32::SkirmishGdiReleaseTrace;
struct MenuState {
 HFONT titleFont=nullptr,captionFont=nullptr,normalFont=nullptr,smallFont=nullptr;
 SkirmishGdiReleaseTrace* gdiReleaseTrace=nullptr;
 ~MenuState(){
        if (titleFont != nullptr) {
            const BOOL disposed = DeleteObject(titleFont);
            if (gdiReleaseTrace) gdiReleaseTrace->record(3, disposed != FALSE);
        }
        if (captionFont != nullptr) {
            const BOOL disposed = DeleteObject(captionFont);
            if (gdiReleaseTrace) gdiReleaseTrace->record(4, disposed != FALSE);
        }
        if (normalFont != nullptr) {
            const BOOL disposed = DeleteObject(normalFont);
            if (gdiReleaseTrace) gdiReleaseTrace->record(5, disposed != FALSE);
        }
        if (smallFont != nullptr) {
            const BOOL disposed = DeleteObject(smallFont);
            if (gdiReleaseTrace) gdiReleaseTrace->record(6, disposed != FALSE);
        }
 }
};
void initializeFonts(MenuState& state) {
    state.titleFont = CreateFontW(
        -26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI"
    );
    state.captionFont = CreateFontW(
        -14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI"
    );
    state.normalFont = CreateFontW(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI"
    );
    state.smallFont = CreateFontW(
        -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI"
    );
}
}
