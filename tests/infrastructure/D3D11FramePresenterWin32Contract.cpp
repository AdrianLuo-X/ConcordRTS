#include "runtime/presentation/win32/D3D11FramePresenterWin32.h"
#include "runtime/performance/RuntimeFpsDisplay.h"
#include <dwmapi.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <thread>
RECT runtimeMapViewport(const RECT& r){auto v=r;v.right-=240;return v;}
#include "runtime/performance/RuntimeFpsDisplay.inl"
using namespace concord::presentation;
using namespace concord::presentation::win32;
using concord::ui::win32::MainBackBufferWin32;
namespace {
int checks=0,failures=0;
void check(bool ok,const char* what){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<what<<'\n';}}
void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
void activationRoundTrip(HWND mainWindow){
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    const std::wstring title=L"Concord S39 activation peer "+std::to_wstring(GetCurrentProcessId());
    std::wstring command=L"\""+std::wstring(executable)+L"\" --activation-peer "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE,"owned activation peer process");
    if(!child.hProcess)return;
    HWND peer=nullptr;
    for(int i=0;i<500&&!peer;++i){peer=FindWindowW(L"STATIC",title.c_str());pump();if(!peer)Sleep(5);}
    DWORD peerPid=0;if(peer)GetWindowThreadProcessId(peer,&peerPid);
    check(peer&&peerPid==child.dwProcessId,"activation peer native PID verified");
    if(peer&&peerPid==child.dwProcessId){
        SetForegroundWindow(mainWindow);pump();
        check(GetForegroundWindow()==mainWindow,"graphics HWND became foreground");
        SetForegroundWindow(peer);
        for(int i=0;i<50&&GetForegroundWindow()!=peer;++i){pump();Sleep(2);}
        check(GetForegroundWindow()==peer,"actual cross-process application deactivation (Alt-Tab event path)");
        SetForegroundWindow(mainWindow);
        for(int i=0;i<50&&GetForegroundWindow()!=mainWindow;++i){SetForegroundWindow(mainWindow);pump();Sleep(2);}
        check(GetForegroundWindow()==mainWindow,"actual graphics application reactivation");
        PostMessageW(peer,WM_CLOSE,0,0);
    }
    check(WaitForSingleObject(child.hProcess,10000)==WAIT_OBJECT_0,"activation peer normal close");
    DWORD code=STILL_ACTIVE;GetExitCodeProcess(child.hProcess,&code);check(code==0,"activation peer actual exit zero");
    CloseHandle(child.hThread);CloseHandle(child.hProcess);
}
struct Window {
    HWND handle=nullptr;
    Window(int w,int h){RECT area{};SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);
        handle=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"Concord owned D3D11 contract",WS_POPUP,
            area.left+12,area.top+12,w,h,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(handle!=nullptr,"native window created");show(w,h);}
    void show(int w,int h){SetWindowPos(handle,HWND_TOPMOST,0,0,w,h,SWP_NOMOVE|SWP_NOACTIVATE|SWP_SHOWWINDOW);pump();}
    ~Window(){if(IsWindow(handle))DestroyWindow(handle);pump();}
};
std::vector<unsigned char> pattern(int w,int h,std::size_t stride){
    std::vector<unsigned char> v(stride*static_cast<std::size_t>(h),0xAB);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto* p=v.data()+static_cast<std::size_t>(y)*stride+static_cast<std::size_t>(x)*4;
        p[0]=static_cast<unsigned char>((x*19+y*7)%256);p[1]=static_cast<unsigned char>((x*3+y*29)%256);
        p[2]=static_cast<unsigned char>((x*31+y*11)%256);p[3]=static_cast<unsigned char>((x+y*5)%256);}
    return v;
}
SoftwareFrameView frame(std::vector<unsigned char>& v,int w,int h,std::size_t stride,std::uint64_t seq){
    return {{{v.data(),v.size()},w,h,stride,seq},seq,1,120,SoftwareFrameContent::Redrawn};
}
bool same(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b,int w,int h,std::size_t stride,bool alpha){
    if(a.size()!=static_cast<std::size_t>(w)*static_cast<std::size_t>(h)*4)return false;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)for(int c=0;c<(alpha?4:3);++c)
        if(a[static_cast<std::size_t>((y*w+x)*4+c)]!=b[static_cast<std::size_t>(y)*stride+static_cast<std::size_t>(x*4+c)])return false;
    return true;
}
std::vector<unsigned char> visible(HWND w,int width,int height){
    DwmFlush();HDC dc=GetDC(w);MainBackBufferWin32 read;
    check(read.ensure(dc,width,height),"visible capture DIB allocation");
    check(BitBlt(read.memoryDc,0,0,width,height,dc,0,0,SRCCOPY)!=FALSE,"visible capture copy");
    auto v=read.completedPixels();std::vector<unsigned char> result(v.bytes.begin(),v.bytes.end());ReleaseDC(w,dc);return result;
}
void deviceCase(FrameDevice driver){
    Window w(97,61);FramePresentStatistics stats;D3D11FrameProbeWin32 probe;probe.captureUpload=true;
    auto p=D3D11FramePresenterWin32::create(w.handle,{FrameBackend::D3D11,driver},stats,&probe);
    check(p&&p->active(),"real D3D11 device and swap chain");if(!p||!p->active())return;
    check(p->report().softwareDevice==(driver==FrameDevice::Warp),"actual hardware/WARP classification");
    check(p->report().featureLevel>=0xB000,"minimum feature level");
    std::uint64_t seq=1;
    for(auto size:{std::pair{97,61},std::pair{800,600},std::pair{1280,720},std::pair{799,599},std::pair{97,61}}){
        w.show(size.first,size.second);const auto stride=static_cast<std::size_t>(size.first)*4+13;
        auto bytes=pattern(size.first,size.second,stride);const auto before=bytes;
        auto f=frame(bytes,size.first,size.second,stride,seq++);
        check(p->present(f)==FramePresentResult::Presented,"actual CPU frame present");
        check(same(probe.uploadedPixels,bytes,size.first,size.second,stride,true),"all BGRA upload bytes exact incl alpha/odd stride");
        check(bytes==before,"source and padding immutable");
        check(same(visible(w.handle,size.first,size.second),bytes,size.first,size.second,stride,false),"all visible client BGR exact");
        check(p->present(f)==FramePresentResult::Rejected,"duplicate sequence rejected");
        const auto successful=stats.successful;
        std::thread other([&]{auto next=f;next.sequence=seq+100;check(p->present(next)==FramePresentResult::Rejected,"foreign thread rejected");});other.join();
        check(stats.successful==successful,"rejections do not count");
    }
    check(p->timingCount()==0&&probe.readbacks==5,"diagnostic readback excluded from performance");
    probe.captureUpload=false;
    auto bytes=pattern(97,61,97*4);auto f=frame(bytes,97,61,97*4,seq++);
    ShowWindow(w.handle,SW_MINIMIZE);pump();check(p->present(f)==FramePresentResult::Failed,"minimized not counted");
    ShowWindow(w.handle,SW_RESTORE);w.show(97,61);
    activationRoundTrip(w.handle);
    Window peer(50,50);SetForegroundWindow(peer.handle);SetFocus(peer.handle);pump();
    w.show(97,61);
    for(int i=0;i<24;++i){f.sequence=seq++;check(p->present(f)==FramePresentResult::Presented,"normal present without readback");}
    check(probe.readbacks==5,"normal path never reads GPU pixels");
    check(p->report().gpuValid>0,"real asynchronous valid GPU timestamps");
    check(p->timingCount()==24,"bounded per-submit timing samples");
    DestroyWindow(w.handle);f.sequence=seq++;
    check(p->present(f)==FramePresentResult::Failed&&!p->active(),"closed window retires GPU safely");
    p.reset();
}
void fontCase(){
    Window gpu(800,600);FramePresentStatistics stats;D3D11FrameProbeWin32 probe;probe.captureUpload=true;
    auto p=D3D11FramePresenterWin32::create(gpu.handle,{FrameBackend::D3D11,FrameDevice::Hardware},stats,&probe);
    check(p&&p->active(),"font case device");if(!p||!p->active())return;
    // Separate opaque oracle HWND never has a GPU chain.
    Window oracle(800,600);HDC native=GetDC(oracle.handle);
    MainBackBufferWin32 clean,completed,expected;
    check(clean.ensure(native,800,600)&&completed.ensure(native,800,600)&&expected.ensure(native,800,600),"font CPU owners");
    auto bytes=pattern(800,600,800*4);std::copy(bytes.begin(),bytes.end(),static_cast<unsigned char*>(clean.pixels));
    RECT client{0,0,800,600};const auto r=runtimeFpsDisplayRect(client);
    for(int fps:{0,1,2,59,60,144,999,9999,10000}){
        RuntimeFpsDisplayMeter meter;if(fps){meter.recordPresentedFrame(0);for(int i=1;i<=fps;++i)meter.recordPresentedFrame(static_cast<double>(i)/fps);}
        BitBlt(native,0,0,800,600,clean.memoryDc,0,0,SRCCOPY);drawRuntimeFpsDisplay(native,client,meter);GdiFlush();
        BitBlt(expected.memoryDc,0,0,800,600,native,0,0,SRCCOPY);GdiFlush();
        BitBlt(completed.memoryDc,0,0,800,600,clean.memoryDc,0,0,SRCCOPY);
        struct C{RECT client;RuntimeFpsDisplayMeter meter;} c{client,meter};
        const auto draw=+[](HDC dc,void* cookie){const auto& x=*static_cast<C*>(cookie);drawRuntimeFpsDisplay(dc,x.client,x.meter);};
        check(p->composeDeviceOverlay(clean,completed,{r.left,r.top,r.right,r.bottom},draw,&c),"private transparent device font composition");
        const auto e=expected.completedPixels(),a=completed.completedPixels();
        bool exact=e.valid()&&a.valid();for(std::size_t i=0;exact&&i<e.bytes.size();++i)if(i%4!=3&&a.bytes[i]!=e.bytes[i])exact=false;
        check(exact,"complete CPU frame font colors exact without touching DXGI HWND");
    }
    ReleaseDC(oracle.handle,native);
}
#include "FrameLifetimeD3D11Checks.inl"
void fallbackCases(){
    for(int failure=0;failure<8;++failure){
        Window w(97,61);FramePresentStatistics stats;D3D11FrameProbeWin32 probe;
        probe.failDevice=failure==0;probe.failSwapChain=failure==1;probe.failResource=failure==2;
        auto p=D3D11FramePresenterWin32::create(w.handle,{FrameBackend::D3D11,FrameDevice::Hardware},stats,&probe);
        check(p!=nullptr,"fallback owner exists");if(!p)continue;
        auto bytes=pattern(97,61,97*4);auto f=frame(bytes,97,61,97*4,1);
        if(failure>=3){probe.failResize=failure==3;probe.failUpload=failure==4;probe.failPresent=failure==5;probe.deviceRemoved=failure==6;
            if(failure==7)p->fallbackToSoftware("CPU-complete-frame-allocation",E_OUTOFMEMORY);
            else check(p->present(f)==FramePresentResult::Failed,"injected resource/loss/present failure returned");}
        check(!p->active()&&p->report().selected==FrameBackend::Software&&p->report().fallbackReason[0],"permanent explicit software fallback");
        HDC dc=GetDC(w.handle);MainBackBufferWin32 buffer;check(buffer.ensure(dc,97,61),"software fallback CPU owner");
        std::copy(bytes.begin(),bytes.end(),static_cast<unsigned char*>(buffer.pixels));
        auto pixels=buffer.completedPixels();f.pixels=pixels;f.sequence=2;
        DibFramePresenterWin32 software(dc,buffer,stats);
        check(software.present(f)==FramePresentResult::Presented,"original GDI really presents after full GPU release");
        check(same(visible(w.handle,97,61),bytes,97,61,97*4,false),"actual fallback visible pixels exact");
        check(stats.successful==1,"failed GPU submission not counted as successful");ReleaseDC(w.handle,dc);
    }
}
}
int main(int argc,char** argv){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(argc==3&&std::string(argv[1])=="--activation-peer"){
        Window peer(50,50);const std::wstring title=L"Concord S39 activation peer "+std::wstring(argv[2],argv[2]+std::char_traits<char>::length(argv[2]));SetWindowTextW(peer.handle,title.c_str());
        const DWORD parent=static_cast<DWORD>(std::stoul(argv[2]));
        const auto started=GetTickCount64();while(IsWindow(peer.handle)&&GetTickCount64()-started<12000){pump();if(GetForegroundWindow()==peer.handle)AllowSetForegroundWindow(parent);Sleep(2);}return IsWindow(peer.handle)?1:0;
    }
    deviceCase(FrameDevice::Hardware);deviceCase(FrameDevice::Warp);fontCase();
    const DWORD gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),userBefore=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    fallbackCases();fontCase();lifetimeCase(FrameDevice::Hardware);lifetimeCase(FrameDevice::Warp);
    check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gdiBefore,"all fallback/font GDI resources balanced");
    check(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)==userBefore,"all fallback/font USER resources balanced");
    std::cout<<"D3D11 complete frame contract checks="<<checks<<" failures="<<failures
        <<" upload=all-BGRA visible=all-BGR shader=none driver=hardware-and-WARP readback=diagnostic-only fallback=actual-GDI\n";
    return failures?1:0;
}
