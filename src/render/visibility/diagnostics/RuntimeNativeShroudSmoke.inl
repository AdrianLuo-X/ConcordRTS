bool runRuntimeNativeShroudSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    RuntimeMapRenderContext assets;
    assets.assetRootPath=runtimeAssetRootPath(live.config);
    assets.structureRootPath=runtimeStructureRootPath(live.config,live.report.map);
    if (!runRuntimeShroudPerformanceContractCheck(assets,error)
        || !runRuntimeNativeShroudContractCheck(assets,error)
        || !runRuntimeNativeShroudMovementContractCheck(assets,error)) return false;
    const auto fail=[&](const std::string& why) { error="Native shroud scene: "+why;return false; };
    auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
    seedRuntimeLoadScratch(s,live,true);
    s.lighting.reset(s.mapFacts.lighting);s.lightingInitialized=true;
    s.config.computerAiEnabled=false;s.config.multiplayerActive=false;
    s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
    s.runtimePreferencePersistenceEnabled=false;s.campaignMode=false;
    s.visionEnabled=true;s.economy.owner="Americans";
    s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
    s.world.accessObjects().clear();s.selectedObjectIds.clear();s.behaviors.clear();
    s.vision.reset();s.tick=1;
    s.vision.update(s.world,&s.rules,{{"Americans",{"Americans"}}},s.tick);
    int minX=512,minY=512,maxX=0,maxY=0;
    for(const auto& cell:s.world.getTerrainCells()) {
        minX=std::min(minX,cell.x);minY=std::min(minY,cell.y);
        maxX=std::max(maxX,cell.x);maxY=std::max(maxY,cell.y);
    }
    RuntimeCellPoint center{(minX+maxX)/2,(minY+maxY)/2};
    if(s.world.getSummary().theater=="TEMPERATE")center.y-=15;
    if(!s.vision.exploreArea(s.economy.owner,center.x,center.y,10))return fail("explored patch");
    const char* output=std::getenv("CONCORD_NATIVE_SHROUD_OUTPUT");
    const std::string root=output&&*output?output:"build/native-shroud-smoke";
    std::filesystem::create_directories(root);
    const auto render=[&](RuntimeWindowState& state,const std::string& name,
            std::vector<std::uint32_t>& pixels) {
        const auto stats=drawRuntimeStateOffscreenAtCell(state,center,960,640,error,
            root+"/"+name+".bmp",false,false,-1,&pixels,false,nullptr,false,true);
        for(auto& pixel:pixels)pixel&=0xffffffU;
        return stats && stats->visionShroudedCells>0
            && state.renderCache.visionOverlay.nativeShroudFingerprint!=0;
    };
    std::vector<std::uint32_t> initial,warm,restored,expanded;
    const auto hash=s.world.deterministicSimulationStateHash();
    if(!render(s,"initial",initial)||!render(s,"warm",warm)||initial!=warm
        ||s.world.deterministicSimulationStateHash()!=hash)return fail("warm cache or render changed simulation: "+error);
    std::ostringstream save;
    if(!writeRuntimeStateToStream(s,save,error))return fail("save "+error);
    IniFile saved;if(!saved.loadFromString(save.str()))return fail("save parse");
    auto loaded=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*loaded,s,true);
    if(!materializeRuntimeLoadStateFromIni(*loaded,saved,error))return fail("load "+error);
    if(loaded->vision.exploredCellIndices(s.economy.owner)!=s.vision.exploredCellIndices(s.economy.owner))
        return fail("exploration did not restore");
    if(!render(*loaded,"restored",restored)||initial!=restored)return fail("restored pixels "+error);
    s.vision.exploreArea(s.economy.owner,center.x+8,center.y-4,7);
    if(!render(s,"expanded",expanded)||initial==expanded)return fail("exploration did not refresh edge");
    s.vision.resetShroudArea(s.economy.owner,center.x,center.y,3);
    if(!render(s,"reshrouded",warm)||warm==expanded)return fail("shroud reset did not refresh edge");
    std::ofstream savedFile(root+"/scene-save.ini",std::ios::binary);savedFile<<save.str();
    std::cout<<"Native shroud scene: theater="<<s.world.getSummary().theater
        <<" warm=exact saveLoad=exact exploredRefresh=yes shroudReset=yes simulation=unchanged screenshots=5\n";
    return true;
}
