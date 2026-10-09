// Black-box screen picking: the expected cells come from independently
// projected flat/bridge surfaces or the actual renderer's TMP pixel owners.
bool runRuntimeScreenDestinationSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const char* output=std::getenv("CONCORD_SCREEN_DESTINATION_OUTPUT");
    const std::filesystem::path root=output&&*output?output:
        "build/smoke/feature-set-19-complete/01-destination/screen-checks";
    std::filesystem::create_directories(root);
    std::ofstream pixels(root/"screen-pixels.csv"), arrivals(root/"screen-arrivals.csv"), hashes(root/"screen-hashes.csv");
    pixels << "scene,tile_width,pan,phase,pixel_x,pixel_y,expected_x,expected_y,hit_x,hit_y,result\n";
    arrivals << "scene,type,hit_x,hit_y,accepted,target_x,target_y,final_x,final_y,render_x,render_y,on_bridge,ticks,result\n";
    hashes << "scene,tile_width,pan,before,after,result\n";
    const auto fatal=[&](const std::string& why){error="Screen destination: "+why;return false;};
    if(!pixels||!arrivals||!hashes)return fatal("cannot open evidence files");
    auto stock=std::make_unique<RuntimeWindowState>();stock->config=config;
    stock->config.playerSlots.clear();stock->config.gameModeRulesOverride.clear();stock->config.modRootPath.clear();
    stock->report=buildRuntimeStartupReport(stock->config,&stock->world,&stock->theater,
        &stock->art,&stock->rules,&stock->mapFacts);
    if(!stock->report.success)return fatal("original resources unavailable");
    // Keep the startup map/theater/resource context, but make the rule set an
    // explicit retail fixture: no scenario or game-mode overlay is test input.
    const auto nativeRoot=std::filesystem::path(stock->config.localDataRootPath)
        /"resources"/"mix_exports"/"ra2md"/"localmd";
    const auto rulesPath=nativeRoot/"rulesmd.ini",artPath=nativeRoot/"artmd.ini";
    IniFile nativeRules,nativeArt;
    if(!nativeRules.loadFromFile(rulesPath.string())||!nativeArt.loadFromFile(artPath.string())
        ||!stock->rules.loadFromIni(nativeRules)||!stock->art.loadFromIni(nativeArt))
        return fatal("explicit retail rules/art fixture unavailable");
    std::cout<<"Screen destination sources: startupRules="<<stock->report.rulesPath
        <<" fixtureRules="<<rulesPath.generic_string()<<" fixtureArt="<<artPath.generic_string()<<'\n';
    stock->report.rulesPath=rulesPath.generic_string();stock->report.artPath=artPath.generic_string();
    constexpr int width=960,height=640;
    const RECT client{0,0,width,height},viewport=runtimeMapViewport(client);
    struct Surface {
        HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;std::uint32_t* pixels=nullptr;
        Surface(){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* raw=nullptr;
            dc=CreateCompatibleDC(nullptr);if(dc)bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
            pixels=static_cast<std::uint32_t*>(raw);if(bitmap&&pixels)previous=SelectObject(dc,bitmap);}
        ~Surface(){if(previous&&dc)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    } surface;
    if(!surface.dc||!surface.bitmap||!surface.pixels)return fatal("DIB unavailable");
    const auto contextFor=[&](RuntimeWindowState& s){
        RuntimeMapRenderContext c;c.rules=&s.rules;c.art=&s.art;c.theater=&s.theater;
        c.tileRootPath=runtimeTileRootPath(s.config,s.report.map);
        c.palettePath=runtimePalettePath(s.config,s.report.map);
        c.assetRootPath=runtimeAssetRootPath(s.config);
        c.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
        c.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);
        c.surfacePixels=surface.pixels;c.surfaceWidth=width;c.surfaceHeight=height;
        c.useTileTextures=true;c.shadowsEnabled=false;c.vision=nullptr;
        c.animationTick=s.tick;c.fixedTickHz=s.config.fixedTickHz;return c;
    };
    auto metadataContext=contextFor(*stock);
    auto metadataCache=std::make_unique<RuntimeMapRenderCache>();
    TerrainCell clearTile,waterTile,rampTile;bool haveClear=false,haveWater=false,haveRamp=false;
    for(const auto& cell:stock->world.getTerrainCells()) {
        if(cell.hasOverlay)continue;
        const auto land=resolveRuntimeTerrainSubTileLandType(cell,metadataContext,*metadataCache);
        const auto ramp=resolveRuntimeTerrainSubTileRampType(cell,metadataContext,*metadataCache);
        if(!haveClear&&land==RuntimeTerrainLandType::Clear&&!ramp){clearTile=cell;haveClear=true;}
        if(!haveWater&&land==RuntimeTerrainLandType::Water&&!ramp){waterTile=cell;haveWater=true;}
        if(haveClear&&haveWater)break;
    }
    const int rampSet=stock->theater.getGeneralTileSetIndex("RampBase");
    for(const auto& set:stock->theater.getTileSets())if(set.index==rampSet)
        for(int tile=0;tile<set.tilesInSet&&!haveRamp;++tile)for(int sub=0;sub<4&&!haveRamp;++sub){
            TerrainCell candidate;candidate.tileIndex=set.startTileIndex+tile;candidate.subTileIndex=sub;
            const auto ramp=resolveRuntimeTerrainSubTileRampType(candidate,metadataContext,*metadataCache);
            if(ramp>0&&ramp<=4){rampTile=candidate;haveRamp=true;}
        }
    if(!haveClear||!haveWater||!haveRamp)return fatal("stock clear/water/ramp TMP metadata missing");
    // MapDatabase derives OverlayPack identity directly from the ordered INI
    // registry. Check the loaded database against that independent identity;
    // skipping missing sections during id repair previously renamed GAWALL
    // to BRIDGE1 while retaining the wall's image and numeric key.
    const std::string bridgeId="BRIDGE1",bridgeImage=nativeRules.getValue(bridgeId,"Image","");
    int bridgeOrdinal=-1,bridgeIniKey=-1;
    const auto* overlays=nativeRules.getSection("OverlayTypes");
    if(overlays)for(std::size_t i=0;i<overlays->size();++i)if((*overlays)[i].second==bridgeId){
        bridgeOrdinal=static_cast<int>(i);bridgeIniKey=parseRuntimeIntOrDefault((*overlays)[i].first,-1);}
    if(bridgeImage!="BRIDGE"||bridgeOrdinal!=24||bridgeIniKey!=25)
        return fatal("stock BRIDGE1 resource mapping unavailable");
    const auto& loadedOverlays=stock->rules.getOverlayTypes();
    if(static_cast<std::size_t>(bridgeOrdinal)>=loadedOverlays.size()
        ||loadedOverlays[bridgeOrdinal].id!=bridgeId||loadedOverlays[bridgeOrdinal].image!=bridgeImage
        ||loadedOverlays[bridgeOrdinal].listIndex!=bridgeIniKey)
        return fatal("loaded BRIDGE1 identity differs from the original ordered INI registry");
    std::cout<<"Screen destination TMPs: clear="<<clearTile.tileIndex<<'/'<<clearTile.subTileIndex
        <<" water="<<waterTile.tileIndex<<'/'<<waterTile.subTileIndex
        <<" ramp="<<rampTile.tileIndex<<'/'<<rampTile.subTileIndex<<'\n';
    struct Scene {const char* name;int level;bool bridge;bool water;bool ramp;};
    const std::array<Scene,5> scenes{{{"plain",0,false,false,false},{"high12",12,false,false,false},
        {"high_bridge",0,true,false,false},{"stock_ramp",0,false,false,true},{"water",0,false,true,false}}};
    constexpr RuntimeCellPoint goal{32,32},start{27,32};
    int probes=0,pixelFailures=0,commands=0,commandFailures=0;
    std::string firstFailure;
    const auto note=[&](const std::string& detail){if(firstFailure.empty())firstFailure=detail;};
    const auto logBridgeCache=[&](const char* phase,const RuntimeMapRenderCache& cache){
        for(const auto& [path,file]:cache.tmpTilesByPath)if(toLowerAsciiLocal(path).find("bridge")!=std::string::npos){
            std::cout<<"Bridge TMP "<<phase<<": "<<path<<" loaded="<<file.loaded<<" error="<<file.errorMessage;
            if(!file.images.empty()){const auto& frame=file.images.front();
                std::cout<<" offset="<<frame.offsetX<<','<<frame.offsetY<<" size="<<frame.width<<','<<frame.height
                    <<" global="<<frame.globalWidth<<','<<frame.globalHeight;}
            std::cout<<'\n';}
        for(const auto& [key,file]:cache.auxiliaryShpBytesByImage)
            if(file.loaded||toLowerAsciiLocal(key).find("bridge")!=std::string::npos)
                std::cout<<"Bridge SHP "<<phase<<": key="<<key<<" image="<<file.resolvedImageId
                    <<" loaded="<<file.loaded<<" frames="<<file.frameCount<<" source="<<file.sourcePath
                    <<" error="<<file.errorMessage<<'\n';
        for(const auto& [name,path]:cache.looseResourcePathsByName)if(toLowerAsciiLocal(name).find("bridge")!=std::string::npos)
            std::cout<<"Bridge resource "<<phase<<": "<<name<<" -> "<<path<<'\n';
    };
    const auto make=[&](const Scene& scene){
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        s.config=stock->config;s.config.fixedTickHz=30;s.config.gameMode="Battle";
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=false;
        s.config.crates=s.config.superweapons=s.config.shortGame=false;
        s.rules=stock->rules;s.art=stock->art;s.theater=stock->theater;s.report=stock->report;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;
        s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=-1;
        s.economy.owner="Americans";s.economy.skirmishStartApplied=true;s.triggers.loaded=s.triggers.bootstrapComplete=true;
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        RuntimeHumanHouseAuthority authority;authority.owner="Americans";authority.canonicalOwner="americans";
        authority.localFacade=true;s.humanHouseAuthorities.push_back(std::move(authority));
        IniFile mapIni;MapDatabase map;
        if(mapIni.loadFromString("[Header]\nWidth=64\nHeight=64\n[Map]\nTheater=TEMPERATE\nSize=0,0,64,64\nLocalSize=0,0,64,64\n")
            &&map.loadFromIni(mapIni))s.world.loadFromMap(map,&s.rules);
        s.world.accessTerrainCells().clear();
        for(int y=0;y<52;++y)for(int x=0;x<52;++x){
            TerrainCell cell=scene.water?waterTile:clearTile;
            if(scene.ramp&&x==goal.x&&y==goal.y)cell=rampTile;
            cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;cell.level=scene.level;
            cell.hasOverlay=cell.overlayIsOre=cell.overlayIsGem=false;
            cell.overlayIndex=-1;cell.overlayFrame=-1;cell.overlayType.clear();cell.overlayImage.clear();
            if(scene.bridge&&y==goal.y&&x>=24&&x<=40){
                // OverlayPack uses registry order; numeric INI keys start at
                // one and contain gaps, so listIndex is not a native ordinal.
                cell.hasOverlay=true;cell.overlayIndex=bridgeOrdinal;cell.overlayFrame=0;
                cell.overlayType=bridgeId;cell.overlayImage=bridgeImage;
            }
            s.world.accessTerrainCells().push_back(std::move(cell));
        }
        const auto logGoal=[&](const char* phase){if(!scene.bridge)return;
            const auto& c=s.world.getTerrainCells()[goal.y*52+goal.x];
            const auto* art=s.art.findArtType("BRIDGE");
            std::cout<<"Bridge fixture "<<scene.name<<' '<<phase<<": cell="<<c.x<<','<<c.y<<" index512="<<c.cellIndex512
                <<" level="<<c.level<<" overlay="<<c.hasOverlay<<'/'<<c.overlayIndex<<'/'<<c.overlayFrame
                <<" type="<<c.overlayType<<" image="<<c.overlayImage<<" ore="<<c.overlayIsOre<<" gem="<<c.overlayIsGem
                <<" registryOrdinal="<<bridgeOrdinal<<" iniKey="<<bridgeIniKey
                <<" bridgeArt="<<(art?art->id:"null")<<" artImage="<<(art?art->image:"")
                <<" artTheater="<<(art&&art->theater)<<'\n';};
        logGoal("before_init");s.terrainByCell=buildRuntimeTerrainLookup(s.world);initializeRuntimeNavigation(s);logGoal("after_init");
        s.camera.initialized=true;return box;
    };
    // This independent forward formula is fixed by the diamond's four known
    // corners, including the high bridge's four-level deck above its terrain.
    const auto centerFor=[&](const RuntimeWindowState& s,const Scene& scene){
        const int level=scene.level+(scene.bridge?4:0);
        return POINT{viewport.left+((goal.x-goal.y)*s.camera.tileWidth)/2-s.camera.worldOffsetX,
            viewport.top+((goal.x+goal.y-level)*s.camera.tileHeight)/2-s.camera.worldOffsetY};
    };
    const auto cameraFor=[&](RuntimeWindowState& s,const Scene& scene,int tileWidth,int pan){
        s.camera.initialized=true;s.camera.tileWidth=tileWidth;s.camera.tileHeight=tileWidth/2;
        s.camera.worldOffsetX=-((viewport.right-viewport.left)/2)+pan;
        s.camera.worldOffsetY=((goal.x+goal.y-scene.level-(scene.bridge?4:0))*s.camera.tileHeight)/2
            -((viewport.bottom-viewport.top)/2)-pan;
    };
    const auto checkPixel=[&](RuntimeWindowState& s,const Scene& scene,int pan,const char* phase,
        POINT point,RuntimeCellPoint expected){
        const POINT hit=approximateCellFromScreenPoint(s,viewport,point);++probes;
        const bool correct=hit.x==expected.x&&hit.y==expected.y;
        if(!correct){++pixelFailures;note(std::string(scene.name)+" "+phase+" screen cell mismatch");}
        pixels<<scene.name<<','<<s.camera.tileWidth<<','<<pan<<','<<phase<<','<<point.x<<','<<point.y
            <<','<<expected.x<<','<<expected.y<<','<<hit.x<<','<<hit.y<<','<<(correct?"PASS":"FAIL")<<'\n';
    };
    for(const auto& scene:scenes){
        auto box=make(scene);auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        for(int tileWidth:{48,60,72})for(int pan:{0,37}){
            cameraFor(s,scene,tileWidth,pan);
            std::string hashError;
            const auto beforeHash=runtimeStandaloneSimulationStateHash(s,hashError);
            if(!beforeHash||!hashError.empty())return fatal("fixture authority hash: "+hashError);
            const POINT center=centerFor(s,scene);
            if(scene.bridge&&tileWidth==60&&pan==0){
                auto directCache=std::make_unique<RuntimeMapRenderCache>();auto directCamera=s.camera;auto directContext=contextFor(s);
                std::cout<<"Bridge context: tileRoot="<<directContext.tileRootPath<<" assetRoot="<<directContext.assetRootPath
                    <<" structureRoot="<<directContext.structureRootPath<<" theater="<<s.theater.getTileExtension()<<'\n';
                (void)drawRuntimeMap(surface.dc,viewport,s.world,directContext,directCamera,*directCache);GdiFlush();
                std::string imageError;if(!writeRuntimeSmokeBmp((root/"high_bridge-direct-no-query.bmp").string(),
                    surface.pixels,width,height,imageError))return fatal(imageError);
                logBridgeCache("direct_no_query",*directCache);
            }
            if(!scene.ramp)checkPixel(s,scene,pan,"before_paint",center,goal);
            auto context=contextFor(s);
            const auto stats=drawRuntimeMap(surface.dc,viewport,s.world,context,s.camera,s.renderCache);GdiFlush();
            if(!stats.rendered||!s.renderCache.terrainLayer.built)return fatal(std::string(scene.name)+" terrain not rendered");
            const std::vector<std::uint32_t> rendered(surface.pixels,surface.pixels+width*height);
            int scenePixels=0;
            if(scene.bridge){
                if(stats.bridgeOverlayHighCells==0||stats.bridgeOverlaySprites==0)
                    return fatal("high bridge original overlay did not render: high="
                        +std::to_string(stats.bridgeOverlayHighCells)+" sprites="+std::to_string(stats.bridgeOverlaySprites)
                        +" missing="+std::to_string(stats.bridgeOverlayMissing)+" image="+bridgeImage);
                GameWorld bare=s.world;
                for(auto& cell:bare.accessTerrainCells()){cell.hasOverlay=false;cell.overlayIndex=-1;cell.overlayType.clear();cell.overlayImage.clear();}
                auto bareCache=std::make_unique<RuntimeMapRenderCache>();auto bareCamera=s.camera;
                (void)drawRuntimeMap(surface.dc,viewport,bare,context,bareCamera,*bareCache);GdiFlush();
                const std::string imageStem=std::string(scene.name)+"-"+std::to_string(tileWidth)+"-"+std::to_string(pan);
                std::string imageError;
                if(!writeRuntimeSmokeBmp((root/(imageStem+"-on.bmp")).string(),rendered.data(),width,height,imageError)
                    ||!writeRuntimeSmokeBmp((root/(imageStem+"-off.bmp")).string(),surface.pixels,width,height,imageError))
                    return fatal(imageError);
                // Only pixels actually changed by the real bridge sprite are
                // evidence for the deck; the underlying terrain owner is not.
                for(int dy=-s.camera.tileHeight/2+2;dy<=s.camera.tileHeight/2-2;dy+=3)
                    for(int dx=-s.camera.tileWidth/2+2;dx<=s.camera.tileWidth/2-2;dx+=3){
                        if(2.0*std::abs(dx)/s.camera.tileWidth+2.0*std::abs(dy)/s.camera.tileHeight>=0.85)continue;
                        const POINT p{center.x+dx,center.y+dy};const auto index=std::size_t(p.y)*width+p.x;
                        if(rendered[index]==surface.pixels[index])continue;
                        checkPixel(s,scene,pan,"opaque_deck",p,goal);++scenePixels;
                    }
                (void)drawRuntimeMap(surface.dc,viewport,s.world,context,s.camera,s.renderCache);GdiFlush();
                std::cout<<"Screen destination bridge: tile="<<tileWidth<<" pan="<<pan
                    <<" center="<<center.x<<','<<center.y<<" opaque="<<scenePixels
                    <<" sprites="<<stats.bridgeOverlaySprites<<" deferred="<<stats.bridgeOverlayDeferredDrawn<<'\n';
                if(scenePixels<5||(tileWidth==60&&pan==0))logBridgeCache("after_query",s.renderCache);
            }else{
                const auto& layer=s.renderCache.terrainLayer;
                for(int py=std::max<int>(viewport.top,center.y-35);py<std::min<int>(viewport.bottom,center.y+35);py+=3)
                    for(int px=std::max<int>(viewport.left,center.x-45);px<std::min<int>(viewport.right,center.x+45);px+=3){
                        const int lx=px-viewport.left+s.camera.worldOffsetX-layer.originX;
                        const int ly=py-viewport.top+s.camera.worldOffsetY-layer.originY;
                        if(lx<0||ly<0||lx>=layer.width||ly>=layer.height)continue;
                        const auto index=std::size_t(ly)*layer.width+lx;
                        if(index>=layer.terrainCellIndices.size())continue;
                        const int owner=layer.terrainCellIndices[index];
                        if(owner<0)continue;const RuntimeCellPoint expected{owner%512,owner/512};
                        if(scene.ramp&&expected!=goal)continue;
                        checkPixel(s,scene,pan,"rendered_tmp",POINT{px,py},expected);++scenePixels;
                    }
            }
            if(scenePixels<5){++pixelFailures;const std::string why=std::string(scene.name)
                +" insufficient independent surface pixels: tile="+std::to_string(tileWidth)
                +" pan="+std::to_string(pan)+" opaque="+std::to_string(scenePixels);
                note(why);std::cout<<"Screen destination: "<<why<<'\n';}
            if(!scene.ramp)checkPixel(s,scene,pan,"after_paint",center,goal);
            const auto afterHash=runtimeStandaloneSimulationStateHash(s,hashError);
            hashes<<scene.name<<','<<tileWidth<<','<<pan<<','<<beforeHash<<','<<afterHash<<','
                <<(beforeHash==afterHash&&hashError.empty()?"PASS":"FAIL")<<'\n';hashes.flush();
            if(beforeHash!=afterHash||!hashError.empty())return fatal("picking/paint changed authority: "+hashError);
            if(tileWidth==60&&pan==0){std::string imageError;
                if(!writeRuntimeSmokeBmp((root/(std::string(scene.name)+"-surface.bmp")).string(),
                    rendered.data(),width,height,imageError))return fatal(imageError);}
        }
        std::vector<std::string> types;
        if(scene.water)types={"DEST"};else if(scene.ramp)types={"SHAD","JUMPJET"};
        else if(scene.bridge||scene.level)types={"MTNK","E1"};else types={"MTNK","SHAD"};
        for(const auto& type:types){
            auto moving=make(scene);auto& m=*moving;RuntimeSnapshotActiveStateScope movingScope(m);
            auto* object=addRuntimeObjectInstance(m,runtimeBuildKindForUnitTypeId(m.rules,type),type,start);
            if(!object)return fatal(type+" mover spawn failed");const int id=object->instanceId;
            object->harvestAutoSearchSuspended=true;
            if(scene.bridge)setRuntimeObjectCell(m,*object,start,true);
            m.selectedObjectIds={id};cameraFor(m,scene,60,37);
            auto context=contextFor(m);(void)drawRuntimeMap(surface.dc,viewport,m.world,context,m.camera,m.renderCache);GdiFlush();
            POINT click=centerFor(m,scene);
            if(scene.ramp){
                // Choose a visible ramp-owned pixel close to the independent
                // cell centre; a tall TMP need not paint its geometric centre.
                const auto& layer=m.renderCache.terrainLayer;bool found=false;
                for(int radius=0;radius<30&&!found;++radius)for(int dy=-radius;dy<=radius&&!found;++dy)
                    for(int dx=-radius;dx<=radius&&!found;++dx){
                        const int lx=click.x+dx-viewport.left+m.camera.worldOffsetX-layer.originX;
                        const int ly=click.y+dy-viewport.top+m.camera.worldOffsetY-layer.originY;
                        if(lx<0||ly<0||lx>=layer.width||ly>=layer.height)continue;
                        const auto index=std::size_t(ly)*layer.width+lx;
                        if(index<layer.terrainCellIndices.size()&&layer.terrainCellIndices[index]==goal.y*512+goal.x){
                            click.x+=dx;click.y+=dy;found=true;}
                    }
                if(!found)return fatal("ramp command pixel missing");
            }
            const POINT hit=approximateCellFromScreenPoint(m,viewport,click);
            const auto& projected=runtimeProjectedObjectsForViewport(m,viewport);
            if(findProjectedObjectAtPoint(projected,click))return fatal(type+" terrain click unexpectedly hit an object");
            // This is the same primary UI dispatch used for a selected mover's
            // unmodified left click on a terrain pixel.
            const bool dispatched=dispatchRuntimeTargetOrderFromUi(m,RuntimeMultiplayerGameplayTargetOrderKind::Primary,{hit.x,hit.y},nullptr);
            RuntimeCellPoint target{-1,-1};const auto command=m.behaviors.find(id);
            if(command!=m.behaviors.end())target=command->second.moveTarget;
            const int accepted=m.lastCommand.acceptedCount;
            bool arrived=false;int ticks=0;
            for(;ticks<30*180&&accepted==1;++ticks){
                ++m.tick;advanceRuntimeSimulation(m);object=findMutableObjectById(m,id);
                if(!object||!object->alive)break;const auto b=m.behaviors.find(id);
                if(!object->renderMoveActive&&(b==m.behaviors.end()||b->second.state==RuntimeBehaviorState::Complete)){
                    arrived=runtimeCellPointForObject(*object)==goal
                        &&std::abs(runtimeObjectRenderX(*object)-goal.x)<1e-6
                        &&std::abs(runtimeObjectRenderY(*object)-goal.y)<1e-6;++ticks;break;}
            }
            object=findMutableObjectById(m,id);++commands;
            const bool correct=dispatched&&accepted==1&&hit.x==goal.x&&hit.y==goal.y&&target==goal&&arrived
                &&object&&(!scene.bridge||object->onBridge);
            if(!correct){++commandFailures;note(std::string(scene.name)+" "+type+" UI destination/arrival mismatch");}
            arrivals<<scene.name<<','<<type<<','<<hit.x<<','<<hit.y<<','<<accepted<<','<<target.x<<','<<target.y
                <<','<<(object?object->x:-1)<<','<<(object?object->y:-1)<<','
                <<(object?runtimeObjectRenderX(*object):-1)<<','<<(object?runtimeObjectRenderY(*object):-1)
                <<','<<(object&&object->onBridge)<<','<<ticks<<','<<(correct?"PASS":"FAIL")<<'\n';arrivals.flush();
            std::cout<<"Screen destination: "<<scene.name<<' '<<type<<" hit="<<hit.x<<','<<hit.y
                <<" target="<<target.x<<','<<target.y<<" ticks="<<ticks<<' '<<(correct?"PASS":"FAIL")<<'\n';
        }
        pixels.flush();
    }
    {
        const Scene bridge{"bridge_over_higher_terrain",0,true,false,false};
        auto box=make(bridge);auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        // A level-eight tile at (34,34) projects onto the same centre as the
        // level-four deck at (32,32). Both use untouched original art. The
        // real compositor paints the opaque bridge after the terrain pass.
        for(auto& cell:s.world.accessTerrainCells())if(cell.x==34&&cell.y==34)cell.level=8;
        initializeRuntimeNavigation(s);cameraFor(s,bridge,60,0);
        std::string hashError;const auto before=runtimeStandaloneSimulationStateHash(s,hashError);
        if(!before||!hashError.empty())return fatal("higher terrain fixture hash: "+hashError);
        auto context=contextFor(s);auto stats=drawRuntimeMap(surface.dc,viewport,s.world,context,s.camera,s.renderCache);GdiFlush();
        if(!stats.rendered||!stats.bridgeOverlayDeferredDrawn)return fatal("higher terrain bridge did not render");
        const std::vector<std::uint32_t> on(surface.pixels,surface.pixels+width*height);
        GameWorld bare=s.world;
        for(auto& cell:bare.accessTerrainCells()){cell.hasOverlay=false;cell.overlayIndex=-1;cell.overlayFrame=-1;
            cell.overlayType.clear();cell.overlayImage.clear();}
        auto bareCache=std::make_unique<RuntimeMapRenderCache>();auto bareCamera=s.camera;
        stats=drawRuntimeMap(surface.dc,viewport,bare,context,bareCamera,*bareCache);GdiFlush();
        if(!stats.rendered||!bareCache->terrainLayer.built)return fatal("higher terrain bare control did not render");
        std::string imageError;
        if(!writeRuntimeSmokeBmp((root/"bridge-over-higher-terrain-on.bmp").string(),on.data(),width,height,imageError)
            ||!writeRuntimeSmokeBmp((root/"bridge-over-higher-terrain-off.bmp").string(),surface.pixels,width,height,imageError))
            return fatal(imageError);
        const POINT center=centerFor(s,bridge);const auto& layer=bareCache->terrainLayer;int covered=0;
        for(int dy=-12;dy<=12;++dy)for(int dx=-25;dx<=25;++dx){
            if(std::abs(dx)/30.0+std::abs(dy)/15.0>=0.85)continue;
            const POINT p{center.x+dx,center.y+dy};const auto frameIndex=std::size_t(p.y)*width+p.x;
            const int lx=p.x-viewport.left+s.camera.worldOffsetX-layer.originX;
            const int ly=p.y-viewport.top+s.camera.worldOffsetY-layer.originY;
            if(lx<0||ly<0||lx>=layer.width||ly>=layer.height)continue;
            const auto pixelIndex=std::size_t(ly)*layer.width+lx;
            if(pixelIndex>=layer.terrainCellIndices.size()||layer.terrainCellIndices[pixelIndex]!=34*512+34
                ||on[frameIndex]==surface.pixels[frameIndex])continue;
            checkPixel(s,bridge,0,"opaque_over_higher_tmp",p,goal);++covered;
        }
        if(covered<5){++pixelFailures;note("higher terrain bridge control has insufficient visible overlap");}
        const auto after=runtimeStandaloneSimulationStateHash(s,hashError);
        hashes<<bridge.name<<",60,0,"<<before<<','<<after<<','
            <<(before==after&&hashError.empty()?"PASS":"FAIL")<<'\n';
        if(before!=after||!hashError.empty())return fatal("higher terrain picking changed authority: "+hashError);
        std::cout<<"Screen destination: higher terrain opaque overlap="<<covered<<'\n';
    }
    // Cache refresh tests retain the same world and terrain vector. Use the
    // production navigation/bridge refresh entry points after authored changes;
    // arbitrary unannounced writes are not a supported runtime mutation path.
    int freshnessCases=0;
    const auto probeCachedPoint=[&](RuntimeWindowState& s,const Scene& label,POINT click,
        RuntimeCellPoint expected,std::uint32_t& paintedPixel){
        std::string hashError;const auto before=runtimeStandaloneSimulationStateHash(s,hashError);
        if(!before||!hashError.empty())return fatal("cache fixture hash: "+hashError);
        checkPixel(s,label,0,"mutation_before_paint",click,expected);
        const auto cacheKey=s.renderCache.terrainLayer.key;
        const auto* cachePixels=s.renderCache.terrainLayer.pixels.data();
        checkPixel(s,label,0,"mutation_warm",click,expected);
        if(s.renderCache.terrainLayer.key!=cacheKey||s.renderCache.terrainLayer.pixels.data()!=cachePixels)
            return fatal("unchanged surface query rebuilt its cache");
        auto context=contextFor(s);
        const auto stats=drawRuntimeMap(surface.dc,viewport,s.world,context,s.camera,s.renderCache);GdiFlush();
        if(!stats.rendered)return fatal("cache mutation surface did not render");
        paintedPixel=surface.pixels[std::size_t(click.y)*width+click.x];
        checkPixel(s,label,0,"mutation_after_paint",click,expected);
        const auto after=runtimeStandaloneSimulationStateHash(s,hashError);
        hashes<<label.name<<','<<s.camera.tileWidth<<",0,"<<before<<','<<after<<','
            <<(before==after&&hashError.empty()?"PASS":"FAIL")<<'\n';hashes.flush();
        if(before!=after||!hashError.empty())return fatal("cache mutation queries changed authority: "+hashError);
        return true;
    };
    {
        const Scene plain{"cache_height_before",0,false,false,false};
        auto box=make(plain);auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        cameraFor(s,plain,60,0);const POINT click=centerFor(s,plain);
        const auto* originalData=s.world.getTerrainCells().data();std::uint32_t color=0;
        if(!probeCachedPoint(s,plain,click,goal,color))return false;
        for(auto& cell:s.world.accessTerrainCells())cell.level=12;
        initializeRuntimeNavigation(s);
        if(s.world.getTerrainCells().data()!=originalData)return fatal("height fixture replaced the terrain buffer");
        const Scene raised{"cache_height_after",12,false,false,false};
        // With camera and pixel fixed, a +12 level plane moves the visible
        // world location +6 cells on each axis by the diamond geometry.
        if(!probeCachedPoint(s,raised,click,{38,38},color))return false;
        const auto& layer=s.renderCache.terrainLayer;
        const int lx=click.x-viewport.left+s.camera.worldOffsetX-layer.originX;
        const int ly=click.y-viewport.top+s.camera.worldOffsetY-layer.originY;
        const auto ownerIndex=std::size_t(ly)*layer.width+lx;
        if(lx<0||ly<0||lx>=layer.width||ly>=layer.height||ownerIndex>=layer.terrainCellIndices.size()
            ||layer.terrainCellIndices[ownerIndex]!=38*512+38)
            return fatal("raised plane rendered TMP owner disagrees with independent fixed-pixel geometry");
        ++freshnessCases;
    }
    {
        const Scene bridge{"cache_bridge_intact",0,true,false,false};
        auto box=make(bridge);auto& s=*box;RuntimeSnapshotActiveStateScope scope(s);
        cameraFor(s,bridge,60,0);const POINT click=centerFor(s,bridge);
        const auto* originalData=s.world.getTerrainCells().data();
        std::vector<std::pair<std::size_t,TerrainCell>> originalDeck;
        std::vector<RuntimeCellPoint> changed;
        for(std::size_t i=0;i<s.world.getTerrainCells().size();++i){
            const auto& cell=s.world.getTerrainCells()[i];if(!cell.hasOverlay)continue;
            originalDeck.emplace_back(i,cell);changed.push_back({cell.x,cell.y});
        }
        std::uint32_t intactPixel=0,gapPixel=0,repairedPixel=0;
        if(!probeCachedPoint(s,bridge,click,goal,intactPixel))return false;
        for(const auto& [index,original]:originalDeck){
            (void)original;auto& cell=s.world.accessTerrainCells()[index];
            cell.hasOverlay=false;cell.overlayIndex=-1;cell.overlayFrame=-1;cell.overlayType.clear();cell.overlayImage.clear();
        }
        refreshRuntimeBridgeMutationState(s,changed);
        const Scene gap{"cache_bridge_gap",0,false,false,false};
        // The fixed deck pixel exposes the base plane two cells behind both
        // axes once its four-level bridge overlay has been removed.
        if(!probeCachedPoint(s,gap,click,{30,30},gapPixel))return false;
        for(const auto& [index,original]:originalDeck)s.world.accessTerrainCells()[index]=original;
        refreshRuntimeBridgeMutationState(s,changed);
        const Scene repaired{"cache_bridge_restored",0,true,false,false};
        if(!probeCachedPoint(s,repaired,click,goal,repairedPixel))return false;
        if(s.world.getTerrainCells().data()!=originalData||intactPixel==gapPixel||intactPixel!=repairedPixel)
            return fatal("bridge cache fixture did not expose and restore the actual visible deck pixel");
        ++freshnessCases;
    }
    pixels.flush();
    std::cout<<"Screen destination: pixels="<<probes<<" pixelFailures="<<pixelFailures
        <<" uiCommands="<<commands<<" commandFailures="<<commandFailures<<'\n';
    std::cout<<"Screen destination: same-buffer surface freshness cases="<<freshnessCases<<'\n';
    if(pixelFailures||commandFailures)return fatal(firstFailure);
    error.clear();return true;
}
