// Differential of the actual unchanged Native entry against the standard
// production port. No SHP decoder/classification/raster algorithm is copied.
#include "render/core/win32/RuntimeAuxiliaryShpDrawingWin32.h"
#include "ui/surface/win32/RuntimeMainBackBufferWin32.h"
#include <array>
#include <tuple>
namespace RuntimeAuxiliaryShpDrawingContractDetail {
struct ReferenceDC {
    HDC dc=GetDC(nullptr);
    ~ReferenceDC(){if(dc)ReleaseDC(nullptr,dc);}
};
auto counters(const RuntimeMapRenderCache& c) {
    return std::make_tuple(c.lastAuxiliaryEffectKind,c.lastProjectileImageId,
        c.lastProjectilePaletteSource,c.projectileShpAttempts,c.projectileShpDrawn,
        c.projectileShpMissing,c.projectileShpSkippedInvisible,c.projectileTravelDrawn);
}
std::uint64_t hashBytes(concord::ui::ConstMenuPixelSurfaceView v) {
    std::uint64_t h=14695981039346656037ULL;
    for(unsigned char b:v.bytes)h=(h^b)*1099511628211ULL;
    return h;
}
}
bool runRuntimeAuxiliaryShpDrawingContract(const RuntimeMapRenderContext& original,
    const std::string& output,std::string& error) {
    using namespace RuntimeAuxiliaryShpDrawingContractDetail;
    const auto fail=[&](const std::string& why){error="Auxiliary SHP contract: "+why;return false;};
    if(!original.rules||!original.art)return fail("original rules/art not loaded");
    std::filesystem::path root;
    std::ofstream rows;
    if(!output.empty()){
        root=std::filesystem::path(output);
        if(!std::filesystem::create_directory(root))return fail("output must be new");
        rows.open(root/"pairs.csv");if(!rows)return fail("output CSV open");
        rows<<"case,mode,image,frame,main,viewport,center,dx,dy,result,referenceHash,standardHash\n";
    }
    constexpr int width=320,height=240;
    constexpr std::uint32_t background=0xff172b39U;
    const std::array<RECT,5> views{{{0,0,width,height},{17,23,293,211},
        {-19,-11,121,103},{54,38,54,38},{90,80,70,60}}};
    const std::array<POINT,5> centers{{{160,120},{0,0},{319,239},{-21,-9},{341,261}}};
    const std::array<const char*,11> images{{"CRATE","WCRATE","MONEY","ARMOR",
        "FIREPOWR","HEALALL","REVEAL","SPEED","VETERAN","DRAGON","CONCORD_P12_MISSING_AUX_IMAGE"}};
    unsigned pairs=0,positive=0,negative=0,changed=0,samples=0,cycles=0,defaultPairs=0;
    std::uint64_t pixelsCompared=0;
    ReferenceDC reference;
    if(!reference.dc)return fail("actual desktop-compatible DC unavailable");
    const DWORD guiInitial=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    DWORD handlesInitial=0;if(!GetProcessHandleCount(GetCurrentProcess(),&handlesInitial))return fail("process handle query");
    {
        concord::ui::win32::MainBackBufferWin32 a,b;
        if(!a.ensure(reference.dc,width,height)||!b.ensure(reference.dc,width,height))
            return fail("existing native surface owner allocation");
        RuntimeMapCamera camera;camera.initialized=true;
        bool crateVisible[3][2]{};
        for(int mode=0;mode<3;++mode){
            auto ac=original,bc=original;
            ac.detailStatsEnabled=bc.detailStatsEnabled=false;
            ac.vision=bc.vision=nullptr;
            ac.surfaceWidth=bc.surfaceWidth=width;ac.surfaceHeight=bc.surfaceHeight=height;
            ac.surfacePixels=mode==2?nullptr:static_cast<std::uint32_t*>(a.pixels);
            bc.surfacePixels=mode==2?nullptr:static_cast<std::uint32_t*>(b.pixels);
            auto ca=std::make_unique<RuntimeMapRenderCache>();
            auto cb=std::make_unique<RuntimeMapRenderCache>();
            const HDC ad=mode==0?nullptr:a.memoryDc,bd=mode==0?nullptr:b.memoryDc;
            for(std::size_t image=0;image<images.size();++image)
            for(int frame:{0,1})for(bool main:{false,true})
            for(std::size_t vi=0;vi<views.size();++vi)
            for(std::size_t pi=0;pi<centers.size();++pi){
                if(!GdiFlush())return fail("pre-write flush");
                a.beginWrite();b.beginWrite();
                std::fill_n(static_cast<std::uint32_t*>(a.pixels),width*height,background);
                std::fill_n(static_cast<std::uint32_t*>(b.pixels),width*height,background);
                if(SelectClipRgn(a.memoryDc,nullptr)==ERROR||SelectClipRgn(b.memoryDc,nullptr)==ERROR)
                    return fail("native clip reset");
                if(vi==1&&(IntersectClipRect(a.memoryDc,29,31,271,197)==ERROR
                    ||IntersectClipRect(b.memoryDc,29,31,271,197)==ERROR))return fail("native clip selection");
                const int dx=(vi%2)?-13:0,dy=(pi%2)?17:0;
                const std::string id=images[image];
                // The original Native function is the reference. It retains
                // its exact source body and receives raw Native values directly.
                const bool defaultCase=vi==0&&pi==0&&frame==0&&!main;
                const bool expected=defaultCase
                    ?::drawRuntimeAuxiliaryShpFrame(ad,views[vi],centers[pi],ac,camera,*ca,id,frame)
                    : ::drawRuntimeAuxiliaryShpFrame(ad,views[vi],centers[pi],ac,camera,*ca,id,frame,dx,dy,main);
                bool actual=false;
                {
                    RuntimeAuxiliaryShpDrawTargetWin32 target(bd);
                    const RuntimeInputPixelRect r{views[vi].left,views[vi].top,views[vi].right,views[vi].bottom};
                    const RuntimeInputPixelPoint p{centers[pi].x,centers[pi].y};
                    actual=defaultCase
                        ?::drawRuntimeAuxiliaryShpFrame(target,r,p,bc,camera,*cb,id,frame)
                        : ::drawRuntimeAuxiliaryShpFrame(target,r,p,bc,camera,*cb,id,frame,dx,dy,main);
                    if(defaultCase)++defaultPairs;
                } // Borrow ends before reading/resetting/releasing its owner.
                const auto av=a.completedPixels(),bv=b.completedPixels();
                if(!av.valid()||!bv.valid())return fail("actual completed raw DIB views");
                if(expected!=actual||av.bytes.size()!=bv.bytes.size()
                    ||!std::equal(av.bytes.begin(),av.bytes.end(),bv.bytes.begin())
                    ||counters(*ca)!=counters(*cb))return fail("native/std bool/raw-pixel/cache mismatch case="+std::to_string(pairs));
                const auto* values=static_cast<const std::uint32_t*>(a.pixels);
                const bool visible=std::any_of(values,values+width*height,
                    [](std::uint32_t p){return p!=background;});
                if(image<2&&vi==0&&pi==0&&frame==0&&main&&expected&&visible)crateVisible[mode][image]=true;
                if(image+1==images.size()&&expected)return fail("reserved missing asset unexpectedly drawn");
                if(expected)++positive;else ++negative;if(visible)++changed;
                pixelsCompared+=static_cast<std::uint64_t>(width)*height;
                if(rows.is_open())rows<<pairs<<','<<mode<<','<<id<<','<<frame<<','<<main<<','<<vi<<','<<pi
                    <<','<<dx<<','<<dy<<','<<actual<<','<<hashBytes(av)<<','<<hashBytes(bv)<<'\n';
                if(!output.empty()&&frame==0&&main&&vi==0&&pi==0){
                    std::vector<std::uint32_t> aa(width*height),bb(width*height);
                    std::memcpy(aa.data(),av.bytes.data(),av.bytes.size());std::memcpy(bb.data(),bv.bytes.data(),bv.bytes.size());
                    const std::string label="mode"+std::to_string(mode)+"-"+std::to_string(image);
                    if(!writeRuntimeBgraBmp((root/(label+"-native.bmp")).string(),width,height,aa)
                        ||!writeRuntimeBgraBmp((root/(label+"-standard.bmp")).string(),width,height,bb))
                        return fail("sample raw BMP write");
                    ++samples;
                }
                ++pairs;
            }
            if(!crateVisible[mode][0]||!crateVisible[mode][1])return fail("original land/water crate visibility not covered");
            const DWORD guiWarm=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
            DWORD handlesWarm=0;if(!GetProcessHandleCount(GetCurrentProcess(),&handlesWarm))return fail("warm handles");
            for(unsigned repeat=0;repeat<128;++repeat){
                if(!drawRuntimeAuxiliaryShpFrameWin32(bd,views[0],centers[0],bc,camera,*cb,"CRATE",0,0,0,true))
                    return fail("borrow/release repeated draw");
                if(!b.completedPixels().valid())return fail("borrowed target invalidated owner");
                ++cycles;
            }
            DWORD handlesAfter=0;if(!GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)
                ||GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)!=guiWarm||handlesAfter!=handlesWarm)
                return fail("borrowed target leaked or destroyed native resources");
        }
        if(!a.release()||!b.release()||!a.nativeResourcesReleased()||!b.nativeResourcesReleased())
            return fail("existing native surface owner release/selection restore");
    }
    DWORD handlesFinal=0;if(!GetProcessHandleCount(GetCurrentProcess(),&handlesFinal)
        ||GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)!=guiInitial||handlesFinal!=handlesInitial)
        return fail("complete resource balance");
    if(rows.is_open()){rows.flush();if(!rows)return fail("CSV final write");}
    if(pairs!=3300||positive==0||negative==0||changed==0||cycles!=384||defaultPairs!=33)return fail("case coverage");
    std::cout<<"Auxiliary SHP drawing native contract: pairs="<<pairs<<" rawPixels="<<pixelsCompared
        <<" positive="<<positive<<" negative="<<negative<<" changed="<<changed
        <<" modes=3 landWater=6 clip=original defaults="<<defaultPairs<<" missing=yes samples="<<samples
        <<" borrowCycles="<<cycles<<" rawAlpha=exact gdi="<<guiInitial<<"="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)
        <<" handles="<<handlesInitial<<"="<<handlesFinal<<" failures=0 skipped=0\n";
    error.clear();return true;
}
