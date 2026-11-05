#include "OriginalMainVideoFooterFontReference.h"
#include "ui/text/win32/RuntimeMainVideoFooterFontWin32.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
using namespace concord::ui;namespace fs=std::filesystem;
unsigned checks=0,failures=0;void require(bool v,const char* what){++checks;if(!v){++failures;std::cerr<<what<<"\n";}}
struct Dib {
 HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;unsigned char* pixels=nullptr;
 Dib(){dc=CreateCompatibleDC(nullptr);BITMAPINFO i{};i.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);i.bmiHeader.biWidth=800;i.bmiHeader.biHeight=-600;i.bmiHeader.biPlanes=1;i.bmiHeader.biBitCount=32;i.bmiHeader.biCompression=BI_RGB;void* p=nullptr;bitmap=CreateDIBSection(dc,&i,DIB_RGB_COLORS,&p,nullptr,0);pixels=static_cast<unsigned char*>(p);if(bitmap)previous=SelectObject(dc,bitmap);require(dc&&bitmap&&pixels&&previous&&previous!=HGDI_ERROR,"actual DIB");}
 ~Dib(){if(dc&&previous)require(SelectObject(dc,previous)==bitmap,"restore bitmap");if(bitmap)require(DeleteObject(bitmap)!=FALSE,"delete bitmap");if(dc)require(DeleteDC(dc)!=FALSE,"delete DC");}
};

struct Call{std::wstring text;RuntimeInputPixelRect rect;bool restore=true;};
std::vector<Call> inputs(const fs::path&path){
 std::ifstream in(path,std::ios::binary);require(bool(in),"actual trace file");std::vector<unsigned char>d((std::istreambuf_iterator<char>(in)),{});if(d.size()<12)return{};require(std::memcmp(d.data(),"W017T001",8)==0,"actual trace identity");std::size_t at=8;
 const auto u32=[&](){require(at+4<=d.size(),"bounded scalar");std::uint32_t v=0;if(at+4<=d.size())std::memcpy(&v,d.data()+at,4);at+=4;return v;};const auto count=u32();require(count==7,"all seven actual draws");std::vector<Call>out;
 for(unsigned n=0;n<count;++n){for(unsigned j=0;j<4;++j)(void)u32();Call c;c.rect={static_cast<std::int32_t>(u32()),static_cast<std::int32_t>(u32()),static_cast<std::int32_t>(u32()),static_cast<std::int32_t>(u32())};require(u32()==0xffffffU,"original white");require(u32()==(DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX),"original flags");require(u32()==0xffffffffU,"original NUL terminated count");const auto units=u32();require(at+units*2<=d.size(),"bounded actual text");for(unsigned j=0;j<units&&at+2<=d.size();++j){std::uint16_t unit=0;std::memcpy(&unit,d.data()+at,2);at+=2;c.text.push_back(static_cast<wchar_t>(unit));}out.push_back(std::move(c));}
 require(at==d.size(),"exact trace EOF");return out;
}
void state(Dib&d,std::size_t n){
 GdiFlush();std::fill_n(d.pixels,800*600*4,static_cast<unsigned char>(0x5b));SelectObject(d.dc,GetStockObject(SYSTEM_FIXED_FONT));SetBkMode(d.dc,n%3?TRANSPARENT:OPAQUE);SetBkColor(d.dc,RGB(71,37,19));SetTextColor(d.dc,RGB(2,101,73));SetTextAlign(d.dc,TA_LEFT|TA_TOP);SetViewportOrgEx(d.dc,7,-3,nullptr);SelectClipRgn(d.dc,nullptr);if(n%2){auto r=CreateRectRgn(9,11,751,581);require(r!=nullptr,"clip allocation");SelectClipRgn(d.dc,r);require(DeleteObject(r)!=FALSE,"clip release");}
}
int main(int argc,char**argv){
 require(argc==2,"explicit actual trace");if(argc!=2)return 1;auto calls=inputs(fs::path(argv[1]));if(failures)return 1;const auto actual=calls.size();
 const std::array<std::wstring,8>texts{L"",L"A&B",L"中文 与 Mixed",L"line1\nline2",std::wstring(L"null\0tail",9),std::wstring({wchar_t(0xd83d),wchar_t(0xde00),wchar_t(0xd800),wchar_t(0xdc00)}),L"trailing   ",std::wstring(257,L'W')};
 const std::array<RuntimeInputPixelRect,5>rects{{{10,10,320,78},{-7,-9,80,25},{20,20,21,21},{90,90,40,40},{798,598,830,625}}};
 for(const auto&t:texts)for(auto r:rects)for(bool restore:{false,true})calls.push_back({t,r,restore});const auto total=calls.size();require(total-actual==80,"all80boundarycases");std::uint64_t compared=0;
 const auto stock=GetStockObject(DEFAULT_GUI_FONT);require(stock&&stock!=GetStockObject(SYSTEM_FIXED_FONT),"distinct real stock font and prior selection");LOGFONTW stockBefore{};require(GetObjectW(stock,sizeof(stockBefore),&stockBefore)==sizeof(stockBefore),"real stock LOGFONT");require(mainStockVideoFooterCapability()==MainSystemFontCapability::NativeText,"native text capability");
 {
 Dib a,b;require(!failures,"real DIBs ready");if(failures)return 1;
 auto af=SelectObject(a.dc,stock),bf=SelectObject(b.dc,stock);TEXTMETRICW ma{},mb{};require(GetTextMetricsW(a.dc,&ma)&&GetTextMetricsW(b.dc,&mb)&&std::memcmp(&ma,&mb,sizeof(ma))==0,"real stock metrics exact");SelectObject(a.dc,af);SelectObject(b.dc,bf);
 const auto run=[&](unsigned pass){
  for(std::size_t n=0;n<total;++n){const auto&c=calls[n];state(a,n);state(b,n);const auto beforeA=GetCurrentObject(a.dc,OBJ_FONT),beforeB=GetCurrentObject(b.dc,OBJ_FONT);const auto colorA=GetTextColor(a.dc),colorB=GetTextColor(b.dc);const int modeA=GetBkMode(a.dc),modeB=GetBkMode(b.dc);const int saveA=c.restore?SaveDC(a.dc):0,saveB=c.restore?SaveDC(b.dc):0;if(c.restore)require(saveA&&saveB,"outer save");
   RECT rect{c.rect.left,c.rect.top,c.rect.right,c.rect.bottom};original_main_video_footer::draw(a.dc,c.text.c_str(),rect);
   if(pass%2==0)drawMainStockVideoFooter(b.dc,MainStockFooterWideTextRequest{c.text.c_str(),c.rect});else{const std::u16string text(c.text.begin(),c.text.end());drawMainStockVideoFooter(b.dc,MainStockFooterTextRequest{text.c_str(),c.rect});}
   GdiFlush();require(std::equal(a.pixels,a.pixels+800*600*4,b.pixels),"whole original/current DIB exact");require(GetCurrentObject(a.dc,OBJ_FONT)==stock&&GetCurrentObject(b.dc,OBJ_FONT)==stock,"original stock remains selected inside boundary");require(GetBkMode(a.dc)==TRANSPARENT&&GetBkMode(b.dc)==TRANSPARENT,"original background mutation including empty");require(GetTextColor(a.dc)==RGB(255,255,255)&&GetTextColor(b.dc)==RGB(255,255,255),"original white including empty");require(GetBkColor(a.dc)==GetBkColor(b.dc)&&GetTextAlign(a.dc)==GetTextAlign(b.dc),"other DC state exact");
   HRGN ra=CreateRectRgn(0,0,0,0),rb=CreateRectRgn(0,0,0,0);require(GetClipRgn(a.dc,ra)==GetClipRgn(b.dc,rb)&&EqualRgn(ra,rb),"clip exact");require(DeleteObject(ra)&&DeleteObject(rb),"clip query cleanup");
   if(saveA)require(RestoreDC(a.dc,saveA)!=FALSE,"original outer restore");if(saveB)require(RestoreDC(b.dc,saveB)!=FALSE,"current outer restore");
   if(c.restore){require(GetCurrentObject(a.dc,OBJ_FONT)==beforeA&&GetCurrentObject(b.dc,OBJ_FONT)==beforeB,"outer font restore");require(GetTextColor(a.dc)==colorA&&GetTextColor(b.dc)==colorB&&GetBkMode(a.dc)==modeA&&GetBkMode(b.dc)==modeB,"outer mode/color restore");}
   else require(GetCurrentObject(b.dc,OBJ_FONT)==stock&&GetTextColor(b.dc)==RGB(255,255,255)&&GetBkMode(b.dc)==TRANSPARENT,"unrestored boundary side effects exact, no injected SaveDC failure claim");
   if(pass>=2)compared+=800ULL*600*4;
  }
 };
 run(0);const auto warm1=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);run(1);const auto warm2=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);require(warm1==warm2,"full sequence warm stable");const auto gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);DWORD handlesBefore=0;require(GetProcessHandleCount(GetCurrentProcess(),&handlesBefore)!=FALSE,"handles before");run(2);run(3);
 for(unsigned n=0;n<128;++n){state(b,n);const int saved=SaveDC(b.dc);require(saved!=0,"cycle outer save");drawMainStockVideoFooter(b.dc,MainStockFooterWideTextRequest{L"（按 空格 跳过）",{0,0,800,32}});require(RestoreDC(b.dc,saved)!=FALSE,"cycle outer restore");LOGFONTW current{};require(GetStockObject(DEFAULT_GUI_FONT)==stock&&GetObjectW(stock,sizeof(current),&current)==sizeof(current)&&std::memcmp(&current,&stockBefore,sizeof(current))==0,"stock identity alive and parameters unchanged");}
 DWORD handlesAfter=0;require(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)!=FALSE,"handles after");const auto gdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);require(gdiBefore==gdiAfter&&handlesBefore==handlesAfter,"strict GDI/process handles equality");
 std::cout<<"Main video footer native: checks="<<checks<<" actualCalls="<<actual<<" boundaryCases=80 fullFramePairs="<<total*2<<" comparedBytes="<<compared<<" gdiBefore="<<gdiBefore<<" gdiAfter="<<gdiAfter<<" handlesBefore="<<handlesBefore<<" handlesAfter="<<handlesAfter<<" stockBorrowOnly=yes nulTerminatedMinus1=yes outerRestoreAndUnrestoredEffects=yes utf16Wide=yes failures="<<failures<<" skipped=0\n";
 }
 require(failures==0,"all final DIB cleanup assertions");return failures?1:0;
}
