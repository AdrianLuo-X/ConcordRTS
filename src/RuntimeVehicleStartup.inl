// A startup owns a continuous accepted route, rather than restarting on each
// tile. Only its accumulated distance is new authoritative state; the existing
// committed endpoints and clock identify the current phase without a timer.
bool runtimeObjectUsesVehicleStartup(const RuntimeWindowState& state,
    const WorldObjectInstance& object) {
    if (object.kind != WorldObjectKind::Unit || object.airborne) return false;
    const auto* type = findRuntimeUnitType(state.rules, object);
    if (!type || !type->accelerates || type->organic || type->speed <= 0) return false;
    const auto mode = runtimeObjectMovementMode(state, object);
    return mode == PathMovementMode::Ground || mode == PathMovementMode::Amphibious
        || mode == PathMovementMode::Naval;
}

double runtimeVehicleStartupAccelerationFactor(const UnitType& type) {
    const bool hover = toLowerAsciiLocal(type.speedType) == "hover"
        || toLowerAsciiLocal(type.locomotor) == "{4a582742-9839-11d1-b709-00a024ddafd1}";
    return hover ? type.hoverAcceleration : type.accelerationFactor;
}

bool runtimeVehicleStartupCannotMove(const RuntimeWindowState& state,
    const WorldObjectInstance& object) {
    const auto* type = findRuntimeUnitType(state.rules, object);
    return runtimeObjectUsesVehicleStartup(state, object) && type
        && !(runtimeVehicleStartupAccelerationFactor(*type) > 0.0);
}

double runtimeVehicleStartupAccelerationDistance(const RuntimeWindowState& state,
    const WorldObjectInstance& object) {
    const auto* type = findRuntimeUnitType(state.rules, object);
    if (!runtimeObjectUsesVehicleStartup(state, object) || !type) return 0.0;
    const double factor = runtimeVehicleStartupAccelerationFactor(*type);
    const double cruiseCellsPerRuleFrame = std::clamp(state.config.fixedTickHz, 1, 1000)
        / (runtimeMovementBaseIntervalTicks(state, object) * runtimeRuleFramesPerSecond);
    return RuntimeVehicleStartupMotion::accelerationDistance(cruiseCellsPerRuleFrame, factor);
}

double runtimeVehicleStartupDistanceBefore(const RuntimeObjectBehavior& behavior,
    double length) {
    const auto segmentDistance = RuntimeVehicleStartupMotion::distanceQ16(length);
    return double(std::max<std::int64_t>(0, behavior.moveStartupDistanceQ16-segmentDistance))
        / RuntimeVehicleStartupMotion::distanceOneQ16;
}

double prepareRuntimeVehicleStartup(const RuntimeWindowState& state,
    const WorldObjectInstance& object, RuntimeObjectBehavior& behavior,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to, double arrivalScale) {
    behavior.renderStartupAccelerationDistance = 0.0;
    if (!runtimeObjectUsesVehicleStartup(state, object)
        || (behavior.kind != RuntimeBehaviorKind::Move && behavior.kind != RuntimeBehaviorKind::Attack)
        || from == to || std::abs(to.x-from.x)>1 || std::abs(to.y-from.y)>1) return arrivalScale;
    const double ramp = runtimeVehicleStartupAccelerationDistance(state, object)
        * runtimeTerrainMovementSpeedMultiplier(state, object, from, to);
    if (!std::isfinite(ramp)) return std::numeric_limits<double>::infinity();
    const double length = std::hypot(double(to.x-from.x), double(to.y-from.y));
    const auto rampQ16 = RuntimeVehicleStartupMotion::rampDistanceQ16(ramp);
    if (runtimeFactoryEgressBypassesArrival(state, object, behavior)) {
        behavior.moveStartupDistanceQ16 = std::min(RuntimeVehicleStartupMotion::maximumDistanceQ16,
            rampQ16 + RuntimeVehicleStartupMotion::distanceQ16(length));
        return arrivalScale;
    }
    if (behavior.state == RuntimeBehaviorState::Blocked && behavior.moveCooldownTicks <= 0)
        behavior.moveStartupDistanceQ16 = 0;
    const auto beforeQ16 = std::clamp(behavior.moveStartupDistanceQ16, std::int64_t(0), rampQ16);
    const auto lengthQ16 = RuntimeVehicleStartupMotion::distanceQ16(length);
    behavior.moveStartupDistanceQ16 = std::min(RuntimeVehicleStartupMotion::maximumDistanceQ16,
        beforeQ16 + lengthQ16);
    const double before = double(beforeQ16) / RuntimeVehicleStartupMotion::distanceOneQ16;
    if (before >= ramp || ramp <= 0.0) return arrivalScale;
    behavior.renderStartupAccelerationDistance = ramp;
    return RuntimeVehicleStartupMotion::durationScale(length, before, ramp,
        behavior.renderArrivalDistanceAfter);
}

double runtimeVehicleStartupRenderProgress(const RuntimeObjectBehavior& behavior, double elapsed) {
    const double length = std::hypot(double(behavior.renderTo.x-behavior.renderFrom.x),
        double(behavior.renderTo.y-behavior.renderFrom.y));
    return RuntimeVehicleStartupMotion::progress(elapsed, length,
        runtimeVehicleStartupDistanceBefore(behavior, length),
        behavior.renderStartupAccelerationDistance, behavior.renderArrivalDistanceAfter);
}
