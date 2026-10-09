bool runtimeObjectUsesVehicleArrival(const RuntimeWindowState& state,
    const WorldObjectInstance& object) {
    if (object.kind != WorldObjectKind::Unit || object.airborne) return false;
    const auto* type = findRuntimeUnitType(state.rules, object);
    if (type == nullptr || type->organic) return false;
    const auto mode = runtimeObjectMovementMode(state, object);
    return mode == PathMovementMode::Ground || mode == PathMovementMode::Amphibious;
}

double prepareRuntimeVehicleArrival(const RuntimeWindowState& state,
    const WorldObjectInstance& object, RuntimeObjectBehavior& behavior,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to) {
    behavior.renderArrivalDistanceAfter = -1.0;
    if (runtimeFactoryEgressBypassesArrival(state, object, behavior)) return 1.0;
    if (behavior.kind != RuntimeBehaviorKind::Move
        || !runtimeObjectUsesVehicleArrival(state, object)
        || from == to || std::abs(to.x - from.x) > 1 || std::abs(to.y - from.y) > 1)
        return 1.0;
    // The caller has consumed the current step. Only a complete remaining route
    // identifies an arrival: an empty local-avoidance/repath segment does not.
    if (behavior.path.empty() ? to != behavior.moveTarget
        : behavior.path.back() != behavior.moveTarget) return 1.0;
    if (behavior.path.size() >= 2) return 1.0;
    double distanceAfter = 0.0;
    auto previous = to;
    for (const auto& next : behavior.path) {
        if (next == previous || std::abs(next.x - previous.x) > 1
            || std::abs(next.y - previous.y) > 1) return 1.0;
        distanceAfter += std::hypot(double(next.x - previous.x), double(next.y - previous.y));
        previous = next;
    }
    behavior.renderArrivalDistanceAfter = distanceAfter;
    return RuntimeVehicleArrivalMotion::durationScale(
        std::hypot(double(to.x - from.x), double(to.y - from.y)), distanceAfter);
}

double runtimeVehicleMoveRenderProgressAtRemaining(const RuntimeObjectBehavior& behavior, int remainingTicks) {
    const double elapsed = 1.0 - double(remainingTicks)
        / std::max(1, behavior.renderMoveTicksTotal);
    if (behavior.renderStartupAccelerationDistance > 0.0)
        return runtimeVehicleStartupRenderProgress(behavior, elapsed);
    if (behavior.renderArrivalDistanceAfter < 0.0) return std::clamp(elapsed, 0.0, 1.0);
    return RuntimeVehicleArrivalMotion::progress(elapsed,
        std::hypot(double(behavior.renderTo.x - behavior.renderFrom.x),
            double(behavior.renderTo.y - behavior.renderFrom.y)),
        behavior.renderArrivalDistanceAfter);
}

double runtimeVehicleMoveRenderProgress(const RuntimeObjectBehavior& behavior) {
    return runtimeVehicleMoveRenderProgressAtRemaining(behavior, behavior.renderMoveTicksRemaining);
}

// A stopped infantry body may still occupy the preceding part of its
// committed step. Complete that step before turning; a direct chord can cut
// the neighbouring building corner once the infantry subcell is added. The
// route is derived from the existing saved endpoints, so restore and order
// inheritance use exactly the same curve.
bool runtimeInfantryOffsetStartsAtSourceSubcell(int subCell, double fromOffsetX, double fromOffsetY) {
    const auto destinationSlot = worldInfantrySubCellOffset(subCell);
    const double startX = destinationSlot.x + fromOffsetX;
    const double startY = destinationSlot.y + fromOffsetY;
    for (int sourceSubcell : {2, 4, 3}) {
        const auto sourceSlot = worldInfantrySubCellOffset(sourceSubcell);
        if (std::abs(startX - sourceSlot.x) <= 1.0e-9
            && std::abs(startY - sourceSlot.y) <= 1.0e-9) return true;
    }
    return false;
}

bool runtimeInfantryMoveUsesCommittedCorner(const WorldObjectInstance& object,
    const RuntimeObjectBehavior& move) {
    if (object.kind != WorldObjectKind::Infantry || object.airborne
        || move.renderFrom == move.renderTo
        || move.renderToOffsetX != 0.0 || move.renderToOffsetY != 0.0) return false;
    // Only an explicit accepted transport transfer bypasses the stopped-body pivot.
    // Its provenance is independent of the original host's current state.
    if (move.transportTransferHostId >= 0 && move.transportTransferKind != RuntimeTransportTransferKind::None) return false;
    // A slot change can shift the base while the body remains at a legal
    // source-cell slot. Start directly there instead of visiting a new slot
    // in the same cell, which could be occupied by another resident.
    if (runtimeInfantryOffsetStartsAtSourceSubcell(object.subCell,
        move.renderFromOffsetX, move.renderFromOffsetY)) return false;
    const double dx = move.renderTo.x - move.renderFrom.x;
    const double dy = move.renderTo.y - move.renderFrom.y;
    return std::abs(move.renderFromOffsetX * dy - move.renderFromOffsetY * dx) > 1.0e-9;
}

std::array<double, 2> runtimeGroundRenderMovePoint(const WorldObjectInstance& object,
    const RuntimeObjectBehavior& move, double progress) {
    progress = std::clamp(progress, 0.0, 1.0);
    const double fromX = move.renderFrom.x + move.renderFromOffsetX;
    const double fromY = move.renderFrom.y + move.renderFromOffsetY;
    const double toX = move.renderTo.x + move.renderToOffsetX;
    const double toY = move.renderTo.y + move.renderToOffsetY;
    if (!runtimeInfantryMoveUsesCommittedCorner(object, move))
        return {fromX + (toX - fromX) * progress, fromY + (toY - fromY) * progress};
    const double first = std::hypot(move.renderFromOffsetX, move.renderFromOffsetY);
    const double second = std::hypot(toX - move.renderFrom.x, toY - move.renderFrom.y);
    const int ticks = std::max(2, move.renderMoveTicksTotal);
    const int firstTicks = std::clamp(static_cast<int>(std::ceil(ticks * first / (first + second))),
        1, ticks - 1);
    const double elapsed = ticks * progress;
    if (elapsed < firstTicks) {
        const double part = elapsed / firstTicks;
        return {fromX + (move.renderFrom.x - fromX) * part,
            fromY + (move.renderFrom.y - fromY) * part};
    }
    const double part = (elapsed - firstTicks) / (ticks - firstTicks);
    return {move.renderFrom.x + (toX - move.renderFrom.x) * part,
        move.renderFrom.y + (toY - move.renderFrom.y) * part};
}

void restoreRuntimeVehicleMovePresentation(WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior) {
    if (behavior.renderMoveTicksTotal <= 0 || behavior.renderMoveTicksRemaining <= 0) return;
    const double progress = runtimeVehicleMoveRenderProgress(behavior);
    object.hasRenderPosition = object.renderMoveActive = true;
    const auto point = runtimeGroundRenderMovePoint(object, behavior, progress);
    object.renderX = point[0]; object.renderY = point[1];
    object.renderLevel = behavior.renderFromLevel
        + (behavior.renderToLevel - behavior.renderFromLevel) * progress;
    object.renderOnBridge = progress < 0.5 ? behavior.renderFromOnBridge : behavior.renderToOnBridge;
}

// A new order takes effect after the already committed cell segment. Keeping
// its authoritative cooldown avoids advancing a ground unit or ship on a
// click, including vehicles in a braking pose. The replacement owns its goal.
void inheritRuntimeVehicleMoveSegment(WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior, const RuntimeObjectBehavior& previous) {
    behavior.moveCooldownTicks = previous.moveCooldownTicks;
    behavior.moveSegmentFrom = previous.moveSegmentFrom;
    behavior.moveIntervalRemainderQ16 = previous.moveIntervalRemainderQ16;
    behavior.moveStartupDistanceQ16 = previous.moveStartupDistanceQ16;
    behavior.renderStartupAccelerationDistance = previous.renderStartupAccelerationDistance;
    behavior.renderFrom = previous.renderFrom;
    behavior.renderTo = previous.renderTo;
    behavior.renderFromOffsetX = previous.renderFromOffsetX;
    behavior.renderFromOffsetY = previous.renderFromOffsetY;
    behavior.renderToOffsetX = previous.renderToOffsetX;
    behavior.renderToOffsetY = previous.renderToOffsetY;
    behavior.renderFromLevel = previous.renderFromLevel;
    behavior.renderToLevel = previous.renderToLevel;
    behavior.renderFromOnBridge = previous.renderFromOnBridge;
    behavior.renderToOnBridge = previous.renderToOnBridge;
    behavior.renderMoveTicksTotal = previous.renderMoveTicksTotal;
    behavior.renderMoveTicksRemaining = previous.renderMoveTicksRemaining;
    behavior.renderArrivalDistanceAfter = previous.renderArrivalDistanceAfter;
    behavior.transportTransferHostId = previous.transportTransferHostId;
    behavior.transportTransferKind = previous.transportTransferKind;
    restoreRuntimeVehicleMovePresentation(object, behavior);
    if (previous.transportTransferKind == RuntimeTransportTransferKind::Boarding) {
        // A replacement order cancels entry permission while keeping the exact shown body.
        // The original adjacent source still owns its navigation cell and infantry slot.
        object.renderMoveActive = false;
        behavior.moveCooldownTicks = behavior.renderMoveTicksTotal = behavior.renderMoveTicksRemaining = 0;
        behavior.moveSegmentFrom = {-1, -1};
        behavior.transportTransferHostId = -1;
        behavior.transportTransferKind = RuntimeTransportTransferKind::None;
    }
}
