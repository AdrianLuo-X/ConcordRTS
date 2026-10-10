// Repair pads occupy authored fractional positions. Body rotation and the
// rendered segment must use the same endpoints, including when leaving a pad.
int runtimeFacingFromRenderedDelta(double dx, double dy) {
    if (dx == 0.0 && dy == 0.0) return -1;
    constexpr double pi = 3.14159265358979323846;
    double degrees = std::atan2(dx - dy, -(dx + dy)) * 180.0 / pi;
    degrees = std::fmod(degrees + 360.0, 360.0);
    if (degrees < 0.0) degrees += 360.0;
    return static_cast<int>(std::lround(degrees * 256.0 / 360.0)) % 256;
}

int runtimeMoveSegmentFacing(const RuntimeObjectBehavior& move) {
    if (move.renderFromOffsetX == 0.0 && move.renderFromOffsetY == 0.0
        && move.renderToOffsetX == 0.0 && move.renderToOffsetY == 0.0)
        return runtimeFacingFromDelta(move.renderTo.x - move.renderFrom.x,
            move.renderTo.y - move.renderFrom.y);
    return runtimeFacingFromRenderedDelta(
        move.renderTo.x + move.renderToOffsetX - move.renderFrom.x - move.renderFromOffsetX,
        move.renderTo.y + move.renderToOffsetY - move.renderFrom.y - move.renderFromOffsetY);
}

void alignRuntimeFacingToRenderMove(WorldObjectInstance& object,
    const RuntimeObjectBehavior& move) {
    int facing = object.kind == WorldObjectKind::Unit && !object.airborne
        ? runtimeMoveSegmentFacing(move)
        : runtimeFacingFromDelta(move.renderTo.x - move.renderFrom.x,
            move.renderTo.y - move.renderFrom.y);
    if (runtimeInfantryMoveUsesCommittedCorner(object, move)) {
        const double first = std::hypot(move.renderFromOffsetX, move.renderFromOffsetY);
        const double second = std::hypot(double(move.renderTo.x - move.renderFrom.x),
            double(move.renderTo.y - move.renderFrom.y));
        const int ticks = std::max(2, move.renderMoveTicksTotal);
        const int firstTicks = std::clamp(static_cast<int>(std::ceil(ticks * first / (first + second))),
            1, ticks - 1);
        const bool approachingCorner = runtimeVehicleMoveRenderProgress(move) * ticks < firstTicks;
        facing = approachingCorner
            ? runtimeFacingFromRenderedDelta(-move.renderFromOffsetX, -move.renderFromOffsetY)
            : runtimeFacingFromDelta(move.renderTo.x - move.renderFrom.x, move.renderTo.y - move.renderFrom.y);
    }
    if (facing >= 0) object.facing = facing;
}

int runtimeMoveTargetFacing(const RuntimeWindowState& state, const WorldObjectInstance& object,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to) {
    if (object.kind != WorldObjectKind::Unit || object.airborne)
        return runtimeFacingFromDelta(to.x - from.x, to.y - from.y);
    const auto dock = runtimeRepairDepotApproachPoint(state, object, to);
    const double fromX = object.hasRenderPosition ? object.renderX : double(from.x);
    const double fromY = object.hasRenderPosition ? object.renderY : double(from.y);
    const double toX = dock ? (*dock)[0] : double(to.x);
    const double toY = dock ? (*dock)[1] : double(to.y);
    if (fromX == from.x && fromY == from.y && toX == to.x && toY == to.y)
        return runtimeFacingFromDelta(to.x - from.x, to.y - from.y);
    return runtimeFacingFromRenderedDelta(toX - fromX, toY - fromY);
}
