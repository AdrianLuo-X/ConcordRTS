// Shared eligibility for the selected factory's mouse actions and rally marker.
const WorldObjectInstance* runtimeSelectedUnitProductionFactory(const RuntimeWindowState& state) {
    if (state.selectedObjectIds.size() != 1 || state.multiplayerLocalSpectator
        || (!state.campaignMode && runtimeLocalObserver(state.config)))
        return nullptr;
    const auto* object = findObjectById(state, state.selectedObjectIds.front());
    if (!object || !object->alive || object->kind != WorldObjectKind::Structure
        || object->buildAnimTicksRemaining > 0 || runtimeGateObjectIsLimbo(*object)
        || runtimeCanonicalHumanHouseOwner(object->owner) != runtimeCanonicalHumanHouseOwner(state.economy.owner))
        return nullptr;
    const auto* building = findRuntimeBuildingTypeById(state.rules, object->type);
    if (!building) return nullptr;
    const auto category = toUpperAsciiLocal(building->factory);
    return category == "INFANTRYTYPE" || category == "UNITTYPE" || category == "AIRCRAFTTYPE" ? object : nullptr;
}

bool runtimeSelectedFactoryCanSetRallyPoint(const RuntimeWindowState& state) {
    return runtimeSelectedUnitProductionFactory(state)
        && !state.runtimeMenuOpen && !state.skirmishMatchFinished
        && !state.forceAttackHeld && state.activeSidebarTool == RuntimeSidebarTool::None
        && !runtimeSelectedPlacement(state).has_value()
        && !state.playerLocalCommands.followTargeting && !state.playerLocalCommands.planning
        && !state.playerCommunication.beaconTargeting;
}

void drawRuntimeFactoryRallyPoint(HDC dc, const RECT& viewport, const RuntimeWindowState& state) {
    const auto* factory = runtimeSelectedUnitProductionFactory(state);
    if (!factory) return;
    const auto rally = state.playerCommands.rallyPoints.find(factory->instanceId);
    if (rally == state.playerCommands.rallyPoints.end()
        || rally->second.owner != runtimeCanonicalHumanHouseOwner(factory->owner)) return;
    const auto cell = rally->second.cell;
    const POINT destination = runtimeScreenPointForCell(state, viewport, {cell.x, cell.y},
        terrainLevelAtRuntimeCell(state, cell.x, cell.y));
    const auto* building = findRuntimeBuildingTypeById(state.rules, factory->type);
    const auto footprint = runtimeStructureFootprintForObject(state, *factory);
    const int width = std::max(1, footprint.width), height = std::max(1, footprint.height);
    // Match the stable production doorway, rather than the building's anchor
    // or an occupancy-dependent alternate spawn position.
    const bool vehicleDoor = building && building->weaponsFactory && !building->naval
        && !building->waterBound && toUpperAsciiLocal(building->factory) == "UNITTYPE";
    const RuntimeCellPoint exitCell = vehicleDoor
        ? RuntimeCellPoint{factory->x + width, factory->y + height / 2}
        : RuntimeCellPoint{factory->x + width / 2, factory->y + height};
    POINT origin = runtimeScreenPointForCell(state, viewport, exitCell,
        terrainLevelAtRuntimeCell(state, exitCell.x, exitCell.y));
    if (building && toUpperAsciiLocal(building->factory) == "AIRCRAFTTYPE" && building->numberOfDocks > 0) {
        const auto pad = runtimeAircraftDockSpawn(state, *factory, 0);
        origin = runtimeScreenPoint(runtimeProjectCellXY(pad.renderX, pad.renderY, pad.level, state.camera),
            viewport, state.camera);
    }
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
    HPEN line = CreatePen(PS_SOLID, 1, RGB(104, 230, 104));
    SelectObject(dc, line); SetBkMode(dc, TRANSPARENT);
    // The selected factory's rally indicator is independent of the optional
    // unit action lines. Its endpoint is the destination itself; no flag.
    MoveToEx(dc, origin.x, origin.y, nullptr); LineTo(dc, destination.x, destination.y);
    RestoreDC(dc, saved);
    DeleteObject(line);
}
