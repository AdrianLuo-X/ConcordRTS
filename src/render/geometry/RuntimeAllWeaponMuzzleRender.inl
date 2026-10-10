// Include after ordinary object/projectile render helpers. Every cell uses
// the native SHP/VXL asset, with a cross at the recorded discharge coordinate.
bool runRuntimeAllWeaponMuzzleRenderContract(
    const RuntimeMapRenderContext& context, const GameWorld& world,
    const std::vector<RuntimeProjectileRenderActor>& actors,
    const std::vector<std::string>& labels,
    const std::vector<std::string>& expectedSequences,
    const std::string& root, std::string& error) {
    if (actors.size() != world.getObjects().size() || actors.size() != labels.size()
        || actors.size() != expectedSequences.size()) {
        error = "all muzzle render: mismatched audit inputs"; return false;
    }
    constexpr int tileWidth = 192, tileHeight = 168, columns = 8, rows = 8;
    constexpr int pageWidth = tileWidth * columns, pageHeight = tileHeight * rows;
    constexpr std::uint32_t background = 0x00242c30U;
    std::vector<std::uint32_t> pixels(tileWidth * tileHeight, background);
    std::vector<std::uint32_t> sheet(pageWidth * pageHeight, background);
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    auto probe = context; probe.vision = nullptr;
    probe.surfacePixels = pixels.data(); probe.surfaceWidth = tileWidth; probe.surfaceHeight = tileHeight;
    probe.animationTick = 0;
    RuntimeMapCamera camera; camera.initialized = true;
    const RECT viewport{0, 0, tileWidth, tileHeight};
    std::vector<std::string> failures;
    std::ofstream details, index;
    if (!root.empty()) {
        details.open(root + "/render-frames.csv");
        index.open(root + "/index.html");
        if (!details || !index) { error = "all muzzle render: output unavailable"; return false; }
        details << "case,label,page,row,column,image,frame,sequence,expectedSequence,muzzleX,muzzleY,bodyPixels,bodyDrawn\n";
        index << "<!doctype html><meta charset=\"utf-8\"><title>Native weapon muzzle audit</title>"
            "<style>body{font:14px sans-serif;background:#182024;color:#e6ecef}img{max-width:100%}"
            "figure{margin:24px 0}pre{line-height:1.5}</style><h1>Native weapon muzzle audit</h1>"
            "<p>Each row has eight clockwise directions. White cross: recorded projectile source. "
            "The CSV maps each cell to its type, weapon, rank, pose and native source asset.</p>"
            "<p><a href=\"weapons.csv\">Weapon inventory</a> | <a href=\"render-frames.csv\">Rendered frames</a></p>";
    }
    int bodyDraws = 0, sequenceChecks = 0, attachmentChecks = 0, zoomChecks = 0, pages = 0;
    const auto writePage = [&](std::size_t last) {
        if (root.empty()) return true;
        const std::string name = "muzzles-" + std::to_string(pages) + ".png";
        if (!ensureRuntimeGdiplusStarted()) return false;
        const CLSID pngEncoder{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
        Gdiplus::Bitmap image(pageWidth, pageHeight, pageWidth * 4, PixelFormat32bppRGB,
            reinterpret_cast<BYTE*>(sheet.data()));
        if (image.Save(std::filesystem::path(root + "/" + name).wstring().c_str(), &pngEncoder, nullptr) != Gdiplus::Ok)
            return false;
        const std::size_t first = static_cast<std::size_t>(pages) * columns * rows;
        index << "<figure><figcaption>Page " << pages << ", cases " << first << "-" << last
            << "</figcaption><img loading=\"lazy\" src=\"" << name << "\"><pre>";
        for (std::size_t row = first; row <= last; row += columns)
            index << "row " << ((row - first) / columns) << ": " << labels[row] << '\n';
        index << "</pre></figure>";
        return true;
    };
    for (std::size_t i = 0; i < actors.size(); ++i) {
        const auto& object = world.getObjects()[i];
        std::fill(pixels.begin(), pixels.end(), background);
        camera.worldOffsetX = camera.worldOffsetY = 0;
        GameWorld scene; scene.accessObjects().push_back(object);
        const auto anchor = runtimeObjectWorldRenderPlacement(object, scene, viewport, probe, camera, *cache).center;
        camera.worldOffsetX = anchor.x - tileWidth / 2;
        camera.worldOffsetY = anchor.y - 110;
        const auto preliminaryMuzzle = runtimeProjectileActorScreenPoint(actors[i], viewport, camera);
        if (preliminaryMuzzle.y < 24) camera.worldOffsetY -= 24 - preliminaryMuzzle.y;
        if (preliminaryMuzzle.x < 16) camera.worldOffsetX -= 16 - preliminaryMuzzle.x;
        else if (preliminaryMuzzle.x >= tileWidth - 16) camera.worldOffsetX += preliminaryMuzzle.x - tileWidth + 17;
        const POINT center = runtimeObjectWorldRenderPlacement(object, scene, viewport, probe, camera, *cache).center;
        bool drawn = false;
        RECT bounds{};
        if (object.kind == WorldObjectKind::Structure)
            drawn = drawRuntimeObjectPreviewSprite(nullptr, viewport, object, probe, camera, *cache, &bounds);
        else if (objectUsesVoxelArt(object, probe))
            drawn = tryDrawVoxelSprite(nullptr, viewport, object, center, probe, camera, *cache);
        else drawn = tryDrawObjectShpSprite(nullptr, viewport, object, center, probe, camera, *cache,
            nullptr, &bounds);
        const int bodyPixels = static_cast<int>(std::count_if(pixels.begin(), pixels.end(),
            [](std::uint32_t value) { return value != background; }));
        const bool weaponRegistrationOnly = object.type == "WEEDGUY" || object.type == "DeathDummy";
        if ((!drawn || bodyPixels == 0) && !weaponRegistrationOnly)
            failures.push_back(labels[i] + ": native body unavailable");
        else if (drawn && bodyPixels > 0) ++bodyDraws;
        RuntimeObjectShpSpriteCacheEntry* sprite = nullptr;
        if (!objectUsesVoxelArt(object, probe)) sprite = loadRuntimeObjectShpSpriteFromCache(object, probe, *cache);
        if (!expectedSequences[i].empty()) {
            ++sequenceChecks;
            if (!sprite || !sprite->loaded || toLowerAscii(sprite->sequenceName) != toLowerAscii(expectedSequences[i]))
                failures.push_back(labels[i] + ": wrong native firing sequence; expected=" + expectedSequences[i]
                    + " actual=" + (sprite ? sprite->sequenceName : "unavailable"));
            else if (object.kind == WorldObjectKind::Infantry) {
                // The reviewed canvas coordinates are an independent native
                // image annotation, not another call to the discharge helper.
                const auto& frame = sprite->image;
                const int width = frame.globalWidth > 0 ? frame.globalWidth : frame.width;
                const int height = frame.globalHeight > 0 ? frame.globalHeight : frame.height;
                POINT authored{};
                if (runtimeFindInfantryMuzzleAnchor(sprite->resolvedImageId, sprite->sequenceName,
                        sprite->frameIndex, width, height, authored)) {
                    const POINT expected{center.x-width/2+authored.x,
                        center.y+camera.tileHeight/2-height+authored.y};
                    const auto actual = runtimeProjectileActorScreenPoint(actors[i], viewport, camera);
                    if (std::abs(actual.x-expected.x)>1 || std::abs(actual.y-expected.y)>1)
                        failures.push_back(labels[i] + ": discharge differs from reviewed native canvas gun point");
                }
                ++attachmentChecks;
            }
        }
        // Native bitmap points do not scale with the map tiles. This oracle
        // projects the reviewed full-canvas annotation afresh at each zoom,
        // independently of the two-reference capture/decay calculation.
        if (sprite && sprite->loaded) {
            const auto& frame = sprite->image;
            const int width = frame.globalWidth > 0 ? frame.globalWidth : frame.width;
            const int height = frame.globalHeight > 0 ? frame.globalHeight : frame.height;
            POINT annotation{};
            const bool infantry = object.kind == WorldObjectKind::Infantry
                && runtimeFindInfantryMuzzleAnchor(sprite->resolvedImageId, sprite->sequenceName,
                    sprite->frameIndex, width, height, annotation);
            const bool marine = object.kind == WorldObjectKind::Unit
                && runtimeFindMarineMuzzleAnchor(sprite->resolvedImageId, sprite->frameIndex,
                    width, height, annotation);
            if (infantry || marine) for (int zoom : {48, 60, 72}) {
                auto zoomCamera = camera; zoomCamera.tileWidth = zoom; zoomCamera.tileHeight = zoom / 2;
                const auto placement = runtimeObjectWorldRenderPlacement(object, scene, viewport,
                    probe, zoomCamera, *cache);
                POINT expected{placement.center.x - width / 2 + annotation.x,
                    placement.center.y + zoomCamera.tileHeight / 2 - height + annotation.y};
                if (marine) expected.y += runtimeUnderwaterShpAlignmentOffsetY(object, probe, zoomCamera, frame);
                const auto actual = runtimeProjectileActorScreenPoint(actors[i], viewport, zoomCamera);
                if (std::abs(actual.x - expected.x) > 1 || std::abs(actual.y - expected.y) > 1)
                    failures.push_back(labels[i] + ": native bitmap muzzle zoom=" + std::to_string(zoom)
                        + " delta=" + std::to_string(actual.x - expected.x) + "," + std::to_string(actual.y - expected.y));
                ++zoomChecks;
            }
        }
        if (object.kind == WorldObjectKind::Structure && probe.rules && probe.art
            && (object.type=="GAPILL" || object.type=="ATESLA" || object.type=="MAYAN"
                || object.type=="YAPSYT" || object.type=="CAEAST01")) {
            const auto* building=probe.rules->findBuildingType(object.type);
            const auto* art=sprite?probe.art->findArtType(sprite->resolvedImageId):nullptr;
            if (!art && building) art=probe.art->findArtType(building->image.empty()?object.type:building->image);
            if (art && building) {
                const bool secondary=actors[i].weaponId==building->secondaryWeapon
                    || (!building->eliteSecondaryWeapon.empty() && actors[i].weaponId==building->eliteSecondaryWeapon);
                auto tuple=secondary?art->secondaryFireFLH:art->primaryFireFLH;
                const auto& elite=secondary?art->eliteSecondaryFireFLH:art->elitePrimaryFireFLH;
                if (object.veterancy>=2 && !elite.empty()) tuple=elite;
                // Native secondary slots inherit the primary FLH when no
                // separate attachment was authored (PrismSupport is one).
                if (tuple.empty()) {
                    const auto& alternateElite=secondary?art->elitePrimaryFireFLH:art->eliteSecondaryFireFLH;
                    tuple=object.veterancy>=2 && !alternateElite.empty()?alternateElite
                        :secondary?art->primaryFireFLH:art->secondaryFireFLH;
                }
                std::replace(tuple.begin(),tuple.end(),',',' ');
                int forward=0,lateral=0,height=0;std::istringstream fields(tuple);fields>>forward>>lateral>>height;
                for (int zoom : {48,60,72}) {
                    auto zoomCamera=camera;zoomCamera.tileWidth=zoom;zoomCamera.tileHeight=zoom/2;
                    const auto placement=runtimeObjectWorldRenderPlacement(object,scene,viewport,probe,zoomCamera,*cache);
                    POINT physical{};
                    const bool resolved=resolveRuntimeStructureWeaponMuzzlePoint(object,placement.center,probe,
                        zoomCamera,*cache,forward,lateral,height,actors[i].facing,secondary,physical);
                    const auto actual=runtimeProjectileActorScreenPoint(actors[i],viewport,zoomCamera);
                    if (!resolved || std::abs(actual.x-physical.x)>1 || std::abs(actual.y-physical.y)>1)
                        failures.push_back(labels[i]+": structure native muzzle zoom="+std::to_string(zoom)
                            +" delta="+std::to_string(actual.x-physical.x)+","+std::to_string(actual.y-physical.y));
                    ++zoomChecks;
                }
            } else failures.push_back(labels[i]+": structure muzzle art missing");
        }
        const POINT muzzle = runtimeProjectileActorScreenPoint(actors[i], viewport, camera);
        std::vector<RuntimeProjectileRenderActor> one{actors[i]};
        probe.projectileActors = &one;
        RuntimeMapRenderStats stats;
        drawRuntimeProjectileActors(nullptr, viewport, probe, camera, *cache, stats, &world);
        for (int d = -3; d <= 3; ++d) {
            if (muzzle.y >= 0 && muzzle.y < tileHeight && muzzle.x + d >= 0 && muzzle.x + d < tileWidth)
                pixels[muzzle.y * tileWidth + muzzle.x + d] = 0x00ffffffU;
            if (muzzle.x >= 0 && muzzle.x < tileWidth && muzzle.y + d >= 0 && muzzle.y + d < tileHeight)
                pixels[(muzzle.y + d) * tileWidth + muzzle.x] = 0x00ffffffU;
        }
        const int col = static_cast<int>(i % columns), row = static_cast<int>((i / columns) % rows);
        for (int y = 0; y < tileHeight; ++y)
            std::copy_n(pixels.begin() + y * tileWidth, tileWidth,
                sheet.begin() + (row * tileHeight + y) * pageWidth + col * tileWidth);
        if (details) details << i << ',' << labels[i] << ',' << pages << ',' << row << ',' << col << ','
            << (sprite ? sprite->resolvedImageId : object.type) << ',' << (sprite ? sprite->frameIndex : -1)
            << ',' << (sprite ? sprite->sequenceName : "voxel") << ',' << expectedSequences[i]
            << ',' << muzzle.x << ',' << muzzle.y << ',' << bodyPixels << ',' << (drawn ? 1 : 0) << '\n';
        if ((i + 1) % (columns * rows) == 0 || i + 1 == actors.size()) {
            if (!writePage(i)) { error = "all muzzle render: atlas write failed"; return false; }
            ++pages; std::fill(sheet.begin(), sheet.end(), background);
        }
    }
    if (!root.empty()) {
        std::ofstream report(root + "/render-failures.txt");
        for (const auto& failure : failures) report << failure << '\n';
    }
    std::cout << "Native muzzle render: bodies=" << bodyDraws << " sequences=" << sequenceChecks
        << " firingFrameAttachments=" << attachmentChecks << " nativeZoomChecks=" << zoomChecks << " pages=" << pages
        << " failures=" << failures.size() << '\n';
    if (!failures.empty()) { error = failures.front(); return false; }
    return true;
}
