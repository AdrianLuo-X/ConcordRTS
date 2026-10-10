// Pixel oracle uses the original archive directly, independently of the
// compositor's cached opacity atlas and tile dependencies.
bool runRuntimeNativeShroudContractCheck(const RuntimeMapRenderContext& assets,
    std::string& error) {
    const auto fail = [&](const std::string& text) { error = "Native shroud: " + text; return false; };
    RuntimeMapRenderCache cache;
    if (!runtimeLoadNativeShroud(assets, cache)) return fail("original SHROUD.SHP did not load");
    std::vector<unsigned char> bytes; std::string source;
    if (!loadRuntimeResourceBytesFromMixArchives(assets.assetRootPath, "shroud.shp",
            cache, bytes, source, error)) return fail(error);
    std::array<RuntimeShpFrameImage, 48> original;
    for (int frame = 0; frame < 48; ++frame)
        if (!decodeRuntimeShpFrameFromBytes(bytes, source, frame, original[frame], error)) return fail(error);
    // Equivalent edge/corner decomposition, deliberately separate from the
    // retail 256-entry table used in production. Bits here follow cell X/Y.
    const auto expectedFrame = [](unsigned int bits) {
        const int edge = ((bits >> 7) & 1) | (bits & 2) | ((bits >> 1) & 4) | ((bits >> 2) & 8);
        const int corner = ((bits >> 6) & 1) | ((bits >> 3) & 2) | ((bits << 2) & 4) | ((bits << 1) & 8);
        constexpr int edges[] = {0,32,35,39,33,36,45,43,34,44,38,42,37,40,41,46};
        constexpr int masks[] = {0,5,12,13,10,15,14,15,3,7,15,15,11,15,15,15};
        if (edge == 0) return edges[corner];
        const int extra = corner & ~masks[edge];
        if (!extra) return edge;
        const std::pair<int,int> corners[] = {{24,16},{34,17},{50,18},{65,19},{97,20},
            {132,21},{152,22},{196,23},{18,24},{33,25},{68,26},{136,27},{26,28},{35,29},{69,30},{140,31}};
        for (const auto [key, value] : corners) if (key == (edge << 4) + extra) return value;
        return -1;
    };
    GameWorld visionWorld, renderWorld;
    for (int y = 9; y <= 11; ++y) for (int x = 9; x <= 11; ++x) {
        TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
        visionWorld.accessTerrainCells().push_back(cell);
        renderWorld.accessTerrainCells().push_back(cell);
    }
    RuntimeVisionSystem vision;
    vision.update(visionWorld, nullptr, {{"Observer", {"Observer"}}}, 1);
    RuntimeMapRenderContext context = assets;
    context.vision=&vision; context.visionObserverOwner="Observer";
    constexpr int width=120, height=60;
    context.surfaceWidth=width; context.surfaceHeight=height;
    const RECT viewport{0,0,width,height};
    RuntimeMapCamera camera; camera.worldOffsetX=-60; camera.worldOffsetY=270;
    std::vector<std::uint32_t> actual(width*height), expected(width*height);
    int samples=0; std::set<int> usedFrames;
    for (int unknown=0; unknown<2; ++unknown) for (unsigned int bits=0;bits<256;++bits) {
        std::vector<int> explored;
        if (!unknown) explored.push_back(10*512+10);
        const std::pair<int,int> offsets[] = {{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1}};
        for (unsigned int n=0;n<8;++n) if (!(bits & (1U<<n)))
            explored.push_back((10+offsets[n].second)*512+10+offsets[n].first);
        vision.resetShroud("Observer");
        vision.restoreExploredCellIndices("Observer",explored);
        const int frame=unknown?15:expectedFrame(bits);
        if (frame<0) return fail("reference neighbour case missing");
        usedFrames.insert(frame);
        for (std::size_t p=0;p<actual.size();++p) actual[p]=expected[p]=
            static_cast<std::uint32_t>((p*91673U+0x214587U)&0xffffffU);
        context.surfacePixels=actual.data(); RuntimeMapRenderStats stats;
        applyRuntimeVisionOverlay(nullptr,viewport,renderWorld,context,camera,cache,stats);
        for (int y=0;y<30;++y) for (int x=0;x<60;++x) {
            const unsigned int value=original[frame].pixels[y*60+x];
            if (value>=127) continue;
            const unsigned int keep=value<=2?0:((value-2)*255+62)/125;
            auto& pixel=expected[(y+15)*width+x+30];
            pixel=(((((pixel>>16)&255)*keep+127)/255)<<16)
                | (((((pixel>>8)&255)*keep+127)/255)<<8) | (((pixel&255)*keep+127)/255);
        }
        for (int y=0;y<30;++y) for (int x=0;x<60;++x) {
            if (original[frame].pixels[y*60+x]==254) continue;
            const int p=(y+15)*width+x+30;
            if (actual[p]!=expected[p] || !cache.visionOverlay.nativeShroudFingerprint)
                return fail("archive pixel mismatch: neighbours="+std::to_string(bits)+" unknown="+std::to_string(unknown));
        }
        ++samples;
    }
    if (usedFrames.size()!=47) return fail("not every original edge frame was exercised");
    std::cout << "Native shroud archive: frames="<<usedFrames.size()<<" neighbourCases="<<samples
        <<" source="<<source<<" pixels=exact\n";
    return true;
}
