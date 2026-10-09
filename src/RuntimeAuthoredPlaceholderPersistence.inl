bool runtimeSavedAuthoredInactivePlaceholder(const RuntimeWindowState& context,
    const WorldObjectInstance& object) {
    // C1A02MD contains a zero-health CALA02 whose stock rules section was
    // commented out by Westwood. Keep that exact inert map record on restore;
    // it cannot create a unit, revive the placeholder or acquire new rules.
    if (object.kind != WorldObjectKind::Structure || object.authoredAircraft
        || object.alive || object.health != 0 || object.mapId.empty()) return false;
    IniFile mapIni;
    MapDatabase map;
    if (!mapIni.loadFromFile(context.config.defaultMapPath) || !map.loadFromIni(mapIni)) return false;
    return std::any_of(map.getStructures().begin(), map.getStructures().end(),
        [&](const auto& authored) {
            return authored.id == object.mapId && authored.type == object.type && authored.health == 0
                && authored.owner == object.owner
                && authored.x == object.x && authored.y == object.y;
        });
}
