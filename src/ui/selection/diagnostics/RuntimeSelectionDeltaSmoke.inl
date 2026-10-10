bool runRuntimeSelectionDeltaSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Selection delta: " + why; return false; };
    const char* requested = std::getenv("CONCORD_SELECTION_DELTA_DIR");
    const std::string root = requested && *requested ? requested
        : "build/smoke/feature-checks/15-selection-delta/checks";
    std::filesystem::create_directories(root);
    auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box; s.config = config;
    s.config.defaultMapPath = "local_data/resources/mix_exports/multimd/2peaks.map";
    s.config.computerAiEnabled = s.config.noCooldown = s.config.multiplayerActive = s.config.crates = false;
    s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear();
    s.report = buildRuntimeStartupReport(s.config, &s.world, &s.theater, &s.art, &s.rules, &s.mapFacts);
    if (!s.report.success) return fail("stock startup/resources");
    s.rulesLoaded = s.artLoaded = s.theaterLoaded = s.mapFactsLoaded = true; s.visionEnabled = false;
    s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = false;
    s.runtimeSuppressPlayerAudioFeedback = true; s.triggers.bootstrapComplete = true;
    s.economy.owner = "Americans"; s.ownerRemapColors[s.economy.owner] = RGB(255, 30, 30);
    s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
    rebuildRuntimeTerrainInfoCache(s);
    RuntimeCellPoint cell{38,61};
    for (const auto& terrain : s.world.getTerrainCells()) {
        const auto it = s.terrainInfoByCell.find(runtimeCellKey(terrain.x, terrain.y));
        if (it == s.terrainInfoByCell.end()) continue;
        bool displayStrip = true;
        for (int index = 0; index < 5; ++index) {
            const auto spot = s.terrainInfoByCell.find(runtimeCellKey(terrain.x + index * 2, terrain.y - index * 2));
            if (spot == s.terrainInfoByCell.end() || !spot->second.passable || spot->second.water
                || spot->second.bridge || spot->second.shore) { displayStrip = false; break; }
        }
        if (displayStrip) { cell = {terrain.x, terrain.y}; break; }
    }
    IniFile source; if (!source.loadFromFile("local_data/rulesmd.ini")) return fail("original rules");
    int rows = 0, nonzero = 0, projections = 0, clicks = 0, pixelsChecked = 0, boundaries = 0, pictures = 0;
    for (const auto* list : {&s.rules.getInfantryTypes(), &s.rules.getVehicleTypes(), &s.rules.getAircraftTypes()}) {
        for (const auto& type : *list) {
            const int expected = std::stoi(source.getValue(type.id, "PixelSelectionBracketDelta", "0"));
            if (type.pixelSelectionBracketDelta != expected) return fail(type.id + " loader/default");
            ++rows; if (expected != 0) ++nonzero;
        }
    }
    if (nonzero != 5 || rows != 155) return fail("stock registry coverage");
    const std::vector<std::pair<std::string,int>> examples{{"GGI",-6},{"BRUTE",-8},{"YURIPR",-26},
        {"ARND",-6},{"RMNV",-6},{"E1",0},{"MTNK",0},{"ORCA",0},{"SUB",0}};
    const RECT viewport{0,0,640,480};
    std::ofstream records(root + "/selection-geometry.csv");
    records << "id,pose,tile_width,level,delta,anchor_x,anchor_y,left,top,right,bottom,health_top\n";
    const auto project = [&]() { invalidateRuntimeObjectProjectionCache(s); return buildProjectedObjects(s, viewport); };
    for (const auto& example : examples) for (int pose = 0; pose < 3; ++pose)
        for (int tileWidth : {48,60,72}) for (int level : {0,2}) {
        const bool infantry = example.first != "MTNK" && example.first != "ORCA" && example.first != "SUB";
        WorldObjectInstance object; object.instanceId = 1500;
        object.kind = infantry ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        object.authoredAircraft = example.first == "ORCA"; object.type = example.first; object.owner = s.economy.owner;
        object.alive = true; object.x = cell.x; object.y = cell.y; object.cell = runtimeCellKey(cell.x,cell.y);
        object.level = level; object.facing = pose * 64; object.mission = "Guard";
        object.deployed = infantry && pose == 1; object.prone = infantry && pose == 2;
        object.airborne = object.authoredAircraft; object.submerged = example.first == "SUB" && pose == 2;
        auto* type = const_cast<UnitType*>(findRuntimeUnitType(s.rules,object));
        if (!type) return fail(example.first + " type missing");
        object.health = type->strength; object.currentAmmo = type->ammo < 0 ? -1 : std::max(0,type->initialAmmo < 0 ? type->ammo : type->initialAmmo);
        s.world.accessObjects() = {object}; s.selectedObjectIds = {object.instanceId};
        s.camera = {}; s.camera.initialized = true; s.camera.tileWidth = tileWidth; s.camera.tileHeight = tileWidth / 2;
        const auto center = runtimeProjectCellXY(cell.x,cell.y,level,s.camera);
        s.camera.worldOffsetX = center.x - 320; s.camera.worldOffsetY = center.y - 240;
        const int actual = type->pixelSelectionBracketDelta;
        type->pixelSelectionBracketDelta = 0; const auto baseline = project();
        type->pixelSelectionBracketDelta = actual; const auto adjusted = project();
        if (baseline.size()!=1 || adjusted.size()!=1) return fail(example.first + " projection hidden");
        const auto& before = baseline[0]; const auto& after = adjusted[0];
        if (actual != example.second || before.center.x != after.center.x || before.center.y != after.center.y
            || before.renderCenter.x != after.renderCenter.x || before.renderCenter.y != after.renderCenter.y
            || !runtimeRectEquals(before.spriteBounds,after.spriteBounds)
            || before.selectionBounds.left != after.selectionBounds.left || before.selectionBounds.right != after.selectionBounds.right
            || after.selectionBounds.top - before.selectionBounds.top != std::min(0,actual)
            || after.selectionBounds.bottom - before.selectionBounds.bottom != std::max(0,actual)
            || runtimeUnitHealthFrame(after).top - runtimeUnitHealthFrame(before).top != std::min(0,actual)
            || !runtimeRectEquals(runtimeUnitHealthFrame(after,false),runtimeUnitHealthFrame(before,false)))
            return fail(example.first + " pose/zoom/height anchor, bracket, bar or cargo changed incorrectly");
        for (const POINT point : {POINT{after.selectionBounds.left,after.selectionBounds.top},
            POINT{after.selectionBounds.right-1,after.selectionBounds.top},
            POINT{after.selectionBounds.left,after.selectionBounds.bottom-1},
            POINT{after.selectionBounds.right-1,after.selectionBounds.bottom-1},
            POINT{(after.spriteBounds.left+after.spriteBounds.right)/2,(after.spriteBounds.top+after.spriteBounds.bottom)/2}}) {
            const auto* hit = findProjectedObjectAtPoint(adjusted,point);
            if (!hit || hit->object->instanceId != object.instanceId) return fail(example.first + " moved bracket/body click");
            ++clicks;
        }
        records << example.first << ',' << pose << ',' << tileWidth << ',' << level << ',' << actual << ','
            << after.renderCenter.x << ',' << after.renderCenter.y << ',' << after.selectionBounds.left << ','
            << after.selectionBounds.top << ',' << after.selectionBounds.right << ',' << after.selectionBounds.bottom
            << ',' << runtimeUnitHealthFrame(after).top << '\n'; ++projections;
    }
    // INI parsing and handshake hash must distinguish signed values and defaults.
    std::uint64_t previousHash = 0;
    for (const char* value : {"", "-26", "12", "2147483647", "-2147483648"}) {
        IniFile ini;
        if (!ini.loadFromString(std::string("[InfantryTypes]\n0=TEST\n[TEST]\nStrength=100\n")
            + (*value ? std::string("PixelSelectionBracketDelta=") + value + "\n" : ""))) return fail("signed INI");
        RulesDatabase rules; if (!rules.loadFromIni(ini)) return fail("signed load");
        const auto* type = rules.findUnitType("InfantryType","TEST");
        const int expected = *value ? std::stoi(value) : 0;
        if (!type || type->pixelSelectionBracketDelta != expected || rules.getDetailRulesHash() == previousHash)
            return fail("signed value/default/hash");
        previousHash = rules.getDetailRulesHash();
        const RECT bounds = runtimeUnitSelectionBoundsForAnchor({100,100}, expected);
        if (bounds.bottom - bounds.top != 40 || bounds.right - bounds.left != 36) return fail("extreme bounds overflow");
        RuntimeProjectedObject extreme; extreme.selectionBounds = bounds;
        const auto midpoint = runtimeProjectedUnitSelectionCenter(extreme);
        if (midpoint.y != bounds.top + 20) return fail("extreme center overflow");
        if (expected == 12 && bounds.top != 82) return fail("positive delta direction");
        ++boundaries;
    }
    // Draw the real GDI health border/fill and capacity pips onto a clean surface.
    // Health pixels move by the authored delta; capacity pips stay at their base position.
    constexpr int width = 256, height = 200;
    HDC screen = GetDC(nullptr); HDC dc = screen ? CreateCompatibleDC(screen) : nullptr;
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    void* raw=nullptr; HBITMAP bitmap=dc?CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0):nullptr;
    if (!bitmap) { if(dc)DeleteDC(dc); if(screen)ReleaseDC(nullptr,screen); return fail("GDI surface"); }
    const auto oldBitmap=SelectObject(dc,bitmap);
    const auto cleanup=[&]() { SelectObject(dc,oldBitmap); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(nullptr,screen); };
    const auto drawHealth=[&](RuntimeProjectedObject object) {
        std::fill_n(static_cast<std::uint32_t*>(raw),width*height,0x00242c30U);
        drawRuntimeSelectedHealthBar(dc,s,object,nullptr); GdiFlush();
        auto* first=static_cast<std::uint32_t*>(raw); return std::vector<std::uint32_t>(first,first+width*height);
    };
    for (std::size_t index=0;index<5;++index) for(int healthPercent:{100,50,10}) {
        WorldObjectInstance object; object.instanceId=1500;object.type=examples[index].first;
        object.kind=WorldObjectKind::Infantry;object.owner=s.economy.owner;object.alive=true;
        const auto* type=findRuntimeUnitType(s.rules,object);object.health=std::max(1,type->strength*healthPercent/100);
        RuntimeProjectedObject base; base.object=&object;base.observerKind=object.kind;base.observerType=object.type;
        base.center=base.renderCenter={128,130};base.selectionBaseBounds=base.selectionBounds=runtimeUnitSelectionBoundsForAnchor(base.center);
        auto shifted=base;shifted.selectionBounds=runtimeUnitSelectionBoundsForAnchor(base.center,examples[index].second);
        const auto original=drawHealth(base);const auto actual=drawHealth(shifted);
        std::vector<std::uint32_t> expected(width*height,0x00242c30U);
        int ink=0;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const auto color=original[y*width+x]&0x00ffffffU;
            if(color==0x00242c30U)continue;
            const int outputY=y<runtimeUnitHealthFrame(base).bottom+2?y+examples[index].second:y;
            if(outputY<0 || outputY>=height){cleanup();return fail("health draw clipped");}
            expected[outputY*width+x]=color;++ink;
        }
        for(std::size_t i=0;i<actual.size();++i)if((actual[i]&0x00ffffffU)!=expected[i]) {
            cleanup();return fail(object.type+" health/cargo pixel displacement at "+std::to_string(healthPercent));
        }
        if(!ink){cleanup();return fail("empty health pixels");}++pixelsChecked;
    }
    cleanup();
    // Review actual unit sprites with interaction overlays, keeping the same camera for each pair.
    s.world.accessObjects().clear();s.selectedObjectIds.clear();
    for(int index=0;index<5;++index) {
        WorldObjectInstance object;object.instanceId=1500+index;object.type=examples[index].first;
        object.kind=WorldObjectKind::Infantry;object.owner=s.economy.owner;object.alive=true;
        object.x=cell.x+index*2;object.y=cell.y-index*2;object.cell=runtimeCellKey(static_cast<int>(object.x),static_cast<int>(object.y));
        object.health=findRuntimeUnitType(s.rules,object)->strength;object.mission="Guard";
        s.world.accessObjects().push_back(object);s.selectedObjectIds.push_back(object.instanceId);
    }
    for(int pose=0;pose<3;++pose)for(bool apply:{false,true}) {
        for(auto& object:s.world.accessObjects()) {
            object.deployed=pose==1;object.prone=pose==2;
            auto* type=const_cast<UnitType*>(findRuntimeUnitType(s.rules,object));
            type->pixelSelectionBracketDelta=apply?examples[object.instanceId-1500].second:0;
        }
        invalidateRuntimeObjectProjectionCache(s);
        const std::string path=root+"/selection-"+std::to_string(pose)+(apply?"-after.bmp":"-before.bmp");
        if(!drawRuntimeStateOffscreenAtCell(s,{cell.x+4,cell.y-4},960,540,error,path,false,true))return fail(error);
        ++pictures;
    }
    if(s.lastHealthPipResourceDraws<=0)return fail("original health resources not drawn");
    std::cout<<"SelectionDelta PASS stockRows="<<rows<<" nonzero="<<nonzero<<" projections="<<projections
        <<" clicks="<<clicks<<" healthPixelCases="<<pixelsChecked<<" boundaries="<<boundaries<<" images="<<pictures<<'\n';
    return true;
}
