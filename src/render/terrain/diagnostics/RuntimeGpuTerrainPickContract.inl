// G004-G009-01: authoritative CPU surface owner queries never decode a GPU surface.
static void verifyRuntimeGpuTerrainPickContract(const GameWorld& world,const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,const RECT& viewport,RuntimeMapRenderCache& cpu,RuntimeMapRenderCache& gpu) {
    const auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    const auto tagged=[](const auto& cache){return cache.terrainLayer.key.find("|gpu-material=1")!=std::string::npos;};
    require(cpu.terrainLayer.built&&gpu.terrainLayer.built&&!tagged(cpu)&&tagged(gpu),"actual CPU RGB/GPU material terrain before owner query");
    const auto cpuKey=cpu.terrainLayer.key,gpuKey=gpu.terrainLayer.key;const auto gpuPixels=gpu.terrainLayer.pixels;const auto cpuMisses=cpu.terrainLayerCacheMisses,gpuMisses=gpu.terrainLayerCacheMisses;
    const auto query=[&](const RuntimeMapCamera& pose){int positive=0;
        const auto one=[&](int x,int y){POINT a{-32768,32767},b=a;const bool ca=pickRuntimeMapSurfaceCell(world,context,pose,viewport,{x,y},cpu,a),cb=pickRuntimeMapSurfaceCell(world,context,pose,viewport,{x,y},gpu,b);require(ca==cb&&a.x==b.x&&a.y==b.y,"original exact CPU/GPU surface owner and failure outputs");positive+=ca;};
        for(int row=0;row<5;++row)for(int column=0;column<5;++column)one(viewport.left+column*(viewport.right-viewport.left-1)/4,viewport.top+row*(viewport.bottom-viewport.top-1)/4);
        one(viewport.left-1,viewport.top-1);one(viewport.right,viewport.bottom);return positive;
    };
    const int fixed=query(camera);require(fixed>0,"actual nonempty surface owner probes");
    require(cpu.terrainLayer.key==cpuKey&&gpu.terrainLayer.key==gpuKey&&gpu.terrainLayer.pixels==gpuPixels&&cpu.terrainLayerCacheMisses==cpuMisses&&gpu.terrainLayerCacheMisses==gpuMisses,"same camera owner query preserves GPU tagged pixels/key and causes no RGB rebuild");
    auto panned=camera;panned.worldOffsetY+=std::max(camera.tileHeight*9,std::max(1,static_cast<int>(viewport.bottom-viewport.top)/3));const int shifted=query(panned);require(shifted>0,"actual panned nonempty surface owner probes");
    require(cpu.terrainLayerCacheMisses==cpuMisses+1&&gpu.terrainLayerCacheMisses==gpuMisses+1&&!tagged(cpu)&&tagged(gpu),"one original cross-bucket geometry rebuild preserves each colour encoding");
    const auto& a=cpu.terrainLayer;const auto& b=gpu.terrainLayer;require(a.width==b.width&&a.height==b.height&&a.originX==b.originX&&a.originY==b.originY&&a.terrainDepthWords==b.terrainDepthWords&&a.terrainCellIndices==b.terrainCellIndices&&a.surfaceCellIndices==b.surfaceCellIndices,"complete panned CPU terrain native depth and both owner planes exact");
    auto decoded=b.pixels;require(gpu.palette.size()>=256,"original owner-query terrain palette");for(auto& pixel:decoded)if(pixel&0x80000000U){require((pixel&0x7fffff00U)==0,"valid raw terrain index");pixel=packDibColor(gpu.palette[pixel&255U]);}require(decoded==a.pixels,"complete panned tagged terrain decodes exactly to original CPU RGB");
    std::cout<<"G004-G009-01 surface pick fixedPositive="<<fixed<<" pannedPositive="<<shifted<<" sameCameraMisses=0 panRebuilds=1 cpuMode=RGB gpuMode=indices completeGeometryPlanes=exact decodedRGB=exact\n";
}
