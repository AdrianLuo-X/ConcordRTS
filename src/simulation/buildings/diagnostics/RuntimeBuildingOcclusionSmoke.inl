// Geometrically unambiguous, disjoint original foundations. This contract
// supplies an independent near/far order; production sorting never supplies it.
bool runRuntimeBuildingOcclusionSmoke(const RuntimeConfig& config,std::string& error) {
    std::cout<<std::unitbuf;
    const char* requested=std::getenv("CONCORD_BUILDING_OCCLUSION_OUTPUT");
    const std::filesystem::path root=requested&&*requested?requested:
        "build/smoke/feature-set-19-complete/03-building-occlusion/checks";
    std::filesystem::create_directories(root);
    auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
    s.config=config;s.config.playerSlots.clear();s.config.gameModeRulesOverride.clear();
    s.config.computerAiEnabled=s.config.multiplayerActive=s.config.crates=s.config.shortGame=false;
    s.report=buildRuntimeStartupReport(s.config,&s.world,&s.theater,&s.art,&s.rules,&s.mapFacts);
    if(!s.report.success){error="Building occlusion: original startup failed";return false;}
    s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;
    s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
    s.economy.owner="Americans";s.economy.skirmishStartApplied=true;s.triggers.loaded=s.triggers.bootstrapComplete=true;
    RuntimeHumanHouseAuthority authority;authority.owner="Americans";authority.canonicalOwner="americans";authority.localFacade=true;
    s.humanHouseAuthorities.push_back(authority);s.ownerRemapColors["Americans"]=RGB(235,210,45);
    s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=-1;
    s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
    s.world.accessTerrainCells().clear();
    for(int y=10;y<=36;++y)for(int x=10;x<=36;++x){TerrainCell cell;
        cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;cell.tileIndex=cell.subTileIndex=0;s.world.accessTerrainCells().push_back(cell);}
    struct Body {const char* type;int x,y,width,height;};
    struct Scene {const char* name;std::vector<Body> rearToFront;};
    const std::vector<Scene> scenes{
        {"refinery-south",{{"GAREFN",20,20,4,3},{"GAPOWR",20,23,2,2}}},
        {"refinery-south-left",{{"GAREFN",20,20,4,3},{"GAPOWR",19,23,2,2}}},
        {"barracks-south-left",{{"GAPILE",20,20,3,2},{"GAPOWR",19,22,2,2}}},
        {"refinery-east-control",{{"GAREFN",20,20,4,3},{"GAPOWR",24,20,2,2}}},
        {"barracks-east-control",{{"GAPILE",20,20,3,2},{"GAPOWR",23,20,2,2}}},
        {"three-stock-buildings",{{"GAPILE",17,20,3,2},{"GAREFN",20,20,4,3},{"GAPOWR",20,23,2,2}}}
    };
    constexpr int width=640,height=480;const RECT viewport{0,0,width,height};
    struct Surface {
        HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;std::uint32_t* pixels=nullptr;
        Surface(){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
            dc=CreateCompatibleDC(nullptr);void* raw=nullptr;if(dc)bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
            pixels=static_cast<std::uint32_t*>(raw);if(bitmap&&pixels)previous=SelectObject(dc,bitmap);}
        ~Surface(){if(previous&&dc)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    } surface;
    if(!surface.dc||!surface.pixels){error="Building occlusion: DIB allocation failed";return false;}
    RuntimeMapRenderContext view;view.rules=&s.rules;view.art=&s.art;view.theater=&s.theater;
    view.assetRootPath=runtimeAssetRootPath(s.config);view.structureRootPath=runtimeStructureRootPath(s.config,s.report.map);
    view.palettePath=runtimePalettePath(s.config,s.report.map);view.structurePalettePath=runtimeStructurePalettePath(s.config,s.report.map);
    view.ownerRemapColors=&s.ownerRemapColors;view.fixedTickHz=s.config.fixedTickHz;view.animationTick=17;
    view.useTileTextures=view.shadowsEnabled=false;view.vision=nullptr;view.mapFacts=nullptr;
    view.surfacePixels=surface.pixels;view.surfaceWidth=width;view.surfaceHeight=height;
    auto cache=std::make_unique<RuntimeMapRenderCache>();GameWorld renderWorld=s.world;
    std::ofstream rows(root/"building-occlusion.csv");
    rows<<"scene,zoom,level,id_reverse,insertion_reverse,mutation,geometry_edges,pixel_mismatch,reverse_witness,swap_rear_witness,swap_front_witness,overlap_pixels,wrong_picks,before_paint_wrong_picks,cache_equal,pause_equal,coverage,result\n";
    std::ofstream coverageRows(root/"scene-coverage.csv");
    coverageRows<<"scene,reverse_witness,swap_rear_witness,swap_front_witness,overlap_cases,nonoverlap_controls,result\n";
    std::ofstream failuresFile(root/"failures.csv");failuresFile<<"scene,reason\n";
    int failures=0,checks=0,hdcChecks=0;std::uint64_t overlapClicks=0,beforePaintClicks=0;std::string first;
    const auto fail=[&](const std::string& label,const std::string& reason){++failures;if(first.empty())first=label+": "+reason;
        failuresFile<<label<<','<<reason<<'\n';std::cout<<"Building occlusion FAIL "<<label<<": "<<reason<<'\n';};
    const auto difference=[](const auto& a,const auto& b){int count=0;for(std::size_t i=0;i<a.size();++i)
        count+=(a[i]&0xffffffU)!=(b[i]&0xffffffU);return count;};
    for(const auto& scene:scenes){
        std::uint64_t sceneReverseWitness=0,sceneRearWitness=0,sceneFrontWitness=0;
        int sceneOverlapCases=0,sceneNonoverlapControls=0;
        // Rectangle separation proves frontness without projected Y, an ID,
        // far-corner sum, a renderer comparator, or any assumed paint order.
        std::vector<std::vector<bool>> before(scene.rearToFront.size(),std::vector<bool>(scene.rearToFront.size()));
        int edges=0;
        for(std::size_t a=0;a<scene.rearToFront.size();++a)for(std::size_t b=a+1;b<scene.rearToFront.size();++b){
            const auto& rear=scene.rearToFront[a];const auto& front=scene.rearToFront[b];
            const bool overlapX=rear.x<front.x+front.width&&front.x<rear.x+rear.width;
            const bool overlapY=rear.y<front.y+front.height&&front.y<rear.y+rear.height;
            const bool separateX=rear.x+rear.width<=front.x,separateY=rear.y+rear.height<=front.y;
            before[a][b]=(separateX&&overlapY)||(separateY&&overlapX)||(separateX&&separateY);
            if(overlapX&&overlapY){error=std::string(scene.name)+" invalid overlapping foundations";return false;}
            if(before[a][b])++edges;
        }
        for(std::size_t k=0;k<before.size();++k)for(std::size_t a=0;a<before.size();++a)for(std::size_t b=0;b<before.size();++b)
            before[a][b]=before[a][b]||(before[a][k]&&before[k][b]);
        for(std::size_t a=0;a<before.size();++a)for(std::size_t b=a+1;b<before.size();++b)
            if(!before[a][b]){error=std::string(scene.name)+" lacks an independent separating-axis order";return false;}
        for(int zoom:{48,60,72})for(int level:{0,4})for(bool reverseIds:{false,true})for(bool reverseInsertion:{false,true})
            for(int mutation=0;mutation<((std::string(scene.name)=="refinery-south"&&zoom==60&&level==0
                &&!reverseIds&&!reverseInsertion)?4:1);++mutation){
            const int beforeFailures=failures;
            const std::string label=std::string(scene.name)+"-"+std::to_string(zoom)+"-z"+std::to_string(level)
                +"-id"+std::to_string(reverseIds)+"-order"+std::to_string(reverseInsertion)
                +(mutation?"-mutation"+std::to_string(mutation):"");
            std::vector<WorldObjectInstance> ordered;
            for(std::size_t i=0;i<scene.rearToFront.size();++i){const auto& body=scene.rearToFront[i];
                const auto* type=s.rules.findBuildingType(body.type);const auto* art=s.art.findArtType(body.type);
                if(!type||!art||art->foundation!=std::to_string(body.width)+"x"+std::to_string(body.height)||art->height!=4){
                    error=label+" stock foundation/height changed";return false;}
                WorldObjectInstance object;object.instanceId=100+static_cast<int>(reverseIds?scene.rearToFront.size()-i:i);
                object.kind=WorldObjectKind::Structure;object.type=body.type;object.owner="Americans";
                object.x=body.x+(mutation==3?1:0);object.y=body.y+(mutation==3?1:0);
                object.cell=runtimeCellKey(object.x,object.y);object.level=level;
                object.alive=object.poweredOperational=true;object.health=type->strength;object.mission="Guard";
                ordered.push_back(object);
            }
            s.world.accessObjects()=ordered;if(reverseInsertion)std::reverse(s.world.accessObjects().begin(),s.world.accessObjects().end());
            for(auto& cell:s.world.accessTerrainCells())cell.level=level;
            initializeRuntimeNavigation(s);s.structureFootprints=buildRuntimeStructureFootprints(s);
            s.tick=mutation==2?41:17;view.animationTick=s.tick;
            s.selectedObjectIds.clear();s.camera.initialized=true;s.camera.tileWidth=zoom;s.camera.tileHeight=zoom/2;
            const auto pivot=runtimeProjectCellXY(20,22,level,s.camera);
            s.camera.worldOffsetX=pivot.x-width/2+(mutation==1?37:0);
            s.camera.worldOffsetY=pivot.y-height/2-(mutation==1?23:0);
            RuntimeSnapshotActiveStateScope scope(s);
            // Let production camera limits and snapshot setup settle before
            // constructing image coordinates. No hand-sorted picking vector.
            (void)runtimeProjectedObjectsForViewport(s,viewport);
            const auto& projected=runtimeProjectedObjectsForViewport(s,viewport);
            const auto signature=s.projectedObjectsCacheSignature;const auto* data=projected.data();
            const auto& warmed=runtimeProjectedObjectsForViewport(s,viewport);
            const bool cacheEqual=signature==s.projectedObjectsCacheSignature&&data==warmed.data();
            if(!cacheEqual||projected.size()!=ordered.size())fail(label,"production projection count/cache reuse");
            // Exercise the actual picker before any full draw of this state.
            // Save intersections now and consult the independent masks later;
            // otherwise a post-paint picker could accidentally pass by using
            // the previous paint's mutable ownership buffer.
            std::vector<int> beforePaintOwners(width*height,-2);
            for(std::size_t a=0;a<warmed.size();++a)for(std::size_t b=a+1;b<warmed.size();++b){
                RECT intersection{},clipped{};
                if(!IntersectRect(&intersection,&warmed[a].spriteBounds,&warmed[b].spriteBounds)
                    ||!IntersectRect(&clipped,&intersection,&viewport))continue;
                for(int y=clipped.top;y<clipped.bottom;++y)for(int x=clipped.left;x<clipped.right;++x){
                    auto& saved=beforePaintOwners[static_cast<std::size_t>(y)*width+x];
                    if(saved!=-2)continue;
                    const auto* hit=findProjectedObjectAtPoint(warmed,POINT{static_cast<LONG>(x),static_cast<LONG>(y)});
                    saved=hit&&hit->object?hit->object->instanceId:-1;++beforePaintClicks;
                }
            }
            renderWorld=s.world;
            const auto full=[&](const std::vector<WorldObjectInstance>& objects,std::vector<std::uint32_t>& pixels){
                renderWorld.accessObjects()=objects;auto camera=s.camera;
                const auto stats=drawRuntimeMap(surface.dc,viewport,renderWorld,view,camera,*cache);GdiFlush();
                pixels.assign(surface.pixels,surface.pixels+width*height);return stats;};
            std::vector<std::uint32_t> blank,actual,paused;
            const auto emptyStats=full({},blank);
            std::vector<std::vector<unsigned char>> masks(ordered.size(),std::vector<unsigned char>(width*height));
            for(std::size_t i=0;i<ordered.size();++i){
                bool drew=true;
                for(std::uint32_t background:{0x00000000U,0x00ffffffU}){
                    std::fill_n(surface.pixels,width*height,background);
                    drew=drawRuntimeObjectPreviewSprite(surface.dc,viewport,ordered[i],view,s.camera,*cache)&&drew;GdiFlush();
                    for(std::size_t p=0;p<masks[i].size();++p)if((surface.pixels[p]&0xffffffU)!=background)masks[i][p]=1;
                }
                if(!drew)fail(label,"original body/animation resource unavailable");
                std::vector<std::uint32_t> solo;
                const auto soloStats=full({ordered[i]},solo);
                std::copy(blank.begin(),blank.end(),surface.pixels);
                drawRuntimeObjectPreviewSprite(surface.dc,viewport,ordered[i],view,s.camera,*cache);GdiFlush();
                const std::vector<std::uint32_t> preview(surface.pixels,surface.pixels+width*height);
                if(!soloStats.rendered||difference(solo,preview))fail(label,"isolated original preview/full-render calibration differs");
            }
            const auto composite=[&](int permutation){std::copy(blank.begin(),blank.end(),surface.pixels);
                std::vector<std::size_t> order;for(std::size_t i=0;i<ordered.size();++i)order.push_back(i);
                if(permutation==1)std::reverse(order.begin(),order.end());
                else if(permutation==2)std::swap(order[0],order[1]);
                else if(permutation==3)std::swap(order[order.size()-2],order.back());
                for(const std::size_t i:order){
                    drawRuntimeObjectPreviewSprite(surface.dc,viewport,ordered[i],view,s.camera,*cache);}
                GdiFlush();return std::vector<std::uint32_t>(surface.pixels,surface.pixels+width*height);};
            const auto expected=composite(0),wrong=composite(1);
            const auto wrongRear=composite(2),wrongFront=composite(3);
            const auto actualStats=full(s.world.getObjects(),actual),repeatStats=full(s.world.getObjects(),paused);
            const int mismatch=difference(actual,expected),reverseWitness=difference(expected,wrong);
            const int rearWitness=difference(expected,wrongRear),frontWitness=difference(expected,wrongFront);
            sceneReverseWitness+=reverseWitness;sceneRearWitness+=rearWitness;sceneFrontWitness+=frontWitness;
            const bool nonoverlapControl=reverseWitness==0;
            if(nonoverlapControl)++sceneNonoverlapControls;else ++sceneOverlapCases;
            int overlaps=0,wrongPicks=0,beforePaintWrongPicks=0;
            for(std::size_t p=0;p<actual.size();++p){int count=0,front=-1;
                for(std::size_t i=0;i<masks.size();++i)if(masks[i][p]){++count;front=static_cast<int>(i);}
                if(count<2||(expected[p]&0xffffffU)==(wrong[p]&0xffffffU))continue;
                ++overlaps;++overlapClicks;const POINT point{static_cast<LONG>(p%width),static_cast<LONG>(p/width)};
                const auto* hit=findProjectedObjectAtPoint(warmed,point);
                if(!hit||!hit->object||hit->object->instanceId!=ordered[front].instanceId)++wrongPicks;
                if(beforePaintOwners[p]!=ordered[front].instanceId)++beforePaintWrongPicks;
            }
            if(!emptyStats.rendered||!actualStats.rendered||!repeatStats.rendered||actual!=paused)
                fail(label,"full production draw/pause failed");
            if(zoom==60&&level==0&&!reverseIds&&!reverseInsertion&&mutation==0) {
                auto hdcView=view;hdcView.surfacePixels=nullptr;
                hdcView.surfaceWidth=hdcView.surfaceHeight=0;auto hdcCamera=s.camera;
                const auto hdcStats=drawRuntimeMap(surface.dc,viewport,renderWorld,hdcView,hdcCamera,*cache);GdiFlush();
                const std::vector<std::uint32_t> hdcPixels(surface.pixels,surface.pixels+width*height);
                if(!hdcStats.rendered||difference(hdcPixels,actual))fail(label,"HDC and pixel-surface output differs");
                ++hdcChecks;
            }
            // Native sprites keep their authored pixel size while cell spacing
            // changes with zoom. A separated image is a valid control, not a
            // missing-resource failure. Each adjacent relation must instead
            // have a genuine original-pixel witness somewhere across zooms.
            const bool invalidWitness=nonoverlapControl?(rearWitness!=0||frontWitness!=0||overlaps!=0):overlaps==0;
            const bool missingCoreWitness=std::string(scene.name)=="refinery-south"&&zoom==60&&nonoverlapControl;
            if(mismatch||wrongPicks||beforePaintWrongPicks||invalidWitness||missingCoreWitness)
                fail(label,"pixels="+std::to_string(mismatch)+" reverseWitness="+std::to_string(reverseWitness)
                    +" rearWitness="+std::to_string(rearWitness)+" frontWitness="+std::to_string(frontWitness)
                    +" overlap="+std::to_string(overlaps)+" wrongPicks="+std::to_string(wrongPicks)
                    +" beforePaintWrongPicks="+std::to_string(beforePaintWrongPicks));
            if(!reverseIds&&!reverseInsertion){std::vector<std::uint32_t> sheet(width*3*height);
                for(int column=0;column<3;++column){const auto& frame=column==0?actual:column==1?expected:wrong;
                    for(int y=0;y<height;++y)std::copy_n(frame.begin()+y*width,width,sheet.begin()+y*width*3+column*width);}
                std::string detail;
                if(!writeRuntimeSmokeBmp((root/(label+".bmp")).string(),sheet.data(),width*3,height,detail))fail(label,detail);
            }
            rows<<scene.name<<','<<zoom<<','<<level<<','<<reverseIds<<','<<reverseInsertion<<','<<mutation<<','<<edges<<','<<mismatch
                <<','<<reverseWitness<<','<<rearWitness<<','<<frontWitness<<','<<overlaps<<','<<wrongPicks<<','<<beforePaintWrongPicks
                <<','<<cacheEqual<<','<<(actual==paused)
                <<','<<(nonoverlapControl?"nonoverlap-control":"native-overlap")
                <<','<<(beforeFailures==failures?"PASS":"FAIL")<<'\n';rows.flush();++checks;
        }
        const bool coverageComplete=sceneReverseWitness>0&&sceneRearWitness>0&&sceneFrontWitness>0&&sceneOverlapCases>0;
        if(!coverageComplete)fail(scene.name,"no original-pixel witness across zooms for one or more adjacent relations");
        coverageRows<<scene.name<<','<<sceneReverseWitness<<','<<sceneRearWitness<<','<<sceneFrontWitness
            <<','<<sceneOverlapCases<<','<<sceneNonoverlapControls<<','<<(coverageComplete?"PASS":"FAIL")<<'\n';
        coverageRows.flush();
    }
    #include "simulation/buildings/diagnostics/RuntimeBuildingMixedDepthSmoke.inl"
    #include "simulation/buildings/diagnostics/RuntimeBuildingTransparencySmoke.inl"
    #include "simulation/buildings/diagnostics/RuntimeBuildingFrozenDepthSmoke.inl"
    std::cout<<"Building occlusion: cases="<<checks<<" nativeOverlapClicks="<<overlapClicks
        <<" beforePaintClicks="<<beforePaintClicks<<" hdcChecks="<<hdcChecks<<" failures="<<failures<<'\n';
    if(failures){error="Building occlusion: "+first;return false;}error.clear();return checks>0&&overlapClicks>0;
}
