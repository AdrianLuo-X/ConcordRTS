#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ui/text/RuntimeMainBitmapText.h"
#include "OriginalMainFontReference.h"
#include <array>
#include <iostream>
#include <set>
#include <stdexcept>
namespace {
std::uint64_t checks=0, failures=0, comparedFrames=0, comparedBytes=0;
void expect(bool ok,const char* why){++checks;if(!ok){++failures;if(failures<20)std::cerr<<why<<"\n";}}
struct Call {std::wstring text;RuntimeInputPixelRect rect;bool center=false,vertical=false;main_original_reference::FontColorRamp colors;};
std::uint32_t u32(std::istream& in){std::array<unsigned char,4>b{};in.read(reinterpret_cast<char*>(b.data()),4);if(!in)throw std::runtime_error("short trace");return b[0]|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);}
std::u16string narrow(const std::wstring& text){std::u16string units;for(const auto c:text)units.push_back(static_cast<char16_t>(c));return units;}
}
int wmain(int argc,wchar_t** argv){
 try {
  if(argc!=3&&argc!=4)throw std::runtime_error("need real trace/output or layout/font/output");
  const std::filesystem::path trace=argv[1],own=argv[argc-1];if(std::filesystem::exists(own))throw std::runtime_error("output exists");std::filesystem::create_directories(own);
  std::ifstream input(trace,std::ios::binary);char magic[8]{};input.read(magic,8);expect(std::string(magic,8)=="W015M001","actual original trace magic");
  const auto width=u32(input),height=u32(input); auto fontSize=u32(input); const auto callCount=u32(input);
  if(width!=800||height!=600||fontSize>16*1024*1024||callCount>10000)throw std::runtime_error("trace bounds");
  std::vector<unsigned char> fontBytes;
  if(argc==4){
   if(fontSize!=0)throw std::runtime_error("layout fixture must not bundle original font");
   std::ifstream nativeFont(std::filesystem::path(argv[2]),std::ios::binary);if(!nativeFont)throw std::runtime_error("original font missing");
   fontBytes.assign(std::istreambuf_iterator<char>(nativeFont),std::istreambuf_iterator<char>());
   fontSize=static_cast<std::uint32_t>(fontBytes.size());
  }else{
   fontBytes.resize(fontSize);input.read(reinterpret_cast<char*>(fontBytes.data()),fontSize);if(!input)throw std::runtime_error("font bytes truncated");
  }
  std::vector<Call> calls;std::set<std::wstring> strings;std::set<std::uint16_t> units;
  for(std::uint32_t k=0;k<callCount;++k){Call c;const auto n=u32(input);if(n>65536)throw std::runtime_error("trace text bound");c.rect={static_cast<std::int32_t>(u32(input)),static_cast<std::int32_t>(u32(input)),static_cast<std::int32_t>(u32(input)),static_cast<std::int32_t>(u32(input))};const auto flags=u32(input);c.center=(flags&1)!=0;c.vertical=(flags&2)!=0;c.colors={u32(input),u32(input),u32(input)};
   for(std::uint32_t j=0;j<n;++j){unsigned char b[2]{};input.read(reinterpret_cast<char*>(b),2);if(!input)throw std::runtime_error("text truncated");const auto unit=std::uint16_t(b[0]|(std::uint16_t(b[1])<<8));c.text.push_back(static_cast<wchar_t>(unit));units.insert(unit);}strings.insert(c.text);calls.push_back(std::move(c));}
  expect(input.peek()==std::char_traits<char>::eof(),"trace fully consumed");expect(callCount>0&&fontSize==1792690,"actual Main font input retained");
  {std::ofstream f(own/"game.fnt",std::ios::binary);f.write(reinterpret_cast<const char*>(fontBytes.data()),fontBytes.size());}
  {std::ofstream f(own/"short.fnt",std::ios::binary);f<<"fonT";}
  {auto bad=fontBytes;bad[0]='!';std::ofstream f(own/"bad.fnt",std::ios::binary);f.write(reinterpret_cast<const char*>(bad.data()),bad.size());}
  main_original_reference::WestwoodBitFont original;
  expect(!original.load(own/"missing.fnt")&&!original.valid()&&original.bytes.empty()&&original.source.empty(),"original missing file reset");
  expect(!original.load(own/"short.fnt")&&!original.valid(),"original short header");expect(!original.load(own/"bad.fnt")&&!original.valid(),"original bad magic");
  expect(original.load(own/"game.fnt")&&original.valid()&&original.bytes==fontBytes,"original actual native font loader complete");
  using namespace concord::ui;const MainBitmapFontView font{fontBytes,{original.stride,original.bitmapRows,original.lineHeight,original.glyphCount,original.recordSize}};
  expect(font.valid(),"same production std borrow valid");
  for(std::uint32_t k=0;k<65536;++k){std::wstring text(1,static_cast<wchar_t>(k));const auto n=narrow(text);const auto old=original.measure(text);expect(measureMainBitmapText(font,std::wstring_view(text))==old,"complete BMP wide width");expect(measureMainBitmapText(font,std::u16string_view(n))==old,"complete BMP16 width");}
  for(std::uint32_t hi=0xd800;hi<=0xdbff;++hi)for(std::uint32_t lo=0xdc00;lo<=0xdfff;++lo){const std::wstring text{static_cast<wchar_t>(hi),static_cast<wchar_t>(lo)};const auto n=narrow(text);const auto old=original.measure(text);expect(measureMainBitmapText(font,std::wstring_view(text))==old&&measureMainBitmapText(font,std::u16string_view(n))==old,"every valid surrogate pair folds once");}
  const auto total=std::size_t(width)*height*4;std::vector<unsigned char> initial(total),oldPixels(total),newPixels(total),portablePixels(total);for(std::size_t a=0;a<total;++a)initial[a]=static_cast<unsigned char>((a*37+a/17+91)&255);
  const auto compare=[&](const Call& c){const auto n=narrow(c.text);expect(original.measure(c.text)==measureMainBitmapText(font,std::wstring_view(c.text)),"actual/boundary wide measure");expect(original.measure(c.text)==measureMainBitmapText(font,std::u16string_view(n)),"actual/boundary16 measure");oldPixels=initial;newPixels=initial;portablePixels=initial;
   const RECT r{c.rect.left,c.rect.top,c.rect.right,c.rect.bottom};original.draw(oldPixels.data(),width,height,c.text,r,c.center,c.vertical,c.colors);
   const MainBitmapTextRamp colors{RuntimeRgbColorValue::fromPackedRgb(c.colors.bright),RuntimeRgbColorValue::fromPackedRgb(c.colors.middle),RuntimeRgbColorValue::fromPackedRgb(c.colors.dark)};
   drawMainBitmapText(font,{newPixels,static_cast<std::int32_t>(width),static_cast<std::int32_t>(height),width*4},std::wstring_view(c.text),c.rect,c.center,c.vertical,colors);
   drawMainBitmapText(font,{portablePixels,static_cast<std::int32_t>(width),static_cast<std::int32_t>(height),width*4},std::u16string_view(n),c.rect,c.center,c.vertical,colors);
   expect(oldPixels==newPixels,"complete original/new native-wide BGRA frame");expect(oldPixels==portablePixels,"complete original/new16 BGRA frame");comparedFrames+=2;comparedBytes+=total*2;};
  for(const auto& c:calls)compare(c);
  const std::array<RuntimeInputPixelRect,10> rects{{{0,0,800,600},{-8,-9,20,24},{790,590,820,620},{4,5,5,6},{3,4,3,9},{30,40,10,20},{-30,-40,-1,-2},{799,599,800,600},{100,200,170,214},{0,0,1,1}}};
  std::uint64_t boundaries=0;
  for(const auto& originalCall:calls)for(const auto& rect:rects)for(unsigned mode=0;mode<4;++mode){auto c=originalCall;c.rect=rect;c.center=(mode&1)!=0;c.vertical=(mode&2)!=0;c.colors.bright|=0xa5000000U;c.colors.middle|=0xff000000U;c.colors.dark|=0x03000000U;compare(c);++boundaries;}
  for(auto text:std::array<std::wstring,7>{{L"",L"?",L"A",std::wstring{0,L'A',0},std::wstring{wchar_t(0xd800),L'A',wchar_t(0xdc00)},std::wstring{wchar_t(0xdbff),wchar_t(0xdfff)},std::wstring{wchar_t(0xffff)}}}){Call c;c.text=std::move(text);c.rect={-2,-1,40,30};compare(c);}
  expect(!original.load(own/"missing-after-load.fnt")&&!original.valid()&&original.bytes.empty()&&original.source.empty()&&original.recordSize==0,"original reload failure clears prior ownership");
  std::cout<<"Main bitmap original reference: checks="<<checks<<" failures="<<failures<<" calls="<<callCount<<" strings="<<strings.size()<<" codeUnits="<<units.size()<<" fontBytes="<<fontSize<<" boundaries="<<boundaries<<" fullFrames="<<comparedFrames<<" comparedBytes="<<comparedBytes<<" bmpWidths=65536 validPairs=1048576 originalLoader=real noTolerance=yes\n";
  return failures?1:0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
