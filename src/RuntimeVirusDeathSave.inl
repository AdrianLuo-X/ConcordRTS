std::string runtimeMultiplayerSnapshotImpactRecord(const RuntimeImpactEffect& impact);
bool parseRuntimeMultiplayerSnapshotImpactRecord(const std::string& value, RuntimeImpactEffect& impact);

void writeRuntimeVirusDeaths(const RuntimeWindowState& state, std::ostream& output) {
    const auto count = std::count_if(state.impactEffects.begin(), state.impactEffects.end(),
        [](const auto& impact) { return impact.lifecycleState == "VirusDeath"; });
    output << "\n[VirusDeathAnimations]\nSchema=1\nCount=" << count << '\n';
    int index = 0;
    for (const auto& impact : state.impactEffects) {
        if (impact.lifecycleState == "VirusDeath")
            output << index++ << '=' << runtimeMultiplayerSnapshotImpactRecord(impact) << '\n';
    }
}

bool loadRuntimeVirusDeaths(RuntimeWindowState& state, const IniFile& ini, std::string& error) {
    const std::string schema = ini.getValue("VirusDeathAnimations", "Schema", "");
    if (schema.empty()) return true; // Legacy saves had no pending virus deaths.
    int count = 0;
    if (schema != "1" || !parseRuntimeInt(ini.getValue("VirusDeathAnimations", "Count", ""), count)
        || count < 0 || count > 65536) {
        error = "invalid VirusDeathAnimations section";
        return false;
    }
    std::vector<RuntimeImpactEffect> restored;
    for (int index = 0; index < count; ++index) {
        RuntimeImpactEffect impact;
        if (!parseRuntimeMultiplayerSnapshotImpactRecord(
                ini.getValue("VirusDeathAnimations", std::to_string(index), ""), impact)
            || impact.lifecycleState != "VirusDeath" || !impact.death
            || impact.deferredParticleAnimId.empty()) {
            error = "invalid pending virus death animation";
            return false;
        }
        restored.push_back(std::move(impact));
    }
    state.impactEffects = std::move(restored);
    return true;
}
