// Optional presentation state for an infantry step. Logical cells and movement
// cooldowns already live in the ordinary object/behavior records. Keeping the
// interpolation lets a garrison doorway continue visibly after loading.
bool runtimeGroundAttackWalkPresentation(const RuntimeWindowState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& walk) {
    return (object.kind == WorldObjectKind::Infantry
            || (object.kind == WorldObjectKind::Unit && !object.authoredAircraft))
        && !object.airborne && findRuntimeUnitType(state.rules, object) != nullptr
        && runtimeObjectMovementMode(state, object) != PathMovementMode::Air
        && walk.kind == RuntimeBehaviorKind::Attack && walk.moveCooldownTicks > 0
        && walk.moveSegmentFrom.x >= 0 && walk.moveSegmentFrom.x < 512
        && walk.moveSegmentFrom.y >= 0 && walk.moveSegmentFrom.y < 512;
}

void writeRuntimeInfantryWalks(const RuntimeWindowState& state, std::ostream& output,
    bool includeMobileUnits = false) {
    std::vector<int> ids;
    for (const auto& [id, walk] : state.behaviors) {
        const auto* unit = findObjectById(state, id);
        if (unit != nullptr && unit->alive
            && (unit->kind == WorldObjectKind::Infantry
                || (includeMobileUnits && unit->kind == WorldObjectKind::Unit && !unit->authoredAircraft))
            && !unit->airborne && unit->hasRenderPosition && unit->renderMoveActive
            && (walk.kind == RuntimeBehaviorKind::Move
                || (runtimeGroundAttackWalkPresentation(state, *unit, walk)
                    && walk.moveSegmentFrom == walk.renderFrom
                    && runtimeCellPointForObject(*unit) == walk.renderTo))
            && walk.renderMoveTicksRemaining > 0
            && walk.renderMoveTicksRemaining == walk.moveCooldownTicks
            && walk.renderMoveTicksTotal >= walk.renderMoveTicksRemaining
            && walk.renderFrom != walk.renderTo
            && std::abs(walk.renderTo.x - walk.renderFrom.x) <= 1
            && std::abs(walk.renderTo.y - walk.renderFrom.y) <= 1)
            ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    const bool offsets = std::any_of(ids.begin(), ids.end(), [&](int id) {
        const auto& walk = state.behaviors.at(id);
        return walk.renderFromOffsetX != 0.0 || walk.renderFromOffsetY != 0.0
            || walk.renderToOffsetX != 0.0 || walk.renderToOffsetY != 0.0;
    });
    output << (includeMobileUnits ? "\n[MultiplayerMovementSteps]\n" : "\n[InfantryWalkPresentation]\n")
        << "Schema=" << (offsets ? 2 : 1) << "\nCount=" << ids.size() << '\n';
    const auto precision = output.precision();
    output << std::setprecision(17);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto& walk = state.behaviors.at(ids[i]);
        const auto* unit = findObjectById(state, ids[i]);
        output << i << '=' << ids[i] << '|' << walk.renderFrom.x << '|' << walk.renderFrom.y
            << '|' << walk.renderTo.x << '|' << walk.renderTo.y
            << '|' << walk.renderFromLevel << '|' << walk.renderToLevel
            << '|' << runtimeBoolText(walk.renderFromOnBridge) << '|' << runtimeBoolText(walk.renderToOnBridge)
            << '|' << walk.renderMoveTicksTotal << '|' << walk.renderMoveTicksRemaining
            << '|' << unit->movementAnimationDistance;
        if (offsets) output << '|' << walk.renderFromOffsetX << '|' << walk.renderFromOffsetY
            << '|' << walk.renderToOffsetX << '|' << walk.renderToOffsetY;
        output << '\n';
    }
    output.precision(precision);
}

bool loadRuntimeInfantryWalks(RuntimeWindowState& state, const IniFile& ini, std::string& error,
    bool includeMobileUnits = false) {
    const char* section = includeMobileUnits ? "MultiplayerMovementSteps" : "InfantryWalkPresentation";
    if (ini.getSection(section) == nullptr) return true; // Older saves have no interpolation.
    const auto fail = [&]() { error = "invalid infantry walk presentation"; return false; };
    int schema = 0, count = 0;
    if (!parseRuntimeInt(ini.getValue(section, "Schema", ""), schema) || (schema != 1 && schema != 2)
        || !parseRuntimeInt(ini.getValue(section, "Count", ""), count) || count < 0
        || static_cast<std::size_t>(count) > state.world.getObjects().size()) return fail();
    std::unordered_set<int> seen;
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue(section, std::to_string(i), ""));
        int id = -1;
        RuntimeCellPoint from, to;
        double fromLevel = 0.0, toLevel = 0.0, distance = 0.0;
        bool fromBridge = false, toBridge = false;
        int total = 0, remaining = 0;
        std::array<double, 4> offsets{};
        if (fields.size() != (schema == 1 ? 12U : 16U) || !parseRuntimeInt(fields[0], id) || !seen.insert(id).second
            || !parseRuntimeInt(fields[1], from.x) || !parseRuntimeInt(fields[2], from.y)
            || !parseRuntimeInt(fields[3], to.x) || !parseRuntimeInt(fields[4], to.y)
            || !parseRuntimeFiniteDouble(fields[5], fromLevel) || !parseRuntimeFiniteDouble(fields[6], toLevel)
            || !parseRuntimeStrictBool(fields[7], fromBridge) || !parseRuntimeStrictBool(fields[8], toBridge)
            || !parseRuntimeInt(fields[9], total) || !parseRuntimeInt(fields[10], remaining)
            || !parseRuntimeFiniteDouble(fields[11], distance)
            || from.x < 0 || from.y < 0 || to.x < 0 || to.y < 0
            || from.x >= 512 || from.y >= 512 || to.x >= 512 || to.y >= 512
            || from == to || std::abs(to.x - from.x) > 1 || std::abs(to.y - from.y) > 1
            || total <= 0 || total > 100000 || remaining <= 0 || remaining > total
            || distance < 0.0 || distance >= 1024.0) return fail();
        if (schema == 2) {
            for (std::size_t n = 0; n < offsets.size(); ++n)
                if (!parseRuntimeFiniteDouble(fields[12+n], offsets[n]) || std::abs(offsets[n]) > 16.0)
                    return fail();
        }
        auto* unit = findMutableObjectById(state, id);
        auto behavior = state.behaviors.find(id);
        if (unit == nullptr || !unit->alive || unit->airborne
            || (unit->kind != WorldObjectKind::Infantry
                && !(includeMobileUnits && unit->kind == WorldObjectKind::Unit && !unit->authoredAircraft))
            || behavior == state.behaviors.end()
            || (behavior->second.kind != RuntimeBehaviorKind::Move
                && !(runtimeGroundAttackWalkPresentation(state, *unit, behavior->second)
                    && behavior->second.moveSegmentFrom == from))
            || behavior->second.moveCooldownTicks != remaining) return fail();
        if (behavior->second.transportTransferKind == RuntimeTransportTransferKind::Boarding) {
            if (!runtimeTransportSavedBoardingEndpointAllowed(state, *unit, from, to, toLevel, toBridge)) return fail();
        } else if (runtimeCellPointForObject(*unit) != to
            || std::abs(toLevel - unit->level) > 0.000001 || toBridge != unit->onBridge) return fail();
        auto& walk = behavior->second;
        walk.renderFrom = from; walk.renderTo = to;
        walk.renderFromLevel = fromLevel; walk.renderToLevel = toLevel;
        walk.renderFromOnBridge = fromBridge; walk.renderToOnBridge = toBridge;
        walk.renderMoveTicksTotal = total; walk.renderMoveTicksRemaining = remaining;
        walk.renderFromOffsetX = offsets[0]; walk.renderFromOffsetY = offsets[1];
        walk.renderToOffsetX = offsets[2]; walk.renderToOffsetY = offsets[3];
        const double progress = 1.0 - static_cast<double>(remaining) / total;
        unit->hasRenderPosition = unit->renderMoveActive = true;
        const auto point = runtimeGroundRenderMovePoint(*unit, walk, progress);
        unit->renderX = point[0]; unit->renderY = point[1];
        unit->renderLevel = fromLevel + (toLevel - fromLevel) * progress;
        unit->renderOnBridge = progress < 0.5 ? fromBridge : toBridge;
        unit->movementAnimationDistance = distance;
    }
    return true;
}
