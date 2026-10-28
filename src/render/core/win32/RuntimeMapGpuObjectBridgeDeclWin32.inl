static bool runtimeGpuRestoreFrozenUnderlay(const RuntimeMapRenderContext&,int);
static bool runtimeGpuWorldShp(HDC,const RECT&,const POINT&,const RuntimeMapRenderContext&,const RuntimeMapCamera&,
    const RuntimeShpFrameImage&,const std::vector<RuntimePaletteColor>&,bool,COLORREF,int,bool,bool,int,const RuntimeBuildingDepthFrame&,bool);

static std::uint32_t runtimeGpuPrepareSpriteMaterial(const RuntimeMapRenderContext&,const RuntimeMapCamera&,const RuntimeShpFrameImage&,const std::vector<RuntimePaletteColor>&,bool,COLORREF,int,bool,bool);
