#include "ui/text/RuntimeMainBitmapText.h"
#if defined(_WINDOWS_)
#error The bitmap text interface imported a Windows header.
#endif
#include <array>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <limits>
namespace {
int checks=0, failures=0;
void expect(bool value,const char* text) { ++checks; if(!value){++failures;std::cerr<<text<<'\n';} }
void put32(std::vector<unsigned char>& b,std::size_t at,std::uint32_t v){for(unsigned i=0;i<4;++i)b[at+i]=static_cast<unsigned char>(v>>(i*8));}
void map(std::vector<unsigned char>& b,std::uint16_t code,std::uint16_t ref){const auto at=28+static_cast<std::size_t>(code)*2;b[at]=static_cast<unsigned char>(ref);b[at+1]=static_cast<unsigned char>(ref>>8);}
void pixel(std::vector<unsigned char>& b,int x,int y,unsigned char blue,unsigned char green,unsigned char red){const auto at=static_cast<std::size_t>(y*8+x)*4;b[at]=blue;b[at+1]=green;b[at+2]=red;b[at+3]=255;}
}
int main(){
    using namespace concord::ui;
    constexpr std::size_t data=28+65536*2;
    std::vector<unsigned char> bytes(data+12,0);bytes[0]='f';bytes[1]='o';bytes[2]='n';bytes[3]='T';
    put32(bytes,8,1);put32(bytes,12,2);put32(bytes,16,3);put32(bytes,20,4);put32(bytes,24,3);
    map(bytes,u'?',1);map(bytes,u'A',2);map(bytes,0x4e2d,3);map(bytes,u'X',4);
    const unsigned char records[12]{2,0xc0,0x40,3,0xa0,0xe0,1,0x80,0x80,9,0xff,0xff};
    std::copy(std::begin(records),std::end(records),bytes.begin()+data);
    concord::assets::WestwoodFontMetrics metrics;expect(concord::assets::decodeWestwoodFontHeader(bytes,metrics),"synthetic font uses actual byte codec");
    MainBitmapFontView font{bytes,metrics};expect(font.valid(),"font borrow valid");
    expect(measureMainBitmapText(font,u"")==0,"empty width");expect(measureMainBitmapText(font,u"A")==3,"width excludes final advance gap");
    expect(measureMainBitmapText(font,u"Z")==2,"missing mapping falls back question mark");
    expect(measureMainBitmapText(font,u"X")==2,"invalid width uses Main fallback");
    expect(measureMainBitmapText(font,u"AXZ")==9,"mixed valid invalid and missing advance");
    const std::u16string chinese(1,static_cast<char16_t>(0x4e2d));expect(measureMainBitmapText(font,chinese)==1,"Chinese BMP glyph");
    const std::u16string pair{static_cast<char16_t>(0xd83d),static_cast<char16_t>(0xde42)};expect(measureMainBitmapText(font,pair)==2,"Main valid surrogate pair folds once");
    const std::u16string embedded{u'A',0,u'A'};expect(measureMainBitmapText(font,embedded)==10,"embedded NUL still code unit");
    for(std::uint32_t code=0;code<65536;++code){
        const wchar_t wide=static_cast<wchar_t>(code);const char16_t unit=static_cast<char16_t>(code);
        const auto a=measureMainBitmapText(font,std::wstring_view(&wide,1)),b=measureMainBitmapText(font,std::u16string_view(&unit,1));
        const int wanted=code==u'A'?3:code==0x4e2d?1:2;
        expect(a==b,"wide and u16 code-unit projection exact");expect(a==wanted,"complete BMP width policy");
    }
    const MainBitmapTextRamp colors;
    expect(colors.bright.packedRgb()==0x61beffU&&colors.middle.packedRgb()==0x44acf5U&&colors.dark.packedRgb()==0x043b61U,"original default ramp/order");
    std::vector<unsigned char> p(8*8*4,0x6d),want=p;
    auto put=[&](int x,int y,RuntimeRgbColorValue color){pixel(want,x,y,color.blue(),color.green(),color.red());};
    drawMainBitmapText(font,{p,8,8,32},u"A",{1,1,7,7},false,false,colors);
    for(auto [x,y]:std::array<std::pair<int,int>,5>{{{2,2},{4,2},{2,3},{3,3},{4,3}}})put(x,y,colors.dark);
    for(auto [x,y]:std::array<std::pair<int,int>,5>{{{1,1},{3,1},{1,2},{2,2},{3,2}}})put(x,y,colors.middle);
    for(auto [x,y]:std::array<std::pair<int,int>,3>{{{1,1},{3,1},{2,2}}})put(x,y,colors.bright);
    expect(p==want,"exact dark-offset then face then top-boundary highlight BGRA/alpha255");
    auto wide=std::vector<unsigned char>(256,0x6d);drawMainBitmapText(font,{wide,8,8,32},L"A",{1,1,7,7},false,false,colors);expect(p==wide,"wide and portable16 pixels");
    std::fill(p.begin(),p.end(),0x6d);want=p;
    drawMainBitmapText(font,{p,8,8,32},u"A",{0,0,8,8},true,true,colors);
    for(auto [x,y]:std::array<std::pair<int,int>,5>{{{3,3},{5,3},{3,4},{4,4},{5,4}}})put(x,y,colors.dark);
    for(auto [x,y]:std::array<std::pair<int,int>,5>{{{2,2},{4,2},{2,3},{3,3},{4,3}}})put(x,y,colors.middle);
    for(auto [x,y]:std::array<std::pair<int,int>,3>{{{2,2},{4,2},{3,3}}})put(x,y,colors.bright);
    expect(p==want,"integer center and original lineHeight");
    std::fill(p.begin(),p.end(),0x6d);want=p;drawMainBitmapText(font,{p,8,8,32},u"A",{-1,-1,2,2},false,false,colors);
    put(0,1,colors.dark);put(1,1,colors.dark);put(0,0,colors.bright);put(1,0,colors.middle);expect(p==want,"negative origin/exclusive right-bottom");
    const auto retained=p;
    drawMainBitmapText(font,{p,8,8,32},u"A",{5,5,1,1},true,true,colors);expect(p==retained,"reversed rectangle");
    drawMainBitmapText(font,{p,8,8,32},u"A",{2,2,2,2},false,false,colors);expect(p==retained,"empty rectangle");
    drawMainBitmapText(font,{std::span<unsigned char>(p).first(255),8,8,32},u"A",{0,0,8,8},false,false,colors);expect(p==retained,"short surface");
    drawMainBitmapText(font,{p,8,8,31},u"A",{0,0,8,8},false,false,colors);expect(p==retained,"short stride");
    drawMainBitmapText(font,{p,0,8,32},u"A",{0,0,8,8},false,false,colors);expect(p==retained,"zero dimension");
    drawMainBitmapText(font,{p,8,8,std::numeric_limits<std::size_t>::max()},u"A",{0,0,8,8},false,false,colors);expect(p==retained,"row addressing overflow");
    const MainBitmapFontView invalid;expect(measureMainBitmapText(invalid,u"A?")==9,"invalid font missing advance policy");
    drawMainBitmapText(invalid,{p,8,8,32},u"A",{0,0,8,8},false,false,colors);expect(p==retained,"invalid font no system replacement");
    drawMainBitmapText(font,{p,8,8,32},u"",{0,0,8,8},false,false,colors);expect(p==retained,"empty text no write");
    std::vector<unsigned char> padded(320,0x6d),continuous(256,0x6d);
    drawMainBitmapText(font,{padded,8,8,40},u"A",{1,1,7,7},false,false,colors);drawMainBitmapText(font,{continuous,8,8,32},u"A",{1,1,7,7},false,false,colors);
    bool equal=true;for(std::size_t y=0;y<8;++y){equal=equal&&std::equal(continuous.begin()+y*32,continuous.begin()+(y+1)*32,padded.begin()+y*40);for(std::size_t x=32;x<40;++x)equal=equal&&padded[y*40+x]==0x6d;}expect(equal,"existing padded surface rows/padding exact");
    std::cout<<"Main bitmap text: checks="<<checks<<" failures="<<failures<<" bmpCodeUnits=65536 twoTextViews=yes foldedPair=yes threePassExact=yes pixels=exact noNativeHeader=yes\n";
    return failures==0?0:1;
}
