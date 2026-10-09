// Independent SHP/template pixels, not a whole-building near/far assumption.
// Isolate the two stock bodies in memory; production rule/art files stay intact.
{
    using namespace RuntimeBuildingDepthOracle;
    try {
        const auto shape = decodeFrameZero(retailTemplate(std::filesystem::current_path()));
        const auto resources = std::filesystem::path("local_data/resources/mix_exports/extracted_buildings/all01umd");
        const std::array<OriginalFrame, 2> frames{{
            decodeFrameZero(readPinned(resources / "GAPILE.shp", "ff652d8ec56891cd19cee6a04720cdc741029b674bcd38fe546916521cc2ada2")),
            decodeFrameZero(readPinned(resources / "GAPOWR.shp", "0c31af7a95252b9f4ce5c67e5288f13277fe0ebaba0e45a6b2b79d09f4b5e6ac"))
        }};
        auto mixedStorage = std::make_unique<RuntimeWindowState>(); auto& m = *mixedStorage;
        m.config = s.config;
        m.report = buildRuntimeStartupReport(m.config, &m.world, &m.theater, &m.art, &m.rules, &m.mapFacts);
        if (!m.report.success) throw std::runtime_error("mixed-height original startup");
        IniFile bodyArt;
        if (!bodyArt.loadFromString("[GAPILE]\nImage=GAPILE\nFoundation=3x2\nHeight=4\nNewTheater=yes\nRemapable=yes\n"
                "[GAPOWR]\nImage=GAPOWR\nFoundation=2x2\nHeight=4\nNewTheater=yes\nRemapable=yes\n")
            || !m.art.loadFromIni(bodyArt)) throw std::runtime_error("mixed-height isolated body art");
        m.rulesLoaded = m.artLoaded = m.theaterLoaded = true; m.visionEnabled = false;
        m.runtimeSoundsEnabled = m.runtimePreferencePersistenceEnabled = false;
        m.runtimeSuppressPlayerAudioFeedback = true; m.economy = s.economy;
        m.humanHouseAuthorities = s.humanHouseAuthorities; m.ownerRemapColors = s.ownerRemapColors;
        m.report.map.size.clear(); m.report.map.localSize.clear(); m.report.map.width = -1;
        m.world = s.world; for (auto& cell : m.world.accessTerrainCells()) cell.level = 0;
        auto bodyView = view; bodyView.art = &m.art; bodyView.rules = &m.rules; bodyView.theater = &m.theater;
        bodyView.ownerRemapColors = &m.ownerRemapColors; bodyView.animationTick = 0;
        auto bodyCache = std::make_unique<RuntimeMapRenderCache>();
        std::ofstream mixedRows(root / "mixed-height.csv");
        mixedRows << "zoom,rear_level,front_level,reverse,overlap_pixels,pixel_mismatch,wrong_picks,result\n";
        std::uint64_t mixedOverlap = 0; int mixedCases = 0;
        for (int zoom : {48,60,72}) for (const auto levels : {
                std::array<int,2>{0,2}, {2,0}, {0,4}, {4,0}}) for (bool reversed : {false,true}) {
            const std::string label = "mixed-" + std::to_string(zoom) + "-" + std::to_string(levels[0])
                + "-" + std::to_string(levels[1]) + "-" + std::to_string(reversed);
            const int beforeFailures = failures;
            std::array<WorldObjectInstance,2> bodies;
            for (int i = 0; i < 2; ++i) {
                auto& object = bodies[i]; object.type = i ? "GAPOWR" : "GAPILE";
                object.kind = WorldObjectKind::Structure; object.instanceId = 501 + (reversed ? 1-i : i);
                object.owner = "Americans"; object.x = 20; object.y = i ? 22 : 20; object.level = levels[i];
                object.cell = runtimeCellKey(object.x,object.y); object.alive = object.poweredOperational = true;
                object.health = m.rules.findBuildingType(object.type)->strength; object.mission = "Guard";
            }
            m.world.accessObjects().assign(bodies.begin(),bodies.end());
            if (reversed) std::reverse(m.world.accessObjects().begin(),m.world.accessObjects().end());
            // Navigation derives a grounded structure's level from its actual
            // foundation cells. Give each independent foundation its height
            // before initializing, rather than silently normalizing both to 0.
            for (auto& cell : m.world.accessTerrainCells()) {
                cell.level = 0;
                for (int i=0;i<2;++i) if (cell.x>=bodies[i].x && cell.x<bodies[i].x+(i?2:3)
                    && cell.y>=bodies[i].y && cell.y<bodies[i].y+2) cell.level=levels[i];
            }
            initializeRuntimeNavigation(m); m.structureFootprints = buildRuntimeStructureFootprints(m);
            for (const auto& object : m.world.getObjects())
                if (object.level != (object.type=="GAPILE"?levels[0]:levels[1]))
                    throw std::runtime_error("mixed-height foundation did not retain the authored level");
            m.tick = 0; m.camera.initialized = true; m.camera.tileWidth = zoom; m.camera.tileHeight = zoom/2;
            m.camera.worldOffsetX = -width/2 + (reversed ? 19 : 0);
            m.camera.worldOffsetY = 21*zoom/2 - height/2 + (reversed ? -13 : 0);
            invalidateRuntimeObjectProjectionCache(m); RuntimeSnapshotActiveStateScope active(m);
            const auto projected = runtimeProjectedObjectsForViewport(m,viewport); // before any paint
            std::array<std::vector<int>,2> depth;
            for (auto& item : depth) item.assign(width*height,std::numeric_limits<int>::max());
            for (int i=0;i<2;++i) {
                const auto& object=bodies[i]; const auto& f=frames[i]; const int fw=i?2:3,fh=2;
                const int centerX=(object.x-object.y)*zoom/2-m.camera.worldOffsetX;
                const int centerY=static_cast<int>(std::lround((object.x+object.y-object.level)*zoom/4.0))-m.camera.worldOffsetY;
                const int left=centerX-f.canvasWidth/2+f.x, top=centerY-f.canvasHeight/2+f.y;
                const int nativeTop=(object.x+object.y-object.level)*15-f.canvasHeight/2+f.y;
                for(int y=0;y<f.height;++y)for(int x=0;x<f.width;++x) {
                    if(!f.pixels[static_cast<std::size_t>(y)*f.width+x])continue;
                    const int sx=left+x,sy=top+y;
                    if(sx<0||sy<0||sx>=width||sy>=height)continue;
                    const int u=x+f.x-f.canvasWidth/2+198-30*(fw-fh);
                    const int v=y+f.y-f.canvasHeight/2+446-15*(fw+fh-2);
                    if(u<0||v<0||u>=shape.width||v>=shape.height)throw std::runtime_error("mixed-height original body outside native template");
                    const int raw=shape.pixels[static_cast<std::size_t>(v)*shape.width+u];
                    int signedValue=raw?raw-65:0;if(signedValue>127)signedValue-=256;if(signedValue< -128)signedValue+=256;
                    depth[i][static_cast<std::size_t>(sy)*width+sx]=32768-nativeTop-f.height-object.level*15-1-signedValue;
                }
            }
            std::vector<int> owner(width*height,-1); int overlap=0,wrongPicks=0;
            for(std::size_t p=0;p<owner.size();++p) {
                for(int i=0;i<2;++i)if(depth[i][p]!=std::numeric_limits<int>::max()
                    &&(owner[p]<0||depth[i][p]<depth[owner[p]][p]))owner[p]=i;
                if(depth[0][p]==std::numeric_limits<int>::max()||depth[1][p]==std::numeric_limits<int>::max())continue;
                ++overlap;const POINT point{static_cast<LONG>(p%width),static_cast<LONG>(p/width)};
                const auto* hit=findProjectedObjectAtPoint(projected,point);
                if(!hit||!hit->object||hit->object->instanceId!=bodies[owner[p]].instanceId)++wrongPicks;
            }
            GameWorld probe=m.world;
            const auto paint=[&](const std::vector<WorldObjectInstance>& objects) {
                probe.accessObjects()=objects; auto camera=m.camera;
                if(!drawRuntimeMap(surface.dc,viewport,probe,bodyView,camera,*bodyCache).rendered)
                    throw std::runtime_error("mixed-height production draw");
                GdiFlush(); return std::vector<std::uint32_t>(surface.pixels,surface.pixels+width*height);
            };
            auto expected=paint({}); const std::array<std::vector<std::uint32_t>,2> solo{{paint({bodies[0]}),paint({bodies[1]})}};
            for(std::size_t p=0;p<owner.size();++p)if(owner[p]>=0)expected[p]=solo[owner[p]][p];
            const auto actual=paint(m.world.getObjects()); const int mismatch=difference(actual,expected);
            if(mismatch||wrongPicks)fail(label,"mixed-native pixels="+std::to_string(mismatch)+" picks="+std::to_string(wrongPicks));
            mixedRows<<zoom<<','<<levels[0]<<','<<levels[1]<<','<<reversed<<','<<overlap<<','<<mismatch<<','<<wrongPicks
                <<','<<(beforeFailures==failures?"PASS":"FAIL")<<'\n';
            mixedOverlap+=overlap;++mixedCases;
        }
        if(!mixedOverlap)fail("mixed-height","no independent original pixel overlap witness");
        std::cout<<"Mixed-height building bodies: cases="<<mixedCases<<" overlapPixels="<<mixedOverlap<<'\n';
    } catch(const std::exception& detail) {fail("mixed-height",detail.what());}
}
