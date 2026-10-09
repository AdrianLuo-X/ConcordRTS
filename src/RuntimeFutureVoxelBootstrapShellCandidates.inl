// Include only after the existing catalog/country helpers. Loading preparation
// reads values; it never queues, spawns or changes authority. Renderer decides
// ordinary VXL/profile eligibility without a prototype entering the world.
std::vector<std::string> collectRuntimeFutureNormalVoxelTypeIds(
    const RuntimeWindowState& state) {
    constexpr std::size_t maximumCandidateTypeIds = 128;
    std::vector<std::string> result;
    result.reserve(std::min(state.economy.catalog.size(), maximumCandidateTypeIds));
    if (!state.rulesLoaded) return result;
    for (const RuntimeBuildCatalogItem& item : state.economy.catalog) {
        if (item.kind != RuntimeBuildKind::Unit || !item.techTreeAllowed
            || item.secretLabInjected || item.techLevel < 0
            || item.techLevel > state.economy.techLevel
            || !runtimeBuildItemVisibleForPlayerCountry(state, item)
            || !state.rules.findUnitType("UnitType", item.typeId)
            || item.typeId.empty() || item.typeId.size() > 128
            || item.typeId.find('|') != std::string::npos) continue;
        if (std::find(result.begin(), result.end(), item.typeId) != result.end()) continue;
        result.push_back(item.typeId);
        if (result.size() == maximumCandidateTypeIds) break;
    }
    // Do not require current factories, credits, prerequisite buildings,
    // stolen technology or build-limit slots. This is a bounded potential
    // future list, not permission to produce its entries. In particular stock
    // SREF must not be excluded merely because GATECH is not built yet.
    return result;
}
