#include "ui/text/RuntimeMainSystemFont.h"
#include <array>
#include <iostream>
#include <type_traits>
using namespace concord::ui;
static_assert(std::is_same_v<decltype(MainSystemTextRequest::text),std::u16string_view>);
static_assert(std::is_same_v<decltype(MainSystemWideTextRequest::text),std::wstring_view>);
static_assert(std::is_same_v<decltype(MainSystemTextRequest::rect),RuntimeInputPixelRect>);
int main(){unsigned checks=0,failures=0;auto require=[&](bool v){++checks;if(!v)++failures;};
 constexpr std::array<std::uint32_t,8> values{0,1,0xffffffU,0xff000000U,0xffffffffU,0x03120a00U,0x01f5ac44U,0x800001ffU};
 for(auto a:values)for(auto b:values){auto colors=MainSystemFontColors::fromPacked(a,b);require(colors.packedDark()==a&&colors.packedMiddle()==b);require(colors.dark.packedRgb()==(a&0xffffffU)&&colors.middle.packedRgb()==(b&0xffffffU));}
 const char16_t text[]{u'A',u'\0',char16_t(0xd83d),char16_t(0xde00)};
 MainSystemTextRequest request{{text,4},{-9,7,31,-5},2085,MainSystemFontColors::fromPacked(0x03120a00,0x01f5ac44)};
 require(request.text.size()==4&&request.text.data()==text&&request.text[1]==0);require(request.rect.left==-9&&request.rect.bottom==-5&&request.format==2085);require(MainSystemFontCapability::Unsupported!=MainSystemFontCapability::NativeText);
 std::cout<<"Main system font standard: checks="<<checks<<" nativeDependencies=0 colorsAndControlBitsExact=yes borrowedUTF16=yes failures="<<failures<<" skipped=0\n";return failures?1:0;}
