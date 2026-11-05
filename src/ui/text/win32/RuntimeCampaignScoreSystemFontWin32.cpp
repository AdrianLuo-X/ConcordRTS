#include "ui/text/win32/RuntimeCampaignScoreSystemFontWin32.h"
#include <algorithm>
#include <cmath>
#include <string>
namespace concord::ui {
static_assert(sizeof(wchar_t)==sizeof(char16_t));
void CampaignScoreSystemFontsWin32::initialize(CampaignScoreFontPage page,double verticalScale){
 reset();page_=page;initialized_=true;
 const std::array<int,4> sizes=page==CampaignScoreFontPage::Fallback
  ?std::array<int,4>{30,21,16,32}:std::array<int,4>{19,14,12,15};
 const std::array<int,4> weights=page==CampaignScoreFontPage::Fallback
  ?std::array<int,4>{FW_BOLD,FW_BOLD,FW_NORMAL,FW_BOLD}:std::array<int,4>{FW_BOLD,FW_NORMAL,FW_BOLD,FW_BOLD};
 for(std::size_t n=0;n<fonts_.size();++n)fonts_[n]=CreateFontW(
  -std::max(10,static_cast<int>(std::lround(sizes[n]*verticalScale))),
  0,0,0,weights[n],FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
  CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Arial");
}
CampaignScoreSystemFontsWin32::~CampaignScoreSystemFontsWin32(){reset();}
void CampaignScoreSystemFontsWin32::reset()noexcept{
 if(!initialized_)return;
 for(auto& font:fonts_){DeleteObject(font);font=nullptr;}
 initialized_=false;
}
HFONT CampaignScoreSystemFontsWin32::nativeFont(CampaignScoreFontRole role)const noexcept{
 switch(role){
 case CampaignScoreFontRole::Title:return fonts_[0];
 case CampaignScoreFontRole::Heading:return page_==CampaignScoreFontPage::Fallback?fonts_[1]:nullptr;
 case CampaignScoreFontRole::Body:return page_==CampaignScoreFontPage::Fallback?fonts_[2]:nullptr;
 case CampaignScoreFontRole::Number:return page_==CampaignScoreFontPage::Fallback?fonts_[3]:nullptr;
 case CampaignScoreFontRole::Message:return page_==CampaignScoreFontPage::OriginalArt?fonts_[1]:nullptr;
 case CampaignScoreFontRole::Label:return page_==CampaignScoreFontPage::OriginalArt?fonts_[2]:nullptr;
 case CampaignScoreFontRole::Value:return page_==CampaignScoreFontPage::OriginalArt?fonts_[3]:nullptr;
 }
 return nullptr;
}
void CampaignScoreSystemFontsWin32::draw(HDC dc,const CampaignScoreSystemWideTextRequest& request)const{
 RECT rect{request.rect.left,request.rect.top,request.rect.right,request.rect.bottom};
 HGDIOBJ old=SelectObject(dc,nativeFont(request.role));
 if(page_==CampaignScoreFontPage::OriginalArt){
  RECT shadow=rect;OffsetRect(&shadow,1,1);SetTextColor(dc,RGB(0,0,0));
  DrawTextW(dc,request.text.empty()?L"":request.text.data(),static_cast<int>(request.text.size()),&shadow,request.format);
 }
 SetTextColor(dc,request.packedColor());
 DrawTextW(dc,request.text.empty()?L"":request.text.data(),static_cast<int>(request.text.size()),&rect,request.format);
 SelectObject(dc,old);
}
void CampaignScoreSystemFontsWin32::draw(HDC dc,const CampaignScoreSystemTextRequest& request)const{
 const std::wstring text(request.text.begin(),request.text.end());
 draw(dc,CampaignScoreSystemWideTextRequest::fromPacked(request.role,text,request.rect,request.format,request.packedColor()));
}
}
