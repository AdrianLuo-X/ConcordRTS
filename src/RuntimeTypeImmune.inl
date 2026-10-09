// TechnoClass::ReceiveDamage 0x701900: healing and IgnoreDefenses bypass
// TypeImmune; ordinary damage compares the real source type and House pointer.
const WorldObjectInstance* runtimeTypeImmuneSource(
    const RuntimeWindowState& state, int objectId) {
    const auto* source = findRuntimeObjectByIdIncludingInactive(state, objectId);
    if (!source || source->health <= 0) return nullptr;
    if (source->alive || runtimeCombatSourceAvailable(state.rules,
            state.world.getObjects(), *source)) return source;
    if (source->garrisonHostObjectId >= 0) {
        const auto* host = findObjectById(state, source->garrisonHostObjectId);
        if (host && std::find(host->garrisonOccupantIds.begin(),
                host->garrisonOccupantIds.end(), objectId) != host->garrisonOccupantIds.end())
            return source;
    }
    if (source->mission == "ParasiteLimbo") {
        for (const auto& [hostId, link] : state.specialMechanisms.parasiteLinks())
            if (link.sourceObjectId == objectId && findObjectById(state, hostId)) return source;
    }
    if (state.specialMechanisms.isCarrierChildDocked(objectId)) return source;
    return nullptr;
}

std::string runtimeTypeImmuneHouse(const RuntimeWindowState& state, const std::string& owner) {
    std::string key = toLowerAsciiLocal(trimAsciiLocal(owner));
    if (state.campaignMode) {
        const auto alias = state.triggers.campaignCanonicalHouseByAlias.find(key);
        if (alias != state.triggers.campaignCanonicalHouseByAlias.end())
            key = toLowerAsciiLocal(trimAsciiLocal(alias->second));
    }
    return key;
}

bool runtimeDamageIsTypeImmune(const RuntimeWindowState& state,
    const WorldObjectInstance& target, const WorldObjectInstance* source,
    int authoredDamage, bool ignoreDefenses = false) {
    if (ignoreDefenses || authoredDamage < 0 || !source || source->kind != target.kind
        || runtimeTypeImmuneHouse(state, source->owner).empty()
        || runtimeTypeImmuneHouse(state, source->owner)
            != runtimeTypeImmuneHouse(state, target.owner)) return false;
    if (target.kind == WorldObjectKind::Structure) {
        const auto* type = findRuntimeBuildingTypeById(state.rules, target.type);
        return type && type->typeImmune
            && type == findRuntimeBuildingTypeById(state.rules, source->type);
    }
    const auto* type = findRuntimeUnitType(state.rules, target);
    return type && type->typeImmune && type == findRuntimeUnitType(state.rules, *source);
}
