#include "ui/radar/win32/RuntimeRadarPixelStoreWin32.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>
static_assert(sizeof(BOOL)==4&&sizeof(COLORREF)==4);
static_assert(std::is_same_v<decltype(RuntimeRadarPixelStoreSummary::stores),std::uint64_t>);
namespace {
unsigned checks=0,failures=0,flushCalls=0;void check(bool v){++checks;if(!v)++failures;}
BOOL WINAPI countedFlush(){++flushCalls;return GdiFlush();}
BOOL WINAPI failedFlush(){++flushCalls;return FALSE;}
struct Dib {
 HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;void* pixels=nullptr;
 Dib(){dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=19;info.bmiHeader.biHeight=-13;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;if(dc)bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(bitmap)old=SelectObject(dc,bitmap);}
 bool valid()const{return dc&&bitmap&&old&&old!=HGDI_ERROR&&pixels;}
 void close(){if(old&&old!=HGDI_ERROR){check(SelectObject(dc,old)!=nullptr);old=nullptr;}if(bitmap){check(DeleteObject(bitmap)!=FALSE);bitmap=nullptr;}if(dc){check(DeleteDC(dc)!=FALSE);dc=nullptr;}pixels=nullptr;}
 ~Dib(){close();}
};
struct Probe {int phase=0;unsigned prepared=0,finished=0;RuntimeRadarPixelStoreSummary final{};};
RuntimeRadarPixelStoreObserver observer(Probe& p){return {&p,[](void* raw,HDC dc,HBITMAP bitmap,int w,int h)noexcept{auto& p=*static_cast<Probe*>(raw);check(p.phase==0&&dc&&bitmap&&w==19&&h==13);p.phase=1;++p.prepared;},[](void* raw,const RuntimeRadarPixelStoreSummary& summary)noexcept{auto& p=*static_cast<Probe*>(raw);check(p.phase==1);p.phase=2;++p.finished;p.final=summary;}};}
}
static_assert(sizeof(RuntimeRadarPixelStoreSummary)==48 && alignof(RuntimeRadarPixelStoreSummary)==8);
static_assert(offsetof(RuntimeRadarPixelStoreSummary,flushCalls)==40 && offsetof(RuntimeRadarPixelStoreSummary,reference)==44 && offsetof(RuntimeRadarPixelStoreSummary,flushSucceeded)==47);
int main(){
 {Dib warm;check(warm.valid());check(GdiFlush()!=FALSE);}const DWORD baseline=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);DWORD handlesBefore=0;check(GetProcessHandleCount(GetCurrentProcess(),&handlesBefore)!=FALSE);
 std::uint64_t stores=0,pairs=0;
 for(int mode=0;mode<3;++mode){
  Dib oracle,actual;check(oracle.valid()&&actual.valid());if(!oracle.valid()||!actual.valid())return 1;check(GdiFlush()!=FALSE);
  auto* expected=static_cast<std::uint32_t*>(oracle.pixels);auto* obtained=static_cast<std::uint32_t*>(actual.pixels);
  for(unsigned k=0;k<247;++k)expected[k]=obtained[k]=0xa5731100U^(k*7919U);
  Probe probe;auto obs=observer(probe);flushCalls=0;
  {RuntimeRadarPixelStoreObserverScope scope(obs);RuntimeRadarPixelStore writer(actual.dc,actual.bitmap,actual.pixels,19,13,true,mode==1,{mode==2?failedFlush:countedFlush});check(probe.prepared==1&&probe.finished==0&&probe.phase==1);
   check(writer.summary().reference==(mode==1));check(writer.summary().flushCalls==(mode==1?0U:1U));check(flushCalls==(mode==1?0U:1U));
   for(int y=0;y<13;++y)for(int x=0;x<19;++x){COLORREF color=RGB((x*17+y)&255,(y*31+x)&255,(x^y)&255);BOOL a=SetPixelV(oracle.dc,x,y,color),b=writer.store(x,y,color);check(a==b);++stores;}
   for(auto c:std::array<COLORREF,5>{PALETTERGB(10,20,30),RGB(91,7,201),CLR_INVALID,RGB(33,66,99),PALETTEINDEX(0)}){check(SetPixelV(oracle.dc,7,3,c)==writer.store(7,3,c));++stores;}
   check(SetPixelV(oracle.dc,-1,0,RGB(1,2,3))==writer.store(-1,0,RGB(1,2,3)));++stores;
   check(!writer.summary().directEnabled&&writer.summary().nonRgbStores==3&&writer.summary().outsideStores==1);
   check(writer.summary().directStores==(mode==0?247U:0U));check(writer.summary().gdiStores==(mode==0?6U:253U));check(flushCalls==(mode==1?0U:1U));
  }
  check(probe.prepared==1&&probe.finished==1&&probe.phase==2&&probe.final.stores==253);check(GdiFlush()!=FALSE);check(std::equal(expected,expected+247,obtained));++pairs;
 }
 // Flush failure never enables CPU access and preserves the old fallback path.
 // The complete original game fixture independently covers 17 native mapping,
 // identity/clip/bottom-up/24-bit/palette and queued-fill oracle cases.
 for(unsigned cycle=0;cycle<128;++cycle){Dib value;check(value.valid());if(!value.valid())return 1;check(GdiFlush()!=FALSE);flushCalls=0;{RuntimeRadarPixelStore writer(value.dc,value.bitmap,value.pixels,19,13,true,false,{countedFlush});check(writer.store(0,0,RGB(2,4,6))==TRUE);check(writer.summary().directStores==1&&writer.summary().flushCalls==1);}check(flushCalls==1);check(GdiFlush()!=FALSE);}
 check(GdiFlush()!=FALSE);const DWORD final=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);DWORD handlesAfter=0;check(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)!=FALSE);check(baseline==final&&handlesBefore==handlesAfter);
 std::cout<<"Radar pixel native: checks="<<checks<<" nativeOracleWrites="<<stores<<" wholeRawDibPairs="<<pairs<<" flushFailure=exact observerOrder=exact nativeLatch=exact cycles=128 GDI="<<baseline<<'='<<final<<" handles="<<handlesBefore<<'='<<handlesAfter<<" summaryBytes="<<sizeof(RuntimeRadarPixelStoreSummary)<<" failures="<<failures<<" skipped=0\n";
 return failures?1:0;
}
