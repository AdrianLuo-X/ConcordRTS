// Ground and naval repair use the authored Foundation / DockingOffset coordinate
// space as aircraft pads. Integer cells remain the navigation authority; the
// half-cell position must survive attachment and ordinary save restoration.
const RuntimeRepairDepotRelation* findRuntimeRepairDepotRelationForUnit(
    const RuntimeRepairDepotSystemState&, int);
int runtimeAdvanceBodyFacing(const RuntimeWindowState&, WorldObjectInstance&, int, int);
void beginRuntimeRenderMove(WorldObjectInstance&, RuntimeObjectBehavior&,
    const RuntimeCellPoint&, const RuntimeCellPoint&, double, bool, int,
    RuntimeWindowState*);

std::optional<std::array<double, 2>> runtimeRepairDepotApproachPoint(
    const RuntimeWindowState& state, const WorldObjectInstance& unit, const RuntimeCellPoint& target) {
    if (!state.repairDepotsInitialized || unit.kind != WorldObjectKind::Unit || unit.airborne) return std::nullopt;
    const auto* relation = findRuntimeRepairDepotRelationForUnit(state.repairDepots, unit.instanceId);
    if (!relation || relation->phase != RuntimeRepairDepotRelationPhase::Reserved
        || target.x != relation->dockCell.x || target.y != relation->dockCell.y) return std::nullopt;
    const auto* host = findObjectById(state, relation->hostId);
    const auto* building = host ? state.rules.findBuildingType(host->type) : nullptr;
    if (!host || !host->alive || !building || !building->unitRepair
        || relation->dockIndex < 0 || relation->dockIndex >= building->numberOfDocks)
        return std::nullopt;
    const auto domain = building->naval || building->waterBound
        ? RuntimeRepairDepotDomain::Naval : RuntimeRepairDepotDomain::Ground;
    if (relation->domain != domain) return std::nullopt;
    const auto dock = runtimeAircraftDockSpawn(state, *host, relation->dockIndex);
    if (dock.cell != target) return std::nullopt;
    return std::array<double, 2>{dock.renderX, dock.renderY};
}

double runtimeRepairDepotApproachDistanceScale(const RuntimeWindowState& state,
    const WorldObjectInstance& unit, const RuntimeCellPoint& from, const RuntimeCellPoint& to) {
    if (unit.kind != WorldObjectKind::Unit || unit.airborne) return 1.0;
    const auto dock = runtimeRepairDepotApproachPoint(state, unit, to);
    // The caller has committed the next integer cell but still retains the
    // visible departure point. Include it when leaving an authored half-cell
    // dock, so a longer diagonal departure does not get a one-cell duration.
    const double fromX = unit.hasRenderPosition ? unit.renderX : double(from.x);
    const double fromY = unit.hasRenderPosition ? unit.renderY : double(from.y);
    const double toX = dock ? (*dock)[0] : double(to.x);
    const double toY = dock ? (*dock)[1] : double(to.y);
    if (!dock && fromX == from.x && fromY == from.y) return 1.0;
    // A reserved ship may already occupy the dock's navigation cell. Its
    // remaining authored offset is a real, shorter movement segment.
    if (dock && from == to) return std::hypot(toX-fromX, toY-fromY);
    return std::max(1.0, std::hypot(toX-fromX, toY-fromY)
        / std::max(1.0, std::hypot(double(to.x-from.x), double(to.y-from.y))));
}

bool beginRuntimeRepairDepotSameCellApproach(RuntimeWindowState& state,
    WorldObjectInstance& unit, RuntimeObjectBehavior& behavior) {
    const auto current = runtimeCellPointForObject(unit);
    if (behavior.kind != RuntimeBehaviorKind::Move || behavior.moveCooldownTicks > 0
        || current != behavior.moveTarget || unit.kind != WorldObjectKind::Unit
        || unit.airborne || runtimeObjectMovementMode(state, unit) != PathMovementMode::Naval)
        return false;
    const auto dock = runtimeRepairDepotApproachPoint(state, unit, current);
    if (!dock) return false;
    const double fromX = unit.hasRenderPosition ? unit.renderX : double(unit.x);
    const double fromY = unit.hasRenderPosition ? unit.renderY : double(unit.y);
    const double dx = (*dock)[0] - fromX, dy = (*dock)[1] - fromY;
    const double directionScale = std::max(std::abs(dx), std::abs(dy));
    if (directionScale < 1.0e-9) return false;

    // Face the actual offset using the ordinary ROT clock. Integer from/to
    // cells are equal here, so the normal cell-facing helper has no direction.
    const int desiredFacing = runtimeFacingFromDelta(
        static_cast<int>(std::llround(dx / directionScale * 1024.0)),
        static_cast<int>(std::llround(dy / directionScale * 1024.0)));
    unit.facing = unit.facing < 0 ? desiredFacing
        : runtimeAdvanceBodyFacing(state, unit, unit.facing, desiredFacing);
    if (unit.facing != desiredFacing) {
        behavior.state = RuntimeBehaviorState::Cooldown;
        return true;
    }
    behavior.moveCooldownTicks = consumeRuntimeMovementSegmentIntervalTicks(
        state, unit, behavior, current, current);
    beginRuntimeRenderMove(unit, behavior, current, current,
        unit.hasRenderPosition ? unit.renderLevel : static_cast<double>(unit.level),
        unit.hasRenderPosition ? unit.renderOnBridge : unit.onBridge,
        behavior.moveCooldownTicks, &state);
    behavior.state = RuntimeBehaviorState::Moving;
    setRuntimeMissionIntent(state, unit, RuntimeMissionIntent::Moving,
        "repairDepotSameCellApproach");
    ++state.actionSummary.movedThisTick;
    return true;
}

bool runtimeObjectUsesNavalDockPresentation(const RuntimeWindowState& state,
    const WorldObjectInstance& unit) {
    const auto* type = findRuntimeUnitType(state.rules, unit);
    return unit.kind == WorldObjectKind::Unit && !unit.airborne && !unit.onBridge
        && type && type->naval && toLowerAsciiLocal(type->locomotor)
            == "{2bea74e1-7cca-11d3-be14-00104b62a16c}";
}

std::optional<std::array<double, 3>> runtimeNavalDockCommandPose(
    const RuntimeWindowState& state, const WorldObjectInstance& unit) {
    if (!runtimeObjectUsesNavalDockPresentation(state, unit) || !unit.hasRenderPosition)
        return std::nullopt;
    if (unit.renderMoveActive) {
        const auto found = state.behaviors.find(unit.instanceId);
        if (found == state.behaviors.end() || found->second.kind != RuntimeBehaviorKind::Move
            || found->second.moveCooldownTicks <= 0) return std::nullopt;
        const auto& move = found->second;
        if (move.renderFromOffsetX == 0.0 && move.renderFromOffsetY == 0.0
            && move.renderToOffsetX == 0.0 && move.renderToOffsetY == 0.0) return std::nullopt;
    } else if (unit.renderX == unit.x && unit.renderY == unit.y) return std::nullopt;
    return std::array<double, 3>{unit.renderX, unit.renderY, unit.renderLevel};
}

std::optional<RuntimeObjectBehavior> runtimeNavalDockCommandContinuation(
    const RuntimeWindowState& state, const WorldObjectInstance& unit) {
    if (!unit.renderMoveActive || !runtimeNavalDockCommandPose(state, unit)) return std::nullopt;
    return state.behaviors.at(unit.instanceId);
}

void restoreRuntimeNavalDockCommandStopPose(WorldObjectInstance& unit,
    const std::optional<std::array<double, 3>>& pose) {
    if (!pose) return;
    unit.hasRenderPosition = true;
    unit.renderMoveActive = false;
    unit.renderX = (*pose)[0]; unit.renderY = (*pose)[1]; unit.renderLevel = (*pose)[2];
    unit.renderOnBridge = false;
}

bool applyRuntimeRepairDepotDockPose(
    const RuntimeWindowState& state,
    WorldObjectInstance& unit,
    const WorldObjectInstance& host,
    int dockIndex
) {
    const auto* building = state.rules.findBuildingType(host.type);
    if (!unit.alive || unit.kind != WorldObjectKind::Unit || unit.airborne || !host.alive
        || !building || !building->unitRepair
        || unit.dockedToObjectId != host.instanceId || dockIndex < 0
        || dockIndex >= building->numberOfDocks) return false;
    const auto dock = runtimeAircraftDockSpawn(state, host, dockIndex);
    if (unit.x != dock.cell.x || unit.y != dock.cell.y) return false;
    const bool changed = !unit.hasRenderPosition || unit.renderMoveActive
        || unit.renderX != dock.renderX || unit.renderY != dock.renderY
        || unit.renderLevel != static_cast<double>(unit.level)
        || unit.renderOnBridge != unit.onBridge;
    unit.hasRenderPosition = true;
    unit.renderX = dock.renderX;
    unit.renderY = dock.renderY;
    unit.renderMoveActive = false;
    unit.renderLevel = static_cast<double>(unit.level);
    unit.renderOnBridge = unit.onBridge;
    return changed;
}

bool refreshRuntimeRepairDepotDockPoses(RuntimeWindowState& state,
    const RuntimeWindowState* authoredState = nullptr) {
    bool changed = false;
    for (const auto& relation : state.repairDepots.relations()) {
        if (relation.phase != RuntimeRepairDepotRelationPhase::Docked) continue;
        auto* unit = findMutableObjectById(state, relation.unitId);
        const auto* host = findObjectById(state, relation.hostId);
        const auto& authored = authoredState ? *authoredState : state;
        const auto* building = host ? authored.rules.findBuildingType(host->type) : nullptr;
        if (!building || relation.domain != (building->naval || building->waterBound
                ? RuntimeRepairDepotDomain::Naval : RuntimeRepairDepotDomain::Ground)) continue;
        if (unit && host)
            // Ownership transactions intentionally omit the immutable ART
            // database from their scratch state. Read the live authored dock
            // offsets while applying the pose to the staged world objects.
            changed = applyRuntimeRepairDepotDockPose(authored,
                *unit, *host, relation.dockIndex) || changed;
    }
    if (changed) state.repaintRequested = true;
    return changed;
}
