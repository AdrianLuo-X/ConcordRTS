#include "ui/text/win32/RuntimeMainVideoFooterFontWin32.h"
#include <string>
#include <string_view>
namespace concord::ui {
static_assert(sizeof(wchar_t)==sizeof(char16_t));
void drawMainStockVideoFooter(HDC dc,const MainStockFooterWideTextRequest& request){
 RECT footer{request.rect.left,request.rect.top,request.rect.right,request.rect.bottom};
 SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
 SetBkMode(dc,TRANSPARENT);
 SetTextColor(dc,RGB(255,255,255));
 DrawTextW(dc,request.nulTerminatedText?request.nulTerminatedText:L"",-1,&footer,
  DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
}
void drawMainStockVideoFooter(HDC dc,const MainStockFooterTextRequest& request){
 const std::u16string_view input=request.nulTerminatedText?request.nulTerminatedText:u"";
 const std::wstring text(input.begin(),input.end());
 drawMainStockVideoFooter(dc,MainStockFooterWideTextRequest{text.c_str(),request.rect});
}
}
