#include "ui/surface/win32/RuntimeMainBackBufferWin32.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace concord::ui;
using namespace concord::ui::win32;
namespace {
int mode=0,checks=0;
void need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
HDC WINAPI createDc(HDC dc){return mode==1?nullptr:CreateCompatibleDC(dc);}
HBITMAP WINAPI createDib(HDC dc,const BITMAPINFO* i,UINT u,void** p,HANDLE s,DWORD o){
    if(mode==2)return nullptr;
    const auto b=CreateDIBSection(dc,i,u,p,s,o);if(mode==6)*p=nullptr;return b;
}
HGDIOBJ WINAPI selectObject(HDC dc,HGDIOBJ o){return mode==3?nullptr:mode==4?HGDI_ERROR:SelectObject(dc,o);}
BOOL WINAPI flush(){return mode==5?FALSE:GdiFlush();}
SkirmishBackBufferGdiApi api(){SkirmishBackBufferGdiApi a;a.createDc=createDc;a.createDib=createDib;a.select=selectObject;a.flush=flush;return a;}
}
void compatibleHudDeviceContract(HDC screen){
    std::cout<<"HUD before window="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<'\n';
    RECT area{};SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Concord native HUD verification",WS_POPUP,area.left+8,area.top+8,200,60,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    need(window!=nullptr,"actual HUD device window");
    SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    std::cout<<"HUD after window="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<'\n';
    HDC actual=GetDC(window);need(actual!=nullptr,"HUD window DC");
    MainBackBufferWin32 fromWindow,fromDdb,directDib;
    need(fromWindow.ensure(screen,200,60)&&fromDdb.ensure(screen,200,60)&&directDib.ensure(screen,200,60),"HUD readback DIBs");
    HDC device=CreateCompatibleDC(actual);HBITMAP bitmap=CreateCompatibleBitmap(actual,200,60);
    need(device&&bitmap,"compatible HUD device bitmap");const auto previous=SelectObject(device,bitmap);need(previous&&previous!=HGDI_ERROR,"compatible HUD selection");
    const auto draw=[](HDC dc){const int saved=SaveDC(dc);RECT rect{0,0,200,60};SetDCBrushColor(dc,RGB(19,29,39));FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(235,240,245));rect.left+=6;rect.right-=4;DrawTextW(dc,L"FPS: 144",-1,&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);RestoreDC(dc,saved);};
    std::cout<<"HUD before draw="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<'\n';
    draw(actual);std::cout<<"HUD after first window text="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<'\n';draw(device);draw(directDib.memoryDc);need(GdiFlush()!=FALSE,"HUD drawing flush");
    need(BitBlt(fromWindow.memoryDc,0,0,200,60,actual,0,0,SRCCOPY)!=FALSE&&BitBlt(fromDdb.memoryDc,0,0,200,60,device,0,0,SRCCOPY)!=FALSE,"actual HUD readbacks");
    const auto a=fromWindow.completedPixels(),b=fromDdb.completedPixels(),c=directDib.completedPixels();need(a.valid()&&b.valid()&&c.valid(),"completed HUD pixel lease");
    std::size_t deviceMismatch=0,dibMismatch=0;for(std::size_t i=0;i<a.bytes.size();++i)if(i%4!=3){deviceMismatch+=a.bytes[i]!=b.bytes[i];dibMismatch+=a.bytes[i]!=c.bytes[i];}
    std::cout<<"Compatible HUD device all-BGR mismatches="<<deviceMismatch<<" directDibDiagnosticMismatches="<<dibMismatch<<" pixels=12000 alpha=ignored\n";
    need(SelectObject(device,previous)==bitmap,"HUD restore selected bitmap");need(DeleteObject(bitmap)!=FALSE,"HUD bitmap deleted");need(DeleteDC(device)!=FALSE,"HUD DC deleted");need(ReleaseDC(window,actual)!=0,"HUD window DC released");need(DestroyWindow(window)!=FALSE,"HUD native window destroyed");
    need(deviceMismatch==0,"compatible bitmap preserves actual window text RGB over complete image");
}
int main()try{
    const auto cold=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    // GDI realizes its stock GUI font on the first actual DrawText, retaining
    // one process resource until exit. Prime that required native subsystem;
    // measured owner cycles below still require exact pre/post equality.
    HDC warmReference=GetDC(nullptr);need(warmReference!=nullptr,"font warmup display reference");
    compatibleHudDeviceContract(warmReference);need(ReleaseDC(nullptr,warmReference)!=0,"warmup display reference released");
    const auto initial=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    std::cout<<"First actual GUI text initialization cold="<<cold<<" initialized="<<initial<<'\n';
    HDC screen=GetDC(nullptr);need(screen!=nullptr,"actual screen DC");
    {
        MainBackBufferWin32 b(api());
        need(!b.ensure(screen,0,8)&&!b.pixelView().valid(),"zero dimensions rejected/released");
        need(!b.ensure(nullptr,8,8)&&!b.pixelView().valid(),"null reference rejected/released");
        for(const int failure:{1,2,3,4,6})for(int repeat=0;repeat<4;++repeat){
            const auto count=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);mode=failure;
            need(!b.ensure(screen,17,13),"actual native allocation/selection failure");
            need(!b.pixelView().valid()&&!b.memoryDc&&!b.bitmap&&!b.pixels,"no partial surface published");
            mode=0;need(b.ensure(screen,17,13),"real allocation recovers");
            need(b.completedPixels().valid(),"recovered completed view");need(b.release(),"owner release");
            need(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==count,"GDI allocation failure balance");
        }
        for(int i=0;i<100;++i){
            const int w=31+i%9,h=19+i%7;need(b.ensure(screen,w,h),"repeated resize");
            const auto v=b.completedPixels();need(v.valid()&&b.owns(v),"owned completed lease");
            need(v.rowStride==static_cast<std::size_t>(w)*4&&v.bytes.size()==v.rowStride*static_cast<std::size_t>(h),"complete top-down stride/capacity");
            const auto oldGeneration=v.generation;const auto dc=b.memoryDc;
            need(b.ensure(screen,w,h)&&b.pixelView().generation==oldGeneration&&b.memoryDc==dc,"same dimensions reuse allocation/generation");
            b.beginWrite();need(!b.owns(v),"next writer invalidates old lease without reallocating");
            auto* bytes=static_cast<unsigned char*>(b.pixels);
            for(std::size_t j=0;j<v.bytes.size();++j)bytes[j]=static_cast<unsigned char>((j*13+static_cast<std::size_t>(i))%256);
            const auto working=b.writablePixelView();need(working.valid()&&working.bytes.data()==b.pixels&&working.width==w&&working.height==h,"actual generic working surface producer borrow");
            const auto written=b.completedPixels();need(written.valid(),"CPU completion view");
            for(std::size_t j=0;j<written.bytes.size();++j)need(written.bytes[j]==static_cast<unsigned char>((j*13+static_cast<std::size_t>(i))%256),"all BGRA bytes preserved");
            need(SetPixelV(b.memoryDc,0,0,RGB(13,27,41))!=FALSE,"real GDI write");
            mode=5;need(!b.completedPixels().valid(),"failed flush refuses publication");
            mode=0;const auto done=b.completedPixels();need(done.valid()&&done.bytes[0]==41&&done.bytes[1]==27&&done.bytes[2]==13,"GDI finish before pixel lease");
            need(b.ensure(screen,w+1,h+1)&&!b.owns(v)&&b.pixelView().generation!=oldGeneration,"resize invalidates metadata without dereferencing old storage");
        }
        MainBackBufferWin32 other;
        need(other.ensure(screen,11,9),"other owner for exchange");
        b.beginWrite();other.beginWrite();
        std::fill_n(static_cast<unsigned char*>(b.pixels),b.pixelView().bytes.size(),static_cast<unsigned char>(173));
        std::fill_n(static_cast<unsigned char*>(other.pixels),other.pixelView().bytes.size(),static_cast<unsigned char>(91));
        const auto a=b.completedPixels(),z=other.completedPixels();
        const auto dcA=b.memoryDc,dcZ=other.memoryDc;
        const std::vector<unsigned char> bytesA(a.bytes.begin(),a.bytes.end()),bytesZ(z.bytes.begin(),z.bytes.end());
        b.swap(other);need(b.memoryDc==dcZ&&other.memoryDc==dcA,"explicit complete owner transfer");
        need(!b.owns(z)&&!other.owns(a),"exchange invalidates both previous generations");
        const auto newA=b.completedPixels(),newZ=other.completedPixels();
        need(std::equal(newA.bytes.begin(),newA.bytes.end(),bytesZ.begin(),bytesZ.end())&&std::equal(newZ.bytes.begin(),newZ.bytes.end(),bytesA.begin(),bytesA.end()),"complete original BGRA bytes preserved across explicit owner transfer");
        b.swap(other);need(b.memoryDc==dcA&&other.memoryDc==dcZ,"explicit exchange restored");
        const auto valid=b.completedPixels();const auto oldWidth=b.width;
        b.width=oldWidth+1;need(!b.pixelView().valid(),"alias dimensions cannot forge owned capacity");b.width=oldWidth;
        HBITMAP alien=CreateBitmap(2,2,1,1,nullptr);need(alien!=nullptr,"foreign bitmap for selection guard");
        const auto selected=SelectObject(b.memoryDc,alien);need(selected==b.bitmap,"temporary foreign bitmap selection");
        need(!b.completedPixels().valid(),"wrong selected bitmap cannot publish owned pixels");
        need(SelectObject(b.memoryDc,selected)==alien&&DeleteObject(alien),"foreign selection restored and released");
        need(b.owns(valid)&&b.completedPixels().valid(),"original owned storage remains after guarded native borrow");
        need(b.release()&&b.release()&&b.nativeResourcesReleased(),"idempotent release with internal native ownership empty");
        MainBackBufferWin32 external;need(external.ensure(screen,9,7),"external DIB owner");
        MainBackBufferWin32 borrowed;need(borrowed.borrow(external.memoryDc,external.bitmap,external.pixels,9,7,true),"explicit borrowed fixture");
        need(!borrowed.owns(borrowed.pixelView())&&!borrowed.completedPixels().valid(),"borrow not represented as owned synchronized surface");
        need(!borrowed.ensure(screen,12,10),"borrow never silently adopted");
        need(borrowed.release()&&external.completedPixels().valid(),"borrow release does not delete external DC/DIB");
        need(borrowed.borrow(external.memoryDc,external.bitmap,external.pixels,9,7),"borrow legacy unknown row order");
        need(!borrowed.pixelView().valid(),"no top-down proof no pixel view");borrowed.detachBorrowed();
        need(!borrowed.memoryDc&&external.completedPixels().valid(),"explicit detach preserves owner");
    }
    std::cout<<"GDI before HUD="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<" initial="<<initial<<'\n';
    compatibleHudDeviceContract(screen);
    std::cout<<"GDI after HUD="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<'\n';
    compatibleHudDeviceContract(screen);
    std::cout<<"GDI after second HUD="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<'\n';
    ReleaseDC(nullptr,screen);GdiFlush();std::cout<<"GDI final="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<'\n';need(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==initial,"final native GDI count unchanged");
    std::cout<<"Main backbuffer ownership contract passed checks="<<checks<<" actualResizes=100 failures=20 borrowed=explicit stride=full BGRA=exact flush=required GDI=balanced\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}