#include "ui/surface/win32/MapNativeOverlayWin32.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
namespace concord::ui::win32 {
using concord::render::MapDrawCommand;
using concord::render::MapDrawKind;
using concord::render::MapDrawList;
namespace {
thread_local MapNativeOverlayWin32* currentMapNative=nullptr;
bool nativeDiagnosticFlag(const char* name) noexcept {std::size_t required=0;return ::getenv_s(&required,nullptr,0,name)==0&&required>0;}
}
#include "ui/surface/win32/MapNativeOverlayReplayWin32.inl"
struct MapNativeOverlayWin32::Impl {
    MapNativeOverlayWin32* owner=nullptr;
    DWORD thread=GetCurrentThreadId();
    HDC meta=nullptr;
    HENHMETAFILE closed=nullptr;
    int width=0,height=0,cacheWidth=0,cacheHeight=0;
    MapDrawList* list=nullptr;
    std::uint32_t slot=0;
    MapNativeOverlayProbe* probe=nullptr;
    MapNativeOverlayReport report;
    SkirmishBackBufferWin32 replaySurface;
    std::vector<unsigned char> cacheEmf;
    std::vector<NativeOverlayLayer> cacheLayers;
    std::vector<NativeOverlayTable> cacheTables;
    std::uint64_t cacheScratch=0,cachePixels=0;
    bool failed=false,cacheFullReplay=false;
    std::array<DWORD,4> fontEnvironment{},cachedFontEnvironment{};
    bool ownsThread() const noexcept {return thread==GetCurrentThreadId();}
    void fail(const char* reason) noexcept {
        if(!failed){
            const auto* message=reason?reason:"native-unspecified-failure";
            std::snprintf(report.failure.data(),report.failure.size(),"%s",message);
            std::fprintf(stderr,"Map native overlay rejected reason=%s width=%d height=%d commands=%zu resourceBytes=%zu\n",
                message,width,height,list?list->commands().size():std::size_t{0},list?list->resources().size()*4:std::size_t{0});
        }
        failed=true;
    }
    bool marker(std::uint32_t kind,std::uint32_t value) noexcept {
        const std::uint32_t data[]{mapNativeMagic,kind,value};
        if(!GdiComment(meta,sizeof(data),reinterpret_cast<const BYTE*>(data))){fail("native-comment-write");return false;}return true;
    }
    void pending() {
        MapDrawCommand c;c.right=width;c.bottom=height;c.kind=MapDrawKind::Pending;
        slot=static_cast<std::uint32_t>(list->commands().size());list->push(c);
    }
    void releaseMeta() noexcept {
        if(currentMapNative==owner)currentMapNative=nullptr;
        if(meta) {
            const auto old=meta;const auto result=CloseEnhMetaFile(meta);meta=nullptr;
            if(result)DeleteEnhMetaFile(result);
            else if(GetObjectType(old)==OBJ_ENHMETADC)DeleteDC(old);
        }
        if(closed){DeleteEnhMetaFile(closed);closed=nullptr;}
    }
    void discard() noexcept {
        releaseMeta();if(list){try{list->reset();}catch(...){}list=nullptr;}
    }
    bool install(const std::vector<NativeOverlayLayer>& layers,const std::vector<NativeOverlayTable>& tables) {
        if(!list)return false;
        std::uint32_t tableOffset=0;
        if(!tables.empty())tableOffset=list->intern({reinterpret_cast<const unsigned char*>(tables.data()),tables.size()*sizeof(NativeOverlayTable)});
        for(const auto& layer:layers) {
            if(layer.slot>=list->commands().size()||list->commands()[layer.slot].kind!=MapDrawKind::Pending){fail("native-pending-slot-changed");return false;}
            MapDrawCommand c;c.left=layer.bounds.left;c.top=layer.bounds.top;c.right=layer.bounds.right;c.bottom=layer.bounds.bottom;
            const bool empty=layer.pixels.empty()||std::all_of(layer.pixels.begin(),layer.pixels.end(),[](const auto& p){return p.mode==0;});
            if(empty){c={};c.right=width;c.bottom=height;c.kind=MapDrawKind::NoOp;}
            else {
                c.kind=MapDrawKind::NativeLookup;c.originX=c.left;c.originY=c.top;
                c.width=static_cast<std::uint32_t>(c.right-c.left);c.height=static_cast<std::uint32_t>(c.bottom-c.top);c.stride=c.width*8;
                c.palette=tableOffset;c.parameter=static_cast<std::uint32_t>(tables.size());
                c.offset=list->intern({reinterpret_cast<const unsigned char*>(layer.pixels.data()),layer.pixels.size()*sizeof(NativePixelDescriptor)});
            }
            list->replace(layer.slot,c);
        }
        return list->valid();
    }
    bool compile() {
        const auto count=GetEnhMetaFileBits(closed,0,nullptr);
        if(!count||count>MapNativeOverlayWin32::maxEmfBytes){fail("native-emf-budget");return false;}
        std::vector<unsigned char> bytes(count);
        if(GetEnhMetaFileBits(closed,count,bytes.data())!=count){fail("native-emf-bytes");return false;}
        report.emfBytes=count;
        const bool fullReplay=nativeDiagnosticFlag("CONCORD_S41_NATIVE_FULL_REPLAY");
        if(cacheWidth==width&&cacheHeight==height&&cacheFullReplay==fullReplay&&fontEnvironment==cachedFontEnvironment&&bytes==cacheEmf) {
            report.cacheHit=true;report.segments=static_cast<std::uint32_t>(cacheLayers.size());report.lookupGroups=static_cast<std::uint32_t>(cacheTables.size());
            report.lookupBytes=cacheTables.size()*1024;report.scratchBytes=cacheScratch;report.nativePixels=cachePixels;
            return install(cacheLayers,cacheTables);
        }
        if(probe&&probe->failAllocation){fail("native-allocation-injected");return false;}
        NativeOverlayCompilation data;data.width=width;data.height=height;data.fullReplayReference=fullReplay;
        if(!EnumEnhMetaFile(nullptr,closed,scanNativeOverlay,&data,nullptr)||data.failed) {fail(data.failure?data.failure:"native-scan");return false;}
        if(!data.allocate()){fail(data.failure?data.failure:"native-scratch-budget");return false;}
        if(!replaySurface.ensure(metaReference_,width,height)){fail("native-replay-DIB");return false;}
        data.surface=reinterpret_cast<std::uint32_t*>(replaySurface.writablePixels());
        const RECT destination{0,0,width,height};
        const int passCount=fullReplay||!data.exactSampleMask.empty()?256:2;
        for(int pass=0;pass<passCount;++pass) {
            data.gray=pass==0?0:pass==1?255:pass-1;data.replayLayer=0;
            data.partitions.clear();data.usedParents.clear();data.constantSeeds.clear();data.identitySeed=UINT32_MAX;
            if((probe&&probe->failFlush)||!replaySurface.finishGdiWrites()){fail("native-replay-flush");return false;}
            data.clearReplaySurface();
            const int saved=SaveDC(replaySurface.dc());if(!saved){fail("native-replay-save");return false;}
            const bool replayed=!(probe&&probe->failReplay)&&EnumEnhMetaFile(replaySurface.dc(),closed,replayNativeOverlay,&data,&destination)!=FALSE;
            const bool restored=RestoreDC(replaySurface.dc(),saved)!=FALSE;
            if(!replayed||!restored||data.failed||data.replayLayer!=data.layers.size()){
                std::fprintf(stderr,"Map native replay detail gray=%d replayed=%d restored=%d dataFailed=%d layer=%zu expected=%zu reason=%s lastError=%lu\n",
                    data.gray,static_cast<int>(replayed),static_cast<int>(restored),static_cast<int>(data.failed),data.replayLayer,data.layers.size(),data.failure?data.failure:"none",GetLastError());
                fail(data.failure?data.failure:"native-replay");return false;
            }
        }
        report.segments=static_cast<std::uint32_t>(data.layers.size());report.lookupGroups=static_cast<std::uint32_t>(data.tables.size());
        report.lookupBytes=data.tables.size()*1024;report.scratchBytes=data.scratchBytes;report.nativePixels=data.nativePixels;
        if(!install(data.layers,data.tables)){if(!failed)fail("native-frame-values");return false;}
        std::uint64_t sensitivePixels=0;for(auto& layer:data.layers){sensitivePixels+=layer.sensitivePixels.size();std::vector<std::uint32_t>().swap(layer.zero);std::vector<NativeSensitivePixel>().swap(layer.sensitivePixels);}
        if(nativeDiagnosticFlag("CONCORD_S41_NATIVE_DIAGNOSTICS"))std::fprintf(stderr,"Native compilation stats fullReference=%d passes=%d exactMaskBytes=%zu sensitivePixels=%llu nativePixels=%llu scratchBytes=%llu\n",static_cast<int>(fullReplay),passCount,data.exactSampleMask.capacity(),static_cast<unsigned long long>(sensitivePixels),static_cast<unsigned long long>(data.nativePixels),static_cast<unsigned long long>(data.scratchBytes));
        std::uint64_t cached=bytes.size()+data.tables.size()*1024;
        for(const auto& layer:data.layers)cached+=layer.pixels.size()*sizeof(NativePixelDescriptor)+sizeof(NativeOverlayLayer);
        if(cached<=MapDrawList::maxBytes) {
            cacheWidth=width;cacheHeight=height;cacheFullReplay=fullReplay;cachedFontEnvironment=fontEnvironment;
            cacheEmf=std::move(bytes);cacheLayers=std::move(data.layers);cacheTables=std::move(data.tables);
            cacheScratch=report.scratchBytes;cachePixels=report.nativePixels;
        }else {cacheEmf.clear();cacheLayers.clear();cacheTables.clear();cacheWidth=cacheHeight=0;}
        return true;
    }
    const std::uint32_t* referencePixels=nullptr;
    HDC metaReference_=nullptr; // synchronous caller borrow, never retained after finish/cancel
};
MapNativeOverlayWin32::MapNativeOverlayWin32(MapNativeOverlayProbe* probe) noexcept {
    try{impl_=std::make_unique<Impl>();impl_->owner=this;impl_->probe=probe;}catch(...){}
}
MapNativeOverlayWin32::~MapNativeOverlayWin32(){cancel();}
HDC MapNativeOverlayWin32::begin(HDC reference,int width,int height,MapDrawList& list,const std::uint32_t* targetPixels) noexcept {
    if(!impl_||!impl_->ownsThread()||impl_->meta||currentMapNative)return nullptr;
    auto& p=*impl_;p.report={};p.failed=false;
    if(!reference||width<=0||height<=0||width>MapDrawList::maxExtent||height>MapDrawList::maxExtent||GetMapMode(reference)!=MM_TEXT
        ||(p.probe&&p.probe->failBegin)){p.fail("native-begin");return nullptr;}
    try {
        if(GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext())!=DPI_AWARENESS_PER_MONITOR_AWARE) {p.fail("native-reference-DPI-contract");return nullptr;}
        const UINT settings[]{SPI_GETFONTSMOOTHING,SPI_GETFONTSMOOTHINGTYPE,SPI_GETFONTSMOOTHINGCONTRAST,SPI_GETFONTSMOOTHINGORIENTATION};
        for(std::size_t i=0;i<p.fontEnvironment.size();++i)if(!SystemParametersInfoW(settings[i],0,&p.fontEnvironment[i],0)){p.fail("native-font-environment");return nullptr;}
        const auto cx=GetDeviceCaps(reference,HORZRES),cy=GetDeviceCaps(reference,VERTRES);
        if(cx<=0||cy<=0){p.fail("native-reference-DPI");return nullptr;}
        const RECT frame{0,0,MulDiv(width,GetDeviceCaps(reference,HORZSIZE)*100,cx),MulDiv(height,GetDeviceCaps(reference,VERTSIZE)*100,cy)};
        p.meta=CreateEnhMetaFileW(reference,nullptr,&frame,L"ConcordRTS\0S41 map native overlay\0\0");
        if(!p.meta){p.fail("native-create-EMF");return nullptr;}
        // Borrow only from the original owner. GetObject normalizes even top-down DIB heights on this Windows implementation.
        p.referencePixels=targetPixels;
        p.width=width;p.height=height;p.list=&list;p.metaReference_=reference;currentMapNative=this;
        p.pending();
        SetBkMode(p.meta,GetBkMode(reference));SetBkColor(p.meta,GetBkColor(reference));SetTextColor(p.meta,GetTextColor(reference));
        SetROP2(p.meta,GetROP2(reference));SetTextAlign(p.meta,GetTextAlign(reference));SetPolyFillMode(p.meta,GetPolyFillMode(reference));
        SetStretchBltMode(p.meta,GetStretchBltMode(reference));
        for(const int kind:{OBJ_FONT,OBJ_PEN,OBJ_BRUSH}){const auto object=GetCurrentObject(reference,static_cast<UINT>(kind));if(object)SelectObject(p.meta,object);}
        POINT origin{};
        if(GetWindowOrgEx(reference,&origin)&&(origin.x||origin.y))SetWindowOrgEx(p.meta,origin.x,origin.y,nullptr);
        if(GetViewportOrgEx(reference,&origin)&&(origin.x||origin.y))SetViewportOrgEx(p.meta,origin.x,origin.y,nullptr);
        if(GetCurrentPositionEx(reference,&origin))MoveToEx(p.meta,origin.x,origin.y,nullptr);
        HRGN clip=CreateRectRgn(0,0,0,0);
        if(!clip){p.fail("native-reference-clip");p.discard();return nullptr;}
        const int clipResult=GetClipRgn(reference,clip);
        const bool copiedClip=clipResult>=0&&(clipResult==0||SelectClipRgn(p.meta,clip)!=ERROR);
        const bool releasedClip=DeleteObject(clip)!=FALSE;
        if(!copiedClip||!releasedClip){p.fail("native-reference-clip");p.discard();return nullptr;}
        IntersectClipRect(p.meta,0,0,width,height);setBrushColor(p.meta,GetDCBrushColor(reference));setPenColor(p.meta,GetDCPenColor(reference));
        return p.meta;
    }catch(...){p.fail("native-begin-allocation");p.discard();return nullptr;}
}
bool MapNativeOverlayWin32::emit(MapDrawCommand command) noexcept {
    if(!impl_||!impl_->ownsThread()||!impl_->meta||impl_->failed)return false;
    auto& p=*impl_;
    try{if(!p.marker(0,p.slot))return false;p.list->push(command);
        if(command.kind==MapDrawKind::Indexed)++p.report.indexedDraws;
        if(command.kind==MapDrawKind::Shift)++p.report.shifts;
        p.pending();return true;}
    catch(...){p.fail("native-command-budget");return false;}
}
bool MapNativeOverlayWin32::finish() noexcept {
    if(!impl_||!impl_->ownsThread()||!impl_->meta)return false;
    auto& p=*impl_;const auto start=std::chrono::steady_clock::now();bool success=false;
    try {
        if(!p.failed&&p.marker(0,p.slot)) {
            p.closed=CloseEnhMetaFile(p.meta);p.meta=nullptr;currentMapNative=nullptr;
            if(p.closed)success=p.compile();else p.fail("native-close-EMF");
        }
    }catch(...){p.fail("native-finish-budget");}
    p.report.compileMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    if(success){p.releaseMeta();p.list=nullptr;}else p.discard();p.metaReference_=nullptr;return success;
}
void MapNativeOverlayWin32::cancel() noexcept {
    if(impl_&&impl_->ownsThread()){impl_->discard();impl_->metaReference_=nullptr;}
}
bool MapNativeOverlayWin32::active() const noexcept {return impl_&&impl_->ownsThread()&&impl_->meta&&!impl_->failed;}
HDC MapNativeOverlayWin32::dc() const noexcept {return impl_&&impl_->ownsThread()?impl_->meta:nullptr;}
const MapNativeOverlayReport& MapNativeOverlayWin32::report() const noexcept {static const MapNativeOverlayReport empty;return impl_?impl_->report:empty;}
MapNativeOverlayWin32* MapNativeOverlayWin32::current() noexcept {return currentMapNative;}
void MapNativeOverlayWin32::release() noexcept {if(!impl_||!impl_->ownsThread())return;cancel();impl_->replaySurface.release();impl_->cacheEmf.clear();impl_->cacheLayers.clear();impl_->cacheTables.clear();impl_->cacheWidth=impl_->cacheHeight=0;}
void MapNativeOverlayWin32::reject(const char* reason) noexcept {if(impl_&&impl_->ownsThread())impl_->fail(reason);}
MapDrawList* MapNativeOverlayWin32::drawList() noexcept {return active()?impl_->list:nullptr;}
bool MapNativeOverlayWin32::targets(const std::uint32_t* pixels) const noexcept {return active()&&pixels&&impl_->referencePixels==pixels;}
int MapNativeOverlayWin32::width() const noexcept {return impl_?impl_->width:0;}
int MapNativeOverlayWin32::height() const noexcept {return impl_?impl_->height:0;}
void MapNativeOverlayWin32::recordVisionEffects(std::size_t count) noexcept {if(impl_&&impl_->ownsThread())impl_->report.visionEffects+=count;}
COLORREF MapNativeOverlayWin32::readPixel(HDC dc,int x,int y) noexcept {
    if(currentMapNative&&currentMapNative->dc()==dc){currentMapNative->reject("native-destination-read-unsupported");return CLR_INVALID;}
    return GetPixel(dc,x,y);
}
COLORREF MapNativeOverlayWin32::setBrushColor(HDC dc,COLORREF color) noexcept {
    const auto previous=SetDCBrushColor(dc,color);
    if(currentMapNative&&currentMapNative->impl_->meta==dc)currentMapNative->impl_->marker(1,color);
    return previous;
}
COLORREF MapNativeOverlayWin32::setPenColor(HDC dc,COLORREF color) noexcept {
    const auto previous=SetDCPenColor(dc,color);
    if(currentMapNative&&currentMapNative->impl_->meta==dc)currentMapNative->impl_->marker(2,color);
    return previous;
}
} // namespace concord::ui::win32