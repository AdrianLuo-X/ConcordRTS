#include "simulation/movement/ground/RuntimeStoppedMovePoseSave.inl"

// Startup distance controls the next accepted segment, so retain it even at
// a segment boundary where no interpolation record is active. Earlier saves
// already had a cruise clock; importing that active clock must not restart
// acceleration on the following cell.
std::int64_t runtimeLegacyVehicleStartupDistanceQ16(const RuntimeWindowState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move) {
    if (move.moveCooldownTicks <= 0) return 0;
    const double ramp = runtimeVehicleStartupAccelerationDistance(state, object);
    if (!(ramp > 0.0) || !std::isfinite(ramp)) return 0;
    double segment = std::sqrt(2.0);
    if (move.moveSegmentFrom.x >= 0 && move.moveSegmentFrom.y >= 0)
        segment = std::hypot(double(object.x - move.moveSegmentFrom.x),
            double(object.y - move.moveSegmentFrom.y));
    const long double distance = std::ceil((static_cast<long double>(ramp) + segment) * 65536.0L);
    return distance >= static_cast<long double>(RuntimeVehicleStartupMotion::maximumDistanceQ16)
        ? RuntimeVehicleStartupMotion::maximumDistanceQ16
        : static_cast<std::int64_t>(distance);
}

// The original Omni approach can retain navigation ownership of FROM until
// the occupied destination becomes available.
bool runtimeOmniCrushAttackMovePresentation(const RuntimeWindowState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move) {
    return move.kind == RuntimeBehaviorKind::Attack
        && move.targetObjectId >= 0 && !move.attackGround && !move.attackBridge
        && move.renderArrivalDistanceAfter == -1.0
        && runtimeObjectUsesOmniCrushApproach(state, object);
}

bool runtimeGroundVehicleAttackMovePresentation(const RuntimeWindowState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move) {
    return object.kind == WorldObjectKind::Unit && !object.authoredAircraft && !object.airborne
        && findRuntimeUnitType(state.rules, object) != nullptr
        && runtimeObjectMovementMode(state, object) != PathMovementMode::Air
        && move.kind == RuntimeBehaviorKind::Attack && move.moveCooldownTicks > 0
        && move.moveSegmentFrom.x >= 0 && move.moveSegmentFrom.x < 512
        && move.moveSegmentFrom.y >= 0 && move.moveSegmentFrom.y < 512;
}

// Optional visual state. Movement cooldowns remain in the ordinary behavior
// records and determine gameplay timing, including after a legacy save loads.
bool runtimeNavalOffsetMovePresentation(const RuntimeWindowState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move) {
    // A cancelled repair order can still be finishing its committed segment,
    // so the offsets, rather than a live repair reservation, own this state.
    return runtimeObjectUsesNavalDockPresentation(state, object)
        && move.renderArrivalDistanceAfter == -1.0
        && (move.renderFromOffsetX != 0.0 || move.renderFromOffsetY != 0.0
            || move.renderToOffsetX != 0.0 || move.renderToOffsetY != 0.0);
}

bool runtimeNavalFactoryMovePresentation(const RuntimeWindowState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& move) {
    if (!runtimeObjectUsesNavalDockPresentation(state, object)
        || move.kind != RuntimeBehaviorKind::Move || move.renderArrivalDistanceAfter != -1.0)
        return false;
    // Ordinary ship egress has zero offsets too. Its delivery lease remains
    // live until the last visible segment ends, so save that segment alongside
    // the lease instead of treating its committed navigation cell as arrival.
    return std::any_of(state.factoryDeliveries.leases().begin(), state.factoryDeliveries.leases().end(),
        [&](const RuntimeFactoryDeliveryLease& lease) {
            return lease.producedObjectId == object.instanceId
                && lease.domain == RuntimeFactoryDeliveryDomain::Naval
                && lease.lane == RuntimeProductionLane::Ships
                && !lease.rallyCell.onBridge
                && move.moveTarget == RuntimeCellPoint{lease.rallyCell.x, lease.rallyCell.y};
        });
}

void writeRuntimeVehicleArrivals(const RuntimeWindowState& state, std::ostream& output) {
    std::vector<int> ids;
    for (const auto& [id, move] : state.behaviors) {
        const auto* object = findObjectById(state, id);
        const bool navalOffset = object && runtimeNavalOffsetMovePresentation(state, *object, move);
        const bool navalFactory = object && runtimeNavalFactoryMovePresentation(state, *object, move);
        const bool committedAttack = object && runtimeGroundVehicleAttackMovePresentation(state, *object, move)
            && move.moveSegmentFrom == move.renderFrom;
        if (object != nullptr && object->alive
            && (runtimeObjectUsesVehicleArrival(state, *object) || runtimeObjectUsesVehicleStartup(state, *object)
                || navalOffset || navalFactory || committedAttack)
            && (move.kind == RuntimeBehaviorKind::Move
                || runtimeOmniCrushAttackMovePresentation(state, *object, move) || committedAttack)
            && object->hasRenderPosition
            && object->renderMoveActive && move.renderMoveTicksRemaining > 0
            && move.renderMoveTicksRemaining == move.moveCooldownTicks
            && move.renderMoveTicksTotal >= move.renderMoveTicksRemaining
            && (move.renderFrom != move.renderTo
                || (navalOffset && (move.renderFromOffsetX != move.renderToOffsetX
                    || move.renderFromOffsetY != move.renderToOffsetY))))
            ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    output << "\n[VehicleMovePresentation]\nSchema=2\nCount=" << ids.size() << '\n';
    const auto precision = output.precision();
    output << std::setprecision(17);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto& move = state.behaviors.at(ids[i]);
        const auto* object = findObjectById(state, ids[i]);
        output << i << '=' << ids[i] << '|' << move.renderFrom.x << '|' << move.renderFrom.y
            << '|' << move.renderTo.x << '|' << move.renderTo.y
            << '|' << move.renderFromLevel << '|' << move.renderToLevel
            << '|' << runtimeBoolText(move.renderFromOnBridge) << '|' << runtimeBoolText(move.renderToOnBridge)
            << '|' << move.renderMoveTicksTotal << '|' << move.renderMoveTicksRemaining
            << '|' << object->movementAnimationDistance << '|' << move.renderArrivalDistanceAfter
            << '|' << move.renderFromOffsetX << '|' << move.renderFromOffsetY
            << '|' << move.renderToOffsetX << '|' << move.renderToOffsetY << '\n';
    }
    ids.clear();
    for (const auto& [id, move] : state.behaviors) {
        const auto* object = findObjectById(state, id);
        if (object && object->alive && runtimeObjectUsesVehicleStartup(state, *object)
            && (move.kind == RuntimeBehaviorKind::Move || move.kind == RuntimeBehaviorKind::Attack)
            && (move.moveStartupDistanceQ16 != 0 || move.renderStartupAccelerationDistance != 0.0))
            ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    output << "\n[VehicleStartupMotion]\nSchema=1\nCount=" << ids.size() << '\n';
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto& move = state.behaviors.at(ids[i]);
        output << i << '=' << ids[i] << '|' << move.moveStartupDistanceQ16
            << '|' << move.renderStartupAccelerationDistance << '\n';
    }
    ids.clear();
    for (const auto& object : state.world.getObjects()) {
        const auto move = state.behaviors.find(object.instanceId);
        if (object.alive && runtimeObjectUsesNavalDockPresentation(state, object)
            && object.hasRenderPosition && !object.renderMoveActive && object.dockedToObjectId < 0
            && (object.renderX != object.x || object.renderY != object.y)
            && (move == state.behaviors.end() || move->second.moveCooldownTicks <= 0))
            ids.push_back(object.instanceId);
    }
    std::sort(ids.begin(), ids.end());
    output << "\n[NavalDockStopPoses]\nSchema=1\nCount=" << ids.size() << '\n';
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto* object = findObjectById(state, ids[i]);
        output << i << '=' << ids[i] << '|' << object->renderX << '|' << object->renderY
            << '|' << object->renderLevel << '|' << runtimeBoolText(object->renderOnBridge) << '\n';
    }
    writeRuntimeStoppedMovePoses(state, output);
    output.precision(precision);
}

bool loadRuntimeVehicleArrivals(RuntimeWindowState& state, const IniFile& ini, std::string& error) {
    constexpr const char* section = "VehicleMovePresentation";
    const auto fail = [&]() { error = "invalid vehicle move presentation"; return false; };
    int schema = 2, count = 0;
    if (ini.getSection(section) && (!parseRuntimeInt(ini.getValue(section, "Schema", ""), schema) || (schema != 1 && schema != 2)
        || !parseRuntimeInt(ini.getValue(section, "Count", ""), count) || count < 0
        || static_cast<std::size_t>(count) > state.world.getObjects().size())) return fail();
    struct PendingStartup {
        std::int64_t distanceQ16 = 0;
        double accelerationDistance = 0.0;
    };
    std::unordered_map<int, PendingStartup> startups;
    constexpr const char* startupSection = "VehicleStartupMotion";
    const bool hasStartup = ini.getSection(startupSection) != nullptr;
    for (const auto& [id, move] : state.behaviors) {
        const auto* object = findObjectById(state, id);
        if (object && object->alive && runtimeObjectUsesVehicleStartup(state, *object)
            && (move.kind == RuntimeBehaviorKind::Move || move.kind == RuntimeBehaviorKind::Attack))
            startups.emplace(id, PendingStartup{hasStartup ? 0
                : runtimeLegacyVehicleStartupDistanceQ16(state, *object, move), 0.0});
    }
    if (hasStartup) {
        int startupSchema = 0, startupCount = -1;
        if (!parseRuntimeInt(ini.getValue(startupSection, "Schema", ""), startupSchema) || startupSchema != 1
            || !parseRuntimeInt(ini.getValue(startupSection, "Count", ""), startupCount) || startupCount < 0
            || static_cast<std::size_t>(startupCount) > state.world.getObjects().size()) return fail();
        std::unordered_set<int> startupIds;
        for (int i = 0; i < startupCount; ++i) {
            const auto fields = splitRuntimeSaveFields(ini.getValue(startupSection, std::to_string(i), ""));
            int id = -1; std::uint64_t distanceQ16 = 0; double ramp = 0.0;
            if (fields.size() != 3 || !parseRuntimeInt(fields[0], id) || !startupIds.insert(id).second
                || !parseRuntimeUint64Strict(fields[1], distanceQ16)
                || distanceQ16 > static_cast<std::uint64_t>(RuntimeVehicleStartupMotion::maximumDistanceQ16)
                || !parseRuntimeFiniteDouble(fields[2], ramp) || ramp < 0.0) return fail();
            const auto found = startups.find(id);
            const auto move = state.behaviors.find(id);
            if (found == startups.end() || move == state.behaviors.end()
                || (ramp > 0.0 && (distanceQ16 == 0 || move->second.moveCooldownTicks <= 0))) return fail();
            found->second = {static_cast<std::int64_t>(distanceQ16), ramp};
        }
    }
    std::unordered_set<int> seen;
    std::vector<std::pair<int, std::array<double, 3>>> stopped;
    constexpr const char* stoppedSection = "NavalDockStopPoses";
    if (ini.getSection(stoppedSection)) {
        int stoppedSchema = 0, stoppedCount = 0;
        if (!parseRuntimeInt(ini.getValue(stoppedSection, "Schema", ""), stoppedSchema) || stoppedSchema != 1
            || !parseRuntimeInt(ini.getValue(stoppedSection, "Count", ""), stoppedCount) || stoppedCount < 0
            || static_cast<std::size_t>(stoppedCount) > state.world.getObjects().size()) return fail();
        for (int i = 0; i < stoppedCount; ++i) {
            const auto fields = splitRuntimeSaveFields(ini.getValue(stoppedSection, std::to_string(i), ""));
            int id = -1; std::array<double, 3> pose{}; bool bridge = false;
            if (fields.size() != 5 || !parseRuntimeInt(fields[0], id) || !seen.insert(id).second
                || !parseRuntimeFiniteDouble(fields[1], pose[0]) || !parseRuntimeFiniteDouble(fields[2], pose[1])
                || !parseRuntimeFiniteDouble(fields[3], pose[2]) || !parseRuntimeStrictBool(fields[4], bridge)
                || bridge || pose[0] < 0 || pose[0] >= 512 || pose[1] < 0 || pose[1] >= 512) return fail();
            const auto* object = findObjectById(state, id);
            const auto move = state.behaviors.find(id);
            if (!object || !object->alive || !runtimeObjectUsesNavalDockPresentation(state, *object)
                || object->dockedToObjectId >= 0 || object->dockIndex >= 0
                || std::abs(pose[2] - object->level) > 0.000001
                || std::abs(pose[0] - object->x) >= 2 || std::abs(pose[1] - object->y) >= 2
                || (pose[0] == object->x && pose[1] == object->y)
                || (move != state.behaviors.end() && move->second.moveCooldownTicks > 0)) return fail();
            stopped.push_back({id, pose});
        }
    }
    std::vector<RuntimeStoppedMovePoseRecord> stoppedMoves;
    if (!parseRuntimeStoppedMovePoses(state, ini, stoppedMoves, seen, error)) return false;
    struct PendingPresentation {
        int id = -1;
        RuntimeObjectBehavior move;
        double animationDistance = 0.0;
    };
    std::vector<PendingPresentation> pending;
    std::unordered_set<int> movingPresentationIds;
    pending.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue(section, std::to_string(i), ""));
        int id = -1, total = 0, remaining = 0;
        RuntimeCellPoint from, to;
        double fromLevel = 0.0, toLevel = 0.0, distance = 0.0, after = -1.0;
        bool fromBridge = false, toBridge = false;
        if (fields.size() != (schema == 1 ? 13U : 17U) || !parseRuntimeInt(fields[0], id) || !seen.insert(id).second
            || !parseRuntimeInt(fields[1], from.x) || !parseRuntimeInt(fields[2], from.y)
            || !parseRuntimeInt(fields[3], to.x) || !parseRuntimeInt(fields[4], to.y)
            || !parseRuntimeFiniteDouble(fields[5], fromLevel) || !parseRuntimeFiniteDouble(fields[6], toLevel)
            || !parseRuntimeStrictBool(fields[7], fromBridge) || !parseRuntimeStrictBool(fields[8], toBridge)
            || !parseRuntimeInt(fields[9], total) || !parseRuntimeInt(fields[10], remaining)
            || !parseRuntimeFiniteDouble(fields[11], distance) || !parseRuntimeFiniteDouble(fields[12], after)
            || from.x < 0 || from.y < 0 || to.x < 0 || to.y < 0
            || from.x >= 512 || from.y >= 512 || to.x >= 512 || to.y >= 512
            || std::abs(to.x - from.x) > 1 || std::abs(to.y - from.y) > 1
            || total <= 0 || total > 100000 || remaining <= 0 || remaining > total
            || distance < 0.0 || distance >= 1024.0
            || (after != -1.0 && (after < 0.0 || after >= RuntimeVehicleArrivalMotion::brakingDistance))) return fail();
        auto* object = findMutableObjectById(state, id);
        const auto found = state.behaviors.find(id);
        if (object == nullptr || !object->alive
            || found == state.behaviors.end()
            || (found->second.kind != RuntimeBehaviorKind::Move
                && !runtimeOmniCrushAttackMovePresentation(state, *object, found->second)
                && !(runtimeGroundVehicleAttackMovePresentation(state, *object, found->second)
                    && found->second.moveSegmentFrom == from))
            || found->second.moveCooldownTicks != remaining) return fail();
        const bool pendingOmni = from != to
            && runtimeObjectUsesOmniCrushApproach(state, *object)
            && runtimeCellPointForObject(*object) == from;
        const bool fromLayerMatches = std::abs(fromLevel - object->level) <= 0.000001
            && fromBridge == object->onBridge;
        if (pendingOmni) {
            // This source has approached but not entered the occupied cell.
            // Its navigation authority is still FROM; validate TO against the
            // real terrain instead of weakening the original layer checks.
            const auto terrain = state.terrainByCell.find(runtimeCellKey(to.x, to.y));
            const bool hasDestination = (terrain != state.terrainByCell.end() && terrain->second)
                || std::any_of(state.world.getTerrainCells().begin(), state.world.getTerrainCells().end(),
                    [&](const TerrainCell& cell) { return cell.x == to.x && cell.y == to.y; });
            if (found->second.moveSegmentFrom != from || !hasDestination
                || std::abs(toLevel - terrainLevelAtRuntimeCell(state, to.x, to.y)) > 0.000001
                || toBridge != runtimeCellIsBridge(state, to.x, to.y)) return fail();
        } else if (found->second.transportTransferKind == RuntimeTransportTransferKind::Boarding) {
            const int hostId = found->second.transportTransferHostId;
            const auto hostPose = std::find_if(stoppedMoves.begin(), stoppedMoves.end(),
                [&](const auto& pose) { return pose.objectId == hostId; });
            const std::optional<std::pair<double, bool>> validatedHostPose = hostPose == stoppedMoves.end()
                ? std::nullopt : std::optional<std::pair<double,bool>>{{hostPose->position[2], hostPose->onBridge}};
            if (!runtimeTransportSavedBoardingEndpointAllowed(state, *object, from, to,
                    toLevel, toBridge, validatedHostPose)) return fail();
        } else if (runtimeCellPointForObject(*object) != to
            || std::abs(toLevel - object->level) > 0.000001 || toBridge != object->onBridge) return fail();
        // Validate all rows before changing live behavior or world poses. A
        // malformed late row must not partly restore an earlier unit.
        auto move = found->second;
        if (const auto startup = startups.find(id); startup != startups.end()) {
            move.moveStartupDistanceQ16 = startup->second.distanceQ16;
            move.renderStartupAccelerationDistance = startup->second.accelerationDistance;
            if (move.renderStartupAccelerationDistance > 0.0
                && move.moveStartupDistanceQ16 < std::llround(std::hypot(double(to.x - from.x),
                    double(to.y - from.y)) * 65536.0)) return fail();
        }
        move.renderFromOffsetX = move.renderFromOffsetY = move.renderToOffsetX = move.renderToOffsetY = 0.0;
        if (schema == 2) {
            if (!parseRuntimeFiniteDouble(fields[13], move.renderFromOffsetX)
                || !parseRuntimeFiniteDouble(fields[14], move.renderFromOffsetY)
                || !parseRuntimeFiniteDouble(fields[15], move.renderToOffsetX)
                || !parseRuntimeFiniteDouble(fields[16], move.renderToOffsetY)) return fail();
            for (double offset : {move.renderFromOffsetX, move.renderFromOffsetY,
                    move.renderToOffsetX, move.renderToOffsetY})
                if (std::abs(offset) > 16.0) return fail();
            if (from.x+move.renderFromOffsetX < 0 || from.x+move.renderFromOffsetX >= 512
                || from.y+move.renderFromOffsetY < 0 || from.y+move.renderFromOffsetY >= 512
                || to.x+move.renderToOffsetX < 0 || to.x+move.renderToOffsetX >= 512
                || to.y+move.renderToOffsetY < 0 || to.y+move.renderToOffsetY >= 512) return fail();
        }
        move.renderFrom = from; move.renderTo = to;
        move.renderFromLevel = fromLevel; move.renderToLevel = toLevel;
        move.renderFromOnBridge = fromBridge; move.renderToOnBridge = toBridge;
        move.renderMoveTicksTotal = total; move.renderMoveTicksRemaining = remaining;
        move.renderArrivalDistanceAfter = after;
        if (pendingOmni && !fromLayerMatches) {
            // At the end of an approach, a moving victim or new crew can keep
            // the destination occupied. Its one-tick clock freezes at the real
            // destination layer while navigation still owns FROM. A new
            // approach can also inherit a real stopped fractional pose; use
            // the existing StoppedMovePoses bounds for that departure only.
            const bool holdingDestination = schema == 2 && total == 1 && remaining == 1
                && after == -1.0
                && from.x + move.renderFromOffsetX == to.x + move.renderToOffsetX
                && from.y + move.renderFromOffsetY == to.y + move.renderToOffsetY
                && fromLevel == toLevel && fromBridge == toBridge;
            const bool inheritedStoppedPose = schema == 2
                && (move.renderFromOffsetX != 0.0 || move.renderFromOffsetY != 0.0)
                && std::abs(move.renderFromOffsetX) < 2.0
                && std::abs(move.renderFromOffsetY) < 2.0
                && fromLevel >= -128.0 && fromLevel <= 512.0;
            if (!holdingDestination && !inheritedStoppedPose) return fail();
        }
        if (move.kind == RuntimeBehaviorKind::Attack
            && !runtimeOmniCrushAttackMovePresentation(state, *object, move)
            && !(runtimeGroundVehicleAttackMovePresentation(state, *object, move)
                && move.moveSegmentFrom == from)) return fail();
        if (pendingOmni) {
            auto pose = *object;
            restoreRuntimeVehicleMovePresentation(pose, move);
            if (!runtimeOmniCrushPendingSegment(state, pose, move)) return fail();
        }
        const bool navalOffset = schema == 2
            && runtimeNavalOffsetMovePresentation(state, *object, move);
        const bool navalFactory = schema == 2
            && runtimeNavalFactoryMovePresentation(state, *object, move);
        if (!runtimeObjectUsesVehicleArrival(state, *object) && !runtimeObjectUsesVehicleStartup(state, *object)
            && !navalOffset && !navalFactory
            && !runtimeGroundVehicleAttackMovePresentation(state, *object, move)) return fail();
        if ((navalOffset || navalFactory) && (fromBridge || toBridge || std::abs(fromLevel - object->level) > 0.000001
            || std::abs(move.renderFromOffsetX) >= 2 || std::abs(move.renderFromOffsetY) >= 2
            || std::abs(move.renderToOffsetX) >= 2 || std::abs(move.renderToOffsetY) >= 2)) return fail();
        if (from == to && (!navalOffset
            || (move.renderFromOffsetX == move.renderToOffsetX
                && move.renderFromOffsetY == move.renderToOffsetY))) return fail();
        movingPresentationIds.insert(id);
        pending.push_back({id, std::move(move), distance});
    }
    for (const auto& [id, startup] : startups) {
        if (startup.accelerationDistance > 0.0 && !movingPresentationIds.contains(id)) return fail();
    }
    // All sidecar and presentation rows have passed, including any late row.
    // Commit startup authority before the restored curve evaluates its pose.
    for (const auto& [id, startup] : startups) {
        auto& move = state.behaviors.at(id);
        move.moveStartupDistanceQ16 = startup.distanceQ16;
        move.renderStartupAccelerationDistance = startup.accelerationDistance;
    }
    for (auto& presentation : pending) {
        auto& move = state.behaviors.at(presentation.id);
        move = std::move(presentation.move);
        auto* object = findMutableObjectById(state, presentation.id);
        restoreRuntimeVehicleMovePresentation(*object, move);
        object->movementAnimationDistance = presentation.animationDistance;
    }
    for (const auto& [id, pose] : stopped)
        restoreRuntimeNavalDockCommandStopPose(*findMutableObjectById(state, id), pose);
    for (const auto& pose : stoppedMoves)
        restoreRuntimeStoppedMovePose(*findMutableObjectById(state, pose.objectId), pose);
    return true;
}
