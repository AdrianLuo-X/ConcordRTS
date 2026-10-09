#include "AiTriggerSandbox.h"

#include "EconomySandbox.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct ParsedScriptType {
    std::string id;
    std::string name;
    int stepCount = 0;
};

struct ParsedTaskForce {
    std::string id;
    std::string name;
    int unitCount = 0;
};

struct ParsedTeamType {
    std::string id;
    std::string name;
    std::string house;
    std::string scriptId;
    std::string taskForceId;
    int priority = 0;
    int max = 0;
    bool autocreate = false;
    bool prebuild = false;
    bool reinforce = false;
    bool aggressive = false;
};

struct ParsedTrigger {
    std::string id;
    std::string house;
    std::string linkedTriggerId;
    std::string name;
};

struct ParsedAction {
    int type = 0;
    std::string targetId;
};

struct ParsedTriggerData {
    AiTriggerParsedSummary summary;
    std::vector<ParsedTrigger> triggers;
    std::vector<ParsedTeamType> teams;
    std::map<std::string, ParsedTaskForce> taskForces;
    std::map<std::string, ParsedScriptType> scripts;
    std::map<std::string, std::vector<ParsedAction>> actionsByTrigger;
};

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string toLower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

std::vector<std::string> splitCommaList(const std::string& value) {
    std::vector<std::string> tokens;
    std::stringstream input(value);
    std::string token;

    while (std::getline(input, token, ',')) {
        tokens.push_back(trim(token));
    }

    return tokens;
}

bool parseInt(const std::string& value, int& output) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        if (consumed != value.size()) {
            return false;
        }
        output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

int parseIntOrDefault(const std::string& value, int defaultValue) {
    int parsed = defaultValue;
    return parseInt(value, parsed) ? parsed : defaultValue;
}

bool parseBool(const std::string& value) {
    const std::string normalized = toLower(value);
    return normalized == "yes" || normalized == "true" || normalized == "1";
}

int countSectionEntries(const IniFile& ini, const std::string& sectionName) {
    const IniFile::Section* section = ini.getSection(sectionName);
    return section == nullptr ? 0 : static_cast<int>(section->size());
}

std::vector<std::string> indexedIds(
    const IniFile& ini,
    const std::string& sectionName
) {
    std::vector<std::string> ids;
    const IniFile::Section* section = ini.getSection(sectionName);
    if (section == nullptr) {
        return ids;
    }

    ids.reserve(section->size());
    for (const auto& [index, value] : *section) {
        (void)index;
        if (!value.empty()) {
            ids.push_back(value);
        }
    }
    return ids;
}

void appendEvent(
    AiTriggerSandboxReport& report,
    int tick,
    const std::string& text
) {
    report.events.push_back({tick, text});
}

ParsedScriptType parseScriptType(const IniFile& ini, const std::string& id) {
    ParsedScriptType script;
    script.id = id;
    script.name = ini.getValue(id, "Name", id);

    if (const IniFile::Section* section = ini.getSection(id)) {
        for (const auto& [key, value] : *section) {
            int stepIndex = 0;
            if (parseInt(key, stepIndex) && !value.empty()) {
                ++script.stepCount;
            }
        }
    }

    return script;
}

ParsedTaskForce parseTaskForce(const IniFile& ini, const std::string& id) {
    ParsedTaskForce taskForce;
    taskForce.id = id;
    taskForce.name = ini.getValue(id, "Name", id);

    if (const IniFile::Section* section = ini.getSection(id)) {
        for (const auto& [key, value] : *section) {
            int unitIndex = 0;
            if (!parseInt(key, unitIndex)) {
                continue;
            }

            const std::vector<std::string> tokens = splitCommaList(value);
            if (tokens.empty()) {
                continue;
            }

            taskForce.unitCount += parseIntOrDefault(tokens.front(), 0);
        }
    }

    return taskForce;
}

ParsedTeamType parseTeamType(const IniFile& ini, const std::string& id) {
    ParsedTeamType team;
    team.id = id;
    team.name = ini.getValue(id, "Name", id);
    team.house = ini.getValue(id, "House", "");
    team.scriptId = ini.getValue(id, "Script", "");
    team.taskForceId = ini.getValue(id, "TaskForce", "");
    team.priority = parseIntOrDefault(ini.getValue(id, "Priority", "0"), 0);
    team.max = parseIntOrDefault(ini.getValue(id, "Max", "0"), 0);
    team.autocreate = parseBool(ini.getValue(id, "Autocreate", "no"));
    team.prebuild = parseBool(ini.getValue(id, "Prebuild", "no"));
    team.reinforce = parseBool(ini.getValue(id, "Reinforce", "no"));
    team.aggressive = parseBool(ini.getValue(id, "Aggressive", "no"));
    return team;
}

std::vector<ParsedTrigger> parseTriggers(const IniFile& ini) {
    std::vector<ParsedTrigger> triggers;
    const IniFile::Section* section = ini.getSection("Triggers");
    if (section == nullptr) {
        return triggers;
    }

    triggers.reserve(section->size());
    for (const auto& [id, value] : *section) {
        const std::vector<std::string> tokens = splitCommaList(value);
        ParsedTrigger trigger;
        trigger.id = id;
        if (!tokens.empty()) {
            trigger.house = tokens[0];
        }
        if (tokens.size() > 1) {
            trigger.linkedTriggerId = tokens[1];
        }
        if (tokens.size() > 2) {
            trigger.name = tokens[2];
        }
        triggers.push_back(trigger);
    }

    return triggers;
}

std::map<std::string, std::vector<ParsedAction>> parseActionsByTrigger(
    const IniFile& ini
) {
    std::map<std::string, std::vector<ParsedAction>> actionsByTrigger;
    const IniFile::Section* section = ini.getSection("Actions");
    if (section == nullptr) {
        return actionsByTrigger;
    }

    for (const auto& [triggerId, value] : *section) {
        const std::vector<std::string> tokens = splitCommaList(value);
        std::vector<ParsedAction> actions;
        if (tokens.empty()) {
            actionsByTrigger[triggerId] = actions;
            continue;
        }

        const int actionCount = parseIntOrDefault(tokens.front(), 0);
        std::size_t offset = 1;
        for (int index = 0; index < actionCount && offset < tokens.size(); ++index) {
            ParsedAction action;
            action.type = parseIntOrDefault(tokens[offset], 0);
            if (offset + 2 < tokens.size()) {
                action.targetId = tokens[offset + 2];
            }
            actions.push_back(action);
            offset += 8;
        }

        actionsByTrigger[triggerId] = actions;
    }

    return actionsByTrigger;
}

ParsedTriggerData parseTriggerData(const IniFile* mapIni) {
    ParsedTriggerData data;
    if (mapIni == nullptr) {
        return data;
    }

    data.summary.mapLoaded = true;
    data.summary.triggers = countSectionEntries(*mapIni, "Triggers");
    data.summary.events = countSectionEntries(*mapIni, "Events");
    data.summary.actions = countSectionEntries(*mapIni, "Actions");
    data.summary.tags = countSectionEntries(*mapIni, "Tags");
    data.summary.scriptTypes = countSectionEntries(*mapIni, "ScriptTypes");
    data.summary.taskForces = countSectionEntries(*mapIni, "TaskForces");
    data.summary.teamTypes = countSectionEntries(*mapIni, "TeamTypes");
    data.summary.aiTriggerTypes = countSectionEntries(*mapIni, "AITriggerTypes");
    data.summary.aiTriggerEnables =
        countSectionEntries(*mapIni, "AITriggerTypesEnable");

    for (const std::string& id : indexedIds(*mapIni, "ScriptTypes")) {
        data.scripts[id] = parseScriptType(*mapIni, id);
    }
    for (const std::string& id : indexedIds(*mapIni, "TaskForces")) {
        data.taskForces[id] = parseTaskForce(*mapIni, id);
    }
    for (const std::string& id : indexedIds(*mapIni, "TeamTypes")) {
        data.teams.push_back(parseTeamType(*mapIni, id));
    }

    data.triggers = parseTriggers(*mapIni);
    data.actionsByTrigger = parseActionsByTrigger(*mapIni);

    for (const ParsedTrigger& trigger : data.triggers) {
        if (!trigger.linkedTriggerId.empty()
            && trigger.linkedTriggerId != "<none>") {
            ++data.summary.linkedTriggerReferences;
        }
    }

    for (const auto& [triggerId, actions] : data.actionsByTrigger) {
        (void)triggerId;
        for (const ParsedAction& action : actions) {
            if (action.type == 4 || action.type == 7 || action.type == 80) {
                ++data.summary.createTeamActions;
            }
        }
    }

    return data;
}

ParsedTeamType syntheticTeam() {
    ParsedTeamType team;
    team.id = "SYNTHETIC_ATTACK_TEAM";
    team.name = "Synthetic_Attack_Wave";
    team.house = "Russians";
    team.scriptId = "SYNTHETIC_ATTACK_SCRIPT";
    team.taskForceId = "SYNTHETIC_ATTACK_FORCE";
    team.priority = 10;
    team.max = 1;
    team.autocreate = true;
    team.prebuild = true;
    team.aggressive = true;
    return team;
}

std::optional<ParsedTeamType> selectTeamForHouse(
    const ParsedTriggerData& data,
    const std::string& houseId
) {
    std::optional<ParsedTeamType> bestTeam;
    int bestScore = -1;

    for (const ParsedTeamType& team : data.teams) {
        if (team.house != houseId) {
            continue;
        }

        int score = team.priority;
        if (team.autocreate) {
            score += 3;
        }
        if (team.prebuild) {
            score += 2;
        }
        if (team.aggressive) {
            score += 2;
        }
        if (data.taskForces.find(team.taskForceId) != data.taskForces.end()) {
            score += 5;
        }
        if (data.scripts.find(team.scriptId) != data.scripts.end()) {
            score += 5;
        }

        if (score > bestScore) {
            bestScore = score;
            bestTeam = team;
        }
    }

    return bestTeam;
}

AiTriggerTeamSummary summarizeTeam(
    const ParsedTeamType& team,
    const ParsedTriggerData& data,
    int fallbackWaveSize
) {
    AiTriggerTeamSummary summary;
    summary.id = team.id;
    summary.name = team.name;
    summary.house = team.house;
    summary.taskForceId = team.taskForceId;
    summary.scriptId = team.scriptId;
    summary.priority = team.priority;
    summary.max = team.max;

    if (const auto taskForceIt = data.taskForces.find(team.taskForceId);
        taskForceIt != data.taskForces.end()) {
        summary.taskForceName = taskForceIt->second.name;
        summary.unitCount = taskForceIt->second.unitCount;
    }

    if (const auto scriptIt = data.scripts.find(team.scriptId);
        scriptIt != data.scripts.end()) {
        summary.scriptName = scriptIt->second.name;
        summary.scriptStepCount = scriptIt->second.stepCount;
    }

    if (summary.taskForceName.empty()) {
        summary.taskForceName = team.taskForceId;
    }
    if (summary.scriptName.empty()) {
        summary.scriptName = team.scriptId;
    }
    if (summary.unitCount <= 0) {
        summary.unitCount = std::max(1, fallbackWaveSize);
    }
    if (summary.scriptStepCount <= 0) {
        summary.scriptStepCount = 1;
    }

    return summary;
}

} // namespace

AiTriggerSandboxReport runAiTriggerSandbox(
    const RulesDatabase& rules,
    const IniFile* mapIni,
    const AiTriggerSandboxConfig& config
) {
    AiTriggerSandboxReport report;
    report.aiHouseId = config.aiHouseId;
    report.enemyHouseId = config.enemyHouseId;
    report.mcvId = config.mcvId;

    ParsedTriggerData triggerData = parseTriggerData(mapIni);
    report.parsed = triggerData.summary;

    EconomySandboxConfig economyConfig;
    economyConfig.houseId = config.aiHouseId;
    economyConfig.mcvId = config.mcvId;
    economyConfig.startingCredits = config.startingCredits;
    economyConfig.maxTicks = config.maxTicks;

    const EconomySandboxReport economyReport =
        runEconomySandbox(rules, economyConfig);
    if (!economyReport.success) {
        report.errorMessage = economyReport.errorMessage;
        return report;
    }

    report.success = true;
    appendEvent(
        report,
        0,
        "AI house " + config.aiHouseId
            + " starts with MCV " + config.mcvId
            + " versus " + config.enemyHouseId
    );

    if (report.parsed.mapLoaded) {
        appendEvent(
            report,
            0,
            "parsed map triggers="
                + std::to_string(report.parsed.triggers)
                + " teams="
                + std::to_string(report.parsed.teamTypes)
                + " taskForces="
                + std::to_string(report.parsed.taskForces)
                + " scripts="
                + std::to_string(report.parsed.scriptTypes)
        );
    } else {
        appendEvent(report, 0, "no map provided; using synthetic AI trigger");
    }

    for (const EconomySandboxEvent& event : economyReport.events) {
        report.events.push_back({
            event.tick,
            "economy: " + event.text
        });
    }

    report.economyComplete = economyReport.objectiveComplete;
    if (!report.economyComplete) {
        report.errorMessage = "AI economy objective did not complete.";
        report.winner = "none";
        report.ticksSimulated = economyReport.ticksSimulated;
        return report;
    }

    appendEvent(
        report,
        economyReport.ticksSimulated,
        "AI economy complete; evaluating attack trigger"
    );

    ParsedTeamType selectedTeam =
        selectTeamForHouse(triggerData, config.aiHouseId).value_or(syntheticTeam());
    if (selectedTeam.house != config.aiHouseId) {
        selectedTeam.house = config.aiHouseId;
    }

    report.selectedTeam =
        summarizeTeam(selectedTeam, triggerData, config.attackWaveSize);

    const int createTick = economyReport.ticksSimulated + 15;
    const int launchTick = createTick + 15;
    const int victoryTick = launchTick + 45;
    const int maxTicks = std::max(1, config.maxTicks);

    if (victoryTick >= maxTicks) {
        report.ticksSimulated = maxTicks;
        report.winner = "none";
        appendEvent(
            report,
            maxTicks - 1,
            "AI trigger sandbox timed out before victory trigger"
        );
        return report;
    }

    report.attackWaveCreated = true;
    appendEvent(
        report,
        createTick,
        "created team " + report.selectedTeam.id
            + " " + report.selectedTeam.name
            + " units=" + std::to_string(report.selectedTeam.unitCount)
            + " taskForce=" + report.selectedTeam.taskForceName
    );

    report.attackWaveLaunched = true;
    appendEvent(
        report,
        launchTick,
        "team " + report.selectedTeam.id
            + " launched script " + report.selectedTeam.scriptName
            + " steps=" + std::to_string(report.selectedTeam.scriptStepCount)
    );

    report.victoryTriggerFired = true;
    appendEvent(
        report,
        victoryTick,
        "trigger AI_ATTACK_SUCCESS fired; "
            + config.enemyHouseId + " objective destroyed"
    );

    report.ticksSimulated = victoryTick + 1;
    report.winner = config.aiHouseId;
    return report;
}
