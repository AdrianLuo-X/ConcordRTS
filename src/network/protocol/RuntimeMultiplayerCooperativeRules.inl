bool validateCooperativeRules(const RuntimeMultiplayerMatchRules& rules, std::string& error) {
    const auto& mission = rules.cooperativeMission;
    if (rules.gameMode != "cooperative") {
        if (mission != RuntimeMultiplayerCooperativeMission{})
            return fail(error, "ordinary match cannot carry cooperative mission rules");
        return true;
    }
    if (!validCanonicalToken(mission.mapId, runtimeMultiplayerMatchMaximumCanonicalMapIdBytes)
        || mission.scenarioFingerprint == 0)
        return fail(error, "cooperative mission identity is missing or invalid");
    for (const auto* countries : {&mission.playerCountries, &mission.enemyCountries}) {
        if (countries->empty() || countries->size() > 64)
            return fail(error, "cooperative coalition country list is missing or too large");
        std::set<std::string> unique;
        for (const auto& country : *countries) {
            if (!validCanonicalToken(country, runtimeMultiplayerMatchMaximumSlotOptionBytes)
                || country == "random" || !unique.insert(country).second)
                return fail(error, "cooperative coalition country list is invalid");
        }
    }
    return true;
}

bool validateCooperativeSlots(const RuntimeMultiplayerMatchSetup& setup, std::string& error) {
    if (setup.rules.gameMode != "cooperative") return true;
    if (setup.slots.size() != 4)
        return fail(error, "cooperative mission requires exactly four A-D slots");
    auto mapName = setup.map.canonicalId.substr(setup.map.canonicalId.find_last_of('/') + 1);
    if (const auto extension = mapName.find_last_of('.'); extension != std::string::npos)
        mapName.resize(extension);
    if (mapName != setup.rules.cooperativeMission.mapId)
        return fail(error, "cooperative mission does not match the selected map");
    for (std::size_t index = 0; index < setup.slots.size(); ++index) {
        const auto& slot = setup.slots[index];
        if (index < 2 && slot.controller == RuntimeMultiplayerMatchSlotController::Closed) continue;
        const auto expected = index < 2 ? RuntimeMultiplayerMatchSlotController::Human
                                       : RuntimeMultiplayerMatchSlotController::Computer;
        if (slot.controller != expected || slot.startPosition != index + 1
            || slot.teamId != (index < 2 ? "1" : "2"))
            return fail(error, "cooperative A/B players and C/D enemies have fixed sides and starts");
        const auto& countries = index < 2 ? setup.rules.cooperativeMission.playerCountries
                                         : setup.rules.cooperativeMission.enemyCountries;
        if (std::find(countries.begin(), countries.end(), slot.countryId) == countries.end())
            return fail(error, "country is unavailable in this cooperative mission");
    }
    return true;
}

void writeCooperativeRules(ByteWriter& writer, const RuntimeMultiplayerCooperativeMission& mission) {
    writer.writeString(mission.mapId);
    writer.writeU64(mission.scenarioFingerprint);
    for (const auto* countries : {&mission.playerCountries, &mission.enemyCountries}) {
        writer.writeU8(static_cast<std::uint8_t>(countries->size()));
        for (const auto& country : *countries) writer.writeString(country);
    }
}

bool readCooperativeRules(ByteReader& reader, RuntimeMultiplayerCooperativeMission& mission) {
    if (!reader.readString(runtimeMultiplayerMatchMaximumCanonicalMapIdBytes, mission.mapId, "cooperative map")
        || !reader.readU64(mission.scenarioFingerprint)) return false;
    for (auto* countries : {&mission.playerCountries, &mission.enemyCountries}) {
        std::uint8_t count = 0;
        if (!reader.readU8(count)) return false;
        if (count > 64) return reader.fail("cooperative country count exceeds its limit");
        countries->reserve(count);
        for (std::uint8_t index = 0; index < count; ++index) {
            std::string country;
            if (!reader.readString(runtimeMultiplayerMatchMaximumSlotOptionBytes, country, "cooperative country")) return false;
            countries->push_back(std::move(country));
        }
    }
    return true;
}
