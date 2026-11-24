#pragma once
#include "simulation/ai/RuntimeComputerAiState.h"
#include "simulation/ai/RuntimeComputerTacticReservations.h"
struct UnitType;
struct WorldObjectInstance;
namespace concord::simulation::ai {
// Synchronous typed borrows to existing owners. No private Session or host.
class RuntimeComputerTacticRecruitmentQueries : public RuntimeComputerTacticObjectReservationQueries {
public:
    virtual bool ownerMatches(const WorldObjectInstance&, const std::string&) const = 0;
    virtual bool disabledByParasite(int objectId) const = 0;
    virtual const UnitType* unitType(const WorldObjectInstance&) const = 0;
    virtual bool hasAttackAmmo(const WorldObjectInstance&) const = 0;
    virtual const RuntimeTaskForce* taskForce(const std::string&) const = 0;
    virtual const RuntimeTeamType* teamType(const std::string&) const = 0;
    virtual const std::vector<WorldObjectInstance>& objects() const = 0;
    virtual RuntimeCellPoint objectCell(const WorldObjectInstance&) const = 0;
    virtual double cellDistance(const RuntimeCellPoint&, const RuntimeCellPoint&) const = 0;
    virtual const WorldObjectInstance* object(int objectId) const = 0;
    virtual RuntimeCellPoint missionCenter(const RuntimeTeamMissionState&) const = 0;
};
class RuntimeComputerTacticRecruitmentEffects {
public:
    virtual ~RuntimeComputerTacticRecruitmentEffects() = default;
    virtual void detachMember(int objectId) = 0;
    virtual void eraseBehavior(int objectId) = 0;
    virtual WorldObjectInstance* mutableObject(int objectId) = 0;
    virtual int issueTeamAttack(const std::vector<int>&, const WorldObjectInstance&) = 0;
    virtual int issueTeamMove(const std::vector<int>&, const RuntimeCellPoint&) = 0;
};
struct RuntimeComputerTacticRecruitmentState {
    const int& tick;
    std::map<int, RuntimeTeamMissionState>& teamMissions;
    const std::map<std::string, RuntimeScriptType>& scripts;
};
int runtimeComputerTacticGroup(const RuntimeComputerTacticRecruitmentQueries&, const RuntimeTeamType&);
bool runtimeComputerTacticMayRecruit(const RuntimeComputerTacticRecruitmentQueries&, const RuntimeComputerAiState&, const RuntimeComputerTacticTeam&, const RuntimeTeamType&, const WorldObjectInstance&);
bool recruitRuntimeComputerTacticMembers(const RuntimeComputerTacticRecruitmentState&, const RuntimeComputerTacticRecruitmentQueries&, RuntimeComputerTacticRecruitmentEffects&, RuntimeComputerAiState&, RuntimeComputerTacticsState&, RuntimeComputerTacticTeam&, const RuntimeTeamType&);
bool joinRuntimeComputerTacticReinforcements(const RuntimeComputerTacticRecruitmentState&, const RuntimeComputerTacticRecruitmentQueries&, RuntimeComputerTacticRecruitmentEffects&, RuntimeComputerTacticTeam&, const RuntimeTeamType&);
}
