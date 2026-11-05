#include "ui/text/RuntimeMainSystemFont.h"
#include <iostream>
#include <type_traits>
using namespace concord::ui;
int main(){
 static_assert(std::is_standard_layout_v<MainStockFooterTextRequest>);
 static_assert(std::is_trivially_copyable_v<MainStockFooterTextRequest>);
 static_assert(std::is_same_v<decltype(MainStockFooterTextRequest{}.nulTerminatedText),const char16_t*>);
 unsigned checks=0,failures=0;const auto check=[&](bool v){++checks;if(!v)++failures;};
 constexpr char16_t text[]{u'A',0,u'B',0};constexpr wchar_t wide[]{L'W',0,L'Z',0};
 MainStockFooterTextRequest r{text,{-7,9,101,37}};check(r.nulTerminatedText==text);check(r.nulTerminatedText[1]==0&&r.nulTerminatedText[2]==u'B');check(r.rect.left==-7&&r.rect.top==9&&r.rect.right==101&&r.rect.bottom==37);
 const auto copy=r;check(copy.nulTerminatedText==text&&copy.rect.left==-7);
 MainStockFooterTextRequest empty{};check(empty.nulTerminatedText==nullptr);check(empty.rect.left==0&&empty.rect.top==0&&empty.rect.right==0&&empty.rect.bottom==0);
 MainStockFooterWideTextRequest w{wide,{0,0,800,32}};check(w.nulTerminatedText==wide&&w.nulTerminatedText[1]==0&&w.nulTerminatedText[2]==L'Z');check(w.rect.right==800&&w.rect.bottom==32);
 check(static_cast<unsigned>(MainSystemFontRole::Button)==0&&static_cast<unsigned>(MainSystemFontRole::Title)==1&&static_cast<unsigned>(MainSystemFontRole::Small)==2);check(static_cast<unsigned>(MainSystemFontCapability::Unsupported)==0);
 std::cout<<"Main video footer values: checks="<<checks<<" borrowedNulTerminatedUTF16Wide=yes originalMainRoles=yes nativeDependencies=0 failures="<<failures<<" skipped=0\n";return failures?1:0;
}
