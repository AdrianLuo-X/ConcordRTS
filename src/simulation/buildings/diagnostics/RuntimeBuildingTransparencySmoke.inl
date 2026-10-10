// Partial-opacity building animation must retain the other building's color
// underneath it. YAPOWR's real 1--4 person stages blend the stock A/B frames.
// These fixtures use the unchanged stock art, rules and rendering preview;
// no production depth buffer or owner map supplies the expected result.
{
    try {
        auto transparentStorage = std::make_unique<RuntimeWindowState>();
        auto& t = *transparentStorage;
        t.config = s.config; t.report = s.report;
        t.rules = s.rules; t.art = s.art; t.theater = s.theater;
        t.rulesLoaded = t.artLoaded = t.theaterLoaded = true;
        t.visionEnabled = false;
        t.runtimeSoundsEnabled = t.runtimePreferencePersistenceEnabled = false;
        t.runtimeSuppressPlayerAudioFeedback = true;
        t.economy = s.economy; t.humanHouseAuthorities = s.humanHouseAuthorities;
        t.ownerRemapColors = s.ownerRemapColors; t.world = s.world;
        for (auto& cell : t.world.accessTerrainCells()) cell.level = 0;
        const auto* reactorType = t.rules.findBuildingType("YAPOWR");
        const auto* reactorArt = t.art.findArtType("YAPOWR");
        const auto* barracksType = t.rules.findBuildingType("GAPILE");
        const auto* barracksArt = t.art.findArtType("GAPILE");
        if (!reactorType || !reactorArt || !barracksType || !barracksArt
            || !reactorType->infantryAbsorb || reactorType->passengers != 5
            || reactorArt->foundation != "2x2" || barracksArt->foundation != "3x2")
            throw std::runtime_error("transparent building stock rules/art changed");
        auto transparentView = view;
        transparentView.rules = &t.rules; transparentView.art = &t.art;
        transparentView.theater = &t.theater;
        transparentView.ownerRemapColors = &t.ownerRemapColors;
        transparentView.animationTick = 0;
        std::vector<SpecialMechanismRenderSnapshot> marks;
        transparentView.specialMechanismSnapshots=&marks;
        const auto laserColor=t.rules.getAudioVisualValue("LaserTargetColorAdd");
        if(!laserColor.empty()&&laserColor!="24,0,0")throw std::runtime_error("stock laser tint changed");
        auto transparentCache = std::make_unique<RuntimeMapRenderCache>();
        std::ofstream transparentRows(root / "building-transparency.csv");
        transparentRows << "occupants,id_reverse,insertion_reverse,pan,marked_building,partial_pixels,"
            "foreign_background_witness,pixel_mismatch,wrong_picks,hdc_mismatch,result\n";
        int transparentCases = 0, transparentHdcChecks = 0;
        std::uint64_t transparentWitnesses = 0;
        for (int occupants : {1,2,3,4}) for (bool reverseIds : {false,true})
            for (bool reverseInsertion : {false,true}) for (bool pan : {false,true}) for(int marked:{-1,0,1}) {
            const int beforeFailures = failures;
            const std::string label = "transparent-reactor-" + std::to_string(occupants)
                + "-id" + std::to_string(reverseIds) + "-order" + std::to_string(reverseInsertion)
                + "-pan" + std::to_string(pan)+"-marked"+std::to_string(marked);
            std::array<WorldObjectInstance,2> ordered;
            for (int i = 0; i < 2; ++i) {
                auto& object = ordered[i];
                object.kind = WorldObjectKind::Structure;
                object.type = i ? "YAPOWR" : "GAPILE";
                object.instanceId = 701 + (reverseIds ? 1-i : i);
                object.owner = "Americans"; object.x = i ? 20 : 19; object.y = i ? 20 : 18;
                object.cell = runtimeCellKey(object.x,object.y);
                object.alive = object.poweredOperational = true;
                object.health = i ? reactorType->strength : barracksType->strength;
                object.mission = "Guard";
            }
            // South separation, with overlapping X intervals: the 3x2 rear
            // foundation ends at y=20, exactly where the 2x2 front one starts.
            // Independent raw stock frame inspection finds six B-only edge
            // pixels at (26,-23),(26,-22),(27,-21)..(27,-18), relative to the
            // reactor anchor, over this barracks. The black/white experiment
            // below verifies actual partial coverage without trusting a mask.
            for (int i = 0; i < occupants; ++i) ordered[1].transportPassengerIds.push_back(800+i);
            marks.clear();
            if(marked>=0) {SpecialMechanismRenderSnapshot mark;mark.kind="airstrikeMarker";
                mark.targetObjectId=ordered[marked].instanceId;mark.intensity=1;mark.expireTick=100;
                mark.targetCell={-1,-1};marks.push_back(mark);}
            t.world.accessObjects().assign(ordered.begin(),ordered.end());
            if (reverseInsertion) std::reverse(t.world.accessObjects().begin(),t.world.accessObjects().end());
            initializeRuntimeNavigation(t); t.structureFootprints = buildRuntimeStructureFootprints(t);
            t.tick = 0; t.camera.initialized = true;
            t.camera.tileWidth = 60; t.camera.tileHeight = 30;
            t.camera.worldOffsetX = -width/2 + (pan ? 37 : 0);
            t.camera.worldOffsetY = 600-height/2 + (pan ? -23 : 0);
            invalidateRuntimeObjectProjectionCache(t);
            RuntimeSnapshotActiveStateScope active(t);
            const auto projected = runtimeProjectedObjectsForViewport(t,viewport); // before paint
            if (projected.size() != ordered.size()) fail(label,"transparent projection count");
            GameWorld transparentWorld = t.world;
            const auto paint = [&](const std::vector<WorldObjectInstance>& objects) {
                transparentWorld.accessObjects() = objects;
                auto camera = t.camera;
                if (!drawRuntimeMap(surface.dc,viewport,transparentWorld,transparentView,camera,*transparentCache).rendered)
                    throw std::runtime_error("transparent full draw");
                GdiFlush(); return std::vector<std::uint32_t>(surface.pixels,surface.pixels+width*height);
            };
            const auto preview = [&](const WorldObjectInstance& object) {
                auto previewView=transparentView;
                previewView.airstrikeBodyColorAdd=marked>=0&&object.instanceId==ordered[marked].instanceId?24U<<11U:0;
                if (!drawRuntimeObjectPreviewSprite(surface.dc,viewport,object,previewView,t.camera,*transparentCache))
                    throw std::runtime_error("transparent original preview resource missing");
                GdiFlush(); return std::vector<std::uint32_t>(surface.pixels,surface.pixels+width*height);
            };
            const auto blank = paint({});
            std::copy(blank.begin(),blank.end(),surface.pixels);
            const auto rear = preview(ordered[0]);
            const auto expected = preview(ordered[1]); // authoritatively separated back -> front
            std::copy(blank.begin(),blank.end(),surface.pixels);
            const auto front = preview(ordered[1]);
            std::fill_n(surface.pixels,width*height,0x00000000U);
            const auto onBlack = preview(ordered[1]);
            std::fill_n(surface.pixels,width*height,0x00ffffffU);
            const auto onWhite = preview(ordered[1]);
            int partialPixels = 0, foreignWitness = 0, wrongPicks = 0;
            for (std::size_t p = 0; p < blank.size(); ++p) {
                const auto black = onBlack[p]&0xffffffU, white = onWhite[p]&0xffffffU;
                // Opaque pixels are background invariant; untouched pixels
                // retain exactly black/white. Only partial opacity meets both.
                if (black == white || (black == 0 && white == 0xffffffU)) continue;
                ++partialPixels;
                if ((rear[p]&0xffffffU) == (blank[p]&0xffffffU)
                    || (expected[p]&0xffffffU) == (front[p]&0xffffffU)
                    || (expected[p]&0xffffffU) == (rear[p]&0xffffffU)) continue;
                ++foreignWitness;
                const auto* hit = findProjectedObjectAtPoint(projected,
                    POINT{static_cast<LONG>(p%width),static_cast<LONG>(p/width)});
                if (!hit || !hit->object || hit->object->instanceId != ordered[1].instanceId) ++wrongPicks;
            }
            const auto actual = paint(t.world.getObjects());
            const int mismatch = difference(actual,expected);
            int hdcMismatch = 0;
            if (!reverseIds && !reverseInsertion && !pan) {
                auto hdcView = transparentView; hdcView.surfacePixels = nullptr;
                hdcView.surfaceWidth = hdcView.surfaceHeight = 0; auto camera = t.camera;
                if (!drawRuntimeMap(surface.dc,viewport,transparentWorld,hdcView,camera,*transparentCache).rendered)
                    throw std::runtime_error("transparent HDC draw");
                GdiFlush(); const std::vector<std::uint32_t> hdcPixels(surface.pixels,surface.pixels+width*height);
                hdcMismatch = difference(hdcPixels,expected); ++transparentHdcChecks;
                std::vector<std::uint32_t> sheet(width*3*height);
                for (int column=0;column<3;++column) {
                    const auto& image = column==0 ? actual : column==1 ? expected : front;
                    for (int y=0;y<height;++y) std::copy_n(image.begin()+y*width,width,
                        sheet.begin()+y*width*3+column*width);
                }
                std::string detail;
                if (!writeRuntimeSmokeBmp((root/(label+".bmp")).string(),sheet.data(),width*3,height,detail)) fail(label,detail);
            }
            if (!partialPixels || !foreignWitness || mismatch || wrongPicks || hdcMismatch)
                fail(label,"partial="+std::to_string(partialPixels)+" foreignBackground="+std::to_string(foreignWitness)
                    +" pixels="+std::to_string(mismatch)+" picks="+std::to_string(wrongPicks)
                    +" hdc="+std::to_string(hdcMismatch));
            transparentRows << occupants << ',' << reverseIds << ',' << reverseInsertion << ',' << pan
                << ',' << marked << ',' << partialPixels << ',' << foreignWitness << ',' << mismatch << ',' << wrongPicks
                << ',' << hdcMismatch << ',' << (beforeFailures==failures?"PASS":"FAIL") << '\n';
            transparentRows.flush(); ++transparentCases; transparentWitnesses += foreignWitness;
        }
        std::cout << "Transparent building animation: cases=" << transparentCases
            << " foreignBackgroundWitnesses=" << transparentWitnesses << " hdcChecks=" << transparentHdcChecks << '\n';
    } catch (const std::exception& detail) { fail("transparent-building",detail.what()); }
}
