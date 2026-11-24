#pragma once
#include "simulation/ai/RuntimeComputerAiState.h"
#include <string>
#include <unordered_map>
class RulesDatabase;
class SuperWeaponSystem;
struct SuperWeaponRule;
struct SuperWeaponWorldContext;
namespace concord::simulation::superweapons {
class RuntimeSuperWeaponAiBindings {
public:
    virtual ~RuntimeSuperWeaponAiBindings() = default;
    virtual void updateBaseCenter(RuntimeComputerAiState& ai) = 0;
    virtual bool reservesSuperWeapon(const std::string& owner, const SuperWeaponRule* rule) const = 0;
};
int runtimeAiSuperDefenseProbabilityIndex(const std::string& difficulty);
bool runtimeIsForceShieldSuperWeaponRule(const SuperWeaponRule* rule);
bool advanceRuntimeSuperWeaponAiPolicy(bool computerAiEnabled,
    std::unordered_map<std::string, RuntimeComputerAiState>& computerAiByOwner,
    const RulesDatabase& rules, SuperWeaponSystem& superWeapons,
    const SuperWeaponWorldContext& context, const int& tick,
    const bool& campaignMode, const std::string& humanOwner,
    RuntimeSuperWeaponAiBindings& bindings);
}
