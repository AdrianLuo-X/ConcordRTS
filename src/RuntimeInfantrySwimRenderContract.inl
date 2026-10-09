bool runRuntimeInfantrySwimRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& error
) {
    const auto fail = [&](const std::string& message) {
        error = "infantry swim: " + message;
        return false;
    };
    if (context.art == nullptr || context.rules == nullptr) return fail("resources missing");
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    RuntimeMapRenderContext probe = context;
    probe.vision = nullptr;
    probe.specialMechanismSnapshots = nullptr;
    probe.fixedTickHz = 30;
    probe.animationTick = 0;
    MapFacts facts;
    for (int x = 0; x < 2; ++x) {
        MapCellFacts cell;
        cell.x = x;
        cell.y = 0;
        cell.cellIndex = x;
        cell.isWater = x == 0;
        facts.cellIndexLookup.emplace(x, facts.cells.size());
        facts.cells.push_back(cell);
    }
    probe.mapFacts = &facts;

    // Exercise real TMP metadata when this theater supplies water/shore assets.
    // The lunar theater has no water; the state/asset checks below still apply.
    int waterTile = -1, waterSubTile = -1, shoreTile = -1, shoreSubTile = -1;
    if (context.theater != nullptr) {
        for (const auto& set : context.theater->getTileSets()) {
            const std::string name = toLowerAscii(set.fileName + " " + set.setName);
            if (name.find("shore") == std::string::npos && name.find("water") == std::string::npos)
                continue;
            for (int tile = set.startTileIndex; tile < set.startTileIndex + set.tilesInSet; ++tile) {
                const auto* file = loadRuntimeTerrainTileForIndex(tile, probe, *cache);
                if (file == nullptr) continue;
                for (std::size_t sub = 0; sub < file->images.size(); ++sub) {
                    const auto& image = file->images[sub];
                    if (!image.present) continue;
                    const int land = runtimeLandTypeForTmpImage(&image);
                    if (land == RuntimeLandTypeWater && waterTile < 0) {
                        waterTile = tile;
                        waterSubTile = static_cast<int>(sub);
                    } else if (land >= 0 && land != RuntimeLandTypeWater
                        && name.find("shore") != std::string::npos && shoreTile < 0) {
                        shoreTile = tile;
                        shoreSubTile = static_cast<int>(sub);
                    }
                }
                if (waterTile >= 0 && shoreTile >= 0) break;
            }
            if (waterTile >= 0 && shoreTile >= 0) break;
        }
    }
    if (waterTile >= 0 && shoreTile >= 0) {
        for (int x = 0; x < 2; ++x) {
            auto& cell = facts.cells[x];
            cell.hasTerrain = true;
            cell.terrain.tileIndex = x == 0 ? waterTile : shoreTile;
            cell.terrain.subTileIndex = x == 0 ? waterSubTile : shoreSubTile;
            // The coarse flags intentionally cannot distinguish these cells.
            cell.isWater = true;
            cell.isShore = true;
        }
    }

    WorldObjectInstance object;
    object.instanceId = 1;
    object.kind = WorldObjectKind::Infantry;
    object.type = "GHOST";
    object.owner = "Americans";
    object.health = 100;
    object.alive = true;
    object.facing = 0;
    object.x = object.y = 0;
    object.hasRenderPosition = true;
    object.renderX = object.renderY = 0;
    object.renderMoveActive = true;
    object.mission = "Move";
    const auto select = [&]() {
        return loadRuntimeObjectShpSpriteFromCache(object, probe, *cache);
    };
    const auto expects = [&](const char* name) {
        const auto* sprite = select();
        if (sprite == nullptr || !sprite->usedSequenceFrame || sprite->sequenceName != name) {
            error = "infantry swim: " + object.type + " expected " + name + " got "
                + (sprite == nullptr ? "missing SHP" : sprite->sequenceName);
            return false;
        }
        return true;
    };
    int checkedFrames = 0;
    for (const char* type : {"GHOST", "TANY"}) {
        object.type = type;
        const auto* art = findArtTypeForObject(object, probe);
        if (art == nullptr) return fail(object.type + " art missing");
        const int start = object.type == "GHOST" ? 388 : 506;
        const auto* swim = findRuntimeSequenceFrame(*art, {"Swim"});
        if (swim == nullptr || swim->startFrame != start || swim->frameCount != 6
            || swim->facingMultiplier != 6) return fail(object.type + " original Swim strip missing");
        for (int facing = 0; facing < 8; ++facing) {
            object.facing = facing * 32;
            for (int frame = 0; frame < 6; ++frame) {
                object.movementAnimationDistance = (frame + 0.1) / 6.0;
                if (!expects("Swim")) return false;
                const int expected = start + ((8 - facing) % 8) * 6 + frame;
                const auto* sprite = select();
                if (sprite->frameIndex != expected || sprite->image.pixels.empty())
                    return fail(object.type + " swimming direction/phase incorrect");
                ++checkedFrames;
            }
        }
    }
    object.type = "GHOST";
    object.facing = 64;
    object.movementAnimationDistance = 0.25;
    // Reuse one cache while the logical destination is ahead of the visible body.
    object.x = 1;
    object.renderX = 0.49;
    if (!expects("Swim")) return false;
    object.renderX = 0.51;
    if (!expects("Walk")) return false;
    object.x = 0;
    object.renderX = 0.51;
    if (!expects("Walk")) return false;
    object.renderX = 0.49;
    if (!expects("Swim")) return false;
    object.renderX = 0;
    object.onBridge = true;
    if (!expects("Swim")) return false;
    object.renderOnBridge = true;
    if (!expects("Walk")) return false;
    object.hasRenderPosition = false;
    if (!expects("Ready")) return false;
    object.onBridge = false;
    if (!expects("Tread")) return false;
    object.hasRenderPosition = true;
    object.renderOnBridge = false;
    facts.cells[0].isBridge = true; // A swimmer can be below a bridge or its destroyed span.
    if (!expects("Swim")) return false;
    facts.cells[0].isBridge = false;
    object.prone = true;
    if (!expects("Swim")) return false;
    object.renderX = 1;
    if (!expects("Crawl")) return false;
    object.prone = false;
    if (!expects("Walk")) return false;
    object.renderX = 0;
    object.mission = "Attack";
    if (!expects("Swim")) return false;
    object.renderMoveActive = false;
    if (!expects("WetAttack")) return false;
    object.mission = "SecondaryFire";
    if (!expects("WetAttack")) return false;
    object.mission = "Idle1";
    if (!expects("WetIdle1")) return false;
    object.mission = "Idle2";
    if (!expects("WetIdle2")) return false;
    object.mission = "Guard";
    if (!expects("Tread")) return false;
    const int restingFrame = select()->frameIndex;
    if (select()->frameIndex != restingFrame) return fail("same simulation tick changed tread pose");
    probe.animationTick = 6;
    const int nextFrame = select()->frameIndex;
    if (nextFrame == restingFrame) return fail("treading water never animated");
    probe.fixedTickHz = 60;
    probe.animationTick = 12;
    if (select()->frameIndex != nextFrame) return fail("tread speed depends on simulation frequency");
    object.mission = "Temporal";
    const int frozenFrame = select()->frameIndex;
    probe.animationTick = 999;
    if (select()->frameIndex != frozenFrame) return fail("frozen swimmer kept treading");
    probe.fixedTickHz = 30;
    probe.animationTick = 0;
    object.mission = "Move";
    object.renderMoveActive = true;
    if (!expects("Swim")) return false;
    const auto cachedFrame = select()->frameIndex;
    const auto* art = findArtTypeForObject(object, probe);
    const int visibleFrames = runtimeVisibleMainFrameCount(
        select()->frameCount, runtimeObjectSourceHasSplitShadow(object, probe));
    if (selectRuntimeObjectShpFrame(object, probe, visibleFrames) != cachedFrame)
        return fail("uncached terrain selection differs from production cache");
    object.type = "E1";
    if (!expects("Walk")) return false;
    object.type = "JUMPJET";
    object.airborne = true;
    if (!expects("Fly")) return false;
    object.airborne = false;
    object.type = "GHOST";

    // Draw actual authored frames through the production palette/remap path.
    constexpr int panelWidth = 120, panelHeight = 108;
    constexpr int width = panelWidth * 6, height = panelHeight * 6;
    constexpr std::uint32_t background = 0x002c403cU;
    std::vector<std::uint32_t> pixels(width * height, background);
    probe.surfacePixels = pixels.data();
    probe.surfaceWidth = width;
    probe.surfaceHeight = height;
    RuntimeMapCamera camera;
    camera.initialized = true;
    const char* missions[] = {"Move", "Move", "Guard", "Attack", "Idle1", "Idle2"};
    const char* sequences[] = {"Walk", "Swim", "Tread", "WetAttack", "WetIdle1", "WetIdle2"};
    for (int row = 0; row < 6; ++row) {
        object.renderX = row == 0 ? 1 : 0;
        object.mission = missions[row];
        object.renderMoveActive = row < 2;
        std::vector<std::uint64_t> hashes;
        for (int column = 0; column < 6; ++column) {
            object.movementAnimationDistance = (column + 0.1) / 6.0;
            probe.animationTick = column * 6;
            if (!expects(sequences[row])) return false;
            const RECT viewport{column * panelWidth, row * panelHeight,
                (column + 1) * panelWidth, (row + 1) * panelHeight};
            const POINT center{viewport.left + panelWidth / 2, viewport.top + 80};
            if (!tryDrawObjectShpSprite(nullptr, viewport, object, center, probe, camera, *cache))
                return fail("original SEAL frame could not be drawn");
            std::uint64_t hash = 1469598103934665603ULL;
            int painted = 0;
            for (int y = viewport.top; y < viewport.bottom; ++y)
                for (int x = viewport.left; x < viewport.right; ++x) {
                    const auto pixel = pixels[y * width + x];
                    hash ^= pixel;
                    hash *= 1099511628211ULL;
                    painted += pixel != background;
                }
            if (painted == 0) return fail("original SEAL frame produced no pixels");
            hashes.push_back(hash);
        }
        if (std::all_of(hashes.begin(), hashes.end(), [&](auto hash) { return hash == hashes.front(); }))
            return fail(std::string(sequences[row]) + " produced identical pixels in every phase");
    }
    if (const char* path = std::getenv("CONCORD_INFANTRY_SWIM_BMP")) {
        if (*path != '\0' && !writeRuntimeBgraBmp(path, width, height, pixels)) return fail("preview write failed");
    }
    if (const char* dir = std::getenv("CONCORD_TANYA_SELECTION_DIR"); dir && *dir) {
        constexpr int tanyaHeight = panelHeight * 8;
        std::vector<std::uint32_t> tanyaPixels(width*tanyaHeight,background);
        probe.surfacePixels=tanyaPixels.data();probe.surfaceHeight=tanyaHeight;
        object.type="TANY";object.mission="Guard";object.renderMoveActive=false;
        object.x=object.y=0;object.renderX=object.renderY=0;object.facing=0;
        object.hasRenderPosition=false;object.onBridge=false;object.prone=false;
        const auto* tanyaArt=findArtTypeForObject(object,probe);
        for(int facing=0;facing<8;++facing) {
            object.facing=facing*32;
            std::cout << "Tanya Tread facing=" << facing << " frames=";
            for(int frame=0;frame<6;++frame) {
                probe.animationTick=frame*runtimeInfantrySequenceDelayTicks(tanyaArt, probe.fixedTickHz);
                if(!expects("Tread"))return false;
                const auto* sprite=select();std::cout << sprite->frameIndex << ',';
                const RECT view{frame*panelWidth,facing*panelHeight,(frame+1)*panelWidth,(facing+1)*panelHeight};
                const POINT center{view.left+panelWidth/2,view.top+80};
                if(!tryDrawObjectShpSprite(nullptr,view,object,center,probe,camera,*cache))return fail("Tanya tread pixels");
            }
            std::cout << '\n';
        }
        if(!writeRuntimeBgraBmp(std::string(dir)+"/tanya-tread-frames.bmp",width,tanyaHeight,tanyaPixels))
            return fail("Tanya tread preview write");
        constexpr int idleWidth=120*15,idleHeight=108*2;
        std::vector<std::uint32_t> idlePixels(idleWidth*idleHeight,background);
        probe.surfacePixels=idlePixels.data();probe.surfaceWidth=idleWidth;probe.surfaceHeight=idleHeight;
        object.facing=128;
        for(int row=0;row<2;++row) {
            object.mission=row==0?"Idle1":"Idle2";
            std::cout << "Tanya " << object.mission << " frames=";
            for(int frame=0;frame<15;++frame) {
                probe.animationTick=frame*runtimeInfantrySequenceDelayTicks(tanyaArt, probe.fixedTickHz);
                const auto* sprite=select();std::cout << sprite->frameIndex << ',';
                const RECT view{frame*120,row*108,(frame+1)*120,(row+1)*108};
                const POINT center{view.left+60,view.top+80};
                if(!tryDrawObjectShpSprite(nullptr,view,object,center,probe,camera,*cache))return fail("Tanya idle pixels");
            }
            std::cout << '\n';
        }
        if(!writeRuntimeBgraBmp(std::string(dir)+"/tanya-wet-idle-frames.bmp",idleWidth,idleHeight,idlePixels))
            return fail("Tanya wet idle preview write");
    }
    std::cout << "Infantry swim: GHOST/TANY directionalFrames=" << checkedFrames
        << " wetPoses=ok shoreline=ok bridge=ok cache=ok fixedTickHz=ok frozen=ok pixels=ok shoreTMP="
        << (waterTile >= 0 && shoreTile >= 0 ? "ok" : "not-authored") << '\n';
    return true;
}
