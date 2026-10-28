#include "ui/surface/win32/MapNativeOverlayWin32.h"
#include "runtime/presentation/win32/D3D11FramePresenterWin32.h"
#include "render/effects/RuntimeScreenShake.h"
#include <iostream>
#include <cstdlib>
#include <thread>
using namespace concord::render;
using namespace concord::ui::win32;
using namespace concord::presentation;
using namespace concord::presentation::win32;
namespace {
void paint(HDC dc,HDC source,int part,COLORREF brushColor) {
    const int saved=SaveDC(dc);SetBkMode(dc,TRANSPARENT);
    if(part==0) {
        RECT box{3,3,310,27};MapNativeOverlayWin32::setBrushColor(dc,brushColor);FillRect(dc,&box,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));SetTextColor(dc,RGB(235,240,245));
        DrawTextW(dc,L"FPS: 37 + native palette",-1,&box,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        const auto clipped=SaveDC(dc);IntersectClipRect(dc,40,20,140,120);
        SetPixelV(dc,12,30,RGB(255,7,91));SetPixelV(dc,-17,-13,RGB(71,253,19));
        SetPixelV(dc,400,230,RGB(31,11,239));RestoreDC(dc,clipped);
        const BLENDFUNCTION blend{AC_SRC_OVER,0,160,0};GdiAlphaBlend(dc,8,75,250,29,source,0,0,1,1,blend);
    } else {
        IntersectClipRect(dc,0,20,350,150);
        HFONT font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
        HGDIOBJ old=SelectObject(dc,font);SetTextColor(dc,RGB(227,81,173));const wchar_t* text=L"Concord 8: \u6d4b\u8bd5 012345";TextOutW(dc,4,29,text,lstrlenW(text));SelectObject(dc,old);DeleteObject(font);
        HPEN pen=CreatePen(PS_SOLID,1,RGB(137,49,231));old=SelectObject(dc,pen);SetROP2(dc,R2_XORPEN);MoveToEx(dc,181,79,nullptr);LineTo(dc,249,119);SelectObject(dc,old);DeleteObject(pen);
        MapNativeOverlayWin32::setPenColor(dc,RGB(29,133,81));old=SelectObject(dc,GetStockObject(DC_PEN));MoveToEx(dc,9,135,nullptr);LineTo(dc,318,112);SelectObject(dc,old);
    }
    RestoreDC(dc,saved);
}
MapDrawCommand fillCommand(int w,int h) {MapDrawCommand c;c.left=50;c.top=15;c.right=w-71;c.bottom=h-21;c.parameter=0x004739abU;return c;}
void cpuFill(MainBackBufferWin32& b,MapDrawCommand c) {
    GdiFlush();auto* pixels=static_cast<std::uint32_t*>(b.pixels);
    for(int y=c.top;y<c.bottom;++y)for(int x=c.left;x<c.right;++x)pixels[static_cast<std::size_t>(y)*b.width+x]=c.parameter;
}
void base(MapDrawList& list,MainBackBufferWin32& reference,std::uint32_t seed) {
    list.reset();reference.beginWrite();GdiFlush();auto* pixels=static_cast<std::uint32_t*>(reference.pixels);
    for(std::size_t i=0;i<static_cast<std::size_t>(reference.width)*reference.height;++i){seed=seed*1664525U+1013904223U;pixels[i]=seed;}
    MapDrawCommand c;c.right=reference.width;c.bottom=reference.height;c.kind=MapDrawKind::Bgra;c.width=reference.width;c.height=reference.height;c.stride=c.width*4;
    c.offset=list.intern({reinterpret_cast<const unsigned char*>(reference.pixels),static_cast<std::size_t>(c.stride)*c.height});list.push(c);
}
bool capture(MapNativeOverlayWin32& native,MapDrawList& list,MainBackBufferWin32& reference,HDC source,COLORREF color) {
    const auto c=fillCommand(reference.width,reference.height);
    MoveToEx(reference.memoryDc,0,0,nullptr);
    HDC dc=native.begin(reference.memoryDc,reference.width,reference.height,list,static_cast<const std::uint32_t*>(reference.pixels));if(!dc||!native.targets(static_cast<const std::uint32_t*>(reference.pixels)))return false;
    const auto opaque=[](auto target){MoveToEx(target,30,40,nullptr);LineTo(target,100,70);SetPixelV(target,21,61,RGB(39,191,211));};
    opaque(reference.memoryDc);opaque(dc);
    MapDrawCommand boundary;boundary.right=reference.width;boundary.bottom=reference.height;boundary.kind=MapDrawKind::NoOp;
    if(!native.emit(boundary))return false;
    paint(reference.memoryDc,source,0,color);paint(dc,source,0,color);
    cpuFill(reference,c);if(!native.emit(c))return false;
    const auto positionText=[](auto target){const auto saved=SaveDC(target);SetTextAlign(target,TA_UPDATECP|TA_LEFT|TA_TOP);SetBkMode(target,TRANSPARENT);SetTextColor(target,RGB(231,251,21));TextOutW(target,0,0,L"CP",2);RestoreDC(target,saved);};
    positionText(reference.memoryDc);positionText(dc);
    paint(reference.memoryDc,source,1,color);paint(dc,source,1,color);
    return native.finish();
}
bool same(const D3D11FrameProbeWin32& probe,const MainBackBufferWin32& reference) {
    GdiFlush();const auto size=static_cast<std::size_t>(reference.width)*reference.height*4;
    if(probe.uploadedPixels.size()!=size)return false;
    const auto* wanted=static_cast<const unsigned char*>(reference.pixels);
    if(std::memcmp(probe.uploadedPixels.data(),wanted,size)==0)return true;
    for(std::size_t i=0;i<size;i+=4)if(std::memcmp(probe.uploadedPixels.data()+i,wanted+i,4)!=0) {
        std::uint32_t a=0,b=0;std::memcpy(&a,probe.uploadedPixels.data()+i,4);std::memcpy(&b,wanted+i,4);
        std::cerr<<"first native GPU diff x="<<(i/4)%reference.width<<" y="<<(i/4)/reference.width<<" actual="<<a<<" expected="<<b<<'\n';break;
    }
    return false;
}
}
int main() {
    if(!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))return 2;
    int checks=0,failures=0;const auto check=[&](bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<"Native map overlay "<<label<<'\n';}};
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"Concord native overlay",WS_POPUP,20,20,384,224,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(window!=nullptr,"own real window");if(!window)return 1;
    SetWindowPos(window,HWND_TOPMOST,0,0,384,224,SWP_NOMOVE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    HDC windowDc=GetDC(window);MainBackBufferWin32 reference;SkirmishBackBufferWin32 source;
    check(reference.ensure(windowDc,384,224),"original main DIB owner");check(source.ensure(windowDc,1,1),"owned opaque native source");ReleaseDC(window,windowDc);
    if(!reference.pixels||!source.ready()){DestroyWindow(window);return 1;}
    DIBSECTION observed{};const int observedSize=GetObjectW(reference.bitmap,sizeof(observed),&observed);
    std::cout<<"Native DIB observation size="<<observedSize<<" expected="<<sizeof(observed)<<" width="<<observed.dsBm.bmWidth
        <<" bitmapHeight="<<observed.dsBm.bmHeight<<" headerHeight="<<observed.dsBmih.biHeight
        <<" bpp="<<observed.dsBmih.biBitCount<<" sameOwnedPixels="<<(observed.dsBm.bmBits==reference.pixels)<<'\n';
    GdiFlush();*reinterpret_cast<std::uint32_t*>(source.writablePixels())=0xff000000U;
    MapDrawList list;
    {MapNativeOverlayWin32 warm;base(list,reference,0x1a7cU);check(capture(warm,list,reference,source.dc(),RGB(19,29,39)),"native warm capture");}
    const DWORD gdiBaseline=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(auto driver:{FrameDevice::Hardware,FrameDevice::Warp}) {
        FramePresentStatistics counters;D3D11FrameProbeWin32 probe;probe.captureUpload=true;
        auto gpu=D3D11FramePresenterWin32::create(window,{FrameBackend::D3D11,driver},counters,&probe);
        check(gpu&&gpu->active(),"actual requested GPU");if(!gpu||!gpu->active())continue;
        MapNativeOverlayWin32 native;
        base(list,reference,0x39a7U);GdiFlush();
        const auto* original=static_cast<const unsigned char*>(reference.pixels);
        const std::vector<unsigned char> untouched(original,original+384*224*4);
        const auto clipState=SaveDC(reference.memoryDc);IntersectClipRect(reference.memoryDc,40,20,140,120);
        check(!PtVisible(reference.memoryDc,12,30)&&!PtVisible(reference.memoryDc,-17,-13)&&!PtVisible(reference.memoryDc,400,230),"original CPU DC confirms clipped points");
        const auto clippedResult=SetPixelV(reference.memoryDc,12,30,RGB(255,7,91));SetPixelV(reference.memoryDc,-17,-13,RGB(71,253,19));SetPixelV(reference.memoryDc,400,230,RGB(31,11,239));
        RestoreDC(reference.memoryDc,clipState);GdiFlush();check(std::memcmp(untouched.data(),reference.pixels,untouched.size())==0,"original clipped pixel attempts preserve all BGRA");
        std::cout<<"Original CPU clipped SetPixelV return="<<clippedResult<<" fullBgraUnchanged=1\n";
        check(capture(native,list,reference,source.dc(),RGB(19,29,39)),native.report().failure.data());
        check(list.valid(),"no unfilled native slots");check(!native.report().cacheHit,"first real capture compiled");
        check(native.report().lookupGroups>0,"native blend/text/XOR use actual tables");
        check(gpu->presentMap({list,1,384,224}),"GPU consumes native value commands");check(same(probe,reference),"all BGRA exact including original fonts/alpha/ROP/clip and interleaved GPU fill");
        base(list,reference,0x729bU);check(capture(native,list,reference,source.dc(),RGB(19,29,39)),"repeat capture");
        check(native.report().cacheHit,"full identical native trace cache");check(gpu->presentMap({list,2,384,224}),"same native trace over different world RGB");check(same(probe,reference),"cache has no baked world background");
        base(list,reference,0x729bU);check(capture(native,list,reference,source.dc(),RGB(183,41,91)),"same-size brush state change");
        check(!native.report().cacheHit,"equal-size native bytes invalidated");check(gpu->presentMap({list,3,384,224}),"new native state submitted");check(same(probe,reference),"reloaded native state exact");
        char* priorReference=nullptr;std::size_t priorSize=0;
        check(_dupenv_s(&priorReference,&priorSize,"CONCORD_S41_NATIVE_FULL_REPLAY")==0,"snapshot diagnostic process environment");
        const std::string savedReference=priorReference?priorReference:"";std::free(priorReference);
        check(_putenv_s("CONCORD_S41_NATIVE_FULL_REPLAY","1")==0,"diagnostic full256 reference selector");
        base(list,reference,0x4c19U);check(capture(native,list,reference,source.dc(),RGB(183,41,91)),"full256 scalar reference compiled");
        check(!native.report().cacheHit,"reference selector invalidates same native trace cache");
        check(gpu->presentMap({list,4,384,224}),"GPU full256 reference packet");check(same(probe,reference),"full256 reference equals complete original CPU BGRA");
        check(_putenv_s("CONCORD_S41_NATIVE_FULL_REPLAY",savedReference.c_str())==0,"restore diagnostic process environment");
        base(list,reference,0x4c19U);check(capture(native,list,reference,source.dc(),RGB(183,41,91)),"exact Boolean and sensitive native compilation");
        check(!native.report().cacheHit,"fast selector invalidates scalar reference cache");
        check(gpu->presentMap({list,5,384,224}),"GPU exact accelerated native packet");check(same(probe,reference),"fast native equals full reference and complete original CPU BGRA including alpha/XOR/text/clip");
        std::uint64_t sequence=6;
        for(const auto offset:std::array<RuntimeScreenShakeOffset,6>{{{9,7},{-11,8},{13,-9},{-15,-12},{384,224},{-384,-224}}}) {
            base(list,reference,0x71abU);HDC dc=native.begin(reference.memoryDc,384,224,list);check(dc!=nullptr,"screen-shake capture");
            paint(reference.memoryDc,source.dc(),0,RGB(19,29,39));paint(dc,source.dc(),0,RGB(19,29,39));
            MapDrawCommand shift;shift.kind=MapDrawKind::Shift;shift.left=17;shift.top=11;shift.right=365;shift.bottom=211;
            shift.originX=offset.x;shift.originY=offset.y;std::vector<std::uint32_t> scratch;
            check(runtimeTranslateScreenShakePixels(static_cast<std::uint32_t*>(reference.pixels),384,224,17,11,365,211,offset,scratch),"original CPU integer screen shake");
            check(native.emit(shift),"ordered GPU screen-shake command");
            paint(reference.memoryDc,source.dc(),1,RGB(19,29,39));paint(dc,source.dc(),1,RGB(19,29,39));
            check(native.finish(),"native layers around screen shake");check(gpu->presentMap({list,sequence++,384,224}),"GPU screen shake presented");
            check(same(probe,reference),"full BGRA and outside clip exact including extended edges, native before and after shift");
        }
        bool foreign=true;std::thread other([&]{foreign=native.begin(reference.memoryDc,384,224,list)!=nullptr;});other.join();check(!foreign,"foreign owner rejected");
        base(list,reference,91);HDC held=native.begin(reference.memoryDc,384,224,list);check(held!=nullptr,"begin unfinished frame");
        check(!list.valid(),"pending native slot cannot submit");check(native.begin(reference.memoryDc,384,224,list)==nullptr,"duplicate begin rejected");native.cancel();check(!list.valid(),"cancel invalidates packet");
        base(list,reference,97);held=native.begin(reference.memoryDc,384,224,list);const DWORD unknown=0x99U;GdiComment(held,sizeof(unknown),reinterpret_cast<const BYTE*>(&unknown));
        check(!native.finish()&&!list.valid(),"unsupported record rejects whole frame, no omission");
        base(list,reference,0x129aU);HDC readable=native.begin(reference.memoryDc,384,224,list);
        check(readable!=nullptr,"destination-read guard capture");check(MapNativeOverlayWin32::readPixel(readable,4,4)==CLR_INVALID,"native destination read is explicitly rejected before submission");
        check(!native.finish()&&!list.valid(),"destination-read discards whole packet for full software rerender");
        check(MapNativeOverlayWin32::readPixel(reference.memoryDc,4,4)==GetPixel(reference.memoryDc,4,4),"ordinary software original destination read unchanged");
        gpu.reset();
    }
    for(int iteration=0;iteration<6;++iteration) {
        {MapNativeOverlayWin32 native;base(list,reference,static_cast<std::uint32_t>(iteration+3));check(capture(native,list,reference,source.dc(),RGB(19,29,39)),"repeat private owner close");}
        check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gdiBaseline,"post-warm GDI owner balance");
    }
    for(int failure=0;failure<4;++failure) {
        MapNativeOverlayProbe injected;injected.failBegin=failure==0;injected.failReplay=failure==1;injected.failFlush=failure==2;injected.failAllocation=failure==3;
        {MapNativeOverlayWin32 native(&injected);base(list,reference,51);HDC dc=native.begin(reference.memoryDc,384,224,list);
            if(failure==0)check(dc==nullptr,"begin failure");else {check(dc!=nullptr,"injected capture owns DC");paint(dc,source.dc(),1,RGB(19,29,39));check(!native.finish()&&!list.valid(),"injected failure invalidates complete packet");}}
        check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gdiBaseline,"failure cleanup balance");
    }
    reference.release();source.release();DestroyWindow(window);
    std::cout<<"Native map overlay checks="<<checks<<" failures="<<failures<<" hardwareAndWarp=separate fullBGRA=exact normalGPUReadback=none postWarmGDI=balanced\n";return failures?1:0;
}