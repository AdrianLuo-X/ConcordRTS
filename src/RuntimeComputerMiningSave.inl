void writeRuntimeComputerMining(std::ostream& output, const RuntimeWindowState& state) {
    output << "\n[ComputerMining]\nSchema=1\nCount="
           << state.computerMiningByOwner.size() << '\n';
    std::size_t index = 0;
    for (const auto& [owner, mining] : state.computerMiningByOwner) {
        output << index++ << '=' << runtimeSaveField(owner) << '|'
            << mining.storedResourceUnits << '|' << mining.storedResourceValue << '|'
            << mining.storedResourceGemUnits << '|' << mining.resourceTrips << '|'
            << mining.harvesterTeleports << '|' << mining.slaveMinerLoads << '\n';
    }
}

bool readRuntimeComputerMining(const IniFile& ini,
    std::map<std::string, RuntimeComputerMiningState>& output, std::string& error) {
    std::map<std::string, RuntimeComputerMiningState> candidate;
    if (ini.getSection("ComputerMining") == nullptr) {
        output.clear();
        return true; // Old saves had real cargo, but no computer ore storage.
    }
    int schema = 0, count = -1;
    if (!parseRuntimeInt(ini.getValue("ComputerMining", "Schema", ""), schema)
        || schema != 1
        || !parseRuntimeInt(ini.getValue("ComputerMining", "Count", ""), count)
        || count < 0 || count > 10000) {
        error = "invalid computer mining header"; return false;
    }
    for (int index = 0; index < count; ++index) {
        const auto fields = splitRuntimeSaveFields(
            ini.getValue("ComputerMining", std::to_string(index), ""));
        if (fields.size() != 7U || trimAsciiLocal(fields[0]).empty()) {
            error = "invalid computer mining record"; return false;
        }
        RuntimeComputerMiningState mining;
        int* values[] = {&mining.storedResourceUnits, &mining.storedResourceValue,
            &mining.storedResourceGemUnits, &mining.resourceTrips,
            &mining.harvesterTeleports, &mining.slaveMinerLoads};
        for (int i = 0; i < 6; ++i) {
            if (!parseRuntimeInt(fields[i + 1], *values[i]) || *values[i] < 0) {
                error = "invalid computer mining value"; return false;
            }
        }
        if (mining.storedResourceGemUnits > mining.storedResourceUnits
            || !candidate.emplace(toLowerAsciiLocal(trimAsciiLocal(fields[0])), mining).second) {
            error = "invalid computer mining owner or cargo"; return false;
        }
    }
    output = std::move(candidate);
    return true;
}
