std::string runtimeMultiplayerSnapshotBehaviorRecord(int objectId, const RuntimeObjectBehavior& behavior);
bool parseRuntimeMultiplayerSnapshotBehaviorRecord(const std::string& value,
    int& objectId, RuntimeObjectBehavior& behavior);

struct RuntimeCarrierFlightPoseRecord {
    int objectId = -1;
    double x = 0.0, y = 0.0, level = 0.0, animationDistance = 0.0;
    bool moving = false;
    bool hasRenderPosition = true;
    std::optional<RuntimeObjectBehavior> order;
};

void writeRuntimeCarrierFlightPoses(const RuntimeWindowState& state, std::ostream& output) {
    std::vector<const WorldObjectInstance*> children;
    for (const auto& [id, sortie] : state.specialMechanisms.carrierChildren()) {
        const auto* child = findObjectById(state, id);
        if (child && child->alive && child->airborne
            && sortie.phase != CarrierChildPhase::Docked
            && sortie.phase != CarrierChildPhase::Destroyed
            && !state.rules.findSpawnedMissileProfile(child->type)) children.push_back(child);
    }
    if (children.empty()) return;
    output << "\n[CarrierFlightPoses]\nSchema=2\nCount=" << children.size() << '\n';
    const auto precision = output.precision();
    output << std::setprecision(17);
    for (std::size_t i = 0; i < children.size(); ++i) {
        const auto& child = *children[i];
        output << i << '=' << child.instanceId << '|' << runtimeObjectRenderX(child) << '|' << runtimeObjectRenderY(child)
            << '|' << runtimeObjectRenderLevel(child) << '|' << runtimeBoolText(child.renderMoveActive)
            << '|' << child.movementAnimationDistance << '|' << runtimeBoolText(child.hasRenderPosition) << '\n';
        const auto order = state.behaviors.find(child.instanceId);
        if (order != state.behaviors.end() && order->second.kind == RuntimeBehaviorKind::Attack) {
            const auto& move = order->second;
            output << "Order" << i << '=' << iniHexEncode(
                runtimeMultiplayerSnapshotBehaviorRecord(child.instanceId, move)) << '\n';
            if (move.renderMoveTicksTotal > 0) {
                output << "Segment" << i << '=' << move.renderFrom.x << '|' << move.renderFrom.y
                    << '|' << move.renderTo.x << '|' << move.renderTo.y
                    << '|' << move.renderFromLevel << '|' << move.renderToLevel
                    << '|' << move.renderMoveTicksTotal << '|' << move.renderMoveTicksRemaining
                    << '|' << move.renderFromOffsetX << '|' << move.renderFromOffsetY
                    << '|' << move.renderToOffsetX << '|' << move.renderToOffsetY << '\n';
            }
        }
    }
    output.precision(precision);
}

bool validateRuntimeCarrierAircraftCellOrders(const RuntimeWindowState& state, std::string& error) {
    const auto fail = [&]() { error = "invalid carrier aircraft cell target"; return false; };
    // A synthetic target must remain bound to the existing terrain attack
    // order, rather than admitting arbitrary negative object references.
    for (const auto& [id, sortie] : state.specialMechanisms.carrierChildren()) {
        if (sortie.targetObjectId >= -1) continue;
        SpecialCell cell;
        bool bridge = false;
        const auto* child = findObjectById(state, id);
        const auto* unit = child ? findRuntimeUnitType(state.rules, *child) : nullptr;
        if (!decodeRuntimeCarrierCellTarget(sortie.targetObjectId, cell, bridge)
            || !unit || !unit->spawned || !runtimeUnitUsesAirMovement(*unit)
            || unit->missileSpawn || state.rules.findSpawnedMissileProfile(child->type)
            || !state.terrainByCell.contains(runtimeCellKey(cell.x, cell.y))) return fail();
        if (sortie.phase == CarrierChildPhase::Launching
            || sortie.phase == CarrierChildPhase::Attacking) {
            const auto order = state.behaviors.find(id);
            if (order == state.behaviors.end() || order->second.kind != RuntimeBehaviorKind::Attack
                || order->second.targetObjectId != sortie.targetObjectId
                || order->second.attackBridge != bridge
                || order->second.attackGround == bridge
                || (!bridge && !order->second.forceFire)) return fail();
            if (bridge) {
                // A clicked bridge lane resolves to the span's damage anchor;
                // the order retains the requested lane, not that anchor.
                const auto target = runtimeBridgeAttackTargetObject(state, order->second.attackBridgeCell);
                if (!target || target->instanceId != sortie.targetObjectId) return fail();
            } else if (order->second.attackBridgeCell.x != cell.x
                || order->second.attackBridgeCell.y != cell.y) return fail();
        }
    }
    return true;
}

bool parseRuntimeCarrierFlightPoses(const RuntimeWindowState& state, const IniFile& ini,
    std::vector<RuntimeCarrierFlightPoseRecord>& poses, std::string& error) {
    constexpr const char* section = "CarrierFlightPoses";
    const auto fail = [&]() { error = "invalid carrier aircraft flight pose"; return false; };
    if (!validateRuntimeCarrierAircraftCellOrders(state, error)) return false;
    if (!ini.getSection(section)) return true;
    int schema = 0, count = -1;
    if (!parseRuntimeInt(ini.getValue(section, "Schema", ""), schema) || (schema != 1 && schema != 2)
        || !parseRuntimeInt(ini.getValue(section, "Count", ""), count) || count < 0
        || static_cast<std::size_t>(count) > state.specialMechanisms.carrierChildren().size()) return fail();
    std::unordered_set<int> seen;
    std::vector<RuntimeCarrierFlightPoseRecord> staged;
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue(section, std::to_string(i), ""));
        RuntimeCarrierFlightPoseRecord pose;
        if (fields.size() != (schema == 1 ? 6U : 7U) || !parseRuntimeInt(fields[0], pose.objectId)
            || !seen.insert(pose.objectId).second
            || !parseRuntimeFiniteDouble(fields[1], pose.x)
            || !parseRuntimeFiniteDouble(fields[2], pose.y)
            || !parseRuntimeFiniteDouble(fields[3], pose.level)
            || !parseRuntimeStrictBool(fields[4], pose.moving)
            || !parseRuntimeFiniteDouble(fields[5], pose.animationDistance)
            || (schema == 2 && !parseRuntimeStrictBool(fields[6], pose.hasRenderPosition))
            || (pose.moving && !pose.hasRenderPosition)
            || pose.x < -0.5 || pose.x > 512.5 || pose.y < -0.5 || pose.y > 512.5
            || pose.level < -128.0 || pose.level > 512.0 || pose.animationDistance < 0.0) return fail();
        const auto* child = findObjectById(state, pose.objectId);
        const auto sortie = state.specialMechanisms.carrierChildren().find(pose.objectId);
        const auto* unit = child ? findRuntimeUnitType(state.rules, *child) : nullptr;
        if (!child || !child->alive || !child->airborne || !unit || !unit->spawned
            || !runtimeUnitUsesAirMovement(*unit) || unit->missileSpawn
            || state.rules.findSpawnedMissileProfile(child->type)
            || child->dockedToObjectId >= 0 || child->dockIndex >= 0
            || sortie == state.specialMechanisms.carrierChildren().end()
            || sortie->second.phase == CarrierChildPhase::Docked
            || sortie->second.phase == CarrierChildPhase::Destroyed
            || std::abs(pose.x - child->x) >= 2.0
            || std::abs(pose.y - child->y) >= 2.0) return fail();
        const auto savedOrder = ini.getValue(section, "Order" + std::to_string(i), "");
        if (!savedOrder.empty()) {
            std::string text;
            int orderId = -1;
            RuntimeObjectBehavior order;
            const auto ordinary = state.behaviors.find(pose.objectId);
            if (!iniHexDecode(savedOrder, text, 1024U * 1024U)
                || !parseRuntimeMultiplayerSnapshotBehaviorRecord(text, orderId, order)
                || orderId != pose.objectId || order.kind != RuntimeBehaviorKind::Attack
                || ordinary == state.behaviors.end() || ordinary->second.kind != order.kind
                || ordinary->second.targetObjectId != order.targetObjectId
                || ordinary->second.forceFire != order.forceFire
                || ordinary->second.attackGround != order.attackGround
                || ordinary->second.attackBridge != order.attackBridge
                || ordinary->second.attackBridgeCell != order.attackBridgeCell
                || ordinary->second.moveCooldownTicks != order.moveCooldownTicks
                || order.moveCooldownTicks < 0 || order.moveCooldownTicks > 100000
                || order.weaponCooldownTicks < 0 || order.weaponCooldownTicks > 100000
                || order.moveIntervalRemainderQ16 < -0x8000 || order.moveIntervalRemainderQ16 > 0x7fff
                || order.blockedTicks < 0 || order.pathReplans < 0 || order.nextRepathTick < 0
                || !std::isfinite(order.pendingRangeCells) || order.pendingRangeCells < 0.0
                || order.pendingRangeCells > 10000.0) return fail();
            for (const auto& cell : order.path)
                if (cell.x < 0 || cell.x > 999 || cell.y < 0 || cell.y > 999) return fail();
            const auto segment = ini.getValue(section, "Segment" + std::to_string(i), "");
            if (!segment.empty()) {
                const auto fields = splitRuntimeSaveFields(segment);
                if (fields.size() != (schema == 1 ? 8U : 12U) || !parseRuntimeInt(fields[0], order.renderFrom.x)
                    || !parseRuntimeInt(fields[1], order.renderFrom.y)
                    || !parseRuntimeInt(fields[2], order.renderTo.x)
                    || !parseRuntimeInt(fields[3], order.renderTo.y)
                    || !parseRuntimeFiniteDouble(fields[4], order.renderFromLevel)
                    || !parseRuntimeFiniteDouble(fields[5], order.renderToLevel)
                    || !parseRuntimeInt(fields[6], order.renderMoveTicksTotal)
                    || !parseRuntimeInt(fields[7], order.renderMoveTicksRemaining)
                    || order.renderFrom.x < 0 || order.renderFrom.x > 999
                    || order.renderFrom.y < 0 || order.renderFrom.y > 999
                    || order.renderTo.x != child->x || order.renderTo.y != child->y
                    || std::abs(order.renderTo.x - order.renderFrom.x) > 1
                    || std::abs(order.renderTo.y - order.renderFrom.y) > 1
                    || order.renderFrom == order.renderTo || !pose.moving
                    || order.renderFromLevel < -128.0 || order.renderFromLevel > 512.0
                    || order.renderToLevel != child->level
                    || order.renderMoveTicksTotal <= 0 || order.renderMoveTicksTotal > 100000
                    || order.renderMoveTicksRemaining <= 0
                    || order.renderMoveTicksRemaining > order.renderMoveTicksTotal
                    || order.renderMoveTicksRemaining != order.moveCooldownTicks) return fail();
                if (schema == 2 && (!parseRuntimeFiniteDouble(fields[8], order.renderFromOffsetX)
                    || !parseRuntimeFiniteDouble(fields[9], order.renderFromOffsetY)
                    || !parseRuntimeFiniteDouble(fields[10], order.renderToOffsetX)
                    || !parseRuntimeFiniteDouble(fields[11], order.renderToOffsetY)
                    || std::abs(order.renderFromOffsetX) >= 2.0 || std::abs(order.renderFromOffsetY) >= 2.0
                    || order.renderToOffsetX != 0.0 || order.renderToOffsetY != 0.0)) return fail();
            } else if (order.moveCooldownTicks > 0) return fail();
            pose.order = std::move(order);
        }
        staged.push_back(pose);
    }
    poses = std::move(staged);
    return true;
}

void restoreRuntimeCarrierFlightPoses(RuntimeWindowState& state,
    const std::vector<RuntimeCarrierFlightPoseRecord>& poses) {
    for (const auto& pose : poses) {
        if (auto* child = findMutableObjectById(state, pose.objectId)) {
            if (pose.order) {
                state.behaviors[pose.objectId] = *pose.order;
                if (pose.order->pathRequestPending
                    && std::find(state.pendingPathObjectIds.begin(), state.pendingPathObjectIds.end(),
                        pose.objectId) == state.pendingPathObjectIds.end())
                    state.pendingPathObjectIds.push_back(pose.objectId);
            }
            child->hasRenderPosition = pose.hasRenderPosition; child->renderMoveActive = pose.moving;
            child->renderX = pose.x; child->renderY = pose.y; child->renderLevel = pose.level;
            child->renderOnBridge = false;
            child->movementAnimationDistance = pose.animationDistance;
            applyRuntimeCarrierDeckFlightPresentation(state, *child);
        }
    }
}

void restoreRuntimeCarrierFlightSegments(RuntimeWindowState& state,
    const std::vector<RuntimeCarrierFlightPoseRecord>& poses) {
    for (const auto& pose : poses) {
        if (auto* child = findMutableObjectById(state, pose.objectId))
            applyRuntimeCarrierDeckFlightPresentation(state, *child);
        const auto order = state.behaviors.find(pose.objectId);
        if (!pose.order || order == state.behaviors.end()) continue;
        auto& move = order->second;
        move.renderFrom = pose.order->renderFrom; move.renderTo = pose.order->renderTo;
        move.renderFromLevel = pose.order->renderFromLevel; move.renderToLevel = pose.order->renderToLevel;
        move.renderMoveTicksTotal = pose.order->renderMoveTicksTotal;
        move.renderMoveTicksRemaining = pose.order->renderMoveTicksRemaining;
        move.renderFromOffsetX = pose.order->renderFromOffsetX;
        move.renderFromOffsetY = pose.order->renderFromOffsetY;
        move.renderToOffsetX = pose.order->renderToOffsetX;
        move.renderToOffsetY = pose.order->renderToOffsetY;
    }
}
