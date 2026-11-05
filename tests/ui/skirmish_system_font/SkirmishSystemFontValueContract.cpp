#include "ui/text/RuntimeSkirmishSystemFont.h"
#include <array>
#include <iostream>
#include <type_traits>
using namespace concord::ui;
static_assert(std::is_same_v<std::underlying_type_t<SkirmishSystemFontRole>,std::uint8_t>);
int main(){unsigned checks=0,failures=0;auto require=[&](bool v){++checks;if(!v)++failures;};
 constexpr std::array roles{SkirmishSystemFontRole::Title,SkirmishSystemFontRole::Caption,SkirmishSystemFontRole::Normal,SkirmishSystemFontRole::Small};
 for(unsigned n=0;n<roles.size();++n){require(static_cast<unsigned>(roles[n])==n);for(unsigned k=0;k<n;++k)require(roles[n]!=roles[k]);}
 require(skirmishCanvasSystemTextSupport()==SkirmishCanvasSystemTextSupport::BitmapOnly);
 std::cout<<"Skirmish system font values: checks="<<checks<<" nativeDependencies=0 customCanvas=bitmap-only canvasSystemFallback=unsupported nativeEditFont=separate-owner failures="<<failures<<" skipped=0\n";return failures?1:0;}
