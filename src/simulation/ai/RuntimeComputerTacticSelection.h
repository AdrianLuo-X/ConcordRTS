#pragma once
#include "simulation/ai/RuntimeComputerAiState.h"
#include "simulation/ai/RuntimeComputerTactics.h"
#include "simulation/production/RuntimeBuildCatalogItem.h"
#include "simulation/triggers/RuntimeTriggerState.h"
#include <optional>
#include <string>
#include <vector>

struct ComputerTacticsRules;
struct WorldObjectInstance;
struct SuperWeaponSidebarSnapshot;

namespace concord::simulation::ai {
// Synchronous reads of the existing owners. No retained session, copied World,
// selection cache or new random source. Views live for the original call only.
class RuntimeComputerTacticSelectionQueries {
public:
    virtual ~RuntimeComputerTacticSelectionQueries() = default;
    virtual int tick() const = 0;
    virtual int fixedTickHz() const = 0;
    virtual int techLevel() const = 0;
    virtual bool superweaponsEnabled() const = 0;
    virtual const ComputerTacticsRules& rules() const = 0;
    virtual int difficultyIndex(const std::string&) const = 0;
    virtual int legacyScriptTicks(int) const = 0;
    virtual int countrySide(const std::string&) const = 0;
    virtual const std::vector<WorldObjectInstance>& objects() const = 0;
    virtual bool hostile(const std::string&, const std::string&) const = 0;
    virtual RuntimeCellPoint objectCell(const WorldObjectInstance&) const = 0;
    virtual double cellDistance(const RuntimeCellPoint&, const RuntimeCellPoint&) const = 0;
    virtual bool neutral(const std::string&) const = 0;
    virtual bool ownerMatches(const WorldObjectInstance&, const std::string&) const = 0;
    virtual std::optional<int> buildingPower(const std::string&) const = 0;
    virtual std::optional<int> enemyCredits(const std::string&) const = 0;
    virtual const std::map<std::string, RuntimeTaskForce>& taskForces() const = 0;
    virtual const std::map<std::string, RuntimeScriptType>& scripts() const = 0;
    virtual const std::vector<RuntimeCampaignAiTriggerDefinition>& triggers() const = 0;
    virtual const RuntimeTeamType* teamType(const std::string&) const = 0;
    virtual std::optional<RuntimeBuildCatalogItem> buildItem(const std::string&, const std::string&) const = 0;
    virtual bool itemAllowed(const RuntimeComputerAiState&, const RuntimeBuildCatalogItem&) const = 0;
};

// The original lazy initialization and sidebar query keep their actual owner.
class RuntimeComputerTacticSelectionEffects {
public:
    virtual ~RuntimeComputerTacticSelectionEffects() = default;
    virtual void ensureSuperWeaponsInitialized() = 0;
    virtual const std::vector<SuperWeaponSidebarSnapshot>& superWeaponSnapshots(const std::string&) = 0;
};

bool runtimeComputerTacticCompare(int actual, int expected, int comparator);
std::string runtimeComputerTacticEnemy(const RuntimeComputerTacticSelectionQueries&, const RuntimeComputerAiState&);
bool runtimeComputerTacticCondition(const RuntimeComputerTacticSelectionQueries&,
    RuntimeComputerTacticSelectionEffects&, const RuntimeComputerAiState&,
    const RuntimeCampaignAiTriggerDefinition&, const std::string& enemy);
bool runtimeComputerTacticTemplateBuildable(const RuntimeComputerTacticSelectionQueries&,
    const RuntimeComputerAiState&, const RuntimeTeamType&);
bool selectRuntimeComputerTactic(const RuntimeComputerTacticSelectionQueries&,
    RuntimeComputerTacticSelectionEffects&, RuntimeComputerAiState&, RuntimeComputerTacticsState&);
}
