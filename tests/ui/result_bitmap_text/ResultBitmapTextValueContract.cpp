#include "ui/text/RuntimeResultBitmapText.h"
#if defined(_WINDOWS_)
#error Standard result layout imported Windows.
#endif
#include <algorithm>
#include <array>
#include <iostream>
#include <vector>
namespace {
int checks=0,failures=0;
void expect(bool v,const char* why){++checks;if(!v){++failures;std::cerr<<why<<'\n';}}
void put32(std::vector<unsigned char>& b,std::size_t a,std::uint32_t v){for(unsigned k=0;k<4;++k)b[a+k]=static_cast<unsigned char>(v>>(8*k));}
void map(std::vector<unsigned char>& b,std::uint16_t c,std::uint16_t r){const auto a=28+std::size_t(c)*2;b[a]=static_cast<unsigned char>(r);b[a+1]=static_cast<unsigned char>(r>>8);}
bool same(const RuntimeInputPixelRect& a,const RuntimeInputPixelRect& b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
struct Run {RuntimeInputPixelRect rect;bool shadow;};
struct Capture final:concord::ui::ResultBitmapTextSink {
 std::vector<Run> runs;std::vector<int> events;RuntimeInputPixelRect clip{};bool shadow=false;
 Capture(){runs.reserve(4096);events.reserve(64);}
 void beginText(const RuntimeInputPixelRect& r) noexcept override{clip=r;events.push_back(10);}
 void beginPass(bool s) noexcept override{shadow=s;events.push_back(s?11:12);}
 void fillRun(const RuntimeInputPixelRect& r) noexcept override{runs.push_back({r,shadow});}
 void endPass() noexcept override{events.push_back(13);}
 void endText() noexcept override{events.push_back(14);}
};
bool equal(const Capture& a,const Capture& b){if(!same(a.clip,b.clip)||a.events!=b.events||a.runs.size()!=b.runs.size())return false;for(std::size_t k=0;k<a.runs.size();++k)if(!same(a.runs[k].rect,b.runs[k].rect)||a.runs[k].shadow!=b.runs[k].shadow)return false;return true;}
}
int main(){
 using namespace concord::ui;constexpr std::size_t data=28+65536*2;std::vector<unsigned char> bytes(data+12,0);bytes[0]='f';bytes[1]='o';bytes[2]='n';bytes[3]='T';
 put32(bytes,8,1);put32(bytes,12,2);put32(bytes,16,3);put32(bytes,20,4);put32(bytes,24,3);
 map(bytes,u'?',1);map(bytes,u'A',2);map(bytes,0x4e2d,3);map(bytes,u'X',4);
 const unsigned char records[12]{2,0xc0,0x40,3,0xa0,0xe0,1,0x80,0x80,9,0xff,0xff};std::copy(std::begin(records),std::end(records),bytes.begin()+data);
 concord::assets::WestwoodFontMetrics m;expect(concord::assets::decodeWestwoodFontHeader(bytes,m),"actual unique R002 codec fixture");const ResultBitmapFontView font{bytes,m};
 expect(measureResultBitmapText(font,u"")==0,"empty width");expect(measureResultBitmapText(font,u"A")==3,"final gap excluded");expect(measureResultBitmapText(font,u"Z")==2,"missing mapping fallback");expect(measureResultBitmapText(font,u"X")==4,"invalid width remains unavailable, no Main fallback");expect(measureResultBitmapText(font,u"AXZ")==11,"original mixed advances");
 const std::u16string pair{char16_t(0xd83d),char16_t(0xde42)},unpaired{char16_t(0xd83d),u'A',char16_t(0xde42)},embedded{u'A',0,u'A'};
 expect(measureResultBitmapText(font,pair)==5,"result visits both surrogate units");expect(measureResultBitmapText(font,unpaired)==9,"unpaired unchanged");expect(measureResultBitmapText(font,embedded)==10,"embedded NUL remains unit");
 for(std::uint32_t k=0;k<65536;++k){const auto w=static_cast<wchar_t>(k);const auto c=static_cast<char16_t>(k);const int wanted=k==u'A'?3:k==0x4e2d?1:k==u'X'?4:2;
  expect(measureResultBitmapText(font,std::wstring_view(&w,1))==wanted,"all BMP native-wide widths");expect(measureResultBitmapText(font,std::u16string_view(&c,1))==wanted,"all BMP portable16 widths");}
 Capture a;drawResultBitmapText(font,{1,1,7,7},u"A",false,a);expect(same(a.clip,{1,1,7,7}),"original base clip forwarded");expect(a.events==std::vector<int>({10,11,13,12,13,14}),"clip/shadow/face/restore exact order");
 const std::array<RuntimeInputPixelRect,6> gold{{{2,3,3,4},{4,3,5,4},{2,4,5,5},{1,2,2,3},{3,2,4,3},{1,3,4,4}}};expect(a.runs.size()==gold.size(),"contiguous set bits become original runs");for(std::size_t k=0;k<std::min(a.runs.size(),gold.size());++k)expect(same(a.runs[k].rect,gold[k])&&a.runs[k].shadow==(k<3),"hand-authored original run coordinates and pass");
 Capture wide;drawResultBitmapText(font,{1,1,7,7},L"A",false,wide);expect(equal(a,wide),"wide and16 entire ordered run list");
 Capture center;drawResultBitmapText(font,{0,0,8,8},u"A",true,center);expect(center.runs.size()==6&&same(center.runs[0].rect,{3,3,4,4}),"integer center uses measured width and original lineHeight");
 Capture truncated,dots;drawResultBitmapText(font,{1,1,7,7},u"AAAA",false,truncated);drawResultBitmapText(font,{1,1,7,7},u"...",false,dots);expect(equal(truncated,dots)&&truncated.runs.size()==12,"even too narrow base retains three dots");expect(truncated.runs.back().rect.right>truncated.clip.right,"device consumer clips, layout preserves original outside run");
 Capture negative;drawResultBitmapText(font,{-1,-1,2,2},u"A",false,negative);expect(negative.runs.size()==6&&same(negative.runs[0].rect,{0,0,1,1}),"negative logical run origins");
 Capture reversed;drawResultBitmapText(font,{4,4,1,1},u"A",false,reversed);expect(reversed.runs.size()==12&&same(reversed.clip,{4,4,1,1}),"reversed rectangle keeps original ellipsis/device clip policy");
 Capture unavailable;drawResultBitmapText(font,{0,0,20,20},u"X",false,unavailable);expect(unavailable.runs.empty()&&unavailable.events==a.events,"unavailable glyph retains original empty brush passes");
 Capture empty;drawResultBitmapText(font,{0,0,10,10},u"",false,empty);expect(empty.runs.empty()&&empty.events.empty(),"empty text no device/sink calls");
 ResultBitmapFontView missing;expect(measureResultBitmapText(missing,u"A?")==9,"missing font original advance");drawResultBitmapText(missing,{0,0,10,10},u"A",false,empty);expect(empty.events.empty(),"empty font no sink calls");
 std::cout<<"Result bitmap value: checks="<<checks<<" failures="<<failures<<" bmpUnits=65536 resultPerUnit=yes ellipsis=original runs=exact noNativeHeader=yes\n";return failures?1:0;
}
