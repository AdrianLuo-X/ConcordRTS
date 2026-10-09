// Retained sell mode is a Win32 input contract. No test calls the sale or
// repair business function directly; even routed commands originate in WM.
bool runRuntimeSellModeSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    IniFile original; RulesDatabase rules;
    if (!original.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(original)) {
        error = "Sell mode: original rules unavailable"; return false;
    }
    const auto* power = rules.findBuildingType("GAPOWR");
    if (!power || power->unsellable || !power->repairable || !power->clickRepairable || power->strength < 2) {
        error = "Sell mode: original sellable/repairable GAPOWR unavailable"; return false;
    }
    int landTile = -1;
    for (int tile = 0; tile < static_cast<int>(live.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(live, cell);
        if (info.passable && !info.water && !info.cliff && !info.bridge) { landTile = tile; break; }
    }
    if (landTile < 0) { error = "Sell mode: original land TMP unavailable"; return false; }
    constexpr int width = 1280, height = 720;
    constexpr std::uint32_t background = 0x00283840U;
    const RECT client{0, 0, width, height}, viewport = runtimeMapViewport(client), sidebar = runtimeSidebarRect(client);
    int scenes = 0, clicks = 0, cursorChecks = 0, originalPixelChecks = 0, acceptedSales = 0, acceptedRepairs = 0;
    std::string label, stage; std::vector<std::string> failures;
    const auto fail = [&](const std::string& why) {
        error = "Sell mode " + label + " [" + stage + "]: " + why; return false;
    };
    const auto run = [&](int hz, bool routed) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = live.config; s.report = live.report; s.world = live.world;
        s.rules = rules; s.art = live.art; s.theater = live.theater;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.config.fixedTickHz = hz; s.config.playerCountry = "Americans"; s.config.gameMode = "Battle";
        s.config.multiplayerActive = s.config.computerAiEnabled = s.config.noCooldown = false;
        s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear();
        // This fixture exercises the original L key rather than a user's
        // persisted remapping; loading the reference layout is normal UI setup.
        s.config.keyboardReferenceLayout = false; s.config.keyBindings.clear();
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        s.report.map.size.clear(); s.report.map.localSize.clear();
        s.mapFacts.theater = live.mapFacts.theater; s.mapFacts.lighting = live.mapFacts.lighting;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y = 48; y <= 84; ++y) for (int x = 48; x <= 84; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = landTile; cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        s.economy.owner = "Commander"; s.economy.credits = 1000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        initializeRuntimeLocalProductionLanes(s.economy);
        s.skirmishCountryByOwner["commander"] = "Americans";
        RuntimeHumanHouseAuthority house; house.owner = "Commander"; house.canonicalOwner = "commander";
        house.country = "Americans"; house.localFacade = true; s.humanHouseAuthorities.push_back(house);
        const auto add = [&](int id, RuntimeCellPoint cell, bool damaged = false, const std::string& owner = "Commander") {
            WorldObjectInstance object; object.instanceId = id; object.mapId = "SellMode" + std::to_string(id);
            object.kind = WorldObjectKind::Structure; object.type = "GAPOWR"; object.owner = owner;
            object.x = cell.x; object.y = cell.y; object.cell = runtimeCellKey(cell.x, cell.y);
            object.alive = object.poweredOperational = true; object.mission = "Guard";
            object.health = power->strength - (damaged ? 1 : 0); object.currentAmmo = -1;
            s.world.accessObjects().push_back(object);
        };
        add(1, {60, 60}); add(2, {66, 60}); add(3, {60, 66}); add(4, {66, 66}, true);
        add(5, {72, 66}, false, "SkirmishPlayer2");
        initializeRuntimeNavigation(s); completeRuntimeWorldRefresh(s); rebuildRuntimeEconomyPower(s);
        RuntimeSnapshotActiveStateScope active(s);
        centerRuntimeCameraOnCell(s, client, {65, 64}); ensureRuntimeCameraForViewport(s, viewport);
        if (!s.world.getSummary().loaded || !s.camera.initialized) return fail("loaded world / camera unavailable");
        int routedSales = 0, routedRepairs = 0;
        if (routed) {
            initializeRuntimeOfflineGameplayRouting(s);
            s.multiplayerIntentRouter.setCaptureCallback([&](const auto& command, std::string& reason) {
                const bool applied = dispatchRuntimeOfflineGameplayCommand(s, command, reason);
                if (applied && std::get_if<RuntimeMultiplayerGameplaySellCommand>(&command.body)) ++routedSales;
                if (applied && std::get_if<RuntimeMultiplayerGameplayRepairCommand>(&command.body)) ++routedRepairs;
                return applied;
            });
        }
        if (runtimeUsesGameplayIntentRouter(s) != routed) return fail("direct/routed fixture authority mismatch");
        HWND window = CreateWindowExW(0, L"STATIC", L"Retained sell mode smoke", WS_POPUP,
            0, 0, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) return fail("hidden Win32 input window unavailable");
        struct Cleanup {
            RuntimeWindowState& state; HWND window; HDC screen = nullptr; WNDPROC originalProc = nullptr;
            ~Cleanup() {
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                if (originalProc) SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalProc));
                if (GetCapture() == window) ReleaseCapture();
                releaseRuntimeBackBuffer(state.backBuffer); if (screen) ReleaseDC(nullptr, screen);
                DestroyWindow(window);
            }
        } cleanup{s, window, GetDC(nullptr)};
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));
        cleanup.originalProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(runtimeWindowProc)));
        if (!cleanup.originalProc) return fail("actual Win32 input procedure unavailable");
        if (!cleanup.screen || !ensureRuntimeBackBuffer(s.backBuffer, cleanup.screen, width, height)) return fail("native cursor DIB unavailable");
        const auto move = [&](POINT p) { SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y)); };
        const auto click = [&](POINT p, bool right = false) {
            move(p);
            SendMessageW(window, right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN,
                right ? MK_RBUTTON : MK_LBUTTON, MAKELPARAM(p.x, p.y));
            SendMessageW(window, right ? WM_RBUTTONUP : WM_LBUTTONUP, 0, MAKELPARAM(p.x, p.y));
            ++clicks;
        };
        const auto objectPoint = [&](int id) -> std::optional<POINT> {
            invalidateRuntimeObjectProjectionCache(s);
            const auto& projections = runtimeProjectedObjectsForViewport(s, viewport);
            const auto hits = [&](POINT p) {
                const auto* selected = findProjectedObjectAtPoint(projections, p);
                const auto* cursor = runtimeCursorObjectAtPoint(s, viewport, p);
                return pointInRectInclusive(viewport, p) && selected && selected->object
                    && selected->object->instanceId == id && cursor && cursor->instanceId == id;
            };
            for (const auto& projected : projections) if (projected.object && projected.object->instanceId == id) {
                for (const auto p : projected.footprintHitCenters) if (hits(p)) return p;
                const POINT middle{(projected.bounds.left + projected.bounds.right) / 2,
                    (projected.bounds.top + projected.bounds.bottom) / 2};
                if (hits(middle)) return middle;
                for (int y = projected.bounds.top; y < projected.bounds.bottom; y += 2)
                    for (int x = projected.bounds.left; x < projected.bounds.right; x += 2)
                        if (hits({x, y})) return POINT{x, y};
            }
            return std::nullopt;
        };
        std::optional<POINT> empty;
        for (int y = 70; y <= 78 && !empty; ++y) for (int x = 58; x <= 74 && !empty; ++x) {
            const RuntimeCellPoint cell{x, y};
            const POINT p = runtimeScreenPointForCell(s, viewport, cell, terrainLevelAtRuntimeCell(s, x, y));
            const POINT picked = approximateCellFromScreenPoint(s, viewport, p);
            if (pointInRectInclusive(viewport, p) && picked.x == x && picked.y == y
                && !runtimeCursorObjectAtPoint(s, viewport, p)
                && !findProjectedObjectAtPoint(runtimeProjectedObjectsForViewport(s, viewport), p)) empty = p;
        }
        if (!empty) return fail("actual visible empty map cell unavailable");
        const auto first = objectPoint(1), second = objectPoint(2), third = objectPoint(3), repair = objectPoint(4), enemy = objectPoint(5);
        if (!first || !second || !third || !repair || !enemy) return fail("distinct actual structure hit points unavailable");
        std::vector<RuntimePaletteColor> palette; std::string resource;
        const auto cacheMix = pathString(std::filesystem::path(runtimeAssetRootPath(s.config)) / "ra2" / "cache.mix");
        const auto mouseMix = pathString(std::filesystem::path(runtimeAssetRootPath(s.config)) / "ra2" / "conquer.mix");
        if (!loadRuntimePaletteFromMixArchive(cacheMix, s.renderCache, "mousepal", palette, resource, error)
            || palette.size() < 256 || resource.empty()) return fail("original mouse palette: " + error);
        const auto cursor = [&](POINT p, int frameIndex, bool originalPixels = false) {
            move(p); const auto frame = runtimeMouseCursorFrameForPoint(s, client, p);
            const int offsetX = frameIndex == 0 ? 0 : 27, offsetY = frameIndex == 0 ? 0 : 21;
            if (!frame || frame->atlasId != "mouse" || frame->frameIndex != frameIndex
                || frame->offsetX != offsetX || frame->offsetY != offsetY) {
                return fail("cursor expected=" + std::to_string(frameIndex) + " actual="
                    + std::to_string(frame ? frame->frameIndex : -1));
            }
            RuntimeShpFrameImage native;
            if (!loadRuntimeShpFrameFromMixArchive(mouseMix, s.renderCache, "mouse", frameIndex, native, resource, error)
                || resource.empty() || std::none_of(native.pixels.begin(), native.pixels.end(), [](auto index) { return index != 0; }))
                return fail("original mouse.shp frame unavailable: " + error);
            ++cursorChecks;
            if (!originalPixels) return true;
            GdiFlush(); std::fill_n(static_cast<std::uint32_t*>(s.backBuffer.pixels), width * height, background);
            drawRuntimeMouseCursorOverlay(s.backBuffer.memoryDc, client, s); GdiFlush();
            std::vector<std::uint32_t> expected(width * height, background);
            for (int y = 0; y < native.height; ++y) for (int x = 0; x < native.width; ++x) {
                const auto index = native.pixels[y * native.width + x];
                const int dx = p.x - offsetX + native.offsetX + x, dy = p.y - offsetY + native.offsetY + y;
                if (index && dx >= 0 && dx < width && dy >= 0 && dy < height) {
                    const auto color = palette[index]; expected[dy * width + dx] = (color.r << 16) | (color.g << 8) | color.b;
                }
            }
            const auto* actual = static_cast<const std::uint32_t*>(s.backBuffer.pixels);
            for (std::size_t n = 0; n < expected.size(); ++n) if ((actual[n] & 0xffffffU) != expected[n])
                return fail("original cursor overlay pixels differ at " + std::to_string(n) + " frame=" + std::to_string(frameIndex));
            ++originalPixelChecks; return true;
        };
        stage = routed ? "real L sell hotkey" : "real sidebar sell click";
        if (routed) {
            if (runtimeMenuKeyboardBindingCode(s, "ToggleSell", 'L') != 'L') return fail("fresh original L binding changed");
            SendMessageW(window, WM_KEYDOWN, 'L', 1); SendMessageW(window, WM_KEYUP, 'L', 1);
        } else {
            const RECT tool = runtimeSidebarToolRect(sidebar, RuntimeSidebarTool::Sell);
            click({(tool.left + tool.right) / 2, (tool.top + tool.bottom) / 2});
        }
        if (s.activeSidebarTool != RuntimeSidebarTool::Sell) return fail("actual input did not enter Sell");
        stage = "original sellable 129..138 and forbidden 149 cursors";
        for (int phase = 0; phase < 10; ++phase) {
            s.tick = phase * 4;
            if (!cursor(*first, 129 + phase, phase == 0 || phase == 9)) return false;
        }
        if (!cursor(*empty, 149, true) || !cursor(*enemy, 149)) return false;
        const int initialCredits = s.economy.credits;
        const int refund = runtimeBuildingSaleRefund(power, rules.getRefundPercent());
        stage = "first real sale retains Sell and single native refund";
        click(*first);
        if (findObjectById(s, 1) || s.activeSidebarTool != RuntimeSidebarTool::Sell
            || s.economy.credits != initialCredits + refund || !findObjectById(s, 2) || !findObjectById(s, 3))
            return fail("first sale/removal/refund/retained tool failed: " + s.interactionStatus);
        ++acceptedSales;
        click(*first);
        if (s.economy.credits != initialCredits + refund || s.activeSidebarTool != RuntimeSidebarTool::Sell)
            return fail("sold location repeated refund or cancelled Sell");
        stage = "second real sale without another tool activation";
        if (!cursor(*second, 138)) return false;
        click(*second);
        if (findObjectById(s, 2) || s.activeSidebarTool != RuntimeSidebarTool::Sell
            || s.economy.credits != initialCredits + 2 * refund || !findObjectById(s, 3))
            return fail("second consecutive sale/refund failed: " + s.interactionStatus);
        ++acceptedSales;
        stage = "forbidden and empty clicks reject while retaining Sell";
        const int stableCredits = s.economy.credits;
        if (!cursor(*enemy, 149) || !cursor(*empty, 149)) return false;
        click(*enemy); click(*empty);
        if (!findObjectById(s, 5) || findObjectById(s, 5)->health != power->strength
            || s.economy.credits != stableCredits || s.activeSidebarTool != RuntimeSidebarTool::Sell)
            return fail("illegal sale changed target/credits or exited Sell");
        stage = "complete map right click cancels Sell";
        click(*empty, true);
        if (s.activeSidebarTool != RuntimeSidebarTool::None || s.rightDragActive || s.rightDragMoved
            || s.selectionDragActive || !s.selectedObjectIds.empty() || s.economy.credits != stableCredits)
            return fail("real right down/up did not finish cancellation");
        if (!cursor(*empty, 0, true)) return false;
        stage = "third ordinary left click selects rather than sells";
        click(*third);
        if (!findObjectById(s, 3) || findObjectById(s, 3)->health != power->strength
            || s.economy.credits != stableCredits || s.activeSidebarTool != RuntimeSidebarTool::None
            || !selectionContains(s, 3)) return fail("normal click sold/changed the third structure or failed to select");
        stage = "successful Repair preserves its original single-action exit";
        const RECT repairTool = runtimeSidebarToolRect(sidebar, RuntimeSidebarTool::Repair);
        click({(repairTool.left + repairTool.right) / 2, (repairTool.top + repairTool.bottom) / 2});
        if (s.activeSidebarTool != RuntimeSidebarTool::Repair) return fail("real sidebar did not enter Repair");
        click(*repair);
        if (!s.repairingStructureTicks.contains(4) || s.activeSidebarTool != RuntimeSidebarTool::None
            || !findObjectById(s, 4) || findObjectById(s, 4)->health != power->strength - 1
            || s.economy.credits != stableCredits) return fail("Repair did not start/exit with original deferred payment");
        ++acceptedRepairs;
        if (routed && (routedSales != 2 || routedRepairs != 1))
            return fail("actual routed accepted commands differ: sell=" + std::to_string(routedSales)
                + " repair=" + std::to_string(routedRepairs));
        return true;
    };
    for (int hz : {30, 60}) for (bool routed : {false, true}) {
        label = std::to_string(hz) + (routed ? "-routed-L" : "-direct-sidebar"); stage = "original fixture";
        if (run(hz, routed)) { ++scenes; std::cout << "Sell mode " << label << " PASS\n"; }
        else { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    }
    std::cout << "Sell mode results: scenes=" << scenes << "/4 sales=" << acceptedSales << " repairStarts=" << acceptedRepairs
        << " Win32Clicks=" << clicks << " nativeCursorFrames=" << cursorChecks << " originalPixelChecks=" << originalPixelChecks << '\n';
    if (!failures.empty()) { error = std::to_string(failures.size()) + " Sell mode scenes failed; first: " + failures.front(); return false; }
    return true;
}
