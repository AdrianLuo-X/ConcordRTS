// The stock carrier's black central deck opening was decoded independently
// from carrier.vxl/HVA. These are model coordinates relative to its centered
// hull pivot, converted through the same native 0.18 model-units/lepton scale
// as weapon origins; camera fitting never controls simulation motion.
bool runtimeCarrierHasDeckFlight(const RuntimeWindowState& state,
    const WorldObjectInstance& child) {
    const auto sortie = state.specialMechanisms.carrierChildren().find(child.instanceId);
    const auto* parent = sortie == state.specialMechanisms.carrierChildren().end()
        ? nullptr : findObjectById(state, sortie->second.parentObjectId);
    return child.alive && toUpperAsciiLocal(child.type) == "HORNET" && parent && parent->alive
        && toUpperAsciiLocal(parent->type) == "CARRIER";
}

bool runtimeCarrierHasContinuousAttackFlight(const RuntimeWindowState& state,
    const WorldObjectInstance& child) {
    const auto sortie = state.specialMechanisms.carrierChildren().find(child.instanceId);
    const auto* unit = findRuntimeUnitType(state.rules, child);
    return child.airborne && runtimeCarrierHasDeckFlight(state, child)
        && sortie != state.specialMechanisms.carrierChildren().end()
        && sortie->second.phase == CarrierChildPhase::Attacking
        && unit && runtimeUnitUsesAirMovement(*unit) && !runtimeUnitUsesJumpjetMovement(*unit)
        && !unit->missileSpawn && !state.rules.findSpawnedMissileProfile(child.type);
}

std::array<double, 3> runtimeCarrierDeckMouth(const WorldObjectInstance& parent) {
    constexpr double modelUnitsPerCell = 0.18 * 256.0;
    constexpr double x = 4.485871315002441 / modelUnitsPerCell;
    constexpr double y = 0.9853174076523885 / modelUnitsPerCell;
    constexpr double z = (7.883922510390932 / 0.18) * (2.449489742783178 / 256.0);
    const auto forward = runtimeWorldForwardForFacing(std::max(0, parent.facing));
    return {runtimeObjectRenderX(parent) + forward[0] * x - forward[1] * y,
        runtimeObjectRenderY(parent) + forward[1] * x + forward[0] * y,
        runtimeObjectRenderLevel(parent) + z};
}

// A docked aircraft is cargo of its living ship, not a second blast target.
// Derive protection from the registered slot and physical departure pose so
// saved airborne aircraft never gain a type-wide or launch-phase immunity.
bool runtimeCarrierAircraftHasDeckProtection(const RuntimeWindowState& state,
    const WorldObjectInstance& child) {
    if (!child.alive || child.kind != WorldObjectKind::Unit) return false;
    const auto registered = state.specialMechanisms.carrierChildren().find(child.instanceId);
    if (registered == state.specialMechanisms.carrierChildren().end()) return false;
    const auto& sortie = registered->second;
    const auto parentState = state.specialMechanisms.carrierParents().find(sortie.parentObjectId);
    const auto* parent = findObjectById(state, sortie.parentObjectId);
    if (!parent || !parent->alive || parent->kind != WorldObjectKind::Unit
        || parentState == state.specialMechanisms.carrierParents().end()
        || sortie.childObjectId != child.instanceId || sortie.slot < 0
        || sortie.slot >= parentState->second.capacity) return false;
    const auto slot = parentState->second.childBySlot.find(sortie.slot);
    if (slot == parentState->second.childBySlot.end() || slot->second != child.instanceId) return false;
    const std::string childType = toUpperAsciiLocal(child.type);
    const std::string parentType = toUpperAsciiLocal(parent->type);
    // Unreleased spawned missiles remain cargo of the exact registered slot.
    // Separation removes this graph before the independent airborne object flies.
    const auto* childUnit = findRuntimeUnitType(state.rules, child);
    const auto* parentUnit = findRuntimeUnitType(state.rules, *parent);
    if (childUnit && childUnit->missileSpawn
        && state.rules.findSpawnedMissileProfile(child.type)
        && parentUnit && parentUnit->spawnsNumber > sortie.slot
        && toUpperAsciiLocal(parentUnit->spawns) == childType
        && toUpperAsciiLocal(parentState->second.childType) == childType) {
        if (sortie.phase != CarrierChildPhase::Docked || child.airborne
            || child.dockedToObjectId != parent->instanceId
            || child.dockIndex != sortie.slot) return false;
        const auto preparing = state.specialMechanisms.spawnedMissiles().find(child.instanceId);
        return preparing == state.specialMechanisms.spawnedMissiles().end()
            || (!preparing->second.released
                && preparing->second.objectId == child.instanceId
                && preparing->second.parentObjectId == parent->instanceId
                && preparing->second.slot == sortie.slot
                && toUpperAsciiLocal(preparing->second.typeId) == childType);
    }
    const bool hornet = childType == "HORNET" && parentType == "CARRIER";
    if (!hornet && !(childType == "ASW" && parentType == "DEST")) return false;
    if (!child.airborne) return sortie.phase == CarrierChildPhase::Docked
        || sortie.phase == CarrierChildPhase::Launching;
    if (!hornet || sortie.phase != CarrierChildPhase::Launching || !child.hasRenderPosition) return false;
    const auto mouth = runtimeCarrierDeckMouth(*parent);
    return std::abs(runtimeObjectRenderLevel(child) - mouth[2]) <= 1e-10
        && std::hypot(runtimeObjectRenderX(child) - mouth[0],
            runtimeObjectRenderY(child) - mouth[1]) <= 0.20;
}

void applyRuntimeCarrierDeckFlightPresentation(const RuntimeWindowState& state,
    WorldObjectInstance& child) {
    if (!runtimeCarrierHasDeckFlight(state, child)) return;
    const auto& sortie = state.specialMechanisms.carrierChildren().at(child.instanceId);
    const auto* parent = findObjectById(state, sortie.parentObjectId);
    if (!child.airborne || sortie.phase == CarrierChildPhase::Docked) {
        child.aircraftDockFlightProgress = -1.0;
        return;
    }
    const double deck = runtimeCarrierDeckMouth(*parent)[2];
    child.aircraftDockFlightProgress = std::clamp(
        (runtimeObjectRenderLevel(child) - deck) / std::max(0.001, child.flightLevel - deck), 0.0, 1.0);
    if (child.hasRenderPosition) child.level = static_cast<int>(std::lround(child.renderLevel));
}

void setRuntimeCarrierDeckFlightPose(const RuntimeWindowState& state, WorldObjectInstance& child,
    double x, double y, double level, bool moving) {
    const double distance = std::hypot(x - runtimeObjectRenderX(child), y - runtimeObjectRenderY(child));
    child.x = static_cast<int>(std::lround(x)); child.y = static_cast<int>(std::lround(y));
    child.cell = runtimeCellKey(child.x, child.y);
    child.level = static_cast<int>(std::lround(level));
    child.hasRenderPosition = true; child.renderMoveActive = moving;
    child.renderX = x; child.renderY = y; child.renderLevel = level;
    child.onBridge = child.renderOnBridge = false; child.subCell = -1;
    child.movementAnimationDistance = std::fmod(child.movementAnimationDistance + distance, 1024.0);
    applyRuntimeCarrierDeckFlightPresentation(state, child);
}

double runtimeCarrierVerticalStep(const RuntimeWindowState& state, double remaining) {
    if (remaining <= 0.0) return 0.0;
    const auto distance = std::max<std::int64_t>(1,
        static_cast<std::int64_t>(std::llround(remaining * runtimeAircraftCellQ16)));
    return std::min(remaining, double(runtimeFixedWingVerticalStep(state, distance)) / runtimeAircraftCellQ16);
}

void beginRuntimeCarrierDeckDeparture(RuntimeWindowState& state, WorldObjectInstance& child,
    const WorldObjectInstance& parent) {
    const auto mouth = runtimeCarrierDeckMouth(parent);
    if (!child.airborne) queueRuntimeAircraftAuxSound(state, child, true);
    child.airborne = true; child.flightLevel = std::max(runtimeAircraftCruiseLevel(state),
        static_cast<int>(std::ceil(mouth[2])) + 1);
    child.dockedToObjectId = child.dockIndex = -1;
    child.facing = parent.facing; child.bodyFacingClock = {};
    child.mission = "CarrierLaunching";
    setRuntimeCarrierDeckFlightPose(state, child, mouth[0], mouth[1], mouth[2], true);
}

bool advanceRuntimeCarrierDeckDeparture(RuntimeWindowState& state, WorldObjectInstance& child,
    const WorldObjectInstance& parent, const SpecialEventSink& sink) {
    const auto mouth = runtimeCarrierDeckMouth(parent);
    const double before = runtimeObjectRenderLevel(child);
    const double level = std::min(double(child.flightLevel), before
        + runtimeCarrierVerticalStep(state, child.flightLevel - before));
    setRuntimeCarrierDeckFlightPose(state, child, mouth[0], mouth[1], level, true);
    child.mission = "CarrierLaunching";
    if (level >= child.flightLevel) {
        const auto& own = state.specialMechanisms.carrierChildren().at(child.instanceId);
        // Keep the authored ascent; release horizontal flight only after an
        // earlier slot clears the mouth. Existing saved phases/poses own it.
        for (const auto& [id, sortie] : state.specialMechanisms.carrierChildren()) {
            if (sortie.parentObjectId != parent.instanceId || sortie.slot >= own.slot
                || (sortie.phase != CarrierChildPhase::Launching
                    && sortie.phase != CarrierChildPhase::Attacking)) continue;
            const auto* earlier = findObjectById(state, id);
            if (earlier && earlier->alive && earlier->airborne
                && std::hypot(runtimeObjectRenderX(*earlier) - mouth[0],
                    runtimeObjectRenderY(*earlier) - mouth[1]) < 1.0) return true;
        }
        state.specialMechanisms.notifyCarrierChildLaunched(child.instanceId, state.tick, sink);
        child.mission = "Attacking";
    }
    return true;
}

// Cruise owns a continuous body, rather than a sequence of terrain cells.
// The existing world-facing clock retains authored ROT and survives saves;
// once the turn completes the body travels straight toward the live target.
bool advanceRuntimeCarrierAircraftToward(RuntimeWindowState& state, WorldObjectInstance& child,
    RuntimeObjectBehavior& behavior, double goalX, double goalY) {
    if (!runtimeCarrierHasContinuousAttackFlight(state, child)) return false;
    const double x = runtimeObjectRenderX(child), y = runtimeObjectRenderY(child);
    const double dx = std::clamp(goalX, 0.0, 511.999) - x;
    const double dy = std::clamp(goalY, 0.0, 511.999) - y;
    const double distance = std::hypot(dx, dy);
    double travel = std::min(distance, 1.0 / runtimeFixedWingCruiseInterval(state, child));
    double nextX = x, nextY = y;
    if (distance > 1e-12 && travel > 0.0) {
        const int desired = runtimeFacingFromRenderedDelta(dx, dy);
        child.facing = runtimeAdvanceBodyFacing(state, child, std::max(0, child.facing), desired);
        if (child.facing == desired && !child.bodyFacingClock.active()) {
            nextX += dx * travel / distance; nextY += dy * travel / distance;
        } else {
            constexpr double tau = 6.2831853071795864769;
            const double maxTurn = std::max(1, runtimeBodyTurnStep(state, child))
                * tau / 256.0 * 15.0 / std::max(1, state.config.fixedTickHz);
            travel = std::min(travel, distance * maxTurn * 0.5);
            const double heading = (child.bodyFacingClock.active()
                ? child.bodyFacingClock.current16(state.config.fixedTickHz)
                : std::max(0, child.facing) * 256) * tau / 65536.0;
            nextX += (std::sin(heading) - std::cos(heading)) * 0.7071067811865475244 * travel;
            nextY += (-std::sin(heading) - std::cos(heading)) * 0.7071067811865475244 * travel;
        }
    }
    nextX = std::clamp(nextX, 0.0, 511.999); nextY = std::clamp(nextY, 0.0, 511.999);
    behavior.path.clear(); behavior.pathRequestPending = false; behavior.moveCooldownTicks = 0;
    behavior.moveSegmentFrom = {-1, -1};
    behavior.renderMoveTicksTotal = behavior.renderMoveTicksRemaining = 0;
    const auto previous = runtimeCellPointForObject(child);
    const bool moved = nextX != x || nextY != y;
    setRuntimeCarrierDeckFlightPose(state, child, nextX, nextY, runtimeObjectRenderLevel(child), moved);
    const auto next = runtimeCellPointForObject(child);
    if (next != previous) updateRuntimeTickOccupancyForMove(state, child, previous, next);
    behavior.state = RuntimeBehaviorState::Chasing;
    setRuntimeMissionIntent(state, child, RuntimeMissionIntent::Moving, "continuousCarrierAircraftFlight");
    if (moved) ++state.actionSummary.movedThisTick;
    return moved;
}

// Return at the normal aircraft Speed clock, then descend into the opening.
// The parent remains a moving target through the entire approach and landing.
bool advanceRuntimeCarrierDeckReturn(RuntimeWindowState& state, WorldObjectInstance& child,
    const WorldObjectInstance& parent, int slot, const SpecialEventSink& sink) {
    clearRuntimeCarrierAttackPreservingPose(state, child);
    child.dockedToObjectId = child.dockIndex = -1;
    child.airborne = true; child.mission = "CarrierReturning";
    const auto mouth = runtimeCarrierDeckMouth(parent);
    const double x = runtimeObjectRenderX(child), y = runtimeObjectRenderY(child);
    const double dx = mouth[0] - x, dy = mouth[1] - y, distance = std::hypot(dx, dy);
    const double step = 1.0 / std::max(1, runtimeMovementIntervalTicks(state, child));
    const double travel = std::min(step, distance);
    const double nextX = distance > 1e-12 ? x + dx * travel / distance : mouth[0];
    const double nextY = distance > 1e-12 ? y + dy * travel / distance : mouth[1];
    double level = runtimeObjectRenderLevel(child);
    // A recalled partly emerged aircraft returns from its actual height.
    // No cruise-height reset is allowed at the recall boundary.
    if (distance <= std::max(0.20, step * 1.25)) {
        if (level > mouth[2]) level = std::max(mouth[2], level
            - runtimeCarrierVerticalStep(state, level - mouth[2]));
        else if (level < mouth[2]) level = std::min(mouth[2], level
            + runtimeCarrierVerticalStep(state, mouth[2] - level));
    }
    if (distance > 1e-8) {
        const int facing = runtimeFacingFromDelta(static_cast<int>(std::lround(dx * 1024.0)),
            static_cast<int>(std::lround(dy * 1024.0)));
        if (facing >= 0) child.facing = facing;
    }
    setRuntimeCarrierDeckFlightPose(state, child, nextX, nextY, level, true);
    if (distance <= step + 1e-10 && std::abs(level - mouth[2]) < 1e-10) {
        queueRuntimeAircraftAuxSound(state, child, false, 0);
        child.x = parent.x; child.y = parent.y; child.cell = runtimeCellKey(child.x, child.y);
        child.level = parent.level; child.dockedToObjectId = parent.instanceId; child.dockIndex = slot;
        child.airborne = false; child.flightLevel = 0; child.mission = "Dock";
        child.aircraftDockFlightProgress = -1.0; clearRuntimeRenderPosition(child);
        initializeRuntimeObjectAmmo(child, runtimeAmmoRulesForObject(state.rules, child), true);
        state.specialMechanisms.notifyCarrierChildDocked(child.instanceId, state.tick, sink);
    }
    return true;
}
