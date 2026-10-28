bool runtimeProjectileActorUsesPrismBeam(const RuntimeProjectileRenderActor& actor,
    const RuntimeMapRenderContext& context) {
    const auto weapon = toLowerAscii(actor.weaponId);
    if (runtimeProjectileActorUsesPrismTankBeam(actor)
        || toLowerAscii(actor.renderIntent) == "prismsupportbeam"
        || weapon == "prismshot" || weapon == "prismsupport") return true;
    if (context.rules == nullptr) return false;
    const auto* tower = context.rules->findBuildingType(
        context.rules->getSpecialMechanismRules().prismSupport.prismType);
    return tower != nullptr && !weapon.empty()
        && (weapon == toLowerAscii(tower->primaryWeapon)
            || weapon == toLowerAscii(tower->elitePrimaryWeapon));
}

void drawRuntimePrismBeam(HDC dc, const RECT& viewport,
    const RuntimeMapRenderContext& context, POINT from, POINT to,
    COLORREF hue, int thickness, bool support, double intensity = 1.0) {
    // The light band must blend with the scene, including at its centre.
    // Tint toward white instead of outlining the line with an opaque dark pen.
    const COLORREF pale = RGB((GetRValue(hue) + 510) / 3,
        (GetGValue(hue) + 510) / 3, (GetBValue(hue) + 510) / 3);
    // Support links use the stored volley width as well as primary beams.
    // Their lower opacity still distinguishes them without hiding the terrain.
    const double core = 1.0 + 0.25 * (std::clamp(thickness, 1, 32) - 1);
    const double radius = core + (support ? 0.8 : 1.25);
    const double dx = static_cast<double>(to.x) - from.x;
    const double dy = static_cast<double>(to.y) - from.y;
    const double lengthSquared = dx * dx + dy * dy;
    const double length = std::sqrt(lengthSquared);
    const bool direct = hasPixelSurface(context);
    if (direct && dc != nullptr) GdiFlush();
    const int left = std::max<int>(viewport.left, direct ? 0 : viewport.left);
    const int top = std::max<int>(viewport.top, direct ? 0 : viewport.top);
    const int right = std::min<int>(viewport.right, direct ? context.surfaceWidth : viewport.right);
    const int bottom = std::min<int>(viewport.bottom, direct ? context.surfaceHeight : viewport.bottom);
    const auto pixel = [&](int x, int y) {
        if (x < left || x >= right || y < top || y >= bottom) return;
        const double t = lengthSquared == 0.0 ? 0.0 : std::clamp(
            ((x - from.x) * dx + (y - from.y) * dy) / lengthSquared, 0.0, 1.0);
        const double distance = std::hypot(x - from.x - t * dx, y - from.y - t * dy);
        const double coverage = std::clamp((radius - distance) / (radius - core), 0.0, 1.0);
        const int alpha = static_cast<int>(std::lround((support ? 110.0 : 150.0)
            * std::clamp(intensity, 0.0, 1.0) * coverage));
        if (alpha <= 0) return;
        if (direct) {
            blendSurfacePixelWithTint(context, x, y, pale, alpha);
        } else if (dc != nullptr) {
            const COLORREF background = concord::ui::win32::MapNativeOverlayWin32::readPixel(dc, x, y);
            if (background != CLR_INVALID) SetPixelV(dc, x, y, runtimeBlendTint(background, pale, alpha));
        }
    };
    // Visit each covered pixel once along the major axis. This avoids opaque
    // overlaps and keeps work proportional to beam length, even diagonally.
    if (std::abs(dx) >= std::abs(dy)) {
        const double extent = radius * (dx == 0.0 ? 1.0 : length / std::abs(dx));
        const int first = std::max(left, static_cast<int>(std::floor(std::min(from.x, to.x) - radius)));
        const int last = std::min(right - 1, static_cast<int>(std::ceil(std::max(from.x, to.x) + radius)));
        for (int x = first; x <= last; ++x) {
            const double t = dx == 0.0 ? 0.0 : std::clamp((x - from.x) / dx, 0.0, 1.0);
            const double y = from.y + t * dy;
            for (int iy = std::max(top, static_cast<int>(std::floor(y - extent)));
                iy <= std::min(bottom - 1, static_cast<int>(std::ceil(y + extent))); ++iy) pixel(x, iy);
        }
    } else {
        const double extent = radius * length / std::abs(dy);
        const int first = std::max(top, static_cast<int>(std::floor(std::min(from.y, to.y) - radius)));
        const int last = std::min(bottom - 1, static_cast<int>(std::ceil(std::max(from.y, to.y) + radius)));
        for (int y = first; y <= last; ++y) {
            const double t = std::clamp((y - from.y) / dy, 0.0, 1.0);
            const double x = from.x + t * dx;
            for (int ix = std::max(left, static_cast<int>(std::floor(x - extent)));
                ix <= std::min(right - 1, static_cast<int>(std::ceil(x + extent))); ++ix) pixel(ix, y);
        }
    }
}

bool runRuntimePrismBeamRenderContractCheck(std::string& error) {
    const auto fail = [&](const char* message) { error = message; return false; };
    constexpr RECT viewport{4, 4, 60, 60};
    std::vector<std::uint32_t> original(64 * 64);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x)
        original[y * 64 + x] = packRuntimeDibColor(x < 32 ? RGB(20, 40, 60) : RGB(100, 120, 140));
    RuntimeMapRenderContext context;
    context.surfaceWidth = context.surfaceHeight = 64;
    const COLORREF hue = RGB(112, 255, 226);
    const std::pair<POINT, POINT> segments[] = {
        {{8,32}, {56,32}}, {{32,8}, {32,56}}, {{8,8}, {56,56}},
        {{8,56}, {56,8}}, {{-30,15}, {85,46}}, {{32,32}, {32,32}}
    };
    for (bool isSupport : {false, true}) for (const auto& [from, to] : segments) {
        auto pixels = original;
        context.surfacePixels = pixels.data();
        drawRuntimePrismBeam(nullptr, viewport, context, from, to, hue, 1, isSupport);
        const double dx = to.x - from.x, dy = to.y - from.y, square = dx * dx + dy * dy;
        int changed = 0;
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            if (pixels[y * 64 + x] == original[y * 64 + x]) continue;
            ++changed;
            const double t = square == 0 ? 0 : std::clamp(((x - from.x) * dx + (y - from.y) * dy) / square, 0.0, 1.0);
            if (x < 4 || x >= 60 || y < 4 || y >= 60
                || std::hypot(x - from.x - t * dx, y - from.y - t * dy) > 2.5)
                return fail("prism beam bent or leaked outside its viewport/straight band");
        }
        if (changed < (square == 0 ? 5 : 100)) return fail("prism beam too thin or missing");
        auto reverse = original;
        context.surfacePixels = reverse.data();
        drawRuntimePrismBeam(nullptr, viewport, context, to, from, hue, 1, isSupport);
        if (reverse != pixels) return fail("prism beam depends on firing direction");
    }
    auto pixels = original;
    context.surfacePixels = pixels.data();
    drawRuntimePrismBeam(nullptr, viewport, context, {8,32}, {56,32}, hue, 1, false);
    const auto dark = colorRefFromRuntimeDibColor(pixels[32 * 64 + 20]);
    const auto light = colorRefFromRuntimeDibColor(pixels[32 * 64 + 44]);
    if (GetRValue(dark) <= 112 || GetGValue(dark) >= 255
        || GetBValue(light) <= GetBValue(dark) + 5 || GetBValue(light) >= GetBValue(dark) + 80)
        return fail("prism centre did not remain pale and translucent over both backgrounds");
    auto thick = original;
    context.surfacePixels = thick.data();
    drawRuntimePrismBeam(nullptr, viewport, context, {8,32}, {56,32}, hue, 5, false);
    if (thick[35 * 64 + 20] == original[35 * 64 + 20]) return fail("boosted prism thickness ignored");
    auto support = original;
    context.surfacePixels = support.data();
    drawRuntimePrismBeam(nullptr, viewport, context, {8,32}, {56,32}, hue, 3, true);
    if (support[32 * 64 + 20] == original[32 * 64 + 20]
        || support[34 * 64 + 20] == original[34 * 64 + 20]
        || support[35 * 64 + 20] != original[35 * 64 + 20]) return fail("prism support link width incorrect");
    // Terrain contrast must survive through the centre, not merely the edge.
    // The old 210/255 main alpha retained only 18% of this contrast.
    const int mainContrast = GetBValue(light) - GetBValue(dark);
    const auto supportDark = colorRefFromRuntimeDibColor(support[32 * 64 + 20]);
    const auto supportLight = colorRefFromRuntimeDibColor(support[32 * 64 + 44]);
    const int supportContrast = GetBValue(supportLight) - GetBValue(supportDark);
    if (mainContrast < 28 || mainContrast > 40
        || supportContrast < 40 || supportContrast > 56)
        return fail("prism beams obscure too much of the background contrast");
    // Each additional tower broadens the band while keeping the centre's
    // opacity constant: boosting must not turn it into an opaque white stripe.
    for (bool isSupport : {false, true}) {
    int previousCrossSection = 0;
    for (int supporters = 0; supporters <= 8; ++supporters) {
        auto boosted = original;
        context.surfacePixels = boosted.data();
        drawRuntimePrismBeam(nullptr, viewport, context, {8,32}, {56,32},
            hue, 1 + 2 * supporters, isSupport);
        int crossSection = 0;
        for (int y = 4; y < 60; ++y) {
            const auto before = colorRefFromRuntimeDibColor(original[y * 64 + 20]);
            const auto after = colorRefFromRuntimeDibColor(boosted[y * 64 + 20]);
            crossSection += std::abs(static_cast<int>(GetRValue(after)) - GetRValue(before))
                + std::abs(static_cast<int>(GetGValue(after)) - GetGValue(before))
                + std::abs(static_cast<int>(GetBValue(after)) - GetBValue(before));
        }
        if (crossSection <= previousCrossSection
            || boosted[32 * 64 + 20] != (isSupport ? support : pixels)[32 * 64 + 20])
            return fail(isSupport ? "prism support count did not broaden translucent links"
                : "prism support count did not broaden a translucent main beam");
        previousCrossSection = crossSection;
    }
    }
    std::cout << "Prism raster: centre background contrast=" << mainContrast
        << "/80 support=" << supportContrast
        << "/80; 0..8 primary AND support-link widths increase without opaque overlap\n";
    return true;
}

bool runRuntimePrismChargeRenderContractCheck(const RuntimeMapRenderContext& supplied,
    WorldObjectInstance tower, const std::string& root, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "Prism charge render: " + why; return false; };
    constexpr int width = 260, height = 260, columns = 4;
    constexpr std::uint32_t background = 0x00304035;
    std::vector<std::uint32_t> pixels(width * height, background);
    std::vector<std::uint32_t> sheet(width * columns * height * 2, background);
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    RuntimeMapRenderContext context = supplied;
    context.surfacePixels = pixels.data(); context.surfaceWidth = width; context.surfaceHeight = height;
    context.vision = nullptr;
    std::unordered_map<std::string, COLORREF> colors{{toLowerAscii(tower.owner), RGB(240, 40, 40)}};
    context.ownerRemapColors = &colors;
    const auto* type = context.rules->findBuildingType(tower.type);
    if (!type) return fail("missing tower rules");
    tower.buildAnimTicksRemaining = tower.buildAnimTicksTotal = 0;
    tower.poweredOperational = true;
    RuntimeMapCamera camera; camera.initialized = true;
    const auto projected = projectCellXY(tower.x, tower.y, tower.level, camera);
    camera.worldOffsetX = projected.x - width / 2; camera.worldOffsetY = projected.y - 200;
    const RECT viewport{0, 0, width, height};
    const auto center = runtimeObjectBodyAnchorForRender(tower, viewport, context, camera);
    int frames = 0;
    for (int hz : {15, 30, 60}) for (int damaged : {0, 1}) {
        context.fixedTickHz = hz;
        tower.health = damaged ? type->strength / 4 : type->strength;
        tower.chargeTurretStartTick = hz * 11 + 1;
        tower.mission = "DelayedFire";
        for (int age = 0; age <= 28 * hz / 15; ++age) {
            context.animationTick = tower.chargeTurretStartTick + age;
            std::fill(pixels.begin(), pixels.end(), background);
            RuntimeStructureAnimationStats stats;
            const int count = drawRuntimeStructureActiveAnimations(nullptr, viewport, tower,
                center, context, camera, *cache, &stats, false, nullptr, nullptr, 1);
            const int expected = damaged * 10 + std::min(9, age * 15 / (3 * hz));
            if (count != 1 || stats.specialLayers != 1 || stats.firstDrawnFrame != expected
                || stats.firstSpecialSourcePath.empty())
                return fail("hz=" + std::to_string(hz) + " age=" + std::to_string(age)
                    + " expected=" + std::to_string(expected) + " actual=" + std::to_string(stats.firstDrawnFrame));
            ++frames;
            if (hz == 30 && (age == 0 || age == 24 || age == 56)) {
                const int column = age == 0 ? 0 : age == 24 ? 1 : 2;
                tryDrawObjectShpSprite(nullptr, viewport, tower, center, context, camera, *cache);
                drawRuntimeStructureActiveAnimations(nullptr, viewport, tower, center, context, camera, *cache);
                for (int y = 0; y < height; ++y) std::copy_n(pixels.begin() + y * width, width,
                    sheet.begin() + (damaged * height + y) * width * columns + column * width);
            }
        }
        for (const char* mission : {"Guard", "Attacking", "Idle", "Stop"}) {
            tower.mission = mission;
            RuntimeStructureAnimationStats stats;
            if (drawRuntimeStructureActiveAnimations(nullptr, viewport, tower, center, context,
                    camera, *cache, &stats, false, nullptr, nullptr, 1) != 0 || stats.specialLayers != 0)
                return fail(std::string("charge persisted during ") + mission);
        }
        tower.mission = "DelayedFire"; tower.poweredOperational = false;
        if (drawRuntimeStructureActiveAnimations(nullptr, viewport, tower, center, context,
                camera, *cache, nullptr, false, nullptr, nullptr, 1) != 0) return fail("unpowered charge visible");
        tower.poweredOperational = true; tower.mission = "Guard";
        if (hz == 30) {
            std::fill(pixels.begin(), pixels.end(), background);
            tryDrawObjectShpSprite(nullptr, viewport, tower, center, context, camera, *cache);
            drawRuntimeStructureActiveAnimations(nullptr, viewport, tower, center, context, camera, *cache);
            for (int y = 0; y < height; ++y) std::copy_n(pixels.begin() + y * width, width,
                sheet.begin() + (damaged * height + y) * width * columns + 3 * width);
        }
    }
    for (COLORREF color : {RGB(255, 20, 20), RGB(20, 60, 255), RGB(20, 230, 50), RGB(255, 220, 20)}) {
        colors[toLowerAscii(tower.owner)] = color;
        for (const char* weapon : {"PrismShot", "PrismSupport", "Comet", "SuperComet", "CometFragment", "SuperCometFragment"}) {
            RuntimeProjectileRenderActor actor; actor.weaponId = weapon; actor.owner = tower.owner;
            actor.houseColorLaser = true;
            if (runtimeProjectileActorBeamColor(actor, context) != color) return fail("beam lost the chosen player color");
        }
    }
    if (!root.empty() && !writeRuntimeBgraBmp(root + "/tower-charge-native.bmp", width * columns, height * 2, sheet))
        return fail("preview write failed");
    std::cout << "Prism charge render: " << frames << " original frames, normal/damaged, 15/30/60 Hz, idle/power/color=ok\n";
    return true;
}
