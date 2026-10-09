const UnitType* runtimeJumpjetProfile(const RuntimeWindowState& state, const WorldObjectInstance& object) {
    const auto* unit = findRuntimeUnitType(state.rules, object);
    return unit && runtimeUnitUsesJumpjetMovement(*unit) && !unit->spawned && !unit->missileSpawn
        && !runtimeHasSupportFlightPose(object)
        ? unit : nullptr;
}

RuntimeJumpjetFlight& runtimeJumpjetFlightFor(RuntimeWindowState& state, const WorldObjectInstance& object) {
    auto [it, inserted] = state.jumpjetFlights.try_emplace(object.instanceId);
    if (inserted) {
        it->second.height = std::llround((object.hasRenderPosition ? object.renderLevel : object.flightLevel)
            * runtimeJumpjetCellQ16);
        it->second.fromX = it->second.toX = object.x;
        it->second.fromY = it->second.toY = object.y;
    }
    return it->second;
}

#include "RuntimeKirovClearanceMotion.inl"

void applyRuntimeJumpjetPose(const RuntimeWindowState& state, WorldObjectInstance& object) {
    const auto found = state.jumpjetFlights.find(object.instanceId);
    object.aircraftBuildingClearanceLevel = -1.0;
    if (runtimeJumpjetUsesKirovClearance(state, object)
        && found != state.jumpjetFlights.end() && found->second.clearanceTick >= 0)
        object.aircraftBuildingClearanceLevel = double(found->second.clearanceLift) / runtimeJumpjetCellQ16;
    if (object.alive) {
        const auto* visualUnit = runtimeJumpjetProfile(state, object);
        if (visualUnit && runtimeUnitUsesAutomaticJumpjetLanding(*visualUnit)
            && object.factoryExitHostId < 0) {
            const auto ground = std::int64_t(terrainLevelAtRuntimeCell(state, object.x, object.y))
                * runtimeJumpjetCellQ16;
            const auto height = found != state.jumpjetFlights.end() ? found->second.height
                : std::llround((object.hasRenderPosition ? object.renderLevel : object.flightLevel)
                    * runtimeJumpjetCellQ16);
            object.aircraftDockFlightProgress = !object.airborne ? 0.0
                : std::clamp(double(height - ground)
                    / std::max<std::int64_t>(1, visualUnit->jumpJetHeight * runtimeJumpjetQ16), 0.0, 1.0);
        }
    }
    // The derived lift above is presentation only. Preserve the original
    // authority-pose no-op conditions for absent, dead and grounded flights.
    if (!runtimeHistoryQueryReference()
        && (found == state.jumpjetFlights.end() || !object.alive || !object.airborne)) return;
    const auto* unit = runtimeJumpjetProfile(state, object);
    if (found == state.jumpjetFlights.end() || !unit || !object.alive || !object.airborne) return;
    const auto& f = found->second;
    const bool segmentPose = (f.active || f.stopped) && f.length > 0;
    const double p = segmentPose ? double(f.progress) / f.length : 1.0;
    const double fromX = f.fromX, fromY = f.fromY;
    object.renderX = segmentPose ? fromX + (f.toX-fromX)*p : object.x;
    object.renderY = segmentPose ? fromY + (f.toY-fromY)*p : object.y;
    const auto cruise = terrainLevelAtRuntimeCell(state, object.x, object.y)*runtimeJumpjetCellQ16
        + unit->jumpJetHeight*runtimeJumpjetQ16;
    const double wobble = unit->jumpjetNoWobbles || unit->jumpjetWobbles <= 0.0 || f.height != cruise
        ? 0.0 : std::sin(f.wobblePhase * (6.2831853071795864769/65536.0)) * unit->jumpjetDeviation / 256.0;
    object.renderLevel = std::max(double(terrainLevelAtRuntimeCell(state, object.x, object.y)),
        double(f.height)/runtimeJumpjetCellQ16 + wobble);
    object.flightLevel = static_cast<int>((f.height + runtimeJumpjetCellQ16-1)/runtimeJumpjetCellQ16);
    object.level = object.flightLevel;
    object.onBridge = object.renderOnBridge = false;
    object.subCell = -1;
    object.hasRenderPosition = true;
    object.renderMoveActive = f.active;
}

bool advanceRuntimeJumpjetHeight(RuntimeWindowState& state, WorldObjectInstance& object, bool landing) {
    const auto* unit = runtimeJumpjetProfile(state, object);
    if (!unit) return false;
    if (!landing && runtimeFactoryAirExitWaitingForRoof(state, object)) return false;
    if (!landing && object.kind == WorldObjectKind::Unit
        && !state.pendingTransportTargets.empty()
        && runtimeCachedCapabilityProfileForObject(state, object).transport.canTransport)
        cancelRuntimeTransportBoardingForMovingHost(state, object.instanceId);
    auto& f = runtimeJumpjetFlightFor(state, object);
    int ground = terrainLevelAtRuntimeCell(state, object.x, object.y);
    if (!landing) {
        if (f.active || f.stopped) ground = std::max(ground, terrainLevelAtRuntimeCell(state, f.fromX, f.fromY));
        const auto behavior = state.behaviors.find(object.instanceId);
        if (behavior != state.behaviors.end() && !behavior->second.path.empty()) {
            const auto next = behavior->second.path.front();
            ground = std::max(ground, terrainLevelAtRuntimeCell(state, next.x, next.y));
        }
    }
    const std::int64_t target = ground * runtimeJumpjetCellQ16
        + (landing ? 0 : unit->jumpJetHeight * runtimeJumpjetQ16);
    if (f.verticalTick != state.tick) {
        f.verticalTick = state.tick;
        // Retail ordinary landing uses Climb; Crash is the wreck descent rate.
        // Keep the explicit deploy transition's existing clock separate.
        const double rate = std::clamp(landing && runtimeUnitUsesAutomaticJumpjetLanding(*unit)
            ? unit->jumpjetClimb : f.height > target ? unit->jumpjetCrash : unit->jumpjetClimb, 0.0, 10000.0);
        const auto delta = std::max<std::int64_t>(0, std::llround(rate * runtimeJumpjetQ16 * 15.0
            / std::max(1, state.config.fixedTickHz)));
        f.height += std::clamp(target-f.height, -delta, delta);
    }
    object.airborne = !landing || f.height > target;
    object.flightLevel = static_cast<int>((f.height + runtimeJumpjetCellQ16-1)/runtimeJumpjetCellQ16);
    object.level = object.flightLevel;
    object.renderLevel = double(f.height)/runtimeJumpjetCellQ16;
    applyRuntimeJumpjetPose(state, object);
    if (!object.airborne) {
        object.onBridge = object.renderOnBridge = runtimeCellIsBridge(state, object.x, object.y);
        object.renderMoveActive = false;
    }
    syncRuntimeTransportCargoPositions(state, object);
    return f.height == target;
}

bool runtimeJumpjetShouldLand(const RuntimeWindowState& state, const WorldObjectInstance& object,
    const UnitType& unit, const RuntimeJumpjetFlight& flight) {
    if (!runtimeUnitUsesAutomaticJumpjetLanding(unit) || flight.active || flight.stopped
        || object.renderMoveActive || object.factoryExitHostId >= 0) return false;
    const auto mission = toLowerAsciiLocal(object.mission);
    bool transportUnloading = false;
    if (mission == toLowerAsciiLocal(runtimeMissionName(RuntimeMissionIntent::Unloading))
        && object.kind == WorldObjectKind::Unit
        && !object.transportPassengerIds.empty()) {
        const auto transport = runtimeTransportProfileForObject(state, object);
        transportUnloading = transport.canTransport && transport.passengerCapacity > 0;
    }
    if (mission != "guard" && mission != "areaguard" && mission != "idle"
        && !transportUnloading) return false;
    const auto behavior = state.behaviors.find(object.instanceId);
    if (behavior != state.behaviors.end() && behavior->second.state != RuntimeBehaviorState::Complete
        && behavior->second.state != RuntimeBehaviorState::Idle) return false;
    const auto queue = state.playerCommands.queues.find(object.instanceId);
    if (queue != state.playerCommands.queues.end() && !queue->second.orders.empty()) return false;
    if (state.playerCommands.following.contains(object.instanceId)
        || state.playerCommands.travelling.contains(object.instanceId)) return false;
    const auto route = state.playerCommands.waypoints.routes.find(object.instanceId);
    if (route != state.playerCommands.waypoints.routes.end()
        && (route->second.active || route->second.cursor < route->second.points.size()
            || route->second.loopCommitted)) return false;
    const auto current = runtimeCellPointForObject(object);
    const auto terrain = state.terrainByCell.find(runtimeCellKey(current.x, current.y));
    if (terrain == state.terrainByCell.end() || !terrain->second) return false;
    const auto info = runtimeCachedTerrainInfoForCell(state, *terrain->second);
    // Retail ordinary jumpjet landing rejects Water and Beach; a valid bridge
    // deck is ground even when its underlying cell is water.
    if (!info.bridge && (info.water || info.shore)) return false;
    const auto ground = findRuntimePassableCellNear(state, object, current, 0, -1,
        nullptr, nullptr, nullptr, PathMovementMode::Ground);
    return ground && *ground == current;
}

void advanceRuntimeJumpjetFlight(RuntimeWindowState& state, WorldObjectInstance& object) {
    const auto* unit = runtimeJumpjetProfile(state, object);
    if (!unit || !object.alive || !object.airborne) return;
    auto& f = runtimeJumpjetFlightFor(state, object);
    if ((f.active || f.stopped) && (object.x != f.toX || object.y != f.toY)) {
        // A teleport/transport relocation cancels a previous segment, even
        // when arrival recovery freezes the object at its new location.
        f.active = f.stopped = false; f.speed = 0; f.segmentCarry = 0;
        f.length = f.progress = 0;
    }
    if (state.specialMechanismsInitialized && (state.specialMechanisms.isTemporalFrozen(object.instanceId)
        || state.specialMechanisms.isChronoMovementLocked(object.instanceId, state.tick))) {
        applyRuntimeJumpjetPose(state, object);
        return;
    }
    if (f.lastTick == state.tick) { applyRuntimeJumpjetPose(state, object); return; }
    // Do not bank movement while waiting for a path, collision clearance or
    // a new order. A completed segment can only continue during its own tick.
    f.segmentCarry = 0;
    f.lastTick = state.tick;
    const auto mission = toLowerAsciiLocal(object.mission);
    const bool landing = runtimeJumpjetShouldLand(state, object, *unit, f);
    const bool wasAirborne = object.airborne;
    if (mission != "landingtodeploy" && mission != "takingoffafterundeploy")
        (void)advanceRuntimeJumpjetHeight(state, object, landing);
    if (wasAirborne != object.airborne) refreshRuntimeObjectOccupancyAfterLocalChange(state);
    const double multiplier = runtimeVeterancyAbilityMultiplier(state, object, VeteranAbility::Faster);
    const auto maximum = std::llround(unit->jumpjetSpeed * std::clamp(multiplier, 0.01, 10.0) * runtimeJumpjetQ16);
    const auto acceleration = std::llround(unit->jumpjetAccel * runtimeJumpjetQ16);
    const int hz = std::max(1, state.config.fixedTickHz);
    const auto dv = std::max<std::int64_t>(1, acceleration*15/hz);
    std::int64_t desired = 0;
    if (f.active) {
        desired = maximum;
        const auto behavior = state.behaviors.find(object.instanceId);
        const bool terminal = behavior == state.behaviors.end() || behavior->second.path.empty();
        if (terminal && acceleration > 0) {
            const double braking = std::sqrt(2.0 * double(acceleration) * double(f.length-f.progress));
            desired = std::min(desired, static_cast<std::int64_t>(braking));
        }
        if (behavior == state.behaviors.end() && f.speed > 0) desired = std::min(desired, f.speed);
    }
    f.speed = acceleration == 0 ? desired : f.speed + std::clamp(desired-f.speed, -dv, dv);
    if (f.active && f.speed > 0) {
        const auto previous = f.progress;
        const auto available = std::max<std::int64_t>(1, f.speed*15/hz);
        f.progress = std::min(f.length, f.progress + available);
        object.movementAnimationDistance = std::fmod(object.movementAnimationDistance
            + double(f.progress-previous)/runtimeJumpjetCellQ16, 1024.0);
        if (f.progress == f.length) {
            f.active = false;
            f.segmentCarry = available - (f.progress - previous);
        }
    }
    if (!unit->jumpjetNoWobbles && unit->jumpjetWobbles > 0.0
        && !landing
        && mission != "landingtodeploy" && mission != "takingoffafterundeploy") {
        f.wobblePhase = (f.wobblePhase + static_cast<int>(std::llround(unit->jumpjetWobbles*65536.0/hz))) % 65536;
    }
    applyRuntimeJumpjetPose(state, object);
    advanceRuntimeKirovClearanceMotion(state, object, f, *unit);
    applyRuntimeJumpjetPose(state, object);
}

bool advanceRuntimeJumpjetFlights(RuntimeWindowState& state) {
    RuntimePerfTraceScope stageTrace("advanceRuntimeJumpjetFlights", state.tick);
    bool changed = false;
    for (auto& object : state.world.accessObjects()) {
        if (object.alive && object.airborne && runtimeJumpjetProfile(state, object)) {
            advanceRuntimeJumpjetFlight(state, object);
            if (!object.airborne) state.jumpjetFlights.erase(object.instanceId);
            changed = true;
        } else {
            state.jumpjetFlights.erase(object.instanceId);
            object.aircraftBuildingClearanceLevel = -1.0;
        }
    }
    for (auto it = state.jumpjetFlights.begin(); it != state.jumpjetFlights.end();) {
        if (!findObjectById(state, it->first)) it = state.jumpjetFlights.erase(it); else ++it;
    }
    return changed;
}

void bindRuntimeJumpjetMoveGoal(RuntimeWindowState& state, const WorldObjectInstance& object,
    const RuntimeCellPoint& requested, const RuntimeCellPoint& assigned, int minimumRadius = 8) {
    if (!object.airborne || !runtimeJumpjetProfile(state, object)) return;
    auto& f = runtimeJumpjetFlightFor(state, object);
    f.goalX=std::clamp(requested.x,0,511); f.goalY=std::clamp(requested.y,0,511);
    f.targetX=assigned.x; f.targetY=assigned.y;
    f.goalRadius=std::clamp(std::max(minimumRadius,
        static_cast<int>(std::ceil(std::hypot(assigned.x-f.goalX, assigned.y-f.goalY)))),1,512);
}

int beginRuntimeJumpjetSegment(RuntimeWindowState& state, const WorldObjectInstance& object,
    const RuntimeCellPoint& from, const RuntimeCellPoint& to) {
    auto& f = runtimeJumpjetFlightFor(state, object);
    const auto carry = f.lastTick == state.tick && !f.active
        && f.toX == from.x && f.toY == from.y ? f.segmentCarry : 0;
    f.fromX=from.x; f.fromY=from.y; f.toX=to.x; f.toY=to.y;
    f.stopped = false;
    f.length = std::llround(std::hypot(to.x-from.x, to.y-from.y)*runtimeJumpjetCellQ16);
    // Navigation still approves one cell at a time. Stock speeds use less
    // than one cell/tick; extreme mod speeds retain the existing boundary cap.
    f.progress = std::min(carry, std::max<std::int64_t>(0, f.length-1));
    f.segmentCarry = 0;
    f.active = f.length > 0;
    return 2; // A live segment holds the same two air cells until physics completes it.
}

void stopRuntimeUnstartedJumpjetSegment(RuntimeWindowState& state, WorldObjectInstance& object) {
    const auto it = state.jumpjetFlights.find(object.instanceId);
    if (it == state.jumpjetFlights.end() || !it->second.active
        || it->second.progress != 0 || it->second.speed != 0) return;
    auto& f = it->second;
    const auto previous = runtimeCellPointForObject(object);
    const RuntimeCellPoint resting{f.fromX, f.fromY};
    f.active = false; f.length = 0; f.segmentCarry = 0; f.toX = f.fromX; f.toY = f.fromY;
    setRuntimeObjectCell(state, object, resting, false);
    updateRuntimeTickOccupancyForMove(state, object, previous, resting);
    applyRuntimeJumpjetPose(state, object);
}

void stopRuntimeJumpjetSegment(RuntimeWindowState& state, WorldObjectInstance& object) {
    const auto found = state.jumpjetFlights.find(object.instanceId);
    if (found == state.jumpjetFlights.end() || !found->second.active) return;
    // Preserve the old no-travel rollback so an unstarted flight does not
    // occupy the next cell while visibly still sitting at its departure.
    if (found->second.progress == 0 && found->second.speed == 0) {
        stopRuntimeUnstartedJumpjetSegment(state, object);
        return;
    }
    auto& flight = found->second;
    flight.active = false;
    flight.stopped = true;
    flight.speed = 0;
    flight.segmentCarry = 0;
    applyRuntimeJumpjetPose(state, object);
}

void resumeRuntimeJumpjetSegment(RuntimeWindowState& state, WorldObjectInstance& object,
    RuntimeObjectBehavior& behavior, bool resumeStopped = true) {
    if (!object.airborne || !runtimeJumpjetProfile(state, object)) return;
    const auto it = state.jumpjetFlights.find(object.instanceId);
    if (it != state.jumpjetFlights.end() && it->second.stopped && resumeStopped) {
        // A new command releases the frozen segment. Retain its exact
        // progress so the first resumed frame cannot jump to a cell anchor.
        it->second.stopped = false;
        it->second.active = true;
    }
    if (it != state.jumpjetFlights.end() && it->second.active) {
        behavior.moveCooldownTicks = 2;
        behavior.moveSegmentFrom = {it->second.fromX, it->second.fromY};
    }
}

bool advanceRuntimeJumpjetSegment(RuntimeWindowState& state, WorldObjectInstance& object, RuntimeObjectBehavior& behavior) {
    advanceRuntimeJumpjetFlight(state, object);
    const auto& f = runtimeJumpjetFlightFor(state, object);
    behavior.moveCooldownTicks = f.active ? 2 : 1;
    behavior.renderMoveTicksRemaining = behavior.renderMoveTicksTotal = f.active ? 2 : 0;
    return true;
}

bool runtimeJumpjetCanEnter(const RuntimeWindowState& state, const WorldObjectInstance& object,
    const RuntimeCellPoint& to) {
    const auto* unit = runtimeJumpjetProfile(state, object);
    if (!unit || !object.airborne) return true;
    if (!runtimeFactoryAirExitReadyToMove(state, object)) return false;
    if (unit->jumpjetSpeed <= 0.0) return false;
    const auto it = state.jumpjetFlights.find(object.instanceId);
    const auto height = it == state.jumpjetFlights.end() ? object.flightLevel*runtimeJumpjetCellQ16 : it->second.height;
    return height >= terrainLevelAtRuntimeCell(state, to.x, to.y)*runtimeJumpjetCellQ16 + runtimeJumpjetQ16;
}

int runtimeJumpjetTurnStep(const RuntimeWindowState& state, const UnitType& unit) {
    const auto hz = std::max(1, state.config.fixedTickHz);
    const std::int64_t rate = unit.jumpjetTurnRate*15LL;
    return static_cast<int>(std::min<std::int64_t>(128,
        (static_cast<std::int64_t>(state.tick)+1)*rate/hz - static_cast<std::int64_t>(state.tick)*rate/hz));
}
