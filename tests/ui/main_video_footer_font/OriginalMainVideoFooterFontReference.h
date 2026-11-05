#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace original_main_video_footer {
inline void draw(HDC dc,const wchar_t* text,RECT footer){
 SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
 SetBkMode(dc,TRANSPARENT);
 SetTextColor(dc,RGB(255,255,255));
 DrawTextW(dc,text,-1,&footer,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
}
}
