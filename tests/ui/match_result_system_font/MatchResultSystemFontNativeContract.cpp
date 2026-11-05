#include "OriginalMatchResultSystemFontReference.h"
#include "ui/text/win32/RuntimeMatchResultSystemFontWin32.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <vector>
using namespace concord::ui;namespace fs=std::filesystem;
constexpr std::uint32_t ACTUAL_MATCH_CALLS=378;
unsigned checks=0,failures=0;
void require(bool v,const char* what){++checks;if(!v){++failures;std::cerr<<what<<"\n";}}
struct Dib {
 HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;unsigned char* pixels=nullptr;
 Dib(){dc=CreateCompatibleDC(nullptr);BITMAPINFO i{};i.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);i.bmiHeader.biWidth=800;i.bmiHeader.biHeight=-600;i.bmiHeader.biPlanes=1;i.bmiHeader.biBitCount=32;i.bmiHeader.biCompression=BI_RGB;void* p=nullptr;bitmap=CreateDIBSection(dc,&i,DIB_RGB_COLORS,&p,nullptr,0);pixels=static_cast<unsigned char*>(p);if(bitmap)previous=SelectObject(dc,bitmap);require(dc&&bitmap&&pixels&&previous&&previous!=HGDI_ERROR,"actual DIB");}
 ~Dib(){if(dc&&previous)require(SelectObject(dc,previous)==bitmap,"restore bitmap");if(bitmap)require(DeleteObject(bitmap)!=FALSE,"delete bitmap");if(dc)require(DeleteDC(dc)!=FALSE,"delete DC");}
};
struct Call {std::wstring text;RuntimeInputPixelRect rect;std::int32_t height=0;std::uint32_t color=0,flags=0;};
std::vector<Call> inputs(const fs::path& path){
 std::ifstream f(path,std::ios::binary);require(bool(f),"actual trace");std::vector<unsigned char>d((std::istreambuf_iterator<char>(f)),{});if(d.size()<12)return{};
 require(std::memcmp(d.data(),"W017R001",8)==0,"actual trace identity");std::size_t at=8;
 auto u32=[&](){require(at+4<=d.size(),"bounded scalar");std::uint32_t v=0;if(at+4<=d.size())std::memcpy(&v,d.data()+at,4);at+=4;return v;};
 const auto count=u32();require(count==ACTUAL_MATCH_CALLS,"all actual calls");std::vector<Call>out;
 for(unsigned n=0;n<count;++n){Call c;c.height=static_cast<std::int32_t>(u32());c.color=u32();c.flags=u32();for(unsigned j=0;j<8;++j)(void)u32();c.rect={static_cast<std::int32_t>(u32()),static_cast<std::int32_t>(u32()),static_cast<std::int32_t>(u32()),static_cast<std::int32_t>(u32())};const auto units=u32();require(at+units*2<=d.size(),"bounded text");for(unsigned j=0;j<units&&at+2<=d.size();++j){std::uint16_t ch=0;std::memcpy(&ch,d.data()+at,2);at+=2;c.text.push_back(static_cast<wchar_t>(ch));}out.push_back(std::move(c));}
 require(at==d.size(),"exact trace EOF");return out;
}
struct Pair {
 HFONT original=nullptr;MatchResultSystemFontWin32 current;
 explicit Pair(std::int32_t height){original=original_match_result_font::create(height);current.initialize(height);require(original&&current.available(),"real original/current font creation");}
 ~Pair(){if(original)require(DeleteObject(original)!=FALSE,"original font deletion");current.reset();}
};
void state(Dib& d,std::size_t n){
 GdiFlush();std::fill_n(d.pixels,800*600*4,static_cast<unsigned char>(0x5b));SetBkMode(d.dc,n%3?TRANSPARENT:OPAQUE);SetBkColor(d.dc,RGB(71,37,19));SetTextColor(d.dc,RGB(2,101,73));SetTextAlign(d.dc,TA_LEFT|TA_TOP);SetViewportOrgEx(d.dc,7,-3,nullptr);SelectClipRgn(d.dc,nullptr);
 if(n%2){auto r=CreateRectRgn(9,11,751,581);require(r!=nullptr,"clip allocation");SelectClipRgn(d.dc,r);require(DeleteObject(r)!=FALSE,"clip release");}
}
int main(int argc,char**argv){
 static_assert(sizeof(MatchResultSystemFontWin32)==sizeof(HFONT));
 static_assert(alignof(MatchResultSystemFontWin32)==alignof(HFONT));
 require(argc==2,"explicit actual fixture");if(argc!=2)return 1;auto calls=inputs(fs::path(argv[1]));if(failures)return 1;const auto actual=calls.size();
 const std::array<std::wstring,8>texts{L"",L"A&B",L"中文 与 Mixed",L"line1\nline2",std::wstring(L"null\0tail",9),std::wstring({wchar_t(0xd83d),wchar_t(0xde00),wchar_t(0xd800),wchar_t(0xdc00)}),L"trailing   ",std::wstring(257,L'W')};
 const std::array<RuntimeInputPixelRect,5>rects{{{10,10,320,78},{-7,-9,80,25},{20,20,21,21},{90,90,40,40},{798,598,830,625}}};
 const std::array<UINT,5>flags{DT_LEFT|DT_TOP|DT_NOPREFIX,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX,DT_WORDBREAK|DT_END_ELLIPSIS,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX};
 for(std::int32_t height:{-9,-16,-19,-20,-21,-32,0,12})for(const auto&t:texts)for(auto rect:rects)for(auto flag:flags)calls.push_back({t,rect,height,0x0312ab77U,flag});
 std::uint64_t compared=0;const auto total=calls.size();
 {
 std::map<std::int32_t,std::unique_ptr<Pair>>fonts;
 for(const auto& c:calls)if(!fonts.contains(c.height))fonts.emplace(c.height,std::make_unique<Pair>(c.height));
 Dib a,b;require(!failures,"all DIB/font creation");if(failures)return 1;
 for(const auto& [height,pair]:fonts){(void)height;require(pair->current.capability()==MatchResultSystemFontCapability::NativeText,"advertised native capability");
  LOGFONTW x{},y{};require(GetObjectW(pair->original,sizeof(x),&x)==sizeof(x)&&GetObjectW(pair->current.nativeFont(),sizeof(y),&y)==sizeof(y),"LOGFONT actual");
  require(std::memcmp(&x,&y,sizeof(x))==0,"original create params exact");
  auto ax=SelectObject(a.dc,pair->original),by=SelectObject(b.dc,pair->current.nativeFont());TEXTMETRICW mx{},my{};require(GetTextMetricsW(a.dc,&mx)&&GetTextMetricsW(b.dc,&my),"actual metrics");require(std::memcmp(&mx,&my,sizeof(mx))==0,"metrics exact");SelectObject(a.dc,ax);SelectObject(b.dc,by);
 }
 auto run=[&](unsigned pass){
  for(std::size_t n=0;n<total;++n){const auto&c=calls[n];auto& pair=*fonts.at(c.height);state(a,n);state(b,n);const auto fontA=GetCurrentObject(a.dc,OBJ_FONT),fontB=GetCurrentObject(b.dc,OBJ_FONT);const int modeA=GetBkMode(a.dc),modeB=GetBkMode(b.dc);
   HRGN ca=CreateRectRgn(0,0,0,0),cb=CreateRectRgn(0,0,0,0);const int ra=GetClipRgn(a.dc,ca),rb=GetClipRgn(b.dc,cb);RECT rect{c.rect.left,c.rect.top,c.rect.right,c.rect.bottom};
   original_match_result_font::draw(a.dc,pair.original,c.text,rect,c.color,c.flags);
   if(pass%2==0)pair.current.draw(b.dc,MatchResultSystemWideTextRequest::fromPacked(c.text,c.rect,c.flags,c.color));
   else{const std::u16string text(c.text.begin(),c.text.end());pair.current.draw(b.dc,MatchResultSystemTextRequest::fromPacked(text,c.rect,c.flags,c.color));}
   GdiFlush();require(std::equal(a.pixels,a.pixels+800*600*4,b.pixels),"whole original/current DIB exact");
   require(GetCurrentObject(a.dc,OBJ_FONT)==fontA&&GetCurrentObject(b.dc,OBJ_FONT)==fontB,"selected font restored");
   require(GetBkMode(a.dc)==modeA&&GetBkMode(b.dc)==modeB,"original draw leaves background mode unchanged including empty");
   require(GetTextColor(a.dc)==c.color&&GetTextColor(b.dc)==c.color,"original retained color including empty");
   require(GetBkColor(a.dc)==GetBkColor(b.dc)&&GetTextAlign(a.dc)==GetTextAlign(b.dc),"DC state exact");
   HRGN aa=CreateRectRgn(0,0,0,0),bb=CreateRectRgn(0,0,0,0);require(GetClipRgn(a.dc,aa)==ra&&GetClipRgn(b.dc,bb)==rb&&EqualRgn(ca,aa)&&EqualRgn(cb,bb),"clip unchanged");
   for(auto r:{ca,cb,aa,bb})require(DeleteObject(r)!=FALSE,"release region");
   if(pass>=2)compared+=800ULL*600*4;
  }
 };
 run(0);auto warm1=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);run(1);auto warm2=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);require(warm1==warm2,"full sequence warm stable");
 auto gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);DWORD handlesBefore=0;require(GetProcessHandleCount(GetCurrentProcess(),&handlesBefore)!=FALSE,"handles before");
 run(2);run(3);
 for(unsigned n=0;n<128;++n){MatchResultSystemFontWin32 cycle;cycle.initialize(-16);auto font=cycle.nativeFont();require(cycle.available(),"lifetime creation");cycle.reset();LOGFONTW deleted{};require(!cycle.available()&&GetObjectW(font,sizeof(deleted),&deleted)==0,"font identity actually released");cycle.reset();}
 DWORD handlesAfter=0;require(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)!=FALSE,"handles after");auto gdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);require(gdiBefore==gdiAfter&&handlesBefore==handlesAfter,"exact GDI/handle resource equality");
 std::cout<<"Match result system font native: checks="<<checks<<" actualCalls="<<actual<<" boundaryCases=1600 fullFramePairs="<<total*2<<" comparedBytes="<<compared<<" heights="<<fonts.size()<<" gdiBefore="<<gdiBefore<<" gdiAfter="<<gdiAfter<<" handlesBefore="<<handlesBefore<<" handlesAfter="<<handlesAfter<<" originalDcSideEffects=yes utf16AndWide=yes exactNoTolerance=yes failures="<<failures<<" skipped=0\n";
 }
 require(failures==0,"all DIB/font destructors asserted");return failures?1:0;
}
