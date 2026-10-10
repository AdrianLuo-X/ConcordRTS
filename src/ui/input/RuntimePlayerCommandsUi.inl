// Include immediately before handleRuntimeMapRightClick, after the UI dispatch
// helpers and camera/selection functions are defined.
int runtimePlayerCurrentKeyCode(WPARAM key) {
    return runtimePlayerKeyCode(static_cast<int>(key),
        (GetKeyState(VK_SHIFT) & 0x8000) != 0,
        (GetKeyState(VK_CONTROL) & 0x8000) != 0,
        (GetKeyState(VK_MENU) & 0x8000) != 0);
}

// This is presentation authority only. The simulation temporarily borrows
// selectedObjectIds when applying AI and remote players' authoritative orders.
bool runtimePlayerSelectionAllowed(const RuntimeWindowState& state) {
    return !state.multiplayerLocalSpectator
        && (state.campaignMode || !runtimeLocalObserver(state.config));
}

bool rejectRuntimeObserverSelection(RuntimeWindowState& state) {
    if (runtimePlayerSelectionAllowed(state)) return false;
    state.selectedObjectIds.clear();
    state.selectionDragActive = state.selectionDragMoved = false;
    state.repaintRequested = true;
    return true;
}

bool runtimePlayerLocalSelectable(const RuntimeWindowState& state, int id) {
    if (!runtimePlayerSelectionAllowed(state)) return false;
    const auto* object = findObjectById(state, id);
    return object && object->alive && !runtimeGateObjectIsLimbo(*object)
        && runtimeObjectAcceptsPlayerSelection(state, *object)
        && runtimeCanonicalHumanHouseOwner(object->owner)
            == runtimeCanonicalHumanHouseOwner(state.economy.owner)
        && runtimeObjectVisibleToOwner(state, state.economy.owner, *object);
}

// Click, box and keyboard selection all use the same local ownership gate.
// Hit testing remains unfiltered for hover health and target commands.
void applyRuntimePlayerSelection(RuntimeWindowState& state, const std::vector<int>& ids,
    bool append, bool toggle) {
    std::erase_if(state.selectedObjectIds,
        [&](int id) { return !runtimePlayerLocalSelectable(state, id); });
    std::vector<int> eligible;
    for (int id : ids) if (runtimePlayerLocalSelectable(state, id)) eligible.push_back(id);
    applySelection(state, eligible, append, toggle);
}

bool runtimePlayerMobileSelectable(const RuntimeWindowState& state, const WorldObjectInstance& object) {
    const auto* type = findRuntimeUnitType(state.rules, object);
    return runtimePlayerLocalSelectable(state, object.instanceId) && runtimeObjectCanBeBoxSelected(object)
        && !runtimeFactoryDeliveryOwnsEgress(state, object.instanceId)
        && !(type && (type->spawned || type->missileSpawn));
}

bool dispatchRuntimePlayerFactoryFromUi(RuntimeWindowState& state,
    int id, std::optional<RuntimeMultiplayerGameplayCell> cell
) {
    bool accepted = false;
    std::string error;
    if (runtimeUsesGameplayIntentRouter(state)) {
        refreshRuntimeMultiplayerPlayerIntentAuthority(state);
        accepted = cell ? state.multiplayerIntentRouter.setRallyPoint(id, *cell, error)
            : state.multiplayerIntentRouter.setPrimaryFactory(id, error);
    } else accepted = cell ? applyRuntimePlayerRallyPoint(state, state.economy.owner, id, *cell)
        : applyRuntimePlayerPrimaryFactory(state, state.economy.owner, id);
    state.interactionStatus = accepted ? (cell ? "factory rally point set" : "primary factory selected")
        : "select an owned production factory";
    if (accepted && !cell) emitRuntimeSound(state, "EVA_PrimaryBuildingSelected");
    state.repaintRequested = true;
    return accepted;
}

bool handleRuntimePlayerCommandCode(RuntimeWindowState& state, const RECT& client,
    int code, bool repeated, std::uint64_t now) {
    if (state.runtimeMenuOpen || state.skirmishMatchFinished) return false;
    rejectRuntimeObserverSelection(state);
    if (handleRuntimePlayerReferenceKey(state, client, code, repeated)) return true;
    // With NumLock on the keypad is a second set of group-selection keys.
    const int key = code & 0xff;
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9)
        code = (code & ~0xff) | ('0' + key - VK_NUMPAD0);
    const auto matches = [&](const std::string& action, int fallback) {
        return code == runtimeMenuKeyboardBindingCode(state, action, fallback);
    };
    const auto selectable = [&](int id) { return runtimePlayerLocalSelectable(state, id); };
    const RECT viewport = runtimeMapViewport(client);
    auto& local = state.playerLocalCommands;
    const bool previous=matches("PreviousObject",'M');
    if (previous || matches("NextObject",'N')) {
        if (repeated) return true;
        std::vector<int> ids;
        for (const auto& object:state.world.getObjects()) {
            if (runtimePlayerMobileSelectable(state, object)) ids.push_back(object.instanceId);
        }
        if (ids.empty()) return true;
        std::sort(ids.begin(),ids.end());
        const int current=state.selectedObjectIds.empty()?(previous?std::numeric_limits<int>::max():0):state.selectedObjectIds.front();
        auto next=previous?std::lower_bound(ids.begin(),ids.end(),current):std::upper_bound(ids.begin(),ids.end(),current);
        if (previous) {if (next==ids.begin()) next=ids.end();--next;}
        else if (next==ids.end()) next=ids.begin();
        applyRuntimePlayerSelection(state,{*next},false,false);
        state.playerCommunication.selectedBeacon.reset();
        local.cameraFollow=false;
        runtimePlayerCenterSelection(state,client);
        return true;
    }
    for (int number = 1; number <= 10; ++number) {
        const int digit = number == 10 ? '0' : '0' + number;
        const std::string suffix = "_" + std::to_string(number);
        const int index = number - 1;
        if (matches("TeamCreate" + suffix, digit | runtimePlayerControlKey)) {
            if (!repeated) local.teamCreate(index, state.selectedObjectIds, selectable);
            state.interactionStatus = "team " + std::to_string(number) + " assigned";
            return true;
        }
        const bool add = matches("TeamAddSelect" + suffix, digit | runtimePlayerShiftKey);
        const bool center = matches("TeamCenter" + suffix, digit | runtimePlayerAltKey);
        if (matches("TeamSelect" + suffix, digit) || add || center) {
            if (repeated) return true;
            const auto selected = local.teamSelect(index, state.selectedObjectIds, add, selectable);
            const bool twice = local.lastSelectionKey == digit && now - local.lastSelectionAtMs <= 500;
            applyRuntimePlayerSelection(state, selected, false, false);
            state.playerCommunication.selectedBeacon.reset();
            if ((center || twice) && !selected.empty()) {
                long long x = 0, y = 0;
                for (int id : selected) if (const auto* object = findObjectById(state, id)) {
                    x += object->x; y += object->y;
                }
                centerRuntimeCameraOnCell(state, client, {static_cast<int>(x / selected.size()),
                    static_cast<int>(y / selected.size())});
            }
            local.lastSelectionKey = digit; local.lastSelectionAtMs = now;
            return true;
        }
    }
    for (int index = 0; index < 4; ++index) {
        const std::string suffix = std::to_string(index + 1);
        if (matches("SetView" + suffix, (VK_F1 + index) | runtimePlayerControlKey)) {
            const POINT point = approximateCellFromScreenPoint(state, viewport,
                POINT{(viewport.left + viewport.right) / 2, (viewport.top + viewport.bottom) / 2});
            local.views[static_cast<std::size_t>(index)] = RuntimeMultiplayerGameplayCell{point.x, point.y};
            state.interactionStatus = "view " + suffix + " saved";
            return true;
        }
        if (matches("View" + suffix, VK_F1 + index)) {
            const auto& saved = local.views[static_cast<std::size_t>(index)];
            if (saved) centerRuntimeCameraOnCell(state, client, {saved->x, saved->y});
            return true;
        }
    }
    const bool type = matches("TypeSelect", 'T');
    const bool combat = matches("CombatantSelect", 'P');
    const bool veteran = matches("VeterancyNav", 'Y');
    const bool health = matches("HealthNav", 'U');
    if (type || combat || veteran || health) {
        if (repeated) return true;
        const bool wholeMap = local.lastSelectionKey == code && now - local.lastSelectionAtMs <= 500;
        std::set<int> onScreen;
        for (const auto& projected : runtimeProjectedObjectsForViewport(state, viewport))
            if (projected.object && rectsIntersect(viewport, projected.bounds))
                onScreen.insert(projected.object->instanceId);
        std::vector<RuntimePlayerSelectionCandidate> candidates;
        for (const auto& object : state.world.getObjects()) {
            if (!runtimePlayerMobileSelectable(state, object)) continue;
            const auto* unit = findRuntimeUnitType(state.rules, object);
            candidates.push_back({object.instanceId, toUpperAsciiLocal(object.type),
                object.veterancy, unit && unit->isSelectableCombatant, onScreen.contains(object.instanceId),
                object.health, unit ? std::max(1, unit->strength) : std::max(1, object.health)});
        }
        const auto selected = local.quickSelect(type ? RuntimePlayerQuickSelection::Type
            : combat ? RuntimePlayerQuickSelection::Combatant
            : health ? RuntimePlayerQuickSelection::Health : RuntimePlayerQuickSelection::Veterancy,
            state.selectedObjectIds, candidates, wholeMap, false, wholeMap);
        applyRuntimePlayerSelection(state, selected, false, false);
        state.playerCommunication.selectedBeacon.reset();
        local.lastSelectionKey = code; local.lastSelectionAtMs = now;
        return true;
    }
    if (matches("PlanningMode", 'Z')) {
        if (!repeated) dispatchRuntimePlayerWaypointFromUi(state,
            local.planning ? RuntimePlayerWaypointOperation::Commit : RuntimePlayerWaypointOperation::Begin);
        return true;
    }
    if (matches("SetPrimaryFactory", 'U' | runtimePlayerControlKey)) {
        if (!repeated && state.selectedObjectIds.size() == 1)
            dispatchRuntimePlayerFactoryFromUi(state, state.selectedObjectIds.front(), std::nullopt);
        return true;
    }
    if (matches("SetRallyPoint", 'J')) {
        local.rallyTargeting = state.selectedObjectIds.size() == 1;
        local.followTargeting = false;
        state.interactionStatus = "choose a factory rally point";
        return true;
    }
    if (matches("CenterOnRadarEvent", VK_SPACE)) {
        if (state.playerLastRadarCell) {
            centerRuntimeCameraOnCell(state, client,
                {state.playerLastRadarCell->x, state.playerLastRadarCell->y});
        } else if (!state.triggers.campaignRadarEvents.empty()) {
            centerRuntimeCameraOnCell(state, client, state.triggers.campaignRadarEvents.back().cell);
        }
        return true;
    }
    return false;
}

bool handleRuntimePlayerCommandKey(RuntimeWindowState& state, HWND window, WPARAM key, LPARAM flags) {
    RECT client{}; GetClientRect(window, &client);
    return handleRuntimePlayerCommandCode(state, client, runtimePlayerCurrentKeyCode(key),
        (flags & (static_cast<LPARAM>(1) << 30)) != 0, GetTickCount64());
}

bool handleRuntimePlayerTargetClick(RuntimeWindowState& state, const RECT& viewport, POINT point) {
    auto& local = state.playerLocalCommands;
    if (!pointInRectInclusive(viewport, point)) return false;
    const POINT targetCell = approximateCellFromScreenPoint(state, viewport, point);
    if (state.playerCommunication.beaconTargeting) {
        RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
        command.kind = RuntimePlayerCommunicationKind::PlaceBeacon;
        command.cell = {targetCell.x, targetCell.y};
        if (dispatchRuntimePlayerCommunicationFromUi(state, command)) {
            state.playerCommunication.beaconTargeting = false;
            applyRuntimePlayerSelection(state,{},false,false);
            state.playerCommunication.selectedBeacon.reset();
        }
        state.selectionDragActive = state.selectionDragMoved = false;
        state.repaintRequested = true;
        return true;
    }
    if (handleRuntimePlayerWaypointClick(state, viewport, point)) return true;
    if (!local.followTargeting && !local.rallyTargeting && state.activeSidebarTool==RuntimeSidebarTool::None
        && !(GetKeyState(VK_CONTROL)&0x8000) && !(GetKeyState(VK_MENU)&0x8000) && !(GetKeyState(VK_SHIFT)&0x8000)) {
        const auto owner=runtimePlayerCommunicationViewerOwner(state);
        for (const auto& beacon:state.playerCommunication.beacons) {
            if (beacon.owner!=owner || !runtimePlayerBeaconVisibleToLocal(state,beacon)) continue;
            // The input path resolves ground cell centers. Native selection is
            // within half a cell of the beacon's stored ground point.
            const double dx=static_cast<double>(targetCell.x)-beacon.cell.x;
            const double dy=static_cast<double>(targetCell.y)-beacon.cell.y;
            if (dx*dx+dy*dy<0.25) {
                applyRuntimePlayerSelection(state,{},false,false);
                state.playerCommunication.selectedBeacon=beacon.cell;
                state.selectionDragActive=state.selectionDragMoved=false;
                state.repaintRequested=true;
                return true;
            }
        }
        state.playerCommunication.selectedBeacon.reset();
    }
    if (!local.followTargeting && !local.rallyTargeting) return false;
    if (local.rallyTargeting) {
        if (state.selectedObjectIds.size() == 1)
            dispatchRuntimePlayerFactoryFromUi(state, state.selectedObjectIds.front(),
                RuntimeMultiplayerGameplayCell{targetCell.x, targetCell.y});
        local.rallyTargeting = false;
    } else {
        const auto& projected = runtimeProjectedObjectsForViewport(state, viewport);
        const auto* picked = findProjectedObjectAtPoint(projected, point);
        if (picked && picked->object) dispatchRuntimeTargetOrderFromUi(state,
            RuntimeMultiplayerGameplayTargetOrderKind::Follow,
            {targetCell.x, targetCell.y}, picked->object);
        else state.interactionStatus = "follow requires an allied unit";
        local.followTargeting = false;
    }
    state.selectionDragActive = false;
    state.selectionDragMoved = false;
    state.repaintRequested = true;
    return true;
}
