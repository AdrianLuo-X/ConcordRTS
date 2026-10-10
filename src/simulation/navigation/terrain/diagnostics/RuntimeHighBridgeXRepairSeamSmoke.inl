// Test-only original all02 X bridge supplement; no terrain/type/actor replacement.
// Include after offscreen/save/load/Engineer/bridge helpers, beside the Y repair probe.
// RGB stability is not the seam oracle; original native BRIDGE frame0 remains external.
bool runRuntimeHighBridgeXRepairSeamSmoke(RuntimeWindowState& state,
    const std::filesystem::path& outputRoot, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "Original X bridge repair: " + why; return false; };
    if (toLowerAsciiLocal(std::filesystem::path(state.config.defaultMapPath).filename().string()) != "all02umd.map"
        || !state.destroyableBridges || outputRoot.empty())
        return fail("actual all02 map, authored destroyable bridges and private output directory required");
    const auto terrain = [&](RuntimeCellPoint p) -> const TerrainCell* {
        const auto& cells = state.world.getTerrainCells();
        const auto i = std::find_if(cells.begin(), cells.end(), [&](const auto& c) { return c.x == p.x && c.y == p.y; });
        return i == cells.end() ? nullptr : &*i;
    };
    int hutId = -1;
    RuntimeBridgeRepairSpan span;
    for (const auto& [id, candidate] : state.bridgeRepairSpansByHut) {
        const auto* hut = findObjectById(state, id);
        const auto* building = hut ? findRuntimeBuildingTypeById(state.rules, hut->type) : nullptr;
        const bool heads = (candidate.startHead == RuntimeCellPoint{125,56} && candidate.endHead == RuntimeCellPoint{135,56})
            || (candidate.endHead == RuntimeCellPoint{125,56} && candidate.startHead == RuntimeCellPoint{135,56});
        if (!candidate.high || !candidate.alongX || candidate.material != RuntimeBridgeMaterial::Concrete
            || candidate.cells.empty() || !heads || !hut || hut->x != 137 || hut->y != 54
            || !building || !building->bridgeRepairHut
            || candidate.hutObjectId != id) continue;
        const auto* head = terrain({135,56});
        if (!head || toLowerAsciiLocal(state.theater.resolveTileIndex(head->tileIndex).expectedFileName) != "ovrps03.ubn")
            return fail("actual three-lane X end is not original Ovrps03.ubn");
        if (hutId < 0 || id < hutId) { hutId = id; span = candidate; }
    }
    if (hutId < 0) return fail("original CABHUT-bound 125,56 -> 135,56 high X span unavailable");
    const auto* hut = findObjectById(state, hutId);
    const int hutHealth = hut->health;
    const std::string hutOwner = hut->owner;
    const RuntimeCellPoint focus = span.cells[span.cells.size()/2U].point;
    const auto terrainIdentity = [&]() {
        std::ostringstream out;
        for (const auto& c : state.world.getTerrainCells()) out << c.x << ',' << c.y << ':' << c.tileIndex << '/'
            << c.subTileIndex << '/' << c.level << '/' << c.hasOverlay << '/' << c.overlayIndex << '/'
            << c.overlayFrame << '/' << c.overlayType << '/' << c.overlayImage << ';';
        return out.str();
    };
    const std::string originalTerrain = terrainIdentity();
    const auto intact = [&]() {
        if (runtimeBridgeRepairHutNeedsRepair(state, hutId)) return false;
        for (const auto& c : span.cells) if (c.requiresOverlay) {
            const auto* t = terrain(c.point);
            if (!t || !t->hasOverlay || t->overlayIndex != span.overlayIndex || t->overlayType != span.overlayType
                || t->overlayImage != span.overlayImage || t->overlayFrame != c.expectedFrame) return false;
        }
        return true;
    };
    WorldObjectInstance engineerProbe;
    engineerProbe.instanceId = -94111; engineerProbe.kind = WorldObjectKind::Infantry;
    engineerProbe.type = "ENGINEER"; engineerProbe.owner = state.economy.owner; engineerProbe.alive = true;
    const auto corridor = [&]() {
        std::unordered_set<int> ignored;
        for (const auto& object : state.world.getObjects()) ignored.insert(object.instanceId);
        auto pathTerrain = buildRuntimePathTerrainSnapshot(state, engineerProbe, -1, &ignored);
        std::unordered_set<int> allowed;
        for (const auto& c : span.cells) allowed.insert(runtimeCellKey(c.point.x,c.point.y));
        allowed.insert(runtimeCellKey(span.endHead.x,span.endHead.y));
        for (auto& t : pathTerrain) if (!allowed.contains(runtimeCellKey(t.cell.x,t.cell.y))) t.passable = false;
        PathRequestQueue queue(pathTerrain);
        const auto request = queue.submit(toPathCell(span.cells.front().point),toPathCell(span.endHead));
        queue.pump(1);
        for (const auto& r : queue.getResults()) if (r.requestId == request.id) return r.reachable;
        return false;
    };
    if (!intact() || !corridor()) return fail("original all02 X span not initially intact/pathable");
    std::error_code directoryError;
    std::filesystem::create_directories(outputRoot, directoryError);
    if (directoryError) return fail("evidence directory: " + directoryError.message());
    RuntimeMapCamera camera; camera.initialized = true;
    const int focusLevel = terrainLevelAtRuntimeCell(state,focus.x,focus.y);
    const auto center = runtimeProjectCellXY(focus.x,focus.y,focusLevel,camera);
    camera.worldOffsetX = center.x-480; camera.worldOffsetY = center.y-320;
    RuntimeBridgeHeadSeamCapture repaired, loaded;
    repaired.rois.clear();
    std::ofstream heads(outputRoot/"native-heads.csv");
    if (!heads) return fail("native-head evidence open");
    heads << "hut,head,lane,x,y,tile,subtile,level,native_resource,screen_x,screen_y\n";
    for (const auto& endpoint : {std::pair{"start",span.startHead},std::pair{"end",span.endHead}}) {
        const auto* anchor = terrain(endpoint.second);
        if (!anchor) return fail("native head terrain missing");
        const auto point = runtimeProjectCellXY(endpoint.second.x,endpoint.second.y,anchor->level,camera);
        const int x = point.x-camera.worldOffsetX, y = point.y-camera.worldOffsetY;
        repaired.rois.emplace_back(endpoint.first,RECT{std::max(0,x-150),std::max(0,y-110),std::min(960,x+150),std::min(640,y+110)});
        for (int lane=-1; lane<=1; ++lane) {
            const RuntimeCellPoint p{endpoint.second.x,endpoint.second.y+lane};
            const auto* t = terrain(p); if (!t) return fail("actual head side lane absent");
            const auto native = state.theater.resolveTileIndex(t->tileIndex);
            if (!native.validTileSet || native.expectedFileName.empty()) return fail("native head metadata unavailable");
            const auto projected = runtimeProjectCellXY(p.x,p.y,t->level,camera);
            heads << hutId << ',' << endpoint.first << ',' << lane << ',' << p.x << ',' << p.y << ',' << t->tileIndex
                << ',' << t->subTileIndex << ',' << t->level << ',' << native.expectedFileName << ','
                << projected.x-camera.worldOffsetX << ',' << projected.y-camera.worldOffsetY << '\n';
        }
    }
    heads.close(); if (!heads) return fail("native-head evidence write");
    std::ofstream draws(outputRoot/"draws.csv");
    if (!draws) return fail("draw evidence open");
    draws << std::setprecision(17);
    draws << "stage,phase,tick,focus_x,focus_y,focus_level,camera_x,camera_y,tile_width,tile_height,hits,misses,missing,frame_mismatch,tint_red,tint_green,tint_blue\n";
    const auto capture = [&](const std::string& label, RuntimeBridgeHeadSeamCapture& result) {
        result.width=960; result.height=640; result.pixels.clear(); result.terrainIdentity=terrainIdentity();
        for (const char* phase : {"cold","warm","rebuilt"}) {
            if (std::strcmp(phase,"warm") != 0) state.renderCache=RuntimeMapRenderCache{};
            std::vector<std::uint32_t> pixels;
            const auto stats = drawRuntimeStateOffscreenAtCell(state,focus,960,640,error,
                pathString(outputRoot/(label+"-"+phase+".bmp")),false,false,-1,&pixels,false,&camera);
            if (!stats || pixels.size()!=614400U) return fail(label+" actual draw: "+error);
            draws << label << ',' << phase << ',' << state.tick << ',' << focus.x << ',' << focus.y << ',' << focusLevel
                << ',' << camera.worldOffsetX << ',' << camera.worldOffsetY << ',' << camera.tileWidth << ',' << camera.tileHeight
                << ',' << stats->terrainLayerCacheHits << ',' << stats->terrainLayerCacheMisses << ','
                << stats->bridgeOverlayMissing << ',' << stats->bridgeOverlayRuntimeFrameMismatches << ','
                << state.lighting.renderTint().red << ',' << state.lighting.renderTint().green << ','
                << state.lighting.renderTint().blue << '\n';
            if (stats->bridgeOverlayMissing || stats->bridgeOverlayRuntimeFrameMismatches
                || (std::strcmp(phase,"warm")==0 ? stats->terrainLayerCacheHits<=0 : stats->terrainLayerCacheMisses<=0))
                return fail(label+" actual resource/frame/cache-path mismatch");
            if (std::strcmp(phase,"cold")==0) {
                const RuntimeTmpTileFile* file=nullptr; std::string resourceKey;
                const std::string suffix="|bridge.ubn";
                for (const auto& entry : state.renderCache.tmpTilesByPath) {
                    const auto key=toLowerAsciiLocal(entry.first);
                    if (key.size()<suffix.size() || key.compare(key.size()-suffix.size(),suffix.size(),suffix)!=0) continue;
                    if (file) return fail(label+" ambiguous actual BRIDGE.ubn resource");
                    file=&entry.second; resourceKey=entry.first;
                }
                if (!file || !file->loaded || file->images.empty()) return fail(label+" actual BRIDGE.ubn frame0 missing");
                const auto& image=file->images[0U];
                if (!image.present || image.width<=0 || image.height<=0
                    || image.pixels.size()!=static_cast<std::size_t>(image.width)*image.height
                    || !state.renderCache.paletteLoaded || state.renderCache.palette.size()!=256U)
                    return fail(label+" actual indexed frame0/palette incomplete");
                const auto writeBytes=[&](const std::filesystem::path& path,const unsigned char* data,std::size_t size) {
                    std::ofstream out(path,std::ios::binary); if (!out) return false;
                    out.write(reinterpret_cast<const char*>(data),static_cast<std::streamsize>(size));
                    out.close(); return static_cast<bool>(out);
                };
                std::vector<unsigned char> rgb; rgb.reserve(768U);
                for (const auto& c : state.renderCache.palette) { rgb.push_back(c.r); rgb.push_back(c.g); rgb.push_back(c.b); }
                if (!writeBytes(outputRoot/(label+"-bridge-frame0.pal8"),image.pixels.data(),image.pixels.size())
                    || !writeBytes(outputRoot/(label+"-bridge-palette.rgb8"),rgb.data(),rgb.size()))
                    return fail(label+" actual frame0/palette evidence write failed");
                const auto quote=[](const std::string& s) {
                    std::string result="\"";
                    for (char c:s) { result+=c; if (c=='"') result+='"'; }
                    return result+'"';
                };
                std::ofstream meta(outputRoot/(label+"-bridge-frame0-meta.csv"));
                if (!meta) return fail(label+" frame0 metadata open failed");
                meta << "resource_key,frame,width,height,global_width,global_height,offset_x,offset_y,pal8_bytes,palette_rgb8_bytes,palette_path,tint_red,tint_green,tint_blue\n";
                const auto tint=state.lighting.renderTint();
                meta << std::setprecision(17) << quote(resourceKey) << ",0," << image.width << ',' << image.height << ','
                    << image.globalWidth << ',' << image.globalHeight << ',' << image.offsetX << ',' << image.offsetY << ','
                    << image.pixels.size() << ',' << rgb.size() << ',' << quote(state.renderCache.loadedPalettePath) << ','
                    << tint.red << ',' << tint.green << ',' << tint.blue << '\n';
                meta.close(); if (!meta) return fail(label+" frame0 metadata write failed");
            }
            for (auto& p : pixels) p &= 0x00ffffffU;
            if (result.pixels.empty()) result.pixels=std::move(pixels);
            else if (pixels!=result.pixels) return fail(label+" same-tick cold/warm/rebuilt RGB changed");
            if (terrainIdentity()!=result.terrainIdentity) return fail("draw changed original terrain");
        }
        return true;
    };
    RuntimeBridgeHeadSeamCapture original;
    if (!capture("original",original)) return false;
    const auto damage = damageRuntimeBridgeAtCell(state,focus,state.rules.getBridgeStrength(),0);
    const auto* damaged = terrain(focus);
    if (!damage.hit || damage.healthRemaining!=0 || !damaged || damaged->hasOverlay
        || !runtimeBridgeRepairHutNeedsRepair(state,hutId) || corridor())
        return fail("real native midpoint damage did not produce the registered unpathable repair gap");
    RuntimeBridgeHeadSeamCapture broken;
    if (!capture("broken",broken)) return false;
    hut=findObjectById(state,hutId);
    const auto entry = hut ? findRuntimeStructureApproachCell(state,engineerProbe,*hut,"repair",2) : std::nullopt;
    if (!entry) return fail("actual CABHUT has no legal ENGINEER approach cell");
    auto* engineer=addRuntimeObjectInstance(state,RuntimeBuildKind::Infantry,"ENGINEER",*entry);
    if (!engineer) return fail("original ENGINEER spawn failed");
    const int engineerId=engineer->instanceId;
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    applySelection(state,{engineerId},false,false);
    hut=findObjectById(state,hutId);
    if (!hut || !queueRuntimeEngineerCapture(state,*hut)
        || !state.pendingEngineerActions.contains(engineerId)
        || state.pendingEngineerActions.at(engineerId)!=RuntimeEngineerAction::BridgeRepair)
        return fail("actual ENGINEER BridgeRepair command did not queue");
    int budget=360;
    const auto b=state.behaviors.find(engineerId);
    if (b!=state.behaviors.end()) {
        const auto route=findRuntimePath(state,*engineer,runtimeCellPointForObject(*engineer),b->second.moveTarget,hutId);
        RuntimeCellPoint previous=runtimeCellPointForObject(*engineer); int planned=64;
        for (const auto& next : route) { planned+=runtimeMovementSegmentIntervalTicks(state,*engineer,previous,next)+2; previous=next; }
        budget=std::clamp(planned,360,2400);
    }
    bool consumed=false;
    for (int n=0;n<budget;++n) {
        ++state.tick; advanceRuntimeSimulation(state);
        if (!findObjectById(state,engineerId)) { consumed=true; break; }
    }
    hut=findObjectById(state,hutId);
    if (!consumed || !intact() || !corridor() || !hut || hut->health!=hutHealth || hut->owner!=hutOwner
        || terrainIdentity()!=originalTerrain)
        return fail("natural ENGINEER did not consume/restore exact original path, frames, head or hut identity");
    if (!capture("repaired",repaired)) return false;
    const auto savePath=outputRoot/"repaired.ini";
    if (!saveRuntimeState(state,pathString(savePath),error)) return fail("actual save: "+error);
    IniFile actualSaved;
    if (!actualSaved.loadFromFile(pathString(savePath))) return fail("actual written save file could not be reread");
    const auto loadGap=damageRuntimeBridgeAtCell(state,focus,state.rules.getBridgeStrength(),0);
    if (!loadGap.hit || !runtimeBridgeRepairHutNeedsRepair(state,hutId)) return fail("real pre-load perturbation did not break selected span");
    if (!loadRuntimeState(state,pathString(savePath),error)) return fail("actual file load: "+error);
    hut=findObjectById(state,hutId);
    if (!intact() || !corridor() || !hut || hut->health!=hutHealth || hut->owner!=hutOwner
        || terrainIdentity()!=originalTerrain) return fail("actual file load lost original repaired span or hut identity");
    loaded.rois=repaired.rois;
    if (!capture("loaded",loaded) || !compareRuntimeBridgeHeadSeamAfterRealLoad(repaired,loaded,error)) return false;
    draws.close(); if (!draws) return fail("draw evidence write");
    std::cout << "Original high X bridge repair: actualCABHUT=" << hutId << " heads=" << span.startHead.x << ',' << span.startHead.y
        << "->" << span.endHead.x << ',' << span.endHead.y << " ENGINEERconsumed=yes actualFileLoad=yes"
        << " coldWarmRebuilt=yes headROI=exact nativeFrame0Oracle=external-required\n";
    return true;
}
