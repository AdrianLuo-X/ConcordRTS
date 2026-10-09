bool runtimeOwnerHasAlternateRefineryPrerequisite(
    const RuntimeWindowState& state, const std::string& owner) {
    const std::string canonicalOwner = runtimeRepairDepotCanonicalOwnerId(state, owner);
    for (const auto& alias : state.rules.getPrerequisiteAliases()) {
        if (toUpperAsciiLocal(alias.name) != "PROCALTERNATE") continue;
        for (const auto& type : splitRuntimeTokenList(alias.value)) {
            // PrerequisiteProcAlternate names UnitTypes (stock: SMIN), not a
            // permanent technology unlock or an alias for a refinery building.
            if (!state.rules.findUnitType("Vehicle", type)) continue;
            const std::string wanted = toUpperAsciiLocal(type);
            for (const auto& object : state.world.getObjects()) {
                if (object.alive && object.health > 0 && object.kind == WorldObjectKind::Unit
                    && toUpperAsciiLocal(object.type) == wanted
                    && runtimeRepairDepotCanonicalOwnerId(state, object.owner) == canonicalOwner) {
                    return true;
                }
            }
        }
    }
    return false;
}
