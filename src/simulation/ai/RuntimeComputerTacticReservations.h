#pragma once
#include "simulation/ai/RuntimeComputerTactics.h"
#include "simulation/triggers/RuntimeTriggerState.h"
#include <map>
#include <string>
#include <vector>
struct RuntimeGrindingRelation;
struct SuperWeaponRule;
namespace concord::simulation::ai {
class RuntimeComputerTacticObjectReservationQueries {
public:
    virtual ~RuntimeComputerTacticObjectReservationQueries() = default;
    virtual bool pendingTransportTarget(int) const = 0;
    virtual bool pendingEngineerCapture(int) const = 0;
    virtual bool pendingOccupyTarget(int) const = 0;
    virtual const std::vector<RuntimeGrindingRelation>& grindingRelations() const = 0;
    virtual const std::map<std::string,RuntimeComputerTacticsState>& ownerTactics() const = 0;
};
bool runtimeComputerTacticReservesObject(const RuntimeComputerTacticObjectReservationQueries&, int objectId);
struct RuntimeComputerTacticSuperReservationState {
    const bool& campaignMode;
    const std::map<std::string,RuntimeComputerTacticsState>& computerTacticsByOwner;
    const std::map<std::string,RuntimeTeamType>& teams;
    const std::map<std::string,RuntimeScriptType>& scripts;
    const std::map<int,RuntimeTeamMissionState>& teamMissions;
};
bool runtimeComputerTacticReservesSuperWeapon(const RuntimeComputerTacticSuperReservationState&, const std::string& owner, const SuperWeaponRule*);
}
