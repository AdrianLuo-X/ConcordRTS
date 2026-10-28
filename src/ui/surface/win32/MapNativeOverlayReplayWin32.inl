// Included by the P06 private adapter only. Pointwise native replay never reads
// a GPU surface. Unknown/cross-pixel operations reject the complete GPU frame.
namespace {
constexpr std::uint32_t mapNativeMagic=0x314f4d43U;
struct NativePixelDescriptor {std::uint32_t mode=0,value=0;};
static_assert(sizeof(NativePixelDescriptor)==8);
// Only text, bitmap/conversion and alpha operations can require an arbitrary
// component lookup. All other admitted GDI raster operations are per-bit
// Boolean functions of the destination; their exact table is determined by
// f(0) and f(255). Unknown records still reject the entire frame.
static bool nativeNeedsExactSamples(std::uint32_t kind) noexcept {
    switch(kind){
    case EMR_BITBLT:case EMR_STRETCHBLT:case EMR_MASKBLT:case EMR_PLGBLT:
    case EMR_SETDIBITSTODEVICE:case EMR_STRETCHDIBITS:
    case EMR_EXTTEXTOUTA:case EMR_EXTTEXTOUTW:case EMR_POLYTEXTOUTA:case EMR_POLYTEXTOUTW:
    case EMR_ALPHABLEND:case EMR_TRANSPARENTBLT:case EMR_GRADIENTFILL:return true;
    default:return false;
    }
}
struct NativeSensitivePixel {std::uint32_t layerIndex=0,surfaceIndex=0;};
struct NativeOverlayLayer {
    std::uint32_t slot=0;
    bool needsExactSamples=false;
    RECT bounds{};
    std::vector<std::uint32_t> zero;
    std::vector<NativePixelDescriptor> pixels;
    std::vector<NativeSensitivePixel> sensitivePixels;
};
struct NativeOverlayTable {std::array<std::uint32_t,256> colors{};};
struct NativeOverlayCompilation {
    int width=0,height=0,gray=0;
    RECT pending{};
    POINT current{};
    bool haveCurrent=false,transformed=false,failed=false;
    int penPadding=3;
    std::vector<POINT> positions;
    std::vector<NativeOverlayLayer> layers;
    std::vector<NativeOverlayTable> tables;
    std::unordered_map<std::uint64_t,std::uint32_t> partitions;
    std::unordered_set<std::uint32_t> usedParents;
    std::unordered_map<std::uint32_t,std::uint32_t> constantSeeds;
    std::uint32_t identitySeed=UINT32_MAX;
    bool fullReplayReference=false,pendingNeedsExact=false,hasPaths=false;
    std::vector<unsigned char> exactSampleMask;
    std::unordered_set<std::uint32_t> pinnedTables;
    std::uint64_t scratchBytes=0,nativePixels=0;
    std::size_t replayLayer=0;
    std::uint32_t* surface=nullptr;
    const char* failure=nullptr;
    void fail(const char* reason) noexcept {failed=true;failure=reason;}
    bool comment(const ENHMETARECORD* record,std::uint32_t& kind,std::uint32_t& value) noexcept {
        const auto* c=reinterpret_cast<const EMRGDICOMMENT*>(record);
        if(c->cbData!=12||record->nSize<sizeof(EMR)+sizeof(DWORD)+12)return false;
        std::uint32_t data[3]{};std::memcpy(data,c->Data,sizeof(data));
        if(data[0]!=mapNativeMagic)return false;kind=data[1];value=data[2];return kind<=2;
    }
    void dirty(RECT r,bool exactSamples=false) {
        if(transformed)r={0,0,width,height};
        const auto pad=static_cast<long long>(penPadding);
        r.left=static_cast<LONG>(std::clamp(static_cast<long long>(r.left)-pad,0LL,static_cast<long long>(width)));
        r.top=static_cast<LONG>(std::clamp(static_cast<long long>(r.top)-pad,0LL,static_cast<long long>(height)));
        r.right=static_cast<LONG>(std::clamp(static_cast<long long>(r.right)+pad+1,0LL,static_cast<long long>(width)));
        r.bottom=static_cast<LONG>(std::clamp(static_cast<long long>(r.bottom)+pad+1,0LL,static_cast<long long>(height)));
        if(IsRectEmpty(&r))return;
        if(exactSamples)pendingNeedsExact=true;
        if(exactSamples&&!fullReplayReference){
            const auto area=static_cast<std::uint64_t>(width)*height;
            if(area*5+MapNativeOverlayWin32::maxLookupGroups*2048>MapNativeOverlayWin32::maxScratchBytes){fail("native-scratch-budget");return;}
            if(exactSampleMask.empty())exactSampleMask.assign(static_cast<std::size_t>(area),0);
            for(int y=r.top;y<r.bottom;++y)std::fill_n(exactSampleMask.data()+static_cast<std::size_t>(y)*width+r.left,r.right-r.left,1);
        }
        if(IsRectEmpty(&pending))pending=r;
        else {RECT joined{};UnionRect(&joined,&pending,&r);pending=joined;}
    }
    void full() noexcept {dirty({0,0,width,height});}
    bool scan(const ENHMETARECORD* record) {
        if(failed)return false;
        if(record->nSize<sizeof(EMR)){fail("native-record-size");return false;}
        switch(record->iType){case EMR_BEGINPATH:case EMR_ENDPATH:case EMR_CLOSEFIGURE:case EMR_ABORTPATH:case EMR_FLATTENPATH:case EMR_WIDENPATH:case EMR_SELECTCLIPPATH:hasPaths=true;break;default:break;}
        switch(record->iType) {
        case EMR_GDICOMMENT: {
            std::uint32_t kind=0,value=0;if(!comment(record,kind,value)){fail("native-comment-unsupported");return false;}
            if(kind==0) {for(const auto& old:layers)if(old.slot==value){fail("native-duplicate-slot");return false;}
                NativeOverlayLayer layer;layer.slot=value;layer.bounds=pending;layer.needsExactSamples=pendingNeedsExact;pendingNeedsExact=false;layers.push_back(std::move(layer));pending={};}
            return true;
        }
        case EMR_MOVETOEX: {const auto p=reinterpret_cast<const EMRMOVETOEX*>(record)->ptl;current={p.x,p.y};haveCurrent=true;return true;}
        case EMR_LINETO: {
            const auto p=reinterpret_cast<const EMRLINETO*>(record)->ptl;const POINT next{p.x,p.y};
            if(haveCurrent)dirty({std::min(current.x,next.x),std::min(current.y,next.y),std::max(current.x,next.x),std::max(current.y,next.y)});
            else full();current=next;haveCurrent=true;return true;
        }
        case EMR_SAVEDC: positions.push_back(current);return true;
        case EMR_RESTOREDC: {
            const int relative=reinterpret_cast<const EMRRESTOREDC*>(record)->iRelative;
            if(relative<0&&static_cast<std::size_t>(-static_cast<long long>(relative))<=positions.size()) {
                const auto index=positions.size()-static_cast<std::size_t>(-static_cast<long long>(relative));
                current=positions[index];positions.resize(index);
            }else haveCurrent=false;
            return true;
        }
        case EMR_CREATEPEN: {
            const auto widthValue=reinterpret_cast<const EMRCREATEPEN*>(record)->lopn.lopnWidth.x;
            penPadding=std::max(penPadding,static_cast<int>(std::min<long long>(std::abs(static_cast<long long>(widthValue))+2,8194)));return true;
        }
        case EMR_EXTCREATEPEN: {
            const auto widthValue=reinterpret_cast<const EMREXTCREATEPEN*>(record)->elp.elpWidth;
            penPadding=std::max(penPadding,static_cast<int>(std::min<std::uint64_t>(static_cast<std::uint64_t>(widthValue)+2,8194)));return true;
        }
        case EMR_SETPIXELV: {
            const auto p=reinterpret_cast<const EMRSETPIXELV*>(record)->ptlPixel;dirty({p.x,p.y,p.x,p.y});return true;
        }
        case EMR_ANGLEARC: {
            const auto* a=reinterpret_cast<const EMRANGLEARC*>(record);
            const auto radius=static_cast<LONG>(std::min<DWORD>(a->nRadius,8192));
            dirty({a->ptlCenter.x-radius,a->ptlCenter.y-radius,a->ptlCenter.x+radius,a->ptlCenter.y+radius});haveCurrent=false;return true;
        }
        case EMR_SETICMMODE: {
            DWORD mode=0;std::memcpy(&mode,reinterpret_cast<const unsigned char*>(record)+sizeof(EMR),sizeof(mode));
            if(mode!=ICM_OFF){fail("native-ICM-unsupported");return false;}return true;
        }
        case EMR_SETMAPMODE: {
            DWORD mode=0;std::memcpy(&mode,reinterpret_cast<const unsigned char*>(record)+sizeof(EMR),sizeof(mode));
            if(mode!=MM_TEXT)transformed=true;return true;
        }
        case EMR_SETWORLDTRANSFORM:case EMR_MODIFYWORLDTRANSFORM:
        case EMR_SETWINDOWEXTEX:case EMR_SETWINDOWORGEX:case EMR_SETVIEWPORTEXTEX:case EMR_SETVIEWPORTORGEX:
        case EMR_SCALEVIEWPORTEXTEX:case EMR_SCALEWINDOWEXTEX: transformed=true;return true;
        case EMR_POLYBEZIER:case EMR_POLYGON:case EMR_POLYLINE:case EMR_POLYBEZIERTO:case EMR_POLYLINETO:
        case EMR_POLYPOLYLINE:case EMR_POLYPOLYGON:case EMR_POLYDRAW:case EMR_POLYBEZIER16:case EMR_POLYGON16:
        case EMR_POLYLINE16:case EMR_POLYBEZIERTO16:case EMR_POLYLINETO16:case EMR_POLYPOLYLINE16:case EMR_POLYPOLYGON16:case EMR_POLYDRAW16:
        case EMR_ELLIPSE:case EMR_RECTANGLE:case EMR_ROUNDRECT:case EMR_ARC:case EMR_CHORD:case EMR_PIE:case EMR_ARCTO:
        case EMR_FILLPATH:case EMR_STROKEANDFILLPATH:case EMR_STROKEPATH:
        case EMR_FILLRGN:case EMR_FRAMERGN:case EMR_INVERTRGN:case EMR_PAINTRGN:
        case EMR_BITBLT:case EMR_STRETCHBLT:case EMR_MASKBLT:case EMR_PLGBLT:case EMR_SETDIBITSTODEVICE:case EMR_STRETCHDIBITS:
        case EMR_EXTTEXTOUTA:case EMR_EXTTEXTOUTW:case EMR_POLYTEXTOUTA:case EMR_POLYTEXTOUTW:
        case EMR_ALPHABLEND:case EMR_TRANSPARENTBLT:case EMR_GRADIENTFILL: {
            if(record->nSize<sizeof(EMR)+sizeof(RECT)){fail("native-bounds-size");return false;}
            RECT r{};std::memcpy(&r,reinterpret_cast<const unsigned char*>(record)+sizeof(EMR),sizeof(r));dirty(r,nativeNeedsExactSamples(record->iType));return !failed;
        }
        case EMR_HEADER:case EMR_EOF:case EMR_SETBRUSHORGEX:case EMR_SETBKMODE:case EMR_SETPOLYFILLMODE:
        case EMR_SETROP2:case EMR_SETSTRETCHBLTMODE:case EMR_SETTEXTALIGN:case EMR_SETTEXTCOLOR:case EMR_SETBKCOLOR:
        case EMR_SETMAPPERFLAGS:case EMR_SELECTOBJECT:case EMR_CREATEBRUSHINDIRECT:case EMR_DELETEOBJECT:
        case EMR_EXTCREATEFONTINDIRECTW:case EMR_CREATEMONOBRUSH:case EMR_CREATEDIBPATTERNBRUSHPT:
        case EMR_CREATEPALETTE:case EMR_SELECTPALETTE:case EMR_SETPALETTEENTRIES:case EMR_RESIZEPALETTE:case EMR_REALIZEPALETTE:
        case EMR_SETARCDIRECTION:case EMR_SETMITERLIMIT:case EMR_BEGINPATH:case EMR_ENDPATH:case EMR_CLOSEFIGURE:
        case EMR_ABORTPATH:case EMR_FLATTENPATH:case EMR_WIDENPATH:case EMR_SELECTCLIPPATH:case EMR_EXTSELECTCLIPRGN:
        case EMR_EXCLUDECLIPRECT:case EMR_INTERSECTCLIPRECT:case EMR_OFFSETCLIPRGN:case EMR_SETLAYOUT:
            return true;
        default:fail("native-record-unsupported");return false;
        }
    }
    bool allocate() {
        std::uint64_t total=static_cast<std::uint64_t>(width)*height*4+MapNativeOverlayWin32::maxLookupGroups*2048+exactSampleMask.capacity();
        if(total>MapNativeOverlayWin32::maxScratchBytes){fail("native-scratch-budget");return false;}
        for(auto& layer:layers) {
            const auto columns=std::max(0L,layer.bounds.right-layer.bounds.left);
            const auto area=static_cast<std::uint64_t>(columns)*std::max(0L,layer.bounds.bottom-layer.bounds.top);
            std::size_t sensitive=0;
            if(!exactSampleMask.empty())for(int y=layer.bounds.top;y<layer.bounds.bottom;++y)for(int x=layer.bounds.left;x<layer.bounds.right;++x)
                sensitive+=exactSampleMask[static_cast<std::size_t>(y)*width+x]!=0;
            const auto additional=area*12+static_cast<std::uint64_t>(sensitive)*sizeof(NativeSensitivePixel);
            if(additional>MapNativeOverlayWin32::maxScratchBytes-total){fail("native-scratch-budget");return false;}
            total+=additional;layer.zero.resize(static_cast<std::size_t>(area));layer.pixels.resize(static_cast<std::size_t>(area));nativePixels+=area;
            layer.sensitivePixels.reserve(sensitive);
            if(sensitive)for(int y=layer.bounds.top;y<layer.bounds.bottom;++y)for(int x=layer.bounds.left;x<layer.bounds.right;++x){
                const auto surface=static_cast<std::uint32_t>(static_cast<std::size_t>(y)*width+x);
                if(exactSampleMask[surface])layer.sensitivePixels.push_back({static_cast<std::uint32_t>(static_cast<std::size_t>(y-layer.bounds.top)*columns+x-layer.bounds.left),surface});
            }
        }
        scratchBytes=total;return true;
    }
    void clearReplaySurface() {
        const auto background=static_cast<std::uint32_t>(gray)*0x01010101U;
        if(fullReplayReference||gray==0||gray==255)std::fill_n(surface,static_cast<std::size_t>(width)*height,background);
        else for(const auto& layer:layers)for(const auto& pixel:layer.sensitivePixels)surface[pixel.surfaceIndex]=background;
    }
    std::uint32_t group(std::uint32_t a,std::uint32_t b) {
        const auto key=(static_cast<std::uint64_t>(a)<<32U)|b;
        const auto found=partitions.find(key);if(found!=partitions.end())return found->second;
        if(tables.size()>=MapNativeOverlayWin32::maxLookupGroups)throw std::length_error("native-lookup-budget");
        NativeOverlayTable table;table.colors[0]=a;table.colors[255]=b;
        if(!fullReplayReference)for(std::uint32_t value=0;value<256;++value){const auto mask=value*0x01010101U;table.colors[value]=(a&~mask)|(b&mask);}
        const auto id=static_cast<std::uint32_t>(tables.size());tables.push_back(table);if(!fullReplayReference)pinnedTables.insert(id);partitions.emplace(key,id);return id;
    }
    bool sample(std::uint32_t slot) {
        if(replayLayer>=layers.size()||layers[replayLayer].slot!=slot){fail("native-replay-slot");return false;}
        auto& layer=layers[replayLayer++];std::size_t scanIndex=0;
        if(layer.pixels.empty())return true;
        if((fullReplayReference||gray==0||gray==255||!layer.sensitivePixels.empty())&&!GdiFlush()){fail("native-marker-flush");return false;}
        const auto background=static_cast<std::uint32_t>(gray)*0x01010101U;
        const auto consume=[&](std::size_t index,std::uint32_t& pixel){const auto actual=pixel;pixel=background;
            if(gray==0){layer.zero[index]=actual;return;}
            auto& descriptor=layer.pixels[index];
            if(gray==255) {
                const auto first=layer.zero[index];
                if(first==0&&actual==UINT32_MAX)descriptor={0,0};
                else if(first==actual)descriptor={1,actual};
                else descriptor={2,group(first,actual)};
                return;
            }
            if(descriptor.mode==0) {
                if(actual==background)return;
                if(identitySeed==UINT32_MAX) {
                    if(tables.size()>=MapNativeOverlayWin32::maxLookupGroups)throw std::length_error("native-lookup-budget");
                    NativeOverlayTable identity;for(std::uint32_t i=0;i<256;++i)identity.colors[i]=i*0x01010101U;
                    identitySeed=static_cast<std::uint32_t>(tables.size());tables.push_back(identity);if(!fullReplayReference)pinnedTables.insert(identitySeed);
                }
                descriptor={2,identitySeed};
            } else if(descriptor.mode==1) {
                if(actual==descriptor.value)return;
                const auto color=descriptor.value;const auto found=constantSeeds.find(color);
                if(found!=constantSeeds.end())descriptor={2,found->second};
                else {
                    if(tables.size()>=MapNativeOverlayWin32::maxLookupGroups)throw std::length_error("native-lookup-budget");
                    NativeOverlayTable constant;constant.colors.fill(color);
                    const auto id=static_cast<std::uint32_t>(tables.size());tables.push_back(constant);if(!fullReplayReference)pinnedTables.insert(id);constantSeeds.emplace(color,id);descriptor={2,id};
                }
            }
            const auto parent=descriptor.value;
            if(!fullReplayReference&&pinnedTables.contains(parent)){
                if(tables[parent].colors[static_cast<std::size_t>(gray)]==actual)return;
                const auto key=(static_cast<std::uint64_t>(parent)<<32U)|actual;
                const auto found=partitions.find(key);if(found!=partitions.end()){descriptor.value=found->second;return;}
                if(tables.size()>=MapNativeOverlayWin32::maxLookupGroups)throw std::length_error("native-lookup-budget");
                auto copy=tables[parent];copy.colors[static_cast<std::size_t>(gray)]=actual;descriptor.value=static_cast<std::uint32_t>(tables.size());tables.push_back(copy);partitions.emplace(key,descriptor.value);return;
            }
            const auto key=(static_cast<std::uint64_t>(parent)<<32U)|actual;
            const auto found=partitions.find(key);
            if(found!=partitions.end()){descriptor.value=found->second;return;}
            if(usedParents.insert(parent).second) {
                tables[parent].colors[static_cast<std::size_t>(gray)]=actual;partitions.emplace(key,parent);
            } else {
                if(tables.size()>=MapNativeOverlayWin32::maxLookupGroups)throw std::length_error("native-lookup-budget");
                auto copy=tables[parent];copy.colors[static_cast<std::size_t>(gray)]=actual;
                descriptor.value=static_cast<std::uint32_t>(tables.size());tables.push_back(copy);partitions.emplace(key,descriptor.value);
            }
        };
        if(fullReplayReference||gray==0||gray==255){
            for(int y=layer.bounds.top;y<layer.bounds.bottom;++y)for(int x=layer.bounds.left;x<layer.bounds.right;++x,++scanIndex)consume(scanIndex,surface[static_cast<std::size_t>(y)*width+x]);
        }else if(layer.needsExactSamples)for(const auto& pixel:layer.sensitivePixels)consume(pixel.layerIndex,surface[pixel.surfaceIndex]);
        else for(const auto& pixel:layer.sensitivePixels)surface[pixel.surfaceIndex]=background;
        return true;
    }
};
int CALLBACK scanNativeOverlay(HDC,HANDLETABLE*,const ENHMETARECORD* r,int,LPARAM cookie) {
    auto& data=*reinterpret_cast<NativeOverlayCompilation*>(cookie);
    try{return data.scan(r)?1:0;}catch(...){data.fail("native-scan-allocation");return 0;}
}
int CALLBACK replayNativeOverlay(HDC dc,HANDLETABLE* handles,const ENHMETARECORD* r,int count,LPARAM cookie) {
    auto& data=*reinterpret_cast<NativeOverlayCompilation*>(cookie);
    try {
        if(r->iType==EMR_GDICOMMENT) {
            std::uint32_t kind=0,value=0;if(!data.comment(r,kind,value)){data.fail("native-replay-comment");return 0;}
            if(kind==0)return data.sample(value)?1:0;
            if(kind==1)return SetDCBrushColor(dc,value)!=CLR_INVALID;
            return SetDCPenColor(dc,value)!=CLR_INVALID;
        }
        // Endpoint samples already determine every Boolean-only segment.
        // Skip only drawing primitives with no persistent path/state effect;
        // LineTo retains its exact current-position transition via MoveToEx.
        // Path-containing streams keep the complete original native replay.
        if(!data.fullReplayReference&&data.gray!=0&&data.gray!=255&&!data.hasPaths
            &&data.replayLayer<data.layers.size()&&!data.layers[data.replayLayer].needsExactSamples){
            switch(r->iType){
            case EMR_SETPIXELV:case EMR_RECTANGLE:case EMR_ELLIPSE:case EMR_ROUNDRECT:
            case EMR_POLYGON:case EMR_POLYLINE:case EMR_POLYBEZIER:case EMR_POLYGON16:case EMR_POLYLINE16:case EMR_POLYBEZIER16:
            case EMR_FILLRGN:case EMR_FRAMERGN:case EMR_INVERTRGN:case EMR_PAINTRGN:return 1;
            case EMR_LINETO:{const auto& point=reinterpret_cast<const EMRLINETO*>(r)->ptl;
                if(MoveToEx(dc,point.x,point.y,nullptr))return 1;data.fail("native-current-position");return 0;}
            default:break;
            }
        }
        // SetPixelV outside the actual replay DC clip is an original no-op.
        // PlayEnhMetaFileRecord reports FALSE for that clipped pixel; preserve
        // its no-output semantics without swallowing an in-clip/API failure.
        if(r->iType==EMR_SETPIXELV){
            const auto& point=reinterpret_cast<const EMRSETPIXELV*>(r)->ptlPixel;
            if(!PtVisible(dc,point.x,point.y))return 1;
        }
        const auto replayed=PlayEnhMetaFileRecord(dc,handles,r,static_cast<UINT>(count));
        if(!replayed){std::fprintf(stderr,"Map native record detail type=%lu bytes=%lu gray=%d layer=%zu lastError=%lu\n",r->iType,r->nSize,data.gray,data.replayLayer,GetLastError());data.fail("native-play-record");}
        return replayed;
    }catch(...){data.fail("native-replay-allocation");return 0;}
}
} // namespace