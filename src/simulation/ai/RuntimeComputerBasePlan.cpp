#include "simulation/ai/RuntimeComputerBasePlan.h"
#include "data/rules/RulesDatabase.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include "core/io/RuntimeSaveTextFields.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
namespace concord::simulation::ai {
using runtime_save_text::toLowerAsciiLocal;
using runtime_save_text::trimAsciiLocal;
using runtime_authority_hash::runtimeStableHashString;
using runtime_authority_hash::runtimeStableHashMix;
using runtime_authority_hash::runtimeStableHashMixString;
RuntimeComputerAiBaseTargets runtimeComputerAiBaseTargets(
    const RuntimeComputerBasePlanQueries& queries, const RuntimeComputerAiState& ai) {
    RuntimeComputerAiBaseTargets targets;
    const auto& rules = queries.baseBuildingRules();
    const int difficulty = queries.difficultyIndex(ai.difficulty);
    switch (queries.planningSide(ai.country)) {
    case 0: targets.defenses = rules.alliedDefenseCounts[difficulty]; break;
    case 1: targets.defenses = rules.sovietDefenseCounts[difficulty]; break;
    case 2: targets.defenses = rules.thirdDefenseCounts[difficulty]; break;
    default: break; // Retail 0x505C95 leaves an unknown planning side at zero.
    }
    // Retail 0x5059D6 checks the Owner mask of HarvesterUnit entries. The
    // exception is not a hardcoded Yuri-country check: mods can change it.
    targets.slaveRefineries = true;
    for (const auto& typeId : rules.harvesterTypes) {
        const auto* unit = queries.unitTypeById(typeId);
        if (unit != nullptr && queries.ownerListAllowsHouse(unit->owner, ai.country)) {
            targets.slaveRefineries = false;
            break;
        }
    }
    // The first refinery is already present in the retail plan. Slave miners
    // insert (AISlaveMinerNumber - 1); other houses insert AIExtraRefineries.
    targets.refineries = targets.slaveRefineries
        ? std::max(1, queries.miningRules().slaveMinerNumber[difficulty])
        : 1 + rules.extraRefineries[difficulty];
    return targets;
}

std::vector<RuntimeComputerAiStructureRole> runtimeComputerAiBasePlan(
    const RuntimeComputerBasePlanIdentityQueries& queries, const RuntimeComputerAiState& ai, const RuntimeComputerAiBaseTargets& targets) {
    using Role = RuntimeComputerAiStructureRole;
    // Keep the existing technology roles. Retail 0x505A8D inserts extra
    // refineries after the first, and 0x505CF7 inserts defenses after index 3.
    std::vector<Role> plan{Role::Power, Role::Refinery, Role::Barracks,
        Role::VehicleFactory, Role::Radar, Role::Tech, Role::Superweapon};
    std::uint64_t random = runtimeStableHashString("computer base plan/1");
    runtimeStableHashMixString(random, toLowerAsciiLocal(trimAsciiLocal(ai.owner)));
    runtimeStableHashMix(random, queries.planningSide(ai.country));
    runtimeStableHashMix(random, queries.difficultyIndex(ai.difficulty));
    const auto insert = [&](Role role, std::size_t first, int serial) {
        runtimeStableHashMix(random, serial);
        const auto position = first + static_cast<std::size_t>(random % (plan.size() - first + 1));
        plan.insert(plan.begin() + static_cast<std::ptrdiff_t>(position), role);
    };
    for (int i = 1; i < targets.refineries; ++i) insert(Role::Refinery, 2, i);
    for (int i = 0; i < targets.defenses; ++i) insert(Role::Defense, 4, i);
    return plan;
}

void appendRuntimeComputerAiBasePlanGoals(
    const RuntimeComputerBasePlanLimits& limits, const RuntimeComputerBasePlanQueries& queries, const RuntimeComputerAiState& ai,
    const RuntimeComputerAiMetrics& metrics,
    std::vector<RuntimeComputerAiStructureRole>& goals) {
    using Role = RuntimeComputerAiStructureRole;
    const auto targets = runtimeComputerAiBaseTargets(queries, ai);
    // Mobile and deployed forms fill the same quota; moving a slave miner
    // must not make the construction yard order an additional one.
    std::array<int, 9> remaining{};
    remaining[static_cast<std::size_t>(Role::Power)] = metrics.powerPlants;
    remaining[static_cast<std::size_t>(Role::Refinery)] = targets.slaveRefineries
        ? metrics.slaveMiners : metrics.ordinaryRefineries;
    remaining[static_cast<std::size_t>(Role::Barracks)] = metrics.barracks;
    remaining[static_cast<std::size_t>(Role::VehicleFactory)] = metrics.vehicleFactories;
    remaining[static_cast<std::size_t>(Role::Radar)] = metrics.radars;
    remaining[static_cast<std::size_t>(Role::Defense)] = metrics.defenses;
    remaining[static_cast<std::size_t>(Role::Tech)] = metrics.techCenters;
    remaining[static_cast<std::size_t>(Role::Superweapon)] = metrics.superweaponStructures;
    for (const Role role : runtimeComputerAiBasePlan(queries, ai, targets)) {
        auto& count = remaining[static_cast<std::size_t>(role)];
        if (count > 0) { --count; continue; }
        if (role == Role::Superweapon && (!limits.superweapons
                || metrics.structures < 8 || metrics.combatUnits < limits.attackThreshold)) continue;
        if (std::find(goals.begin(), goals.end(), role) == goals.end()) goals.push_back(role);
    }
}
}
