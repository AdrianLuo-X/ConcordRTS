#include "simulation/triggers/RuntimeTriggerPersistence.h"
#include "core/io/IniFile.h"
#include "core/io/RuntimeSaveTextFields.h"
#include <algorithm>
#include <sstream>
#include <utility>

using namespace runtime_save_text;

RuntimeTriggerDefinition* runtimeCampaignTriggerById(
    RuntimeTriggerState& triggers,
    const std::string& id
) {
    const std::string wanted = toLowerAsciiLocal(trimAsciiLocal(id));
    for (RuntimeTriggerDefinition& trigger : triggers.triggers) {
        if (toLowerAsciiLocal(trimAsciiLocal(trigger.id)) == wanted) {
            return &trigger;
        }
    }
    return nullptr;
}

void writeRuntimeCampaignTriggerCollections(const RuntimeTriggerState& triggers, std::ostream& output) {
            output << "[CampaignTriggers]\n";
            output << "Count=" << triggers.triggers.size() << "\n";
            for (std::size_t index = 0;
                index < triggers.triggers.size();
                ++index) {
                const RuntimeTriggerDefinition& trigger =
                    triggers.triggers[index];
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
                   << triggers.campaignLocalVariables.size()
                   << "\n";
            std::size_t variableIndex = 0;
            for (const auto& [variable, variableValue] :
                    triggers.campaignLocalVariables) {
                output << variableIndex++ << "="
                       << variable << "|"
                       << variableValue << "\n";
            }
            output << "\n[CampaignGlobals]\n";
            output << "Count="
                   << triggers.campaignGlobalVariables.size()
                   << "\n";
            std::size_t globalIndex = 0;
            for (const auto& [variable, variableValue] :
                    triggers.campaignGlobalVariables) {
                output << globalIndex++ << "="
                       << variable << "|"
                       << variableValue << "\n";
            }
            output << "\n[CampaignTeams]\n";
            output << "Count="
                   << triggers.campaignTeamObjects.size()
                   << "\n";
            std::size_t teamIndex = 0;
            for (const auto& [teamId, objectIds] :
                    triggers.campaignTeamObjects) {
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
                   << triggers.campaignTechLevelByType.size()
                   << "\n";
            std::size_t techLevelIndex = 0;
            for (const auto& [typeId, techLevel] :
                    triggers.campaignTechLevelByType) {
                output << techLevelIndex++ << "="
                       << runtimeSaveField(typeId) << "|"
                       << techLevel << "\n";
            }
            output << "\n[CampaignBaseCenters]\n";
            output << "Count="
                   << triggers.campaignBaseCenterByOwner.size()
                   << "\n";
            std::size_t baseCenterIndex = 0;
            for (const auto& [owner, cell] :
                    triggers.campaignBaseCenterByOwner) {
                output << baseCenterIndex++ << "="
                       << runtimeSaveField(owner) << "|"
                       << cell.x << "|" << cell.y << "\n";
            }
            output << "\n[CampaignSuperWeaponTargets]\n";
            output << "Count="
                   << triggers
                        .campaignPreferredSuperWeaponTargetByOwner.size()
                   << "\n";
            std::size_t preferredTargetIndex = 0;
            for (const auto& [owner, cell] : triggers
                    .campaignPreferredSuperWeaponTargetByOwner) {
                output << preferredTargetIndex++ << "="
                       << runtimeSaveField(owner) << "|"
                       << cell.x << "|" << cell.y << "\n";
            }
            output << "\n[CampaignAlliances]\n";
            output << "Schema=2\n";
            std::vector<std::pair<std::string, std::string>> savedAlliances;
            for (const std::string& pair : triggers.campaignAlliedPairs) {
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
                   << triggers.campaignHouseProduction.size() << "\n";
            std::size_t houseAiIndex = 0;
            for (const auto& [houseKey, house] :
                    triggers.campaignHouseProduction) {
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
                   << triggers.campaignAiTriggers.size() << "\n";
            for (std::size_t index = 0;
                index < triggers.campaignAiTriggers.size();
                ++index) {
                const RuntimeCampaignAiTriggerDefinition& aiTrigger =
                    triggers.campaignAiTriggers[index];
                output << index << "=" << runtimeSaveField(aiTrigger.id) << "|"
                       << aiTrigger.currentWeight << "|"
                       << aiTrigger.timesExecuted << "|"
                       << aiTrigger.timesCompleted << "|"
                       << aiTrigger.timesFailed << "|"
                       << aiTrigger.nextEligibleTick << "|"
                       << runtimeBoolText(aiTrigger.enabled) << "\n";
            }

            output << "\n[CampaignTeamBuilds]\n";
            output << "Count=" << triggers.campaignTeamBuilds.size()
                   << "\n";
            output << "NextInstanceId="
                   << triggers.nextCampaignTeamBuildId << "\n";
            std::size_t campaignBuildIndex = 0;
            for (const auto& [buildId, build] :
                    triggers.campaignTeamBuilds) {
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

void writeRuntimeTeamMissionState(const RuntimeTriggerState& triggers, std::ostream& output) {
        output << "[TeamMissions]\n";
        output << "Count=" << triggers.teamMissions.size() << "\n";
        output << "NextInstanceId="
               << triggers.nextTeamMissionInstanceId << "\n";
        std::size_t teamMissionIndex = 0;
        for (const auto& [missionId, mission] :
                triggers.teamMissions) {
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

void restoreRuntimeCampaignTriggerCollections(RuntimeTriggerState& triggers, const IniFile& ini,
    const int& tick, const std::set<std::string>& validatedAlliedPairs, RuntimeTriggerPersistenceOperations& operations) {
        triggers.campaignLocalVariables.clear();
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
            triggers.campaignLocalVariables[variable] =
                parseRuntimeIntOrDefault(tokens[1], 0);
        }

        triggers.campaignGlobalVariables.clear();
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
            triggers.campaignGlobalVariables[variable] =
                parseRuntimeIntOrDefault(tokens[1], 0);
        }

        triggers.campaignTeamObjects.clear();
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
                triggers.campaignTeamObjects[
                    toLowerAsciiLocal(trimAsciiLocal(tokens[0]))
                ];
            std::stringstream objectStream(tokens[1]);
            std::string objectToken;
            while (std::getline(objectStream, objectToken, ',')) {
                int objectId = -1;
                if (parseRuntimeInt(objectToken, objectId)
                    && objectId >= 0
                    && operations.objectExists(objectId)) {
                    teamObjects.insert(objectId);
                }
            }
        }

        triggers.campaignTechLevelByType.clear();
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
            triggers.campaignTechLevelByType[typeKey] = techLevel;
            operations.applyTechLevel(typeKey, techLevel);
        }

        triggers.campaignBaseCenterByOwner.clear();
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
                triggers.campaignBaseCenterByOwner[
                    toLowerAsciiLocal(tokens[0])
                ] = cell;
            }
        }

        triggers.campaignPreferredSuperWeaponTargetByOwner.clear();
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
                triggers.campaignPreferredSuperWeaponTargetByOwner[
                    toLowerAsciiLocal(trimAsciiLocal(tokens[0]))
                ] = cell;
            }
        }

        triggers.campaignAlliedPairs =
            validatedAlliedPairs;

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
            const std::string resolvedOwner = operations.resolveHouse(tokens[0]);
            const auto house = triggers.campaignHouseProduction.find(
                toLowerAsciiLocal(trimAsciiLocal(resolvedOwner))
            );
            if (house == triggers.campaignHouseProduction.end()) {
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
                triggers.campaignAiTriggers.begin(),
                triggers.campaignAiTriggers.end(),
                [&triggerKey](
                    const RuntimeCampaignAiTriggerDefinition& candidate
                ) {
                    return toLowerAsciiLocal(trimAsciiLocal(candidate.id))
                        == triggerKey;
                }
            );
            if (trigger == triggers.campaignAiTriggers.end()) {
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

        triggers.campaignTeamBuilds.clear();
        triggers.nextCampaignTeamBuildId = std::max(
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
            build.owner = operations.resolveHouse(tokens[3]);
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
                tick,
                parseRuntimeIntOrDefault(tokens[9], tick)
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
                    && operations.objectExists(objectId)) {
                    build.objectIds.push_back(objectId);
                }
            }
            if (build.instanceId <= 0
                || operations.findTeam(build.teamTypeId)
                    == nullptr
                || build.owner.empty()) {
                continue;
            }
            triggers.nextCampaignTeamBuildId = std::max(
                triggers.nextCampaignTeamBuildId,
                build.instanceId + 1
            );
            triggers.campaignTeamBuilds.emplace(
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
                triggers,
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

void restoreRuntimeTeamMissionState(RuntimeTriggerState& triggers, const IniFile& ini,
    int version, const bool& campaignMode, RuntimeTriggerPersistenceOperations& operations) {
    triggers.teamMissions.clear();
    triggers.nextTeamMissionInstanceId = std::max(
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
                && operations.objectExists(objectId)) {
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
                    && operations.objectExists(id))
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
                if (id >= 0 && x >= 0 && y >= 0 && operations.objectExists(id))
                    mission.regroupDestinations[id] = {x, y};
            }
        }
        // An authored team can have no remaining members while its script
        // still has a pending step or outcome. Preserve that exact state;
        // the next simulation tick performs the ordinary lifecycle update.
        if (mission.instanceId <= 0
            || (mission.objectIds.empty() && !trimAsciiLocal(tokens[11]).empty())
            || triggers.scripts.find(mission.scriptId)
                == triggers.scripts.end()) {
            continue;
        }
        triggers.nextTeamMissionInstanceId = std::max(
            triggers.nextTeamMissionInstanceId,
            mission.instanceId + 1
        );
        triggers.teamMissions.emplace(mission.instanceId, mission);
    }
    if (version < 5 && campaignMode) {
        for (const auto& [teamKey, objectSet] :
                triggers.campaignTeamObjects) {
            const RuntimeTeamType* team = operations.findTeam(teamKey);
            if (team == nullptr
                || triggers.scripts.find(team->scriptId)
                    == triggers.scripts.end()) {
                continue;
            }
            RuntimeTeamMissionState mission;
            mission.instanceId = triggers.nextTeamMissionInstanceId++;
            mission.teamTypeId = team->id;
            mission.scriptId = team->scriptId;
            mission.objectIds.assign(objectSet.begin(), objectSet.end());
            std::sort(mission.objectIds.begin(), mission.objectIds.end());
            mission.status = "restored legacy team";
            if (!mission.objectIds.empty()) {
                triggers.teamMissions.emplace(
                    mission.instanceId,
                    std::move(mission)
                );
            }
        }
    }
}

void writeRuntimeTriggerReinforcements(const RuntimeTriggerState& triggers, std::ostream& output) {
    output << "[TriggerReinforcements]\nSchema=1\nDataHex="
        << iniHexEncode(serializeRuntimeTriggerReinforcements(triggers.reinforcements)) << "\n\n";
}

bool readRuntimeTriggerReinforcements(RuntimeTriggerState& triggers, const IniFile& ini, std::string& error, RuntimeTriggerPersistenceOperations& operations) {
    RuntimeTriggerReinforcements staged;
    if (!ini.getSection("TriggerReinforcements")) {
        triggers.reinforcements = std::move(staged);
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
        const auto* team = operations.findTeam(job.teamTypeId);
        if (!team) { error = "pending reinforcement references missing TeamType"; return false; }
        const auto taskForce = triggers.taskForces.find(team->taskForceId);
        if (taskForce == triggers.taskForces.end()) { error = "pending reinforcement TaskForce missing"; return false; }
        std::map<std::string, std::int64_t> expected, actual;
        for (const auto& member : taskForce->second.members)
            expected[toLowerAsciiLocal(member.unitId)] += member.count;
        for (const auto& member : job.members) {
            if (!operations.unitTypeExists(member.typeId)) {
                error = "pending reinforcement member type missing"; return false;
            }
            actual[toLowerAsciiLocal(member.typeId)] += member.count;
        }
        if (expected != actual) { error = "pending reinforcement composition differs from TaskForce"; return false; }
    }
    triggers.reinforcements = std::move(staged);
    return true;
}
