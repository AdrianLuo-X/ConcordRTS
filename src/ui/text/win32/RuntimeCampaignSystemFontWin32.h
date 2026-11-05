#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "ui/text/RuntimeCampaignSystemFont.h"
namespace concord::ui {
class CampaignSystemFontsWin32 {
public:
 CampaignSystemFontsWin32() noexcept=default;
 ~CampaignSystemFontsWin32();
 CampaignSystemFontsWin32(const CampaignSystemFontsWin32&)=delete;
 CampaignSystemFontsWin32& operator=(const CampaignSystemFontsWin32&)=delete;
 void initialize();
 void reset() noexcept;
 CampaignSystemFontCapability capability() const noexcept {return CampaignSystemFontCapability::NativeText;}
 bool available(CampaignSystemFontRole role) const noexcept {return nativeFont(role)!=nullptr;}
 HFONT nativeFont(CampaignSystemFontRole) const noexcept;
 void draw(HDC,CampaignSystemFontRole,const CampaignSystemWideTextRequest&) const;
 void draw(HDC,CampaignSystemFontRole,const CampaignSystemTextRequest&) const;
 RuntimeInputPixelRect measure(HDC,CampaignSystemFontRole,const CampaignSystemWideTextRequest&) const;
 RuntimeInputPixelRect measure(HDC,CampaignSystemFontRole,const CampaignSystemTextRequest&) const;
private:
 HFONT body_=nullptr,small_=nullptr;
 bool initialized_=false;
};
}
