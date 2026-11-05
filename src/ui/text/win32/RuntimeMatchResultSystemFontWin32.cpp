#include "ui/text/win32/RuntimeMatchResultSystemFontWin32.h"
#include <string>
namespace concord::ui {
static_assert(sizeof(wchar_t)==sizeof(char16_t));
void MatchResultSystemFontWin32::initialize(std::int32_t pixelHeight) {
 font_=CreateFontW(pixelHeight,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
  OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Microsoft YaHei UI");
}
MatchResultSystemFontWin32::~MatchResultSystemFontWin32(){reset();}
void MatchResultSystemFontWin32::reset() noexcept {
 if(font_)DeleteObject(font_);
 font_=nullptr;
}
void MatchResultSystemFontWin32::draw(HDC dc,const MatchResultSystemWideTextRequest& request) const {
 RECT rect{request.rect.left,request.rect.top,request.rect.right,request.rect.bottom};
 HGDIOBJ old=SelectObject(dc,font_);
 SetTextColor(dc,request.packedColor());
 DrawTextW(dc,request.text.empty()?L"":request.text.data(),static_cast<int>(request.text.size()),&rect,static_cast<UINT>(request.format));
 SelectObject(dc,old);
}
void MatchResultSystemFontWin32::draw(HDC dc,const MatchResultSystemTextRequest& request) const {
 const std::wstring text(request.text.begin(),request.text.end());
 draw(dc,MatchResultSystemWideTextRequest::fromPacked(text,request.rect,request.format,request.packedColor()));
}
}
