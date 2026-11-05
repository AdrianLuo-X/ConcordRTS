// Frozen pre-extraction codec only; private state adapter holds the exact original map.
// This has no world or tick and is not a W028 simulation fixture.
namespace original_tactics_codec {
struct RuntimeWindowState {
    struct { std::map<std::string, RuntimeComputerTacticsState> computerTacticsByOwner; } triggers;
};
std::string toLowerAsciiLocal(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))
        );
    }
    return value;
}

std::string trimAsciiLocal(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size()
        && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first
        && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }

    return value.substr(first, last - first);
}

std::vector<std::string> splitRuntimeSaveFields(const std::string& value) {
    std::vector<std::string> fields;
    std::string current;
    for (char character : value) {
        if (character == '|') {
            fields.push_back(trimAsciiLocal(current));
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    fields.push_back(trimAsciiLocal(current));
    return fields;
}

std::string runtimeSaveField(std::string value) {
    for (char& character : value) {
        if (character == '|' || character == '\r' || character == '\n') {
            character = ' ';
        }
    }
    return value;
}

bool parseRuntimeInt(const std::string& value, int& output) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(trimAsciiLocal(value), &consumed);
        if (consumed != trimAsciiLocal(value).size()) {
            return false;
        }
        output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

double parseRuntimeDoubleOrDefault(
    const std::string& value,
    double fallback
) {
    try {
        std::size_t parsed = 0;
        const double result = std::stod(trimAsciiLocal(value), &parsed);
        return parsed > 0 && std::isfinite(result) ? result : fallback;
    } catch (...) {
        return fallback;
    }
}
void writeRuntimeComputerTactics(std::ostream& output, const RuntimeWindowState& state) {
    std::ostringstream records;
    records << std::setprecision(17);
    int count = 0;
    for (const auto& [owner, tactics] : state.triggers.computerTacticsByOwner) {
        const auto key = runtimeSaveField(owner);
        records << count++ << "=O|" << key << '|' << tactics.nextTriggerTick << '|'
            << tactics.selectionSerial << '|' << tactics.nextTeamInstanceId << '\n';
        for (const auto& [id, team] : tactics.teams) {
            const int flags = int(team.secondary) | int(team.baseDefense) << 1 | int(team.launched) << 2
                | int(team.outcomeReported) << 3 | int(team.missionFinished) << 4 | int(team.achievedGreatSuccess) << 5;
            records << count++ << "=T|" << key << '|' << id << '|' << runtimeSaveField(team.triggerId)
                << '|' << runtimeSaveField(team.teamTypeId) << '|' << runtimeSaveField(team.targetOwner)
                << '|' << team.createdTick << '|' << team.lastProgressTick << '|' << team.missionInstanceId
                << '|' << flags << '\n';
            for (int objectId : team.objectIds)
                records << count++ << "=B|" << key << '|' << id << '|' << objectId << '\n';
            for (int objectId : team.joiningObjectIds)
                records << count++ << "=J|" << key << '|' << id << '|' << objectId << '\n';
            for (const auto& missing : team.missingMembers)
                records << count++ << "=M|" << key << '|' << id << '|' << runtimeSaveField(missing) << '\n';
        }
        for (int lane = 0; lane < 3; ++lane) {
            const auto& order = tactics.production[lane];
            if (order.typeId.empty()) continue;
            records << count++ << "=P|" << key << '|' << lane << '|' << order.teamInstanceId
                << '|' << runtimeSaveField(order.typeId) << '|' << order.remainingTicks
                << '|' << order.paidCost << '|' << order.totalTicks << '\n';
        }
        for (const auto& [id, history] : tactics.history)
            records << count++ << "=H|" << key << '|' << runtimeSaveField(id) << '|' << history.weight
                << '|' << history.executions << '|' << history.successes << '|' << history.failures << '\n';
        for (const auto& [target, capture] : tactics.captureDecisions)
            records << count++ << "=C|" << key << '|' << target << '|' << capture.controllerId
                << '|' << capture.startedTick << '|' << capture.decision << '\n';
    }
    output << "\n[ComputerTactics]\nSchema=2\nCount=" << count << '\n' << records.str();
}

bool readRuntimeComputerTactics(const IniFile& ini,
    std::map<std::string, RuntimeComputerTacticsState>& output, std::string& error) {
    std::map<std::string, RuntimeComputerTacticsState> candidate;
    if (!ini.getSection("ComputerTactics")) { output.clear(); return true; }
    const auto fail = [&]() { error = "invalid computer tactic state"; return false; };
    int schema = 0, count = -1;
    if (!parseRuntimeInt(ini.getValue("ComputerTactics", "Schema", ""), schema) || (schema != 1 && schema != 2)
        || !parseRuntimeInt(ini.getValue("ComputerTactics", "Count", ""), count)
        || count < 0 || count > 250000) return fail();
    std::set<int> claimedObjects, claimedMissions;
    for (int index = 0; index < count; ++index) {
        const auto fields = splitRuntimeSaveFields(ini.getValue("ComputerTactics", std::to_string(index), ""));
        if (fields.size() < 2 || fields[1].empty()) return fail();
        const auto key = toLowerAsciiLocal(trimAsciiLocal(fields[1]));
        const auto integer = [&](int position, int& target, int minimum = 0) {
            return position < static_cast<int>(fields.size())
                && parseRuntimeInt(fields[position], target) && target >= minimum;
        };
        if (fields[0] == "O") {
            RuntimeComputerTacticsState tactics;
            if (fields.size() != 5 || !integer(2, tactics.nextTriggerTick)
                || !integer(3, tactics.selectionSerial) || !integer(4, tactics.nextTeamInstanceId, 1)
                || !candidate.emplace(key, tactics).second) return fail();
            continue;
        }
        const auto house = candidate.find(key);
        if (house == candidate.end()) return fail();
        auto& tactics = house->second;
        if (fields[0] == "T") {
            RuntimeComputerTacticTeam team; int flags = 0;
            if (fields.size() != 10 || !integer(2, team.instanceId, 1)
                || team.instanceId >= tactics.nextTeamInstanceId || fields[3].empty() || fields[4].empty()
                || !integer(6, team.createdTick) || !integer(7, team.lastProgressTick)
                || team.lastProgressTick < team.createdTick || !integer(8, team.missionInstanceId, -1)
                || !integer(9, flags) || flags > 63) return fail();
            team.triggerId = fields[3]; team.teamTypeId = fields[4]; team.targetOwner = fields[5];
            team.secondary = (flags & 1) != 0; team.baseDefense = (flags & 2) != 0;
            team.launched = (flags & 4) != 0; team.outcomeReported = (flags & 8) != 0;
            team.missionFinished = (flags & 16) != 0; team.achievedGreatSuccess = (flags & 32) != 0;
            if ((team.launched && team.missionInstanceId < 0)
                || (team.missionInstanceId >= 0 && !claimedMissions.insert(team.missionInstanceId).second)
                || !tactics.teams.emplace(team.instanceId, team).second) return fail();
        } else if (fields[0] == "B" || fields[0] == "M" || fields[0] == "J") {
            int id = 0;
            if (fields.size() != 4 || !integer(2, id, 1)) return fail();
            const auto team = tactics.teams.find(id);
            if (team == tactics.teams.end()) return fail();
            if (fields[0] == "J") {
                int objectId = -1;
                if (!integer(3, objectId) || !team->second.launched
                    || std::find(team->second.objectIds.begin(), team->second.objectIds.end(), objectId) == team->second.objectIds.end()
                    || std::find(team->second.joiningObjectIds.begin(), team->second.joiningObjectIds.end(), objectId) != team->second.joiningObjectIds.end()) return fail();
                team->second.joiningObjectIds.push_back(objectId);
            } else if (fields[0] == "B") {
                int objectId = -1;
                if (!integer(3, objectId) || !claimedObjects.insert(objectId).second) return fail();
                team->second.objectIds.push_back(objectId);
            } else {
                if (fields[3].empty()) return fail();
                team->second.missingMembers.push_back(fields[3]);
            }
            if (team->second.objectIds.size() + team->second.missingMembers.size() > 256) return fail();
        } else if (fields[0] == "P") {
            int lane = -1; RuntimeComputerTacticProduction order;
            if (fields.size() != 8 || !integer(2, lane) || lane >= 3
                || !tactics.production[lane].typeId.empty() || !integer(3, order.teamInstanceId, -1)
                || fields[4].empty() || !integer(5, order.remainingTicks) || !integer(6, order.paidCost)
                || !integer(7, order.totalTicks, 1) || order.remainingTicks > order.totalTicks) return fail();
            order.typeId = fields[4];
            if (order.teamInstanceId >= 0) {
                const auto team = tactics.teams.find(order.teamInstanceId);
                if (team == tactics.teams.end()
                    || std::find(team->second.missingMembers.begin(), team->second.missingMembers.end(), order.typeId)
                        == team->second.missingMembers.end()) return fail();
            }
            tactics.production[lane] = order;
        } else if (fields[0] == "C") {
            int target = -1; RuntimeComputerTacticsState::CaptureDecision capture;
            if (schema < 2 || fields.size() != 6 || !integer(2, target)
                || !integer(3, capture.controllerId) || !integer(4, capture.startedTick)
                || !integer(5, capture.decision, 1) || capture.decision > 5
                || !tactics.captureDecisions.emplace(target, capture).second) return fail();
        } else if (fields[0] == "H") {
            RuntimeComputerTacticHistory history;
            if (fields.size() != 7 || fields[2].empty() || !integer(4, history.executions)
                || !integer(5, history.successes) || !integer(6, history.failures)) return fail();
            history.weight = parseRuntimeDoubleOrDefault(fields[3], -1.0);
            if (!std::isfinite(history.weight) || history.weight < 0.0
                || static_cast<long long>(history.successes) + history.failures > history.executions
                || !tactics.history.emplace(fields[2], history).second) return fail();
        } else return fail();
    }
    output = std::move(candidate);
    return true;
}

}
