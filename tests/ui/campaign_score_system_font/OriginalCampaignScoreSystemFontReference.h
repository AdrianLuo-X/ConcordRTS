#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
namespace original_campaign_score_font {
inline std::array<HFONT,4> create(bool originalArt,double verticalScale){
 const auto makeFont=[verticalScale](int size,int weight){
  return CreateFontW(-std::max(10,static_cast<int>(std::lround(size*verticalScale))),
   0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
   ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Arial");
 };
 if(originalArt)return {makeFont(19,FW_BOLD),makeFont(14,FW_NORMAL),makeFont(12,FW_BOLD),makeFont(15,FW_BOLD)};
 return {makeFont(30,FW_BOLD),makeFont(21,FW_BOLD),makeFont(16,FW_NORMAL),makeFont(32,FW_BOLD)};
}
inline void draw(HDC dc,HFONT font,bool originalArt,const std::wstring& value,RECT rect,COLORREF color,UINT format){
 HGDIOBJ old=SelectObject(dc,font);
 if(originalArt){RECT shadow=rect;OffsetRect(&shadow,1,1);SetTextColor(dc,RGB(0,0,0));
  DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&shadow,format);}
 SetTextColor(dc,color);DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&rect,format);SelectObject(dc,old);
}
}
