bool dispatchRuntimePlayerWaypointFromUi(RuntimeWindowState& state,
    RuntimePlayerWaypointOperation operation, std::uint64_t pointId = 0
) {
    RuntimeMultiplayerGameplayWaypointCommand body{operation, pointId};
    bool accepted = false; std::string error;
    if (runtimeUsesGameplayIntentRouter(state)) {
        refreshRuntimeMultiplayerPlayerIntentAuthority(state);
        accepted = state.multiplayerIntentRouter.planWaypoints(body, error);
    } else accepted = applyRuntimePlayerWaypointCommand(state, state.economy.owner, body);
    if (accepted) {
        if (operation == RuntimePlayerWaypointOperation::Begin) state.playerLocalCommands.planning = true;
        if (operation == RuntimePlayerWaypointOperation::Commit) state.playerLocalCommands.planning = false;
        if (operation != RuntimePlayerWaypointOperation::CloseLoop) state.playerLocalCommands.selectedWaypoint = 0;
        state.interactionStatus = operation == RuntimePlayerWaypointOperation::Begin ? "planning: all routed groups wait"
            : operation == RuntimePlayerWaypointOperation::Commit ? "planning complete: all routed groups start"
            : operation == RuntimePlayerWaypointOperation::CloseLoop ? "patrol loop linked" : "waypoint deleted";
    }
    state.repaintRequested = true;
    return accepted;
}

std::optional<POINT> runtimePlayerWaypointScreenPoint(const RuntimeWindowState& state,
    const RECT& viewport, const RuntimePlayerWaypoint& point
) {
    RuntimeCellPoint cell;
    if (const auto* location = std::get_if<RuntimeMultiplayerGameplayCellTarget>(&point.order.target))
        cell = {location->cell.x, location->cell.y};
    else {
        const auto* target = findObjectById(state, std::get<RuntimeMultiplayerGameplayObjectTarget>(point.order.target).objectId);
        if (!target || !runtimeGameplayObjectVisibleToOwner(state, state.economy.owner, *target)) return std::nullopt;
        cell = runtimeCellPointForObject(*target);
    }
    return runtimeScreenPointForCell(state, viewport, cell, terrainLevelAtRuntimeCell(state, cell.x, cell.y));
}

bool handleRuntimePlayerWaypointClick(RuntimeWindowState& state, const RECT& viewport, POINT point);

void drawRuntimePlayerWaypoints(HDC dc, const RECT& viewport, const RuntimeWindowState& state) {
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
    HFONT font = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    SelectObject(dc, font); SetBkMode(dc, TRANSPARENT);
    const auto owner = runtimeCanonicalHumanHouseOwner(state.economy.owner);
    std::set<std::uint64_t> drawnPoints;
    std::set<std::pair<std::uint64_t, std::uint64_t>> drawnEdges;
    for (const auto& [id, route] : state.playerCommands.waypoints.routes) {
        if (route.owner != owner || (!state.playerLocalCommands.planning && !selectionContains(state, id))) continue;
        const auto* actor = findObjectById(state, id);
        if (!actor || !actor->alive || runtimeCanonicalHumanHouseOwner(actor->owner) != owner) continue;
        const COLORREF color = route.loopStart ? RGB(98, 235, 145) : RGB(255, 216, 72);
        HPEN pen = CreatePen(PS_SOLID, 1, color); auto oldPen = SelectObject(dc, pen);
        const auto connect = [&](std::size_t from, std::size_t to) {
            if (!drawnEdges.emplace(route.points[from].id, route.points[to].id).second) return;
            const auto a = runtimePlayerWaypointScreenPoint(state, viewport, route.points[from]);
            const auto b = runtimePlayerWaypointScreenPoint(state, viewport, route.points[to]);
            if (a && b) { MoveToEx(dc, a->x, a->y, nullptr); LineTo(dc, b->x, b->y); }
        };
        for (std::size_t i = 1; i < route.points.size(); ++i) connect(i - 1, i);
        if (route.loopStart) connect(route.points.size() - 1, route.indexOf(route.loopStart));
        if (!route.points.empty()) {
            const auto destination = runtimePlayerWaypointScreenPoint(state, viewport, route.points[route.cursor]);
            const auto origin = runtimeScreenPointForCell(state, viewport, runtimeCellPointForObject(*actor),
                terrainLevelAtRuntimeCell(state, actor->x, actor->y));
            if (destination) { MoveToEx(dc, origin.x, origin.y, nullptr); LineTo(dc, destination->x, destination->y); }
        }
        for (std::size_t i = 0; i < route.points.size(); ++i) {
            if (!drawnPoints.insert(route.points[i].id).second) continue;
            const auto p = runtimePlayerWaypointScreenPoint(state, viewport, route.points[i]);
            if (!p || !pointInRectInclusive(viewport, *p)) continue;
            const bool selected = route.points[i].id == state.playerLocalCommands.selectedWaypoint;
            HBRUSH brush = CreateSolidBrush(selected ? RGB(245, 245, 245) : RGB(18, 31, 29));
            auto oldBrush = SelectObject(dc, brush);
            Ellipse(dc, p->x - 11, p->y - 11, p->x + 12, p->y + 12);
            SelectObject(dc, oldBrush); DeleteObject(brush);
            SetTextColor(dc, selected ? RGB(20, 30, 35) : color);
            RECT bounds{p->x - 11, p->y - 11, p->x + 12, p->y + 12};
            const auto label = std::to_wstring(i + 1);
            DrawTextW(dc, label.c_str(), -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        SelectObject(dc, oldPen); DeleteObject(pen);
    }
    if (state.playerLocalCommands.planning && !state.playerCommunication.chatOpen && !state.playerCommunication.diplomacyOpen) {
        SetTextColor(dc, RGB(255, 230, 130));
        const std::wstring hint = L"路径规划：点击加点 · 点已有路径点闭环 · Delete 删选中点 · 右键换部队 · Z 全部出发";
        RECT bounds{viewport.left + 12, viewport.bottom - 32, viewport.right - 12, viewport.bottom - 8};
        DrawTextW(dc, hint.c_str(), -1, &bounds, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    RestoreDC(dc, saved); DeleteObject(font);
}
