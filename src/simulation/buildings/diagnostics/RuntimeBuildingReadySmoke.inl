bool runRuntimeBuildingReadySmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail = [&](const std::string& why) { error = why; return false; };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& state = *storage;
    state.config = config;
    state.config.gameMode = "Battle";
    state.config.gameModeRulesOverride.clear();
    state.config.computerAiEnabled = false;
    state.config.multiplayerActive = false;
    state.config.noCooldown = false;
    state.config.startingCredits = 100000;
    state.config.startingUnits = 0;
    state.config.playerCountry = "Americans";
    state.config.playerSlots.clear();
    state.config.playerSlots.resize(2);
    for (std::size_t i = 0; i < 2; ++i) {
        auto& slot = state.config.playerSlots[i];
        slot.enabled = true; slot.human = i == 0; slot.local = i == 0;
        slot.country = i == 0 ? "Americans" : "Russians";
        slot.team = "-"; slot.startPosition = static_cast<int>(i) + 1;
    }
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.runtimePreferencePersistenceEnabled = false;
    state.report = buildRuntimeStartupReport(state.config, &state.world, &state.theater,
        &state.art, &state.rules, &state.mapFacts);
    state.rulesLoaded = state.report.rulesLoaded;
    state.artLoaded = state.report.artLoaded;
    state.theaterLoaded = state.report.theaterLoaded;
    if (!state.report.success) return fail("stock map/rules unavailable");
    initializeRuntimeNavigation(state);
    initializeRuntimeEconomy(state);
    if (!initializeRuntimeSkirmishStart(state)) return fail(state.economy.status);
    if (!deployRuntimeMcv(state,state.economy.skirmishStartObjectId,false)) return fail("manual opening deployment");
    state.triggers.bootstrapComplete = true;
    const auto* powerCatalog = findRuntimeGameplayCatalogItem(state, state.economy.owner, "GAPOWR");
    const auto* wallCatalog = findRuntimeGameplayCatalogItem(state, state.economy.owner, "GAWALL");
    if (powerCatalog == nullptr || wallCatalog == nullptr) return fail("missing stock building catalog");
    const RuntimeBuildCatalogItem power = *powerCatalog, wall = *wallCatalog;
    constexpr RECT clientRect{0, 0, 1024, 768};
    const auto key = [&](int code, bool repeated = false) {
        return handleRuntimePlayerCommandCode(state, clientRect, code, repeated, GetTickCount64());
    };
    const auto tickProduction = [&]() { ++state.tick; advanceRuntimeProductionEconomy(state); };
    if (!key('Q') || runtimeSelectedPlacement(state)) return fail("empty production tab selected a building");
    if (!dispatchRuntimeQueueProductionFromUi(state, power)) return fail("initial sidebar click failed");
    tickProduction();
    if (!key('Q') || runtimeSelectedPlacement(state)) return fail("unfinished building selected by hotkey");
    if (!completeRuntimeActiveProductionForProbe(state, power)) return fail("production did not start");
    tickProduction();
    const auto ready = runtimeOldestReadyPlacement(state);
    if (!ready || ready->order.itemKey != power.typeId || runtimeSelectedPlacement(state).has_value())
        return fail("completion did not remain ready without selecting the cursor");
    const int paidCredits = state.economy.credits;
    const auto productionHash = runtimeProductionLanesStateHash(state.economy.localProductionLanes);
    const auto objectCount = state.world.getObjects().size();
    const auto cell = findRuntimePlacementCellNearBase(state, power);
    if (!cell || placeRuntimeReadyBuilding(state, *cell)
        || state.world.getObjects().size() != objectCount || state.economy.credits != paidCredits)
        return fail("map placement succeeded before a second sidebar click");
    for (int tick = 0; tick < 60; ++tick) tickProduction();
    if (runtimeSelectedPlacement(state).has_value()
        || runtimeProductionLanesStateHash(state.economy.localProductionLanes) != productionHash
        || state.economy.credits != paidCredits) return fail("waiting ready order changed or auto-selected");

    // Save/load must preserve an unselected ready building. Selecting it is a
    // separate action, and a save taken while placing keeps that explicit choice.
    const auto checkSave = [&](bool selected) {
        std::ostringstream output;
        if (!writeRuntimeStateToStream(state, output, error)) return false;
        IniFile ini;
        if (!ini.loadFromString(output.str())) return fail("save INI");
        auto loaded = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*loaded, state, state.visionEnabled);
        if (!materializeRuntimeLoadStateFromIni(*loaded, ini, error)) return false;
        ++loaded->tick;
        advanceRuntimeProductionEconomy(*loaded);
        const auto loadedPlacement = findRuntimePlacementCellNearBase(*loaded, power);
        if (!loadedPlacement || !runtimeCellCanPlaceSidebarBuilding(*loaded, power, *loadedPlacement))
            return fail("save/load lost the base placement area");
        return (runtimeSelectedPlacement(*loaded).has_value() == selected
            && runtimeOldestReadyPlacement(*loaded).has_value()
            && loaded->economy.credits == state.economy.credits)
            || fail("save/load changed ready/selected state or charged twice");
    };
    if (!checkSave(false)) return false;

    if (const char* bitmapPath = std::getenv("CONCORD_BUILDING_READY_BMP")) {
        struct Surface {
            RuntimeBackBuffer buffer;
            ~Surface() { releaseRuntimeBackBuffer(buffer); }
        } surface;
        HDC screenDc = GetDC(nullptr);
        const bool allocated = ensureRuntimeBackBuffer(surface.buffer, screenDc, 1024, 768);
        if (screenDc != nullptr) ReleaseDC(nullptr, screenDc);
        if (!allocated) return fail("sidebar surface allocation");
        state.activeBuildTab = RuntimeBuildKind::Building;
        drawRuntimeSidebar(surface.buffer.memoryDc, clientRect, state, 0);
        GdiFlush();
        const RECT sidebar = runtimeSidebarRect(clientRect);
        const int width = sidebar.right - sidebar.left;
        const int height = sidebar.bottom - sidebar.top;
        const auto* pixels = static_cast<const std::uint32_t*>(surface.buffer.pixels);
        std::vector<std::uint32_t> cropped(static_cast<std::size_t>(width) * height);
        for (int y = 0; y < height; ++y)
            std::copy_n(pixels + (y + sidebar.top) * 1024 + sidebar.left, width, cropped.data() + y * width);
        if (!writeRuntimeSmokeBmp(bitmapPath, cropped.data(), width, height, error)) return false;
    }
    if (!beginRuntimePlacementFromUi(state, *ready, true) || !runtimeSelectedPlacement(state))
        return fail("second sidebar click did not activate placement");
    if (!checkSave(true)) return false;
    const RECT mapViewport = runtimeMapViewport(clientRect);
    const POINT mapPoint{mapViewport.left + 100, mapViewport.top + 100};
    if (!handleRuntimeMapRightClick(state, clientRect, mapPoint) || runtimeSelectedPlacement(state)
        || runtimeProductionLanesStateHash(state.economy.localProductionLanes) != productionHash
        || state.economy.credits != paidCredits) return fail("map right-click discarded/refunded the building");
    tickProduction();
    if (runtimeSelectedPlacement(state)) return fail("dismissed placement returned on the next tick");
    if (!beginRuntimePlacementFromUi(state, *ready, true) || !dismissRuntimePlacementFromUi(state)
        || state.economy.credits != paidCredits) return fail("Escape dismissal did not retain the ready order");

    // A hotkey must enter the same preview as moving the mouse after a click,
    // including when the pointer has not moved since the building became ready.
    if (!beginRuntimePlacementFromUi(state, *ready, true)) return fail("mouse preview fixture");
    updateRuntimePlacementPreview(state, mapViewport, mapPoint);
    const auto mousePreviewCell = runtimePlacementCellForUi(state);
    if (!dismissRuntimePlacementFromUi(state)) return fail("mouse preview dismissal");
    state.mouseClientPoint = mapPoint;
    state.mouseClientPointValid = true;
    state.activeBuildTab = RuntimeBuildKind::Defense;
    state.activeSidebarTool = RuntimeSidebarTool::Repair;
    if (!key('Q') || state.activeBuildTab != RuntimeBuildKind::Building
        || !runtimeSelectedPlacement(state) || runtimeSelectedPlacement(state)->order.serial != ready->order.serial
        || runtimePlacementCellForUi(state) != mousePreviewCell
        || state.activeSidebarTool != RuntimeSidebarTool::None
        || runtimeProductionLanesStateHash(state.economy.localProductionLanes) != productionHash
        || state.economy.credits != paidCredits) return fail("Q did not select the ready building at the mouse");
    if (!dismissRuntimePlacementFromUi(state) || !key('Q', true) || runtimeSelectedPlacement(state))
        return fail("held Q reactivated a dismissed building");
    const auto structureBinding = std::find_if(state.runtimeMenuKeyboardBindings.begin(),
        state.runtimeMenuKeyboardBindings.end(), [](const auto& binding) { return binding.first == "StructureTab"; });
    if (structureBinding == state.runtimeMenuKeyboardBindings.end()) return fail("missing building tab binding");
    structureBinding->second = 'I';
    if (!key('I') || !runtimeSelectedPlacement(state)
        || runtimeSelectedPlacement(state)->order.serial != ready->order.serial)
        return fail("ready placement ignored the rebound tab key");
    structureBinding->second = 'Q';
    if (!dismissRuntimePlacementFromUi(state)) return fail("rebound preview dismissal");

    // Canceling the local preview in multiplayer must emit no production cancel
    // command and must not alter the shared ledger.
    RuntimeMultiplayerRuntimeBridge bridge;
    int capturedCommands = 0;
    state.multiplayerBridge = &bridge;
    state.multiplayerIntentRouter.setCaptureCallback([&](const RuntimeMultiplayerGameplayCommand&, std::string&) {
        ++capturedCommands; return true;
    });
    const bool multiplayerSelected = key('Q') && runtimeSelectedPlacement(state).has_value()
        && runtimeSelectedPlacement(state)->order.serial == ready->order.serial
        && runtimePlacementCellForUi(state) == mousePreviewCell;
    const bool multiplayerDismissed = handleRuntimeMapRightClick(state, clientRect, mapPoint);
    const bool multiplayerClear = !state.multiplayerIntentRouter.placement().has_value();
    state.multiplayerBridge = nullptr;
    state.multiplayerIntentRouter.setCaptureCallback({});
    if (!multiplayerSelected || !multiplayerDismissed || !multiplayerClear || capturedCommands != 0
        || runtimeProductionLanesStateHash(state.economy.localProductionLanes) != productionHash)
        return fail("multiplayer preview cancellation changed production");

    // A second ready lane must stay in the sidebar after placing the first.
    if (!stageRuntimeReadyPlacementForProbe(state, wall)) return fail("defense ready fixture");
    const auto wallReady = runtimeSelectedPlacement(state);
    if (!wallReady || !dismissRuntimePlacementFromUi(state)
        || !key('W') || !runtimeSelectedPlacement(state)
        || runtimeSelectedPlacement(state)->order.serial != wallReady->order.serial
        || state.activeBuildTab != RuntimeBuildKind::Defense
        || !key('Q') || !runtimeSelectedPlacement(state)
        || runtimeSelectedPlacement(state)->order.serial != ready->order.serial)
        return fail("Q/W did not independently select their ready lanes");
    tickProduction();
    if (!runtimeSelectedPlacement(state) || runtimeSelectedPlacement(state)->order.serial != ready->order.serial)
        return fail("production tick replaced an explicit selection");
    if (!placeRuntimeReadyBuilding(state, *cell) || runtimeSelectedPlacement(state)
        || !runtimeReadyProductionForItem(state, wall) || state.economy.credits != paidCredits)
        return fail("placing one building auto-selected another or charged twice");
    const auto beforeRefund = state.economy.credits;
    if (!dispatchRuntimeBuildButtonSecondaryActionFromUi(state, wall)
        || runtimeReadyProductionForItem(state, wall) != nullptr || runtimeSelectedPlacement(state)
        || state.economy.credits != beforeRefund + wall.cost)
        return fail("sidebar right-click no longer cancels/refunds production");
    std::cout << "Building ready: completion/wait/map guard, sidebar click, Q/W stationary mouse/repeat/rebind, right-click/Escape, dual lanes, full save/load and multiplayer preview passed\n";
    return true;
}
