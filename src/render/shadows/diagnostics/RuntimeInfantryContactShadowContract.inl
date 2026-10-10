// This oracle scans actual compositor output. It never calls the new contact
// helper to construct its expected contact or its old-ground counterexample.
bool runRuntimeInfantryContactShadowContractCheck(const GameWorld& sourceWorld,
    const RuntimeMapRenderContext& sourceContext, std::string& errorMessage) {
    const auto fail = [&](const std::string& why) {
        errorMessage = "infantry contact shadow: " + why; return false;
    };
    if (!sourceContext.rules || !sourceContext.art) return fail("original rules/art unavailable");
    const char* requested = std::getenv("CONCORD_INFANTRY_CONTACT_SHADOW_OUTPUT");
    const auto output = std::filesystem::u8path(requested && *requested ? requested
        : "build/smoke/feature-set-19-complete/10-order-line/infantry-contact");
    std::filesystem::create_directories(output);
    std::ofstream rows(output / "contact.csv"), inventory(output / "inventory.csv");
    if (!rows || !inventory) return fail("evidence output unavailable");
    rows << "type,pose,facing,zoom,frame,body_pixels,shadow_pixels,coloured_bottom,shadow_top,shadow_bottom,contact_distance,old_distance,fallback,pass\n";
    inventory << "type,status\n";
    constexpr int width = 384, height = 320;
    constexpr std::uint32_t background = 0x00ddeeffU;
    const RECT viewport{0, 0, width, height};
    struct Surface {
        HDC dc = nullptr; HBITMAP bitmap = nullptr; HGDIOBJ oldBitmap = nullptr;
        std::uint32_t* pixels = nullptr;
        ~Surface() { if (oldBitmap) SelectObject(dc, oldBitmap);
            if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); }
    } surface;
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    surface.dc = CreateCompatibleDC(nullptr); void* bits = nullptr;
    if (surface.dc) surface.bitmap = CreateDIBSection(surface.dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!surface.dc || !surface.bitmap || !bits) return fail("DIB allocation");
    surface.oldBitmap = SelectObject(surface.dc, surface.bitmap);
    surface.pixels = static_cast<std::uint32_t*>(bits);
    auto context = sourceContext;
    context.vision = nullptr; context.mapFacts = nullptr; context.selectedObjectIds = nullptr;
    context.specialMechanismSnapshots = nullptr; context.impactActors = nullptr;
    context.projectileActors = nullptr; context.particleSnapshots = nullptr;
    context.particleBeamSnapshots = nullptr; context.localLightSnapshots = nullptr;
    context.shadowsEnabled = true; context.fixedTickHz = 30;
    context.surfacePixels = surface.pixels; context.surfaceWidth = width; context.surfaceHeight = height;
    RuntimeMapRenderCache cache;
    const auto clear = [&]() { GdiFlush(); std::fill(surface.pixels, surface.pixels + width * height, background); };
    const auto read = [&]() { GdiFlush(); return std::vector<std::uint32_t>(surface.pixels, surface.pixels + width * height); };
    const auto changed = [&](const std::vector<std::uint32_t>& pixels, bool coloured) {
        std::vector<POINT> points;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const auto value = pixels[static_cast<std::size_t>(y) * width + x] & 0xffffffU;
            if (value != background && (!coloured || value != 0)) points.push_back({x, y});
        }
        return points;
    };
    const auto distance = [](const std::vector<POINT>& support, const std::vector<POINT>& shadow) {
        int best = 100000;
        for (const auto a : support) for (const auto b : shadow)
            best = std::min(best, static_cast<int>(std::max(std::abs(a.x-b.x), std::abs(a.y-b.y))));
        return best;
    };
    int checks = 0, fallbackChecks = 0, authoredChecks = 0, suppressedChecks = 0, oldGapWitnesses = 0, excluded = 0;
    const auto check = [&](const UnitType& type, const std::string& pose,
        int direction, int zoom, bool picture, bool airborneControl = false) {
        WorldObjectInstance object; object.instanceId = 941001; object.kind = WorldObjectKind::Infantry;
        object.type = type.id; object.owner = "Americans"; object.alive = true;
        object.health = std::max(1, type.strength); object.x = object.y = 20;
        object.cell = 20020; object.subCell = direction % 3;
        object.facing = direction * 32; object.currentAmmo = type.ammo;
        object.prone = pose == "Prone"; object.airborne = airborneControl;
        object.mission = pose == "Walk0" || pose == "Walk1" ? "Move" : "Guard";
        object.renderMoveActive = object.mission == "Move";
        object.hasRenderPosition = object.renderMoveActive;
        object.renderX = 20.375; object.renderY = 20.125;
        object.renderLevel = 0; object.movementAnimationDistance = pose == "Walk1" ? 0.625 : 0.25;
        RuntimeMapCamera camera; camera.initialized = true; camera.tileWidth = zoom; camera.tileHeight = zoom / 2;
        const auto projected = runtimeObjectDrawPoint(object, {}, camera);
        camera.worldOffsetX = projected.x - width / 2; camera.worldOffsetY = projected.y - 220;
        const POINT anchor = screenPoint(projected, viewport, camera);
        context.animationTick = pose == "Walk1" ? 19 : 7;
        const auto* sprite = loadRuntimeObjectShpSpriteFromCache(object, context, cache);
        if (!sprite || !sprite->loaded) return fail(type.id + " missing original body resource");
        clear(); if (!tryDrawObjectShpSprite(surface.dc, viewport, object, anchor, context, camera, cache))
            return fail(type.id + " compositor body failed");
        const auto bodyImage = read(); const auto body = changed(bodyImage, false);
        const auto coloured = changed(bodyImage, true);
        if (body.empty() || coloured.empty()) return fail(type.id + " original pose has no coloured body");
        LONG bottom = 0; for (const auto p : coloured) bottom = std::max(bottom, p.y);
        std::vector<POINT> support;
        for (const auto p : coloured) if (p.y >= bottom - 2) support.push_back(p);
        clear(); const auto result = drawRuntimeWorldObjectShadow(surface.dc, viewport, object,
            anchor, context, camera, cache, false);
        const auto shadowImage = read(); const auto shadow = changed(shadowImage, false);
        clear(); drawRuntimeObjectSoftShadow(surface.dc, object, anchor, context,
            sprite->image.width, sprite->image.height, &viewport);
        const auto oldImage = read(); const auto oldShadow = changed(oldImage, false);
        const int contactDistance = distance(support, shadow), oldDistance = distance(support, oldShadow);
        LONG shadowTop = height, shadowBottom = -1;
        for (const auto p : shadow) { shadowTop = std::min(shadowTop, p.y); shadowBottom = std::max(shadowBottom, p.y); }
        bool ok = result.drawn && !shadow.empty();
        if (type.noShadow || !runtimeArtShadowEnabled(findArtTypeForObject(object, context), true)) {
            ++suppressedChecks;
            ok = result.suppressed && !result.drawn && shadow.empty();
        } else if (airborneControl) {
            ok = ok && result.generatedFallback && shadowImage == oldImage;
        } else if (result.generatedFallback) {
            ++fallbackChecks;
            ok = ok && contactDistance <= 2 && shadowBottom >= bottom - 1 && shadowTop <= bottom + 2;
            if (oldDistance > 2 && contactDistance <= 2) ++oldGapWitnesses;
        } else {
            ++authoredChecks;
            clear(); int count = 0;
            const bool valid = drawRuntimeShpShadowFrame(surface.dc, viewport, anchor, context, camera,
                sprite->shadowImage, &count);
            ok = ok && valid && result.shpMask && read() == shadowImage;
        }
        // The shadow query must not relocate or recolour the original body.
        clear(); (void)tryDrawObjectShpSprite(surface.dc, viewport, object, anchor, context, camera, cache);
        ok = ok && read() == bodyImage;
        ++checks;
        rows << type.id << ',' << pose << ',' << object.facing << ',' << zoom << ',' << sprite->frameIndex
            << ',' << body.size() << ',' << shadow.size() << ',' << bottom << ',' << shadowTop << ',' << shadowBottom
            << ',' << contactDistance << ',' << oldDistance << ',' << result.generatedFallback << ',' << ok << '\n';
        if (picture) {
            const auto name = type.id + "-" + pose + "-" + std::to_string(object.facing) + "-" + std::to_string(zoom);
            for (const auto& item : std::array<std::pair<const char*, const std::vector<std::uint32_t>*>,2>{{
                {"actual", &shadowImage}, {"old-ground", &oldImage}}}) {
                std::copy(item.second->begin(), item.second->end(), surface.pixels);
                (void)tryDrawObjectShpSprite(surface.dc, viewport, object, anchor, context, camera, cache);
                const auto composed = read();
                if (!writeRuntimeBgraBmp(output / (name + "-" + item.first + ".bmp"), width, height, composed))
                    return fail("BMP evidence");
            }
        }
        if (!ok) return fail(type.id + " " + pose + " contact=" + std::to_string(contactDistance)
            + " old=" + std::to_string(oldDistance) + " fallback=" + std::to_string(result.generatedFallback));
        return true;
    };
    for (const auto& type : sourceContext.rules->getInfantryTypes()) {
        WorldObjectInstance object; object.kind = WorldObjectKind::Infantry; object.type = type.id;
        if (type.id == "WEEDGUY" || runtimeObjectLooksAircraftForRender(object, context)) {
            inventory << type.id << ',' << (type.id == "WEEDGUY" ? "original registration has no artwork" : "aircraft ground-shadow contract") << '\n';
            ++excluded; continue;
        }
        inventory << type.id << ",original body rendered\n";
        if (!check(type, "Guard", 0, 60, type.id == "E1" || type.id == "E2")) return false;
    }
    for (const char* id : {"E1", "E2", "FLAKT", "DOG"}) {
        const auto* type = sourceContext.rules->findUnitType("InfantryType", id);
        if (!type) return fail(std::string(id) + " stock rule missing");
        for (int direction = 0; direction < 8; ++direction) for (int zoom : {48,60,72})
            for (const char* pose : {"Guard", "Walk0", "Walk1", "Prone"})
                if (!check(*type, pose, direction, zoom, zoom == 60 && direction == 2)) return false;
    }
    const auto* gi = sourceContext.rules->findUnitType("InfantryType", "E1");
    if (!gi || !check(*gi, "AirborneGuard", 0, 60, false, true)) return false;
    RuntimeShpFrameImage blackOnly; blackOnly.width = blackOnly.height = 3;
    blackOnly.pixels.assign(9, 12); POINT unchanged{17,23}; RuntimeMapCamera camera;
    if (runtimeInfantryFallbackContact(blackOnly, {100,100}, camera, unchanged)
        || unchanged.x != 17 || unchanged.y != 23) return fail("black-only fallback changed");
    if (!fallbackChecks || oldGapWitnesses < 8) return fail("missing real old-ground gap counterexamples");
    if (!runRuntimeFlyingInfantryShadowRenderContractCheck(sourceWorld, sourceContext, errorMessage)) return false;
    std::cout << "Infantry contact shadows: checks=" << checks << " fallback=" << fallbackChecks
        << " authored=" << authoredChecks << " suppressed=" << suppressedChecks
        << " oldGapWitnesses=" << oldGapWitnesses << " excluded=" << excluded
        << " bodyUnchanged=yes airborneUnchanged=yes\n";
    errorMessage.clear(); return true;
}
