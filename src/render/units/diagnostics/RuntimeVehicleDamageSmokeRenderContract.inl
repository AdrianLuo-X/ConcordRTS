bool runRuntimeVehicleDamageSmokeRenderContractCheck(
    const RuntimeMapRenderContext& context, const std::vector<WorldObjectInstance>& vehicles,
    const std::vector<std::vector<RuntimeParticleRenderSnapshot>>& smoke, std::string& error
) {
    const auto fail = [&](const std::string& text) { error = "vehicle damage smoke render: " + text; return false; };
    if (vehicles.size() != 4U || smoke.size() != 4U) return fail("four original vehicle poses missing");
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    auto probe = context;
    probe.vision = nullptr;
    constexpr int width = 220, height = 200, columns = 4;
    constexpr std::uint32_t background = 0x004c5850U;
    std::vector<std::uint32_t> pixels(width * height, background);
    std::vector<std::uint32_t> sheet(width * columns * height * 2, background);
    probe.surfacePixels = pixels.data(); probe.surfaceWidth = width; probe.surfaceHeight = height;
    RuntimeMapCamera camera;
    camera.initialized = true;
    const RECT viewport{0, 0, width, height};
    for (int row = 0; row < 2; ++row) for (int col = 0; col < columns; ++col) {
        std::fill(pixels.begin(), pixels.end(), background);
        const auto& vehicle = vehicles[col];
        const auto origin = runtimeObjectDrawPoint(vehicle, {}, camera);
        camera.worldOffsetX = static_cast<int>(std::lround(origin.x)) - width / 2;
        camera.worldOffsetY = static_cast<int>(std::lround(origin.y)) - 145;
        const POINT center = runtimeObjectBodyAnchorForRender(vehicle, viewport, probe, camera);
        if (!tryDrawVoxelSprite(nullptr, viewport, vehicle, center, probe, camera, *cache))
            return fail("original voxel could not be drawn: " + vehicle.type);
        const auto body = pixels;
        if (row == 1) {
            if (smoke[col].empty()) return fail("damaged vehicle has no smoke: " + vehicle.type);
            probe.particleSnapshots = &smoke[col];
            RuntimeMapRenderStats stats;
            drawRuntimeParticleSnapshots(nullptr, viewport, probe, camera, *cache, stats);
            int changed = 0, above = 0;
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                if (pixels[y * width + x] == body[y * width + x]) continue;
                ++changed;
                if (y < center.y - 15) ++above;
            }
            if (stats.particleActorShpDrawn < 1 || stats.particleActorFallbackDrawn != 0
                || stats.particleActorMissing != 0 || changed < 20 || above < 10)
                return fail("original rising SHP smoke pixels missing: " + vehicle.type);
            probe.particleSnapshots = nullptr;
        }
        for (int y = 0; y < height; ++y)
            std::copy_n(pixels.begin() + y * width, width,
                sheet.begin() + (row * height + y) * width * columns + col * width);
    }
    if (const char* path = std::getenv("CONCORD_VEHICLE_DAMAGE_SMOKE_BMP"))
        if (*path != '\0' && !writeRuntimeBgraBmp(path, width * columns, height * 2, sheet))
            return fail("preview write failed");
    if (const char* directory = std::getenv("CONCORD_VEHICLE_DAMAGE_SMOKE_FACING_DIR")) {
        if (*directory != '\0') {
            std::filesystem::create_directories(directory);
            const std::string path = std::string(directory) + "/turret-facing-"
                + std::to_string(vehicles.front().facing) + ".bmp";
            if (!writeRuntimeBgraBmp(path, width * columns, height * 2, sheet))
                return fail("turret smoke preview write failed");
        }
    }
    return true;
}
