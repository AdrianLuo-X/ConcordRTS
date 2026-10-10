void synchronizeRuntimeCooperativeOutcome(RuntimeWindowState& state);

bool readRuntimeCooperativeSession(const RuntimeWindowState& state, const IniFile& ini,
    RuntimeCooperativeSessionState& result, std::string& error) {
    result = state.cooperativeSession;
    if (!runtimeIsCooperativeMode(state.config)) {
        if (ini.getSection("CooperativeSessionRuntime") != nullptr) {
            error = "cooperative save cannot be loaded into another mode"; return false;
        }
        return true;
    }
    if (!state.cooperativeSession.active()) {
        error = "cooperative save has no initialized mission"; return false;
    }
    if (ini.getSection("CooperativeSessionRuntime") == nullptr) {
        if (!state.config.multiplayerActive) return true; // Earlier local saves had no session section.
        error = "cooperative network snapshot is missing mission state"; return false;
    }
    IniFile scenarioIni, mapIni;
    CooperativeScenarioDatabase database;
    MapDatabase map;
    if (!scenarioIni.loadFromFile(runtimeCooperativeScenarioPath(state.config).generic_string())
        || !database.loadFromIni(scenarioIni)
        || !mapIni.loadFromFile(state.config.defaultMapPath) || !map.loadFromIni(mapIni)) {
        error = "saved cooperative mission resources unavailable"; return false;
    }
    const auto* scenario = database.findByMapId(state.cooperativeSession.mapId());
    if (!scenario || !result.load(ini, *scenario,
        database.validateMap(*scenario, map, mapIni), &error)) return false;
    for (std::size_t i = 0; i < runtimeCooperativeSlotCount; ++i) {
        const auto& expected = state.cooperativeSession.slots()[i];
        const auto& loaded = result.slots()[i];
        if (loaded.owner != expected.owner || loaded.country != expected.country) {
            error = "saved cooperative players differ from the current mission"; return false;
        }
    }
    const int tick = parseRuntimeIntOrDefault(ini.getValue("RuntimeSave", "Tick", "-1"), -1);
    if (result.hasOutcomeSnapshot() && result.lastOutcomeSnapshot().tick > tick) {
        error = "cooperative outcome is newer than the saved world"; return false;
    }
    return true;
}

// The same previous-frame observations must survive a save/reconnect. Rebuilding
// them from the loaded world would erase a just-destroyed/captured target event.
template<class Map> void writeRuntimeCooperativeObservationMap(std::ostream& output,
    const char* name, const Map& map) {
    std::vector<int> keys;
    for (const auto& entry : map) keys.push_back(entry.first);
    std::sort(keys.begin(), keys.end());
    output << "\n[CooperativeObservation_" << name << "]\nCount=" << keys.size() << '\n';
    for (std::size_t i = 0; i < keys.size(); ++i) {
        output << i << '=' << keys[i] << '|';
        if constexpr (std::is_same_v<typename Map::mapped_type, std::string>)
            output << iniHexEncode(map.at(keys[i]));
        else output << static_cast<int>(map.at(keys[i]));
        output << '\n';
    }
}

template<class Map> bool readRuntimeCooperativeObservationMap(const IniFile& ini,
    const char* name, Map& map, std::string& error) {
    const std::string section = std::string("CooperativeObservation_") + name;
    int count = -1;
    if (!parseRuntimeInt(ini.getValue(section, "Count", ""), count) || count < 0 || count > 1000000) {
        error = "invalid cooperative observation header " + section; return false;
    }
    Map result;
    for (int i = 0; i < count; ++i) {
        const auto fields = splitRuntimeSaveFields(ini.getValue(section, std::to_string(i), ""));
        int key = -1;
        typename Map::mapped_type value{};
        if (fields.size() != 2 || !parseRuntimeInt(fields[0], key) || key < 0) {
            error = "invalid cooperative observation record " + section; return false;
        }
        if constexpr (std::is_same_v<typename Map::mapped_type, std::string>) {
            if (!iniHexDecode(fields[1], value, 4096)) {
                error = "invalid cooperative observation owner"; return false;
            }
        } else {
            int number = 0;
            if (!parseRuntimeInt(fields[1], number)) {
                error = "invalid cooperative observation value"; return false;
            }
            if constexpr (std::is_same_v<typename Map::mapped_type, bool>)
                if (number != 0 && number != 1) { error = "invalid cooperative alive flag"; return false; }
            value = static_cast<typename Map::mapped_type>(number);
        }
        if (!result.emplace(key, std::move(value)).second) {
            error = "duplicate cooperative observation"; return false;
        }
    }
    map = std::move(result);
    return true;
}

#define RUNTIME_COOPERATIVE_OBSERVATIONS(OP) \
    OP(campaignPreviousCellByObject) \
    OP(campaignPreviousHealthByObject) \
    OP(campaignInitialHealthByObject) \
    OP(campaignPreviousAliveByObject) \
    OP(campaignInitialOwnerByObject) \
    OP(campaignPreviousOwnerByObject) \
    OP(campaignAttackedTickByObject) \
    OP(campaignAttackerHouseByObject) \
    OP(campaignDestroyedTickByObject) \
    OP(campaignDestroyerHouseByObject) \
    OP(campaignEnteredTickByTargetObject) \
    OP(campaignEntrantHouseByTargetObject) \
    OP(campaignSpiedTickByTargetObject) \
    OP(campaignSpyHouseByTargetObject) \
    OP(campaignBridgeDestroyedTickByCell)

void writeRuntimeCooperativeObservations(std::ostream& output, const RuntimeWindowState& state,
    bool replayCampaign = false) {
    if (!state.cooperativeSession.active() && !replayCampaign) return;
    if (state.cooperativeSession.active()) { output << '\n'; state.cooperativeSession.save(output); }
    if (replayCampaign) output << "\n[ReplayTriggerObservations]\nSchema=1\n";
#define WRITE_COOP_OBSERVATION(field) writeRuntimeCooperativeObservationMap(output, #field, state.triggers.field);
    RUNTIME_COOPERATIVE_OBSERVATIONS(WRITE_COOP_OBSERVATION)
#undef WRITE_COOP_OBSERVATION
    const auto saveSet = [&](const char* name, const auto& set) {
        std::map<int, int> values; for (int id : set) values[id] = 1;
        writeRuntimeCooperativeObservationMap(output, name, values);
    };
    saveSet("Known", state.triggers.campaignPreviouslyKnownObjects);
    saveSet("Discovered", state.triggers.campaignPreviouslyDiscoveredObjects);
    if (replayCampaign) saveSet("Selected", state.triggers.campaignPreviousSelectedObjects);
}

bool readRuntimeCooperativeObservations(const IniFile& ini, RuntimeTriggerState& triggers,
    std::string& error, bool replayCampaign = false) {
    if (!replayCampaign && ini.getSection("CooperativeSessionRuntime") == nullptr) return true;
    if (replayCampaign && ini.getValue("ReplayTriggerObservations", "Schema", "") != "1") {
        error = "missing replay campaign trigger observations"; return false;
    }
#define READ_COOP_OBSERVATION(field) if (!readRuntimeCooperativeObservationMap(ini, #field, triggers.field, error)) return false;
    RUNTIME_COOPERATIVE_OBSERVATIONS(READ_COOP_OBSERVATION)
#undef READ_COOP_OBSERVATION
    const auto loadSet = [&](const char* name, auto& set) {
        std::map<int, int> values;
        if (!readRuntimeCooperativeObservationMap(ini, name, values, error)) return false;
        set.clear(); for (const auto& [id, value] : values) { (void)value; set.insert(id); }
        return true;
    };
    return loadSet("Known", triggers.campaignPreviouslyKnownObjects)
        && loadSet("Discovered", triggers.campaignPreviouslyDiscoveredObjects)
        && (!replayCampaign || loadSet("Selected", triggers.campaignPreviousSelectedObjects));
}
#undef RUNTIME_COOPERATIVE_OBSERVATIONS
