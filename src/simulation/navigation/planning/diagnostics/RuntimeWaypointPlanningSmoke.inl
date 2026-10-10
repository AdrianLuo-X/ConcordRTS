bool runRuntimeWaypointPlanningSmokeProbe(std::string& error) {
    using Kind = RuntimeMultiplayerGameplayTargetOrderKind;
    using Op = RuntimePlayerWaypointOperation;
    const auto fail = [&](const std::string& reason) { error = "waypoint planning: " + reason; return false; };
    const auto makeState = []() {
        auto state = std::make_unique<RuntimeWindowState>();
        IniFile ini;
        ini.loadFromString(R"PLAN(
[VehicleTypes]
0=WPTANK
1=WPTARGET
[WPTANK]
Strength=200
Speed=8
Sight=6
GuardRange=6
SpeedType=Track
MovementZone=Normal
Primary=WPGUN
[WPTARGET]
Strength=120
Speed=1
SpeedType=Track
MovementZone=Normal
[WPGUN]
Damage=60
Range=4
ROF=6
Projectile=WPSHOT
Speed=100
OmniFire=yes
Warhead=WPWH
[WPSHOT]
AG=yes
Instant=yes
[WPWH]
Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%
AffectsAllies=no
)PLAN");
        state->rules.loadFromIni(ini); state->rulesLoaded = true;
        state->config.fixedTickHz = 15; state->runtimeSuppressPlayerAudioFeedback = true;
        state->visionEnabled = false; state->economy.owner = "Player";
        RuntimeHumanHouseAuthority authority; authority.owner = "Player";
        authority.canonicalOwner = runtimeCanonicalHumanHouseOwner(authority.owner);
        authority.localFacade = true; state->humanHouseAuthorities.push_back(std::move(authority));
        for (int y = 0; y < 20; ++y) for (int x = 0; x < 40; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x; cell.tileIndex = 0;
            state->world.accessTerrainCells().push_back(cell);
            state->terrainInfoByCell.emplace(runtimeCellKey(x, y), RuntimeTerrainInfo{});
        }
        state->terrainByCell = buildRuntimeTerrainLookup(state->world);
        return state;
    };
    const auto add = [](RuntimeWindowState& state, int id, int x, int y, bool enemy = false) {
        WorldObjectInstance actor; actor.instanceId = id; actor.kind = WorldObjectKind::Unit;
        actor.owner = enemy ? "Enemy" : "Player"; actor.type = enemy ? "WPTARGET" : "WPTANK";
        actor.mapId = "Plan" + std::to_string(id); actor.x = x; actor.y = y;
        actor.cell = runtimeCellKey(x, y); actor.health = enemy ? 120 : 200;
        actor.alive = actor.poweredOperational = true; actor.mission = "Guard";
        state.world.accessObjects().push_back(std::move(actor));
    };
    const auto tick = [](RuntimeWindowState& state) { ++state.tick; (void)advanceRuntimeSimulation(state, false); };
    const auto point = [](RuntimeWindowState& state, int actor, int x, int y) {
        state.selectedObjectIds = {actor};
        return dispatchRuntimeTargetOrderFromUi(state, Kind::Move, {x, y});
    };
    const RECT client{0, 0, 1280, 720};
    const RECT viewport = runtimeMapViewport(client);
    {
        auto storage = makeState(); auto& state = *storage;
        add(state, 1, 2, 5); add(state, 2, 2, 11); initializeRuntimeNavigation(state);
        centerRuntimeCameraOnCell(state, client, {14, 8});
        if (!handleRuntimePlayerCommandCode(state, client, 'Z', false, 10) || !state.playerLocalCommands.planning
            || !point(state, 1, 10, 5) || !point(state, 1, 15, 5) || !point(state, 1, 25, 5)
            || !point(state, 2, 18, 11)) return fail("planning UI did not accept two groups");
        const auto removedId = state.playerCommands.waypoints.routes.at(1).points[1].id;
        for (int i = 0; i < 80; ++i) tick(state);
        if (findObjectById(state, 1)->x != 2 || findObjectById(state, 2)->x != 2)
            return fail("groups moved while planning");
        const auto encoded = state.playerCommands.serialize();
        RuntimePlayerCommandState restored;
        if (!restored.restore(encoded, error) || restored.serialize() != encoded)
            return fail("held plan did not survive save/restore");
        state.playerCommands = std::move(restored);
        const auto p = runtimePlayerWaypointScreenPoint(state, viewport, state.playerCommands.waypoints.routes.at(1).points[1]);
        state.selectedObjectIds.clear();
        if (!p || !handleRuntimePlayerWaypointClick(state, viewport, *p)
            || state.playerLocalCommands.selectedWaypoint != removedId
            || !handleRuntimePlayerCommandCode(state, client, VK_DELETE, false, 20)
            || state.playerCommands.waypoints.routes.at(1).points.size() != 2
            || state.playerCommands.waypoints.routes.at(1).points.back().order.target
                != RuntimeMultiplayerGameplayTarget{RuntimeMultiplayerGameplayCellTarget{{25, 5}}})
            return fail("click/Delete did not connect the surrounding points");
        if (!handleRuntimePlayerCommandCode(state, client, 'Z', false, 30)) return fail("commit hotkey");
        tick(state);
        if (!state.playerCommands.waypoints.routes.at(1).active || !state.playerCommands.waypoints.routes.at(2).active)
            return fail("groups did not start in the same simulation tick");
        bool consumedFirst = false;
        for (int i = 0; i < 1200 && !state.playerCommands.waypoints.routes.empty(); ++i) {
            tick(state);
            const auto route = state.playerCommands.waypoints.routes.find(1);
            consumedFirst = consumedFirst || (route != state.playerCommands.waypoints.routes.end() && route->second.points.size() == 1);
        }
        if (!consumedFirst || !state.playerCommands.waypoints.routes.empty()
            || findObjectById(state, 1)->x != 25 || findObjectById(state, 2)->x != 18)
            return fail("ordinary routes did not finish and remove consumed points");
    }
    {
        auto storage = makeState(); auto& state = *storage;
        add(state, 1, 2, 5); add(state, 2, 21, 8, true); initializeRuntimeNavigation(state);
        centerRuntimeCameraOnCell(state, client, {14, 8});
        if (!dispatchRuntimePlayerWaypointFromUi(state, Op::Begin)
            || !point(state, 1, 8, 5) || !point(state, 1, 20, 5)
            || !point(state, 1, 20, 11) || !point(state, 1, 8, 11)) return fail("patrol creation");
        const auto loopId = state.playerCommands.waypoints.routes.at(1).points[1].id;
        const auto p = runtimePlayerWaypointScreenPoint(state, viewport, state.playerCommands.waypoints.routes.at(1).points[1]);
        if (!p || !handleRuntimePlayerWaypointClick(state, viewport, *p)
            || state.playerCommands.waypoints.routes.at(1).loopStart != loopId) return fail("click did not close the patrol loop");
        if (const char* imagePath = std::getenv("CONCORD_WAYPOINT_SMOKE_IMAGE")) {
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = 1280; info.bmiHeader.biHeight = -720;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            void* pixels = nullptr; HDC dc = CreateCompatibleDC(nullptr);
            HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
            if (!dc || !bitmap || !pixels) return fail("waypoint overlay render surface");
            auto previous = SelectObject(dc, bitmap);
            std::fill_n(static_cast<std::uint32_t*>(pixels), 1280 * 720, 0x0029352b);
            drawRuntimePlayerWaypoints(dc, viewport, state); GdiFlush();
            const bool saved = writeRuntimeSmokeBmp(imagePath, pixels, 1280, 720, error);
            SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc);
            if (!saved) return fail("waypoint overlay capture");
        }
        std::string reason;
        if (validateRuntimePlayerWaypointCommand(state, "Enemy", {Op::DeletePoint, loopId}, reason))
            return fail("another owner was allowed to delete the route");
        if (!dispatchRuntimePlayerWaypointFromUi(state, Op::Commit)) return fail("patrol commit");
        int transitions = 0; std::uint64_t previousPoint = 0; bool fought = false;
        for (int i = 0; i < 2200 && transitions < 10; ++i) {
            tick(state);
            const auto route = state.playerCommands.waypoints.routes.find(1);
            if (route == state.playerCommands.waypoints.routes.end()) return fail("patrol disappeared after a lap");
            const auto current = route->second.points[route->second.cursor].id;
            if (previousPoint && previousPoint != current) ++transitions;
            previousPoint = current;
            const auto* enemy = findObjectById(state, 2);
            fought = fought || !enemy || !enemy->alive || enemy->health < 120;
            if (i == 100) {
                RuntimePlayerCommandState restored;
                const auto saved = state.playerCommands.serialize();
                if (!restored.restore(saved, error) || restored.serialize() != saved) return fail("active patrol save/restore");
                state.playerCommands = std::move(restored);
            }
        }
        if (!fought || transitions < 10 || state.playerCommands.waypoints.routes.at(1).points.size() != 3)
            return fail("patrol did not fight, resume and repeat; transitions=" + std::to_string(transitions));
        if (!dispatchRuntimePlayerWaypointFromUi(state, Op::Begin)) return fail("reopen active patrol");
        const auto position = runtimeCellPointForObject(*findObjectById(state, 1));
        for (int i = 0; i < 50; ++i) tick(state);
        if (runtimeCellPointForObject(*findObjectById(state, 1)) != position) return fail("editing patrol failed to pause movement");
        if (!dispatchRuntimePlayerWaypointFromUi(state, Op::DeletePoint, loopId)
            || state.playerCommands.waypoints.routes.at(1).points.size() != 2
            || !state.playerCommands.waypoints.routes.at(1).loopStart) return fail("deleting a committed loop point broke the remaining loop");
        dispatchRuntimePlayerWaypointFromUi(state, Op::Commit);
        state.selectedObjectIds = {1};
        if (!dispatchRuntimeTargetOrderFromUi(state, Kind::Move, {4, 4})
            || state.playerCommands.waypoints.routes.contains(1)) return fail("replacement did not cancel patrol");
    }
    std::cout << "Waypoint planning: held groups=yes simultaneous start=yes point editing=yes patrol combat/laps=yes save=yes\n";
    return true;
}
