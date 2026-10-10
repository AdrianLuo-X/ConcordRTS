std::string serializeRuntimeBuildingCollapse(const RuntimeBuildingCollapseState& collapse) {
    return std::to_string(collapse.objectId) + '|' + runtimeSaveField(collapse.animId) + '|'
        + std::to_string(collapse.startTick) + '|' + std::to_string(collapse.frameCount) + '|'
        + std::to_string(collapse.rate);
}

void writeRuntimeBuildingCollapses(const RuntimeWindowState& state, std::ostream& output) {
    output << "\n[BuildingCollapses]\nSchema=1\nCount=" << state.buildingCollapses.size() << '\n';
    for (std::size_t i = 0; i < state.buildingCollapses.size(); ++i)
        output << i << '=' << serializeRuntimeBuildingCollapse(state.buildingCollapses[i]) << '\n';
}

bool restoreRuntimeBuildingCollapses(RuntimeWindowState& state, const IniFile& ini, std::string& error) {
    std::vector<RuntimeBuildingCollapseState> restored;
    if (!ini.getSection("BuildingCollapses")) { state.buildingCollapses.clear(); return true; }
    int schema = 0, count = 0;
    const auto fail = [&] { error = "invalid building collapse state"; return false; };
    if (!parseRuntimeInt(ini.getValue("BuildingCollapses", "Schema", ""), schema) || schema != 1
        || !parseRuntimeInt(ini.getValue("BuildingCollapses", "Count", ""), count) || count < 0 || count > 100000) return fail();
    std::unordered_set<int> ids;
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue("BuildingCollapses", std::to_string(i), ""));
        RuntimeBuildingCollapseState collapse;
        if (fields.size() != 5 || !parseRuntimeInt(fields[0], collapse.objectId)
            || collapse.objectId < 0 || !ids.insert(collapse.objectId).second || fields[1].empty()
            || !parseRuntimeInt(fields[2], collapse.startTick) || collapse.startTick < 0 || collapse.startTick > state.tick
            || !parseRuntimeInt(fields[3], collapse.frameCount) || collapse.frameCount <= 0 || collapse.frameCount > 100000
            || !parseRuntimeInt(fields[4], collapse.rate) || collapse.rate <= 0 || collapse.rate > 100000) return fail();
        collapse.animId = fields[1];
        const auto* object = findRuntimeObjectByIdIncludingInactive(state, collapse.objectId);
        if (!object || object->alive || object->kind != WorldObjectKind::Structure
            || runtimeBuildingCollapseFrame(collapse, state.tick, state.config.fixedTickHz) >= collapse.frameCount) return fail();
        restored.push_back(std::move(collapse));
    }
    state.buildingCollapses = std::move(restored); return true;
}

void hashRuntimeBuildingCollapses(std::uint64_t& hash, const RuntimeWindowState& state) {
    runtimeStableHashMixString(hash, "BuildingCollapses/1");
    runtimeStableHashMix(hash, static_cast<long long>(state.buildingCollapses.size()));
    for (const auto& collapse : state.buildingCollapses)
        runtimeStableHashMixString(hash, serializeRuntimeBuildingCollapse(collapse));
}
