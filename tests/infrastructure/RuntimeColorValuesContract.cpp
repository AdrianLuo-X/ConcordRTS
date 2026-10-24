#include "ui/values/RuntimeColorValues.h"
#include <iostream>
#include <type_traits>
#include <cstdint>
static_assert(sizeof(RuntimeRgbColorValue) == 4);
static_assert(alignof(RuntimeRgbColorValue) == alignof(std::uint32_t));
static_assert(std::is_trivially_copyable_v<RuntimeRgbColorValue>);
static_assert(std::is_standard_layout_v<RuntimeRgbColorValue>);
int main() {
    int checks=0, failures=0;
    const auto expect=[&](bool value){++checks;if(!value)++failures;};
    const auto mixed=RuntimeRgbColorValue::fromPackedRgb(0xa5123456U);
    expect(mixed.red()==0x56);expect(mixed.green()==0x34);expect(mixed.blue()==0x12);expect(mixed.packedRgb()==0x123456U);
    const auto shadow=RuntimeRgbColorValue::fromChannels(20,12,0);
    expect(shadow.red()==20 && shadow.green()==12 && shadow.blue()==0 && shadow.packedRgb()==0x00000c14U);
    expect(RuntimeRgbColorValue{}.packedRgb()==0);
    for(unsigned int i=0;i<256;++i) {
        const auto channel=static_cast<std::uint8_t>(i);
        const auto r=RuntimeRgbColorValue::fromChannels(channel,0,0),g=RuntimeRgbColorValue::fromChannels(0,channel,0),b=RuntimeRgbColorValue::fromChannels(0,0,channel);
        expect(r.red()==i && r.green()==0 && r.blue()==0);expect(g.red()==0 && g.green()==i && g.blue()==0);expect(b.red()==0 && b.green()==0 && b.blue()==i);
    }
    std::cout<<"UI RGB value: checks="<<checks<<" failures="<<failures<<" size=4 noNativeHeader=yes\n";
    return failures==0?0:1;
}
