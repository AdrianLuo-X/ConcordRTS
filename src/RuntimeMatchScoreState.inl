void recordRuntimeMatchBuild(RuntimeWindowState& state, const std::string& owner, int count = 1) {
    if (state.campaignMode || state.skirmishMatchFinished || count <= 0
        || trimAsciiLocal(owner).empty() || runtimeOwnerIsNeutralForAutoTarget(owner)) return;
    auto& built = state.matchScores[runtimeCanonicalHumanHouseOwner(owner)].built;
    built = static_cast<int>(std::min<long long>(
        static_cast<long long>(built) + count, std::numeric_limits<int>::max()));
}

void recordRuntimeMatchObjectDestruction(
    RuntimeWindowState& state, const WorldObjectInstance& object,
    const std::string& sourceOwner
) {
    if (state.campaignMode || state.skirmishMatchFinished
        || runtimeOwnerIsNeutralForAutoTarget(object.owner)
        || !runtimeVeterancyObjectAwardsExperience(state, object)) return;
    const auto add = [](int& value, int amount) {
        value = static_cast<int>(std::min<long long>(
            static_cast<long long>(value) + std::max(0, amount), std::numeric_limits<int>::max()));
    };
    auto& lost = state.matchScores[runtimeCanonicalHumanHouseOwner(object.owner)];
    const bool structure = object.kind == WorldObjectKind::Structure;
    add(structure ? lost.structuresLost : lost.unitsLost, 1);
    if (trimAsciiLocal(sourceOwner).empty() || runtimeOwnerIsNeutralForAutoTarget(sourceOwner)
        || runtimeOwnersAreAllied(state, sourceOwner, object.owner)) return;
    auto& killed = state.matchScores[runtimeCanonicalHumanHouseOwner(sourceOwner)];
    add(structure ? killed.structuresDestroyed : killed.unitsDestroyed, 1);
    // The engine's match score is the destroyed enemy's authored base Cost.
    // Friendly fire, neutral objects, sold/transformed actors and DontScore
    // objects do not award points. This is not a claimed retail score formula.
    add(killed.score, runtimeVeterancyObjectCost(state, object));
}

void writeRuntimeMatchScore(const RuntimeWindowState& state, std::ostream& output) {
    const bool hasBuilds = std::any_of(state.matchScores.begin(), state.matchScores.end(),
        [](const auto& entry) { return entry.second.built != 0; });
    output << "\n[RuntimeMatchScore]\nSchema=" << (hasBuilds ? 2 : 1)
        << "\nCount=" << state.matchScores.size() << '\n';
    int index = 0;
    for (const auto& [owner, score] : state.matchScores) {
        output << index++ << '=' << iniHexEncode(owner) << ' '
            << score.unitsDestroyed << ' ' << score.structuresDestroyed << ' '
            << score.unitsLost << ' ' << score.structuresLost << ' ' << score.score;
        if (hasBuilds) output << ' ' << score.built;
        output << '\n';
    }
}

bool loadRuntimeMatchScore(RuntimeWindowState& state, const IniFile& ini, std::string& error) {
    std::map<std::string, RuntimeMatchScoreEntry> loaded;
    if (ini.getSection("RuntimeMatchScore") == nullptr) {
        state.matchScores.clear();
        return true;
    }
    const int count = parseRuntimeIntOrDefault(ini.getValue("RuntimeMatchScore", "Count", "-1"), -1);
    const int schema = parseRuntimeIntOrDefault(ini.getValue("RuntimeMatchScore", "Schema", ""), 0);
    if ((schema != 1 && schema != 2) || count < 0 || count > 4096) {
        error = "invalid match score schema/count";
        return false;
    }
    for (int index = 0; index < count; ++index) {
        std::istringstream record(ini.getValue("RuntimeMatchScore", std::to_string(index), ""));
        std::string encodedOwner, owner, trailing;
        RuntimeMatchScoreEntry score;
        if (!(record >> encodedOwner >> score.unitsDestroyed >> score.structuresDestroyed
                >> score.unitsLost >> score.structuresLost >> score.score)
            || (schema == 2 && !(record >> score.built))
            || (record >> trailing) || !iniHexDecode(encodedOwner, owner, 1024)
            || trimAsciiLocal(owner).empty() || score.unitsDestroyed < 0 || score.structuresDestroyed < 0
            || score.unitsLost < 0 || score.structuresLost < 0 || score.score < 0 || score.built < 0
            || !loaded.emplace(runtimeCanonicalHumanHouseOwner(owner), score).second) {
            error = "invalid match score record " + std::to_string(index);
            return false;
        }
    }
    state.matchScores = std::move(loaded);
    return true;
}

void hashRuntimeMatchScore(const RuntimeWindowState& state, std::uint64_t& hash) {
    // Leave legacy empty-score snapshots' authoritative digest unchanged.
    if (state.matchScores.empty()) return;
    runtimeStableHashMixString(hash, "match-score-v1");
    runtimeStableHashMix(hash, static_cast<long long>(state.matchScores.size()));
    for (const auto& [owner, score] : state.matchScores) {
        runtimeStableHashMixString(hash, owner);
        runtimeStableHashMix(hash, score.unitsDestroyed);
        runtimeStableHashMix(hash, score.structuresDestroyed);
        runtimeStableHashMix(hash, score.unitsLost);
        runtimeStableHashMix(hash, score.structuresLost);
        runtimeStableHashMix(hash, score.score);
    }
    // Preserve existing snapshots' digest when they have no build facts.
    for (const auto& [owner, score] : state.matchScores) {
        if (score.built == 0) continue;
        runtimeStableHashMixString(hash, "match-built-v2");
        runtimeStableHashMixString(hash, owner);
        runtimeStableHashMix(hash, score.built);
    }
}
