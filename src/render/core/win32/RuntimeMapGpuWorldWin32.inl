// S41/G004 minimum destination-colour boundary. CPU geometry remains authoritative.
using RuntimeGpuWorldNative = concord::ui::win32::MapNativeOverlayWin32;
static thread_local RuntimeGpuWorldNative* runtimeGpuWorldOwner=nullptr;
static thread_local const std::vector<RuntimePaletteColor>* runtimeGpuTerrainPalette=nullptr;
struct RuntimeGpuTerrainPaletteLease {
    const std::vector<RuntimePaletteColor>* previous=runtimeGpuTerrainPalette;
    explicit RuntimeGpuTerrainPaletteLease(const std::vector<RuntimePaletteColor>* value){runtimeGpuTerrainPalette=value;}
    ~RuntimeGpuTerrainPaletteLease(){runtimeGpuTerrainPalette=previous;}
};
struct RuntimeGpuWorldScope {
    RuntimeGpuWorldNative* previous=runtimeGpuWorldOwner;
    explicit RuntimeGpuWorldScope(HDC dc) {auto* n=RuntimeGpuWorldNative::current();runtimeGpuWorldOwner=n&&n->dc()==dc?n:nullptr;}
    ~RuntimeGpuWorldScope(){runtimeGpuWorldOwner=previous;}
};
static bool runtimeGpuWorldTarget(const RuntimeMapRenderContext& context) {
    return runtimeGpuWorldOwner&&context.surfacePixels==nullptr
        &&context.surfaceWidth==runtimeGpuWorldOwner->width()&&context.surfaceHeight==runtimeGpuWorldOwner->height();
}
static bool runtimeGpuWorldDc(HDC dc) {return runtimeGpuWorldOwner&&dc&&runtimeGpuWorldOwner->dc()==dc;}
static bool runtimeGpuWorldEmit(concord::render::MapDrawCommand command) {
    if(!runtimeGpuWorldOwner)return false;return runtimeGpuWorldOwner->emit(command);
}
static concord::render::MapDrawCommand runtimeGpuWorldRegion(const RECT& rect,concord::render::MapDrawKind kind) {
    concord::render::MapDrawCommand c;c.left=std::max(0L,rect.left);c.top=std::max(0L,rect.top);
    c.right=std::min<LONG>(runtimeGpuWorldOwner->width(),rect.right);c.bottom=std::min<LONG>(runtimeGpuWorldOwner->height(),rect.bottom);c.kind=kind;return c;
}
static concord::render::MapDrawCommand runtimeGpuWorldRegion(int left,int top,int right,int bottom,concord::render::MapDrawKind kind){return runtimeGpuWorldRegion(RECT{left,top,right,bottom},kind);}
static RECT runtimeGpuWorldViewport(const RECT* clip,int width,int height){return clip?*clip:RECT{0,0,width,height};}
static bool runtimeGpuWorldPixel(const RuntimeMapRenderContext& context,int x,int y,concord::render::MapDrawKind kind,std::uint32_t value,int alpha=255) {
    if(!runtimeGpuWorldTarget(context)||x<0||y<0||x>=context.surfaceWidth||y>=context.surfaceHeight)return false;
    auto c=runtimeGpuWorldRegion(RECT{x,y,x+1,y+1},kind);c.parameter=value;c.auxiliary=static_cast<unsigned>(std::clamp(alpha,0,255));
    (void)runtimeGpuWorldEmit(c);return true;
}
static bool runtimeGpuWorldIndexed(HDC dc,const RECT& viewport,int originX,int originY,int width,int height,
    const std::vector<unsigned char>& indices,const std::vector<RuntimePaletteColor>& palette) {
    if(!runtimeGpuWorldDc(dc))return false;
    try {
        if(width<=0||height<=0||indices.size()<static_cast<std::size_t>(width)*height||palette.size()<256){runtimeGpuWorldOwner->reject("world-indexed-input");return true;}
        const RECT bounds{std::max<LONG>(viewport.left,originX),std::max<LONG>(viewport.top,originY),std::min<LONG>(viewport.right,originX+width),std::min<LONG>(viewport.bottom,originY+height)};
        auto c=runtimeGpuWorldRegion(bounds,concord::render::MapDrawKind::Indexed);if(c.left>=c.right||c.top>=c.bottom)return true;
        std::array<std::uint32_t,256> colors{};for(std::size_t i=0;i<colors.size();++i)colors[i]=packRuntimeDibColor(palette[i]);
        auto* list=runtimeGpuWorldOwner->drawList();if(!list)return true;c.originX=originX;c.originY=originY;c.width=width;c.height=height;c.stride=width;c.flags=1;c.parameter=255;
        c.offset=list->intern(indices);c.palette=list->internValues<std::uint32_t>(colors);(void)runtimeGpuWorldEmit(c);
    }catch(...){runtimeGpuWorldOwner->reject("world-indexed-budget");}return true;
}
static bool runtimeGpuWorldChain(const RECT& bounds,int width,int height,
    const std::vector<std::array<std::uint32_t,2>>& descriptors,const std::vector<std::array<std::uint32_t,4>>& operations) {
    if(!runtimeGpuWorldOwner)return false;if(operations.empty())return true;
    try{auto* list=runtimeGpuWorldOwner->drawList();if(!list)return true;auto c=runtimeGpuWorldRegion(bounds,concord::render::MapDrawKind::Chain);
        c.originX=bounds.left;c.originY=bounds.top;c.width=width;c.height=height;c.stride=width*8;c.flags=1;c.parameter=static_cast<std::uint32_t>(operations.size());
        c.offset=list->internValues<std::array<std::uint32_t,2>>(descriptors);c.palette=list->internValues<std::array<std::uint32_t,4>>(operations);(void)runtimeGpuWorldEmit(c);
    }catch(...){runtimeGpuWorldOwner->reject("world-depth-chain-budget");}return true;
}
static bool runtimeGpuWorldShadow(HDC dc,const RECT& viewport,int originX,int originY,int width,int height,
    const std::vector<unsigned char>& mask,bool binary,bool rawAlpha,int* darkenedPixels) {
    if(!runtimeGpuWorldDc(dc))return false;
    try{const RECT bounds{std::max<LONG>(viewport.left,originX),std::max<LONG>(viewport.top,originY),std::min<LONG>(viewport.right,originX+width),std::min<LONG>(viewport.bottom,originY+height)};
        auto c=runtimeGpuWorldRegion(bounds,concord::render::MapDrawKind::Shadow);if(c.left>=c.right||c.top>=c.bottom)return true;
        c.originX=originX;c.originY=originY;c.width=width;c.height=height;c.stride=width;c.flags=(binary?1U:0U)|(!runtimeFrameScalingReferenceForRender()?2U:0U)|(rawAlpha?4U:0U);
        auto* list=runtimeGpuWorldOwner->drawList();if(!list)return true;c.offset=list->intern(mask);(void)runtimeGpuWorldEmit(c);
        if(darkenedPixels)for(int y=c.top;y<c.bottom;++y)for(int x=c.left;x<c.right;++x)if(mask[static_cast<std::size_t>(y-originY)*width+x-originX])++*darkenedPixels;
    }catch(...){runtimeGpuWorldOwner->reject("world-shadow-budget");}return true;
}

static bool runtimeGpuWorldFill(HDC dc,const RECT& bounds,std::uint32_t color) {
    if(!runtimeGpuWorldDc(dc))return false;auto c=runtimeGpuWorldRegion(bounds,concord::render::MapDrawKind::Fill);c.parameter=color;(void)runtimeGpuWorldEmit(c);return true;
}

#include "render/core/win32/RuntimeMapGpuSpriteMaterialWin32.inl"
