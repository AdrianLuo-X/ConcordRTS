#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "ui/text/RuntimeMainSystemFont.h"
namespace concord::ui {
// Stock font is borrowed from Windows, never created, owned or deleted here.
constexpr MainSystemFontCapability mainStockVideoFooterCapability() noexcept {return MainSystemFontCapability::NativeText;}
void drawMainStockVideoFooter(HDC,const MainStockFooterWideTextRequest&);
void drawMainStockVideoFooter(HDC,const MainStockFooterTextRequest&);
}
