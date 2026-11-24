#include "RuntimeSuperWeaponAiPolicy.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/superweapons/SuperWeaponSystem.h"
#include <algorithm>
#include <vector>
namespace concord::simulation::superweapons {
using runtime_authority_hash::toUpperAsciiLocal;
using runtime_save_text::trimAsciiLocal;
using runtime_save_text::toLowerAsciiLocal;
int runtimeAiSuperDefenseProbabilityIndex(const std::string& difficulty) {
    const std::string key = toUpperAsciiLocal(trimAsciiLocal(difficulty));
    if (key == "HARD" || key == "BRUTAL") {
        return 0;
    }
    if (key == "EASY") {
        return 2;
    }
    return 1;
}
bool runtimeIsForceShieldSuperWeaponRule(const SuperWeaponRule* rule) {
    if (rule == nullptr) {
        return false;
    }
    const std::string identity = toLowerAsciiLocal(
        trimAsciiLocal(rule->type) + " " + trimAsciiLocal(rule->action)
    );
    return identity.find("forceshield") != std::string::npos
        || identity.find("force shield") != std::string::npos;
}
bool advanceRuntimeSuperWeaponAiPolicy(bool computerAiEnabled,
    std::unordered_map<std::string, RuntimeComputerAiState>& computerAiByOwner,
    const RulesDatabase& rules, SuperWeaponSystem& superWeapons,
    const SuperWeaponWorldContext& context, const int& tick,
    const bool& campaignMode, const std::string& humanOwner,
    RuntimeSuperWeaponAiBindings& bindings) {
    bool changed = false;
    std::vector<std::string> computerOwners;
    computerOwners.reserve(computerAiByOwner.size());
    for (const auto& [ownerKey, ai] : computerAiByOwner) {
        (void)ai;
        computerOwners.push_back(ownerKey);
    }
    std::sort(computerOwners.begin(), computerOwners.end());
    const SpecialMechanismRules& mechanismRules =
        rules.getSpecialMechanismRules();
    for (const std::string& ownerKey : computerOwners) {
        if (!computerAiEnabled) {
            break;
        }
        auto found = computerAiByOwner.find(ownerKey);
        if (found == computerAiByOwner.end()
            || !found->second.initialized
            || trimAsciiLocal(found->second.owner).empty()) {
            continue;
        }
        RuntimeComputerAiState& ai = found->second;
        bindings.updateBaseCenter(ai);
        const int probabilityIndex =
            runtimeAiSuperDefenseProbabilityIndex(ai.difficulty);
        std::string reason;
        if (superWeapons.fireAiDefense(
                ai.owner,
                {ai.baseCenter.x, ai.baseCenter.y},
                mechanismRules.aiSuperDefense.probabilityPercent[
                    static_cast<std::size_t>(probabilityIndex)
                ],
                mechanismRules.aiSuperDefense.alertFrames,
                mechanismRules.aiSuperDefense.distanceCells,
                context,
                &reason
            )) {
            changed = true;
        }
    }

    // The runtime does not have a separate computer-player command queue
    // yet.  Fire ready enemy super weapons at a fixed cadence through the
    // same validated command path used by the player, so AI effects remain
    // deterministic and replay-visible instead of bypassing the system.
    if (computerAiEnabled
        && tick > 0
        && (tick % std::max(1, context.fixedTickHz)) == 0) {
        std::vector<std::pair<std::string, std::string>> aiWeapons;
        for (const SuperWeaponSidebarSnapshot& snapshot :
                superWeapons.sidebarSnapshots()) {
            const SuperWeaponRule* rule = rules.findSuperWeaponType(
                snapshot.superWeaponId
            );
            if (snapshot.state == SuperWeaponRuntimeState::Ready
                && !runtimeIsForceShieldSuperWeaponRule(rule)
                && !bindings.reservesSuperWeapon(snapshot.owner, rule)
                && (campaignMode || computerAiByOwner.contains(toLowerAsciiLocal(trimAsciiLocal(snapshot.owner))))
                && toLowerAsciiLocal(trimAsciiLocal(snapshot.owner))
                    != toLowerAsciiLocal(trimAsciiLocal(humanOwner))) {
                aiWeapons.emplace_back(snapshot.owner, snapshot.superWeaponId);
            }
        }
        for (const auto& [owner, superWeaponId] : aiWeapons) {
            std::string reason;
            if (superWeapons.fireAi(owner, superWeaponId, context, &reason)) {
                changed = true;
            }
        }
    }
    return changed;
}
}
