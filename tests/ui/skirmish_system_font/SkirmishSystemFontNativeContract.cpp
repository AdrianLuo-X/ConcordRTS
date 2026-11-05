#include "OriginalSkirmishSystemFontReference.h"
#include "ui/text/win32/RuntimeSkirmishSystemFontWin32.h"
#include <array>
#include <cstring>
#include <iostream>
#include <type_traits>
#include <vector>
#include <algorithm>
using namespace concord::ui;using namespace concord::ui::win32;
static_assert(sizeof(SkirmishSystemFontsWin32)==4*sizeof(HFONT));
static_assert(alignof(SkirmishSystemFontsWin32)==alignof(HFONT));
static_assert(!std::is_copy_constructible_v<SkirmishSystemFontsWin32>);
unsigned checks=0,failures=0;
void require(bool v,const char* why){++checks;if(!v){++failures;std::cerr<<why<<"\n";}}
HFONT oldFont(const original_skirmish_system_font::MenuState& s,unsigned n){return n==0?s.titleFont:n==1?s.captionFont:n==2?s.normalFont:s.smallFont;}
std::uint64_t nativeEditComparedBytes=0;unsigned nativeEditPairs=0;

void compareNativeEditFont(HFONT oldFontValue,SkirmishSystemFontsWin32& now){
 HWND host=CreateWindowExW(0,L"STATIC",L"Owned font binding reference",WS_OVERLAPPED,0,0,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 require(host!=nullptr,"owned native edit parent");
 auto make=[&](){return CreateWindowExW(0,L"EDIT",L"中文 é font 123",WS_CHILD|WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,0,0,368,25,host,nullptr,GetModuleHandleW(nullptr),nullptr);};
 HWND oldEdit=make(),newEdit=make();require(oldEdit&&newEdit,"actual native edit controls");
 if(oldEdit&&newEdit){
  SendMessageW(oldEdit,WM_SETFONT,reinterpret_cast<WPARAM>(oldFontValue),FALSE);
  now.bindNativeControl(newEdit,SkirmishSystemFontRole::Normal);
  require(reinterpret_cast<HFONT>(SendMessageW(oldEdit,WM_GETFONT,0,0))==oldFontValue,"original native edit font borrow");
  require(reinterpret_cast<HFONT>(SendMessageW(newEdit,WM_GETFONT,0,0))==now.nativeFont(SkirmishSystemFontRole::Normal),"same production binding consumed by native EDIT");
  SetWindowPos(oldEdit,nullptr,10,10,368,25,SWP_NOZORDER|SWP_NOACTIVATE);SetWindowPos(newEdit,nullptr,10,55,368,25,SWP_NOZORDER|SWP_NOACTIVATE);
  ShowWindow(host,SW_SHOWNOACTIVATE);ShowWindow(oldEdit,SW_SHOWNOACTIVATE);ShowWindow(newEdit,SW_SHOWNOACTIVATE);UpdateWindow(host);RedrawWindow(oldEdit,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_FRAME);RedrawWindow(newEdit,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_FRAME);
  RECT a{},b{};GetClientRect(oldEdit,&a);GetClientRect(newEdit,&b);require(EqualRect(&a,&b)!=FALSE&&a.right>0&&a.bottom>0,"same real edit client");
  const auto capture=[&](HWND edit){std::vector<unsigned char> bytes;HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=a.right;info.bmiHeader.biHeight=-a.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);require(dc&&bitmap&&pixels,"owned edit capture DIB");auto old=SelectObject(dc,bitmap);
   std::fill_n(static_cast<unsigned char*>(pixels),static_cast<std::size_t>(a.right*a.bottom*4),static_cast<unsigned char>(0x55));
   HDC source=GetDC(edit);require(source!=nullptr,"real visible native EDIT DC");require(BitBlt(dc,0,0,a.right,a.bottom,source,0,0,SRCCOPY)!=FALSE,"actual visible native EDIT client capture");require(ReleaseDC(edit,source)!=0,"release real EDIT DC");require(GdiFlush()!=FALSE,"complete actual EDIT rendering");const auto* data=static_cast<const unsigned char*>(pixels);bytes.assign(data,data+static_cast<std::size_t>(a.right*a.bottom*4));require(SelectObject(dc,old)==bitmap,"edit capture restore selection");require(DeleteObject(bitmap)!=FALSE&&DeleteDC(dc)!=FALSE,"edit capture release");return bytes;};
  const auto x=capture(oldEdit),y=capture(newEdit);require(x.size()==y.size()&&!x.empty(),"whole native edit dimensions");std::uint64_t bgr=0,ink=0,bright=0,changed=0;for(std::size_t n=0;n<x.size();++n)if(n%4!=3){require(x[n]==y[n],"whole native edit BGR exact");++bgr;if(x[n]<128)++ink;if(x[n]>220)++bright;if(x[n]!=0x55)++changed;}
  require(ink>20&&bright>20&&changed>100,"actual EDIT painted nonuniform text/background beyond sentinel");nativeEditComparedBytes+=bgr;++nativeEditPairs;
 }
 require(DestroyWindow(host)!=FALSE,"native parent destroys actual font borrowers");require(!IsWindow(oldEdit)&&!IsWindow(newEdit),"borrowers gone before font release");GdiFlush();
}

void cycle(bool inspectEdit=false){
 SkirmishGdiReleaseTrace oldTrace,newTrace;std::array<HFONT,4> oldHandles{},newHandles{};
 {
  original_skirmish_system_font::MenuState old;old.gdiReleaseTrace=&oldTrace;
  original_skirmish_system_font::initializeFonts(old);
  SkirmishSystemFontsWin32 now;now.initialize();
  require(now.canvasTextSupport()==SkirmishCanvasSystemTextSupport::BitmapOnly,"actual scene no system text fallback");
  HDC dc=CreateCompatibleDC(nullptr);require(dc!=nullptr,"owned metric DC");const auto selected=GetCurrentObject(dc,OBJ_FONT);
  for(unsigned n=0;n<4;++n){
   auto role=static_cast<SkirmishSystemFontRole>(n);oldHandles[n]=oldFont(old,n);newHandles[n]=now.nativeFont(role);require(oldHandles[n]!=nullptr&&newHandles[n]!=nullptr&&now.available(role),"actual four font objects");
   require(GetObjectType(oldHandles[n])==OBJ_FONT&&GetObjectType(newHandles[n])==OBJ_FONT,"actual font type");
   LOGFONTW a{},b{};require(GetObjectW(oldHandles[n],sizeof(a),&a)==sizeof(a)&&GetObjectW(newHandles[n],sizeof(b),&b)==sizeof(b),"actual LOGFONT");
   require(std::memcmp(&a,&b,sizeof(a))==0,"original face height quality weight flags exact");
   SelectObject(dc,oldHandles[n]);TEXTMETRICW ma{},mb{};require(GetTextMetricsW(dc,&ma)!=FALSE,"original metrics");SelectObject(dc,newHandles[n]);require(GetTextMetricsW(dc,&mb)!=FALSE,"current metrics");require(std::memcmp(&ma,&mb,sizeof(ma))==0,"actual metrics exact");SelectObject(dc,selected);
  }
  require(GetCurrentObject(dc,OBJ_FONT)==selected,"borrowed selection restored before release");
  require(DeleteDC(dc)!=FALSE,"owned DC release");if(inspectEdit)compareNativeEditFont(old.normalFont,now);now.reset(&newTrace);
  for(unsigned n=0;n<4;++n)require(!now.available(static_cast<SkirmishSystemFontRole>(n)),"roles empty after reset");
  now.reset(&newTrace);require(newTrace.count==4,"idempotent reset no repeated trace/deletion");
 }
 const std::array<int,7> order{3,4,5,6,0,0,0};require(oldTrace.count==4&&newTrace.count==4,"exact four actual releases");
 require(oldTrace.order==order&&newTrace.order==order,"original3/4/5/6 deletion order");
 for(unsigned n=0;n<4;++n){require(oldTrace.succeeded[n+3]&&newTrace.succeeded[n+3],"original/native DeleteObject succeeded");LOGFONTW oldGone{},newGone{};require(GetObjectW(oldHandles[n],sizeof(oldGone),&oldGone)==0&&GetObjectW(newHandles[n],sizeof(newGone),&newGone)==0,"deleted font LOGFONT queries fail");}
}
int main(){
 {SkirmishSystemFontsWin32 empty;SkirmishGdiReleaseTrace trace;empty.reset(&trace);require(trace.count==0,"nativeFonts false owns/deletes nothing");for(unsigned n=0;n<4;++n)require(!empty.available(static_cast<SkirmishSystemFontRole>(n)),"initial role unavailable");}
 cycle(true);const auto warm1=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);cycle(true);const auto warm2=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);require(warm1==warm2,"complete ownership/metrics warm stable");auto before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);DWORD hb=0;require(GetProcessHandleCount(GetCurrentProcess(),&hb)!=FALSE,"handles before");
 for(unsigned n=0;n<128;++n)cycle(n==0);
 DWORD ha=0;require(GetProcessHandleCount(GetCurrentProcess(),&ha)!=FALSE,"handles after");auto after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);require(before==after,"native GDI lifetime stable");require(hb==ha,"native process handles stable");
 std::cout<<"Skirmish system font owner: checks="<<checks<<" roles=4 cycles=128 createParameters=original releaseOrder=3,4,5,6 customCanvasBitmapOnly=yes nativeEditBorrow=normal legacyGdiDrawingCredit=0 gdiBefore="<<before<<" gdiAfter="<<after<<" handlesBefore="<<hb<<" handlesAfter="<<ha<<" nativeEditPairs="<<nativeEditPairs<<" nativeEditBgrBytes="<<nativeEditComparedBytes<<" failures="<<failures<<" skipped=0\n";return failures?1:0;
}
