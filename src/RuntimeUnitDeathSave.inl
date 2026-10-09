// Unit death visuals keep their timeline when an ordinary game is saved.
// Virus deaths use their existing separate section because they also release
// a delayed damaging particle system. Wreck water animations reuse this
// existing timeline codec, with a closed lifecycle/presentation whitelist.
bool runtimePersistentWreckWaterAnimation(const RuntimeImpactEffect& impact) {
    return !impact.death && impact.remainingTicks > 0
        && (impact.lifecycleState == "WreckWaterEntry" || impact.lifecycleState == "WreckWake"
            || impact.lifecycleState == "WreckWaterImpact");
}

bool runtimePersistentUnitDeath(const RuntimeImpactEffect& impact) {
    return (impact.death && impact.remainingTicks > 0
        && (impact.lifecycleState == "UnitDestruction" || impact.lifecycleState == "InfantryDeath"))
        || runtimePersistentWreckWaterAnimation(impact);
}

bool runtimeValidWreckWaterAnimation(const RuntimeWindowState& state, const RuntimeImpactEffect& impact) {
    if (!runtimePersistentWreckWaterAnimation(impact) || impact.renderIntent != "waterWake"
        || !impact.hasWorldPosition || !std::isfinite(impact.worldX)
        || !std::isfinite(impact.worldY) || !std::isfinite(impact.worldLevel)
        || impact.damage != 0 || impact.cellSpread != 0.0
        || impact.warheadImpactIntentId != impact.animId) return false;
    return impact.lifecycleState == "WreckWake"
        ? impact.animId == state.rules.getWake() && !impact.animId.empty()
        : impact.animId == "H2O_EXP3";
}

void writeRuntimeUnitDeaths(const RuntimeWindowState& state, std::ostream& output) {
    const auto count = std::count_if(state.impactEffects.begin(), state.impactEffects.end(), runtimePersistentUnitDeath);
    output << "\n[UnitDeathAnimations]\nSchema=1\nCount=" << count << '\n';
    int index = 0;
    for (const auto& impact : state.impactEffects) {
        if (!runtimePersistentUnitDeath(impact)) continue;
        output << "Impact" << index << '=' << runtimeMultiplayerSnapshotImpactRecord(impact) << '\n';
        output << "Presentation" << index++ << '=' << iniHexEncode(impact.animId) << '|'
            << iniHexEncode(impact.renderIntent) << '|' << impact.frameIndex << '|'
            << impact.facing << '|' << impact.radius << '|' << runtimeBoolText(impact.bright) << '\n';
    }
}

bool loadRuntimeUnitDeaths(RuntimeWindowState& state, const IniFile& ini, std::string& error, bool commit = true) {
    const std::string schema = ini.getValue("UnitDeathAnimations", "Schema", "");
    if (!ini.getSection("UnitDeathAnimations")) return true; // Earlier saves omitted ordinary corpses.
    int count = 0;
    if (schema != "1" || !parseRuntimeInt(ini.getValue("UnitDeathAnimations", "Count", ""), count)
        || count < 0 || count > 65536) {
        error = "invalid UnitDeathAnimations section"; return false;
    }
    std::vector<RuntimeImpactEffect> restored;
    restored.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        RuntimeImpactEffect impact;
        const auto presentation = splitRuntimeSaveFields(
            ini.getValue("UnitDeathAnimations", "Presentation" + std::to_string(index), ""));
        if (!parseRuntimeMultiplayerSnapshotImpactRecord(
                ini.getValue("UnitDeathAnimations", "Impact" + std::to_string(index), ""), impact)
            || !runtimePersistentUnitDeath(impact) || presentation.size() != 6
            || !iniHexDecode(presentation[0], impact.animId, 256U)
            || !iniHexDecode(presentation[1], impact.renderIntent, 256U)
            || !parseRuntimeInt(presentation[2], impact.frameIndex)
            || !parseRuntimeInt(presentation[3], impact.facing)
            || !parseRuntimeInt(presentation[4], impact.radius)
            || !parseRuntimeStrictBool(presentation[5], impact.bright)
            || impact.facing < -1 || impact.facing > 255 || impact.radius < 0 || impact.radius > 4096
            || impact.frameIndex < -1 || impact.frameIndex > 1000000
            || (runtimePersistentWreckWaterAnimation(impact)
                ? !runtimeValidWreckWaterAnimation(state, impact)
                : (impact.renderIntent != "unitDestruction" && impact.renderIntent != "infantryDeath"))) {
            error = "invalid unit death animation"; return false;
        }
        restored.push_back(std::move(impact));
    }
    if (commit) for (auto& impact : restored) state.impactEffects.push_back(std::move(impact));
    return true;
}
