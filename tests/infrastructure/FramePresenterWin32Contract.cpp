#include "runtime/presentation/win32/FramePresenterWin32.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <string>
#include "runtime/performance/RuntimeFpsDisplay.h"
RECT runtimeMapViewport(const RECT& r){auto v=r;v.right-=240;return v;}
#include "runtime/performance/RuntimeFpsDisplay.inl"
using namespace concord::presentation;
using namespace concord::presentation::win32;
using namespace concord::ui::win32;
namespace {
int checks=0,mode=0,blits=0,flushes=0;
void need(bool v,const char* s){++checks;if(!v)throw std::runtime_error(s);}
BOOL WINAPI blit(HDC a,int x,int y,int w,int h,HDC b,int sx,int sy,DWORD op){++blits;return mode==1?FALSE:BitBlt(a,x,y,w,h,b,sx,sy,op);}
BOOL WINAPI flush(){++flushes;return mode==2?FALSE:GdiFlush();}
HBITMAP WINAPI deviceBitmap(HDC dc,int w,int h){return mode==3?nullptr:CreateCompatibleBitmap(dc,w,h);}
}
namespace {
int overlayBlits=0,overlayFail=0,overlayFlushes=0,overlayFlushFail=0,overlayDcs=0;
BOOL WINAPI overlayBlt(HDC a,int x,int y,int w,int h,HDC b,int sx,int sy,DWORD op){
    if(++overlayBlits==overlayFail)return FALSE;return BitBlt(a,x,y,w,h,b,sx,sy,op);
}
BOOL WINAPI overlayFlush(){if(++overlayFlushes==overlayFlushFail)return FALSE;return GdiFlush();}
HDC WINAPI overlayDc(HWND w){++overlayDcs;return GetDC(w);}
bool sameColor(concord::ui::ConstMenuPixelSurfaceView a,concord::ui::ConstMenuPixelSurfaceView b){
    if(!a.valid()||!b.valid()||a.width!=b.width||a.height!=b.height)return false;
    for(int y=0;y<a.height;++y)for(int x=0;x<a.width;++x)for(int c=0;c<3;++c)
        if(a.bytes[static_cast<std::size_t>(y)*a.rowStride+static_cast<std::size_t>(x)*4+c]
            !=b.bytes[static_cast<std::size_t>(y)*b.rowStride+static_cast<std::size_t>(x)*4+c])return false;
    return true;
}
void nativeFontWindowContract(){
    using SetDpi=BOOL(WINAPI*)(HANDLE);const auto dpi=reinterpret_cast<SetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext"));
    need(dpi!=nullptr&&dpi(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)))!=FALSE,"same original product PMv2 DPI mode before any DC");
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Concord owned native overlay contract",WS_POPUP,work.left+8,work.top+8,400,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    need(window!=nullptr,"real device font window");SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    HDC native=GetDC(window);need(native!=nullptr,"real device font DC");const RECT client{0,0,400,200};const auto bounds=runtimeFpsDisplayRect(client);
    RuntimeFpsDisplayMeter meter;meter.recordPresentedFrame(10);meter.recordPresentedFrame(10.5);meter.recordPresentedFrame(11);
    {
        MainBackBufferWin32 clean,completed,expected,before,after;
        need(clean.ensure(native,400,200)&&completed.ensure(native,400,200)&&expected.ensure(native,400,200)&&before.ensure(native,400,200)&&after.ensure(native,400,200),"complete native font proof storage");
        auto* p=static_cast<unsigned char*>(clean.pixels);for(std::size_t i=0;i<400u*200u*4u;++i)p[i]=static_cast<unsigned char>((i*23)%256);
        need(BitBlt(native,0,0,400,200,clean.memoryDc,0,0,SRCCOPY)!=FALSE,"independent old scene on window");drawRuntimeFpsDisplay(native,client,meter);need(GdiFlush()!=FALSE,"independent old device font completion");need(BitBlt(expected.memoryDc,0,0,400,200,native,0,0,SRCCOPY)!=FALSE,"independent old complete window readback");
        auto* sentinel=static_cast<unsigned char*>(before.pixels);for(std::size_t i=0;i<400u*200u*4u;++i)sentinel[i]=static_cast<unsigned char>((i*31+79)%256);
        struct DrawContext{const RECT* client;const RuntimeFpsDisplayMeter* meter;bool throws=false;}context{&client,&meter};
        const auto draw=+[](HDC target,void* cookie){const auto& c=*static_cast<DrawContext*>(cookie);drawRuntimeFpsDisplay(target,*c.client,*c.meter);if(c.throws){const auto r=runtimeFpsDisplayRect(*c.client);IntersectClipRect(target,r.left,r.top,r.left+1,r.top+1);SetViewportOrgEx(target,7,9,nullptr);throw std::runtime_error("injected overlay draw failure after native DC mutation");}};
        SkirmishBackBufferWin32 saved;WindowOverlayGdiApi api;api.getDc=overlayDc;api.bitBlt=overlayBlt;api.flush=overlayFlush;
        const RuntimeInputPixelRect region{bounds.left,bounds.top,bounds.right,bounds.bottom};
        const auto reset=[&]{need(BitBlt(native,0,0,400,200,before.memoryDc,0,0,SRCCOPY)!=FALSE&&BitBlt(completed.memoryDc,0,0,400,200,clean.memoryDc,0,0,SRCCOPY)!=FALSE,"proof sentinel/reset");completed.beginWrite();overlayBlits=overlayFlushes=overlayDcs=0;overlayFail=overlayFlushFail=0;context.throws=false;};
        const auto restored=[&]{need(BitBlt(after.memoryDc,0,0,400,200,native,0,0,SRCCOPY)!=FALSE,"read restored actual window");need(sameColor(before.completedPixels(),after.completedPixels()),"entire actual window restored before frame leaves private font bridge");};
        for(int repeat=0;repeat<20;++repeat){reset();need(composeWindowDeviceOverlayWin32(window,clean,completed,saved,region,draw,&context,api),"device font complete overlay success");restored();need(sameColor(expected.completedPixels(),completed.completedPixels()),"all complete BGR bytes equal independent original PMv2 window font");}
        for(int fail:{1,2,3}){reset();overlayFail=fail;need(!composeWindowDeviceOverlayWin32(window,clean,completed,saved,region,draw,&context,api),"snapshot/base/readback failure refused");restored();}
        reset();overlayFlushFail=2;need(!composeWindowDeviceOverlayWin32(window,clean,completed,saved,region,draw,&context,api),"overlay flush failure refused");restored();
        reset();context.throws=true;need(!composeWindowDeviceOverlayWin32(window,clean,completed,saved,region,draw,&context,api),"draw exception refused");restored();
        reset();bool foreign=true;std::jthread thread([&]{foreign=composeWindowDeviceOverlayWin32(window,clean,completed,saved,region,draw,&context,api);});thread.join();need(!foreign&&overlayDcs==0,"foreign thread refused before native DC or pixel access");
        auto invalid=region;invalid.right=401;need(!composeWindowDeviceOverlayWin32(window,clean,completed,saved,invalid,draw,&context,api)&&overlayDcs==0,"out of bounds overlay refused");
        reset();need(composeWindowDeviceOverlayWin32(window,clean,completed,saved,region,draw,&context,api),"recovered native font composition");FramePresentStatistics statistics;DibFramePresenterWin32 presenter(native,completed,statistics);SoftwareFrameView frame{completed.completedPixels(),1,1,0,SoftwareFrameContent::Redrawn};need(presenter.present(frame)==FramePresentResult::Presented&&statistics.successful==1,"only final complete presenter counts successful frame");need(BitBlt(after.memoryDc,0,0,400,200,native,0,0,SRCCOPY)!=FALSE&&sameColor(after.completedPixels(),expected.completedPixels()),"actual final displayed full frame exact after native font bridge");
    }
    need(ReleaseDC(window,native)!=0&&DestroyWindow(window)!=FALSE,"native font proof own window released");
    std::cout<<"Native window overlay passed PMv2 exactColor=whole-client snapshotRestore=whole-client failures=copy,readback,flush,throw foreign=noDC fontInit=beforeResourceBaseline\n";
}
}
int main()try{
    nativeFontWindowContract();
    const auto start=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);HDC screen=GetDC(nullptr);need(screen!=nullptr,"actual display reference");
    {
        MainBackBufferWin32 source,target;need(source.ensure(screen,37,23)&&target.ensure(screen,37,23),"actual DIB allocation");
        source.beginWrite();auto* bytes=static_cast<unsigned char*>(source.pixels);const auto size=source.pixelView().bytes.size();for(std::size_t i=0;i<size;++i)bytes[i]=static_cast<unsigned char>((i*19)%256);
        FramePresentStatistics counters;DibPresentGdiApi api;api.bitBlt=blit;api.flush=flush;DibFramePresenterWin32 presenter(target.memoryDc,source,counters,api);
        SoftwareFrameView frame{source.completedPixels(),1,1,127,SoftwareFrameContent::Redrawn};need(presenter.present(frame)==FramePresentResult::Presented,"actual complete software frame adopted");
        const auto result=target.completedPixels();need(result.valid(),"target completion");for(std::size_t i=0;i<size;++i)if(i%4!=3)need(result.bytes[i]==frame.pixels.bytes[i],"all final BGR bytes exact");
        const auto calls=blits;need(presenter.present(frame)==FramePresentResult::Rejected&&blits==calls,"duplicate never reaches GDI");
        frame.sequence=2;frame.content=SoftwareFrameContent::ReusedScene;need(presenter.present(frame)==FramePresentResult::Presented&&counters.successful==2,"paused/reused frame counts success");
        mode=1;frame.sequence=3;need(presenter.present(frame)==FramePresentResult::Failed&&counters.successful==2,"native BitBlt failure not counted");
        mode=2;frame.sequence=4;need(presenter.present(frame)==FramePresentResult::Failed&&counters.successful==2,"native flush failure not counted");mode=0;
        const auto attempts=counters.attempted;frame.sequence=5;FramePresentResult foreign=FramePresentResult::Presented;
        std::jthread other([&]{foreign=presenter.present(frame);});other.join();need(foreign==FramePresentResult::Rejected&&counters.attempted==attempts,"foreign worker refused before any stats/GDI access");
        source.beginWrite();need(presenter.present(frame)==FramePresentResult::Failed&&counters.successful==2,"next UI writer invalidates old lease before read");
        frame.sequence=6;frame.pixels=source.completedPixels();need(presenter.present(frame)==FramePresentResult::Presented&&counters.successful==3,"fresh same-storage lease succeeds");
        const auto old=frame.pixels;need(source.ensure(screen,38,24),"actual source resize");frame.sequence=7;frame.pixels=old;const auto beforeBlit=blits;
        need(presenter.present(frame)==FramePresentResult::Failed&&blits==beforeBlit,"expired resize lease rejected before pixel/native access");
        source.release();frame.sequence=8;need(presenter.present(frame)==FramePresentResult::Failed&&blits==beforeBlit,"released source rejected");
        SkirmishBackBufferGdiApi allocationApi;allocationApi.createDeviceBitmap=deviceBitmap;SkirmishBackBufferWin32 overlay(allocationApi);
        mode=3;need(!overlay.ensureDeviceBitmap(screen,96,22)&&!overlay.ready(),"device HUD allocation failure rollback");mode=0;
        for(int i=0;i<50;++i){need(overlay.ensureDeviceBitmap(screen,96+i%3,22),"device HUD repeated resize");need(overlay.ready()&&overlay.pixels()==nullptr,"device bitmap never exposed as CPU storage");need(overlay.finishGdiWrites(),"device HUD completes native writes");}
        need(overlay.ensureDeviceBitmap(screen,96,22),"device bitmap fixed dimensions before mode switch");
        need(overlay.ensure(screen,96,22)&&overlay.pixels()!=nullptr,"same dimensions switching DDB to DIB actually reallocates correct storage");
    }
    ReleaseDC(nullptr,screen);GdiFlush();need(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==start,"all native resources balanced");
    std::cout<<"DIB frame presenter contract passed checks="<<checks<<" pixels=all-BGR wrongThread=rejected stale=write,resize,release duplicate=rejected failures=blit,flush,allocation successCount=exact GDI=balanced\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}