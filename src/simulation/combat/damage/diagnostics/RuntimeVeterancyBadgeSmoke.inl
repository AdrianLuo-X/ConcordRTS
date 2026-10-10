bool runRuntimeVeterancyBadgeSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "veterancy badge: " + why; return false; };
    auto storage = std::make_unique<RuntimeWindowState>(); auto& probe = *storage;
    seedRuntimeLoadScratch(probe, live, false);
    probe.world = live.world; probe.world.accessObjects().clear();
    probe.world.accessTerrainObjects().clear(); probe.world.accessSmudges().clear();
    probe.world.accessTerrainCells().clear();
    probe.economy.owner = "Americans"; probe.runtimeSuppressPlayerAudioFeedback = true;
    for (int y = 48; y <= 80; ++y) for (int x = 48; x <= 80; ++x) {
        TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
        cell.tileIndex=cell.subTileIndex=0; probe.world.accessTerrainCells().push_back(cell);
    }
    const std::array<const char*,8> types{"E1","E2","DESO","GGI","ROCK","MTNK","APOC","DISK"};
    constexpr int width=800, height=600, panelWidth=192, panelHeight=192;
    std::vector<std::uint32_t> sheet(panelWidth*8*panelHeight*2,0x00232d37U);
    const char* output=std::getenv("CONCORD_VETERANCY_BADGE_DIR");
    for (std::size_t typeIndex=0;typeIndex<types.size();++typeIndex) {
        probe.world.accessObjects().clear();
        const std::string type=types[typeIndex];
        auto* unit=addRuntimeObjectInstance(probe,typeIndex<5?RuntimeBuildKind::Infantry:RuntimeBuildKind::Unit,type,{64,64});
        if(!unit) return fail("missing stock "+type);
        unit->mission="Guard"; unit->facing=128; unit->subCell=-1;
        if(type=="ROCK" || type=="DISK") { unit->airborne=true; unit->flightLevel=unit->level=2; }
        initializeRuntimeNavigation(probe); probe.selectedObjectIds.clear();
        std::vector<std::uint32_t> baseline;
        if(!drawRuntimeStateOffscreenAtCell(probe,{64,64},width,height,error,"",false,true,-1,&baseline)) return fail(error);
        for(int rank : {1,2}) {
            unit->veterancy=rank; probe.projectedObjectsCacheValid=false;
            std::vector<std::uint32_t> pixels;
            if(!drawRuntimeStateOffscreenAtCell(probe,{64,64},width,height,error,
                    output?std::string(output)+"/"+type+"-"+std::to_string(rank)+".bmp":"",false,true,-1,&pixels)
                || probe.lastVeterancyResourceDraws!=1 || probe.lastVeterancyFallbackDraws!=0) return fail("stock rank resource "+type);
            const RuntimeProjectedObject* projected=nullptr;
            for(const auto& object:probe.projectedObjectsCache) if(object.object->instanceId==unit->instanceId) {projected=&object;break;}
            if(!projected) return fail("projected body "+type);
            RECT ink{width,height,0,0}; int changed=0;
            for(int y=0;y<height;++y) for(int x=0;x<width;++x) if(pixels[y*width+x]!=baseline[y*width+x]) {
                ink.left=std::min<LONG>(ink.left,x); ink.top=std::min<LONG>(ink.top,y);
                ink.right=std::max<LONG>(ink.right,x+1); ink.bottom=std::max<LONG>(ink.bottom,y+1); ++changed;
            }
            if(changed<5 || ink.right-ink.left>8 || ink.bottom-ink.top>13) return fail("rank changed more than its native badge "+type);
            if(typeIndex<5 && (ink.top-projected->spriteBounds.bottom<0
                    || ink.top-projected->spriteBounds.bottom>3)) return fail("badge remained detached from infantry feet "+type);
            const int cropX=projected->renderCenter.x-32;
            const int cropY=(typeIndex<5?projected->spriteBounds.bottom:projected->renderCenter.y)-48;
            for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
                const int sourceX=std::clamp(cropX+x,0,width-1),sourceY=std::clamp(cropY+y,0,height-1);
                const auto color=pixels[sourceY*width+sourceX];
                for(int sy=0;sy<3;++sy) for(int sx=0;sx<3;++sx)
                    sheet[((rank-1)*panelHeight+y*3+sy)*(panelWidth*8)+typeIndex*panelWidth+x*3+sx]=color;
            }
            std::cout<<"Rank badge "<<type<<" rank="<<rank<<" native-pixels="<<changed
                <<" foot-gap="<<ink.top-projected->spriteBounds.bottom<<" selected=false\n";
        }
    }
    if(output && !writeRuntimeSmokeBmp(std::string(output)+"/rank-comparison.bmp",sheet.data(),panelWidth*8,panelHeight*2,error)) return fail(error);
    return true;
}
