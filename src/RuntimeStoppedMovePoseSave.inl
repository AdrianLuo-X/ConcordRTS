struct RuntimeStoppedMovePoseRecord {
    int objectId = -1;
    std::array<double, 3> position{};
    bool onBridge = false;
    double animationDistance = 0.0;
};

void writeRuntimeStoppedMovePoses(const RuntimeWindowState& state, std::ostream& output) {
    std::vector<const WorldObjectInstance*> objects;
    for (const auto& object : state.world.getObjects()) {
        if (object.alive && worldObjectHasStoppedGroundPose(object)
            && !runtimeObjectUsesNavalDockPresentation(state, object)) objects.push_back(&object);
    }
    std::sort(objects.begin(), objects.end(), [](const auto* left, const auto* right) {
        return left->instanceId < right->instanceId;
    });
    output << "\n[StoppedMovePoses]\nSchema=1\nCount=" << objects.size() << '\n';
    const auto precision = output.precision();
    output << std::setprecision(17);
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const auto& object = *objects[i];
        output << i << '=' << object.instanceId << '|' << object.renderX << '|' << object.renderY
            << '|' << object.renderLevel << '|' << runtimeBoolText(object.renderOnBridge)
            << '|' << object.movementAnimationDistance << '\n';
    }
    output.precision(precision);
}

bool parseRuntimeStoppedMovePoses(const RuntimeWindowState& state, const IniFile& ini,
    std::vector<RuntimeStoppedMovePoseRecord>& pending, std::unordered_set<int>& seen, std::string& error) {
    constexpr const char* section = "StoppedMovePoses";
    if (!ini.getSection(section)) return true;
    const auto fail = [&]() { error = "invalid stopped move pose"; return false; };
    int schema = 0, count = -1;
    if (!parseRuntimeInt(ini.getValue(section, "Schema", ""), schema) || schema != 1
        || !parseRuntimeInt(ini.getValue(section, "Count", ""), count) || count < 0
        || static_cast<std::size_t>(count) > state.world.getObjects().size()) return fail();
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue(section, std::to_string(i), ""));
        RuntimeStoppedMovePoseRecord pose;
        if (fields.size() != 6 || !parseRuntimeInt(fields[0], pose.objectId)
            || !seen.insert(pose.objectId).second
            || !parseRuntimeFiniteDouble(fields[1], pose.position[0])
            || !parseRuntimeFiniteDouble(fields[2], pose.position[1])
            || !parseRuntimeFiniteDouble(fields[3], pose.position[2])
            || !parseRuntimeStrictBool(fields[4], pose.onBridge)
            || !parseRuntimeFiniteDouble(fields[5], pose.animationDistance)
            || pose.position[0] < -0.5 || pose.position[0] > 512.5
            || pose.position[1] < -0.5 || pose.position[1] > 512.5
            || pose.position[2] < -128.0 || pose.position[2] > 512.0
            || pose.animationDistance < 0.0 || pose.animationDistance >= 1024.0) return fail();
        const auto* object = findObjectById(state, pose.objectId);
        const auto behavior = state.behaviors.find(pose.objectId);
        if (!object || !object->alive || object->airborne
            || (object->kind != WorldObjectKind::Unit && object->kind != WorldObjectKind::Infantry)
            || object->dockedToObjectId >= 0 || object->tankBunkerHostId >= 0
            || object->factoryExitHostId >= 0 || object->garrisonHostObjectId >= 0
            || std::abs(pose.position[0] - object->x) >= 2.0
            || std::abs(pose.position[1] - object->y) >= 2.0
            || (behavior != state.behaviors.end() && behavior->second.moveCooldownTicks > 0)) return fail();
        pending.push_back(pose);
    }
    return true;
}

void restoreRuntimeStoppedMovePose(WorldObjectInstance& object, const RuntimeStoppedMovePoseRecord& pose) {
    object.hasRenderPosition = true;
    object.renderMoveActive = false;
    object.renderX = pose.position[0]; object.renderY = pose.position[1];
    object.renderLevel = pose.position[2]; object.renderOnBridge = pose.onBridge;
    object.movementAnimationDistance = pose.animationDistance;
}
