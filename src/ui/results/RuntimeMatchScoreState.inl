void recordRuntimeMatchBuild(RuntimeWindowState& state, const std::string& owner, int count = 1) {
    if (state.campaignMode || state.skirmishMatchFinished || count <= 0
        || trimAsciiLocal(owner).empty() || runtimeOwnerIsNeutralForAutoTarget(owner)) return;
    recordRuntimeMatchScoreBuild(state.matchScores, {owner, count});
}

void recordRuntimeMatchObjectDestruction(
    RuntimeWindowState& state, const WorldObjectInstance& object,
    const std::string& sourceOwner
) {
    if (state.campaignMode || state.skirmishMatchFinished
        || runtimeOwnerIsNeutralForAutoTarget(object.owner)
        || !runtimeVeterancyObjectAwardsExperience(state, object)) return;
    const bool credited = !trimAsciiLocal(sourceOwner).empty()
        && !runtimeOwnerIsNeutralForAutoTarget(sourceOwner)
        && !runtimeOwnersAreAllied(state, sourceOwner, object.owner);
    recordRuntimeMatchScoreDestruction(state.matchScores, {
        object.owner, credited ? sourceOwner : std::string{},
        object.kind == WorldObjectKind::Structure,
        credited ? runtimeVeterancyObjectCost(state, object) : 0});
}

void writeRuntimeMatchScore(const RuntimeWindowState& state, std::ostream& output) {
    writeRuntimeMatchScore(state.matchScores, output);
}

bool loadRuntimeMatchScore(RuntimeWindowState& state, const IniFile& ini, std::string& error) {
    return loadRuntimeMatchScore(state.matchScores, ini, error);
}

void hashRuntimeMatchScore(const RuntimeWindowState& state, std::uint64_t& hash) {
    hashRuntimeMatchScore(state.matchScores, hash);
}
