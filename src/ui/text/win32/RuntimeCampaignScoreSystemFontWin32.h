#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "ui/text/RuntimeCampaignScoreSystemFont.h"
#include <array>
namespace concord::ui {
class CampaignScoreSystemFontsWin32 {
public:
 CampaignScoreSystemFontsWin32()noexcept=default;
 ~CampaignScoreSystemFontsWin32();
 CampaignScoreSystemFontsWin32(const CampaignScoreSystemFontsWin32&)=delete;
 CampaignScoreSystemFontsWin32& operator=(const CampaignScoreSystemFontsWin32&)=delete;
 void initialize(CampaignScoreFontPage,double verticalScale);
 void reset()noexcept;
 CampaignScoreSystemFontCapability capability()const noexcept{return CampaignScoreSystemFontCapability::NativeText;}
 HFONT nativeFont(CampaignScoreFontRole)const noexcept;
 void draw(HDC,const CampaignScoreSystemWideTextRequest&)const;
 void draw(HDC,const CampaignScoreSystemTextRequest&)const;
private:
 std::array<HFONT,4> fonts_{};
 CampaignScoreFontPage page_=CampaignScoreFontPage::Fallback;
 bool initialized_=false;
};
}
