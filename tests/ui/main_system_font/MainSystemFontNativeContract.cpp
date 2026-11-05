#include "OriginalMainSystemFontReference.h"
#include "ui/text/win32/RuntimeMainSystemFontWin32.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
using namespace concord::ui;
namespace fs=std::filesystem;
unsigned checks=0,failures=0;
void require(bool v,const char* what){++checks;if(!v){++failures;std::cerr<<what<<"\n";}}
struct Dib {
 HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;unsigned char* pixels=nullptr;
 Dib(){dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=800;info.bmiHeader.biHeight=-600;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* data=nullptr;bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);pixels=static_cast<unsigned char*>(data);if(bitmap)previous=SelectObject(dc,bitmap);require(dc&&bitmap&&pixels&&previous&&previous!=HGDI_ERROR,"actual owned DIB");}
 ~Dib(){if(dc&&previous)require(SelectObject(dc,previous)==bitmap,"reselect original bitmap");if(bitmap)require(DeleteObject(bitmap)!=FALSE,"delete owned bitmap");if(dc)require(DeleteDC(dc)!=FALSE,"delete owned DC");}
};
struct Call {std::wstring text;RuntimeInputPixelRect rect;std::uint32_t role=0,flags=0,dark=0,middle=0;};
std::vector<Call> inputs(const fs::path& path){
 std::ifstream f(path,std::ios::binary);require(bool(f),"actual request fixture");std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)),{});if(data.size()<20)return{};
 require(std::memcmp(data.data(),"W017M001",8)==0,"trace identity");std::size_t at=8;
 auto u32=[&](){require(at+4<=data.size(),"bounded trace scalar");std::uint32_t v=0;if(at+4<=data.size())std::memcpy(&v,data.data()+at,4);at+=4;return v;};
 require(u32()==800&&u32()==600,"actual canvas");auto count=u32();require(count==426,"all actual426 calls");std::vector<Call> out;
 for(unsigned n=0;n<count;++n){Call c;c.role=u32();c.flags=u32();c.dark=u32();c.middle=u32();c.rect={static_cast<std::int32_t>(u32()),static_cast<std::int32_t>(u32()),static_cast<std::int32_t>(u32()),static_cast<std::int32_t>(u32())};auto units=u32();require(c.role<3&&at+units*2<=data.size(),"bounded actual text");for(unsigned k=0;k<units&&at+2<=data.size();++k){std::uint16_t ch=0;std::memcpy(&ch,data.data()+at,2);at+=2;c.text.push_back(static_cast<wchar_t>(ch));}out.push_back(std::move(c));}
 require(at==data.size(),"exact fixture EOF");return out;
}
HFONT originalFont(const original_main_system_font::MenuState& s,unsigned role){return role==0?s.buttonFont:role==1?s.titleFont:s.smallFont;}
void setState(Dib& d,bool clip){
 GdiFlush();std::fill_n(d.pixels,800*600*4,static_cast<unsigned char>(0x5b));
 SetBkMode(d.dc,OPAQUE);SetBkColor(d.dc,RGB(71,37,19));SetTextColor(d.dc,RGB(2,101,73));SetTextAlign(d.dc,TA_LEFT|TA_TOP);
 SetViewportOrgEx(d.dc,7,-3,nullptr);
 SelectClipRgn(d.dc,nullptr);if(clip){auto region=CreateRectRgn(9,11,751,581);require(region!=nullptr,"clip allocated");SelectClipRgn(d.dc,region);require(DeleteObject(region)!=FALSE,"release clip object");}
}
int main(int argc,char** argv){
 require(argc==2,"explicit layout fixture argument");if(argc!=2)return 1;
 auto calls=inputs(fs::path(argv[1]));if(failures)return 1;
 const std::array<std::wstring,8> texts{L"",L"A&B",L"中文 与 Mixed",L"line1\nline2",std::wstring(L"null\0tail",9),std::wstring({wchar_t(0xd83d),wchar_t(0xde00),wchar_t(0xd800),wchar_t(0xdc00)}),L"trailing   ",std::wstring(257,L'W')};
 const std::array<RuntimeInputPixelRect,5> rects{{{10,10,320,78},{-7,-9,80,25},{20,20,21,21},{90,90,40,40},{798,598,830,625}}};
 const std::array<UINT,5> flags{DT_LEFT|DT_TOP|DT_NOPREFIX,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX,DT_WORDBREAK|DT_END_ELLIPSIS,DT_CALCRECT|DT_WORDBREAK};
 for(unsigned role=0;role<3;++role)for(const auto&text:texts)for(auto rect:rects)for(auto flag:flags)calls.push_back({text,rect,role,flag,0x03120a00U,0x01f5ac44U});
 const auto cases=calls.size();std::uint64_t compared=0;
 {
 original_main_system_font::MenuState old;original_main_system_font::initializeFonts(old);MainSystemFontsWin32 now;now.initialize();
 require(now.capability()==MainSystemFontCapability::NativeText,"advertised native text capability");
 Dib a,b;if(failures)return 1;
 for(unsigned role=0;role<3;++role){
  const auto key=static_cast<MainSystemFontRole>(role);require(now.available(key)&&originalFont(old,role)!=nullptr,"actual three fonts");
  LOGFONTW x{},y{};require(GetObjectW(originalFont(old,role),sizeof(x),&x)==sizeof(x),"original LOGFONT");require(GetObjectW(now.nativeFont(key),sizeof(y),&y)==sizeof(y),"current LOGFONT");require(std::memcmp(&x,&y,sizeof(x))==0,"original create font parameters exact");
  auto previousA=SelectObject(a.dc,originalFont(old,role)),previousB=SelectObject(b.dc,now.nativeFont(key));TEXTMETRICW mtA{},mtB{};require(GetTextMetricsW(a.dc,&mtA)&&GetTextMetricsW(b.dc,&mtB),"actual text metrics");require(std::memcmp(&mtA,&mtB,sizeof(mtA))==0,"text metrics exact");SelectObject(a.dc,previousA);SelectObject(b.dc,previousB);
 }
 auto run=[&](unsigned pass){
  for(std::size_t n=0;n<calls.size();++n){
   const auto&c=calls[n];setState(a,n%2!=0);setState(b,n%2!=0);const auto fontA=GetCurrentObject(a.dc,OBJ_FONT),fontB=GetCurrentObject(b.dc,OBJ_FONT);
   HRGN clipA=CreateRectRgn(0,0,0,0),clipB=CreateRectRgn(0,0,0,0);const int clipResultA=GetClipRgn(a.dc,clipA),clipResultB=GetClipRgn(b.dc,clipB);
   RECT rect{c.rect.left,c.rect.top,c.rect.right,c.rect.bottom};original_main_system_font::FontColorRamp ramp;ramp.dark=c.dark;ramp.middle=c.middle;
   original_main_system_font::drawShadowText(a.dc,originalFont(old,c.role),c.text,rect,c.flags,ramp);
   const auto key=static_cast<MainSystemFontRole>(c.role);const auto colors=MainSystemFontColors::fromPacked(c.dark,c.middle);
   if(pass%2==0)now.draw(b.dc,key,MainSystemWideTextRequest{c.text,c.rect,c.flags,colors});
   else{const std::u16string utf16(c.text.begin(),c.text.end());now.draw(b.dc,key,MainSystemTextRequest{utf16,c.rect,c.flags,colors});}
   GdiFlush();require(std::equal(a.pixels,a.pixels+800*600*4,b.pixels),"complete original/current DIB exact");
   require(GetCurrentObject(a.dc,OBJ_FONT)==fontA&&GetCurrentObject(b.dc,OBJ_FONT)==fontB,"original font selection restored");
   require(GetBkMode(a.dc)==GetBkMode(b.dc)&&GetTextColor(a.dc)==GetTextColor(b.dc)&&GetBkColor(a.dc)==GetBkColor(b.dc)&&GetTextAlign(a.dc)==GetTextAlign(b.dc),"DC colors/background/align side effects exact");
   if(c.text.empty())require(GetBkMode(b.dc)==OPAQUE&&GetTextColor(b.dc)==RGB(2,101,73),"empty text no DC side effects");
   else require(GetBkMode(b.dc)==TRANSPARENT&&GetTextColor(b.dc)==c.middle,"original retained transparent/middle state");
   HRGN afterA=CreateRectRgn(0,0,0,0),afterB=CreateRectRgn(0,0,0,0);require(GetClipRgn(a.dc,afterA)==clipResultA&&GetClipRgn(b.dc,afterB)==clipResultB,"clip presence preserved");require(EqualRgn(clipA,afterA)&&EqualRgn(clipB,afterB),"clip values preserved");
   for(auto region:{clipA,clipB,afterA,afterB})require(DeleteObject(region)!=FALSE,"release region");
   if(pass>=2)compared+=800ULL*600*4;
  }
 };
 run(0);const auto warm1=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);run(1);const auto warm2=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);require(warm1==warm2,"complete exact sequence warm stability");
 auto gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);DWORD handlesBefore=0;require(GetProcessHandleCount(GetCurrentProcess(),&handlesBefore)!=FALSE,"handles before");run(2);run(3);
 for(unsigned n=0;n<128;++n){MainSystemFontsWin32 cycle;cycle.initialize();auto font=cycle.nativeFont(MainSystemFontRole::Button);require(font!=nullptr,"lifetime actual creation");cycle.reset();require(!cycle.available(MainSystemFontRole::Button)&&!cycle.available(MainSystemFontRole::Title)&&!cycle.available(MainSystemFontRole::Small),"reset clears all roles");LOGFONTW deleted{};require(GetObjectW(font,sizeof(deleted),&deleted)==0,"actual released font identity");cycle.reset();}
 DWORD handlesAfter=0;require(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)!=FALSE,"handles after");auto gdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);require(gdiBefore==gdiAfter,"fonts/regions/DC object lifetime stable");require(handlesBefore==handlesAfter,"process handles stable");
 std::cout<<"Main system font native: checks="<<checks<<" actualCalls=426 boundaryCases=600 fullFramePairs="<<cases*2<<" comparedBytes="<<compared<<" gdiBefore="<<gdiBefore<<" gdiAfter="<<gdiAfter<<" handlesBefore="<<handlesBefore<<" handlesAfter="<<handlesAfter<<" originalDcSideEffects=yes utf16AndWide=yes exactNoTolerance=yes failures="<<failures<<" skipped=0\n";
 }
 require(failures==0,"all native destructors");
 return failures?1:0;
}
