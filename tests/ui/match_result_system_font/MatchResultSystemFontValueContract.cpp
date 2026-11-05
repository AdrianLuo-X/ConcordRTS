#include "ui/text/RuntimeMatchResultSystemFont.h"
#include <iostream>
#include <type_traits>
using namespace concord::ui;
int main(){
 unsigned checks=0,failed=0;auto check=[&](bool v){++checks;if(!v)++failed;};
 static_assert(std::is_standard_layout_v<MatchResultSystemTextRequest>);
 static_assert(std::is_trivially_copyable_v<MatchResultSystemTextRequest>);
 constexpr char16_t text[]{u'A',0,u'B',0xd800,0xdc00};
 for(auto c:{0U,0xffffffffU,0x01020304U,0x0312ab77U,0x80fedcbaU}){
  auto r=MatchResultSystemTextRequest::fromPacked({text,5},{-7,2,99,118},0x12345678U,c);
  check(r.packedColor()==c);check(r.text.data()==text&&r.text.size()==5);
  check(r.rect.left==-7&&r.rect.bottom==118&&r.format==0x12345678U);
 }
 MatchResultSystemTextRequest empty{};check(empty.text.empty()&&empty.rect.left==0&&empty.format==0&&empty.packedColor()==0);
 check(static_cast<unsigned>(MatchResultSystemFontCapability::Unsupported)==0);
 std::cout<<"Match result system font values: checks="<<checks<<" nativeDependencies=0 borrowedUTF16=yes packedColorBitsExact=yes failures="<<failed<<" skipped=0\n";
 return failed?1:0;
}
