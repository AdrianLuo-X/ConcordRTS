bool runRuntimeUnitIdleAnimationContractCheck(const RuntimeMapRenderContext& context, std::string& error) {
    const auto fail = [&](const std::string& message) { error = "unit idle: " + message; return false; };
    if (context.rules == nullptr || context.art == nullptr) return fail("resources missing");

    // Two frames, two independent sections: selecting the second frame must
    // advance both matrices without mixing the section and frame strides.
    std::vector<unsigned char> hva(24 + 32 + 2 * 2 * 48, 0);
    const auto putInt = [&](std::size_t offset, std::uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) hva[offset + byte] = static_cast<unsigned char>(value >> (byte * 8));
    };
    const auto putFloat = [&](std::size_t offset, float value) { std::memcpy(hva.data() + offset, &value, 4); };
    putInt(16, 2);
    putInt(20, 2);
    for (int frame = 0; frame < 2; ++frame) for (int section = 0; section < 2; ++section) {
        const std::size_t offset = 56 + (frame * 2 + section) * 48;
        for (int matrix : {0, 5, 10}) putFloat(offset + matrix * 4, 1.0F);
        putFloat(offset + 3 * 4, static_cast<float>(frame * 10 + section));
    }
    std::vector<RuntimeHvaTransform> firstTransforms, nextTransforms;
    if (!parseRuntimeHvaFrameTransformsFromBytes(hva, "idle-test", firstTransforms, error, 0)
        || !parseRuntimeHvaFrameTransformsFromBytes(hva, "idle-test", nextTransforms, error, 1)
        || firstTransforms.size() != 2 || nextTransforms.size() != 2
        || firstTransforms[0].m[3] != 0 || firstTransforms[1].m[3] != 1
        || nextTransforms[0].m[3] != 10 || nextTransforms[1].m[3] != 11)
        return fail("HVA frame/section selection failed");
    if (parseRuntimeHvaFrameTransformsFromBytes(hva, "idle-test", nextTransforms, error, 2))
        return fail("out-of-range HVA frame was accepted");
    hva.resize(hva.size() - 1);
    if (parseRuntimeHvaFrameTransformsFromBytes(hva, "idle-test", nextTransforms, error, 1))
        return fail("truncated HVA frame was accepted");
    error.clear();

    IniFile customRulesIni, customArtIni;
    customRulesIni.loadFromString("[VehicleTypes]\n0=IDLETEST\n[IDLETEST]\nIdleRate=3\nWalkRate=2\n");
    customArtIni.loadFromString("[IDLETEST]\nVoxel=no\nFacings=8\nStandingFrames=4\nStartStandFrame=64\nWalkFrames=6\n");
    RulesDatabase customRules;
    ArtDatabase customArt;
    customRules.loadFromIni(customRulesIni);
    customArt.loadFromIni(customArtIni);
    const auto* customUnit = customRules.findUnitType("vehicle", "IDLETEST");
    if (customUnit == nullptr || customUnit->idleRate != 3 || customUnit->walkRate != 2)
        return fail("animation rates were not parsed");
    RuntimeMapRenderContext custom = context;
    custom.rules = &customRules;
    custom.art = &customArt;
    custom.fixedTickHz = 30;
    WorldObjectInstance object;
    object.instanceId = 1;
    object.kind = WorldObjectKind::Unit;
    object.type = "IDLETEST";
    object.owner = "Americans";
    object.health = 100;
    object.x = object.y = 0;
    object.facing = 0;
    object.mission = "Guard";
    for (int step = 0; step <= 8; ++step) {
        custom.animationTick = step * 6;
        if (selectRuntimeObjectShpFrame(object, custom, 96) != 64 + step % 4)
            return fail("authored standing strip did not loop at IdleRate");
    }
    custom.rules = nullptr;
    custom.fixedTickHz = 60;
    custom.animationTick = 11;
    if (selectRuntimeObjectShpFrame(object, custom, 96) != 64)
        return fail("art-only idle advanced early at 60 Hz");
    custom.animationTick = 12;
    if (selectRuntimeObjectShpFrame(object, custom, 96) != 65)
        return fail("art-only idle ignored simulation frequency");
    custom.rules = &customRules;
    custom.fixedTickHz = 30;
    customArtIni.loadFromString("[IDLETEST]\nVoxel=no\nFacings=8\nWalkFrames=4\nStartWalkFrame=32\n");
    customArt.loadFromIni(customArtIni);
    custom.animationTick = 6;
    if (selectRuntimeObjectShpFrame(object, custom, 64) != 33)
        return fail("explicit-facing idle ignored StartWalkFrame");

    auto cache = std::make_unique<RuntimeMapRenderCache>();
    constexpr int panelWidth = 160, panelHeight = 144, width = panelWidth * 4, height = panelHeight * 4;
    constexpr std::uint32_t background = 0x00242c30U;
    std::vector<std::uint32_t> pixels(width * height, background);
    RuntimeMapRenderContext probe = context;
    probe.fixedTickHz = 30;
    probe.vision = nullptr;
    probe.specialMechanismSnapshots = nullptr;
    probe.stableRenderConfiguration = true;
    probe.surfacePixels = pixels.data();
    probe.surfaceWidth = width;
    probe.surfaceHeight = height;
    RuntimeMapCamera camera;
    camera.worldOffsetX = -panelWidth / 2;
    camera.worldOffsetY = -100;
    camera.initialized = true;
    const auto fingerprint = [&](const std::vector<std::uint32_t>& surface) {
        std::uint64_t hash = 1469598103934665603ULL;
        for (const auto pixel : surface) { hash ^= pixel; hash *= 1099511628211ULL; }
        return hash;
    };
    const RECT firstPanel{0, 0, panelWidth, panelHeight};
    const auto draw = [&](const RECT& viewport) {
        const POINT point = screenPoint(runtimeObjectDrawPoint(object, {}, camera), viewport, camera);
        return objectUsesVoxelArt(object, probe)
            ? tryDrawVoxelSprite(nullptr, viewport, object, point, probe, camera, *cache)
            : tryDrawObjectShpSprite(nullptr, viewport, object, point, probe, camera, *cache);
    };

    int rotorFrames = 0, groundedChecks = 0;
    for (const std::string type : {"SHAD", "HIND", "SCHP", "SCHD"}) {
        object.type = type;
        object.airborne = true;
        object.hasRenderPosition = object.renderMoveActive = false;
        object.deployed = false;
        object.mission = "Guard";
        probe.fixedTickHz = 30;
        probe.animationTick = 0;
        const auto* unit = context.rules->findUnitType("vehicle", type);
        const auto* art = context.art->findArtType(type);
        if (unit == nullptr || art == nullptr || !art->voxel || !unit->jumpJet
            || unit->idleRate != 0 || unit->walkRate != 1)
            return fail(type + " original registered voxel/jumpjet/rates changed");
        const auto* base = loadRuntimeVoxelFromCache(object, probe, *cache);
        if (base == nullptr || !base->hasSprite || toLowerAscii(base->metadata.imageId) != toLowerAscii(type))
            return fail(type + " original body image could not be loaded");
        const auto& source = loadRuntimeVoxelSourceFromCache(base->metadata.imageId, probe, *cache, false);
        const auto rawInt = [&](std::size_t offset) {
            return static_cast<std::uint32_t>(source.hvaBytes[offset])
                | static_cast<std::uint32_t>(source.hvaBytes[offset + 1]) << 8
                | static_cast<std::uint32_t>(source.hvaBytes[offset + 2]) << 16
                | static_cast<std::uint32_t>(source.hvaBytes[offset + 3]) << 24;
        };
        if (!source.loaded || source.hvaBytes.size() < 24) return fail(type + " original HVA bytes missing");
        const auto rawFrames = rawInt(16), rawSections = rawInt(20);
        const std::size_t matrices = 24 + static_cast<std::size_t>(rawSections) * 16;
        const std::size_t frameBytes = static_cast<std::size_t>(rawSections) * 48;
        if (rawFrames == 0 || rawSections == 0 || rawFrames > 1000 || rawSections > 1000
            || matrices + static_cast<std::size_t>(rawFrames) * frameBytes > source.hvaBytes.size()
            || base->metadata.hvaFrameCount != static_cast<int>(rawFrames)
            || base->metadata.hvaSectionCount != static_cast<int>(rawSections)
            || ((type == "SHAD" || type == "SCHP") && (rawFrames != 2 || rawSections != 3))
            || (type == "SCHD" && (rawFrames != 1 || rawSections != 1)))
            return fail(type + " original HVA header/section/frame contract differs");
        const int count = base->metadata.hvaFrameCount;
        const double scale = base->rasterFitScale;
        const int shadowSection = base->shadowSectionApplied;
        // SCHD is an original one-frame deployed body. Do not invent rotor
        // frames for it, or for another original image with a single HVA pose.
        bool changingTransforms = false;
        for (int frame = 1; frame < count; ++frame)
            changingTransforms = changingTransforms || !std::equal(source.hvaBytes.begin() + matrices,
                source.hvaBytes.begin() + matrices + frameBytes,
                source.hvaBytes.begin() + matrices + static_cast<std::size_t>(frame) * frameBytes);
        if ((type == "SHAD" || type == "SCHP") && !changingTransforms)
            return fail(type + " original HVA flight matrices never change");
        const auto firstImage = base->image;
        std::vector<std::uint64_t> hashes;
        int missesAfterWarmup = 0;
        for (int cycle = 0; cycle < 2; ++cycle) {
            for (int frame = 0; frame < count; ++frame) {
                probe.animationTick = (cycle * count + frame) * 2;
                const auto* voxel = loadRuntimeVoxelFromCache(object, probe, *cache);
                if (voxel == nullptr || voxel->animationFrameIndex != frame || !voxel->hasSprite
                    || std::abs(voxel->rasterFitScale - scale) > 0.000001
                    || voxel->shadowSectionApplied != shadowSection)
                    return fail(type + " reused the wrong pose or changed body scale/shadow section");
                std::fill(pixels.begin(), pixels.end(), background);
                if (!draw(firstPanel)) return fail(type + " frame could not be drawn");
                const auto hash = fingerprint(pixels);
                if (cycle == 0) hashes.push_back(hash);
                else if (hash != hashes[frame]) return fail(type + " loop changed after caching");
            }
            if (cycle == 0) missesAfterWarmup = cache->voxelCacheMisses;
            else if (cache->voxelCacheMisses != missesAfterWarmup) return fail(type + " cache grew on every animation cycle");
        }
        if (changingTransforms && std::all_of(hashes.begin(), hashes.end(), [&](auto hash) { return hash == hashes.front(); }))
            return fail(type + " rotor pixels never changed");
        probe.animationTick = 2;
        const int pose30 = loadRuntimeVoxelFromCache(object, probe, *cache)->animationFrameIndex;
        probe.fixedTickHz = 60;
        probe.animationTick = 4;
        if (loadRuntimeVoxelFromCache(object, probe, *cache)->animationFrameIndex != pose30)
            return fail("rotor speed depended on simulation rate");
        probe.fixedTickHz = 30;
        SpecialMechanismRenderSnapshot frozen;
        frozen.kind = "chrono";
        frozen.objectId = object.instanceId;
        frozen.phase = "Cooldown";
        frozen.progress = 0;
        std::vector<SpecialMechanismRenderSnapshot> snapshots{frozen};
        probe.specialMechanismSnapshots = &snapshots;
        probe.animationTick = 2;
        const auto frozenKey = loadRuntimeVoxelFromCache(object, probe, *cache)->image.rasterCacheKey;
        snapshots.front().progress = 21;
        probe.animationTick = 23;
        if (loadRuntimeVoxelFromCache(object, probe, *cache)->image.rasterCacheKey != frozenKey)
            return fail("frozen rotor continued animating");
        probe.specialMechanismSnapshots = nullptr;

        object.airborne = false;
        object.hasRenderPosition = true;
        object.renderX = object.renderY = object.renderLevel = 0;
        probe.animationTick = 0;
        std::fill(pixels.begin(), pixels.end(), background);
        if (!draw(firstPanel)) return fail(type + " grounded frame zero could not be drawn");
        const auto groundedHash = fingerprint(pixels);
        const int groundedMisses = cache->voxelCacheMisses;
        for (const int hz : {30, 60}) for (const bool moving : {false, true})
            for (const std::string mission : {"Guard", "Attack", "Move"})
                for (const int tick : {0, 2, 4, 16, 9999}) {
                    probe.fixedTickHz = hz;
                    probe.animationTick = tick;
                    object.renderMoveActive = moving;
                    object.mission = mission;
                    const auto* grounded = loadRuntimeVoxelFromCache(object, probe, *cache);
                    if (grounded == nullptr || grounded->animationFrameIndex != 0
                        || grounded->image.width != firstImage.width || grounded->image.height != firstImage.height
                        || grounded->image.offsetX != firstImage.offsetX || grounded->image.offsetY != firstImage.offsetY
                        || grounded->image.pixels != firstImage.pixels
                        || grounded->shadowSectionApplied != shadowSection)
                        return fail(type + " grounded body did not keep original frame zero at hz="
                            + std::to_string(hz) + " tick=" + std::to_string(tick) + " mission=" + mission);
                    std::fill(pixels.begin(), pixels.end(), background);
                    if (!draw(firstPanel) || fingerprint(pixels) != groundedHash)
                        return fail(type + " grounded pixels animated across an order/clock boundary");
                    ++groundedChecks;
                }
        if (cache->voxelCacheMisses != groundedMisses)
            return fail(type + " grounded order/clock changed the cached original pose");
        {
            auto uncached = std::make_unique<RuntimeMapRenderCache>();
            RuntimeRendererEnvironmentScope noCache([](const char* name) -> const char* {
                return std::strcmp(name, "CONCORD_VXL_DISABLE_CACHE") == 0 ? "1" : std::getenv(name);
            });
            const auto* grounded = loadRuntimeVoxelFromCache(object, probe, *uncached);
            if (grounded == nullptr || grounded->animationFrameIndex != 0
                || grounded->image.width != firstImage.width || grounded->image.height != firstImage.height
                || grounded->image.offsetX != firstImage.offsetX || grounded->image.offsetY != firstImage.offsetY
                || grounded->image.pixels != firstImage.pixels)
                return fail(type + " uncached grounded body lost its original first-frame pixels");
        }
        object.airborne = true;
        object.hasRenderPosition = object.renderMoveActive = false;
        object.mission = "Guard";
        probe.fixedTickHz = 30;
        probe.animationTick = 2;
        const auto* resumed = loadRuntimeVoxelFromCache(object, probe, *cache);
        if (resumed == nullptr || resumed->animationFrameIndex != 1 % count)
            return fail(type + " takeoff retained the cached grounded HVA pose");
        rotorFrames += count;
        std::cout << "Idle rotor: " << type << " frames=" << count << " sections=" << base->metadata.hvaSectionCount
            << " originalChangingTransforms=" << (changingTransforms ? "yes" : "no")
            << " stableScale=ok cacheLoop=ok frozen=ok groundedFrame0=ok groundedPixels=ok takeoff=ok\n";
    }

    object.airborne = false;
    for (const std::string type : {"DLPH", "SQD"}) {
        object.type = type;
        const auto* unit = context.rules->findUnitType("vehicle", type);
        const auto* art = context.art->findArtType(type);
        if (unit == nullptr || art == nullptr || unit->idleRate <= 0) return fail(type + " IdleRate missing");
        const int count = art->walkFrames;
        probe.animationTick = 0;
        const int frame0 = selectRuntimeObjectShpFrame(object, probe, count * 8 + 8);
        probe.animationTick = unit->idleRate * 2 - 1;
        if (selectRuntimeObjectShpFrame(object, probe, count * 8 + 8) != frame0) return fail(type + " idle advanced early");
        std::vector<std::uint64_t> hashes;
        for (int frame = 0; frame < count; ++frame) {
            probe.animationTick = frame * unit->idleRate * 2;
            if (selectRuntimeObjectShpFrame(object, probe, count * 8 + 8) != frame0 + frame)
                return fail(type + " used the wrong idle strip");
            std::fill(pixels.begin(), pixels.end(), background);
            if (!draw(firstPanel)) return fail(type + " idle could not be drawn");
            hashes.push_back(fingerprint(pixels));
        }
        if (std::all_of(hashes.begin(), hashes.end(), [&](auto hash) { return hash == hashes.front(); }))
            return fail(type + " idle pixels never changed");
        std::cout << "Idle sprite: " << type << " frames=" << count << " IdleRate=" << unit->idleRate << '\n';
    }
    for (const std::string type : {"MTNK", "BUS"}) {
        object.type = type;
        probe.animationTick = 0;
        const auto* staticVoxel = loadRuntimeVoxelFromCache(object, probe, *cache);
        if (staticVoxel == nullptr) return fail(type + " original static vehicle missing");
        const auto staticKey = staticVoxel->image.rasterCacheKey;
        probe.animationTick = 9999;
        if (loadRuntimeVoxelFromCache(object, probe, *cache)->image.rasterCacheKey != staticKey)
            return fail(type + " static vehicle acquired an invented idle animation");
    }

    object.type = "SHAD";
    object.airborne = true;
    probe.animationTick = 2;
    const auto* expected = loadRuntimeVoxelFromCache(object, probe, *cache);
    {
        auto uncached = std::make_unique<RuntimeMapRenderCache>();
        RuntimeRendererEnvironmentScope noCache([](const char* name) -> const char* {
            return std::strcmp(name, "CONCORD_VXL_DISABLE_CACHE") == 0 ? "1" : std::getenv(name);
        });
        const auto* fresh = loadRuntimeVoxelFromCache(object, probe, *uncached);
        if (fresh == nullptr || fresh->animationFrameIndex != 1
            || fresh->image.width != expected->image.width || fresh->image.height != expected->image.height
            || fresh->image.offsetX != expected->image.offsetX || fresh->image.offsetY != expected->image.offsetY
            || fresh->image.pixels != expected->image.pixels)
            return fail("uncached animation lost its frame transforms");
    }

    std::fill(pixels.begin(), pixels.end(), background);
    // Rows are the four original helicopter bodies. The first two columns
    // show grounded ticks 0/2; the last two show flight ticks 0/2.
    for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column) {
        object.type = row == 0 ? "SHAD" : row == 1 ? "HIND" : row == 2 ? "SCHP" : "SCHD";
        object.airborne = column >= 2;
        object.hasRenderPosition = object.renderMoveActive = false;
        probe.animationTick = (column % 2) * 2;
        const RECT viewport{column * panelWidth, row * panelHeight, (column + 1) * panelWidth, (row + 1) * panelHeight};
        if (!draw(viewport)) return fail("preview draw failed");
    }
    if (const char* path = std::getenv("CONCORD_UNIT_IDLE_BMP")) {
        if (*path != '\0' && !writeRuntimeBgraBmp(path, width, height, pixels)) return fail("preview write failed");
    }
    std::cout << "Unit idle animation: HVAFrames=" << rotorFrames
        << " groundedChecks=" << groundedChecks
        << " HVAParser=ok standingStrip=ok IdleRate=ok fixedTickHz=ok staticVehicle=ok\n";
    return true;
}
