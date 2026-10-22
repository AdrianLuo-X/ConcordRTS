bool runRuntimeHarvesterCursorSmoke(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "harvester cursor: " + why; return false; };
    const char* output = std::getenv("CONCORD_HARVESTER_CURSOR_OUTPUT");
    const std::string root = output ? output : "";
    if (!root.empty()) std::filesystem::create_directories(root);
    constexpr int width=960, height=640;
    constexpr std::uint32_t background=0x00283840U;
    const RECT client{0,0,width,height}, viewport=runtimeMapViewport(client);
    int cases=0, frames=0, pixelsCompared=0, mismatches=0, exclusions=0, clicks=0;
    for (const std::string type : {"CMIN","HARV","SMIN","YAREFN"})
        for (const std::string family : {"TIB","GEM"}) {
        auto storage=std::make_unique<RuntimeWindowState>(); auto& s=*storage;
        seedRuntimeLoadScratch(s,live,false); s.config.computerAiEnabled=false;
        s.config.multiplayerActive=false; s.config.fixedTickHz=30; s.config.noCooldown=false;
        s.runtimePreferencePersistenceEnabled=false; s.runtimeSuppressPlayerAudioFeedback=true;
        s.runtimeSoundsEnabled=false; s.economy.skirmishStartApplied=true; s.triggers.bootstrapComplete=true;
        s.economy.owner=type=="CMIN"?"Americans":type=="HARV"?"Russians":"YuriCountry";
        s.config.playerCountry=s.economy.owner;s.config.playerSlots.clear();
        s.world=live.world; s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear(); s.world.accessTerrainCells().clear();
        s.terrainByCell.clear(); s.terrainInfoByCell.clear(); s.mapFacts={}; s.mapFactsLoaded=false;
        s.visionEnabled=false;
        const RuntimeCellPoint ore{64,64}, empty{66,64};
        for(int y=44;y<=88;++y)for(int x=44;x<=88;++x) {
            // Native surface ownership uses the map's 512-column index;
            // runtimeCellKey is a separate 1000-column simulation lookup.
            TerrainCell cell; cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;
            cell.tileIndex=0;cell.subTileIndex=0;
            // Point at the field edge so a Slave Miner has a clear 2x2
            // foundation beside the clicked ore, just as in a playable map.
            if(x>=63&&x<=65&&y>=64&&y<=66)setRuntimeResourceTerrainBails(cell,family,12);
            s.world.accessTerrainCells().push_back(cell);
        }
        auto* miner=addRuntimeObjectInstance(s,type=="YAREFN"?RuntimeBuildKind::Building:RuntimeBuildKind::Unit,type,{58,64});
        if(!miner)return fail("original miner missing "+type);
        const int minerId=miner->instanceId;
        miner->owner=s.economy.owner;miner->mission="Guard";miner->harvestAutoSearchSuspended=true;
        miner->buildAnimTicksRemaining=miner->buildAnimTicksTotal=0;
        auto* tank=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"MTNK",{57,60});
        if(!tank)return fail("control tank missing");
        const int tankId=tank->instanceId;tank->owner=s.economy.owner;
        initializeRuntimeNavigation(s); rebuildRuntimeEconomyPower(s);
        RuntimeSnapshotActiveStateScope active(s);
        if ((type=="CMIN"||type=="HARV") && s.economy.harvesters!=1)
            return fail(type+" fixture harvester census");
        s.selectedObjectIds={minerId};s.mouseClientPointValid=true;
        centerRuntimeCameraOnCell(s,client,ore);invalidateRuntimeObjectProjectionCache(s);
        const auto point=runtimeScreenPointForCell(s,viewport,ore,0);
        const auto ground=runtimeScreenPointForCell(s,viewport,empty,0);
        const auto mapped=approximateCellFromScreenPoint(s,viewport,point);
        const auto* resourceHit=runtimeCursorObjectAtPoint(s,viewport,point);
        if(mapped.x!=ore.x||mapped.y!=ore.y||resourceHit) {
            // Failure-only diagnostics preserve the original hit and all
            // cursor/pixel/command gates; no gameplay fixture is changed.
            auto context=runtimeTerrainMetadataContext(s);
            context.art=s.artLoaded?&s.art:nullptr;context.rules=s.rulesLoaded?&s.rules:nullptr;
            context.mapFacts=s.mapFactsLoaded?&s.mapFacts:nullptr;
            context.palettePath=runtimePalettePath(s.config,s.report.map);
            context.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
            context.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);
            POINT surface{-1,-1};
            const bool picked=pickRuntimeMapSurfaceCell(s.world,context,s.camera,viewport,point,s.renderCache,surface);
            const auto& layer=s.renderCache.terrainLayer;
            const int px=point.x-viewport.left+s.camera.worldOffsetX-layer.originX;
            const int py=point.y-viewport.top+s.camera.worldOffsetY-layer.originY;
            const bool inLayer=px>=0&&py>=0&&px<layer.width&&py<layer.height;
            const std::size_t offset=inLayer?static_cast<std::size_t>(py)*layer.width+px:0;
            const auto* fixtureCell=findRuntimeTerrainCellAt(s,ore);
            const auto* fact=runtimeMapQueryCell(s,ore);
            const auto* liveFact=runtimeMapQueryCell(live,ore);
            std::ostringstream details;
            details<<type<<" resource hit fixture family="<<family<<" expectedOre="<<ore.x<<','<<ore.y
                <<" mapped="<<mapped.x<<','<<mapped.y<<" hitId="<<(resourceHit?resourceHit->instanceId:-1)
                <<" point="<<point.x<<','<<point.y<<" viewport="<<viewport.left<<','<<viewport.top<<','<<viewport.right<<','<<viewport.bottom
                <<" camera="<<s.camera.worldOffsetX<<','<<s.camera.worldOffsetY<<','<<s.camera.tileWidth<<','<<s.camera.tileHeight
                <<" activeIsFixture="<<(gRuntimeSnapshotActiveState==&s)<<" activeIsLive="<<(gRuntimeSnapshotActiveState==&live)
                <<" context="<<s.rulesLoaded<<','<<s.artLoaded<<','<<s.theaterLoaded<<','<<s.mapFactsLoaded
                <<" tileRoot="<<context.tileRootPath<<" palette="<<context.palettePath
                <<" surfacePicked="<<picked<<" surface="<<surface.x<<','<<surface.y
                <<" layer="<<layer.built<<','<<layer.width<<','<<layer.height<<','<<layer.originX<<','<<layer.originY
                <<" layerPoint="<<px<<','<<py<<" inLayer="<<inLayer
                <<" surfaceOwner="<<(inLayer&&offset<layer.surfaceCellIndices.size()?layer.surfaceCellIndices[offset]:-1)
                <<" terrainOwner="<<(inLayer&&offset<layer.terrainCellIndices.size()?layer.terrainCellIndices[offset]:-1)
                <<" actualTerrainPixel="<<std::hex<<(inLayer&&offset<layer.pixels.size()?layer.pixels[offset]:0U)<<std::dec
                <<" layerCells="<<layer.terrainCells<<','<<layer.texturedTerrainCells<<','<<layer.fallbackTerrainCells;
            if(fixtureCell)details<<" fixtureCell="<<fixtureCell->x<<','<<fixtureCell->y<<','<<fixtureCell->level
                <<" tile="<<fixtureCell->tileIndex<<','<<fixtureCell->subTileIndex
                <<" overlay="<<fixtureCell->hasOverlay<<','<<fixtureCell->overlayType<<','<<fixtureCell->overlayImage
                <<','<<fixtureCell->overlayIndex<<','<<fixtureCell->overlayFrame<<','<<fixtureCell->overlayIsOre<<','<<fixtureCell->overlayIsGem
                <<" renderRequestedFrame="<<fixtureCell->overlayFrame;
            const auto appendFact=[&](const char* name,const MapCellFacts* value){
                details<<' '<<name<<"Present="<<(value!=nullptr);
                if(value)details<<" overlayFact="<<value->hasOverlay<<','<<value->overlayResource.present<<','<<value->overlayResource.image
                    <<','<<value->overlayResource.requestedFrame<<','<<value->overlayResource.resourceFrame<<','<<value->overlayResource.fallbackFrame;
            };
            appendFact("fixture",fact);appendFact("root",liveFact);
            for(const auto& object:s.world.getObjects())details<<" object="<<object.instanceId<<','<<object.type<<','<<object.x<<','<<object.y
                <<','<<object.level<<','<<object.alive<<','<<object.hasRenderPosition<<','<<object.renderX<<','<<object.renderY;
            for(const auto& object:runtimeProjectedObjectsForViewport(s,viewport))details<<" projected="<<(object.object?object.object->instanceId:-1)
                <<','<<object.bounds.left<<','<<object.bounds.top<<','<<object.bounds.right<<','<<object.bounds.bottom;
            std::vector<std::string> tileKeys;for(const auto& [key,tile]:s.renderCache.tmpTilesByPath){(void)tile;tileKeys.push_back(key);}
            std::sort(tileKeys.begin(),tileKeys.end());
            for(std::size_t n=0;n<std::min<std::size_t>(tileKeys.size(),12);++n){const auto& tile=s.renderCache.tmpTilesByPath.at(tileKeys[n]);
                details<<" nativeTMP="<<tileKeys[n]<<','<<tile.loaded<<','<<tile.images.size()<<','<<tile.errorMessage;}
            details<<" footer="<<(mapped.x!=ore.x||mapped.y!=ore.y?"surface-cell-mismatch":"unexpected-object-hit")
                <<" fullSceneRendered=no";
            return fail(details.str());
        }
        s.mouseClientPoint=point;
        struct Surface {
            RuntimeWindowState& state;HDC screen=GetDC(nullptr);
            ~Surface(){releaseRuntimeBackBuffer(state.backBuffer);if(screen)ReleaseDC(nullptr,screen);}
        } surface{s};
        if(!surface.screen||!ensureRuntimeBackBuffer(s.backBuffer,surface.screen,width,height))return fail("surface");
        const auto clear=[&]{GdiFlush();std::fill_n(static_cast<std::uint32_t*>(s.backBuffer.pixels),width*height,background);};
        const auto capture=[&]{GdiFlush();const auto* p=static_cast<std::uint32_t*>(s.backBuffer.pixels);
            std::vector<std::uint32_t> image(p,p+width*height);for(auto& color:image)color&=0xffffffU;return image;};
        const auto cursor=[&](POINT p){return runtimeMouseCursorFrameForPoint(s,client,p);};
        const auto attack=[&](POINT p){const auto c=cursor(p);return c&&c->frameIndex>=53&&c->frameIndex<=57;};
        std::vector<RuntimePaletteColor> palette;std::string path;
        const auto cacheMix=pathString(std::filesystem::path(runtimeAssetRootPath(s.config))/"ra2"/"cache.mix");
        const auto mouseMix=pathString(std::filesystem::path(runtimeAssetRootPath(s.config))/"ra2"/"conquer.mix");
        if(!loadRuntimePaletteFromMixArchive(cacheMix,s.renderCache,"mousepal",palette,path,error))return fail(error);
        for(int phase=0;phase<5;++phase) {
            s.tick=phase*4;const auto c=cursor(point);
            const bool correct=c&&c->atlasId=="mouse"&&c->frameIndex==53+phase&&c->offsetX==27&&c->offsetY==21;
            if(!correct)++mismatches;
            clear();drawRuntimeMouseCursorOverlay(s.backBuffer.memoryDc,client,s);const auto actual=capture();
            RuntimeShpFrameImage sprite;
            if(!loadRuntimeShpFrameFromMixArchive(mouseMix,s.renderCache,"mouse",53+phase,sprite,path,error))return fail(error);
            std::vector<std::uint32_t> expected(width*height,background);
            for(int y=0;y<sprite.height;++y)for(int x=0;x<sprite.width;++x) {
                const int dx=point.x-27+sprite.offsetX+x,dy=point.y-21+sprite.offsetY+y;
                const auto index=sprite.pixels[y*sprite.width+x];
                if(index&&dx>=0&&dx<width&&dy>=0&&dy<height) {
                    const auto color=palette[index];expected[dy*width+dx]=(color.r<<16)|(color.g<<8)|color.b;
                }
            }
            if(correct&&actual!=expected)return fail(type+" original cursor pixels/hotspot/palette");
            if(correct)++pixelsCompared;
            ++frames;
            if(!root.empty()&&!writeRuntimeSmokeBmp(root+"/"+type+"-"+family+"-"+std::to_string(phase)+".bmp",actual.data(),width,height,error))
                return fail("cursor capture");
        }
        if(attack(ground))return fail("empty ground offers harvest");++exclusions;
        s.selectedObjectIds.clear();if(attack(point))return fail("no selection offers harvest");++exclusions;
        s.selectedObjectIds={tankId};if(attack(point))return fail("ordinary tank offers harvest");++exclusions;
        s.selectedObjectIds={minerId};
        miner=findMutableObjectById(s,minerId);const auto owner=miner->owner;miner->owner="Neutral";
        if(attack(point))return fail("foreign miner offers harvest");miner->owner=owner;++exclusions;
        auto* cell=findMutableRuntimeTerrainCellAt(s,ore); // Preserve the exact resource fixture.
        if(!cell)return fail("resource cell missing");
        const auto original=*cell;setRuntimeResourceTerrainBails(*cell,family,0);
        if(attack(point))return fail("depleted cell offers harvest");*cell=original;++exclusions;
        s.activeSidebarTool=RuntimeSidebarTool::Repair;
        if(cursor(point)->frameIndex!=190)return fail("harvest overrode repair mode");++exclusions;
        s.activeSidebarTool=RuntimeSidebarTool::Sell;
        if(cursor(point)->frameIndex!=149)return fail("harvest overrode sell mode");++exclusions;
        s.activeSidebarTool=RuntimeSidebarTool::None;s.rightPointer.active=true;
        if(cursor(point)->frameIndex!=385)return fail("harvest overrode map pan");++exclusions;
        s.rightPointer.active=false;s.forceAttackHeld=true;
        if(!attack(ground))return fail("force-fire ground was changed");s.forceAttackHeld=false;++exclusions;
        s.visionEnabled=true;s.vision.reset();
        GameWorld unexplored=s.world;unexplored.accessObjects().clear();
        s.vision.update(unexplored,&s.rules,{{s.economy.owner,{s.economy.owner}}},s.tick);
        if(!runtimeLocalVisionRestricted(s)||s.vision.isCellExploredBy(s.economy.owner,ore.x,ore.y))
            return fail("unexplored observer fixture");
        if(attack(point))return fail("hidden resource leaked through cursor");++exclusions;
        s.visionEnabled=false;
        s.selectedObjectIds={tankId,minerId};
        if(!attack(point))++mismatches;
        s.selectedObjectIds={minerId};
        if(!root.empty()) {
            std::vector<std::uint32_t> scene;
            if(!drawRuntimeStateOffscreenAtCell(s,ore,width,height,error,"",false,true,-1,&scene,true,&s.camera))
                return fail(type+" scene: "+error);
            GdiFlush();std::memcpy(s.backBuffer.pixels,scene.data(),scene.size()*sizeof(std::uint32_t));
            drawRuntimeMouseCursorOverlay(s.backBuffer.memoryDc,client,s);GdiFlush();
            if(!writeRuntimeSmokeBmp(root+"/"+type+"-"+family+"-scene.bmp",s.backBuffer.pixels,width,height,error))
                return fail("scene capture");
        }
        // Use the same Primary dispatch as the real left-button release.
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,ore)
            ||s.lastCommand.acceptedCount!=1||s.lastCommand.verb!="harvest"||!s.projectiles.empty())
            return fail(type+" ore click was not a harvest command: "+s.lastCommand.verb+" "+s.interactionStatus);
        ++clicks;
        bool gathered=false;
        for(int step=0;step<1800&&!gathered;++step) {
            ++s.tick;advanceRuntimeSimulation(s);
            for(const auto& [id,task]:s.harvesterTasks)gathered=gathered||task.cargoUnits>0;
            for(const auto& [id,task]:s.slaveMinerTasks)gathered=gathered||task.cargoUnits>0;
            gathered=gathered||s.economy.slaveMinerLoads>0;
        }
        if(!gathered)return fail(type+" clicked resource never yielded cargo: "+s.economy.status);
        ++cases;
        std::cout<<"Harvester cursor: "<<type<<' '<<family<<" original frames checked; Primary click and real cargo passed\n";
    }
    std::cout<<"Harvester cursor: "<<cases<<" cases, "<<frames<<" frames, "<<pixelsCompared
        <<" native pixel comparisons, "<<exclusions<<" exclusions, "<<clicks<<" real harvest commands; mismatches="<<mismatches<<'\n';
    return mismatches==0?true:fail("mineral cursor mappings missing: "+std::to_string(mismatches));
}
