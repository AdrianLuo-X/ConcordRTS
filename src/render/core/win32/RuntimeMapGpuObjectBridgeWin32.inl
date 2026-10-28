// G005 actual GPU source materials. CPU depth/owner acceptance and order remain authoritative.
static void runtimeGpuDepthOperations(const RuntimeBuildingDepthBlendPixel& blend,std::size_t afterOrdinal,
    std::vector<std::array<std::uint32_t,4>>& operations) {
    int groupOwner=-1;const auto finish=[&]{if(groupOwner>=0&&runtimeBuildingDepthFrame->frozenOwners.contains(groupOwner))operations.push_back({0,257});};
    for(const auto& layer:blend.layers){if(afterOrdinal){const auto found=runtimeBuildingDepthFrame->ownerPaintOrdinals.find(layer.owner);
            if(found!=runtimeBuildingDepthFrame->ownerPaintOrdinals.end()&&found->second<=afterOrdinal)continue;}
        if(layer.owner!=groupOwner){finish();groupOwner=layer.owner;if(runtimeBuildingDepthFrame->frozenOwners.contains(groupOwner))operations.push_back({0,256});}
        operations.push_back(layer.material==UINT32_MAX?std::array<std::uint32_t,4>{layer.color,static_cast<std::uint32_t>(layer.opacity),0,0}:std::array<std::uint32_t,4>{layer.material,static_cast<std::uint32_t>(layer.opacity),layer.sourceIndex,1});
    }finish();
}
static bool runtimeGpuRestoreFrozenUnderlay(const RuntimeMapRenderContext& context,int owner) {
    if(!runtimeGpuWorldTarget(context))return false;auto* frame=runtimeBuildingDepthFrame;
    if(!frame||!frame->frozenOwners.contains(owner)||frame->frozenUnderlays.empty())return true;
    try{const int height=frame->viewport.bottom-frame->viewport.top;std::vector<std::array<std::uint32_t,2>> descriptors(frame->pixels.size());std::vector<std::array<std::uint32_t,4>> operations;
        for(const auto& [p,blend]:frame->frozenUnderlays){if(frame->pixels[p].visibleOwner!=owner)continue;const auto start=operations.size();
            runtimeGpuDepthOperations(blend,frame->nonBuildingPixels[p],operations);descriptors[p]={static_cast<std::uint32_t>(start),static_cast<std::uint32_t>(operations.size()-start)};}
        (void)runtimeGpuWorldChain(frame->viewport,frame->width,height,descriptors,operations);
    }catch(...){runtimeGpuWorldOwner->reject("world-frozen-underlay-budget");}return true;
}
static bool runtimeGpuWorldShp(HDC dc,const RECT& viewport,const POINT& center,const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,const RuntimeShpFrameImage& image,const std::vector<RuntimePaletteColor>& palette,
    bool ownerRemap,COLORREF remapColor,int brightnessLift,bool vxlColorGrade,bool vxlPaletteLookup,int opacity,
    const RuntimeBuildingDepthFrame& depth,bool depthEnabled) {
    if(!runtimeGpuWorldDc(dc)||!runtimeGpuWorldTarget(context))return false;
    try{
        const auto rect=runtimeShpFrameScreenRect(center,camera,image);const RECT clipped{std::max(viewport.left,rect.left),std::max(viewport.top,rect.top),std::min(viewport.right,rect.right),std::min(viewport.bottom,rect.bottom)};
        auto c=runtimeGpuWorldRegion(clipped,concord::render::MapDrawKind::Sprite);if(c.left>=c.right||c.top>=c.bottom)return true;
        const auto material=runtimeGpuPrepareSpriteMaterial(context,camera,image,palette,ownerRemap,remapColor,brightnessLift,vxlColorGrade,vxlPaletteLookup);
        if(material==UINT32_MAX)return true;
        std::vector<unsigned char> mask(static_cast<std::size_t>(image.width)*image.height);std::vector<std::array<std::uint32_t,2>> descriptors;std::vector<std::array<std::uint32_t,4>> operations;
        if(depthEnabled&&!runtimeBuildingDepthFrame->blends.empty())descriptors.resize(mask.size());
        for(int sy=c.top;sy<c.bottom;++sy)for(int sx=c.left;sx<c.right;++sx){const int x=sx-rect.left,y=sy-rect.top;const auto index=static_cast<std::size_t>(y)*image.width+x;if(!image.pixels[index])continue;
            if(depthEnabled&&!acceptRuntimeBuildingPixel(depth,rect,x,y,opacity))continue;
            if(depthEnabled&&!descriptors.empty()){
                auto* frame=runtimeBuildingDepthFrame;const auto p=static_cast<std::size_t>(sy-frame->viewport.top)*frame->width+sx-frame->viewport.left;auto found=frame->blends.find(p);
                if(found!=frame->blends.end()){if(!found->second.emitted){const auto start=operations.size();runtimeGpuDepthOperations(found->second,0,operations);
                        descriptors[index]={static_cast<std::uint32_t>(start),static_cast<std::uint32_t>(operations.size()-start)};found->second.emitted=true;}continue;}}
            mask[index]=1;if(!depthEnabled&&opacity>0)recordRuntimeBuildingUnderlayForeground(sx,sy);
        }
        auto* list=runtimeGpuWorldOwner->drawList();if(!list)return true;c.originX=rect.left;c.originY=rect.top;c.width=image.width;c.height=image.height;c.stride=image.width;c.flags=0;c.parameter=static_cast<std::uint32_t>(std::clamp(opacity,0,255));
        c.offset=material;c.auxiliary=list->intern(mask);(void)runtimeGpuWorldEmit(c);
        if(!operations.empty())(void)runtimeGpuWorldChain(rect,image.width,image.height,descriptors,operations);
    }catch(...){runtimeGpuWorldOwner->reject("world-sprite-bridge-budget");}return true;
}

static std::uint32_t runtimeGpuPrepareSpriteMaterial(const RuntimeMapRenderContext& context,const RuntimeMapCamera& camera,
    const RuntimeShpFrameImage& image,const std::vector<RuntimePaletteColor>& palette,bool ownerRemap,COLORREF remapColor,
    int brightnessLift,bool vxlColorGrade,bool vxlPaletteLookup) {
    try {std::optional<std::uint32_t> captured;const auto* observation=runtimeSpriteGeometryObservation;
        const int before=observation?observation->geometry.layerCount:0;
        {RuntimeSpriteGeometryObservationScope observe(nullptr);RuntimeBuildingDepthColorSampleScope isolate;
            RuntimeGpuSpriteMaterialCaptureScope capture(captured);const auto rect=runtimeShpFrameScreenRect(POINT{0,0},camera,image);
            const POINT center{-rect.left,-rect.top};const RECT bounds{0,0,image.width,image.height};auto input=context;
            input.surfacePixels=nullptr;input.surfaceWidth=image.width;input.surfaceHeight=image.height;
            if(!drawRuntimeShpFrame(nullptr,bounds,center,input,camera,image,palette,ownerRemap,remapColor,brightnessLift,vxlColorGrade,vxlPaletteLookup,255)||!captured)
                throw std::runtime_error("sprite material capture");}
        if(runtimeSpriteGeometryObservation!=observation||(observation&&observation->geometry.layerCount!=before))throw std::runtime_error("sprite material geometry leak");
        if(runtimeRendererEnvironmentValue("CONCORD_S41_TRACE_SOURCE_MATERIAL"))std::fprintf(stderr,"S41 source-material captured=1 cpuSourceRgbPixels=0 beforeLayers=%d afterLayers=%d width=%d height=%d\n",before,observation?observation->geometry.layerCount:0,image.width,image.height);
        return *captured;
    }catch(...){if(runtimeGpuWorldOwner)runtimeGpuWorldOwner->reject("world-sprite-material-budget");return UINT32_MAX;}
}
