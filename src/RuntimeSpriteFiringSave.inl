// Optional presentation clock: old saves have no action in progress. This
// section also travels through canonical multiplayer snapshots and replays.
void writeRuntimeSpriteFirings(const RuntimeWindowState& state, std::ostream& output) {
    std::vector<const WorldObjectInstance*> active;
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || object.health <= 0 || object.spriteFiringStartTick < 0) continue;
        const auto* art = runtimeArtTypeForObject(state, object);
        if (art && art->firingFrames > 0 && runtimeRuleFrameAtTick(
                state.tick - object.spriteFiringStartTick, state.config.fixedTickHz) < 2LL * art->firingFrames)
            active.push_back(&object);
    }
    std::sort(active.begin(), active.end(), [](const auto* a, const auto* b) {
        return a->instanceId < b->instanceId;
    });
    output << "\n[SpriteFiringPresentation]\nSchema=1\nCount=" << active.size() << '\n';
    for (std::size_t i = 0; i < active.size(); ++i)
        output << i << '=' << active[i]->instanceId << '|' << active[i]->spriteFiringStartTick << '\n';
}

bool loadRuntimeSpriteFirings(RuntimeWindowState& state, const IniFile& ini, std::string& error) {
    constexpr const char* section = "SpriteFiringPresentation";
    if (ini.getSection(section) == nullptr) return true;
    const auto fail = [&]() { error = "invalid sprite firing presentation"; return false; };
    int schema = 0, count = -1;
    if (!parseRuntimeInt(ini.getValue(section, "Schema", ""), schema) || schema != 1
        || !parseRuntimeInt(ini.getValue(section, "Count", ""), count) || count < 0
        || static_cast<std::size_t>(count) > state.world.getObjects().size()) return fail();
    std::vector<std::pair<WorldObjectInstance*, int>> pending;
    std::unordered_set<int> seen;
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue(section, std::to_string(i), ""));
        int id = -1, start = -1;
        if (fields.size() != 2 || !parseRuntimeInt(fields[0], id) || !seen.insert(id).second
            || !parseRuntimeInt(fields[1], start) || start < 0 || start > state.tick) return fail();
        auto* object = findMutableObjectById(state, id);
        if (!object || !object->alive || object->health <= 0 || object->kind != WorldObjectKind::Unit)
            return fail();
        const auto* art = runtimeArtTypeForObject(state, *object);
        if (!art || art->voxel || art->firingFrames <= 0 || runtimeRuleFrameAtTick(
                state.tick - start, state.config.fixedTickHz) >= 2LL * art->firingFrames) return fail();
        pending.emplace_back(object, start);
    }
    for (auto& object : state.world.accessObjects()) object.spriteFiringStartTick = -1;
    for (const auto& [object, start] : pending) object->spriteFiringStartTick = start;
    return true;
}
