#pragma once
#include "simulation/ai/RuntimeComputerBasePlanValues.h"
#include "simulation/ai/RuntimeComputerAiState.h"
#include "simulation/ai/RuntimeComputerAiMetrics.h"
#include <string>
#include <vector>
struct ComputerBaseBuildingRules;
struct ComputerMiningRules;
struct UnitType;
namespace concord::simulation::ai {
using RuntimeComputerAiStructureRole=concord::runtime::session_detail::RuntimeComputerAiStructureRole;
using RuntimeComputerAiBaseTargets=concord::runtime::session_detail::RuntimeComputerAiBaseTargets;
class RuntimeComputerBasePlanIdentityQueries {
public:
    virtual ~RuntimeComputerBasePlanIdentityQueries()=default;
    virtual int difficultyIndex(const std::string&) const=0;
    virtual int planningSide(const std::string&) const=0;
};
class RuntimeComputerBasePlanQueries : public RuntimeComputerBasePlanIdentityQueries {
public:
    virtual ~RuntimeComputerBasePlanQueries()=default;
    virtual const ComputerBaseBuildingRules& baseBuildingRules() const=0;
    virtual const ComputerMiningRules& miningRules() const=0;
    virtual const UnitType* unitTypeById(const std::string&) const=0;
    virtual bool ownerListAllowsHouse(const std::string&,const std::string&) const=0;
};
struct RuntimeComputerBasePlanLimits {
    const bool& superweapons;
    const int& attackThreshold;
};
RuntimeComputerAiBaseTargets runtimeComputerAiBaseTargets(const RuntimeComputerBasePlanQueries&,const RuntimeComputerAiState&);
std::vector<RuntimeComputerAiStructureRole> runtimeComputerAiBasePlan(const RuntimeComputerBasePlanIdentityQueries&,const RuntimeComputerAiState&,const RuntimeComputerAiBaseTargets&);
void appendRuntimeComputerAiBasePlanGoals(const RuntimeComputerBasePlanLimits&,const RuntimeComputerBasePlanQueries&,const RuntimeComputerAiState&,const RuntimeComputerAiMetrics&,std::vector<RuntimeComputerAiStructureRole>&);
}
