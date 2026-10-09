// A real map contains both authored objects and placed structure anchors.
// Reconstructing their poses from integer cells changes the saved authority,
// including active movement/aircraft poses consumed by targeting and vision.
void writeRuntimeMultiplayerObjectPoses(const RuntimeWindowState& state, std::ostream& output) {
    std::vector<const WorldObjectInstance*> objects;
    for (const auto& object : state.world.getObjects()) objects.push_back(&object);
    std::sort(objects.begin(), objects.end(), [](const auto* a, const auto* b) {
        return a->instanceId < b->instanceId;
    });
    output << "\n[MultiplayerObjectPoses]\nSchema=1\nCount=" << objects.size() << '\n';
    for (std::size_t index = 0; index < objects.size(); ++index) {
        const auto& object = *objects[index];
        output << index << '=' << object.instanceId << '|' << runtimeBoolText(object.hasRenderPosition)
            << '|' << runtimeMultiplayerSnapshotCanonicalDoubleBits(object.renderX)
            << '|' << runtimeMultiplayerSnapshotCanonicalDoubleBits(object.renderY)
            << '|' << runtimeBoolText(object.renderMoveActive)
            << '|' << runtimeMultiplayerSnapshotCanonicalDoubleBits(object.renderLevel)
            << '|' << runtimeBoolText(object.renderOnBridge) << '\n';
    }
}

bool restoreRuntimeMultiplayerObjectPoses(RuntimeWindowState& state, const IniFile& ini, std::string& error) {
    if (ini.getSection("MultiplayerObjectPoses") == nullptr) return true; // Earlier snapshot images.
    int schema = 0, count = -1;
    const auto fail = [&]() { error = "invalid multiplayer object poses"; return false; };
    if (!parseRuntimeInt(ini.getValue("MultiplayerObjectPoses", "Schema", ""), schema) || schema != 1
        || !parseRuntimeInt(ini.getValue("MultiplayerObjectPoses", "Count", ""), count)
        || count < 0 || static_cast<std::size_t>(count) != state.world.getObjects().size()) return fail();
    std::unordered_map<int, WorldObjectInstance*> remaining;
    for (auto& object : state.world.accessObjects()) remaining.emplace(object.instanceId, &object);
    for (int index = 0; index < count; ++index) {
        const auto fields = splitRuntimeSaveFields(ini.getValue("MultiplayerObjectPoses", std::to_string(index), ""));
        int id = -1; bool hasPose = false, moving = false, onBridge = false;
        double x = 0, y = 0, level = 0;
        if (fields.size() != 7 || !parseRuntimeInt(fields[0], id)
            || !parseRuntimeStrictBool(fields[1], hasPose)
            || !runtimeMultiplayerSnapshotDoubleFromBits(fields[2], x)
            || !runtimeMultiplayerSnapshotDoubleFromBits(fields[3], y)
            || !parseRuntimeStrictBool(fields[4], moving)
            || !runtimeMultiplayerSnapshotDoubleFromBits(fields[5], level)
            || !parseRuntimeStrictBool(fields[6], onBridge)
            || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(level)
            || (moving && !hasPose)) return fail();
        const auto found = remaining.find(id);
        if (found == remaining.end()) return fail();
        auto& object = *found->second;
        object.hasRenderPosition = hasPose; object.renderX = x; object.renderY = y;
        object.renderMoveActive = moving; object.renderLevel = level; object.renderOnBridge = onBridge;
        remaining.erase(found);
    }
    return remaining.empty();
}
