#include "ui/skirmish/win32/RuntimeSkirmishBackBufferWin32.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <type_traits>
#include <vector>
using namespace concord::ui;
using namespace concord::ui::win32;
namespace {
int checks=0,failures=0,mode=0,flushCalls=0,seenHeight=0,seenWidth=0;
unsigned short seenBits=0;DWORD seenCompression=999;
void expect(bool value,const char* name){++checks;if(!value){++failures;std::cerr<<name<<'\n';}}
HDC WINAPI createDc(HDC reference){return mode==1?nullptr:CreateCompatibleDC(reference);}
HBITMAP WINAPI createDib(HDC reference,const BITMAPINFO* info,UINT usage,void** bits,HANDLE section,DWORD offset){
    seenWidth=info->bmiHeader.biWidth;seenHeight=info->bmiHeader.biHeight;seenBits=info->bmiHeader.biBitCount;seenCompression=info->bmiHeader.biCompression;
    if(mode==2)return nullptr;
    const auto bitmap=CreateDIBSection(reference,info,usage,bits,section,offset);
    if(mode==6)*bits=nullptr;
    return bitmap;
}
HGDIOBJ WINAPI selectObject(HDC dc,HGDIOBJ object){
    if(mode==3)return nullptr;if(mode==4)return HGDI_ERROR;return SelectObject(dc,object);
}
BOOL WINAPI flush(){++flushCalls;return mode==5?FALSE:GdiFlush();}
BOOL WINAPI deleteObject(HGDIOBJ object){if(mode==7){mode=0;return FALSE;}return DeleteObject(object);}
BOOL WINAPI deleteDc(HDC dc){if(mode==8){mode=0;return FALSE;}return DeleteDC(dc);}
SkirmishBackBufferGdiApi api(){SkirmishBackBufferGdiApi a;a.createDc=createDc;a.createDib=createDib;a.select=selectObject;a.flush=flush;a.deleteObject=deleteObject;a.deleteDc=deleteDc;return a;}
ConstMenuPixelSurfaceView read(const MenuPixelBuffer& b){return b.view();}
bool exact(const SkirmishBackBufferWin32& b,ConstMenuPixelSurfaceView s){
    for(int y=0;y<s.height;++y)
        if(!std::equal(s.bytes.data()+static_cast<std::size_t>(y)*s.rowStride,
            s.bytes.data()+static_cast<std::size_t>(y)*s.rowStride+static_cast<std::size_t>(s.width)*4,
            b.pixels()+static_cast<std::size_t>(y)*static_cast<std::size_t>(s.width)*4))return false;
    return true;
}
}
int main(){
    static_assert(!std::is_copy_constructible_v<SkirmishBackBufferWin32>);
    {SkirmishBackBufferWin32 warm;expect(warm.ensure(nullptr,2,2),"actual warm allocation");expect(warm.release(),"actual warm release");}
    const auto baseline=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    {
        SkirmishBackBufferWin32 b(api());MenuPixelBuffer frame;
        expect(frame.reset(2,3,12),"padded CPU source");auto source=frame.view();
        for(std::size_t i=0;i<source.bytes.size();++i)source.bytes[i]=static_cast<unsigned char>(i*17);
        expect(!b.ready()&&b.dc()==nullptr&&b.bitmap()==nullptr&&b.pixels()==nullptr,"native owner initially empty");
        expect(b.ensure(nullptr,2,3),"actual DC/DIB/select");expect(b.ready(),"complete native ownership");
        expect(seenWidth==2&&seenHeight==-3&&seenBits==32&&seenCompression==BI_RGB,"original topdown32bit BI_RGB creation contract");
        DIBSECTION section{};expect(GetObjectW(b.bitmap(),sizeof(section),&section)==sizeof(section),"actual native DIB metadata");
        expect(section.dsBm.bmBits==b.pixels()&&section.dsBm.bmWidth==2&&section.dsBm.bmHeight==3&&section.dsBm.bmBitsPixel==32,"actual bitmap owns expected bytes and dimensions");
        const auto originalDc=b.dc();const auto originalBitmap=b.bitmap();expect(b.ensure(nullptr,2,3)&&b.dc()==originalDc&&b.bitmap()==originalBitmap,"same dimensions reuse native resources");
        const auto beforeUpload=flushCalls;expect(b.upload(read(frame))&&flushCalls==beforeUpload+1,"exactly one successful GDI flush precedes copy");expect(exact(b,read(frame)),"topdown rows exact, input padding skipped");
        const auto oldPixels=std::vector<unsigned char>(b.pixels(),b.pixels()+24);
        for(auto& v:source.bytes)v=static_cast<unsigned char>(v^0xff);
        mode=5;const auto beforeFailure=flushCalls;
        expect(!b.upload(read(frame))&&flushCalls==beforeFailure+1,"failed GdiFlush rejected");expect(b.ready()&&std::equal(oldPixels.begin(),oldPixels.end(),b.pixels()),"flush failure never writes partially synchronized DIB and keeps valid owner");
        mode=0;expect(b.upload(read(frame))&&exact(b,read(frame)),"subsequent real flush can retry unchanged owner");
        auto invalid=read(frame);invalid.bytes=invalid.bytes.first(3);const auto flushBeforeInvalid=flushCalls;
        expect(!b.upload(invalid)&&flushCalls==flushBeforeInvalid,"short source fails before flush or write");
        invalid=read(frame);invalid.width=1;expect(!b.upload(invalid)&&flushCalls==flushBeforeInvalid,"dimension mismatch fails before flush");
        expect(exact(b,read(frame)),"rejected uploads preserve native bytes");
        {
            SkirmishBackBufferWin32 target;expect(target.ensure(nullptr,2,3),"owned GDI destination");
            expect(BitBlt(target.dc(),0,0,2,3,b.dc(),0,0,SRCCOPY)!=FALSE&&GdiFlush()!=FALSE,"actual original GDI blit succeeds");
            for(int y=0;y<3;++y)for(int x=0;x<2;++x){
                const auto at=static_cast<std::size_t>(y)*12+static_cast<std::size_t>(x)*4;
                expect(GetPixel(target.dc(),x,y)==RGB(source.bytes[at+2],source.bytes[at+1],source.bytes[at]),"native presentation preserves BGR colors");
            }
        }
        SkirmishGdiReleaseTrace trace;expect(b.release(&trace),"actual native release succeeds");
        expect(trace.count==3&&trace.order[0]==0&&trace.order[1]==1&&trace.order[2]==2&&trace.succeeded[0]&&trace.succeeded[1]&&trace.succeeded[2],"restore borrowed old bitmap, delete owned bitmap, then DC");
        expect(b.release(&trace)&&trace.count==3&&!b.ready()&&b.dc()==nullptr&&b.bitmap()==nullptr&&b.pixels()==nullptr,"repeat release idempotent and does not trace twice");
    }
    expect(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==baseline,"normal upload/release actual GDI balance");
    for(const int fault:{1,2,3,4,6})for(int attempt=0;attempt<4;++attempt){
        const auto before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        {
            SkirmishBackBufferWin32 b(api());mode=fault;
            expect(!b.ensure(nullptr,5,4),"injected actual API allocation/select failure");
            expect(!b.ready()&&b.dc()==nullptr&&b.bitmap()==nullptr&&b.pixels()==nullptr,"partial creation rolls back every owned object");
            mode=0;expect(b.ensure(nullptr,5,4),"retry after partial creation failure");
            expect(b.release(),"retry owner disposes");
        }
        expect(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==before,"failure path actual GDI balance");
    }
    for(const int cleanupFault:{7,8}){
        const auto before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        {
            SkirmishBackBufferWin32 b(api());expect(b.ensure(nullptr,3,2),"cleanup failure initial owner");
            mode=cleanupFault;SkirmishGdiReleaseTrace trace;
            expect(!b.release(&trace),"actual cleanup failure reported");
            expect(!b.ready()&&(b.bitmap()!=nullptr||b.dc()!=nullptr),"failed deletion retains precise remaining handle, no false ready");
            expect(trace.count==3&&!trace.succeeded[static_cast<std::size_t>(cleanupFault==7?1:2)],"first failed delete remains recorded");
            expect(b.release()&&b.dc()==nullptr&&b.bitmap()==nullptr&&b.pixels()==nullptr,"retry actually deletes retained object");
        }
        expect(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==before,"cleanup failure/retry real GDI balance");
    }
    for(int cycle=0;cycle<100;++cycle){
        const auto before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        {
            SkirmishBackBufferWin32 b(api());
            expect(b.ensure(nullptr,2,3),"resize initial owner");expect(b.ensure(nullptr,7,5),"resize recreates complete owner");
            DIBSECTION s{};expect(GetObjectW(b.bitmap(),sizeof(s),&s)==sizeof(s)&&s.dsBm.bmWidth==7&&s.dsBm.bmHeight==5,"resized native dimensions exact");
            mode=2;expect(!b.ensure(nullptr,8,6)&&!b.ready()&&b.dc()==nullptr&&b.bitmap()==nullptr&&b.pixels()==nullptr,"resize creation failure releases previous owner and rolls back new DC");
            mode=0;expect(b.ensure(nullptr,3,2),"resize failure recovery");
            expect(!b.ensure(nullptr,0,2)&&!b.ready(),"zero size releases old owner and fails");
            expect(!b.ensure(nullptr,-1,2)&&!b.ready(),"negative size rejected");
            expect(b.ensure(nullptr,800,600),"original menu logical dimensions allocate");
            MenuPixelBuffer pixels;expect(pixels.reset(800,600),"actual-size CPU frame");clearMenuPixels(pixels.view());
            fillMenuPixels(pixels.view(),{17,23,799,599},RuntimeRgbColorValue::fromChannels(11,37,83));
            expect(b.upload(read(pixels))&&exact(b,read(pixels)),"actual-size completed CPU frame through native owner exact");
        }
        expect(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==before,"100 resize/failure/recovery/destructor cycles balanced");
    }
    mode=0;expect(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==baseline,"all real native owners final balance");
    std::cout<<"Skirmish native back buffer: checks="<<checks<<" failures="<<failures<<" faultPaths=5 retries=20 resizeCycles=100 cleanupFailure=retainedThenRetried flushFailure=noWrite actualGdiBalance=yes restoreDeleteOrder=exact baseline="<<baseline<<"\n";
    return failures?1:0;
}
