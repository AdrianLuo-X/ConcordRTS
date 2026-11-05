#include "simulation/triggers/RuntimeTriggerPersistence.h"
#include "core/io/IniFile.h"
#include "core/io/RuntimeSaveTextFields.h"
#include <algorithm>
#include <bit>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_WINSOCKAPI_)
#error H039 trigger persistence contract included a Windows surface
#endif
using namespace runtime_save_text;
struct RuntimeBuildCatalogItem { std::string typeId; int techLevel=0; };
struct FixtureSession {
    RuntimeTriggerState triggers;
    struct Economy { std::vector<RuntimeBuildCatalogItem> catalog; } economy;
    int tick=40;bool campaignMode=true;
    struct Rules {} rules;
};
struct RecordingOperations final : RuntimeTriggerPersistenceOperations {
    FixtureSession& session_;
    mutable std::vector<std::string> trace;
    int failAt=0;bool missingUnit=false;
    explicit RecordingOperations(FixtureSession& value) : session_(value) {}
    void mark(const std::string& event) const {
        trace.push_back(event);
        if (failAt>0 && trace.size()==static_cast<std::size_t>(failAt))
            throw std::runtime_error("planned-domain-operation");
    }
    bool objectExists(int id) const override {
        mark("object:"+std::to_string(id));return id==1 || id==2 || id==5 || id==7;
    }
    const RuntimeTeamType* findTeam(const std::string& id) const override {
        mark("team:"+id);const auto it=session_.triggers.teams.find(toLowerAsciiLocal(trimAsciiLocal(id)));
        return it==session_.triggers.teams.end()?nullptr:&it->second;
    }
    std::string resolveHouse(const std::string& name) const override {
        mark("house:"+name);
        // Supplied lookup data only; no second production owner algorithm.
        const std::map<std::string,std::string> names{{"Alias","HOUSE-A"},{"HouseA","HOUSE-A"},{"",""}};
        const auto it=names.find(name);return it==names.end()?name:it->second;
    }
    void applyTechLevel(const std::string& typeKey,int techLevel) override {
        mark("catalog:"+typeKey+":"+std::to_string(techLevel));
            const auto item = std::find_if(
                session_.economy.catalog.begin(),
                session_.economy.catalog.end(),
                [&typeKey](const RuntimeBuildCatalogItem& candidate) {
                    return toLowerAsciiLocal(candidate.typeId) == typeKey;
                }
            );
            if (item != session_.economy.catalog.end()) {
                item->techLevel = techLevel;
            }
    }
    bool unitTypeExists(const std::string& id) const override {
        mark("unit:"+id);return !missingUnit && id=="UNIT-A";
    }
};
namespace frozen_original {
static RecordingOperations* bound=nullptr;
const int* findRuntimeObjectByIdIncludingInactive(FixtureSession&,int id) {
    static const int present=1;return bound->objectExists(id)?&present:nullptr;
}
const RuntimeTeamType* findRuntimeTriggerTeamById(FixtureSession&,const std::string& id) { return bound->findTeam(id); }
std::string runtimeCampaignHouseName(FixtureSession&,const std::string& id) { return bound->resolveHouse(id); }
const int* findRuntimeUnitTypeById(const FixtureSession::Rules&,const std::string& id) {
    static const int present=1;return bound->unitTypeExists(id)?&present:nullptr;
}
std::vector<std::string> splitRuntimeTokenList(const std::string& value) {
    std::vector<std::string> tokens;
    std::string current;
    for (char character : value) {
        if (character == ',' || character == ';') {
            const std::string token = trimAsciiLocal(current);
            if (!token.empty()) {
                tokens.push_back(token);
            }
            current.clear();
        } else {
            current.push_back(character);
        }
    }

    const std::string token = trimAsciiLocal(current);
    if (!token.empty()) {
        tokens.push_back(token);
    }
    return tokens;
}
int parseRuntimeIntOrDefault(const std::string& value, int fallback) {
    int parsed = fallback;
    return parseRuntimeInt(value, parsed) ? parsed : fallback;
}
bool parseRuntimeBool(const std::string& value) {
    const std::string normalized = toLowerAsciiLocal(trimAsciiLocal(value));
    return normalized == "yes"
        || normalized == "true"
        || normalized == "1";
}
std::string runtimeBoolText(bool value) {
    return value ? "yes" : "no";
}
void writeCollections(const FixtureSession& state,std::ostream& output) {
            output << "[CampaignTriggers]\n";
            output << "Count=" << state.triggers.triggers.size() << "\n";
            for (std::size_t index = 0;
                index < state.triggers.triggers.size();
                ++index) {
                const RuntimeTriggerDefinition& trigger =
                    state.triggers.triggers[index];
                output << index << "="
                       << runtimeSaveField(trigger.id) << "|"
                       << runtimeBoolText(trigger.fired) << "|"
                       << runtimeBoolText(trigger.enabled) << "|"
                       << runtimeBoolText(trigger.repeating) << "|"
                       << trigger.activationTick << "|"
                       << trigger.lastFiredTick << "|"
                       << trigger.nextEligibleTick << "|";
                bool firstRemainingTarget = true;
                for (int objectId : trigger.remainingOnceAllObjectIds) {
                    if (!firstRemainingTarget) {
                        output << ",";
                    }
                    firstRemainingTarget = false;
                    output << objectId;
                }
                output << "\n";
            }
            output << "\n[CampaignVariables]\n";
            output << "Count="
                   << state.triggers.campaignLocalVariables.size()
                   << "\n";
            std::size_t variableIndex = 0;
            for (const auto& [variable, variableValue] :
                    state.triggers.campaignLocalVariables) {
                output << variableIndex++ << "="
                       << variable << "|"
                       << variableValue << "\n";
            }
            output << "\n[CampaignGlobals]\n";
            output << "Count="
                   << state.triggers.campaignGlobalVariables.size()
                   << "\n";
            std::size_t globalIndex = 0;
            for (const auto& [variable, variableValue] :
                    state.triggers.campaignGlobalVariables) {
                output << globalIndex++ << "="
                       << variable << "|"
                       << variableValue << "\n";
            }
            output << "\n[CampaignTeams]\n";
            output << "Count="
                   << state.triggers.campaignTeamObjects.size()
                   << "\n";
            std::size_t teamIndex = 0;
            for (const auto& [teamId, objectIds] :
                    state.triggers.campaignTeamObjects) {
                output << teamIndex++ << "="
                       << runtimeSaveField(teamId) << "|";
                bool firstObject = true;
                for (int objectId : objectIds) {
                    if (!firstObject) {
                        output << ",";
                    }
                    firstObject = false;
                    output << objectId;
                }
                output << "\n";
            }
            output << "\n[CampaignTechLevels]\n";
            output << "Count="
                   << state.triggers.campaignTechLevelByType.size()
                   << "\n";
            std::size_t techLevelIndex = 0;
            for (const auto& [typeId, techLevel] :
                    state.triggers.campaignTechLevelByType) {
                output << techLevelIndex++ << "="
                       << runtimeSaveField(typeId) << "|"
                       << techLevel << "\n";
            }
            output << "\n[CampaignBaseCenters]\n";
            output << "Count="
                   << state.triggers.campaignBaseCenterByOwner.size()
                   << "\n";
            std::size_t baseCenterIndex = 0;
            for (const auto& [owner, cell] :
                    state.triggers.campaignBaseCenterByOwner) {
                output << baseCenterIndex++ << "="
                       << runtimeSaveField(owner) << "|"
                       << cell.x << "|" << cell.y << "\n";
            }
            output << "\n[CampaignSuperWeaponTargets]\n";
            output << "Count="
                   << state.triggers
                        .campaignPreferredSuperWeaponTargetByOwner.size()
                   << "\n";
            std::size_t preferredTargetIndex = 0;
            for (const auto& [owner, cell] : state.triggers
                    .campaignPreferredSuperWeaponTargetByOwner) {
                output << preferredTargetIndex++ << "="
                       << runtimeSaveField(owner) << "|"
                       << cell.x << "|" << cell.y << "\n";
            }
            output << "\n[CampaignAlliances]\n";
            output << "Schema=2\n";
            std::vector<std::pair<std::string, std::string>> savedAlliances;
            for (const std::string& pair : state.triggers.campaignAlliedPairs) {
                const std::size_t separator = pair.find('|');
                if (separator == std::string::npos
                    || pair.find('|', separator + 1U) != std::string::npos) {
                    continue;
                }
                std::string left = toLowerAsciiLocal(trimAsciiLocal(
                    pair.substr(0, separator)
                ));
                std::string right = toLowerAsciiLocal(trimAsciiLocal(
                    pair.substr(separator + 1U)
                ));
                if (left.empty() || right.empty() || left == right) {
                    continue;
                }
                if (right < left) {
                    std::swap(left, right);
                }
                savedAlliances.emplace_back(std::move(left), std::move(right));
            }
            std::sort(savedAlliances.begin(), savedAlliances.end());
            savedAlliances.erase(
                std::unique(savedAlliances.begin(), savedAlliances.end()),
                savedAlliances.end()
            );
            output << "Count=" << savedAlliances.size() << "\n";
            for (std::size_t allianceIndex = 0;
                allianceIndex < savedAlliances.size();
                ++allianceIndex) {
                output << allianceIndex << "="
                       << runtimeSaveField(savedAlliances[allianceIndex].first)
                       << "|"
                       << runtimeSaveField(savedAlliances[allianceIndex].second)
                       << "\n";
            }
            output << "\n[CampaignHouseAI]\n";
            output << "Count="
                   << state.triggers.campaignHouseProduction.size() << "\n";
            std::size_t houseAiIndex = 0;
            for (const auto& [houseKey, house] :
                    state.triggers.campaignHouseProduction) {
                (void)houseKey;
                output << houseAiIndex++ << "="
                       << runtimeSaveField(house.owner) << "|"
                       << runtimeSaveField(house.country) << "|"
                       << house.techLevel << "|" << house.iq << "|"
                       << house.side << "|" << house.credits << "|"
                       << house.nextAiTriggerCheckTick << "|"
                       << house.selectionSerial << "|"
                       << house.activeBuildId << "|"
                       << runtimeBoolText(house.playerControlled) << "|"
                       << runtimeBoolText(house.productionStarted) << "|"
                       << runtimeBoolText(house.aiTriggersEnabled) << "|"
                       << house.preferredTargetQuarry << "|"
                       << house.aiTriggerTeamRatio << "|"
                       << house.teamAircraftRatio << "|"
                       << house.teamInfantryRatio << "|"
                       << house.teamUnitRatio << "\n";
            }

            output << "\n[CampaignAITriggers]\n";
            output << "Count="
                   << state.triggers.campaignAiTriggers.size() << "\n";
            for (std::size_t index = 0;
                index < state.triggers.campaignAiTriggers.size();
                ++index) {
                const RuntimeCampaignAiTriggerDefinition& aiTrigger =
                    state.triggers.campaignAiTriggers[index];
                output << index << "=" << runtimeSaveField(aiTrigger.id) << "|"
                       << aiTrigger.currentWeight << "|"
                       << aiTrigger.timesExecuted << "|"
                       << aiTrigger.timesCompleted << "|"
                       << aiTrigger.timesFailed << "|"
                       << aiTrigger.nextEligibleTick << "|"
                       << runtimeBoolText(aiTrigger.enabled) << "\n";
            }

            output << "\n[CampaignTeamBuilds]\n";
            output << "Count=" << state.triggers.campaignTeamBuilds.size()
                   << "\n";
            output << "NextInstanceId="
                   << state.triggers.nextCampaignTeamBuildId << "\n";
            std::size_t campaignBuildIndex = 0;
            for (const auto& [buildId, build] :
                    state.triggers.campaignTeamBuilds) {
                output << campaignBuildIndex++ << "=" << buildId << "|"
                       << runtimeSaveField(build.aiTriggerId) << "|"
                       << runtimeSaveField(build.teamTypeId) << "|"
                       << runtimeSaveField(build.owner) << "|"
                       << build.memberIndex << "|"
                       << build.memberProduced << "|"
                       << build.remainingTicks << "|"
                       << build.paidCost << "|"
                       << runtimeSaveField(build.activeTypeId) << "|"
                       << build.deadlineTick << "|"
                       << runtimeBoolText(build.completed) << "|"
                       << runtimeBoolText(build.failed) << "|"
                       << runtimeSaveField(build.status) << "|";
                for (std::size_t objectIndex = 0;
                    objectIndex < build.objectIds.size();
                    ++objectIndex) {
                    if (objectIndex > 0) {
                        output << ",";
                    }
                    output << build.objectIds[objectIndex];
                }
                output << "\n";
            }
            output << "\n";
}
void writeMissions(const FixtureSession& state,std::ostream& output) {
        output << "[TeamMissions]\n";
        output << "Count=" << state.triggers.teamMissions.size() << "\n";
        output << "NextInstanceId="
               << state.triggers.nextTeamMissionInstanceId << "\n";
        std::size_t teamMissionIndex = 0;
        for (const auto& [missionId, mission] :
                state.triggers.teamMissions) {
            std::vector<int> objectIds = mission.objectIds;
            std::sort(objectIds.begin(), objectIds.end());
            output << teamMissionIndex++ << "="
                   << missionId << "|"
                   << runtimeSaveField(mission.teamTypeId) << "|"
                   << runtimeSaveField(mission.scriptId) << "|"
                   << mission.stepIndex << "|"
                   << mission.stepStartedTick << "|"
                   << runtimeBoolText(mission.stepIssued) << "|"
                   << mission.targetObjectId << "|"
                   << mission.targetCell.x << "|"
                   << mission.targetCell.y << "|"
                   << runtimeBoolText(mission.completed) << "|"
                   << runtimeSaveField(mission.status) << "|";
            for (std::size_t objectIndex = 0;
                objectIndex < objectIds.size();
                ++objectIndex) {
                if (objectIndex > 0) {
                    output << ",";
                }
                output << objectIds[objectIndex];
            }
            output << "|" << runtimeSaveField(mission.sourceAiTriggerId)
                   << "|" << mission.sourceAiBuildId
                   << "|" << runtimeBoolText(mission.aiOutcomeReported)
                   << "|" << runtimeBoolText(mission.achievedGreatSuccess)
                   << "|" << runtimeBoolText(mission.targetInteractionCompleted)
                   << "|";
            bool firstReturn = true;
            for (const auto& [id, cell] : mission.transportReturnCells) {
                if (!firstReturn) output << ";";
                firstReturn = false;
                output << id << "," << cell.x << "," << cell.y;
            }
            output << "|" << runtimeBoolText(mission.regrouping) << "|" << mission.regroupCell.x
                   << "|" << mission.regroupCell.y << "|" << mission.regroupStartedTick << "|";
            bool firstRegroup = true;
            for (const auto& [id, cell] : mission.regroupDestinations) {
                if (!firstRegroup) output << ";";
                firstRegroup = false;
                output << id << "," << cell.x << "," << cell.y;
            }
            output << "\n";
        }
        output << "\n";

}
void readCollections(FixtureSession& state,const IniFile& ini,const FixtureSession* validatedLateState) {
        state.triggers.campaignLocalVariables.clear();
        const int variableCount = parseRuntimeIntOrDefault(
            ini.getValue("CampaignVariables", "Count", "0"),
            0
        );
        for (int index = 0; index < variableCount; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue("CampaignVariables", std::to_string(index), "")
            );
            if (tokens.size() < 2) {
                continue;
            }
            const int variable = parseRuntimeIntOrDefault(tokens[0], 0);
            state.triggers.campaignLocalVariables[variable] =
                parseRuntimeIntOrDefault(tokens[1], 0);
        }

        state.triggers.campaignGlobalVariables.clear();
        const int globalCount = parseRuntimeIntOrDefault(
            ini.getValue("CampaignGlobals", "Count", "0"),
            0
        );
        for (int index = 0; index < globalCount; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue("CampaignGlobals", std::to_string(index), "")
            );
            if (tokens.size() < 2) {
                continue;
            }
            const int variable = parseRuntimeIntOrDefault(tokens[0], 0);
            state.triggers.campaignGlobalVariables[variable] =
                parseRuntimeIntOrDefault(tokens[1], 0);
        }

        state.triggers.campaignTeamObjects.clear();
        const int campaignTeamCount = parseRuntimeIntOrDefault(
            ini.getValue("CampaignTeams", "Count", "0"),
            0
        );
        for (int index = 0; index < campaignTeamCount; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue("CampaignTeams", std::to_string(index), "")
            );
            if (tokens.size() < 2 || tokens[0].empty()) {
                continue;
            }
            std::unordered_set<int>& teamObjects =
                state.triggers.campaignTeamObjects[
                    toLowerAsciiLocal(trimAsciiLocal(tokens[0]))
                ];
            std::stringstream objectStream(tokens[1]);
            std::string objectToken;
            while (std::getline(objectStream, objectToken, ',')) {
                int objectId = -1;
                if (parseRuntimeInt(objectToken, objectId)
                    && objectId >= 0
                    && findRuntimeObjectByIdIncludingInactive(
                        state,
                        objectId
                    ) != nullptr) {
                    teamObjects.insert(objectId);
                }
            }
        }

        state.triggers.campaignTechLevelByType.clear();
        const int campaignTechLevelCount = parseRuntimeIntOrDefault(
            ini.getValue("CampaignTechLevels", "Count", "0"),
            0
        );
        for (int index = 0; index < campaignTechLevelCount; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue(
                    "CampaignTechLevels",
                    std::to_string(index),
                    ""
                )
            );
            if (tokens.size() < 2 || tokens[0].empty()) {
                continue;
            }
            const std::string typeKey = toLowerAsciiLocal(tokens[0]);
            const int techLevel = parseRuntimeIntOrDefault(tokens[1], -1);
            state.triggers.campaignTechLevelByType[typeKey] = techLevel;
            bound->mark("catalog:"+typeKey+":"+std::to_string(techLevel));
            const auto item = std::find_if(
                state.economy.catalog.begin(),
                state.economy.catalog.end(),
                [&typeKey](const RuntimeBuildCatalogItem& candidate) {
                    return toLowerAsciiLocal(candidate.typeId) == typeKey;
                }
            );
            if (item != state.economy.catalog.end()) {
                item->techLevel = techLevel;
            }
        }

        state.triggers.campaignBaseCenterByOwner.clear();
        const int campaignBaseCenterCount = parseRuntimeIntOrDefault(
            ini.getValue("CampaignBaseCenters", "Count", "0"),
            0
        );
        for (int index = 0; index < campaignBaseCenterCount; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue(
                    "CampaignBaseCenters",
                    std::to_string(index),
                    ""
                )
            );
            if (tokens.size() < 3 || tokens[0].empty()) {
                continue;
            }
            const RuntimeCellPoint cell{
                parseRuntimeIntOrDefault(tokens[1], -1),
                parseRuntimeIntOrDefault(tokens[2], -1)
            };
            if (cell.x >= 0 && cell.y >= 0) {
                state.triggers.campaignBaseCenterByOwner[
                    toLowerAsciiLocal(tokens[0])
                ] = cell;
            }
        }

        state.triggers.campaignPreferredSuperWeaponTargetByOwner.clear();
        const int preferredTargetCount = parseRuntimeIntOrDefault(
            ini.getValue("CampaignSuperWeaponTargets", "Count", "0"),
            0
        );
        for (int index = 0; index < preferredTargetCount; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue(
                    "CampaignSuperWeaponTargets",
                    std::to_string(index),
                    ""
                )
            );
            if (tokens.size() < 3 || tokens[0].empty()) {
                continue;
            }
            const RuntimeCellPoint cell{
                parseRuntimeIntOrDefault(tokens[1], -1),
                parseRuntimeIntOrDefault(tokens[2], -1)
            };
            if (cell.x >= 0 && cell.y >= 0) {
                state.triggers.campaignPreferredSuperWeaponTargetByOwner[
                    toLowerAsciiLocal(trimAsciiLocal(tokens[0]))
                ] = cell;
            }
        }

        state.triggers.campaignAlliedPairs =
            validatedLateState->triggers.campaignAlliedPairs;

        const int campaignHouseAiCount = std::max(
            0,
            parseRuntimeIntOrDefault(
                ini.getValue("CampaignHouseAI", "Count", "0"),
                0
            )
        );
        for (int index = 0; index < campaignHouseAiCount; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue("CampaignHouseAI", std::to_string(index), "")
            );
            if (tokens.size() < 12 || tokens[0].empty()) {
                continue;
            }
            const std::string resolvedOwner = runtimeCampaignHouseName(
                state,
                tokens[0]
            );
            const auto house = state.triggers.campaignHouseProduction.find(
                toLowerAsciiLocal(trimAsciiLocal(resolvedOwner))
            );
            if (house == state.triggers.campaignHouseProduction.end()) {
                continue;
            }
            RuntimeCampaignHouseProductionState& restored = house->second;
            restored.country = tokens[1].empty()
                ? restored.country
                : trimAsciiLocal(tokens[1]);
            restored.techLevel = std::max(
                0,
                parseRuntimeIntOrDefault(tokens[2], restored.techLevel)
            );
            restored.iq = std::max(
                0,
                parseRuntimeIntOrDefault(tokens[3], restored.iq)
            );
            restored.side = parseRuntimeIntOrDefault(tokens[4], restored.side);
            restored.credits = std::max(
                0,
                parseRuntimeIntOrDefault(tokens[5], restored.credits)
            );
            restored.nextAiTriggerCheckTick = std::max(
                0,
                parseRuntimeIntOrDefault(
                    tokens[6],
                    restored.nextAiTriggerCheckTick
                )
            );
            restored.selectionSerial = std::max(
                0,
                parseRuntimeIntOrDefault(tokens[7], restored.selectionSerial)
            );
            restored.activeBuildId = parseRuntimeIntOrDefault(
                tokens[8],
                restored.activeBuildId
            );
            restored.playerControlled = parseRuntimeBool(tokens[9]);
            restored.productionStarted = parseRuntimeBool(tokens[10]);
            restored.aiTriggersEnabled = parseRuntimeBool(tokens[11]);
            if (tokens.size() >= 17U) {
                restored.preferredTargetQuarry = std::clamp(
                    parseRuntimeIntOrDefault(tokens[12], 0),
                    0,
                    9
                );
                restored.aiTriggerTeamRatio = std::clamp(
                    parseRuntimeIntOrDefault(tokens[13], 100),
                    0,
                    100
                );
                restored.teamAircraftRatio = std::clamp(
                    parseRuntimeIntOrDefault(tokens[14], 100),
                    0,
                    100
                );
                restored.teamInfantryRatio = std::clamp(
                    parseRuntimeIntOrDefault(tokens[15], 100),
                    0,
                    100
                );
                restored.teamUnitRatio = std::clamp(
                    parseRuntimeIntOrDefault(tokens[16], 100),
                    0,
                    100
                );
            }
        }

        const int campaignAiTriggerCount = std::max(
            0,
            parseRuntimeIntOrDefault(
                ini.getValue("CampaignAITriggers", "Count", "0"),
                0
            )
        );
        for (int index = 0; index < campaignAiTriggerCount; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue(
                    "CampaignAITriggers",
                    std::to_string(index),
                    ""
                )
            );
            if (tokens.size() < 7 || tokens[0].empty()) {
                continue;
            }
            const std::string triggerKey =
                toLowerAsciiLocal(trimAsciiLocal(tokens[0]));
            const auto trigger = std::find_if(
                state.triggers.campaignAiTriggers.begin(),
                state.triggers.campaignAiTriggers.end(),
                [&triggerKey](
                    const RuntimeCampaignAiTriggerDefinition& candidate
                ) {
                    return toLowerAsciiLocal(trimAsciiLocal(candidate.id))
                        == triggerKey;
                }
            );
            if (trigger == state.triggers.campaignAiTriggers.end()) {
                continue;
            }
            trigger->currentWeight = std::clamp(
                parseRuntimeDoubleOrDefault(tokens[1], trigger->currentWeight),
                trigger->minimumWeight,
                trigger->maximumWeight
            );
            trigger->timesExecuted = std::max(
                0,
                parseRuntimeIntOrDefault(tokens[2], trigger->timesExecuted)
            );
            trigger->timesCompleted = std::max(
                0,
                parseRuntimeIntOrDefault(tokens[3], trigger->timesCompleted)
            );
            trigger->timesFailed = std::max(
                0,
                parseRuntimeIntOrDefault(tokens[4], trigger->timesFailed)
            );
            trigger->nextEligibleTick = std::max(
                0,
                parseRuntimeIntOrDefault(
                    tokens[5],
                    trigger->nextEligibleTick
                )
            );
            trigger->enabled = parseRuntimeBool(tokens[6]);
        }

        state.triggers.campaignTeamBuilds.clear();
        state.triggers.nextCampaignTeamBuildId = std::max(
            1,
            parseRuntimeIntOrDefault(
                ini.getValue("CampaignTeamBuilds", "NextInstanceId", "1"),
                1
            )
        );
        const int campaignTeamBuildCount = std::max(
            0,
            parseRuntimeIntOrDefault(
                ini.getValue("CampaignTeamBuilds", "Count", "0"),
                0
            )
        );
        for (int index = 0; index < campaignTeamBuildCount; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue(
                    "CampaignTeamBuilds",
                    std::to_string(index),
                    ""
                )
            );
            if (tokens.size() < 14) {
                continue;
            }
            RuntimeCampaignTeamBuild build;
            build.instanceId = parseRuntimeIntOrDefault(tokens[0], 0);
            build.aiTriggerId = trimAsciiLocal(tokens[1]);
            build.teamTypeId = trimAsciiLocal(tokens[2]);
            build.owner = runtimeCampaignHouseName(state, tokens[3]);
            build.memberIndex = static_cast<std::size_t>(std::max(
                0,
                parseRuntimeIntOrDefault(tokens[4], 0)
            ));
            build.memberProduced = std::max(
                0,
                parseRuntimeIntOrDefault(tokens[5], 0)
            );
            build.remainingTicks = std::max(
                0,
                parseRuntimeIntOrDefault(tokens[6], 0)
            );
            build.paidCost = std::max(
                0,
                parseRuntimeIntOrDefault(tokens[7], 0)
            );
            build.activeTypeId = trimAsciiLocal(tokens[8]);
            build.deadlineTick = std::max(
                state.tick,
                parseRuntimeIntOrDefault(tokens[9], state.tick)
            );
            build.completed = parseRuntimeBool(tokens[10]);
            build.failed = parseRuntimeBool(tokens[11]);
            build.status = tokens[12];
            std::stringstream objectStream(tokens[13]);
            std::string objectToken;
            while (std::getline(objectStream, objectToken, ',')) {
                int objectId = -1;
                if (parseRuntimeInt(objectToken, objectId)
                    && objectId >= 0
                    && findRuntimeObjectByIdIncludingInactive(state, objectId)
                        != nullptr) {
                    build.objectIds.push_back(objectId);
                }
            }
            if (build.instanceId <= 0
                || findRuntimeTriggerTeamById(state, build.teamTypeId)
                    == nullptr
                || build.owner.empty()) {
                continue;
            }
            state.triggers.nextCampaignTeamBuildId = std::max(
                state.triggers.nextCampaignTeamBuildId,
                build.instanceId + 1
            );
            state.triggers.campaignTeamBuilds.emplace(
                build.instanceId,
                std::move(build)
            );
        }

        const int triggerStateCount = parseRuntimeIntOrDefault(
            ini.getValue("CampaignTriggers", "Count", "0"),
            0
        );
        for (int index = 0; index < triggerStateCount; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue("CampaignTriggers", std::to_string(index), "")
            );
            if (tokens.size() < 3) {
                continue;
            }
            RuntimeTriggerDefinition* trigger = runtimeCampaignTriggerById(
                state.triggers,
                tokens[0]
            );
            if (trigger == nullptr) {
                continue;
            }
            trigger->fired = parseRuntimeBool(tokens[1]);
            trigger->enabled = parseRuntimeBool(tokens[2]);
            if (tokens.size() >= 4) {
                trigger->repeating = parseRuntimeBool(tokens[3]);
            }
            if (tokens.size() >= 5) {
                trigger->activationTick = parseRuntimeIntOrDefault(
                    tokens[4],
                    trigger->activationTick
                );
            }
            if (tokens.size() >= 6) {
                trigger->lastFiredTick = parseRuntimeIntOrDefault(
                    tokens[5],
                    trigger->lastFiredTick
                );
            }
            if (tokens.size() >= 7) {
                trigger->nextEligibleTick = parseRuntimeIntOrDefault(
                    tokens[6],
                    trigger->nextEligibleTick
                );
            }
            if (tokens.size() >= 8) {
                trigger->remainingOnceAllObjectIds.clear();
                std::stringstream targetStream(tokens[7]);
                std::string targetToken;
                while (std::getline(targetStream, targetToken, ',')) {
                    int objectId = -1;
                    if (parseRuntimeInt(targetToken, objectId)
                        && objectId >= 0) {
                        trigger->remainingOnceAllObjectIds.insert(objectId);
                    }
                }
            }
        }

}
void readMissions(FixtureSession& state,const IniFile& ini,int version) {
    state.triggers.teamMissions.clear();
    state.triggers.nextTeamMissionInstanceId = std::max(
        1,
        parseRuntimeIntOrDefault(
            ini.getValue("TeamMissions", "NextInstanceId", "1"),
            1
        )
    );
    const int teamMissionCount = std::max(
        0,
        parseRuntimeIntOrDefault(
            ini.getValue("TeamMissions", "Count", "0"),
            0
        )
    );
    for (int index = 0; index < teamMissionCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("TeamMissions", std::to_string(index), "")
        );
        if (tokens.size() < 12) {
            continue;
        }
        RuntimeTeamMissionState mission;
        mission.instanceId = parseRuntimeIntOrDefault(tokens[0], 0);
        mission.teamTypeId = trimAsciiLocal(tokens[1]);
        mission.scriptId = trimAsciiLocal(tokens[2]);
        mission.stepIndex = static_cast<std::size_t>(std::max(
            0,
            parseRuntimeIntOrDefault(tokens[3], 0)
        ));
        mission.stepStartedTick = parseRuntimeIntOrDefault(tokens[4], -1);
        mission.stepIssued = parseRuntimeBool(tokens[5]);
        mission.targetObjectId = parseRuntimeIntOrDefault(tokens[6], -1);
        mission.targetCell = {
            parseRuntimeIntOrDefault(tokens[7], -1),
            parseRuntimeIntOrDefault(tokens[8], -1)
        };
        mission.completed = parseRuntimeBool(tokens[9]);
        mission.status = tokens[10];
        std::stringstream objectStream(tokens[11]);
        std::string objectToken;
        while (std::getline(objectStream, objectToken, ',')) {
            int objectId = -1;
            if (parseRuntimeInt(objectToken, objectId)
                && objectId >= 0
                && findRuntimeObjectByIdIncludingInactive(state, objectId)
                    != nullptr) {
                mission.objectIds.push_back(objectId);
            }
        }
        if (tokens.size() >= 13) {
            mission.sourceAiTriggerId = trimAsciiLocal(tokens[12]);
        }
        if (tokens.size() >= 14) {
            mission.sourceAiBuildId = parseRuntimeIntOrDefault(
                tokens[13],
                -1
            );
        }
        if (tokens.size() >= 15) {
            mission.aiOutcomeReported = parseRuntimeBool(tokens[14]);
        }
        if (tokens.size() >= 16) {
            mission.achievedGreatSuccess = parseRuntimeBool(tokens[15]);
        }
        if (tokens.size() >= 17) {
            mission.targetInteractionCompleted = parseRuntimeBool(tokens[16]);
        }
        if (tokens.size() >= 18) {
            std::stringstream returnStream(tokens[17]);
            std::string entry;
            while (std::getline(returnStream, entry, ';')) {
                const auto fields = splitRuntimeTokenList(entry);
                if (fields.size() != 3) continue;
                const int id = parseRuntimeIntOrDefault(fields[0], -1);
                const int x = parseRuntimeIntOrDefault(fields[1], -1);
                const int y = parseRuntimeIntOrDefault(fields[2], -1);
                if (id >= 0 && x >= 0 && y >= 0
                    && findRuntimeObjectByIdIncludingInactive(state, id) != nullptr)
                    mission.transportReturnCells[id] = {x, y};
            }
        }
        if (tokens.size() >= 23) {
            mission.regrouping = parseRuntimeBool(tokens[18]);
            mission.regroupCell = {parseRuntimeIntOrDefault(tokens[19], -1), parseRuntimeIntOrDefault(tokens[20], -1)};
            mission.regroupStartedTick = parseRuntimeIntOrDefault(tokens[21], -1);
            std::stringstream regroupStream(tokens[22]); std::string entry;
            while (std::getline(regroupStream, entry, ';')) {
                const auto fields = splitRuntimeTokenList(entry);
                if (fields.size() != 3) continue;
                const int id = parseRuntimeIntOrDefault(fields[0], -1);
                const int x = parseRuntimeIntOrDefault(fields[1], -1), y = parseRuntimeIntOrDefault(fields[2], -1);
                if (id >= 0 && x >= 0 && y >= 0 && findRuntimeObjectByIdIncludingInactive(state, id))
                    mission.regroupDestinations[id] = {x, y};
            }
        }
        // An authored team can have no remaining members while its script
        // still has a pending step or outcome. Preserve that exact state;
        // the next simulation tick performs the ordinary lifecycle update.
        if (mission.instanceId <= 0
            || (mission.objectIds.empty() && !trimAsciiLocal(tokens[11]).empty())
            || state.triggers.scripts.find(mission.scriptId)
                == state.triggers.scripts.end()) {
            continue;
        }
        state.triggers.nextTeamMissionInstanceId = std::max(
            state.triggers.nextTeamMissionInstanceId,
            mission.instanceId + 1
        );
        state.triggers.teamMissions.emplace(mission.instanceId, mission);
    }
    if (version < 5 && state.campaignMode) {
        for (const auto& [teamKey, objectSet] :
                state.triggers.campaignTeamObjects) {
            const RuntimeTeamType* team = findRuntimeTriggerTeamById(
                state,
                teamKey
            );
            if (team == nullptr
                || state.triggers.scripts.find(team->scriptId)
                    == state.triggers.scripts.end()) {
                continue;
            }
            RuntimeTeamMissionState mission;
            mission.instanceId = state.triggers.nextTeamMissionInstanceId++;
            mission.teamTypeId = team->id;
            mission.scriptId = team->scriptId;
            mission.objectIds.assign(objectSet.begin(), objectSet.end());
            std::sort(mission.objectIds.begin(), mission.objectIds.end());
            mission.status = "restored legacy team";
            if (!mission.objectIds.empty()) {
                state.triggers.teamMissions.emplace(
                    mission.instanceId,
                    std::move(mission)
                );
            }
        }
    }
}
void writeRuntimeTriggerReinforcements(const FixtureSession& state, std::ostream& output) {
    output << "[TriggerReinforcements]\nSchema=1\nDataHex="
        << iniHexEncode(serializeRuntimeTriggerReinforcements(state.triggers.reinforcements)) << "\n\n";
}

bool readRuntimeTriggerReinforcements(FixtureSession& state, const IniFile& ini, std::string& error) {
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

}
auto authorityFields(const RuntimeCellPoint& v) { return std::tie(v.x, v.y); }
auto authorityFields(const RuntimeScriptStep& v) { return std::tie(v.action, v.argument); }
auto authorityFields(const RuntimeScriptType& v) { return std::tie(v.id, v.name, v.steps); }
auto authorityFields(const RuntimeTaskForceMember& v) { return std::tie(v.count, v.unitId); }
auto authorityFields(const RuntimeTaskForce& v) { return std::tie(v.id, v.name, v.group, v.members); }
auto authorityFields(const RuntimeTriggerEvent& v) { return std::tie(v.type, v.authoredOrdinal, v.lightingEventId, v.arguments); }
auto authorityFields(const RuntimeTeamType& v) { return std::tie(v.id, v.name, v.house, v.waypointId, v.scriptId, v.taskForceId, v.priority, v.max, v.group, v.autocreate, v.prebuild, v.reinforce, v.full, v.aggressive, v.isBaseDefense, v.recruitable, v.recruiter, v.looseRecruit, v.avoidThreats, v.suicide, v.guardSlower, v.onlyTargetHouseEnemy, v.transportsReturnOnUnload, v.veteranLevel, v.mindControlDecision); }
auto authorityFields(const RuntimeCampaignHouseProductionState& v) { return std::tie(v.owner, v.country, v.techLevel, v.iq, v.side, v.credits, v.nextAiTriggerCheckTick, v.selectionSerial, v.activeBuildId, v.preferredTargetQuarry, v.aiTriggerTeamRatio, v.teamAircraftRatio, v.teamInfantryRatio, v.teamUnitRatio, v.playerControlled, v.productionStarted, v.aiTriggersEnabled); }
auto authorityFields(const RuntimeCampaignTeamBuild& v) { return std::tie(v.instanceId, v.aiTriggerId, v.teamTypeId, v.owner, v.memberIndex, v.memberProduced, v.remainingTicks, v.paidCost, v.activeTypeId, v.objectIds, v.deadlineTick, v.completed, v.failed, v.status); }
auto authorityFields(const RuntimeTeamMissionState& v) { return std::tie(v.instanceId, v.teamTypeId, v.scriptId, v.objectIds, v.stepIndex, v.stepStartedTick, v.stepIssued, v.targetObjectId, v.targetCell, v.completed, v.sourceAiTriggerId, v.sourceAiBuildId, v.aiOutcomeReported, v.achievedGreatSuccess, v.targetInteractionCompleted, v.transportReturnCells, v.regrouping, v.regroupCell, v.regroupStartedTick, v.regroupDestinations, v.status); }
auto authorityFields(const RuntimeTriggerAction& v) { return std::tie(v.type, v.authoredOrdinal, v.targetId, v.arguments); }
auto authorityFields(const RuntimeTriggerDefinition& v) { return std::tie(v.id, v.house, v.linkedTriggerId, v.name, v.fired, v.enabled, v.repeating, v.difficultyEnabled, v.activationTick, v.lastFiredTick, v.nextEligibleTick, v.tagRepeatType, v.tagIds, v.remainingOnceAllObjectIds, v.events, v.actions); }
auto authorityFields(const RuntimeCampaignRadarEvent& v) { return std::tie(v.cell, v.eventType, v.untilTick); }
auto authorityFields(const RuntimePublicBuildingLocation& v) { return std::tie(v.owner, v.cell, v.noticeTick); }
auto authorityFields(const RuntimeCampaignAiTriggerDefinition& v) { return std::tie(v.id, v.name, v.primaryTeamId, v.owner, v.techLevel, v.conditionType, v.conditionObject, v.conditionNumber, v.conditionComparator, v.currentWeight, v.minimumWeight, v.maximumWeight, v.availableInSkirmish, v.baseDefense, v.side, v.scope, v.secondaryTeamId, v.difficultyEnabled, v.enabled, v.valid, v.timesExecuted, v.timesCompleted, v.timesFailed, v.nextEligibleTick); }

auto authorityFields(const RuntimeComputerTacticHistory& v) { return std::tie(v.weight,v.executions,v.successes,v.failures); }
auto authorityFields(const RuntimeComputerTacticTeam& v) { return std::tie(v.instanceId,v.triggerId,v.teamTypeId,v.targetOwner,v.objectIds,v.joiningObjectIds,v.missingMembers,v.createdTick,v.lastProgressTick,v.missionInstanceId,v.secondary,v.baseDefense,v.launched,v.outcomeReported,v.missionFinished,v.achievedGreatSuccess); }
auto authorityFields(const RuntimeComputerTacticProduction& v) { return std::tie(v.teamInstanceId,v.typeId,v.remainingTicks,v.paidCost,v.totalTicks); }
auto authorityFields(const RuntimeComputerTacticsState::CaptureDecision& v) { return std::tie(v.controllerId,v.startedTick,v.decision); }
auto authorityFields(const RuntimeComputerTacticsState& v) { return std::tie(v.nextTriggerTick,v.selectionSerial,v.nextTeamInstanceId,v.production,v.teams,v.history,v.captureDecisions); }

// Canonical complete value encoding for comparisons: unordered containers
// are sorted by encoded entries, while authored vectors retain their order.
struct Encoder {
    std::ostringstream out;
    template<class T> void value(const T& v) {
        if constexpr (std::is_same_v<T,std::string>) out << v.size() << ':' << v << ';';
        else if constexpr (std::is_same_v<T,double>) out << std::bit_cast<std::uint64_t>(v) << ';';
        else if constexpr (std::is_arithmetic_v<T>) out << v << ';';
        else if constexpr (requires { v.first; v.second; }) { value(v.first); value(v.second); }
        else if constexpr (requires { v.begin(); v.end(); }) {
            out << v.size() << '[';
            if constexpr (requires { typename T::hasher; }) {
                std::vector<std::string> entries;
                for (const auto& x:v) { Encoder e; e.value(x); entries.push_back(e.out.str()); }
                std::sort(entries.begin(), entries.end());
                for (const auto& x:entries) value(x);
            } else for (const auto& x:v) value(x);
            out << ']';
        } else std::apply([&](const auto&... field) { (value(field),...); },authorityFields(v));
    }
};
template<class Trigger> std::string completeTriggerValue(const Trigger& value) {
    Encoder e;
    e.value(value.publicBuildingLocations);
    e.value(value.loaded);
    e.value(value.truckCrate);
    e.value(value.trainCrate);
    e.value(value.bootstrapComplete);
    e.value(value.triggerCount);
    e.value(value.eventCount);
    e.value(value.actionCount);
    e.value(value.tagCount);
    e.value(value.teamTypeCount);
    e.value(value.taskForceCount);
    e.value(value.scriptTypeCount);
    e.value(value.createTeamActionCount);
    e.value(value.firedTriggers);
    e.value(value.teamsCreated);
    e.value(value.unitsCreated);
    e.value(value.ordersIssued);
    e.value(value.scriptStepsIssued);
    e.value(value.scriptStepsCompleted);
    e.value(value.unsupportedScriptSteps);
    e.value(value.unsupportedActions);
    e.value(value.unsupportedEvents);
    e.value(value.allowLegacyBootstrapForSmokeTest);
    e.value(value.collectSmokeTimings);
    e.value(value.smokeCreateTeamMs);
    e.value(value.smokeIssueOrderMs);
    e.value(value.status);
    e.value(value.triggers);
    e.value(value.teams);
    e.value(value.taskForces);
    e.value(value.scripts);
    e.value(value.teamIds);
    e.value(value.scriptIds);
    e.value(value.teamMissions);
    e.value(serializeRuntimeTriggerReinforcements(value.reinforcements));
    e.value(value.nextTeamMissionInstanceId);
    e.value(value.campaignHouseByIndex);
    e.value(value.campaignCountryByIndex);
    e.value(value.campaignCanonicalHouseByAlias);
    e.value(value.campaignCountryByHouse);
    e.value(value.campaignHouseProduction);
    e.value(value.campaignAiTriggers);
    e.value(value.computerAiTriggers);
    e.value(value.computerTacticsByOwner);
    e.value(value.campaignTeamBuilds);
    e.value(value.nextCampaignTeamBuildId);
    e.value(value.campaignAiTriggerSelections);
    e.value(value.campaignAiTriggerTeamsCompleted);
    e.value(value.campaignAiTriggerTeamsFailed);
    e.value(value.campaignAlliedPairs);
    e.value(value.campaignLocalVariables);
    e.value(value.campaignGlobalVariables);
    e.value(value.campaignCellTagByCell);
    e.value(value.campaignPreviousCellByObject);
    e.value(value.campaignPreviousHealthByObject);
    e.value(value.campaignInitialHealthByObject);
    e.value(value.campaignPreviousAliveByObject);
    e.value(value.campaignInitialOwnerByObject);
    e.value(value.campaignPreviousOwnerByObject);
    e.value(value.campaignPreviouslyKnownObjects);
    e.value(value.campaignPreviouslyDiscoveredObjects);
    e.value(value.campaignPreviousSelectedObjects);
    e.value(value.campaignAttackedTickByObject);
    e.value(value.campaignAttackerHouseByObject);
    e.value(value.campaignDestroyedTickByObject);
    e.value(value.campaignDestroyerHouseByObject);
    e.value(value.campaignEnteredTickByTargetObject);
    e.value(value.campaignEntrantHouseByTargetObject);
    e.value(value.campaignSpiedTickByTargetObject);
    e.value(value.campaignSpyHouseByTargetObject);
    e.value(value.campaignBridgeDestroyedTickByCell);
    e.value(value.campaignMoviePathByIndex);
    e.value(value.campaignTeamObjects);
    e.value(value.campaignMissionTimerStarted);
    e.value(value.campaignMissionTimerRunning);
    e.value(value.campaignMissionTimerRemainingTicks);
    e.value(value.campaignMissionTimerLabel);
    e.value(value.campaignInputLocked);
    e.value(value.campaignForceDepth);
    e.value(value.campaignRadarBlackoutUntilTick);
    e.value(value.campaignChronoScreenEffectUntilTick);
    e.value(value.campaignIonStormUntilTick);
    e.value(value.campaignNextIonStormStrikeTick);
    e.value(value.campaignRadarEvents);
    e.value(value.campaignTechLevelByType);
    e.value(value.campaignBaseCenterByOwner);
    e.value(value.campaignPreferredSuperWeaponTargetByOwner);
    e.value(value.buildableTaskForceCache);
    return e.out.str();
}

namespace {
int checks=0,observations=0,scenarios=0;
void require(bool ok,const char* why) { ++checks;if(!ok)throw std::runtime_error(why); }
IniFile document(const std::string& text) { IniFile ini;require(ini.loadFromString(text),"fixture INI");return ini; }
FixtureSession initial(int seed) {
    FixtureSession s;s.tick=40+seed;s.campaignMode=(seed&1)==0;
    auto& t=s.triggers;t.loaded=true;t.status="keep-other-state";t.campaignMissionTimerLabel="unchanged";
    t.campaignLocalVariables[99]=8;t.campaignGlobalVariables[99]=9;
    t.campaignAlliedPairs={"old|pair"};t.campaignTeamObjects["old"]={1,7};t.campaignTeamObjects["TEAM-A"]={5,1};
    t.campaignTechLevelByType["old"]=-1;t.campaignBaseCenterByOwner["old"]={9,9};
    t.campaignPreferredSuperWeaponTargetByOwner["old"]={8,8};
    s.economy.catalog={{"UNIT-A",3},{"unit-a",4},{"unrelated",11}};
    RuntimeScriptType script;script.id="SCRIPT";script.steps={{1,2}};t.scripts["SCRIPT"]=script;
    RuntimeTeamType team;team.id="TEAM-A";team.scriptId="SCRIPT";team.taskForceId="TF";t.teams["team-a"]=team;
    RuntimeTaskForce task;task.id="TF";task.members={{2,"UNIT-A"}};t.taskForces["TF"]=task;
    RuntimeCampaignHouseProductionState house;house.owner="HOUSE-A";house.country="old-country";house.credits=19;t.campaignHouseProduction["house-a"]=house;
    RuntimeCampaignAiTriggerDefinition ai;ai.id="T1";ai.minimumWeight=10;ai.maximumWeight=50;ai.currentWeight=20;ai.enabled=true;t.campaignAiTriggers.push_back(ai);
    RuntimeTriggerDefinition trigger;trigger.id="TRIG";trigger.name="authored";trigger.enabled=true;
    trigger.remainingOnceAllObjectIds={1,99};trigger.events.push_back(RuntimeTriggerEvent{});t.triggers.push_back(trigger);
    RuntimeTeamMissionState old;old.instanceId=90;old.scriptId="SCRIPT";old.status="old-mission";t.teamMissions[90]=old;
    t.nextTeamMissionInstanceId=91;
    RuntimeTriggerReinforcement job;job.instanceId=1;job.teamTypeId="TEAM-A";job.owner="HouseA";job.members={{"UNIT-A",2}};job.originX=1;job.originY=2;job.actionType=80;job.nextAttemptTick=5;
    t.reinforcements.nextInstanceId=2;t.reinforcements.pending[1]=job;
    // Non-persistent values are encoded too, so unintended clearing is visible.
    t.campaignPreviousHealthByObject[1]=55;t.campaignPreviousOwnerByObject[1]="old";
    t.computerTacticsByOwner["owner"].selectionSerial=seed+7;
    t.publicBuildingLocations[42]={"owner",{3,4},5};
    return s;
}
std::string complete(const FixtureSession& s) {
    Encoder e;e.value(completeTriggerValue(s.triggers));e.value(s.tick);e.value(s.campaignMode);
    for(const auto& c:s.economy.catalog) { e.value(c.typeId);e.value(c.techLevel); }
    return e.out.str();
}
std::string validCollections() {
    return "[CampaignVariables]\nCount=4\n0=4|5\n1=malformed\n2=4|9\n3=not-int|bad\n"
      "[CampaignGlobals]\nCount=1\n0=7|8\n"
      "[CampaignTeams]\nCount=2\n0= TEAM-A |1,99,2,5\n1=empty|99\n"
      "[CampaignTechLevels]\nCount=2\n0=UNIT-A|-5\n1=unknown|9\n"
      "[CampaignBaseCenters]\nCount=3\n0=Owner|3|4\n1=bad|-1|5\n2=Owner|7|8\n"
      "[CampaignSuperWeaponTargets]\nCount=1\n0= Owner |9|10\n"
      "[CampaignHouseAI]\nCount=1\n0=Alias||-1|-2|-3|-4|-5|-6|-7|yes|yes|no|11|-1|101|37|49\n"
      "[CampaignAITriggers]\nCount=1\n0=t1|999|2|3|4|5|yes\n"
      "[CampaignTeamBuilds]\nCount=2\nNextInstanceId=0\n0=8|T1|TEAM-A|Alias|-1|-2|-3|-4|UNIT-A|1|no|no| pending |1,99,2\n"
      "1=8|T1|TEAM-A|HouseA|2|2|2|2|UNIT-A|900|yes|yes|duplicate|5\n"
      "[CampaignTriggers]\nCount=1\n0=trig|yes|no|yes|12|13|14|2,99,-1,bad\n";
}
std::string validMissions() {
    return "[TeamMissions]\nCount=4\nNextInstanceId=0\n"
      "0=3|TEAM-A|SCRIPT|2|3|yes|5|7|8|no|active|1,99,2|T1|8|yes|no|yes|1,9,10;99,3,4|yes|11|12|13|2,6,7;99,3,4\n"
      "1=4|TEAM-A|SCRIPT|0|-1|no|-1|-1|-1|no|empty||T1|-1|no|no|no\n"
      "2=5|TEAM-A|SCRIPT|0|-1|no|-1|-1|-1|no|unknown|99\n"
      "3=3|TEAM-A|SCRIPT|8|9|no|-1|1|1|yes|duplicate|5\n";
}
std::string singleCellMissions() {
    std::string value=validMissions();const std::string extra=";99,3,4";
    for(std::size_t pos=value.find(extra);pos!=std::string::npos;pos=value.find(extra))value.erase(pos,extra.size());
    return value;
}
void compareRead(const std::string& text,int seed,int version,int phase,int failAt=0,bool missingUnit=false) {
    FixtureSession a=initial(seed),b=initial(seed),validated;
    validated.triggers.campaignAlliedPairs={"a|b","a|c"};
    RecordingOperations oa(a),ob(b);oa.failAt=ob.failAt=failAt;oa.missingUnit=ob.missingUnit=missingUnit;
    auto ini=document(text);std::string ea="stale",eb=ea;bool ra=true,rb=true,ta=false,tb=false;
    frozen_original::bound=&oa;
    try {
        if(phase==0)frozen_original::readCollections(a,ini,&validated);
        else if(phase==1)frozen_original::readMissions(a,ini,version);
        else ra=frozen_original::readRuntimeTriggerReinforcements(a,ini,ea);
    } catch(const std::runtime_error& ex) { require(std::string(ex.what())=="planned-domain-operation","old exception");ta=true; }
    try {
        if(phase==0)restoreRuntimeCampaignTriggerCollections(b.triggers,ini,b.tick,validated.triggers.campaignAlliedPairs,ob);
        else if(phase==1)restoreRuntimeTeamMissionState(b.triggers,ini,version,b.campaignMode,ob);
        else rb=::readRuntimeTriggerReinforcements(b.triggers,ini,eb,ob);
    } catch(const std::runtime_error& ex) { require(std::string(ex.what())=="planned-domain-operation","new exception");tb=true; }
    require(complete(a)==complete(b),"complete trigger+catalog+external state equality including exception point");
    require(std::tie(ra,ea,ta)==std::tie(rb,eb,tb),"result/error/throw equality");if(oa.trace!=ob.trace) {
        std::cerr<<"Trace diagnostic seed="<<seed<<" phase="<<phase<<" version="<<version<<" failAt="<<failAt<<"\n";
        std::cerr<<"old map:";for(const auto& entry:a.triggers.campaignTeamObjects)std::cerr<<' '<<entry.first;
        std::cerr<<"\nnew map:";for(const auto& entry:b.triggers.campaignTeamObjects)std::cerr<<' '<<entry.first;
        std::cerr<<"\nold trace:";for(const auto& entry:oa.trace)std::cerr<<' '<<entry;
        std::cerr<<"\nnew trace:";for(const auto& entry:ob.trace)std::cerr<<' '<<entry;
        std::cerr<<"\n";
    }
    require(oa.trace==ob.trace,"exact lookup/catalog order");
    ++observations;
    if(seed==0 && failAt==0 && phase==0 && text==validCollections()) {
        require(b.triggers.campaignLocalVariables.at(4)==9 && b.triggers.campaignLocalVariables.at(0)==0,"last duplicate/int fallback");
        require(b.economy.catalog[0].techLevel==-5 && b.economy.catalog[1].techLevel==4,"only first matching catalog item");
        require(b.triggers.campaignHouseProduction.at("house-a").preferredTargetQuarry==9,"quarry bound");
        require(b.triggers.campaignAiTriggers[0].currentWeight==50,"AI weight clamp");
        require(b.triggers.campaignTeamBuilds.at(8).deadlineTick==40 && b.triggers.campaignTeamBuilds.at(8).objectIds==std::vector<int>({1,2}),"first build wins/active plus inactive existing ids");
        require(b.triggers.triggers[0].remainingOnceAllObjectIds==std::unordered_set<int>({2,99}),"remaining trigger targets retain unseen ids");
    }
    if(seed==0 && failAt==0 && phase==1 && version==14 && text==validMissions()) {
        require(b.triggers.teamMissions.size()==2 && b.triggers.teamMissions.at(4).objectIds.empty(),"empty authored mission preserved/unknown skipped");
        require(b.triggers.teamMissions.at(3).objectIds==std::vector<int>({1,2}),"original member filtering");
        require(ini.getValue("TeamMissions","0","").find(';')==std::string::npos,"actual existing INI semicolon comment truncation");
        require(a.triggers.teamMissions.at(3).regroupDestinations.empty() && b.triggers.teamMissions.at(3).regroupDestinations.empty()
            && a.triggers.teamMissions.at(3).transportReturnCells.size()==1 && b.triggers.teamMissions.at(3).transportReturnCells.size()==1,
            "frozen and current existing comment-truncated mission semantics");
        require(b.triggers.nextTeamMissionInstanceId==5,"original mission next id from admitted records");
    }
    if(seed==0 && failAt==0 && phase==1 && version==3 && text.empty()) {
        require(b.triggers.teamMissions.size()==1 && b.triggers.teamMissions.at(1).objectIds==std::vector<int>({1,5})
            && b.triggers.teamMissions.at(1).status=="restored legacy team", "positive legacy<5 sorted mission synthesis");
    }
    if(seed==0 && failAt==0 && phase==1 && version==14 && text==singleCellMissions()) {
        require(b.triggers.teamMissions.at(3).transportReturnCells.size()==1 && b.triggers.teamMissions.at(3).regroupDestinations.size()==1,
            "full23field mission admission with one return/regroup entry and no comment delimiter");
    }
}
struct PrefixBuffer final : std::streambuf {
    std::string bytes;std::size_t limit;
    explicit PrefixBuffer(std::size_t n):limit(n){}
    int_type overflow(int_type c) override {
        if(traits_type::eq_int_type(c,traits_type::eof()))return traits_type::not_eof(c);
        if(bytes.size()==limit)throw std::runtime_error("planned-stream");
        bytes.push_back(traits_type::to_char_type(c));return c;
    }
    std::streamsize xsputn(const char* data,std::streamsize n) override {
        for(std::streamsize j=0;j<n;++j)overflow(traits_type::to_int_type(data[j]));return n;
    }
};
void writeOne(bool old,int which,const FixtureSession& s,std::ostream& out) {
    if(old) {
        if(which==0)frozen_original::writeCollections(s,out);
        if(which==1)frozen_original::writeMissions(s,out);
        if(which==2)frozen_original::writeRuntimeTriggerReinforcements(s,out);
    } else {
        if(which==0)writeRuntimeCampaignTriggerCollections(s.triggers,out);
        if(which==1)writeRuntimeTeamMissionState(s.triggers,out);
        if(which==2)::writeRuntimeTriggerReinforcements(s.triggers,out);
    }
}
void writers() {
    for(int seed=0;seed<16;++seed) {
        auto state=initial(seed);RecordingOperations operations(state);auto ini=document(validCollections()+validMissions());
        restoreRuntimeCampaignTriggerCollections(state.triggers,ini,state.tick,state.triggers.campaignAlliedPairs,operations);
        restoreRuntimeTeamMissionState(state.triggers,ini,14,state.campaignMode,operations);
        auto& mission=state.triggers.teamMissions.at(3);
        mission.transportReturnCells[5]={11,12};mission.regrouping=true;
        mission.regroupDestinations[1]={13,14};mission.regroupDestinations[5]={15,16};
        const auto originalState=complete(state);
        for(int which=0;which<3;++which)for(int format=0;format<2;++format) {
            std::ostringstream old,current;if(format) {old<<std::hex<<std::scientific;current<<std::hex<<std::scientific;old.precision(3);current.precision(3);}
            writeOne(true,which,state,old);writeOne(false,which,state,current);
            require(old.str()==current.str() && old.flags()==current.flags() && old.precision()==current.precision(),"all original stream bytes/format state");
            for(std::size_t limit:std::vector<std::size_t>{0,1,10,127,old.str().size()-1,old.str().size(),old.str().size()+1}) {
                PrefixBuffer a(limit),b(limit);std::ostream x(&a),y(&b);x.exceptions(std::ios::badbit|std::ios::failbit);y.exceptions(std::ios::badbit|std::ios::failbit);
                if(format) {x<<std::hex<<std::scientific;y<<std::hex<<std::scientific;x.precision(3);y.precision(3);}
                bool ta=false,tb=false;try{writeOne(true,which,state,x);}catch(const std::runtime_error&){ta=true;}
                try{writeOne(false,which,state,y);}catch(const std::runtime_error&){tb=true;}
                require(ta==tb && a.bytes==b.bytes && x.rdstate()==y.rdstate(),"exact stream prefix/exception point");++observations;
            }
        }
        require(complete(state)==originalState,"writers do not mutate any trigger field");
    }
}
void readers() {
    for(int seed=0;seed<32;++seed)for(int phase=0;phase<2;++phase)for(int version:{3,5,14}) {
        for(const auto& text:std::vector<std::string>{"",phase==0?validCollections():validMissions(),phase==0?validCollections():singleCellMissions(),
          "[CampaignVariables]\nCount=-1\n[TeamMissions]\nCount=-1\nNextInstanceId=-9\n",
          "[CampaignTeamBuilds]\nCount=1\n0=bad|invalid\n[CampaignTriggers]\nCount=1\n0=unknown|yes|yes\n"}) {
            ++scenarios;for(int failAt:{0,1,3,7,17,50})compareRead(text,seed,version,phase,failAt);
        }
    }
    auto s=initial(0);const auto valid=serializeRuntimeTriggerReinforcements(s.triggers.reinforcements);
    auto missingTeam=s;missingTeam.triggers.reinforcements.pending.at(1).teamTypeId="unknown";
    auto wrong=s;wrong.triggers.reinforcements.pending.at(1).members[0].count=3;
    const std::vector<std::string> reinforcements{"", "[TriggerReinforcements]\nSchema=2\nDataHex=GG\n",
      "[TriggerReinforcements]\nSchema=1\nDataHex=GG\n", "[TriggerReinforcements]\nSchema=1\nDataHex="+iniHexEncode(valid)+"\n",
      "[TriggerReinforcements]\nSchema=1\nDataHex="+iniHexEncode(serializeRuntimeTriggerReinforcements(missingTeam.triggers.reinforcements))+"\n",
      "[TriggerReinforcements]\nSchema=1\nDataHex="+iniHexEncode(serializeRuntimeTriggerReinforcements(wrong.triggers.reinforcements))+"\n"};
    for(const auto& text:reinforcements)for(bool missingUnit:{false,true})for(int failAt:{0,1,2,3,5}) {
        ++scenarios;compareRead(text,0,14,2,failAt,missingUnit);
    }
    auto missingForce=initial(0),sameForce=missingForce;missingForce.triggers.taskForces.clear();sameForce=missingForce;
    RecordingOperations oldForce(missingForce),newForce(sameForce);frozen_original::bound=&oldForce;
    const auto forceDoc=document(reinforcements[3]);std::string oldError="old",newError="old";
    require(!frozen_original::readRuntimeTriggerReinforcements(missingForce,forceDoc,oldError)
        && !::readRuntimeTriggerReinforcements(sameForce.triggers,forceDoc,newError,newForce)
        && oldError=="pending reinforcement TaskForce missing" && newError==oldError
        && complete(missingForce)==complete(sameForce) && oldForce.trace==newForce.trace,
        "original missing TaskForce error/atomic queue and exact call order");
    ++observations;
    const auto absent=document("");RecordingOperations op(s);std::string error="stale";
    require(::readRuntimeTriggerReinforcements(s.triggers,absent,error,op) && s.triggers.reinforcements.pending.empty() && error=="stale","absent reinforcement resets queue/stale error");
}
void textHelpers() {
    RuntimeTriggerState lookup;
    RuntimeTriggerDefinition a,b,c;a.id=" SAME ";b.id="same";c.id="";
    lookup.triggers={a,b,c};
    require(runtimeCampaignTriggerById(lookup,"same")==&lookup.triggers[0],"original lookup first normalized match");
    require(runtimeCampaignTriggerById(lookup," ")==&lookup.triggers[2],"original empty lookup policy");
    require(runtimeCampaignTriggerById(lookup,"missing")==nullptr,"original lookup absent");
    for(const auto& text:std::vector<std::string>{"","  ","yes"," true ","1","no","FALSE","2147483648","-2147483649","12junk","2.5junk","a,, b;c; ;d",std::string("\x80",1)}) {
        require(splitRuntimeTokenList(text)==frozen_original::splitRuntimeTokenList(text),"original comma semicolon empty-token semantics");
        require(parseRuntimeBool(text)==frozen_original::parseRuntimeBool(text),"original permissive bool");
        for(int fallback:{-7,0,91})require(parseRuntimeIntOrDefault(text,fallback)==frozen_original::parseRuntimeIntOrDefault(text,fallback),"original int fallback/full token");
    }
    require(runtimeBoolText(true)==frozen_original::runtimeBoolText(true) && runtimeBoolText(false)==frozen_original::runtimeBoolText(false),"original bool text");
}
}
int main() {
    try {textHelpers();readers();writers();std::cout<<"H039 trigger persistence passed checks="<<checks<<" observations="<<observations<<" scenarios="<<scenarios<<" skipped=0\n";return 0;}
    catch(const std::exception& ex) {std::cerr<<ex.what()<<" checks="<<checks<<"\n";return 1;}
}
