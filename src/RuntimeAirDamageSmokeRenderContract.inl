bool runRuntimeAirDamageSmokeRenderContractCheck(const RuntimeMapRenderContext& context,
    const std::vector<WorldObjectInstance>& poses,const std::vector<RuntimeParticlePoint>& origins,
    const std::vector<std::vector<RuntimeParticleRenderSnapshot>>& smoke,
    const std::vector<std::string>& labels,std::string& error) {
    if(poses.empty()||origins.size()!=poses.size()||smoke.size()!=poses.size()||labels.size()!=poses.size()){
        error="Air smoke render: incomplete inputs";return false;}
    const char* output=std::getenv("CONCORD_AIR_DAMAGE_SMOKE_OUTPUT");
    const std::filesystem::path root=output&&*output?output:"build/smoke/feature-set-19/02-air-damage-smoke/checks";
    std::filesystem::create_directories(root);
    std::ofstream rows(root/"render-anchors.csv");
    rows<<"label,tile_width,body_x,body_y,expected_x,expected_y,actual_x,actual_y,pixel_mismatch,body_drawn,result\n";
    auto cache=std::make_unique<RuntimeMapRenderCache>();auto probe=context;probe.vision=nullptr;probe.shadowsEnabled=false;
    constexpr int width=320,height=240,columns=3;
    constexpr std::uint32_t background=0x00363d44U;
    const RECT viewport{0,0,width,height};
    std::vector<std::uint32_t> pixels(width*height,background);
    probe.surfaceWidth=width;probe.surfaceHeight=height;probe.surfacePixels=pixels.data();
    int checked=0,failures=0,images=0,resourcesMissing=0,fullScenes=0,fullPictures=0;std::string firstFailure;
    const auto failure=[&](const std::string& why){++failures;if(firstFailure.empty())firstFailure=why;
        std::cout<<"Air smoke render FAIL "<<why<<'\n';};
    // A loaded, empty world retains all real map-render passes without needing
    // substitute terrain art or a copied map. Only stock body and smoke assets
    // are added to it below.
    IniFile mapIni;MapDatabase map;GameWorld sceneWorld;
    if(!mapIni.loadFromString("[Header]\nWidth=16\nHeight=16\n[Map]\nTheater=TEMPERATE\nSize=0,0,16,16\nLocalSize=0,0,16,16\n")
        ||!map.loadFromIni(mapIni)||!sceneWorld.loadFromMap(map,context.rules)){
        error="Air smoke render: full scene world failed";return false;}
    struct SceneSurface{
        HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;std::uint32_t* pixels=nullptr;
        SceneSurface(){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
            dc=CreateCompatibleDC(nullptr);void* raw=nullptr;if(dc)bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
            pixels=static_cast<std::uint32_t*>(raw);if(bitmap&&pixels)previous=SelectObject(dc,bitmap);}
        ~SceneSurface(){if(previous&&dc)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
    } surface;
    if(!surface.dc||!surface.bitmap||!surface.pixels){error="Air smoke render: full scene surface failed";return false;}
    auto sceneContext=context;sceneContext.vision=nullptr;sceneContext.mapFacts=nullptr;
    sceneContext.useTileTextures=false;sceneContext.effectOnlyProbe=false;sceneContext.shadowsEnabled=false;
    sceneContext.surfacePixels=surface.pixels;sceneContext.surfaceWidth=width;sceneContext.surfaceHeight=height;
    auto sceneCache=std::make_unique<RuntimeMapRenderCache>();
    std::ofstream sceneRows(root/"full-scenes.csv");
    sceneRows<<"label,tile_width,body_pixels,visible_smoke_pixels,smoke_shp,airborne_bodies,pause_equal,result\n";
    for(std::size_t index=0;index<poses.size();++index){
        const auto& object=poses[index];const auto& origin=origins[index];
        const bool pictured=object.type=="DISK"||object.type=="ZEP"||object.type=="SHAD"||object.type=="SCHP"
            ||object.type=="ORCA"||object.type=="BEAG"||object.type=="MTNK"||object.type=="DEST";
        const auto* unit=context.rules?context.rules->findUnitType(object.kind==WorldObjectKind::Infantry?
            "Infantry":object.authoredAircraft?"Aircraft":"Vehicle",object.type):nullptr;
        if(!unit||smoke[index].empty()){failure(labels[index]+" missing rules or smoke");continue;}
        std::array<int,3> offset=unit->damageSmokeOffset;
        // Ground tank outlet is an established independent contract. Every
        // aircraft/ship/custom outlet retains its explicitly authored offset.
        if(equalAsciiNoCase(unit->category,"AFV")&&!unit->naval&&!unit->jumpJet&&!unit->balloonHover
            &&(offset==std::array<int,3>{0,0,0}||offset==std::array<int,3>{100,100,275}))offset={0,0,256};
        std::vector<std::uint32_t> sheet(width*columns*height*3,background);
        std::vector<std::uint32_t> fullSheet;
        if(pictured)fullSheet.resize(width*columns*height*3);
        int column=0;
        for(int tileWidth:{48,60,72}){
            RuntimeMapCamera camera;camera.initialized=true;camera.tileWidth=tileWidth;camera.tileHeight=tileWidth/2;
            const auto uncentered=runtimeObjectBodyAnchorForRender(object,viewport,probe,camera);
            camera.worldOffsetX=uncentered.x-width/2;camera.worldOffsetY=uncentered.y-155;
            const POINT body=runtimeObjectBodyAnchorForRender(object,viewport,probe,camera);
            // This expectation starts at the actual independent body renderer,
            // then adds the authored offset in world axes. It never calls the
            // smoke-origin or smoke-render-position implementation.
            const double x=object.hasRenderPosition?object.renderX:double(object.x);
            const double y=object.hasRenderPosition?object.renderY:double(object.y);
            const double z=object.renderMoveActive||(object.factoryExitHostId>=0&&object.hasRenderPosition)?object.renderLevel:double(object.level);
            const auto base=projectCellXY(x,y,z,camera);
            const auto outlet=projectCellXY(x+offset[0]/256.0,y+offset[1]/256.0,z+offset[2]/256.0,camera);
            const POINT expected{body.x+outlet.x-base.x,body.y+outlet.y-base.y};
            auto sample=smoke[index].front();sample.rawX=origin.x;sample.rawY=origin.y;sample.rawZ=origin.z;
            sample.worldX=origin.x/256.0;sample.worldY=origin.y/256.0;sample.worldLevel=origin.z/256.0;
            sample.frameIndex=0;
            const auto source=projectCellXY(sample.worldX,sample.worldY,sample.worldLevel,camera);
            const POINT projected=screenPoint(source,viewport,camera);
            const POINT actual{projected.x,projected.y+sample.screenOffsetY};
            std::fill(pixels.begin(),pixels.end(),background);
            std::vector<RuntimeParticleRenderSnapshot> sourceOnly{sample};probe.particleSnapshots=&sourceOnly;
            RuntimeMapRenderStats actualStats;drawRuntimeParticleSnapshots(nullptr,viewport,probe,camera,*cache,actualStats);
            const auto actualPixels=pixels;
            std::fill(pixels.begin(),pixels.end(),background);
            const auto* palette=runtimePaletteForAuxiliaryEffectSprite(probe,*cache,sample.imageId,RuntimeAuxiliaryEffectKind::Impact);
            const int opacity=std::clamp(((100-std::clamp(sample.translucency,0,100))*255+50)/100,0,255);
            const bool expectedDrawn=palette&&drawRuntimeAuxiliaryShpSpriteWithPalette(nullptr,viewport,expected,probe,camera,*cache,
                sample.imageId,0,*palette,0,0,false,false,RGB(255,255,255),opacity,true);
            int mismatch=0;for(std::size_t p=0;p<pixels.size();++p)if(pixels[p]!=actualPixels[p])++mismatch;
            const bool valid=actual.x==expected.x&&actual.y==expected.y&&mismatch==0&&expectedDrawn
                &&actualStats.particleActorShpDrawn==1&&actualStats.particleActorFallbackDrawn==0;
            ++checked;if(!valid)failure(labels[index]+" tile="+std::to_string(tileWidth)+" anchor="
                +std::to_string(actual.x-expected.x)+","+std::to_string(actual.y-expected.y)+" pixels="+std::to_string(mismatch));
            std::fill(pixels.begin(),pixels.end(),background);probe.particleSnapshots=nullptr;
            const bool bodyDrawn=tryDrawVoxelSprite(nullptr,viewport,object,body,probe,camera,*cache)
                ||tryDrawObjectShpSprite(nullptr,viewport,object,body,probe,camera,*cache);
            if(!bodyDrawn){++resourcesMissing;if(pictured)failure(labels[index]+" representative body resource missing");}
            rows<<labels[index]<<','<<tileWidth<<','<<body.x<<','<<body.y<<','<<expected.x<<','<<expected.y
                <<','<<actual.x<<','<<actual.y<<','<<mismatch<<','<<bodyDrawn<<','<<(valid?"PASS":"FAIL")<<'\n';
            if(pictured){
                const auto bodyPixels=pixels;
                const auto copy=[&](int row){for(int y0=0;y0<height;++y0)std::copy_n(pixels.begin()+y0*width,width,
                    sheet.begin()+(row*height+y0)*width*columns+column*width);};
                copy(0);probe.particleSnapshots=&smoke[index];RuntimeMapRenderStats stats;
                drawRuntimeParticleSnapshots(nullptr,viewport,probe,camera,*cache,stats);copy(1);
                pixels=bodyPixels;probe.surfacePixels=pixels.data();probe.particleSnapshots=&sourceOnly;
                RuntimeMapRenderStats sourceStats;drawRuntimeParticleSnapshots(nullptr,viewport,probe,camera,*cache,sourceStats);
                const auto mark=[&](POINT point,std::uint32_t color){for(int d=-4;d<=4;++d){
                    if(point.x+d>=0&&point.x+d<width&&point.y>=0&&point.y<height)pixels[point.y*width+point.x+d]=color;
                    if(point.y+d>=0&&point.y+d<height&&point.x>=0&&point.x<width)pixels[(point.y+d)*width+point.x]=color;}};
                mark(expected,0x0000ff00U);if(actual.x!=expected.x||actual.y!=expected.y)mark(actual,0x00ff4040U);copy(2);
                probe.particleSnapshots=nullptr;
                // Unlike the isolated placement oracle above, this is the
                // complete production renderer: ground particles and the later
                // airborne-body pass keep their real compositing order.
                auto sceneCamera=camera;sceneContext.animationTick=30;
                const auto renderScene=[&](bool bodyVisible,bool smokeVisible,std::vector<std::uint32_t>& frame){
                    sceneWorld.accessObjects().clear();if(bodyVisible)sceneWorld.accessObjects().push_back(object);
                    sceneContext.particleSnapshots=smokeVisible?&smoke[index]:nullptr;
                    const auto result=drawRuntimeMap(surface.dc,viewport,sceneWorld,sceneContext,sceneCamera,*sceneCache);
                    GdiFlush();frame.assign(surface.pixels,surface.pixels+width*height);return result;};
                std::vector<std::uint32_t> blank,bodyOnly,full,repeat;
                const auto blankStats=renderScene(false,false,blank);
                const auto bodyStats=renderScene(true,false,bodyOnly);
                const auto fullStats=renderScene(true,true,full);
                const auto repeatStats=renderScene(true,true,repeat);
                int bodyPixelCount=0,smokePixels=0;
                for(std::size_t p=0;p<full.size();++p){
                    bodyPixelCount+=(bodyOnly[p]&0xffffffU)!=(blank[p]&0xffffffU);
                    smokePixels+=(full[p]&0xffffffU)!=(bodyOnly[p]&0xffffffU);}
                const bool sceneValid=blankStats.rendered&&bodyStats.rendered&&fullStats.rendered&&repeatStats.rendered
                    &&bodyPixelCount>20&&smokePixels>0
                    &&fullStats.particleActorShpDrawn==static_cast<int>(smoke[index].size())
                    &&fullStats.particleActorFallbackDrawn==0&&fullStats.particleActorMissing==0&&full==repeat;
                ++fullScenes;if(!sceneValid)failure(labels[index]+" full scene tile="+std::to_string(tileWidth)
                    +" bodyPixels="+std::to_string(bodyPixelCount)+" visibleSmoke="+std::to_string(smokePixels)
                    +" shp="+std::to_string(fullStats.particleActorShpDrawn)+" pauseEqual="+std::to_string(full==repeat));
                sceneRows<<labels[index]<<','<<tileWidth<<','<<bodyPixelCount<<','<<smokePixels<<','<<fullStats.particleActorShpDrawn
                    <<','<<fullStats.airborneForegroundBodiesDrawn<<','<<(full==repeat)<<','<<(sceneValid?"PASS":"FAIL")<<'\n';
                for(int row=0;row<3;++row){const auto& frame=row==0?bodyOnly:row==1?full:repeat;
                    for(int y0=0;y0<height;++y0)std::copy_n(frame.begin()+y0*width,width,
                        fullSheet.begin()+(row*height+y0)*width*columns+column*width);}
            }
            ++column;
        }
        if(pictured){const auto path=root/(labels[index]+".bmp");
            if(!writeRuntimeBgraBmp(path.string(),width*columns,height*3,sheet))failure(labels[index]+" image write");else ++images;
            if(!writeRuntimeBgraBmp(root/(labels[index]+"-full-scene.bmp"),width*columns,height*3,fullSheet))
                failure(labels[index]+" full scene image write");else ++fullPictures;}
    }
    // A small overlap fixture checks the shared air-layer records and the real
    // compositing path. Its reference is explicitly rear body, air puff, front
    // body; neither the ordering helper nor its comparator builds that oracle.
    const auto airLabel=std::find(labels.begin(),labels.end(),"SHAD-30-idle");
    const auto groundLabel=std::find(labels.begin(),labels.end(),"MTNK-30-idle");
    if(airLabel==labels.end()||groundLabel==labels.end())failure("depth fixture stock smoke missing");
    else{
        const std::size_t ai=static_cast<std::size_t>(airLabel-labels.begin());
        const std::size_t gi=static_cast<std::size_t>(groundLabel-labels.begin());
        if(smoke[ai].empty()||smoke[gi].empty())failure("depth fixture puffs missing");
        else{
            const auto airPuff=smoke[ai].front(),groundPuff=smoke[gi].front();
            if(!airPuff.airborneDamageSmoke||groundPuff.airborneDamageSmoke)failure("depth fixture emission layers");
            auto rear=poses[ai],front=poses[ai];rear.instanceId=101;front.instanceId=102;
            for(auto* body:{&rear,&front}){
                const double delta=body==&rear?-0.35:0.35;
                body->hasRenderPosition=body->renderMoveActive=body->airborne=true;
                body->renderX=airPuff.worldX+delta;body->renderY=airPuff.worldY+delta;
                // The nearer plane is 1.5 levels higher: its projected Y is
                // behind the rear plane although its world-depth key is ahead.
                body->renderLevel=airPuff.worldLevel+(body==&front?1.5:0.0);
                body->level=body->flightLevel=static_cast<int>(std::floor(body->renderLevel));
                body->x=static_cast<int>(std::lround(body->renderX));body->y=static_cast<int>(std::lround(body->renderY));
                body->cell=body->y*512+body->x;body->mission="Guard";
            }
            RuntimeMapCamera camera;camera.initialized=true;camera.tileWidth=60;camera.tileHeight=30;
            const auto point=projectCellXY(airPuff.worldX,airPuff.worldY,airPuff.worldLevel,camera);
            camera.worldOffsetX=point.x-width/2;camera.worldOffsetY=point.y+airPuff.screenOffsetY-140;
            if(runtimeObjectSortPoint(front,{},camera).y>=runtimeObjectSortPoint(rear,{},camera).y)
                failure("depth fixture did not oppose projected Y and world depth");
            std::vector<RuntimeParticleRenderSnapshot> puffs{groundPuff,airPuff};
            std::vector<RuntimeWorldObjectDrawRecord> bodies;
            for(const auto* object:{&front,&rear}){RuntimeWorldObjectDrawRecord record;record.object=object;
                record.instanceId=object->instanceId;record.bodyVisible=record.airborneForeground=record.aircraft=true;
                record.drawLevel=object->renderLevel;record.cellSum=object->renderX+object->renderY;
                record.kindBias=objectKindSortBias(object->kind);record.sortPoint=runtimeObjectSortPoint(*object,{},camera);
                record.center=runtimeObjectBodyAnchorForRender(*object,viewport,sceneContext,camera);bodies.push_back(record);}
            const std::vector<RuntimeWaterWakeActor> noWakes;
            const auto groundRecords=buildRuntimeGroundBodyDrawRecords(bodies,noWakes,0,camera,nullptr,&sceneContext,&puffs);
            const auto airRecords=buildRuntimeAirborneBodyDrawRecords(bodies,groundRecords);
            if(airRecords.size()!=3||!airRecords[0].objectRecord||airRecords[0].objectRecord->instanceId!=101
                ||airRecords[1].particleSnapshot!=&puffs[1]||!airRecords[2].objectRecord||airRecords[2].objectRecord->instanceId!=102)
                failure("air layer did not interleave rear body / damage puff / front body");
            if(std::count_if(groundRecords.begin(),groundRecords.end(),[&](const auto& record){return record.particleSnapshot==&puffs[0];})!=1
                ||std::any_of(airRecords.begin(),airRecords.end(),[&](const auto& record){return record.particleSnapshot==&puffs[0];}))
                failure("ground smoke duplicated or migrated into air layer");
            const std::vector<RuntimeWorldObjectDrawRecord> noBodies;
            const auto orphanRecords=buildRuntimeAirborneBodyDrawRecords(noBodies,groundRecords);
            if(orphanRecords.size()!=1||orphanRecords[0].particleSnapshot!=&puffs[1])failure("orphan smoke lost frozen air layer");
            std::vector<std::uint32_t> actual,paused,expected,behindBoth,aheadBoth,orphan;
            const auto full=[&](bool objects,std::vector<std::uint32_t>& frame){
                sceneWorld.accessObjects().clear();if(objects)sceneWorld.accessObjects()={front,rear};
                sceneContext.particleSnapshots=&puffs;const auto stats=drawRuntimeMap(surface.dc,viewport,sceneWorld,sceneContext,camera,*sceneCache);
                GdiFlush();frame.assign(surface.pixels,surface.pixels+width*height);return stats;};
            const auto actualStats=full(true,actual);const auto pausedStats=full(true,paused);const auto orphanStats=full(false,orphan);
            if(actual!=paused||!actualStats.rendered||!pausedStats.rendered||!orphanStats.rendered
                ||actualStats.particleActorShpDrawn!=2||pausedStats.particleActorShpDrawn!=2||orphanStats.particleActorShpDrawn!=2
                ||actualStats.airborneForegroundBodiesDrawn!=2)failure("air/ground puff must draw exactly once, even without source; pause must match");
            const auto manual=[&](int order,std::vector<std::uint32_t>& frame){
                sceneWorld.accessObjects().clear();sceneContext.particleSnapshots=nullptr;
                (void)drawRuntimeMap(surface.dc,viewport,sceneWorld,sceneContext,camera,*sceneCache);GdiFlush();
                const auto puff=[&](const RuntimeParticleRenderSnapshot& snapshot){
                    auto projection=projectCellXY(snapshot.worldX,snapshot.worldY,snapshot.worldLevel,camera);
                    projection.y+=snapshot.screenOffsetY;RuntimeMapRenderStats stats;
                    drawRuntimeParticleSnapshot(surface.dc,viewport,sceneContext,camera,*sceneCache,snapshot,projection,stats);};
                const auto body=[&](const WorldObjectInstance& object){const auto center=runtimeObjectBodyAnchorForRender(object,viewport,sceneContext,camera);
                    if(!tryDrawVoxelSprite(surface.dc,viewport,object,center,sceneContext,camera,*sceneCache)
                        &&!tryDrawObjectShpSprite(surface.dc,viewport,object,center,sceneContext,camera,*sceneCache))failure("depth fixture body missing");};
                puff(groundPuff);if(order<0)puff(airPuff);body(rear);if(order==0)puff(airPuff);body(front);if(order>0)puff(airPuff);
                GdiFlush();frame.assign(surface.pixels,surface.pixels+width*height);
            };
            manual(0,expected);manual(-1,behindBoth);manual(1,aheadBoth);
            const auto different=[&](const auto& a,const auto& b){int count=0;for(std::size_t p=0;p<a.size();++p)
                count+=(a[p]&0xffffffU)!=(b[p]&0xffffffU);return count;};
            const int mismatch=different(actual,expected),behindWitness=different(expected,behindBoth),aheadWitness=different(expected,aheadBoth);
            if(mismatch||behindWitness==0||aheadWitness==0)failure("two-plane depth pixels mismatch="+std::to_string(mismatch)
                +" wrong-behind witness="+std::to_string(behindWitness)+" wrong-front witness="+std::to_string(aheadWitness));
            std::vector<std::uint32_t> sheet(width*4*height);
            for(int column=0;column<4;++column){const auto& frame=column==0?actual:column==1?expected:column==2?behindBoth:aheadBoth;
                for(int y=0;y<height;++y)std::copy_n(frame.begin()+y*width,width,sheet.begin()+y*width*4+column*width);}
            if(!writeRuntimeBgraBmp(root/"two-plane-smoke-depth.bmp",width*4,height,sheet))failure("depth image write");
            std::cout<<"Air smoke depth: order=rear,puff,front; pixelMismatch="<<mismatch<<" behindWitness="<<behindWitness
                <<" frontWitness="<<aheadWitness<<" ground+air=2 each once; orphanAirRecords="<<orphanRecords.size()<<'\n';
        }
    }
    std::cout<<"Air smoke render: anchorCases="<<checked<<" pictures="<<images<<" missingOptionalBodies="
        <<resourcesMissing<<" fullScenes="<<fullScenes<<" fullPictures="<<fullPictures<<" failures="<<failures<<'\n';
    if(failures){error="Air smoke render: "+std::to_string(failures)+" failures; "+firstFailure;return false;}
    error.clear();return true;
}
