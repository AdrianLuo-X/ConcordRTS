// Private diagnostic only: compare the actual original Native overloads against
// the standard port backed by that same producer. No renderer/decoder copy.
#include <cstring>
#include <stdexcept>
namespace RuntimeInternetSkinDrawingContractDetail {
struct Surface {
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;void* pixels=nullptr;
    int width=0,height=0;
    Surface(HDC reference,int w,int h):width(w),height(h){
        dc=CreateCompatibleDC(reference);if(!dc)throw std::runtime_error("contract DC");
        BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;
        bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
        bitmap=CreateDIBSection(reference,&bi,DIB_RGB_COLORS,&pixels,nullptr,0);
        if(!bitmap){DeleteDC(dc);dc=nullptr;throw std::runtime_error("contract DIB");}
        previous=SelectObject(dc,bitmap);
        if(!previous||previous==HGDI_ERROR){DeleteObject(bitmap);DeleteDC(dc);dc=nullptr;bitmap=nullptr;throw std::runtime_error("contract bitmap selection");}
        SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
    }
    ~Surface(){if(dc&&previous)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    Surface(const Surface&)=delete;Surface& operator=(const Surface&)=delete;
    void seed(bool clipped){
        if(!GdiFlush())throw std::runtime_error("seed flush");
        auto* p=static_cast<std::uint32_t*>(pixels);
        for(int i=0;i<width*height;++i)p[i]=0x9d000000U|(static_cast<std::uint32_t>(i*11939)&0xffffffU);
        SelectClipRgn(dc,nullptr);if(clipped)IntersectClipRect(dc,7,11,width-13,height-17);
        SetBkMode(dc,OPAQUE);SetBkColor(dc,RGB(11,33,77));SetTextColor(dc,RGB(21,43,65));
        SetTextAlign(dc,TA_LEFT|TA_TOP|TA_NOUPDATECP);SetROP2(dc,R2_COPYPEN);
    }
};
}
int runRuntimeInternetSkinDrawingContract(const RuntimeConfig& config,const std::string& outputPath) try {
    (void)outputPath;using namespace RuntimeInternetSkinDrawingContractDetail;
    unsigned checks=0,pairs=0,borrows=0;std::uint64_t pixels=0;
    const auto require=[&](bool v,const char* what){++checks;if(!v)throw std::runtime_error(what);};
    wchar_t outputName[32768]{};const DWORD length=GetEnvironmentVariableW(L"CONCORD_INTERNET_SKIN_DRAWING_OUTPUT",outputName,32768);
    require(length>0&&length<32768,"explicit unique cache output required");const fs::path output(outputName);
    require(fs::create_directory(output),"new contract output directory");
    RuntimeInternetMenuSkin skin;require(loadRuntimeInternetMenuSkin(config,skin)&&skin.productionReady,"original45PCX6PAL resources");
    RuntimeInternetMenuSkin absent;
    HDC reference=GetDC(nullptr);require(reference!=nullptr,"real Windows reference DC");
    struct Ref {HDC dc;~Ref(){if(dc)ReleaseDC(nullptr,dc);}} release{reference};
    std::ofstream csv(output/"pairs.csv",std::ios::binary);require(csv.good(),"explicit cache CSV");
    csv<<"pair,kind,width,height,clip,variant,pixels,exactRawBGRA,exactDcState\n";
    const std::array<std::wstring,6> texts{L"CONNECT",L"中文网络",L"",std::wstring(L"A\0B",3),L"length and ellipsis 0123456789",std::wstring(1,static_cast<wchar_t>(0xd800))};
    const auto dcEqual=[&](HDC a,HDC b){
        bool same=GetBkMode(a)==GetBkMode(b)&&GetBkColor(a)==GetBkColor(b)&&GetTextColor(a)==GetTextColor(b)
            &&GetTextAlign(a)==GetTextAlign(b)&&GetROP2(a)==GetROP2(b)&&GetMapMode(a)==GetMapMode(b);
        for(const int kind:{OBJ_FONT,OBJ_PEN,OBJ_BRUSH})same=same&&GetCurrentObject(a,kind)==GetCurrentObject(b,kind);
        RECT aa{},bb{};const int ca=GetClipBox(a,&aa),cb=GetClipBox(b,&bb);return same&&ca==cb&&EqualRect(&aa,&bb);
    };
    for(const auto size:std::array<std::pair<int,int>,2>{{{320,240},{800,600}}}){
        Surface a(reference,size.first,size.second),b(reference,size.first,size.second);
        const RECT full{0,0,size.first,size.second};
        // Warm original native text fallbacks before strict owned GDI equality.
        a.seed(false);drawRuntimeInternetMenuSkinSurface(a.dc,full,skin,RuntimeInternetMenuSkinPage::Browser,RuntimeInternetMenuSkinFaction::Allied,1);
        for(const auto& text:texts)drawRuntimeInternetMenuSkinButton(a.dc,full,skin,"wouact","wodact",text,false,false);
        require(GdiFlush()!=FALSE,"warmup flush");const DWORD gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        const auto compare=[&](const char* kind,bool clip,unsigned variant,bool left,bool right){
            require(left==right,"original and standard bool outcomes");require(GdiFlush()!=FALSE,"actual producer pixels flush");
            const auto bytes=static_cast<std::size_t>(size.first)*size.second*4;
            require(std::memcmp(a.pixels,b.pixels,bytes)==0,"entire BGRA including alpha exact");
            require(dcEqual(a.dc,b.dc),"all original DC side effects equal");
            ++pairs;pixels+=static_cast<std::uint64_t>(size.first)*size.second;
            csv<<pairs<<','<<kind<<','<<size.first<<','<<size.second<<','<<clip<<','<<variant<<','<<size.first*size.second<<",yes,yes\n";
        };
        for(bool clip:{false,true}){
            for(unsigned page=0;page<8;++page)for(unsigned faction=0;faction<4;++faction)for(std::uint32_t rank:{0U,9U,10U}){
                a.seed(clip);b.seed(clip);RuntimeInternetSkinDrawTargetWin32 target(b.dc);++borrows;
                require(target.capability()==RuntimeInternetSkinDrawingCapability::Available,"Native capability");
                const auto p=static_cast<RuntimeInternetMenuSkinPage>(page);const auto f=static_cast<RuntimeInternetMenuSkinFaction>(faction);
                const bool old=drawRuntimeInternetMenuSkinSurface(a.dc,full,skin,p,f,rank);
                const bool now=drawRuntimeInternetMenuSkinSurface(target,{full.left,full.top,full.right,full.bottom},skin,p,f,rank);
                compare("surface",clip,page*100+faction*10+rank,old,now);require(old,"original surface succeeds");
            }
            for(const auto& text:texts)for(bool down:{false,true})for(bool disabled:{false,true}){
                a.seed(clip);b.seed(clip);const RECT button{-3,17,size.first-7,71};RuntimeInternetSkinDrawTargetWin32 target(b.dc);++borrows;
                const bool old=drawRuntimeInternetMenuSkinButton(a.dc,button,skin,"wouact","wodact",text,down,disabled);
                const bool now=drawRuntimeInternetMenuSkinButton(target,{button.left,button.top,button.right,button.bottom},skin,"wouact","wodact",text,down,disabled);
                compare("button",clip,down*2+disabled,old,now);require(old,"original button succeeds");
            }
            for(unsigned failure=0;failure<4;++failure){
                a.seed(clip);b.seed(clip);RuntimeInternetSkinDrawTargetWin32 target(failure==0?nullptr:b.dc);++borrows;
                const auto& use=failure==1?absent:skin;const auto id=failure==2?"original-missing-asset":"wouact";bool old=false,now=false;
                if(failure==3){const RECT empty{9,7,9,7};old=drawRuntimeInternetMenuSkinSurface(a.dc,empty,use,RuntimeInternetMenuSkinPage::Browser,RuntimeInternetMenuSkinFaction::Observer,0);
                    now=drawRuntimeInternetMenuSkinSurface(target,{9,7,9,7},use,RuntimeInternetMenuSkinPage::Browser,RuntimeInternetMenuSkinFaction::Observer,0);}
                else {old=drawRuntimeInternetMenuSkinButton(failure==0?nullptr:a.dc,full,use,id,id,L"original false",false,false);
                    now=drawRuntimeInternetMenuSkinButton(target,{full.left,full.top,full.right,full.bottom},use,id,id,L"original false",false,false);}
                compare("original-boundary",clip,failure,old,now);require(failure==3?old:!old,"original null/notready/missing/empty behavior");
            }
        }
        require(GdiFlush()!=FALSE,"final flush");require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gdiBefore,"original/adapter repeated GDI ownership no growth");
    }
    csv.close();require(csv.good(),"CSV close");require(pairs==496&&borrows==496,"all authored Native cases and short borrows");
    std::cout<<"Internet skin drawing native passed checks="<<checks<<" pairs="<<pairs<<" rawPixels="<<pixels
        <<" borrows="<<borrows<<" rectBytes="<<sizeof(RuntimeInputPixelRect)<<" originalAlgorithms=unique unchanged=1 failures=0 skipped=0\n";
    return 0;
} catch(const std::exception& error){std::cerr<<"Internet skin drawing native FAILED: "<<error.what()<<'\n';return 1;}
