std::string runtimeCaptureEvaSection(const RuntimeWindowState& state, const BuildingType& type) {
    const std::string wanted = toLowerAsciiLocal(trimAsciiLocal(type.captureEvaEvent));
    if (!state.evaLoaded || wanted.empty()) return {};
    // Retail's INI reader resolves names against the allocated DialogList array.
    // Missing, unknown and explicit <none> names all resolve to the silent -1.
    const auto* list = state.evaIni.getSection("DialogList");
    if (!list) return {};
    for (const auto& [key, value] : *list) {
        (void)key;
        const std::string candidate = trimAsciiLocal(value);
        if (toLowerAsciiLocal(candidate) == wanted) return candidate;
    }
    return {};
}

void emitRuntimeEngineerCaptureEva(RuntimeWindowState& state, const BuildingType* type,
    const std::string& previousOwner, const std::string& captureOwner) {
    const auto identity = [&](const std::string& owner) {
        auto key = runtimeCanonicalHumanHouseOwner(owner);
        if (state.campaignMode) {
            const auto alias = state.triggers.campaignCanonicalHouseByAlias.find(key);
            if (alias != state.triggers.campaignCanonicalHouseByAlias.end()) key = runtimeCanonicalHumanHouseOwner(alias->second);
        }
        return key;
    };
    if (state.remoteHumanHouseScopeActive || runtimeVisionOwnerIsNeutral(identity(captureOwner))
        || identity(previousOwner) == identity(captureOwner)) return;
    // This existing canonical-owner helper also maps campaign PlayerControl houses
    // to the local player. Multiplayer peers remain distinct.
    const auto local = runtimeRepairDepotCanonicalOwnerId(state, state.economy.owner);
    const bool gained = runtimeRepairDepotCanonicalOwnerId(state, captureOwner) == local;
    const bool lost = runtimeRepairDepotCanonicalOwnerId(state, previousOwner) == local;
    if (!gained && !lost) return;
    if (type && type->needsEngineer) {
        if (lost) emitRuntimeSound(state, "EVA_TechBuildingLost");
        if (gained) {
            const auto section = runtimeCaptureEvaSection(state, *type);
            if (!section.empty()) emitRuntimeSound(state, "sound:" + section);
        }
    } else {
        emitRuntimeSound(state, runtimeSoundIntentBuildingCaptured);
    }
}
