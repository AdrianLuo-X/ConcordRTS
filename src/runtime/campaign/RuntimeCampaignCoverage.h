#pragma once

#include "core/io/IniFile.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>

struct RuntimeCampaignCoverageInventory {
    std::set<int> eventTypes;
    std::set<int> actionTypes;
    std::set<int> teamScriptActionTypes;
    std::set<int> movieIndexes;
    std::vector<std::string> parseErrors;
    int victoryActionCount = 0;
    int defeatActionCount = 0;
    int reachableVictoryActionCount = 0;
    int reachableDefeatActionCount = 0;
};

inline bool runtimeCampaignCoverageEventTypeSupported(int type) {
    switch (type) {
    case 0:
    case 1:
    case 2:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
    case 32:
    case 33:
    case 36:
    case 37:
    case 39:
    case 40:
    case 41:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
    case 48:
    case 51:
    case 52:
    case 55:
    case 56:
    case 57:
    case 58:
    case 59:
    case 60:
    case 61:
        return true;
    default:
        return false;
    }
}

inline bool runtimeCampaignCoverageActionTypeSupported(int type) {
    switch (type) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
    case 38:
    case 40:
    case 41:
    case 42:
    case 44:
    case 45:
    case 46:
    case 47:
    case 48:
    case 51:
    case 53:
    case 54:
    case 55:
    case 56:
    case 57:
    case 60:
    case 61:
    case 62:
    case 63:
    case 67:
    case 68:
    case 69:
    case 70:
    case 71:
    case 72:
    case 73:
    case 74:
    case 75:
    case 76:
    case 77:
    case 78:
    case 79:
    case 80:
    case 81:
    case 88:
    case 89:
    case 90:
    case 95:
    case 99:
    case 100:
    case 101:
    case 102:
    case 103:
    case 104:
    case 106:
    case 107:
    case 108:
    case 109:
    case 111:
    case 112:
    case 113:
    case 114:
    case 115:
    case 116:
    case 117:
    case 118:
    case 119:
    case 120:
    case 121:
    case 122:
    case 125:
    case 127:
    case 128:
    case 129:
    case 130:
    case 131:
    case 132:
    case 135:
    case 136:
    case 137:
    case 139:
        return true;
    default:
        return false;
    }
}

inline bool runtimeCampaignCoverageTeamScriptActionSupported(int action) {
    switch (action) {
    case 0:
    case 1:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 23:
    case 24:
    case 25:
    case 34:
    case 37:
    case 38:
    case 39:
    case 40:
    case 41:
    case 42:
    case 43:
    case 45:
    case 46:
    case 47:
    case 48:
    case 49:
    case 50:
    case 53:
    case 54:
    case 55:
    case 56:
    case 57:
    case 58:
    case 59:
    case 60:
    case 61:
    case 62:
    case 63:
    case 64:
        return true;
    default:
        return false;
    }
}

namespace runtime_campaign_coverage_detail {

inline std::string trim(std::string value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

inline std::string lower(std::string value) {
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

inline bool parseInt(const std::string& value, int& output) {
    try {
        const std::string text = trim(value);
        std::size_t consumed = 0U;
        const int parsed = std::stoi(text, &consumed);
        if (consumed != text.size()) return false;
        output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

inline std::vector<std::string> splitCsv(const std::string& value) {
    std::vector<std::string> tokens;
    std::size_t start = 0U;
    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        tokens.push_back(trim(value.substr(
            start,
            comma == std::string::npos
                ? std::string::npos
                : comma - start
        )));
        if (comma == std::string::npos) break;
        start = comma + 1U;
    }
    return tokens;
}

inline bool isVictoryAction(int type) {
    return type == 1 || type == 67;
}

inline bool isDefeatAction(int type) {
    return type == 2 || type == 68;
}

} // namespace runtime_campaign_coverage_detail

inline RuntimeCampaignCoverageInventory inspectRuntimeCampaignCoverage(
    const IniFile& ini
) {
    using namespace runtime_campaign_coverage_detail;
    RuntimeCampaignCoverageInventory inventory;
    std::set<std::string> triggerIds;
    std::set<std::string> initiallyEnabled;
    std::map<std::string, int> eventCountByTrigger;
    std::map<std::string, std::vector<std::string>> enabledTargetsByTrigger;
    std::map<std::string, std::vector<int>> resultTypesByTrigger;
    std::vector<std::pair<std::string, std::string>> triggerTargets;

    if (const IniFile::Section* triggers = ini.getSection("Triggers")) {
        for (const auto& [rawId, value] : *triggers) {
            const std::string id = lower(trim(rawId));
            const std::vector<std::string> tokens = splitCsv(value);
            if (id.empty() || tokens.size() < 4U) {
                inventory.parseErrors.push_back(
                    "malformed [Triggers] entry " + rawId
                );
                continue;
            }
            triggerIds.insert(id);
            int disabled = 0;
            if (!parseInt(tokens[3], disabled)) {
                inventory.parseErrors.push_back(
                    "invalid disabled flag for trigger " + rawId
                );
            } else if (disabled == 0) {
                initiallyEnabled.insert(id);
            }
        }
    } else {
        inventory.parseErrors.push_back("missing [Triggers] section");
    }

    if (const IniFile::Section* events = ini.getSection("Events")) {
        for (const auto& [rawId, value] : *events) {
            const std::string id = lower(trim(rawId));
            const std::vector<std::string> tokens = splitCsv(value);
            int count = -1;
            if (tokens.empty() || !parseInt(tokens[0], count) || count < 0) {
                inventory.parseErrors.push_back(
                    "invalid [Events] count for " + rawId
                );
                continue;
            }
            std::size_t offset = 1U;
            int parsedCount = 0;
            for (int index = 0; index < count; ++index) {
                int type = -1;
                int parameterCount = -1;
                if (offset + 1U >= tokens.size()
                    || !parseInt(tokens[offset], type)
                    || !parseInt(tokens[offset + 1U], parameterCount)) {
                    inventory.parseErrors.push_back(
                        "truncated [Events] entry " + rawId
                    );
                    break;
                }
                const std::size_t width = parameterCount == 2 ? 4U : 3U;
                if (offset + width > tokens.size()) {
                    inventory.parseErrors.push_back(
                        "truncated [Events] payload " + rawId
                    );
                    break;
                }
                inventory.eventTypes.insert(type);
                ++parsedCount;
                offset += width;
            }
            eventCountByTrigger[id] = parsedCount;
            if (parsedCount != count) {
                inventory.parseErrors.push_back(
                    "[Events] declared/parsed mismatch for " + rawId
                );
            }
        }
    } else {
        inventory.parseErrors.push_back("missing [Events] section");
    }

    if (const IniFile::Section* actions = ini.getSection("Actions")) {
        for (const auto& [rawId, value] : *actions) {
            const std::string id = lower(trim(rawId));
            const std::vector<std::string> tokens = splitCsv(value);
            int count = -1;
            if (tokens.empty() || !parseInt(tokens[0], count) || count < 0) {
                inventory.parseErrors.push_back(
                    "invalid [Actions] count for " + rawId
                );
                continue;
            }
            int parsedCount = 0;
            for (int index = 0; index < count; ++index) {
                const std::size_t offset =
                    1U + static_cast<std::size_t>(index) * 8U;
                int type = -1;
                if (offset + 8U > tokens.size()
                    || !parseInt(tokens[offset], type)) {
                    inventory.parseErrors.push_back(
                        "truncated [Actions] entry " + rawId
                    );
                    break;
                }
                inventory.actionTypes.insert(type);
                ++parsedCount;
                if (type == 10 || type == 100 || type == 117) {
                    int movieIndex = -1;
                    if (!parseInt(tokens[offset + 2U], movieIndex)
                        || movieIndex < 0) {
                        inventory.parseErrors.push_back(
                            "invalid movie index for action "
                            + std::to_string(type) + " on " + rawId
                        );
                    } else {
                        inventory.movieIndexes.insert(movieIndex);
                    }
                }
                if (isVictoryAction(type)) {
                    ++inventory.victoryActionCount;
                    resultTypesByTrigger[id].push_back(type);
                } else if (isDefeatAction(type)) {
                    ++inventory.defeatActionCount;
                    resultTypesByTrigger[id].push_back(type);
                }
                if (type == 53 || type == 54) {
                    const std::string target = lower(trim(tokens[offset + 2U]));
                    const bool nullTarget = target.empty()
                        || target == "0" || target == "<none>"
                        || target == "none" || target == "-";
                    if (!nullTarget) {
                        triggerTargets.emplace_back(rawId, target);
                        if (type == 53) {
                            enabledTargetsByTrigger[id].push_back(target);
                        }
                    }
                }
            }
            if (parsedCount != count) {
                inventory.parseErrors.push_back(
                    "[Actions] declared/parsed mismatch for " + rawId
                );
            }
        }
    } else {
        inventory.parseErrors.push_back("missing [Actions] section");
    }

    if (const IniFile::Section* scriptTypes = ini.getSection("ScriptTypes")) {
        for (const auto& [unusedIndex, rawScriptId] : *scriptTypes) {
            (void)unusedIndex;
            const std::string scriptId = trim(rawScriptId);
            const IniFile::Section* script = ini.getSection(scriptId);
            if (script == nullptr) {
                inventory.parseErrors.push_back(
                    "missing Team Script section " + scriptId
                );
                continue;
            }
            for (const auto& [stepKey, value] : *script) {
                int stepIndex = -1;
                if (!parseInt(stepKey, stepIndex)) continue;
                const std::vector<std::string> tokens = splitCsv(value);
                int action = -1;
                if (tokens.size() < 2U || !parseInt(tokens[0], action)) {
                    inventory.parseErrors.push_back(
                        "malformed Team Script step " + scriptId + "/"
                        + stepKey
                    );
                    continue;
                }
                inventory.teamScriptActionTypes.insert(action);
            }
        }
    } else {
        inventory.parseErrors.push_back("missing [ScriptTypes] section");
    }

    for (const auto& [source, target] : triggerTargets) {
        if (target.empty() || triggerIds.find(target) == triggerIds.end()) {
            inventory.parseErrors.push_back(
                "trigger action target missing " + source + " -> " + target
            );
        }
    }

    std::set<std::string> reachable = initiallyEnabled;
    bool changed = true;
    while (changed) {
        changed = false;
        const std::vector<std::string> snapshot(
            reachable.begin(), reachable.end()
        );
        for (const std::string& source : snapshot) {
            const auto targets = enabledTargetsByTrigger.find(source);
            if (targets == enabledTargetsByTrigger.end()) continue;
            for (const std::string& target : targets->second) {
                if (triggerIds.find(target) != triggerIds.end()
                    && reachable.insert(target).second) {
                    changed = true;
                }
            }
        }
    }
    for (const auto& [triggerId, resultTypes] : resultTypesByTrigger) {
        const auto eventCount = eventCountByTrigger.find(triggerId);
        if (triggerIds.find(triggerId) == triggerIds.end()
            || eventCount == eventCountByTrigger.end()
            || eventCount->second <= 0) {
            inventory.parseErrors.push_back(
                "result action is not bound to an authored event " + triggerId
            );
            continue;
        }
        if (reachable.find(triggerId) == reachable.end()) continue;
        for (const int type : resultTypes) {
            if (isVictoryAction(type)) {
                ++inventory.reachableVictoryActionCount;
            } else if (isDefeatAction(type)) {
                ++inventory.reachableDefeatActionCount;
            }
        }
    }
    return inventory;
}

inline std::vector<int> unsupportedRuntimeCampaignCoverageEventTypes(
    const RuntimeCampaignCoverageInventory& inventory
) {
    std::vector<int> unsupported;
    for (const int type : inventory.eventTypes) {
        if (!runtimeCampaignCoverageEventTypeSupported(type)) {
            unsupported.push_back(type);
        }
    }
    return unsupported;
}

inline std::vector<int> unsupportedRuntimeCampaignCoverageActionTypes(
    const RuntimeCampaignCoverageInventory& inventory
) {
    std::vector<int> unsupported;
    for (const int type : inventory.actionTypes) {
        if (!runtimeCampaignCoverageActionTypeSupported(type)) {
            unsupported.push_back(type);
        }
    }
    return unsupported;
}

inline std::vector<int> unsupportedRuntimeCampaignCoverageTeamScriptActions(
    const RuntimeCampaignCoverageInventory& inventory
) {
    std::vector<int> unsupported;
    for (const int action : inventory.teamScriptActionTypes) {
        if (!runtimeCampaignCoverageTeamScriptActionSupported(action)) {
            unsupported.push_back(action);
        }
    }
    return unsupported;
}
