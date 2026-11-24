#pragma once
#include "simulation/ai/RuntimeComputerAiState.h"
#include "simulation/ai/RuntimeComputerAiMetrics.h"
#include "simulation/ai/RuntimeComputerTactics.h"
#include "simulation/production/RuntimeBuildCatalogItem.h"
#include "simulation/triggers/RuntimeTriggerState.h"
#include <optional>

struct ComputerTacticsRules;
struct WorldObjectInstance;

namespace concord::simulation::ai {
// Only original lifecycle fields, synchronously borrowed from their owners.
struct RuntimeComputerTacticLifecycleState {
    const std::vector<RuntimeCampaignAiTriggerDefinition>& computerAiTriggers;
    std::map<std::string, RuntimeComputerTacticsState>& computerTacticsByOwner;
    const std::map<std::string, RuntimeTaskForce>& taskForces;
    std::map<int, RuntimeTeamMissionState>& teamMissions;
    const int& nextTeamMissionInstanceId;
    const int& tick;
};
class RuntimeComputerTacticLifecycleQueries {
public:
    virtual ~RuntimeComputerTacticLifecycleQueries() = default;
    virtual const ComputerTacticsRules& rules() const = 0;
    virtual const RuntimeTeamType* teamType(const std::string&) const = 0;
    virtual const WorldObjectInstance* objectIncludingInactive(int) const = 0;
    virtual const WorldObjectInstance* object(int) const = 0;
    virtual bool ownerMatches(const WorldObjectInstance&, const std::string&) const = 0;
    virtual bool pendingTeamRejoinAfterLimbo(int) const = 0;
    virtual int legacyScriptTicks(int) const = 0;
    virtual int difficultyIndex(const std::string&) const = 0;
    virtual RuntimeComputerAiMetrics metrics(const RuntimeComputerAiState&) const = 0;
    virtual std::optional<RuntimeBuildCatalogItem> neededHarvester(const RuntimeComputerAiState&, const RuntimeComputerAiMetrics&) const = 0;
    virtual bool isMegawealthMode() const = 0;
    virtual bool needsOilEngineer(const RuntimeComputerAiState&) const = 0;
    virtual std::optional<RuntimeBuildCatalogItem> selectEngineer(const RuntimeComputerAiState&) const = 0;
    virtual std::optional<RuntimeBuildCatalogItem> buildItem(const std::string&, const std::string&) const = 0;
};
class RuntimeComputerTacticLifecycleEffects {
public:
    virtual ~RuntimeComputerTacticLifecycleEffects() = default;
    virtual bool advanceProduction(RuntimeComputerAiState&, RuntimeComputerTacticsState&) = 0;
    virtual bool joinReinforcements(RuntimeComputerTacticTeam&, const RuntimeTeamType&) = 0;
    virtual bool recruitMembers(RuntimeComputerAiState&, RuntimeComputerTacticsState&, RuntimeComputerTacticTeam&, const RuntimeTeamType&) = 0;
    virtual WorldObjectInstance* mutableObject(int) = 0;
    virtual void issueTeamOrder(const RuntimeTeamType&, const std::vector<int>&, const std::string&, int) = 0;
    virtual bool select(RuntimeComputerAiState&, RuntimeComputerTacticsState&) = 0;
    virtual bool queueProduction(RuntimeComputerAiState&, const RuntimeBuildCatalogItem&, int teamInstanceId = -1) = 0;
};
void reportRuntimeComputerTacticOutcome(const RuntimeComputerTacticLifecycleState&, const RuntimeComputerTacticLifecycleQueries&,
    RuntimeComputerTacticsState&, RuntimeComputerTacticTeam&);
bool runtimeComputerTacticMemberPresent(const RuntimeComputerTacticLifecycleQueries&, const RuntimeComputerAiState&, int);
void refreshRuntimeComputerTacticMissing(const RuntimeComputerTacticLifecycleState&, const RuntimeComputerTacticLifecycleQueries&,
    RuntimeComputerTacticTeam&, const RuntimeTeamType&);
bool advanceRuntimeComputerTactics(const RuntimeComputerTacticLifecycleState&, const RuntimeComputerTacticLifecycleQueries&,
    RuntimeComputerTacticLifecycleEffects&, RuntimeComputerAiState&);
}
