void writeRuntimeFireReveals(const RuntimeWindowState& state, std::ostream& output) {
    output << "\n[FireReveals]\nSchema=1\nDataHex="
        << iniHexEncode(state.visionEnabled ? state.vision.serializeFireReveals(state.tick) : "1 0\n") << '\n';
}

bool readRuntimeFireReveals(RuntimeWindowState& state, const IniFile& ini, std::string& error) {
    if (!ini.getSection("FireReveals")) return true;
    std::string data;
    if (ini.getValue("FireReveals", "Schema", "") != "1"
        || !iniHexDecode(ini.getValue("FireReveals", "DataHex", ""), data, 4U*1024U*1024U)
        || !state.vision.restoreFireReveals(data, state.tick, state.config.fixedTickHz)) {
        error = "invalid weapon fire reveal save section";
        return false;
    }
    return true;
}
