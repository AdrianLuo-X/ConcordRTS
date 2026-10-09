bool runRuntimeGarrisonRenderContractCheck(
    const RuntimeMapRenderContext* assetContext, std::string& error
) {
    const auto fail = [&](const std::string& message) {
        error = "garrison rendering: " + message;
        return false;
    };
    IniFile ini;
    RulesDatabase rules;
    if (!ini.loadFromString(
            "[BuildingTypes]\n0=HOUSE\n1=ORDINARY\n"
            "[HOUSE]\nStrength=1000\nCanBeOccupied=yes\nMaxNumberOccupants=10\n"
            "[ORDINARY]\nStrength=1000\n[AudioVisual]\nConditionRed=30%\n")
        || !rules.loadFromIni(ini)) return fail("fixture failed to load");
    RuntimeMapRenderContext context;
    context.rules = &rules;
    WorldObjectInstance house;
    house.kind = WorldObjectKind::Structure;
    house.type = "HOUSE";
    house.alive = true;
    house.health = 301;
    if (selectRuntimeObjectShpFrame(house, context, 4) != 0)
        return fail("usable yellow house rendered as damaged");
    house.garrisonOccupantIds = {101};
    if (selectRuntimeObjectShpFrame(house, context, 4) != 2
        || selectRuntimeObjectShpFrame(house, context, 3) != 2
        || selectRuntimeObjectShpFrame(house, context, 2) != 0)
        return fail("occupied frame or missing-frame fallback incorrect");
    house.health = 300;
    if (selectRuntimeObjectShpFrame(house, context, 4) != 1)
        return fail("red threshold did not switch to damaged art");
    house.garrisonOccupantIds.clear();
    house.health = 1000;
    if (selectRuntimeObjectShpFrame(house, context, 4) != 0)
        return fail("evacuated/repaired house kept occupied frame");
    house.alive = false;
    house.rubble = true;
    house.health = 0;
    if (selectRuntimeObjectShpFrame(house, context, 4) != 3)
        return fail("rubble lost its frame");
    house.type = "ORDINARY";
    house.alive = true;
    house.rubble = false;
    house.health = 400;
    if (selectRuntimeObjectShpFrame(house, context, 4) != 1)
        return fail("ordinary building damage selection changed");

    if (assetContext == nullptr || std::getenv("CONCORD_GARRISON_RENDER_TEST") == nullptr)
        return true;
    auto probe = *assetContext;
    probe.vision = nullptr;
    probe.specialMechanismSnapshots = nullptr;
    probe.detailStatsEnabled = false;
    probe.animationTick = 0;
    std::unordered_map<std::string, COLORREF> colors{
        {"garrisongold", RGB(255, 210, 25)}, {"garrisonblue", RGB(40, 90, 255)}
    };
    probe.ownerRemapColors = &colors;
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    RuntimeMapCamera camera;
    constexpr int size = 512;
    constexpr int columns = 4;
    const std::array<const char*, 3> types{"CAPRS03", "CAHSE01", "NABNKR"};
    std::vector<std::uint32_t> sheet(size * columns * size * types.size(), 0xff243039);
    for (std::size_t row = 0; row < types.size(); ++row) {
        const auto* building = probe.rules->findBuildingType(types[row]);
        if (building == nullptr) return fail(std::string(types[row]) + " rules missing");
        WorldObjectInstance object;
        object.kind = WorldObjectKind::Structure;
        object.type = types[row];
        object.alive = true;
        object.owner = "Neutral";
        object.health = building->strength;
        const auto* empty = loadRuntimeObjectShpSpriteFromCache(object, probe, *cache);
        if (empty == nullptr || !empty->loaded || empty->frameIndex != 0)
            return fail(object.type + " empty artwork missing");
        const auto foundation = runtimeObjectFoundationDimensions(object, probe);
        const auto anchor = runtimeObjectDrawPoint(object, foundation, camera);
        camera.worldOffsetX = static_cast<int>(anchor.x) - size / 2;
        camera.worldOffsetY = static_cast<int>(anchor.y) - size * 2 / 3;
        std::array<std::vector<std::uint32_t>, columns> draws;
        for (int column = 0; column < columns; ++column) {
            object.owner = column == 2 ? "GarrisonBlue" : "GarrisonGold";
            object.garrisonOccupantIds = column == 1 || column == 2
                ? std::vector<int>{101} : std::vector<int>{};
            object.health = column == 3
                ? static_cast<int>(building->strength * probe.rules->getSpecialMechanismRules().conditionRed)
                : building->strength;
            if (column == 0) object.owner = "Neutral";
            const auto* sprite = loadRuntimeObjectShpSpriteFromCache(object, probe, *cache);
            const int expectedFrame = column == 0 ? 0 : column == 3 ? 1 : 2;
            if (sprite == nullptr || !sprite->loaded || sprite->frameIndex != expectedFrame)
                return fail(object.type + " state " + std::to_string(column) + " selected wrong frame");
            draws[column].assign(size * size, 0xff243039);
            probe.surfacePixels = draws[column].data();
            probe.surfaceWidth = probe.surfaceHeight = size;
            if (!drawRuntimeObjectPreviewSprite(nullptr, RECT{0, 0, size, size},
                    object, probe, camera, *cache)) return fail(object.type + " preview did not draw");
            for (int y = 0; y < size; ++y) {
                std::copy_n(draws[column].data() + y * size, size,
                    sheet.data() + (row * size + y) * size * columns + column * size);
            }
        }
        if (draws[0] == draws[1] || draws[1] == draws[2] || draws[1] == draws[3])
            return fail(object.type + " occupancy, house color or evacuation was invisible");
        object.health = building->strength;
        object.owner = "GarrisonGold";
        object.garrisonOccupantIds = {102};
        std::vector<std::uint32_t> repeated(size * size, 0xff243039);
        probe.surfacePixels = repeated.data();
        if (!drawRuntimeObjectPreviewSprite(nullptr, RECT{0, 0, size, size},
                object, probe, camera, *cache) || repeated != draws[1])
            return fail(object.type + " re-entry reused stale artwork or owner color");
    }
    if (const char* path = std::getenv("CONCORD_GARRISON_RENDER_PREVIEW_BMP")) {
        if (!writeRuntimeBgraBmp(path, size * columns, size * types.size(), sheet))
            return fail("preview write failed");
    }
    std::cout << "Garrison rendering: stockBuildings=3 empty/occupied/damaged/rubble=ok "
        << "ownerColors=ok cachedReentry=ok\n";
    return true;
}
