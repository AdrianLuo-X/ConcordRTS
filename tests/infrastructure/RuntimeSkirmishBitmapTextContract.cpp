#include "ui/text/RuntimeSkirmishBitmapText.h"
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
    SkirmishBitmapFontView font{bytes,metrics};expect(font.valid(),"font borrow valid");
    expect(measureSkirmishBitmapText(font,u"")==0,"empty width");expect(measureSkirmishBitmapText(font,u"A")==3,"width excludes final advance gap");
    expect(measureSkirmishBitmapText(font,u"Z")==2,"missing mapping falls back question mark");
    expect(measureSkirmishBitmapText(font,u"X")==4,"invalid width remains unavailable, not main-menu fallback");
    expect(measureSkirmishBitmapText(font,u"AXZ")==11,"mixed valid invalid and missing advance");
    const std::u16string chinese(1,static_cast<char16_t>(0x4e2d));expect(measureSkirmishBitmapText(font,chinese)==1,"Chinese BMP glyph");
    const std::u16string pair{static_cast<char16_t>(0xd83d),static_cast<char16_t>(0xde42)};expect(measureSkirmishBitmapText(font,pair)==5,"two original surrogate code units remain distinct");
    const std::u16string embedded{u'A',0,u'A'};expect(measureSkirmishBitmapText(font,embedded)==10,"embedded NUL still code unit");
    for(std::uint32_t code=0;code<65536;++code){
        const wchar_t wide=static_cast<wchar_t>(code);const char16_t unit=static_cast<char16_t>(code);
        const auto a=measureSkirmishBitmapText(font,std::wstring_view(&wide,1)),b=measureSkirmishBitmapText(font,std::u16string_view(&unit,1));
        const int wanted=code==u'A'?3:code==0x4e2d?1:code==u'X'?4:2;
        expect(a==b,"wide and u16 code-unit projection exact");expect(a==wanted,"complete BMP width policy");
    }
    const auto color=RuntimeRgbColorValue::fromChannels(10,20,30);
    std::vector<unsigned char> pixels(8*8*4,0x6d),wanted=pixels;
    const RuntimeInputPixelRect rect{1,1,7,7};drawSkirmishBitmapText(font,pixels,8,8,u"A",rect,false,false,color);
    const std::array<std::pair<int,int>,5> shadow{{{2,2},{4,2},{2,3},{3,3},{4,3}}},foreground{{{1,1},{3,1},{1,2},{2,2},{3,2}}};
    for(const auto& [x,y]:shadow)pixel(wanted,x,y,0,12,20);for(const auto& [x,y]:foreground)pixel(wanted,x,y,30,20,10);
    expect(pixels==wanted,"full byte frame, two passes, shadow overwritten by foreground and alpha255");
    auto widePixels=std::vector<unsigned char>(8*8*4,0x6d);drawSkirmishBitmapText(font,widePixels,8,8,L"A",rect,false,false,color);expect(pixels==widePixels,"wide/u16 complete pixel equality");
    std::fill(pixels.begin(),pixels.end(),0x6d);wanted=pixels;drawSkirmishBitmapText(font,pixels,8,8,u"A",{0,0,8,8},true,true,color);
    for(const auto& [x,y]:std::array<std::pair<int,int>,5>{{{3,3},{5,3},{3,4},{4,4},{5,4}}})pixel(wanted,x,y,0,12,20);
    for(const auto& [x,y]:std::array<std::pair<int,int>,5>{{{2,2},{4,2},{2,3},{3,3},{4,3}}})pixel(wanted,x,y,30,20,10);
    expect(pixels==wanted,"integer centered origins and line height");
    std::fill(pixels.begin(),pixels.end(),0x6d);wanted=pixels;drawSkirmishBitmapText(font,pixels,8,8,u"A",{-1,-1,2,2},false,false,color);
    pixel(wanted,0,1,0,12,20);pixel(wanted,1,1,0,12,20);pixel(wanted,0,0,30,20,10);pixel(wanted,1,0,30,20,10);expect(pixels==wanted,"negative origin and half-open rect plus surface clipping");
    const auto retained=pixels;
    drawSkirmishBitmapText(font,pixels,8,8,u"A",{5,5,1,1},true,true,color);expect(pixels==retained,"reversed rect remains no writes");
    drawSkirmishBitmapText(font,pixels,8,8,u"A",{2,2,2,2},false,false,color);expect(pixels==retained,"empty clip no writes");
    drawSkirmishBitmapText(font,std::span<unsigned char>(pixels).first(255),8,8,u"A",rect,false,false,color);expect(pixels==retained,"short span no writes");
    drawSkirmishBitmapText(font,pixels,0,8,u"A",rect,false,false,color);expect(pixels==retained,"zero width no writes");
    drawSkirmishBitmapText(font,pixels,std::numeric_limits<std::int32_t>::max(),std::numeric_limits<std::int32_t>::max(),u"A",rect,false,false,color);expect(pixels==retained,"oversized surface requires capacity");
    const SkirmishBitmapFontView invalid;expect(!invalid.valid(),"invalid font");expect(measureSkirmishBitmapText(invalid,u"A?")==9,"invalid font retains original missing advances");
    drawSkirmishBitmapText(invalid,pixels,8,8,u"A",rect,false,false,color);expect(pixels==retained,"missing font no system fallback/no writes");
    drawSkirmishBitmapText(font,pixels,8,8,u"",rect,false,false,color);expect(pixels==retained,"empty text no writes");
    std::cout<<"Skirmish bitmap text: checks="<<checks<<" failures="<<failures<<" bmpCodeUnits=65536 twoTextViews=yes pixels=exact noNativeHeader=yes\n";
    return failures==0?0:1;
}
