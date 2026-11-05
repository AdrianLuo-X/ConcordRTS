#include "ui/text/RuntimeCampaignSystemFont.h"
#include <iostream>
#include <type_traits>
using namespace concord::ui;
int main(){
 unsigned checks=0,failed=0;auto check=[&](bool b){++checks;if(!b)++failed;};
 static_assert(sizeof(CampaignSystemFontRole)==1);
 static_assert(std::is_standard_layout_v<CampaignSystemTextRequest>);
 static_assert(std::is_trivially_copyable_v<CampaignSystemTextRequest>);
 check(static_cast<unsigned>(CampaignSystemFontRole::Body)==0);
 check(static_cast<unsigned>(CampaignSystemFontRole::Small)==1);
 for(auto c:{0U,0xffffffffU,0x01020304U,0x0312ab77U,0x80fedcbaU})check(CampaignSystemFontColor::fromPacked(c).packed()==c);
 constexpr char16_t text[]{u'A',0,u'B',0xd800,0xdc00};
 CampaignSystemTextRequest r{{text,5},{-7,2,99,118},0x12345678U,CampaignSystemFontColor::fromPacked(0xffa1b2c3U)};
 check(r.text.data()==text&&r.text.size()==5);check(r.rect.left==-7&&r.rect.bottom==118);
 check(r.format==0x12345678U&&r.color.packed()==0xffa1b2c3U);
 CampaignSystemTextRequest empty{};check(empty.text.empty()&&empty.rect.left==0&&empty.format==0);
 std::cout<<"Campaign system font values: checks="<<checks<<" nativeDependencies=0 borrowedUTF16=yes packedColorBitsExact=yes failures="<<failed<<" skipped=0\n";
 return failed?1:0;
}
