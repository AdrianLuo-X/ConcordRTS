#include "ui/text/win32/RuntimeCampaignSystemFontWin32.h"
#include <string>
namespace concord::ui {
static_assert(sizeof(wchar_t)==sizeof(char16_t));
void CampaignSystemFontsWin32::initialize() {
    body_ = CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    small_ = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    initialized_=true;
}
CampaignSystemFontsWin32::~CampaignSystemFontsWin32(){reset();}
void CampaignSystemFontsWin32::reset() noexcept {
 if(!initialized_)return;
 DeleteObject(body_);
 DeleteObject(small_);
 body_=nullptr;small_=nullptr;initialized_=false;
}
HFONT CampaignSystemFontsWin32::nativeFont(CampaignSystemFontRole role) const noexcept {
 switch(role){case CampaignSystemFontRole::Body:return body_;case CampaignSystemFontRole::Small:return small_;}
 return nullptr;
}
void CampaignSystemFontsWin32::draw(HDC dc,CampaignSystemFontRole role,
 const CampaignSystemWideTextRequest& request) const {
 const auto font=nativeFont(role);
 const auto& text=request.text;
 RECT rect{request.rect.left,request.rect.top,request.rect.right,request.rect.bottom};
 const auto color=static_cast<COLORREF>(request.color.packed());
 const auto format=static_cast<UINT>(request.format);
 HGDIOBJ oldFont = SelectObject(dc, font);
 SetBkMode(dc, TRANSPARENT);
 SetTextColor(dc, color);
 DrawTextW(dc, text.empty()?L"":text.data(), static_cast<int>(text.size()), &rect, format);
 SelectObject(dc, oldFont);
}
RuntimeInputPixelRect CampaignSystemFontsWin32::measure(HDC dc,CampaignSystemFontRole role,
 const CampaignSystemWideTextRequest& request) const {
 RECT bounds{request.rect.left,request.rect.top,request.rect.right,request.rect.bottom};
 HGDIOBJ oldFont=SelectObject(dc,nativeFont(role));
 DrawTextW(dc,request.text.empty()?L"":request.text.data(),static_cast<int>(request.text.size()),&bounds,
  static_cast<UINT>(request.format));
 SelectObject(dc,oldFont);
 return {bounds.left,bounds.top,bounds.right,bounds.bottom};
}
void CampaignSystemFontsWin32::draw(HDC dc,CampaignSystemFontRole role,
 const CampaignSystemTextRequest& request) const {
 const std::wstring text(request.text.begin(),request.text.end());
 draw(dc,role,CampaignSystemWideTextRequest{text,request.rect,request.format,request.color});
}
RuntimeInputPixelRect CampaignSystemFontsWin32::measure(HDC dc,CampaignSystemFontRole role,
 const CampaignSystemTextRequest& request) const {
 const std::wstring text(request.text.begin(),request.text.end());
 return measure(dc,role,CampaignSystemWideTextRequest{text,request.rect,request.format,request.color});
}
}
