// Private finite fixture dispatch. Uses actual loaded stock input and the
// existing complete authority serializer; no model/render algorithms copied.
bool runRuntimeAuxiliaryShpDrawingSmoke(RuntimeWindowState& state,std::string& error) {
    const auto before=runtimeStandaloneSimulationStateHash(state,error);
    if(before==0||!error.empty())return false;
    const int tick=state.tick;
    RuntimeMapRenderContext context;
    context.rules=&state.rules;context.art=&state.art;context.theater=&state.theater;
    context.assetRootPath=runtimeAssetRootPath(state.config);
    context.tileRootPath=runtimeTileRootPath(state.config,state.report.map);
    context.palettePath=runtimePalettePath(state.config,state.report.map);
    context.structureRootPath=runtimeStructureRootPath(state.config,state.report.map);
    context.structurePalettePath=runtimeStructurePalettePath(state.config,state.report.map);
    context.fixedTickHz=state.config.fixedTickHz;
    const char* path=std::getenv("CONCORD_AUXILIARY_SHP_DRAWING_OUTPUT");
    if(!runRuntimeAuxiliaryShpDrawingContract(context,path?path:"",error))return false;
    const auto after=runtimeStandaloneSimulationStateHash(state,error);
    if(!error.empty()||after!=before||state.tick!=tick){error="Auxiliary SHP: complete authority changed";return false;}
    std::cout<<"Auxiliary SHP authority: complete="<<before<<" tick="<<tick<<" unchanged=1\n";
    return true;
}
