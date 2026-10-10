// A weaponless Combat building is still player artwork. Exercise metadata,
// not stock IDs, so new support buildings cannot repeat the Gap Generator bug.
bool runRuntimeBuildingOwnerRemapPolicyContractCheck(std::string& error) {
    IniFile rulesIni, artIni;
    RulesDatabase rules;
    ArtDatabase art;
    const std::array<const char*, 5> categories{{"Combat", "Power", "Resource", "Tech", "Defense"}};
    std::string rulesText = "[BuildingTypes]\n", artText;
    for (std::size_t index = 0; index < categories.size(); ++index) {
        rulesText += std::to_string(index) + "=SUPPORT" + std::to_string(index) + "\n";
    }
    rulesText += "5=MISSION_POWER\n6=MISSION_WEAPON\n";
    for (std::size_t index = 0; index < categories.size(); ++index) {
        const std::string id = "SUPPORT" + std::to_string(index);
        rulesText += "[" + id + "]\nBuildCat=" + categories[index]
            + "\nTechLevel=-1\nCapturable=no\nStrength=100\n";
        artText += "[" + id + "]\nFoundation=1x1\nRemapable=no\n";
    }
    rulesText += "[MISSION_POWER]\nTechLevel=-1\nPower=-100\nCapturable=no\n"
        "[MISSION_WEAPON]\nTechLevel=-1\nPrimary=AUDIT_GUN\nCapturable=no\n"
        "[WeaponTypes]\n0=AUDIT_GUN\n[AUDIT_GUN]\nDamage=1\n";
    artText += "[MISSION_POWER]\nFoundation=1x1\nRemapable=no\n"
        "[MISSION_WEAPON]\nFoundation=1x1\nRemapable=no\n";
    artText += "[PART]\nRemapable=no\n[SMOKE]\nAnimPalette=yes\n[GROUND]\nTerrainPalette=yes\n";
    if (!rulesIni.loadFromString(rulesText) || !artIni.loadFromString(artText)
        || !rules.loadFromIni(rulesIni) || !art.loadFromIni(artIni)) {
        error = "support-building remap fixtures did not load";
        return false;
    }
    std::unordered_map<std::string, COLORREF> colors{{"customhouse", RGB(20, 220, 90)}};
    RuntimeMapRenderContext context;
    context.rules = &rules;
    context.art = &art;
    context.ownerRemapColors = &colors;
    for (const auto& building : rules.getBuildingTypes()) {
        WorldObjectInstance object;
        object.type = building.id;
        object.owner = "CustomHouse";
        object.health = 100;
        if (!runtimeObjectShouldOwnerRemap(object, context)
            || !runtimeStructureLayerShouldOwnerRemap(object, context, "PART")
            || !runtimeStructureLayerShouldOwnerRemap(object, context, "IMPLICIT_PART")
            || runtimeStructureLayerShouldOwnerRemap(object, context, "SMOKE")
            || runtimeStructureLayerShouldOwnerRemap(object, context, "GROUND")) {
            error = building.buildCat + " weaponless building or its layers lost player color";
            return false;
        }
        object.rubble = true;
        object.alive = false;
        object.health = 0;
        if (runtimeObjectShouldOwnerRemap(object, context)) {
            error = "rubble inherited the former owner's color";
            return false;
        }
    }
    return true;
}

// Opt-in exhaustive audit of the loaded building catalog and actual SHP frames.
// The CSV records fixed-color artwork, empty remap masks and missing stock
// resources explicitly; an absent mask is never fabricated by tinting red RGB.
bool runRuntimeBuildingOwnerRemapAssetContractCheck(
    const RuntimeMapRenderContext& context, std::string& error
) {
    const auto fail = [&](const std::string& message) { error = "building remap audit: " + message; return false; };
    if (context.rules == nullptr || context.art == nullptr) return fail("databases unavailable");
    auto probe = context;
    IniFile catalogIni;
    auto catalogRules = std::make_unique<RulesDatabase>();
    if (const char* path = std::getenv("CONCORD_BUILDING_REMAP_RULES_INI"); path != nullptr && *path != '\0') {
        if (!catalogIni.loadFromFile(path) || !catalogRules->loadFromIni(catalogIni))
            return fail("original building catalog did not load");
        probe.rules = catalogRules.get();
    }
    probe.vision = nullptr;
    probe.detailStatsEnabled = false;
    std::unordered_map<std::string, COLORREF> colors{{"remapaudit", RGB(255, 230, 10)}};
    probe.ownerRemapColors = &colors;
    RuntimeMapCamera camera;
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    if (!ensureRuntimeStructurePalette(probe, *cache)) return fail("unit palette unavailable");
    std::ostringstream report;
    report << "building,layer,image,owner_remap,frames,empty_frames,remap_pixels,status,source\n";
    int buildings = 0, remappedBuildings = 0, resources = 0, frames = 0, missing = 0, voxelParts = 0, emptyFrames = 0;
    std::uint64_t remapPixels = 0;
    constexpr std::uint32_t background = 0x0019232dU;
    std::vector<std::uint32_t> pixels;
    std::string pixelError;
    const auto verifyFrame = [&](const RuntimeShpFrameImage& image, bool remap) {
        pixelError.clear();
        if (image.width <= 0 || image.height <= 0) return true; // Empty authored frames.
        pixels.assign(static_cast<std::size_t>(image.width) * image.height, background);
        probe.surfacePixels = pixels.data();
        probe.surfaceWidth = image.width;
        probe.surfaceHeight = image.height;
        const RECT viewport{0, 0, image.width, image.height};
        const RECT origin = runtimeShpFrameScreenRect(POINT{}, camera, image);
        const POINT center{-origin.left, -origin.top};
        std::vector<std::uint32_t> first, second;
        for (int pass = 0; pass < 3; ++pass) {
            const COLORREF color = pass == 1 ? RGB(10, 220, 255) : RGB(255, 230, 10);
            std::fill(pixels.begin(), pixels.end(), background);
            if (!drawRuntimeShpFrame(nullptr, viewport, center, probe, camera,
                    image, cache->structurePalette, remap, color)) return false;
            if (pass == 0) first = pixels;
            else if (pass == 1) second = pixels;
            else if (pixels != first) return false;
        }
        for (std::size_t index = 0; index < pixels.size(); ++index) {
            const auto paletteIndex = image.pixels[index];
            const bool maskAvailable = image.ownerRemapMask.size() >= pixels.size();
            const bool changes = remap && paletteIndex != 0 && (maskAvailable
                ? image.ownerRemapMask[index] != 0 : paletteIndex >= 16 && paletteIndex <= 31);
            if ((first[index] != second[index]) != changes) {
                pixelError = " pixel=" + std::to_string(index) + " index=" + std::to_string(paletteIndex)
                    + " expectedChange=" + std::to_string(changes);
                return false;
            }
            if (!changes && paletteIndex != 0) {
                const auto rgb = cache->structurePalette[paletteIndex];
                const auto expected = (std::uint32_t(rgb.r) << 16U)
                    | (std::uint32_t(rgb.g) << 8U) | rgb.b;
                if (first[index] != expected) { pixelError = " fixed palette pixel changed"; return false; }
            }
        }
        return true;
    };
    for (const auto& building : probe.rules->getBuildingTypes()) {
        ++buildings;
        WorldObjectInstance object;
        object.type = building.id;
        object.owner = "RemapAudit";
        object.health = std::max(1, building.strength);
        const ArtType* art = findArtTypeForObject(object, probe);
        const bool remap = runtimeObjectShouldOwnerRemap(object, probe);
        if (remap) ++remappedBuildings;
        if (!building.buildCat.empty() && (art == nullptr || (!art->animPalette && !art->terrainPalette))
            && !remap) return fail(building.id + " construction category was excluded");
        struct Layer { std::string role, image; bool splitShadow; bool body; };
        std::vector<Layer> layers;
        auto* body = loadRuntimeObjectShpSpriteFromCache(object, probe, *cache);
        layers.push_back({"body", body == nullptr ? resolveObjectArtId(object, probe) : body->resolvedImageId,
            runtimeObjectSourceHasSplitShadow(object, probe), true});
        if (art != nullptr) {
            layers.push_back({"buildup", art->buildup, runtimeObjectSourceHasSplitShadow(object, probe), false});
            layers.push_back({"bib", art->bibShape, false, false});
            layers.push_back({"overlay", art->toOverlay, false, false});
            layers.push_back({"gate", art->specialZOverlay, false, false});
            const auto addAnimations = [&](const auto& animations) {
                for (const auto& animation : animations) {
                    layers.push_back({animation.key, animation.image, false, false});
                    layers.push_back({animation.key + "Damaged", animation.damagedImage, false, false});
                }
            };
            addAnimations(art->activeAnimations);
            addAnimations(art->specialAnimations);
            addAnimations(art->auxiliaryAnimations);
        }
        if (!building.turretAnimIsVoxel) layers.push_back({"turret", building.turretAnim, false, false});
        for (const auto& layer : layers) {
            if (layer.image.empty()) continue;
            const bool layerRemap = layer.body ? remap
                : runtimeStructureLayerShouldOwnerRemap(object, probe, layer.image);
            loadRuntimeAuxiliaryShpSpriteFromCache(layer.image, 0, probe, *cache, layer.splitShadow);
            const RuntimeAuxiliaryShpBytesCacheEntry* bytes = nullptr;
            for (const auto& [key, entry] : cache->auxiliaryShpBytesByImage) {
                if (entry.loaded && key.rfind(toLowerAscii(layer.image) + "|", 0) == 0) { bytes = &entry; break; }
            }
            if (bytes == nullptr) {
                ++missing;
                report << building.id << ',' << layer.role << ',' << layer.image << ',' << layerRemap
                    << ",0,0,0,missing,\n";
                continue;
            }
            ++resources;
            const int count = runtimeVisibleMainFrameCount(bytes->frameCount, layer.splitShadow);
            const std::string source = bytes->sourcePath;
            std::uint64_t layerPixels = 0;
            int layerEmptyFrames = 0;
            for (int frame = 0; frame < count; ++frame) {
                const std::size_t header = 8 + static_cast<std::size_t>(frame) * 24;
                if (header + 24 <= bytes->bytes.size()
                    && (readUInt16(bytes->bytes, header + 4) == 0 || readUInt16(bytes->bytes, header + 6) == 0)) {
                    ++emptyFrames;
                    ++layerEmptyFrames;
                    continue; // Zero-size placeholders/shadow slots contain no color pixels.
                }
                auto* entry = loadRuntimeAuxiliaryShpSpriteFromCache(layer.image, frame, probe, *cache, layer.splitShadow);
                if (entry == nullptr || !verifyFrame(entry->image, layerRemap))
                    return fail(building.id + "/" + layer.role + "/" + layer.image + " frame " + std::to_string(frame)
                        + (entry == nullptr ? " decode failed" : pixelError));
                layerPixels += std::count_if(entry->image.pixels.begin(), entry->image.pixels.end(),
                    [](unsigned char index) { return index >= 16 && index <= 31; });
                ++frames;
            }
            remapPixels += layerRemap ? layerPixels : 0;
            report << building.id << ',' << layer.role << ',' << layer.image << ',' << layerRemap << ',' << count
                << ',' << layerEmptyFrames << ',' << layerPixels << ",checked," << source << '\n';
        }
        if (building.turretAnimIsVoxel && !building.turretAnim.empty()) {
            std::vector<std::string> parts{building.turretAnim};
            for (const auto& candidate : runtimeStructureTurretBarrelImageCandidates(building.turretAnim, building.id)) {
                const auto* barrel = loadRuntimeVoxelImageFromCache(candidate, building.id + "|audit", 0, probe, *cache);
                if (barrel != nullptr && barrel->hasSprite) { parts.push_back(candidate); break; }
            }
            for (const auto& part : parts) {
                std::uint64_t partPixels = 0;
                std::string source;
                for (int facing = 0; facing < 32; ++facing) {
                    auto* voxel = loadRuntimeVoxelImageFromCache(part, building.id + "|audit", facing, probe, *cache);
                    if (voxel == nullptr || !voxel->hasSprite
                        || !verifyFrame(voxel->image, runtimeStructureLayerShouldOwnerRemap(object, probe, part)))
                        return fail(building.id + "/voxel/" + part + " facing " + std::to_string(facing) + pixelError);
                    partPixels += std::count_if(voxel->image.ownerRemapMask.begin(), voxel->image.ownerRemapMask.end(),
                        [](unsigned char mask) { return mask != 0; });
                    source = voxel->metadata.vxlPath;
                }
                ++voxelParts;
                report << building.id << ",voxel," << part << ','
                    << runtimeStructureLayerShouldOwnerRemap(object, probe, part)
                    << ",32,0," << partPixels << ",checked," << source << '\n';
            }
        }
        // Frame caches are mutable palette caches too; limit audit memory after
        // exercising each image through A -> B -> A ownership/color changes.
        cache->auxiliaryShpSpritesByImage.clear();
    }

    // End-to-end routes use the current owner for bodies, active/damaged layers,
    // construction/selling frames, bibs and VXL turrets/barrels (all facings).
    constexpr int previewSize = 512;
    std::vector<std::uint32_t> sheet(previewSize * previewSize * 6, background);
    const std::array<const char*, 14> probes{{"GAGAP", "GAPOWR", "GAREFN", "GAWEAP", "GATECH",
        "GACSPH", "TESLA", "ATESLA", "YAPSYT", "GTGCAN", "NALASR", "NAPSYA", "MAYAN", "CAPARS01"}};
    for (const char* id : probes) {
        const auto* building = probe.rules->findBuildingType(id);
        if (building == nullptr) return fail(std::string(id) + " fixture missing");
        WorldObjectInstance object;
        object.type = id;
        object.owner = "RemapAudit";
        object.health = building->strength;
        const auto* baseSprite = loadRuntimeObjectShpSpriteFromCache(object, probe, *cache);
        const int size = baseSprite == nullptr ? previewSize : std::max(previewSize,
            std::max(baseSprite->image.globalWidth, baseSprite->image.globalHeight) + 128);
        const auto foundation = runtimeObjectFoundationDimensions(object, probe);
        for (int state = 0; state < 3; ++state) {
            const auto* art = findArtTypeForObject(object, probe);
            if (state == 2 && (art == nullptr || art->buildup.empty())) continue;
            object.health = state == 1 ? std::max(1, building->strength / 4) : building->strength;
            object.buildAnimTicksTotal = state == 2 ? 100 : 0;
            object.buildAnimTicksRemaining = state == 2 ? 25 : 0;
            probe.animationTick = 48;
            const int facings = building->turretAnimIsVoxel ? 32 : 1;
            for (int facing = 0; facing < facings; ++facing) {
                object.turretFacing = facing * 8;
                std::vector<std::uint32_t> first, second;
                for (int pass = 0; pass < 3; ++pass) {
                    colors["remapaudit"] = pass == 1 ? RGB(10, 220, 255) : RGB(255, 230, 10);
                    pixels.assign(size * size, background);
                    probe.surfacePixels = pixels.data();
                    probe.surfaceWidth = size;
                    probe.surfaceHeight = size;
                    const RECT viewport{0, 0, size, size};
                    const POINT center{size / 2, size / 2};
                    tryDrawRuntimeStructureBib(nullptr, viewport, object, center, probe, camera, *cache);
                    bool drawn = false;
                    if (state == 2) drawn = tryDrawRuntimeStructureBuildup(nullptr, viewport, object, center, probe, camera, *cache);
                    else {
                        drawn = tryDrawObjectShpSprite(nullptr, viewport, object, center, probe, camera, *cache);
                        drawRuntimeStructureActiveAnimations(nullptr, viewport, object, center, probe, camera, *cache);
                        tryDrawRuntimeStructureTurretAnim(nullptr, viewport, object, center,
                            foundation.width, foundation.height, probe, camera, *cache);
                    }
                    if (!drawn) return fail(std::string(id) + " state route did not render");
                    if (pass == 0) first = pixels;
                    else if (pass == 1) second = pixels;
                    else if (pixels != first) return fail(std::string(id) + " reused stale owner-color pixels");
                    if (std::string(id) == "GAGAP" && pass < 2) {
                        for (int y = 0; y < size; ++y) {
                            std::copy_n(pixels.begin() + y * size, size,
                                sheet.begin() + (state * 2 + pass) * size * size + y * size);
                        }
                    }
                }
                if (first == second) {
                    const auto* selected = loadRuntimeObjectShpSpriteFromCache(object, probe, *cache);
                    const bool bodyHasRemap = selected != nullptr && std::any_of(
                        selected->image.pixels.begin(), selected->image.pixels.end(),
                        [](unsigned char index) { return index >= 16 && index <= 31; });
                    // Some armed landmarks (e.g. the Eiffel Tower) have no
                    // house-color pixels in any live frame, only in rubble.
                    // The renderer must not invent a mask for those states.
                    if (std::string(id) == "GAGAP" || state == 2 || bodyHasRemap)
                        return fail(std::string(id) + " rendered state " + std::to_string(state)
                            + " did not follow player color (canvas=" + std::to_string(size) + ")");
                }
            }
        }
    }
    if (const char* path = std::getenv("CONCORD_BUILDING_REMAP_AUDIT_CSV")) {
        std::ofstream output(path);
        output << report.str();
        if (!output) return fail("CSV write failed");
    }
    if (const char* path = std::getenv("CONCORD_BUILDING_REMAP_PREVIEW_BMP")) {
        if (!writeRuntimeBgraBmp(path, previewSize, previewSize * 6, sheet)) return fail("preview write failed");
    }
    std::cout << "Runtime building remap audit: buildings=" << buildings << " playerRemap=" << remappedBuildings
        << " resources=" << resources << " frames=" << frames << " remapPixels=" << remapPixels
        << " emptyFrames=" << emptyFrames << " voxelParts=" << voxelParts << " missingResources=" << missing
        << " stateRoutes=healthy,damaged,buildup cache=A-B-A.\n";
    return true;
}
