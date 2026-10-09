void writeRuntimePublicBuildingLocations(const RuntimeWindowState& state, std::ostream& output) {
    output << "\n[PublicBuildingLocations]\nSchema=1\nCount="
        << state.triggers.publicBuildingLocations.size() << '\n';
    int index = 0;
    for (const auto& [id, location] : state.triggers.publicBuildingLocations)
        output << index++ << '=' << id << '|' << runtimeSaveField(location.owner) << '|'
            << location.cell.x << '|' << location.cell.y << '|' << location.noticeTick << '\n';
}

bool restoreRuntimePublicBuildingLocations(RuntimeWindowState& state,
    const IniFile& ini, std::string& error) {
    std::map<int, RuntimePublicBuildingLocation> restored;
    if (ini.getSection("PublicBuildingLocations") == nullptr) {
        // An older save already explored its public buildings. Recover their
        // owners quietly instead of replaying a construction alert on loading.
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive || object.health <= 0 || object.rubble
                || object.kind != WorldObjectKind::Structure || object.buildAnimTicksRemaining > 0) continue;
            const auto* building = state.rules.findBuildingType(object.type);
            if (building && building->revealToAll)
                restored[object.instanceId] = {runtimeCanonicalHumanHouseOwner(object.owner), {object.x, object.y}, -1};
        }
    } else {
        int schema = 0, count = 0;
        if (!parseRuntimeInt(ini.getValue("PublicBuildingLocations", "Schema", ""), schema) || schema != 1
            || !parseRuntimeInt(ini.getValue("PublicBuildingLocations", "Count", ""), count)
            || count < 0 || count > 100000) { error = "invalid public building location header"; return false; }
        for (int index = 0; index < count; ++index) {
            const auto fields = splitRuntimeSaveFields(ini.getValue("PublicBuildingLocations", std::to_string(index), ""));
            int id = -1; RuntimePublicBuildingLocation location;
            if (fields.size() != 5 || !parseRuntimeInt(fields[0], id) || id < 0
                || fields[1].empty() || !parseRuntimeInt(fields[2], location.cell.x)
                || !parseRuntimeInt(fields[3], location.cell.y)
                || !parseRuntimeInt(fields[4], location.noticeTick) || location.noticeTick < -1
                || location.noticeTick > state.tick || location.cell.x < 0 || location.cell.x >= 512
                || location.cell.y < 0 || location.cell.y >= 512 || restored.contains(id)) {
                error = "invalid public building location record"; return false;
            }
            location.owner = runtimeCanonicalHumanHouseOwner(fields[1]);
            restored.emplace(id, std::move(location));
        }
    }
    state.triggers.publicBuildingLocations = std::move(restored);
    return true;
}

void hashRuntimePublicBuildingLocations(std::uint64_t& hash, const RuntimeWindowState& state) {
    runtimeStableHashMixString(hash, "PublicBuildingLocations/1");
    runtimeStableHashMix(hash, static_cast<long long>(state.triggers.publicBuildingLocations.size()));
    for (const auto& [id, location] : state.triggers.publicBuildingLocations) {
        runtimeStableHashMix(hash, id);
        runtimeStableHashMixString(hash, location.owner);
        runtimeStableHashMix(hash, location.cell.x);
        runtimeStableHashMix(hash, location.cell.y);
        runtimeStableHashMix(hash, location.noticeTick);
    }
}
