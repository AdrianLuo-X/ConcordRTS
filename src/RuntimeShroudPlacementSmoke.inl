bool runRuntimeShroudPlacementSmokeProbe(std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "shroud placement: " + message;
        return false;
    };
    auto stateStorage = std::make_unique<RuntimeWindowState>();
    RuntimeWindowState& state = *stateStorage;
    state.economy.owner = "Alpha";
    state.economy.credits = 500;
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.visionEnabled = true;
    state.tick = 1;

    IniFile rulesIni;
    if (!rulesIni.loadFromString(
            "[BuildingTypes]\n0=PROBE\n1=GAP\n2=ANCHOR\n"
            "[PROBE]\nStrength=100\nFoundation=2x2\n"
            "[ANCHOR]\nStrength=100\nSight=1\n"
            "[GAP]\nStrength=500\nSight=1\nPowered=yes\n"
            "GapGenerator=yes\nGapRadiusInCells=5\n"
        ) || !state.rules.loadFromIni(rulesIni)) {
        return fail("could not load synthetic building rules");
    }
    state.rulesLoaded = true;
    state.structureFootprints["PROBE"] = {2, 2, true};
    for (int y = 0; y <= 16; ++y) {
        for (int x = 0; x <= 16; ++x) {
            TerrainCell terrain;
            terrain.x = x;
            terrain.y = y;
            terrain.cellIndex512 = y * 512 + x;
            state.world.accessTerrainCells().push_back(terrain);
            state.terrainInfoByCell.emplace(
                runtimeCellKey(x, y), RuntimeTerrainInfo{}
            );
        }
    }
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    for (int index = 0; index < 2; ++index) {
        WorldObjectInstance anchor;
        anchor.instanceId = 100 + index;
        anchor.kind = WorldObjectKind::Structure;
        anchor.type = "ANCHOR";
        anchor.owner = index == 0 ? "Alpha" : "Beta";
        anchor.x = 4; anchor.y = 6 + index * 2;
        anchor.alive = true; anchor.health = 100;
        state.world.accessObjects().push_back(anchor);
    }
    const std::vector<RuntimeVisionObserverDefinition> observers{
        {"Alpha", {"Alpha"}}, {"Beta", {"Beta"}}
    };
    state.vision.update(state.world, &state.rules, observers, state.tick);

    RuntimeBuildCatalogItem item;
    item.kind = RuntimeBuildKind::Building;
    item.typeId = "PROBE";
    item.label = "Probe";
    item.cost = 100;
    if (!stageRuntimeReadyPlacementForProbe(state, item)) {
        return fail("could not stage a ready building");
    }
    const RuntimeCellPoint placementCell{6, 6};
    state.economy.placementCell = placementCell;
    state.camera.tileWidth = 40;
    state.camera.tileHeight = 20;
    state.camera.initialized = true;
    constexpr RECT viewport{0, 0, 320, 200};
    const auto projected = runtimeProjectCellXY(6, 6, 0, state.camera);
    state.camera.worldOffsetX = projected.x - 160;
    state.camera.worldOffsetY = projected.y - 100;
    struct PreviewSurface {
        RuntimeBackBuffer buffer;
        ~PreviewSurface() { releaseRuntimeBackBuffer(buffer); }
    } surface;
    HDC screenDc = GetDC(nullptr);
    const bool surfaceReady = ensureRuntimeBackBuffer(
        surface.buffer, screenDc, 320, 200
    );
    if (screenDc != nullptr) {
        ReleaseDC(nullptr, screenDc);
    }
    if (!surfaceReady) {
        return fail("could not allocate preview surface");
    }
    const std::unordered_map<int, int> emptyOccupancy;
    const auto verifyPlacement = [&](int validCells) {
        const bool allowed = validCells == 4;
        const auto checks = runtimePlacementCellChecks(state, item, placementCell);
        if (checks.size() != 4
            || std::count_if(checks.begin(), checks.end(), [](const auto& check) {
                return check.valid;
            }) != validCells
            || runtimeCellCanPlaceSidebarBuilding(state, item, placementCell) != allowed
            || runtimeCellCanPlaceSidebarBuilding(
                state, item, placementCell, &emptyOccupancy
            ) != allowed) {
            return false;
        }
        RuntimeMapRenderContext context;
        drawRuntimePlacementPreview(
            surface.buffer.memoryDc, viewport, state, context
        );
        GdiFlush();
        if (state.lastPlacementPreviewCells != 4
            || state.lastPlacementPreviewValidCells != validCells) {
            return false;
        }
        for (const auto& check : checks) {
            const auto center = runtimeScreenPointForCell(
                state, viewport, check.cell, 0
            );
            const COLORREF expected = check.valid
                ? RGB(80, 238, 128) : RGB(242, 86, 86);
            if ((!check.valid && check.reason != "shrouded")
                || GetPixel(surface.buffer.memoryDc, center.x, center.y)
                    != expected) {
                return false;
            }
        }
        return true;
    };
    if (!verifyPlacement(0)) {
        return fail("unexplored footprint did not reject placement and render red");
    }
    const auto ready = runtimeSelectedPlacement(state);
    const int creditsBefore = state.economy.credits;
    if (!ready.has_value() || placeRuntimeReadyBuilding(state, placementCell)
        || state.world.getObjects().size() != 2
        || state.economy.credits != creditsBefore
        || !runtimeSelectedPlacement(state).has_value()
        || runtimeSelectedPlacement(state)->order.serial != ready->order.serial) {
        return fail("blocked placement changed the world, credits, or ready order");
    }

    // All changes deliberately happen at the same tick/cursor position so
    // the preview must invalidate its cached checks when vision changes.
    state.vision.revealArea("Alpha", 6, 6, 2);
    state.vision.resetShroudArea("Alpha", 7, 7, 0);
    if (!verifyPlacement(3)) {
        return fail("one shrouded corner did not block the whole foundation");
    }
    state.vision.revealArea("Alpha", 7, 7, 0);
    if (!verifyPlacement(4)) {
        return fail("newly explored footprint did not immediately turn green");
    }
    state.vision.update(state.world, &state.rules, observers, state.tick);
    WorldObjectInstance enemy;
    enemy.instanceId = 2;
    enemy.kind = WorldObjectKind::Unit;
    enemy.owner = "Beta";
    enemy.x = 6;
    enemy.y = 6;
    enemy.alive = true;
    enemy.health = 100;
    if (state.vision.visibilityFor("Alpha", 6, 6) != RuntimeCellVisibility::Fogged
        || !runtimeObjectVisibleToOwner(state, "Alpha", enemy)
        || !verifyPlacement(4)) {
        return fail("loss of nearby sight hid enemies or blocked explored terrain");
    }

    state.economy.owner = "Beta";
    if (!verifyPlacement(0)) {
        return fail("another house reused the previous house's green preview");
    }
    state.visionEnabled = false;
    if (!verifyPlacement(4)) {
        return fail("disabled vision unexpectedly blocked legacy placement");
    }
    state.visionEnabled = true;
    state.economy.owner = "Alpha";

    WorldObjectInstance gap;
    gap.instanceId = 1;
    gap.kind = WorldObjectKind::Structure;
    gap.owner = "Beta";
    gap.type = "GAP";
    gap.x = 10;
    gap.y = 6;
    gap.alive = true;
    gap.health = 500;
    gap.poweredOperational = true;
    state.world.accessObjects().push_back(gap);
    state.vision.update(state.world, &state.rules, observers, state.tick);
    if (!verifyPlacement(0)
        || runtimeObjectVisibleToOwner(state, "Alpha", enemy)) {
        return fail("Gap Generator shroud allowed building or exposed an enemy");
    }
    state.world.accessObjects().back().poweredOperational = false;
    state.vision.update(state.world, &state.rules, observers, state.tick);
    if (!verifyPlacement(4)
        || !runtimeObjectVisibleToOwner(state, "Alpha", enemy)) {
        return fail("depowering the gap failed to restore explored visibility");
    }
    error.clear();
    return true;
}
