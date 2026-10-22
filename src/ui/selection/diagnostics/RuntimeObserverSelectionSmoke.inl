// Exercise player input independently of the command executor's temporary
// selection. Observing a match must leave its authoritative orders untouched.
bool runRuntimeObserverSelectionSmoke(RuntimeWindowState& state, int objectId,
    std::string& error) {
    const auto fail = [&](const std::string& reason) {
        error = "observer selection: " + reason; return false;
    };
    const auto config = state.config;
    const auto owner = state.economy.owner;
    const bool spectator = state.multiplayerLocalSpectator;
    const auto localCommands = state.playerLocalCommands;
    const auto* actor = findObjectById(state, objectId);
    if (!actor) return fail("missing visible actor");
    const auto worldHash = state.world.deterministicSimulationStateHash();
    const RECT client{0, 0, 1000, 700};
    const RECT viewport = runtimeMapViewport(client);
    centerRuntimeCameraOnCell(state, client, runtimeCellPointForObject(*actor));
    const auto& projected = runtimeProjectedObjectsForViewport(state, viewport);
    const auto view = std::find_if(projected.begin(), projected.end(), [&](const auto& item) {
        return item.object && item.object->instanceId == objectId;
    });
    if (view == projected.end()) return fail("actor was not projected");
    const RECT bounds = view->bounds;
    std::optional<POINT> hit;
    for (int y = std::max(viewport.top, bounds.top); y < std::min(viewport.bottom, bounds.bottom) && !hit; ++y)
        for (int x = std::max(viewport.left, bounds.left); x < std::min(viewport.right, bounds.right); ++x) {
            const auto* picked = findProjectedObjectAtPoint(projected, {x, y});
            if (picked && picked->object && picked->object->instanceId == objectId) { hit = POINT{x, y}; break; }
        }
    if (!hit) return fail("actor has no unobscured hit pixels");
    HWND window = CreateWindowExW(0, L"STATIC", L"Observer selection check", WS_POPUP,
        0, 0, 1000, 700, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) return fail("hidden input window");
    struct Cleanup { HWND window; ~Cleanup() { DestroyWindow(window); } } cleanup{window};
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    const auto click = [&](WPARAM modifiers) {
        runtimeWindowProc(window, WM_LBUTTONDOWN, MK_LBUTTON | modifiers, MAKELPARAM(hit->x, hit->y));
        runtimeWindowProc(window, WM_LBUTTONUP, modifiers, MAKELPARAM(hit->x, hit->y));
    };
    const auto box = [&] {
        runtimeWindowProc(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bounds.left, bounds.top));
        runtimeWindowProc(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(bounds.right, bounds.bottom));
        runtimeWindowProc(window, WM_LBUTTONUP, 0, MAKELPARAM(bounds.right, bounds.bottom));
    };
    state.config.keyboardReferenceLayout = true; state.config.keyBindings.clear();
    ensureRuntimeMenuKeyboardLoaded(state);
    int checks = 0;
    for (int role = 0; role < 3; ++role) {
        state.config = config;
        state.config.keyboardReferenceLayout = true; state.config.keyBindings.clear();
        state.multiplayerLocalSpectator = role == 2;
        state.config.multiplayerActive = role == 2;
        if (role == 1) state.config.playerSlots.clear();
        // A network spectator may retain a combatant facade after a role
        // change. Ownership filtering alone cannot protect this case.
        state.economy.owner = role == 2 ? actor->owner : owner;
        for (WPARAM modifiers : {WPARAM{0}, WPARAM{MK_SHIFT}, WPARAM{MK_CONTROL}}) {
            state.selectedObjectIds.clear(); click(modifiers);
            if (!state.selectedObjectIds.empty() || state.leftPointer.active)
                return fail("mouse click selected an actor in role " + std::to_string(role));
            ++checks;
        }
        state.selectedObjectIds.clear(); box();
        if (!state.selectedObjectIds.empty() || state.leftPointer.active)
            return fail("box selected an actor");
        ++checks;
        state.playerLocalCommands.teamCreate(0, {objectId}, [](int) { return true; });
        for (int key : std::array<int, 9>{'T', 'P', 'Y', 'U', 'M', 'N', '1', '1' | runtimePlayerShiftKey,
                '1' | runtimePlayerAltKey}) {
            // Also prove an obsolete selection cannot survive keyboard input.
            state.selectedObjectIds = {objectId};
            handleRuntimePlayerCommandCode(state, client, key, false, 1000 + checks * 700);
            if (!state.selectedObjectIds.empty()) return fail("selection shortcut retained an actor");
            ++checks;
        }
        runtimeWindowProc(window, WM_MOUSEMOVE, 0, MAKELPARAM(hit->x, hit->y));
        const auto* hovered = findProjectedObjectAtPoint(runtimeProjectedObjectsForViewport(state, viewport), *hit);
        if (!hovered || !hovered->object || hovered->object->instanceId != objectId
            || !state.mouseClientPointValid) return fail("hover inspection was blocked");
        const auto camera = state.camera;
        const bool coasting = state.runtimeScrollCoastingEnabled;
        state.runtimeScrollCoastingEnabled = false;
        runtimeWindowProc(window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(hit->x, hit->y));
        runtimeWindowProc(window, WM_MOUSEMOVE, MK_RBUTTON, MAKELPARAM(hit->x + 24, hit->y + 16));
        runtimeWindowProc(window, WM_RBUTTONUP, 0, MAKELPARAM(hit->x + 24, hit->y + 16));
        state.runtimeScrollCoastingEnabled = coasting;
        if (state.camera.worldOffsetX == camera.worldOffsetX && state.camera.worldOffsetY == camera.worldOffsetY)
            return fail("right drag camera pan was blocked");
        state.camera = camera; invalidateRuntimeObjectProjectionCache(state);
    }
    // A regular participant can still click and box-select the same actor.
    state.config = config; state.config.playerSlots.clear(); state.config.playerCountry = "Americans";
    state.config.multiplayerActive = false; state.multiplayerLocalSpectator = false;
    state.economy.owner = actor->owner; state.selectedObjectIds.clear();
    click(0);
    if (state.selectedObjectIds != std::vector<int>{objectId}) return fail("participant click rejected");
    state.selectedObjectIds.clear(); box();
    if (!selectionContains(state, objectId)) return fail("participant box rejected");
    if (state.world.deterministicSimulationStateHash() != worldHash) return fail("input changed the watched world");
    state.config = config; state.economy.owner = owner; state.multiplayerLocalSpectator = spectator;
    state.playerLocalCommands = localCommands; state.selectedObjectIds.clear();
    state.leftPointer.active = state.leftPointer.moved = false;
    std::cout << "Observer selection: " << checks << " mouse/box/shortcut checks, three roles, "
        "hover, right-drag camera and participant input passed\n";
    return true;
}
