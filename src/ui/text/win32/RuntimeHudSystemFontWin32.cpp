#include "ui/text/win32/RuntimeHudSystemFontWin32.h"
#include <string>

namespace concord::ui {
static_assert(sizeof(wchar_t) == sizeof(char16_t));
void HudSystemFontWin32::initialize(HudSystemFontRole role) {
    reset();
    switch (role) {
    case HudSystemFontRole::NoticeAndHotkeys:
        font_ = CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
        break;
    case HudSystemFontRole::WaypointLabels:
        font_ = CreateFontW(-14,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
        break;
    case HudSystemFontRole::DiplomacyFallback:
        font_ = CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        break;
    }
}
HudSystemFontWin32::~HudSystemFontWin32() { reset(); }
void HudSystemFontWin32::reset() noexcept {
    if (font_) DeleteObject(font_);
    font_ = nullptr;
}
std::int32_t HudSystemFontWin32::drawText(HDC dc, const HudSystemWideTextRequest& request) const {
    RECT rect{request.rect.left,request.rect.top,request.rect.right,request.rect.bottom};
    return DrawTextW(dc,request.text,request.count,&rect,request.format);
}
std::int32_t HudSystemFontWin32::drawText(HDC dc, const HudSystemTextRequest& request) const {
    if (request.text == nullptr)
        return drawText(dc,HudSystemWideTextRequest{nullptr,request.count,request.rect,request.format});
    const std::u16string_view units = request.count < 0
        ? std::u16string_view(request.text) : std::u16string_view(request.text,static_cast<std::size_t>(request.count));
    const std::wstring text(units.begin(),units.end());
    return drawText(dc,HudSystemWideTextRequest{text.c_str(),request.count,request.rect,request.format});
}
bool HudSystemFontWin32::textOut(HDC dc, std::int32_t x, std::int32_t y, std::wstring_view text) const {
    return TextOutW(dc,x,y,text.data(),static_cast<int>(text.size())) != FALSE;
}
HudSystemTextExtent HudSystemFontWin32::measure(HDC dc, std::wstring_view text) {
    SIZE extent{};
    const bool measured = GetTextExtentPoint32W(dc,text.data(),static_cast<int>(text.size()),&extent) != FALSE;
    return {measured,extent.cx,extent.cy};
}
} // namespace concord::ui
