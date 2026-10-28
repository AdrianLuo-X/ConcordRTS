// Diagnostic only: inspect actual production values, outside formal timing.
static void proveRuntimeMapGpuTerrainPacket(RuntimeWindowState& state,const std::filesystem::path* output,
    const std::string& label,bool gpuActive) {
    const auto require=[](bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);};
    const auto& terrain=state.renderCache.terrainLayer;require(terrain.built&&!terrain.pixels.empty(),"terrain actual cache required");
    const bool materialMode=terrain.key.find("|gpu-material=1")!=std::string::npos;
    require(materialMode==gpuActive,"terrain cache mode follows actual GPU/software fallback");
    std::size_t terrainCommands=0,tagged=0;for(const auto pixel:terrain.pixels)if(pixel&0x80000000U){require((pixel&0x7fffff00U)==0,"terrain source is raw index material");++tagged;}
    if(gpuActive){const auto& commands=state.mapComposition.frame().commands();require(!commands.empty()&&commands.front().kind==concord::render::MapDrawKind::Fill,"GPU world begins with constant, no CPU complete world image");
        for(const auto& c:commands)terrainCommands+=c.kind==concord::render::MapDrawKind::Terrain;require(terrainCommands>0&&tagged>0,"actual terrain material GPU production required");}
    std::cout<<"S41 terrain producer case="<<label<<" gpu="<<gpuActive<<" materialMode="<<materialMode<<" terrainCommands="<<terrainCommands<<" taggedIndices="<<tagged<<" noCpuWorldImage="<<gpuActive<<'\n';
    if(!output||(label!="full800"&&label!="full1280"))return;
    std::vector<std::uint32_t> decoded=terrain.pixels;
    if(materialMode){require(state.renderCache.palette.size()>=256,"terrain original palette");for(auto& p:decoded)if(p&0x80000000U){const auto c=state.renderCache.palette[p&255U];p=(static_cast<std::uint32_t>(c.r)<<16U)|(static_cast<std::uint32_t>(c.g)<<8U)|c.b;}}
    const auto write=[&](const char* suffix,const auto& values){std::ofstream file(*output/(label+suffix),std::ios::binary);file.write(reinterpret_cast<const char*>(values.data()),static_cast<std::streamsize>(values.size()*sizeof(values[0])));require(bool(file),"terrain complete diagnostic cache output");};
    write(".terrain-rgb.bgra",decoded);write(".terrain-z.u16",terrain.terrainDepthWords);write(".terrain-owner.i32",terrain.terrainCellIndices);write(".surface-owner.i32",terrain.surfaceCellIndices);
    std::ofstream meta(*output/(label+".terrain.tsv"));meta<<terrain.width<<'\t'<<terrain.height<<'\t'<<terrain.originX<<'\t'<<terrain.originY<<'\t'<<state.camera.worldOffsetX<<'\t'<<state.camera.worldOffsetY<<'\n';require(bool(meta),"terrain cache geometry dimensions");
}
