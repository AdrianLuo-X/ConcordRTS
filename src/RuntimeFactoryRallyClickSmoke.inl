bool runRuntimeFactoryRallyClickSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    std::string scene;
    const auto fail = [&](const std::string& why) { error = "factory rally click " + scene + ": " + why; return false; };
    const char* output = std::getenv("CONCORD_FACTORY_RALLY_OUTPUT");
    const std::string root = output && *output ? output : "build/smoke/factory-rally/checks";
    std::filesystem::create_directories(root);
    const std::array<std::array<const char*, 3>, 10> types{{
        {{"GAPILE", "E1", "Americans"}}, {{"NAHAND", "E2", "Russians"}}, {{"YABRCK", "INIT", "YuriCountry"}},
        {{"GAWEAP", "MTNK", "Americans"}}, {{"NAWEAP", "HTNK", "Russians"}}, {{"YAWEAP", "LTNK", "YuriCountry"}},
        {{"GAYARD", "DEST", "Americans"}}, {{"NAYARD", "SUB", "Russians"}}, {{"YAYARD", "BSUB", "YuriCountry"}},
        {{"GAAIRC", "ORCA", "British"}}
    }};
    int cases = 0, saves = 0, deliveries = 0, images = 0;
    for (const auto& type : types) for (bool routed : {false, true}) {
        scene = std::string(type[0]) + (routed ? "-routed" : "-direct");
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        s.config = live.config; s.config.computerAiEnabled = false; s.config.multiplayerActive = false;
        s.config.playerSlots.clear(); s.config.playerCountry = type[2]; s.config.crates = false;
        s.report = live.report; s.art = live.art; s.theater = live.theater;
        IniFile stock;
        if (!stock.loadFromFile(live.report.rulesPath) || !s.rules.loadFromIni(stock)) return fail("native rules");
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true; s.runtimePreferencePersistenceEnabled = false;
        s.economy.owner = type[2]; s.economy.credits = 100000; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true;
        initializeRuntimeLocalProductionLanes(s.economy); s.economy.localProductionLanes.nextSerial = 2;
        const auto* building = s.rules.findBuildingType(type[0]);
        const bool naval = building->naval || building->waterBound;
        const bool aircraft = toUpperAsciiLocal(building->factory) == "AIRCRAFTTYPE";
        s.world = live.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        int tileIndex = 0;
        if (naval) {
            tileIndex = -1;
            for (int tile = 0; tile < static_cast<int>(s.theater.getTotalTileSlots()); ++tile) {
                TerrainCell probe; probe.tileIndex = tile; probe.subTileIndex = 0;
                const auto info = runtimeTerrainInfoForCell(s, probe);
                if (info.water && !info.bridge && !info.shore && !info.cliff) { tileIndex = tile; break; }
            }
            if (tileIndex < 0) return fail("native water tile missing");
        }
        for (int y = 40; y <= 88; ++y) for (int x = 40; x <= 88; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = tileIndex; cell.subTileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
        }
        for (int id = 1; id <= 2; ++id) {
            WorldObjectInstance factory; factory.instanceId = id; factory.kind = WorldObjectKind::Structure;
            factory.type = type[0]; factory.owner = type[2]; factory.alive = true; factory.mission = "Guard";
            factory.poweredOperational = true; factory.x = 54 + id * 10; factory.y = 64;
            factory.cell = runtimeCellKey(factory.x, factory.y); factory.health = runtimeMaxHealthForObject(s, factory);
            s.world.accessObjects().push_back(factory);
        }
        s.terrainByCell = buildRuntimeTerrainLookup(s.world); initializeRuntimeNavigation(s);
        s.structureFootprints = buildRuntimeStructureFootprints(s);
        RuntimeSnapshotActiveStateScope active(s);
        const RECT client{0, 0, 1280, 720}; const RECT viewport = runtimeMapViewport(client);
        centerRuntimeCameraOnCell(s, client, {69, 65}); ensureRuntimeCameraForViewport(s, viewport);
        HWND window = CreateWindowExW(0, L"STATIC", L"Factory rally click check", WS_POPUP,
            0, 0, 1280, 720, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) return fail("input window");
        struct Cleanup { HWND h; ~Cleanup() { SetWindowLongPtrW(h, GWLP_USERDATA, 0); DestroyWindow(h); } } cleanup{window};
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));
        const auto pointFor = [&](int id) -> std::optional<POINT> {
            for (const auto& object : runtimeProjectedObjectsForViewport(s, viewport)) if (object.object && object.object->instanceId == id) {
                for (LONG y = std::max(viewport.top, object.bounds.top); y < std::min(viewport.bottom, object.bounds.bottom); y += 3)
                    for (LONG x = std::max(viewport.left, object.bounds.left); x < std::min(viewport.right, object.bounds.right); x += 3) {
                        const POINT p{x, y}; const auto* hit = runtimeCursorObjectAtPoint(s, viewport, p);
                        if (hit && hit->instanceId == id) return p;
                    }
            }
            return std::nullopt;
        };
        const auto first = pointFor(1), second = pointFor(2);
        if (!first || !second) return fail("factory hit points");
        const RuntimeMultiplayerGameplayCell firstGoal{63, 73}, oldGoal{74, 73}, goal{73, 72};
        const auto screen = [&](RuntimeMultiplayerGameplayCell cell) {
            return runtimeScreenPointForCell(s, viewport, {cell.x, cell.y}, terrainLevelAtRuntimeCell(s, cell.x, cell.y));
        };
        const auto click = [&](POINT point, WPARAM modifiers = 0) {
            runtimeWindowProc(window, WM_LBUTTONDOWN, MK_LBUTTON | modifiers, MAKELPARAM(point.x, point.y));
            runtimeWindowProc(window, WM_LBUTTONUP, modifiers, MAKELPARAM(point.x, point.y));
        };
        const auto rallyMatches = [&](const RuntimeWindowState& state, int id, RuntimeMultiplayerGameplayCell cell) {
            const auto rally = state.playerCommands.rallyPoints.find(id);
            return rally != state.playerCommands.rallyPoints.end() && rally->second.cell == cell
                && rally->second.owner == runtimeCanonicalHumanHouseOwner(type[2]);
        };
        int primaryCommands = 0, rallyCommands = 0;
        if (routed) {
            initializeRuntimeOfflineGameplayRouting(s);
            s.multiplayerIntentRouter.setCaptureCallback([&](const auto& command, std::string&) {
                if (const auto* body = std::get_if<RuntimeMultiplayerGameplaySetPrimaryFactoryCommand>(&command.body)) {
                    ++primaryCommands; return applyRuntimePlayerPrimaryFactory(s, s.economy.owner, body->objectId);
                }
                if (const auto* body = std::get_if<RuntimeMultiplayerGameplaySetRallyPointCommand>(&command.body)) {
                    ++rallyCommands; return applyRuntimePlayerRallyPoint(s, s.economy.owner, body->objectId, body->cell);
                }
                return false;
            });
        }
        click(*first);
        if (s.selectedObjectIds != std::vector<int>{1} || !s.playerCommands.rallyPoints.empty()) return fail("first click should select only");
        click(screen(firstGoal));
        if (!rallyMatches(s, 1, firstGoal) || s.selectedObjectIds != std::vector<int>{1}) return fail("mouse did not set first rally or preserve selection");
        click(*second); click(*second);
        const auto key = std::pair{runtimeCanonicalHumanHouseOwner(type[2]), runtimePlayerFactoryCategory(s, *findObjectById(s, 2))};
        if (!s.playerCommands.primaryFactories.contains(key) || s.playerCommands.primaryFactories.at(key) != 2) return fail("primary click regressed");
        auto cursor = runtimeMouseCursorFrameForPoint(s, client, screen(goal));
        if (!cursor || cursor->frameIndex < 31 || cursor->frameIndex > 40) return fail("ground lacks movement cursor");
        click(screen(oldGoal)); click(screen(goal));
        if (!rallyMatches(s, 1, firstGoal) || !rallyMatches(s, 2, goal)
            || s.selectedObjectIds != std::vector<int>{2} || s.playerCommands.primaryFactories.at(key) != 2)
            return fail("replacement rally affected another factory/primary/selection");
        if (routed && (primaryCommands != 1 || rallyCommands != 3)) return fail("wrong command routing/count");
        for (WPARAM modifier : {WPARAM(MK_CONTROL), WPARAM(MK_SHIFT)}) {
            click(screen(oldGoal), modifier); s.selectedObjectIds = {2};
            if (!rallyMatches(s, 2, goal)) return fail("modified click changed rally");
        }
        s.activeSidebarTool = RuntimeSidebarTool::Repair; click(screen(oldGoal)); s.activeSidebarTool = RuntimeSidebarTool::None;
        if (!rallyMatches(s, 2, goal)) return fail("repair click changed rally");
        s.playerLocalCommands.planning = true; click(screen(oldGoal)); s.playerLocalCommands.planning = false; s.selectedObjectIds = {2};
        if (!rallyMatches(s, 2, goal)) return fail("waypoint click changed rally");
        click({100, viewport.top - 1}); s.selectedObjectIds = {2};
        if (!rallyMatches(s, 2, goal)) return fail("click outside map changed rally");
        s.offlineGameplayIntentRouting=false;
        auto* chosen = findMutableObjectById(s, 2);
        chosen->owner = "Neutral";
        if (runtimeSelectedFactoryCanSetRallyPoint(s)) return fail("foreign factory allowed"); chosen->owner = type[2];
        chosen->buildAnimTicksRemaining = 10;
        if (runtimeSelectedFactoryCanSetRallyPoint(s)) return fail("unfinished factory allowed"); chosen->buildAnimTicksRemaining = 0;
        chosen->type = "GACNST";
        if (runtimeSelectedFactoryCanSetRallyPoint(s)) return fail("construction yard treated as unit producer"); chosen->type = type[0];
        s.selectedObjectIds = {1, 2};
        if (runtimeSelectedFactoryCanSetRallyPoint(s)) return fail("group selection allowed"); s.selectedObjectIds = {2};
        s.config.playerCountry = "Observer";
        if (runtimeSelectedFactoryCanSetRallyPoint(s)) return fail("observer allowed"); s.config.playerCountry = type[2];
        s.multiplayerLocalSpectator = true;
        if (runtimeSelectedFactoryCanSetRallyPoint(s)) return fail("network spectator allowed"); s.multiplayerLocalSpectator = false;
        runtimeWindowProc(window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(screen(goal).x, screen(goal).y));
        runtimeWindowProc(window, WM_RBUTTONUP, 0, MAKELPARAM(screen(goal).x, screen(goal).y));
        if (!s.selectedObjectIds.empty() || !rallyMatches(s, 2, goal)) return fail("right click did not deselect and retain rally");
        click(*second);
        if (!rallyMatches(s, 2, goal)) return fail("reselection lost rally");
        // The explicit hotkey mode still submits the same existing command.
        if (!handleRuntimePlayerCommandCode(s, client, runtimeMenuKeyboardBindingCode(s, "SetRallyPoint", 'J'), false, 1000))
            return fail("rally hotkey unavailable");
        click(screen(goal));
        if (!rallyMatches(s, 2, goal) || s.playerLocalCommands.rallyTargeting) return fail("explicit targeting regressed");
        if (building->weaponsFactory && !naval && !routed) {
            RuntimeBuildCatalogItem item; item.typeId=type[1]; item.kind=RuntimeBuildKind::Unit;
            item.requiredFactory=building->factory;
            const auto spawn=findRuntimeProductionSpawnForOwner(s,item,type[2],false);
            if(!spawn||!spawn->anchor||spawn->anchor->instanceId!=2)return fail("doorway production fixture");
            const POINT door=runtimeScreenPointForCell(s,viewport,spawn->cell,
                terrainLevelAtRuntimeCell(s,spawn->cell.x,spawn->cell.y));
            s.runtimeTargetLinesEnabled = false;
            std::vector<std::uint32_t> selectedPixels, enabledPixels, clearPixels;
            const std::string prefix=root+"/"+type[0];
            if (!drawRuntimeStateOffscreenAtCell(s, {69, 65}, 1280, 720, error, prefix + "-selected.bmp",
                    false, true, -1, &selectedPixels, true, &s.camera)) return fail("selected rally image: " + error);
            s.runtimeTargetLinesEnabled = true;
            if (!drawRuntimeStateOffscreenAtCell(s, {69, 65}, 1280, 720, error, prefix + "-action-lines.bmp",
                    false, true, -1, &enabledPixels, true, &s.camera)) return fail("action line toggle image: " + error);
            if(selectedPixels!=enabledPixels)return fail("rally indication depends on optional unit action lines");
            s.selectedObjectIds.clear();
            if (!drawRuntimeStateOffscreenAtCell(s, {69, 65}, 1280, 720, error, prefix + "-deselected.bmp",
                    false, true, -1, &clearPixels, true, &s.camera)) return fail("deselected rally image: " + error);
            const POINT p = screen(goal);
            // Sample the actual production doorway and the whole segment.
            for(int n=0;n<10;++n) {
                const int px=static_cast<int>(std::lround(door.x+(p.x-door.x)*n/10.0));
                const int py=static_cast<int>(std::lround(door.y+(p.y-door.y)*n/10.0));
                bool green=false;
                for(int y=py-1;y<=py+1;++y)for(int x=px-1;x<=px+1;++x) {
                    const auto i=static_cast<std::size_t>(y*1280+x);
                    green=green||((selectedPixels.at(i)&0xffffffU)==0x68e668U&&selectedPixels.at(i)!=clearPixels.at(i));
                }
                if(!green)return fail("line missing between production door and destination sample="+std::to_string(n));
            }
            // The old pole/flag occupied this destination neighborhood. All
            // remaining changed pixels must lie on the ground-level segment.
            const double dx=p.x-door.x,dy=p.y-door.y,length=std::hypot(dx,dy);
            for (int y = p.y - 25; y <= p.y + 5; ++y) for (int x = p.x - 9; x <= p.x + 17; ++x) {
                const auto index = static_cast<std::size_t>(y * 1280 + x);
                if (selectedPixels.at(index) != clearPixels.at(index)
                    &&std::abs(dy*(x-door.x)-dx*(y-door.y))/length>1.5)
                    return fail("flag or other elevated destination glyph still rendered");
            }
            s.selectedObjectIds = {2}; images += 3;
        }
        std::ostringstream saved; IniFile ini;
        auto restored = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored, s, false);
        if (!writeRuntimeStateToStream(s, saved, error) || !ini.loadFromString(saved.str())
            || !materializeRuntimeLoadStateFromIni(*restored, ini, error)) return fail("full reload: " + error);
        if (!rallyMatches(*restored, 1, firstGoal) || !rallyMatches(*restored, 2, goal)
            || restored->playerCommands.primaryFactories.at(key) != 2) return fail("reload lost per-factory settings");
        ++saves;
        // Pad aircraft share the mouse command; exercise complete ground/naval
        // delivery here, including real exit leases and movement after reload.
        if (!aircraft) for (auto* state : {&s, restored.get()}) {
            RuntimeSnapshotActiveStateScope producing(*state);
            RuntimeBuildCatalogItem item; item.typeId = type[1]; item.requiredFactory = building->factory; item.naval = naval;
            item.kind = toUpperAsciiLocal(building->factory) == "INFANTRYTYPE" ? RuntimeBuildKind::Infantry : RuntimeBuildKind::Unit;
            const auto lane = item.kind == RuntimeBuildKind::Infantry ? RuntimeProductionLane::Infantry
                : naval ? RuntimeProductionLane::Ships : RuntimeProductionLane::Vehicles;
            const auto location = findRuntimeProductionSpawnForOwner(*state, item, type[2], false);
            if (!location || !location->anchor || location->anchor->instanceId != 2) return fail("wrong producing factory");
            if (!completeRuntimeProductionOrder(*state, item, false, lane, 1)) return fail("native production: " + state->economy.status);
            const int unitId = state->world.getObjects().back().instanceId;
            if (!state->playerCommands.queues.contains(unitId)) return fail("production did not queue its factory rally");
            bool arrived = false, departed = false;
            for (int frame = 0; frame < state->config.fixedTickHz * 90; ++frame) {
                ++state->tick; advanceRuntimeSimulation(*state, false);
                const auto* unit = findObjectById(*state, unitId);
                const auto move = state->behaviors.find(unitId);
                if (!unit || !unit->alive) return fail("produced unit disappeared");
                if (runtimeFactoryDeliveryOwnsEgress(*state, unitId) && move != state->behaviors.end()
                    && move->second.moveTarget == RuntimeCellPoint{goal.x, goal.y}) return fail("rally replaced factory exit before release");
                departed = departed || !runtimeFactoryDeliveryOwnsEgress(*state, unitId);
                if (unit->x == goal.x && unit->y == goal.y && !unit->renderMoveActive
                    && (move == state->behaviors.end() || move->second.state == RuntimeBehaviorState::Complete)) {
                    arrived = true; break;
                }
            }
            if (!arrived || !departed || !state->factoryDeliveries.leases().empty()) {
                const auto* unit = findObjectById(*state, unitId); const auto move = state->behaviors.find(unitId);
                return fail("rally arrival incomplete: at=" + std::to_string(unit->x) + "," + std::to_string(unit->y)
                    + " mission=" + unit->mission + " behavior=" + (move == state->behaviors.end() ? "none"
                        : std::to_string(static_cast<int>(move->second.state))) + " status=" + state->simulationStatus);
            }
            ++deliveries;
        }
        ++cases; std::cout << "Factory rally " << scene << " PASS\n";
    }
    std::cout << "Factory rally click passed: " << cases << " native/direct/routed mouse scenes, " << saves
        << " full reloads, " << deliveries << " real factory-exit/rally arrivals, " << images
        << " marker images; primary selection, replacement, independent factories, modifiers, observer guards and hotkey preserved\n";
    return true;
}
