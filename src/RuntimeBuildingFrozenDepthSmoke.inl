// A frozen building blends its completed body once with the actual buildings
// and units behind it. Geometry separation and standalone original previews
// supply this oracle; the new building depth/ownership helpers are not used.
{
    try {
        auto frozenStorage = std::make_unique<RuntimeWindowState>(); auto& f = *frozenStorage;
        f.config=s.config; f.report=s.report; f.rules=s.rules; f.art=s.art; f.theater=s.theater;
        f.rulesLoaded=f.artLoaded=f.theaterLoaded=true; f.visionEnabled=false;
        f.runtimeSoundsEnabled=f.runtimePreferencePersistenceEnabled=false;
        f.runtimeSuppressPlayerAudioFeedback=true; f.economy=s.economy;
        f.humanHouseAuthorities=s.humanHouseAuthorities; f.ownerRemapColors=s.ownerRemapColors;
        f.world=s.world; for(auto& cell:f.world.accessTerrainCells())cell.level=0;
        auto frozenView=view; frozenView.rules=&f.rules; frozenView.art=&f.art;
        frozenView.theater=&f.theater; frozenView.ownerRemapColors=&f.ownerRemapColors;
        frozenView.animationTick=17;
        std::vector<SpecialMechanismRenderSnapshot> frozenSnapshots;
        frozenView.specialMechanismSnapshots=&frozenSnapshots;
        auto frozenCache=std::make_unique<RuntimeMapRenderCache>();
        std::ofstream frozenRows(root/"building-frozen-depth.csv");
        frozenRows<<"scene,zoom,id_reverse,insertion_reverse,pan,foreign_background_witness,"
            "tank_through_body_witness,hidden_tank_witness,normal_mismatch,frozen_mismatch,hdc_mismatch,repeat_equal,restored_equal,coverage,result\n";
        int frozenCases=0,frozenHdcChecks=0;
        std::uint64_t frozenForeignWitnesses=0,frozenTankWitnesses=0,frozenHiddenTankWitnesses=0;
        const auto halfBlend=[](std::uint32_t background,std::uint32_t body) {
            std::uint32_t result=0;
            for(unsigned shift:{0U,8U,16U})result|=((((body>>shift)&255U)*128U
                +((background>>shift)&255U)*127U+127U)/255U)<<shift;
            return result;
        };
        for(int sceneKind:{0,1,2})for(int zoom:{48,60})for(bool reverseIds:{false,true})
            for(bool reverseInsertion:{false,true})for(bool pan:{false,true}) {
            const int beforeFailures=failures;
            const bool withTank=sceneKind!=0, tankBehind=sceneKind==2;
            const std::string sceneName=tankBehind?"tank-behind":withTank?"tank-between":"pair";
            const char* coverage=tankBehind?(zoom==48?"hidden-unit-overlap":"nonoverlap-control")
                :withTank?"visible-unit-overlap":"building-overlap";
            const std::string label="frozen-building-"+sceneName+"-"
                +std::to_string(zoom)+"-id"+std::to_string(reverseIds)+"-order"+std::to_string(reverseInsertion)
                +"-pan"+std::to_string(pan);
            std::vector<WorldObjectInstance> ordered(withTank?3:2);
            for(std::size_t i=0;i<ordered.size();++i) {
                auto& object=ordered[i]; const bool tank=withTank&&i==1;
                const bool rear=i==0;
                object.kind=tank?WorldObjectKind::Unit:WorldObjectKind::Structure;
                object.type=rear?"GAREFN":tank?(tankBehind?"MCV":"MTNK"):"GAPOWR";
                object.instanceId=901+static_cast<int>(reverseIds?ordered.size()-1-i:i);
                object.owner="Americans"; object.alive=object.poweredOperational=true;
                object.x=rear?20:tank?(tankBehind?19:23):(withTank&&!tankBehind?23:20);
                object.y=rear?20:tank?(tankBehind?22:23):(withTank&&!tankBehind?24:23);
                object.cell=runtimeCellKey(object.x,object.y); object.mission="Guard";
                object.facing=tank&&tankBehind?128:64; object.turretFacing=128;
                object.health=runtimeMaxHealthForObject(f,object);
            }
            // Pair: refinery [20,24)x[20,23), plant [20,22)x[23,25).
            // Unit control leaves a legal one-cell gap: refinery as above,
            // MTNK cell (23,23), plant [23,25)x[24,26). Existing body submission
            // is rear -> tank -> front as well; no new unit sorting is assumed.
            // Reverse control: MCV (19,22), facing 128, is legally west of
            // the refinery and northwest of the plant, with sort sum 41.
            // Independent original-pixel search in probe11 proved three
            // hidden-unit pixels at zoom48. At zoom60 the fixed-size artwork
            // has no such joint overlap; retain the same pose as a full-pixel
            // nonoverlap control, without counting it as a hidden-unit witness.
            f.world.accessObjects()=ordered;
            if(reverseInsertion)std::reverse(f.world.accessObjects().begin(),f.world.accessObjects().end());
            initializeRuntimeNavigation(f); f.structureFootprints=buildRuntimeStructureFootprints(f);
            f.tick=17; f.camera.initialized=true; f.camera.tileWidth=zoom; f.camera.tileHeight=zoom/2;
            f.camera.worldOffsetX=-width/2+(pan?31:0);
            f.camera.worldOffsetY=static_cast<int>(std::lround(43*zoom/4.0))-height/2+(pan?-17:0);
            invalidateRuntimeObjectProjectionCache(f); RuntimeSnapshotActiveStateScope active(f);
            frozenSnapshots.clear(); (void)runtimeProjectedObjectsForViewport(f,viewport);
            GameWorld frozenWorld=f.world;
            const auto paint=[&](const std::vector<WorldObjectInstance>& objects) {
                frozenWorld.accessObjects()=objects; auto camera=f.camera;
                if(!drawRuntimeMap(surface.dc,viewport,frozenWorld,frozenView,camera,*frozenCache).rendered)
                    throw std::runtime_error("frozen building production draw");
                GdiFlush(); return std::vector<std::uint32_t>(surface.pixels,surface.pixels+width*height);
            };
            const auto preview=[&](const WorldObjectInstance& object) {
                if(!drawRuntimeObjectPreviewSprite(surface.dc,viewport,object,frozenView,f.camera,*frozenCache))
                    throw std::runtime_error("frozen building original preview resource missing");
                GdiFlush(); return std::vector<std::uint32_t>(surface.pixels,surface.pixels+width*height);
            };
            const auto blank=paint({});
            std::copy(blank.begin(),blank.end(),surface.pixels); const auto rear=preview(ordered.front());
            const auto tankOnly=withTank?paint({ordered[1]}):blank;
            // Vehicle art uses the normal VXL path, which the SHP building
            // preview does not provide. Render the independently specified
            // rear-only scene before compositing the foreground building.
            const auto background=withTank?paint({ordered[0],ordered[1]}):rear;
            std::copy(background.begin(),background.end(),surface.pixels);
            const auto completed=preview(ordered.back());
            std::vector<std::uint32_t> expected(completed.size());
            for(std::size_t p=0;p<expected.size();++p)expected[p]=halfBlend(background[p],completed[p]);
            std::copy(rear.begin(),rear.end(),surface.pixels); const auto noTankCompleted=preview(ordered.back());
            std::copy(blank.begin(),blank.end(),surface.pixels); const auto isolatedFront=preview(ordered.back());
            int foreignWitness=0,tankWitness=0,hiddenTankWitness=0;
            for(std::size_t p=0;p<expected.size();++p) {
                if((completed[p]&0xffffffU)==(background[p]&0xffffffU))continue;
                if((rear[p]&0xffffffU)!=(blank[p]&0xffffffU)
                    &&expected[p]!=halfBlend(blank[p],isolatedFront[p]))++foreignWitness;
                if(withTank&&(background[p]&0xffffffU)!=(rear[p]&0xffffffU)
                    &&expected[p]!=halfBlend(rear[p],noTankCompleted[p]))++tankWitness;
                if(tankBehind&&(tankOnly[p]&0xffffffU)!=(blank[p]&0xffffffU)
                    &&(tankOnly[p]&0xffffffU)!=(rear[p]&0xffffffU)
                    &&(rear[p]&0xffffffU)!=(blank[p]&0xffffffU)
                    &&(background[p]&0xffffffU)==(rear[p]&0xffffffU)
                    &&expected[p]==halfBlend(rear[p],noTankCompleted[p]))++hiddenTankWitness;
            }
            const auto normal=paint(f.world.getObjects()); const int normalMismatch=difference(normal,completed);
            SpecialMechanismRenderSnapshot temporal; temporal.kind="temporal";
            temporal.targetObjectId=ordered.back().instanceId; temporal.requiredProgress=10000; temporal.progress=100;
            frozenSnapshots={temporal};
            const auto frozen=paint(f.world.getObjects()),repeat=paint(f.world.getObjects());
            const int frozenMismatch=difference(frozen,expected); int hdcMismatch=0;
            if(!reverseIds&&!reverseInsertion&&!pan) {
                auto hdcView=frozenView; hdcView.surfacePixels=nullptr;
                hdcView.surfaceWidth=hdcView.surfaceHeight=0; auto camera=f.camera;
                if(!drawRuntimeMap(surface.dc,viewport,frozenWorld,hdcView,camera,*frozenCache).rendered)
                    throw std::runtime_error("frozen building HDC draw");
                GdiFlush(); const std::vector<std::uint32_t> hdcPixels(surface.pixels,surface.pixels+width*height);
                hdcMismatch=difference(hdcPixels,expected); ++frozenHdcChecks;
                std::vector<std::uint32_t> sheet(width*3*height);
                for(int column=0;column<3;++column) {
                    const auto& image=column==0?frozen:column==1?expected:background;
                    for(int y=0;y<height;++y)std::copy_n(image.begin()+y*width,width,sheet.begin()+y*width*3+column*width);
                }
                std::string detail;
                if(!writeRuntimeSmokeBmp((root/(label+".bmp")).string(),sheet.data(),width*3,height,detail))fail(label,detail);
            }
            frozenSnapshots.clear(); const auto restored=paint(f.world.getObjects());
            if(!foreignWitness||(sceneKind==1&&!tankWitness)||(tankBehind&&zoom==48&&!hiddenTankWitness)
                ||normalMismatch||frozenMismatch||hdcMismatch
                ||difference(frozen,repeat)||difference(restored,completed))
                fail(label,"foreignBackground="+std::to_string(foreignWitness)+" tankThroughBody="+std::to_string(tankWitness)
                    +" hiddenTank="+std::to_string(hiddenTankWitness)
                    +" normal="+std::to_string(normalMismatch)+" frozen="+std::to_string(frozenMismatch)
                    +" hdc="+std::to_string(hdcMismatch)+" repeat="+std::to_string(difference(frozen,repeat))
                    +" restored="+std::to_string(difference(restored,completed)));
            frozenRows<<sceneName<<','<<zoom<<','<<reverseIds<<','<<reverseInsertion<<','<<pan<<','<<foreignWitness
                <<','<<tankWitness<<','<<hiddenTankWitness<<','<<normalMismatch<<','<<frozenMismatch<<','<<hdcMismatch
                <<','<<(difference(frozen,repeat)==0)<<','<<(difference(restored,completed)==0)
                <<','<<coverage<<','<<(beforeFailures==failures?"PASS":"FAIL")<<'\n'; frozenRows.flush();
            ++frozenCases; frozenForeignWitnesses+=foreignWitness; frozenTankWitnesses+=tankWitness;
            if(tankBehind&&zoom==48)frozenHiddenTankWitnesses+=hiddenTankWitness;
        }
        if(!frozenHiddenTankWitnesses)fail("frozen-building","no fixed original hidden-unit overlap witness");
        std::cout<<"Frozen building depth: cases="<<frozenCases<<" foreignBackgroundWitnesses="<<frozenForeignWitnesses
            <<" tankThroughBodyWitnesses="<<frozenTankWitnesses<<" hiddenTankWitnesses="<<frozenHiddenTankWitnesses
            <<" hdcChecks="<<frozenHdcChecks<<'\n';
    } catch(const std::exception& detail) { fail("frozen-building",detail.what()); }
}
