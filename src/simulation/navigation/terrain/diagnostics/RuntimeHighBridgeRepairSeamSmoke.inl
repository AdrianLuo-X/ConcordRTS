// Test-only issue 23/11 capture supplement. Include after drawRuntimeStateOffscreenAtCell.
// Cache equality proves stability, not that a bridge joint is complete. Acceptance
// additionally requires external original-BRIDGE-frame9 seam pixels, never an
// after screenshot used as a golden image.
struct RuntimeBridgeHeadSeamCapture {
    int width = 960, height = 640;
    std::vector<std::uint32_t> pixels;
    std::string terrainIdentity;
    std::vector<std::pair<std::string, RECT>> rois;
};

bool captureRuntimeBridgeHeadSeamCheckpoint(
    RuntimeWindowState& state, const RuntimeBridgeRepairSpan& span,
    const std::string& label, const std::filesystem::path& outputRoot,
    RuntimeBridgeHeadSeamCapture& result, std::string& error
) {
    const auto fail = [&](const std::string& why) { error = "High bridge seam " + label + ": " + why; return false; };
    if (!span.high || span.cells.empty()) return fail("real high bridge span unavailable");
    // These are complete two-bank ROIs in the original 960x640 2peaks primary
    // view; they include both road lanes and both sidewalks. The external native
    // oracle determines the precise cross-road band within each recorded ROI.
    result = {};
    result.rois = {{"upper-joint", RECT{552,168,758,314}},
                   {"lower-joint", RECT{124,388,320,503}}};
    const auto terrainIdentity = [&]() {
        std::ostringstream text;
        for (const auto& c : state.world.getTerrainCells())
            text << c.x << ',' << c.y << ':' << c.tileIndex << '/' << c.subTileIndex
                 << '/' << c.level << '/' << c.hasOverlay << '/' << c.overlayIndex
                 << '/' << c.overlayFrame << '/' << c.overlayType << '/' << c.overlayImage << ';';
        return text.str();
    };
    result.terrainIdentity = terrainIdentity();
    std::error_code directoryError;
    std::filesystem::create_directories(outputRoot, directoryError);
    if (directoryError) return fail("evidence directory: " + directoryError.message());
    std::ofstream heads(outputRoot / (label + "-native-heads.csv"));
    if (!heads) return fail("native head evidence could not open");
    heads << "end,x,y,tile,subtile,level,native_resource,overlay,frame\n";
    for (const auto& endpoint : {std::pair{"start", span.startHead}, std::pair{"end", span.endHead}})
        for (int lane = -1; lane <= 1; ++lane) {
            const RuntimeCellPoint p{endpoint.second.x + (span.alongX ? 0 : lane),
                                     endpoint.second.y + (span.alongX ? lane : 0)};
            const auto it = std::find_if(state.world.getTerrainCells().begin(), state.world.getTerrainCells().end(),
                [&](const TerrainCell& c) { return c.x == p.x && c.y == p.y; });
            if (it == state.world.getTerrainCells().end()) return fail("real head side lane absent");
            const auto native = state.theater.resolveTileIndex(it->tileIndex);
            if (!native.validTileSet || native.expectedFileName.empty()) return fail("native head tile identity missing");
            heads << endpoint.first << ',' << p.x << ',' << p.y << ',' << it->tileIndex << ','
                  << it->subTileIndex << ',' << it->level << ',' << native.expectedFileName << ','
                  << it->overlayIndex << ',' << it->overlayFrame << '\n';
        }
    heads.close(); if (!heads) return fail("native head evidence write failed");
    std::ofstream roiFile(outputRoot / (label + "-rois.csv"));
    if (!roiFile) return fail("ROI evidence could not open");
    roiFile << "name,left,top,right,bottom,pixels\n";
    for (const auto& [name, r] : result.rois)
        roiFile << name << ',' << r.left << ',' << r.top << ',' << r.right << ',' << r.bottom << ','
                << (r.right-r.left)*(r.bottom-r.top) << '\n';
    roiFile.close(); if (!roiFile) return fail("ROI evidence write failed");
    const RuntimeCellPoint focus = span.cells[span.cells.size()/2U].point;
    const int focusLevel = terrainLevelAtRuntimeCell(state, focus.x, focus.y);
    RuntimeMapCamera camera;
    camera.initialized = true;
    const auto projectedFocus = runtimeProjectCellXY(focus.x, focus.y, focusLevel, camera);
    camera.worldOffsetX = projectedFocus.x - result.width/2;
    camera.worldOffsetY = projectedFocus.y - result.height/2;
    std::ofstream renderEvidence(outputRoot / (label + "-render.csv"));
    if (!renderEvidence) return fail("render evidence could not open");
    renderEvidence << "phase,tick,focus_x,focus_y,focus_level,projected_x,projected_y,camera_offset_x,camera_offset_y,tile_width,tile_height,viewport_left,viewport_top,viewport_right,viewport_bottom,terrain_cache_hits,terrain_cache_misses\n";
    for (const char* phase : {"cold", "warm", "rebuilt"}) {
        if (std::strcmp(phase,"warm") != 0) state.renderCache = RuntimeMapRenderCache{};
        std::vector<std::uint32_t> pixels;
        const auto bmp = outputRoot / (label + "-" + phase + ".bmp");
        const auto stats = drawRuntimeStateOffscreenAtCell(state, focus, result.width, result.height,
            error, pathString(bmp), false, false, -1, &pixels, false, &camera);
        if (!stats || pixels.size() != static_cast<std::size_t>(result.width)*result.height)
            return fail("real " + std::string(phase) + " draw: " + error);
        if (stats->bridgeOverlayMissing || stats->bridgeOverlayRuntimeFrameMismatches)
            return fail("real bridge resources/frame mismatched during " + std::string(phase));
        renderEvidence << phase << ',' << state.tick << ',' << focus.x << ',' << focus.y << ','
            << focusLevel << ',' << projectedFocus.x << ',' << projectedFocus.y << ','
            << camera.worldOffsetX << ',' << camera.worldOffsetY << ',' << camera.tileWidth << ','
            << camera.tileHeight << ",0,0," << result.width << ',' << result.height << ','
            << stats->terrainLayerCacheHits << ',' << stats->terrainLayerCacheMisses << '\n';
        if (std::strcmp(phase,"warm") == 0 ? stats->terrainLayerCacheHits <= 0
                                          : stats->terrainLayerCacheMisses <= 0)
            return fail(std::string(phase) + " did not exercise the required terrain cache path");
        if (std::strcmp(phase,"cold") == 0) {
            const RuntimeTmpTileFile* bridgeFile = nullptr;
            std::string bridgeKey;
            const std::string suffix = "|bridge.tem";
            for (const auto& entry : state.renderCache.tmpTilesByPath) {
                const std::string key = toLowerAsciiLocal(entry.first);
                if (key.size() < suffix.size()
                    || key.compare(key.size()-suffix.size(), suffix.size(), suffix) != 0) continue;
                if (bridgeFile != nullptr) return fail("ambiguous actual BRIDGE.tem cache resource");
                bridgeFile = &entry.second;
                bridgeKey = entry.first;
            }
            if (bridgeFile == nullptr || !bridgeFile->loaded || bridgeFile->images.size() <= 9U)
                return fail("actual loaded BRIDGE.tem frame9 cache resource absent");
            const auto& image = bridgeFile->images[9U];
            if (!image.present || image.width <= 0 || image.height <= 0
                || image.pixels.size() != static_cast<std::size_t>(image.width)*image.height
                || !state.renderCache.paletteLoaded || state.renderCache.palette.size() != 256U)
                return fail("actual frame9 indexed pixels or original 256-color palette incomplete");
            const auto writeBytes = [&](const std::filesystem::path& path,
                                        const unsigned char* data, std::size_t size) {
                std::ofstream file(path, std::ios::binary);
                if (!file) return false;
                file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
                file.close(); return static_cast<bool>(file);
            };
            std::vector<unsigned char> rgb;
            rgb.reserve(768U);
            for (const auto& c : state.renderCache.palette) {
                rgb.push_back(c.r); rgb.push_back(c.g); rgb.push_back(c.b);
            }
            if (!writeBytes(outputRoot/(label+"-bridge-frame9.pal8"), image.pixels.data(), image.pixels.size())
                || !writeBytes(outputRoot/(label+"-bridge-palette.rgb8"), rgb.data(), rgb.size()))
                return fail("actual native frame9/palette evidence write failed");
            const auto csvQuote = [](const std::string& text) {
                std::string out = "\"";
                for (char c : text) { out += c; if (c == '"') out += '"'; }
                return out + '"';
            };
            std::ofstream nativeMeta(outputRoot/(label+"-bridge-frame9-meta.csv"));
            if (!nativeMeta) return fail("actual frame9 metadata could not open");
            nativeMeta << "resource_key,frame,width,height,global_width,global_height,offset_x,offset_y,pal8_bytes,palette_rgb8_bytes,palette_path\n";
            nativeMeta << csvQuote(bridgeKey) << ",9," << image.width << ',' << image.height << ','
                << image.globalWidth << ',' << image.globalHeight << ',' << image.offsetX << ','
                << image.offsetY << ',' << image.pixels.size() << ',' << rgb.size() << ','
                << csvQuote(state.renderCache.loadedPalettePath) << '\n';
            nativeMeta.close(); if (!nativeMeta) return fail("actual frame9 metadata write failed");
        }
        for (auto& p : pixels) p &= 0x00ffffffU; // GDI alpha is not a color channel.
        if (result.pixels.empty()) result.pixels = std::move(pixels);
        else if (pixels != result.pixels) {
            const auto mismatch = std::mismatch(pixels.begin(),pixels.end(),result.pixels.begin());
            const auto index = static_cast<std::size_t>(mismatch.first-pixels.begin());
            return fail(std::string(phase) + " changed same-tick pixels at "
                + std::to_string(index%result.width) + "," + std::to_string(index/result.width));
        }
        if (terrainIdentity() != result.terrainIdentity) return fail("paint mutated native terrain identity");
    }
    renderEvidence.close(); if (!renderEvidence) return fail("render evidence write failed");
    return true; // capture/cache contract only; external native seam oracle remains mandatory.
}

bool compareRuntimeBridgeHeadSeamAfterRealLoad(
    const RuntimeBridgeHeadSeamCapture& before, const RuntimeBridgeHeadSeamCapture& after,
    std::string& error
) {
    const auto fail = [&](const std::string& why) { error="High bridge seam real load: "+why; return false; };
    if (before.width!=after.width || before.height!=after.height || before.pixels.empty()
        || before.pixels.size()!=after.pixels.size() || before.terrainIdentity!=after.terrainIdentity)
        return fail("dimensions or original tile/subtile/level/overlay identity changed");
    if (before.rois.size()!=after.rois.size()) return fail("ROI catalogue changed");
    for (std::size_t n=0;n<before.rois.size();++n) {
        const auto& a=before.rois[n]; const auto& b=after.rois[n];
        if (a.first!=b.first || a.second.left!=b.second.left || a.second.top!=b.second.top
            || a.second.right!=b.second.right || a.second.bottom!=b.second.bottom) return fail("ROI bounds changed");
        const RECT r=a.second;
        if (r.left<0 || r.top<0 || r.right>before.width || r.bottom>before.height
            || r.right<=r.left || r.bottom<=r.top) return fail("empty/out-of-bounds full seam ROI");
        for (int y=r.top;y<r.bottom;++y) for (int x=r.left;x<r.right;++x) {
            const auto index=static_cast<std::size_t>(y)*before.width+x;
            if (before.pixels[index]!=after.pixels[index])
                return fail(a.first+" differs at "+std::to_string(x)+","+std::to_string(y));
        }
    }
    return true;
}
