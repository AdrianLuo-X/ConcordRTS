#include "RuntimeComputerAiTriggerHashOldOracle.inl"
#ifndef CONCORD_OLD_AI_TRIGGER_HASH_ORACLE
#include "simulation/ai/RuntimeComputerAiTriggerHash.h"
#endif
#include <iostream>
#include <limits>
#include <utility>

#ifdef CONCORD_OLD_AI_TRIGGER_HASH_ORACLE
using Definition = original_ai_trigger_hash::RuntimeCampaignAiTriggerDefinition;
static void mix(std::uint64_t& hash, const std::vector<Definition>& definitions) {
    original_ai_trigger_hash::mix(hash, definitions);
}
#else
using Definition = RuntimeCampaignAiTriggerDefinition;
static void mix(std::uint64_t& hash, const std::vector<Definition>& definitions) {
    mixRuntimeComputerAiTriggerDefinitions(hash, definitions);
}
#endif

int main() {
    int checks = 0, failures = 0;
    const auto need = [&](bool ok, const char* label) {
        ++checks; if (!ok) { ++failures; std::cerr << label << '\n'; }
    };
    constexpr std::uint64_t seed = 0x0123456789abcdefULL;
    const auto hashOf = [](const auto& definitions) { std::uint64_t hash = seed; mix(hash, definitions); return hash; };
    const std::vector<Definition> empty;
    need(hashOf(empty) == (seed ^ (0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2))), "empty vector mixes original count");
    Definition base;
    base.id = "TRIGGER"; base.primaryTeamId = "PRIMARY"; base.secondaryTeamId = "SECONDARY";
    base.owner = "HOUSE"; base.conditionObject = "TARGET";
    base.techLevel = 9; base.conditionType = 3; base.conditionNumber = -2;
    base.conditionComparator = 5; base.side = -1; base.scope = 7;
    base.currentWeight = 1.25; base.minimumWeight = -0.5; base.maximumWeight = 9.75;
    base.availableInSkirmish = true; base.baseDefense = false; base.enabled = true; base.valid = true;
    std::vector<Definition> original{base}; const auto expectedBase = hashOf(original);
    auto canonical = original;
    canonical[0].id = " \ttrigger\n"; canonical[0].primaryTeamId = "primary ";
    canonical[0].secondaryTeamId = "\rsecondary"; canonical[0].owner = " house ";
    canonical[0].conditionObject = "target\t";
    need(hashOf(canonical) == expectedBase, "original canonical identifier equivalence");
    for (auto field : {&Definition::id, &Definition::primaryTeamId, &Definition::secondaryTeamId, &Definition::owner, &Definition::conditionObject}) {
        auto changed = original; changed[0].*field += "X";
        need(hashOf(changed) != expectedBase, "all five identifiers affect definition hash");
    }
    for (auto field : {&Definition::techLevel, &Definition::conditionType, &Definition::conditionNumber, &Definition::conditionComparator, &Definition::side, &Definition::scope}) {
        auto changed = original; ++(changed[0].*field);
        need(hashOf(changed) != expectedBase, "all six integer fields affect definition hash");
    }
    for (auto field : {&Definition::availableInSkirmish, &Definition::baseDefense, &Definition::enabled, &Definition::valid}) {
        auto changed = original; changed[0].*field = !(changed[0].*field);
        need(hashOf(changed) != expectedBase, "all four booleans affect definition hash");
    }
    for (std::size_t j = 0; j < 3; ++j) {
        auto changed = original; changed[0].difficultyEnabled[j] = false;
        need(hashOf(changed) != expectedBase, "difficulty flag position matters");
    }
    for (auto field : {&Definition::currentWeight, &Definition::minimumWeight, &Definition::maximumWeight}) {
        auto plus = original, minus = original;
        plus[0].*field = 0.0; minus[0].*field = -0.0;
        need(hashOf(plus) == hashOf(minus), "signed zero canonicalization");
        plus[0].*field = std::bit_cast<double>(0x7ff8000000000001ULL);
        minus[0].*field = std::bit_cast<double>(0xfff8000000000017ULL);
        need(hashOf(plus) == hashOf(minus), "NaN payload and sign canonicalization");
        plus[0].*field = std::numeric_limits<double>::infinity();
        minus[0].*field = -std::numeric_limits<double>::infinity();
        need(hashOf(plus) != hashOf(minus), "infinity sign retained");
    }
    auto runtimeOnly = original;
    runtimeOnly[0].name = "presentation label"; runtimeOnly[0].timesExecuted = 21;
    runtimeOnly[0].timesCompleted = 17; runtimeOnly[0].timesFailed = 4; runtimeOnly[0].nextEligibleTick = 500;
    need(hashOf(runtimeOnly) == expectedBase, "definition hash excludes original runtime counters and label");
    auto two = original; auto other = base; other.id = "OTHER"; two.push_back(other);
    const auto beforeOrder = hashOf(two); std::swap(two[0], two[1]);
    need(hashOf(two) != beforeOrder, "original vector order preserved");
    for (int n = 0; n < 192; ++n) {
        std::vector<Definition> definitions;
        for (int j = 0; j < n % 7; ++j) {
            auto value = base;
            value.id = " \t" + std::to_string(n * 17 + j) + "x\r";
            value.primaryTeamId = std::string("Ab\0Cd", 5);
            value.conditionObject = std::string("\xc3\xa9", 2) + std::to_string(j);
            value.techLevel = n - 80; value.conditionType = j; value.conditionNumber = -n;
            value.currentWeight = n * 0.125; value.minimumWeight = -j * 0.25; value.maximumWeight = (n + j) * 1.5;
            value.side = j - 1; value.scope = n % 5;
            value.enabled = (n & 1) != 0; value.valid = (j & 1) != 0;
            value.difficultyEnabled = {{(n & 1) != 0, (n & 2) != 0, (n & 4) != 0}};
            definitions.push_back(std::move(value));
        }
        for (std::uint64_t initial : std::array<std::uint64_t, 4>{{0ULL, 1ULL, seed, 0xffffffffffffffffULL}}) {
            auto actual = initial, reference = initial;
            mix(actual, definitions); original_ai_trigger_hash::mix(reference, definitions);
            need(actual == reference, "frozen whole original fragment and primitive differential");
        }
    }
    std::cout << "AI trigger definition hash: checks=" << checks << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
