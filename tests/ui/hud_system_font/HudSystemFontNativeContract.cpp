#include "ui/text/win32/RuntimeHudSystemFontWin32.h"
#include "OriginalHudSystemFontReference.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace concord::ui;
namespace {
unsigned checks=0, pairs=0, measurements=0, borrows=0;
std::uint64_t pixelsCompared=0;
void require(bool value,const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr,"HUD font contract failed: %s\n",message); std::exit(1); }
}
struct Surface {
    HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=nullptr;
    HGDIOBJ previous=nullptr;
    std::uint32_t* pixels=nullptr;
    static constexpr int width=320,height=128;
    Surface() {
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=width; info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        void* raw=nullptr;
        if(dc)bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
        pixels=static_cast<std::uint32_t*>(raw);
        if(bitmap)previous=SelectObject(dc,bitmap);
        require(dc&&bitmap&&pixels&&previous,"real top-down DIB and selection");
    }
    ~Surface() {
        if(previous)SelectObject(dc,previous);
        if(bitmap)DeleteObject(bitmap);
        if(dc)DeleteDC(dc);
    }
    void reset(unsigned salt) {
        require(GdiFlush()!=FALSE,"GDI to CPU handoff");
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)
            pixels[y*width+x]=(salt*37U+static_cast<unsigned>(x)*719U+static_cast<unsigned>(y)*65537U)^0x9a376b21U;
    }
};
void compare(Surface& a,Surface& b) {
    require(GdiFlush()!=FALSE,"comparison flush");
    require(std::memcmp(a.pixels,b.pixels,Surface::width*Surface::height*4)==0,"whole raw BGRA including alpha");
    pixelsCompared+=Surface::width*Surface::height; ++pairs;
    require(GetTextColor(a.dc)==GetTextColor(b.dc)&&GetBkColor(a.dc)==GetBkColor(b.dc)
        &&GetBkMode(a.dc)==GetBkMode(b.dc)&&GetTextAlign(a.dc)==GetTextAlign(b.dc),"DC text side effects");
}
void oneRole(unsigned role,bool enabled) {
    Surface a,b;
    HFONT reference=enabled?original_hud_font::create(role):nullptr;
    HudSystemFontWin32 current;
    if(enabled)current.initialize(static_cast<HudSystemFontRole>(role));
    require(current.capability()==HudSystemFontCapability::NativeText,"explicit native capability");
    require(current.available()==(reference!=nullptr),"owned or intentionally absent font");
    if(enabled) {
        require(reference&&current.nativeFont(),"actual authored font created");
        LOGFONTW x{},y{};
        require(GetObjectW(reference,sizeof(x),&x)==sizeof(x)&&GetObjectW(current.nativeFont(),sizeof(y),&y)==sizeof(y),"real LOGFONT");
        require(std::memcmp(&x,&y,sizeof(x))==0,"all original font parameters");
    }
    const int sa=SaveDC(a.dc),sb=SaveDC(b.dc);
    require(sa&&sb,"original outer saved draw scope");
    HGDIOBJ oa=SelectObject(a.dc,reference),ob=SelectObject(b.dc,current.nativeFont());
    SetBkMode(a.dc,TRANSPARENT);SetBkMode(b.dc,TRANSPARENT);
    IntersectClipRect(a.dc,3,2,312,121);IntersectClipRect(b.dc,3,2,312,121);
    TEXTMETRICW ma{},mb{};
    require(GetTextMetricsW(a.dc,&ma)&&GetTextMetricsW(b.dc,&mb)&&std::memcmp(&ma,&mb,sizeof(ma))==0,"selected actual font metrics");
    const std::array<std::wstring,9> text={L"红队已被击败！",L"甲&蓝已被击败！",L"1",L"12",L"[所有人] 路径规划",L"坐标 -7, 18",L"",std::wstring(L"A\0中",3),std::wstring({wchar_t(0xd83d),wchar_t(0xde80),L'中',wchar_t(0xd800)})};
    const std::array<RECT,6> rects={RECT{10,8,300,40},RECT{-20,-4,73,23},RECT{20,20,21,21},RECT{20,20,20,20},RECT{70,35,20,10},RECT{250,105,370,145}};
    const std::array<UINT,3> formats={DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX};
    unsigned ordinal=0;
    for(const auto& s:text) {
        SIZE extent{};
        const bool measured=GetTextExtentPoint32W(a.dc,s.c_str(),static_cast<int>(s.size()),&extent)!=FALSE;
        const auto actual=HudSystemFontWin32::measure(b.dc,s);
        require(measured==actual.measured&&extent.cx==actual.width&&extent.cy==actual.height,"explicit UTF16 measurement including NUL"); ++measurements;
        for(const auto r:rects)for(const UINT format:formats)for(const bool nul:{false,true}) {
            a.reset(ordinal);b.reset(ordinal);++ordinal;
            RECT bounds=r;
            const int count=nul?-1:static_cast<int>(s.size());
            SetTextColor(a.dc,RGB(0,0,0));SetTextColor(b.dc,RGB(0,0,0));
            OffsetRect(&bounds,1,1);
            const int expectedShadow=DrawTextW(a.dc,s.c_str(),count,&bounds,format);
            const auto actualShadow=current.drawText(b.dc,HudSystemWideTextRequest{s.c_str(),count,{r.left+1,r.top+1,r.right+1,r.bottom+1},format});
            require(expectedShadow==actualShadow,"shadow return and count policy");
            bounds=r;
            SetTextColor(a.dc,RGB(49,137,243));SetTextColor(b.dc,RGB(49,137,243));
            const int expected=DrawTextW(a.dc,s.c_str(),count,&bounds,format);
            const auto actualDraw=current.drawText(b.dc,HudSystemWideTextRequest{s.c_str(),count,{r.left,r.top,r.right,r.bottom},format});
            require(expected==actualDraw,"face return and count policy");
            compare(a,b);
        }
        a.reset(ordinal);b.reset(ordinal++);
        require((TextOutW(a.dc,-3,51,s.c_str(),static_cast<int>(s.size()))!=FALSE)==current.textOut(b.dc,-3,51,s),"original TextOut explicit count");compare(a,b);
        const std::u16string units(s.begin(),s.end());
        a.reset(ordinal);b.reset(ordinal++);RECT r{3,2,310,121};
        const int expected=DrawTextW(a.dc,s.c_str(),static_cast<int>(s.size()),&r,formats[0]);
        const auto actualDraw=current.drawText(b.dc,HudSystemTextRequest{units.c_str(),static_cast<int>(units.size()),{3,2,310,121},formats[0]});
        require(expected==actualDraw,"portable UTF16 request");compare(a,b);
    }
    SelectObject(a.dc,oa);SelectObject(b.dc,ob);
    if(role==2) { if(reference)DeleteObject(reference); current.reset(); }
    require(RestoreDC(a.dc,sa)&&RestoreDC(b.dc,sb),"original outer restoration");
    if(role!=2) { if(reference)DeleteObject(reference); current.reset(); }
    require(current.nativeFont()==nullptr&&!current.available(),"reset revokes font owner");
}
}
int main(int argc,char** argv) {
    if(argc==2&&std::strcmp(argv[1],"--stock-cache-reference")==0) {
        for(unsigned n=0;n<12;++n) {
            const DWORD begin=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
            {
                Surface target;
                const int saved=SaveDC(target.dc);
                // The original null-font SelectObject fails, leaving the same
                // DC stock font selected. No provider method is called here.
                SelectObject(target.dc,static_cast<HFONT>(nullptr));
                const std::array<std::wstring,3> strings={L"红队已被击败！",L"[所有人] 路径规划",std::wstring({wchar_t(0xd83d),wchar_t(0xde80),L'中',wchar_t(0xd800)})};
                for(const auto& text:strings) {
                    SIZE extent{};GetTextExtentPoint32W(target.dc,text.c_str(),static_cast<int>(text.size()),&extent);
                    RECT rect{0,0,319,127};DrawTextW(target.dc,text.c_str(),-1,&rect,DT_SINGLELINE|DT_NOPREFIX);
                }
                require(RestoreDC(target.dc,saved)!=FALSE,"reference scope restore");
            }
            std::printf("raw-stock-reference iteration=%u GDI=%lu:%lu providerCalls=0\n",n,begin,GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS));
        }
        return 0;
    }
    // Warm the same native font/driver caches before the lifetime observation.
    for(unsigned role=0;role<3;++role) {oneRole(role,true);oneRole(role,false);}
    const DWORD before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    DWORD handlesBefore=0;require(GetProcessHandleCount(GetCurrentProcess(),&handlesBefore)!=FALSE,"handle baseline");
    for(unsigned role=0;role<3;++role) {oneRole(role,true);oneRole(role,false);std::printf("role=%u GDI=%lu\n",role,GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS));}
    for(unsigned n=0;n<128;++n) {
        Surface target;HudSystemFontWin32 font;
        font.initialize(static_cast<HudSystemFontRole>(n%3));require(font.available(),"borrow font");
        const int saved=SaveDC(target.dc);SelectObject(target.dc,font.nativeFont());
        (void)font.drawText(target.dc,HudSystemWideTextRequest{L"字体借用 中&文",-1,{0,0,319,127},DT_SINGLELINE|DT_NOPREFIX});
        require(RestoreDC(target.dc,saved)!=FALSE,"borrow selection restored before reset");font.reset();++borrows;
    }
    const auto invalid=HudSystemFontWin32::measure(nullptr,L"中");require(!invalid.measured,"invalid DC failure remains explicit");
    const DWORD after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    DWORD handlesAfter=0;require(GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)!=FALSE,"handle final");
    std::printf("lifetime observation GDI=%lu:%lu handles=%lu:%lu pairs=%u measurements=%u borrows=%u\n",before,after,handlesBefore,handlesAfter,pairs,measurements,borrows);
    require(before==after&&handlesBefore==handlesAfter,"zero retained GDI and handle growth");
    std::printf("HUD font native passed checks=%u pairs=%u rawPixels=%llu measurements=%u borrows=%u GDI=%lu:%lu handles=%lu:%lu roles=3 originalFonts=4 UTF16=explicit-and-NUL\n",checks,pairs,static_cast<unsigned long long>(pixelsCompared),measurements,borrows,before,after,handlesBefore,handlesAfter);
}
