bool runRuntimeBuildingPlacementRangeSmoke(std::string& error) {
    const auto fail = [&](const std::string& why) { error = "Building range: " + why; return false; };
    const std::string rulesText =
        "[BuildingTypes]\n0=BASE\n1=PROBE\n2=TOUCH\n3=LONG\n4=WALL\n5=DEFENSE\n6=DEFAULT\n"
        "[VehicleTypes]\n0=MCV\n"
        "[BASE]\nStrength=1000\nBaseNormal=yes\nEligibileForAllyBuilding=yes\nFactory=BuildingType\n"
        "[PROBE]\nStrength=100\nAdjacent=2\nCost=100\n"
        "[TOUCH]\nStrength=100\nAdjacent=0\n"
        "[LONG]\nStrength=100\nAdjacent=4\n"
        "[WALL]\nStrength=100\nAdjacent=4\nBaseNormal=no\nWall=yes\nBuildCat=Combat\n"
        "[DEFENSE]\nStrength=100\nBaseNormal=no\n"
        "[DEFAULT]\nStrength=100\n"
        "[MCV]\nStrength=100\nSpeed=4\nDeploysInto=BASE\n";
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& state = *storage;
    state.config.defaultMapPath = "building-range-smoke";
    state.config.gameMode = "Battle";
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.runtimePreferencePersistenceEnabled = false;
    state.visionEnabled = false;
    state.tick = 1;
    IniFile ini;
    if (!ini.loadFromString(rulesText) || !state.rules.loadFromIni(ini)) return fail("rules fixture");
    state.rulesLoaded = true;
    const auto* defaults = state.rules.findBuildingType("DEFAULT");
    if (!defaults || defaults->adjacent != 1 || !defaults->baseNormal || defaults->eligibleForAllyBuilding
        || !state.rules.findBuildingType("BASE")->eligibleForAllyBuilding
        || state.rules.findBuildingType("DEFENSE")->baseNormal) return fail("typed INI defaults/flags");
    for (const auto& change : {std::pair{"Adjacent=2", "Adjacent=3"},
             std::pair{"BaseNormal=yes", "BaseNormal=no"},
             std::pair{"EligibileForAllyBuilding=yes", "EligibileForAllyBuilding=no"}}) {
        std::string changed = rulesText;
        changed.replace(changed.find(change.first), std::strlen(change.first), change.second);
        IniFile changedIni;
        RulesDatabase changedRules;
        if (!changedIni.loadFromString(changed) || !changedRules.loadFromIni(changedIni)
            || changedRules.getDetailRulesHash() == state.rules.getDetailRulesHash())
            return fail("placement rule omitted from peer rules hash");
    }
    initializeRuntimeLocalProductionLanes(state.economy);
    state.economy.owner = "Alpha";
    state.economy.credits = 900;
    state.structureFootprints["BASE"] = {3, 2, true};
    for (const auto* type : {"PROBE", "TOUCH", "LONG", "DEFAULT"})
        state.structureFootprints[type] = {2, 2, true};
    for (int y = 0; y <= 50; ++y) for (int x = 0; x <= 50; ++x) {
        TerrainCell terrain;
        terrain.x = x; terrain.y = y; terrain.cellIndex512 = y * 512 + x;
        state.world.accessTerrainCells().push_back(terrain);
        state.terrainInfoByCell.emplace(runtimeCellKey(x, y), RuntimeTerrainInfo{});
    }
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    WorldObjectInstance base;
    base.instanceId = 1; base.kind = WorldObjectKind::Structure; base.type = "BASE";
    base.owner = "Alpha"; base.x = 10; base.y = 10; base.health = 1000; base.alive = true;
    state.world.accessObjects().push_back(base);
    RuntimeBuildCatalogItem probe;
    probe.kind = RuntimeBuildKind::Building; probe.typeId = "PROBE";
    probe.label = "Probe"; probe.cost = 100;
    const auto inRange = [&](const char* type, int x, int y) {
        auto item = probe; item.typeId = type;
        return runtimeCellCanPlaceSidebarBuilding(state, item, {x, y});
    };
    if (!inRange("TOUCH", 13, 10) || inRange("TOUCH", 14, 10)
        || !inRange("DEFAULT", 14, 10) || inRange("DEFAULT", 15, 10)
        || !inRange("PROBE", 15, 10) || inRange("PROBE", 16, 10)
        || !inRange("LONG", 17, 10) || inRange("LONG", 18, 10)
        || !inRange("TOUCH", 13, 12) || inRange("TOUCH", 14, 13)
        || !inRange("PROBE", 6, 10) || inRange("PROBE", 5, 10))
        return fail("Adjacent 0/1/2/4 footprint edges or corners");
    const RuntimeCellPoint nearCell{15, 10}, farCell{30, 10};
    if (!runtimeCellCanPlaceBuilding(state, probe, farCell)
        || runtimeCellCanPlaceSidebarBuilding(state, probe, farCell)) return fail("physical/sidebar separation");
    auto& anchor = state.world.accessObjects().front();
    anchor.type = "DEFENSE";
    if (inRange("PROBE", 11, 10)) return fail("BaseNormal=no expanded base");
    anchor.type = "WALL";
    if (inRange("PROBE", 11, 10) || !inRange("WALL", 11, 10)) return fail("wall chain/base distinction");
    anchor = base;
    state.skirmishTeamByOwner["alpha"] = "A";
    state.skirmishTeamByOwner["beta"] = "A";
    state.skirmishTeamByOwner["gamma"] = "B";
    anchor.owner = "Beta";
    state.config.buildOffAlly = false;
    if (inRange("PROBE", 15, 10)) return fail("disabled allied building option");
    state.config.buildOffAlly = true;
    if (!inRange("PROBE", 15, 10)) return fail("eligible allied yard");
    anchor.type = "PROBE";
    if (inRange("PROBE", 13, 10)) return fail("ordinary allied building expanded base");
    anchor = base; anchor.owner = "Gamma";
    if (inRange("PROBE", 15, 10)) return fail("enemy yard expanded base");
    anchor = base;
    state.config.buildOffAlly = false;
    if (!stageRuntimeReadyPlacementForProbe(state, probe)) return fail("ready fixture");
    const auto order = runtimeSelectedPlacement(state)->order;
    const auto beforeHash = runtimeProductionLanesStateHash(state.economy.localProductionLanes);
    if (placeRuntimeReadyBuilding(state, farCell) || state.world.getObjects().size() != 1
        || state.economy.credits != 900
        || beforeHash != runtimeProductionLanesStateHash(state.economy.localProductionLanes))
        return fail("out-of-range click consumed ready order or credits");

    // Exercise the renderer and its same-tick cache, not just the predicate.
    struct Surface { RuntimeBackBuffer buffer; ~Surface() { releaseRuntimeBackBuffer(buffer); } } surface;
    HDC screen = GetDC(nullptr);
    const bool ready = ensureRuntimeBackBuffer(surface.buffer, screen, 480, 280);
    if (screen) ReleaseDC(nullptr, screen);
    if (!ready) return fail("preview surface");
    state.camera.tileWidth = 40; state.camera.tileHeight = 20; state.camera.initialized = true;
    constexpr RECT viewport{0, 0, 480, 280};
    const auto preview = [&](RuntimeCellPoint cell, bool allowed, const char* name) {
        state.economy.placementCell = cell;
        const auto projected = runtimeProjectCellXY(cell.x, cell.y, 0, state.camera);
        state.camera.worldOffsetX = projected.x - 240;
        state.camera.worldOffsetY = projected.y - 120;
        HBRUSH background = CreateSolidBrush(RGB(25, 32, 40));
        FillRect(surface.buffer.memoryDc, &viewport, background);
        DeleteObject(background);
        RuntimeMapRenderContext context;
        drawRuntimePlacementPreview(surface.buffer.memoryDc, viewport, state, context);
        GdiFlush();
        if (state.lastPlacementPreviewValidCells != (allowed ? 4 : 0)
            || runtimePlacementPreviewValid(state) != allowed) return false;
        const auto center = runtimeScreenPointForCell(state, viewport, cell, 0);
        if (GetPixel(surface.buffer.memoryDc, center.x, center.y)
            != (allowed ? RGB(80, 238, 128) : RGB(242, 86, 86))) return false;
        if (const char* output = std::getenv("CONCORD_BUILDING_RANGE_OUTPUT")) {
            return writeRuntimeSmokeBmp(std::string(output) + "/" + name + ".bmp",
                surface.buffer.pixels, 480, 280, error);
        }
        return true;
    };
    if (!preview(nearCell, true, "nearCell") || !preview(farCell, false, "farCell")) return fail("preview red/green");
    if (!preview(nearCell, true, "before-loss")) return fail("initial anchor");
    anchor.alive = false;
    if (!preview(nearCell, false, "destroyed")) return fail("destroyed anchor reused cached preview");
    anchor = base; anchor.owner = "Beta";
    if (!preview(nearCell, false, "captured")) return fail("captured anchor reused cached preview");
    state.config.buildOffAlly = true;
    if (!preview(nearCell, true, "allied-enabled")) return fail("ally option reused cached preview");
    anchor.owner = "Gamma";
    if (!preview(nearCell, false, "enemy")) return fail("enemy reused allied preview");
    anchor = base; anchor.mission = "Limbo";
    if (inRange("PROBE", 15, 10)) return fail("limbo anchor");
    anchor = base; anchor.rubble = true;
    if (inRange("PROBE", 15, 10)) return fail("rubble anchor");
    anchor = base; anchor.poweredOperational = false; anchor.buildAnimTicksRemaining = 20;
    if (!inRange("PROBE", 15, 10)) return fail("power loss or normal buildup removed base area");
    anchor = base;
    const auto suggested = findRuntimePlacementCellNearBase(state, probe);
    if (!suggested || !runtimeCellCanPlaceSidebarBuilding(state, probe, *suggested))
        return fail("default cursor suggestion outside allowed range");

    RuntimeHumanHouseAuthority local;
    local.owner = "Alpha"; local.canonicalOwner = "alpha"; local.localFacade = true;
    state.humanHouseAuthorities.push_back(local);
    RuntimeHumanHouseAuthority remote;
    remote.owner = "Beta"; remote.canonicalOwner = "beta"; remote.slotIndex = 1;
    remote.remoteEconomy.emplace(); remote.remoteTechTree.emplace();
    initializeRuntimeLocalProductionLanes(*remote.remoteEconomy);
    remote.remoteEconomy->owner = "Beta"; remote.remoteEconomy->credits = 700;
    state.humanHouseAuthorities.push_back(std::move(remote));
    std::uint64_t remoteSerial = 0;
    {
        RuntimeHumanHouseScope scope(state, "Beta");
        if (!scope || !stageRuntimeReadyPlacementForProbe(state, probe)) return fail("remote ready fixture");
        remoteSerial = runtimeSelectedPlacement(state)->order.serial;
    }
    state.config.buildOffAlly = false;
    RuntimeMultiplayerGameplayPlaceBuildingCommand command;
    command.productionSerial = remoteSerial; command.buildingTypeId = "PROBE";
    command.cell = {nearCell.x, nearCell.y};
    if (placeRuntimeGameplayBuildingForHumanOwner(state, "Beta", command)
        || state.world.getObjects().size() != 1) return fail("remote command borrowed local base");
    WorldObjectInstance remoteBase = base;
    remoteBase.instanceId = 2; remoteBase.owner = "Beta"; remoteBase.x = 30;
    state.world.accessObjects().push_back(remoteBase);
    command.cell = {35, 10};
    if (!placeRuntimeGameplayBuildingForHumanOwner(state, "Beta", command)
        || state.world.getObjects().back().owner != "Beta"
        || state.economy.owner != "Alpha" || state.economy.credits != 900
        || runtimeHumanHouseEconomy(state, "Beta")->credits != 700)
        return fail("remote command failed at its own base or changed local facade");
    command.productionSerial = order.serial;
    command.cell = {farCell.x, farCell.y};
    if (placeRuntimeGameplayBuildingForHumanOwner(state, "Alpha", command)) return fail("local network range bypass");
    command.cell = {nearCell.x, nearCell.y};
    if (!placeRuntimeGameplayBuildingForHumanOwner(state, "Alpha", command)
        || runtimeSelectedPlacement(state).has_value()) return fail("valid local command did not consume exactly once");

    // A first MCV must still deploy without any other owned building.
    state.world.accessObjects().clear();
    WorldObjectInstance mcv;
    mcv.instanceId = 20; mcv.kind = WorldObjectKind::Unit; mcv.type = "MCV";
    mcv.owner = "Alpha"; mcv.x = 20; mcv.y = 20; mcv.alive = true; mcv.health = 100;
    state.world.accessObjects().push_back(mcv);
    if (!deployRuntimeMcv(state, 20, false, RuntimeCellPoint{20, 20})
        || !runtimeBuildingPlacementInRange(state, probe, {25, 20})) return fail("first MCV deployment");
    std::cout << "Building range: defaults=yes Adjacent=0/1/2/4 foundations=yes wall=yes ally=yes "
        << "preview=yes sameTickLoss=yes readyAtomic=yes remoteAuthority=yes firstMCV=yes rulesHash=yes\n";
    error.clear();
    return true;
}
