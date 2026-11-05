#include "ui/text/win32/RuntimeMainSystemFontWin32.h"
#include <string>
namespace concord::ui {
static_assert(sizeof(wchar_t)==sizeof(char16_t));
void MainSystemFontsWin32::initialize() {
    buttonFont_ = CreateFontW(
        -17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, FF_DONTCARE, L"Arial Narrow"
    );
    titleFont_ = CreateFontW(
        -16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, FF_DONTCARE, L"Arial Narrow"
    );
    smallFont_ = CreateFontW(
        -14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, FF_DONTCARE, L"Arial Narrow"
    );
}
MainSystemFontsWin32::~MainSystemFontsWin32(){reset();}
void MainSystemFontsWin32::reset() noexcept {
 if(buttonFont_!=nullptr)DeleteObject(buttonFont_);
 if(titleFont_!=nullptr)DeleteObject(titleFont_);
 if(smallFont_!=nullptr)DeleteObject(smallFont_);
 buttonFont_=nullptr;titleFont_=nullptr;smallFont_=nullptr;
}
HFONT MainSystemFontsWin32::nativeFont(MainSystemFontRole role) const noexcept {
 switch(role){case MainSystemFontRole::Button:return buttonFont_;
 case MainSystemFontRole::Title:return titleFont_;
 case MainSystemFontRole::Small:return smallFont_;}
 return nullptr;
}
void MainSystemFontsWin32::draw(HDC dc,MainSystemFontRole role,
 const MainSystemWideTextRequest& request) const {
    const auto& text=request.text;
    const auto colors=request.colors;
    RECT rect{request.rect.left,request.rect.top,request.rect.right,request.rect.bottom};
    const auto format=static_cast<UINT>(request.format);
    const auto font=nativeFont(role);
    if (text.empty()) return;
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    RECT shadow = rect;
    OffsetRect(&shadow, 1, 1);
    SetTextColor(dc, colors.packedDark());
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &shadow, format);
    SetTextColor(dc, colors.packedMiddle());
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &rect, format);
    SelectObject(dc, oldFont);
}
void MainSystemFontsWin32::draw(HDC dc,MainSystemFontRole role,
 const MainSystemTextRequest& request) const {
 const std::wstring text(request.text.begin(),request.text.end());
 draw(dc,role,MainSystemWideTextRequest{text,request.rect,request.format,request.colors});
}
}
