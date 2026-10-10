void writeRuntimeTriggerReinforcements(const RuntimeWindowState& state, std::ostream& output) {
    output << "[TriggerReinforcements]\nSchema=1\nDataHex="
        << iniHexEncode(serializeRuntimeTriggerReinforcements(state.triggers.reinforcements)) << "\n\n";
}

bool readRuntimeTriggerReinforcements(RuntimeWindowState& state, const IniFile& ini, std::string& error) {
    RuntimeTriggerReinforcements staged;
    if (!ini.getSection("TriggerReinforcements")) {
        state.triggers.reinforcements = std::move(staged);
        return true;
    }
    std::string payload;
    if (ini.getValue("TriggerReinforcements", "Schema", "") != "1"
        || !iniHexDecode(ini.getValue("TriggerReinforcements", "DataHex", ""), payload, 32U * 1024U * 1024U)
        || !deserializeRuntimeTriggerReinforcements(payload, staged, error)) {
        error = "pending trigger reinforcements: malformed save section";
        return false;
    }
    for (const auto& [id, job] : staged.pending) {
        const auto* team = findRuntimeTriggerTeamById(state, job.teamTypeId);
        if (!team) { error = "pending reinforcement references missing TeamType"; return false; }
        const auto taskForce = state.triggers.taskForces.find(team->taskForceId);
        if (taskForce == state.triggers.taskForces.end()) { error = "pending reinforcement TaskForce missing"; return false; }
        std::map<std::string, std::int64_t> expected, actual;
        for (const auto& member : taskForce->second.members)
            expected[toLowerAsciiLocal(member.unitId)] += member.count;
        for (const auto& member : job.members) {
            if (!findRuntimeUnitTypeById(state.rules, member.typeId)) {
                error = "pending reinforcement member type missing"; return false;
            }
            actual[toLowerAsciiLocal(member.typeId)] += member.count;
        }
        if (expected != actual) { error = "pending reinforcement composition differs from TaskForce"; return false; }
    }
    state.triggers.reinforcements = std::move(staged);
    return true;
}
