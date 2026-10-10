// Item 8: retail quantity semantics applied to the engine's existing base roles.
// This is not the retail global PRNG or a complete AIBuildThis plan generator.
struct RuntimeComputerAiBaseTargets {
    int defenses = 0;
    int refineries = 1;
    bool slaveRefineries = false;
};

RuntimeComputerAiBaseTargets runtimeComputerAiBaseTargets(
    const RuntimeWindowState& state, const RuntimeComputerAiState& ai
) {
    RuntimeComputerAiBaseTargets targets;
    const auto& rules = state.rules.getComputerBaseBuildingRules();
    const int difficulty = runtimeComputerMiningDifficultyIndex(ai.difficulty);
    switch (runtimeBasePlanningSideForCountry(ai.country)) {
    case 0: targets.defenses = rules.alliedDefenseCounts[difficulty]; break;
    case 1: targets.defenses = rules.sovietDefenseCounts[difficulty]; break;
    case 2: targets.defenses = rules.thirdDefenseCounts[difficulty]; break;
    default: break; // Retail 0x505C95 leaves an unknown planning side at zero.
    }
    // Retail 0x5059D6 checks the Owner mask of HarvesterUnit entries. The
    // exception is not a hardcoded Yuri-country check: mods can change it.
    targets.slaveRefineries = true;
    for (const auto& typeId : rules.harvesterTypes) {
        const auto* unit = findRuntimeUnitTypeById(state.rules, typeId);
        if (unit != nullptr && runtimeOwnerListAllowsHouse(unit->owner, ai.country)) {
            targets.slaveRefineries = false;
            break;
        }
    }
    // The first refinery is already present in the retail plan. Slave miners
    // insert (AISlaveMinerNumber - 1); other houses insert AIExtraRefineries.
    targets.refineries = targets.slaveRefineries
        ? std::max(1, state.rules.getComputerMiningRules().slaveMinerNumber[difficulty])
        : 1 + rules.extraRefineries[difficulty];
    return targets;
}

std::vector<RuntimeComputerAiStructureRole> runtimeComputerAiBasePlan(
    const RuntimeComputerAiState& ai, const RuntimeComputerAiBaseTargets& targets
) {
    using Role = RuntimeComputerAiStructureRole;
    // Keep the existing technology roles. Retail 0x505A8D inserts extra
    // refineries after the first, and 0x505CF7 inserts defenses after index 3.
    std::vector<Role> plan{Role::Power, Role::Refinery, Role::Barracks,
        Role::VehicleFactory, Role::Radar, Role::Tech, Role::Superweapon};
    std::uint64_t random = runtimeStableHashString("computer base plan/1");
    runtimeStableHashMixString(random, toLowerAsciiLocal(trimAsciiLocal(ai.owner)));
    runtimeStableHashMix(random, runtimeBasePlanningSideForCountry(ai.country));
    runtimeStableHashMix(random, runtimeComputerMiningDifficultyIndex(ai.difficulty));
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
    const RuntimeWindowState& state, const RuntimeComputerAiState& ai,
    const RuntimeComputerAiProfile& profile, const RuntimeComputerAiMetrics& metrics,
    std::vector<RuntimeComputerAiStructureRole>& goals
) {
    using Role = RuntimeComputerAiStructureRole;
    const auto targets = runtimeComputerAiBaseTargets(state, ai);
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
    for (const Role role : runtimeComputerAiBasePlan(ai, targets)) {
        auto& count = remaining[static_cast<std::size_t>(role)];
        if (count > 0) { --count; continue; }
        if (role == Role::Superweapon && (!state.config.superweapons
                || metrics.structures < 8 || metrics.combatUnits < profile.attackThreshold)) continue;
        if (std::find(goals.begin(), goals.end(), role) == goals.end()) goals.push_back(role);
    }
}
