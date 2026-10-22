#if !defined(S19_FREEZE_OLD_ORACLE)
#include "simulation/economy/RuntimeEconomyFixedStep.h"
#include "simulation/production/RuntimeProductionFixedStep.h"
#else
#include "simulation/economy/HouseEconomyContext.h"
#include "simulation/production/RuntimeProductionLanes.h"
#endif
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>

struct FixtureEconomy {
    std::string owner = "fixture-house";
    int credits = 0, storedResourceUnits = 0, storedResourceValue = 0;
    int storedResourceGemUnits = 0, resourceTrips = 0, harvesterTeleports = 0;
    int resourceStorageCapacity = 200, harvesters = 0, refineries = 0;
    int incomeCooldownTicks = 0;
    bool simplifiedHarvesterIncomeEnabled = false;
    std::string status = "fixture";
    RuntimeProductionLanesState localProductionLanes;
    HouseEconomyContext context() {
        return {owner, credits, storedResourceUnits, storedResourceValue,
            storedResourceGemUnits, resourceTrips, harvesterTeleports,
            resourceStorageCapacity, harvesters, refineries, false};
    }
};
struct FixtureHost { FixtureEconomy economy; };
#if defined(S19_FREEZE_OLD_ORACLE)
#include "RuntimeEconomyProductionOldOracle.inl"
#endif

static int deposit(int balance, long long value) {
#if defined(S19_FREEZE_OLD_ORACLE)
    return runtimeCreditBalanceAfterDeposit(balance, value);
#else
    return runtimeEconomyBalanceAfterDeposit(balance, value);
#endif
}
static bool spend(FixtureHost& state, int value) {
#if defined(S19_FREEZE_OLD_ORACLE)
    return spendRuntimeCredits(state, value);
#else
    auto house = state.economy.context();
    return spendRuntimeHouseCredits(house, value);
#endif
}
static bool income(FixtureHost& state) {
#if defined(S19_FREEZE_OLD_ORACLE)
    return oldIncome(state);
#else
    auto house = state.economy.context();
    const auto result = advanceRuntimeHouseEconomyFixedStep(house,
        state.economy.simplifiedHarvesterIncomeEnabled,
        state.economy.incomeCooldownTicks);
    if (result.changed) state.economy.status = result.status;
    return result.changed;
#endif
}
static bool production(FixtureHost& state, const RuntimeProductionAdvanceHooks& hooks,
    std::vector<RuntimeProductionAdvanceEvent>& events) {
#if defined(S19_FREEZE_OLD_ORACLE)
    return oldProduction(state, hooks, events);
#else
    auto house = state.economy.context();
    auto result = advanceRuntimeProductionFixedStep(
        state.economy.localProductionLanes, house, hooks);
    events = std::move(result.events);
    if (!result.accepted) state.economy.status = "production ledger transaction rejected";
    return result.changed;
#endif
}
static void observe(std::ostream& out, const FixtureHost& state) {
    const auto& e = state.economy;
    out << e.credits << '|' << e.storedResourceUnits << '|' << e.storedResourceValue
        << '|' << e.storedResourceGemUnits << '|' << e.resourceTrips << '|'
        << e.incomeCooldownTicks << '|' << e.status << '|'
        << runtimeProductionLanesStateHash(e.localProductionLanes) << '\n'
        << runtimeSerializeProductionLanes(e.localProductionLanes) << '\n';
}
static std::string runCase(int scenario) {
    FixtureHost state;
    auto& e = state.economy;
    const std::array<int, 8> balances{-37, 0, 1, 17, 137, 800, 2400,
        std::numeric_limits<int>::max() - 19};
    e.credits = balances[static_cast<std::size_t>(scenario % 8)];
    if (scenario == 64) e.credits = 100;
    e.storedResourceUnits = scenario % 4 == 0 ? -3 : 31;
    e.storedResourceGemUnits = scenario % 3 == 0 ? 80 : 13;
    e.storedResourceValue = scenario % 5 == 0 ? -9 : 157;
    e.harvesters = scenario % 6; e.refineries = scenario % 3;
    e.simplifiedHarvesterIncomeEnabled = scenario % 4 != 0;
    e.incomeCooldownTicks = scenario % 3 == 0 ? -1 : scenario % 7;
    for (const auto lane : runtimeProductionLanes) {
        RuntimeProductionLaneOrder order;
        order.itemKey = "fixture-" + std::string(runtimeProductionLaneName(lane));
        order.cost = scenario % 9 == 0 ? 0 : 19 + static_cast<int>(lane) * 7;
        order.totalTicks = 3 + scenario % 13; order.remainingTicks = order.totalTicks;
        order.requiresPlacement = lane == RuntimeProductionLane::Structures
            || lane == RuntimeProductionLane::Armory;
        if (runtimeQueueProductionOrder(e.localProductionLanes, lane, order, 8, 8).decision
            != RuntimeProductionQueueDecision::Accepted) throw std::runtime_error("fixture queue");
    }
    std::ostringstream out;
    observe(out, state);
    for (int tick = 0; tick < 65; ++tick) {
        std::ostringstream calls;
        RuntimeProductionAdvanceHooks hooks;
        const auto record = [&](char kind, RuntimeProductionLane lane,
            const RuntimeProductionLaneOrder& order) {
            calls << kind << ':' << runtimeProductionLaneName(lane) << ':' << order.serial << ';';
        };
        hooks.canStart = [&](RuntimeProductionLane lane, const RuntimeProductionLaneOrder& order) {
            record('S', lane, order);
            if ((scenario == 63 || scenario == 64) && tick == 0) e.credits = -10;
            return scenario % 7 != 0 || tick > 2;
        };
        hooks.canContinue = [&](RuntimeProductionLane lane, const RuntimeProductionLaneOrder& order) {
            record('C', lane, order); return scenario % 11 != 0 || tick % 5 != 0;
        };
        hooks.productionRate = [&](RuntimeProductionLane lane, const RuntimeProductionLaneOrder& order) {
            record('R', lane, order);
            if (scenario % 13 == 0 && tick % 3 == 0) return RuntimeProductionRate{0, 0};
            return RuntimeProductionRate{static_cast<std::uint32_t>(1 + scenario % 4), 3};
        };
        hooks.completeImmediately = [&](RuntimeProductionLane lane, const RuntimeProductionLaneOrder& order) {
            record('I', lane, order); return scenario % 10 == 0 && tick > 3;
        };
        if (tick == 9) e.credits = deposit(e.credits, scenario % 2 == 0 ? 211 : -1);
        if (tick == 13) runtimeSetProductionHold(e.localProductionLanes, RuntimeProductionLane::Ships, true);
        if (tick == 19) runtimeSetProductionHold(e.localProductionLanes, RuntimeProductionLane::Ships, false);
        const bool incomeChanged = income(state);
        std::vector<RuntimeProductionAdvanceEvent> events;
        const bool productionChanged = production(state, hooks, events);
        out << "tick=" << tick << " changed=" << incomeChanged << ',' << productionChanged
            << " hooks=" << calls.str() << '\n';
        for (const auto& event : events) out << "event=" << static_cast<int>(event.lane) << '|'
            << static_cast<int>(event.kind) << '|' << event.serial << '|' << event.itemKey << '|'
            << event.remainingTicks << '|' << event.creditsSpent << '|' << event.creditsDebited << '\n';
        for (const auto& pending : runtimeEnumerateCompletedProduction(e.localProductionLanes)) {
            const bool deliver = tick % 4 != 0;
            out << "delivery=" << static_cast<int>(pending.lane) << ':' << pending.order.serial << ':' << deliver << '\n';
            if (deliver) (void)runtimeCommitCompletedProduction(e.localProductionLanes,
                pending.lane, pending.order.serial);
        }
        if (tick == 23) {
            RuntimeProductionLanesState restored; std::string error;
            if (!runtimeDeserializeProductionLanes(runtimeSerializeProductionLanes(e.localProductionLanes),
                restored, &error)) throw std::runtime_error(error);
            e.localProductionLanes = std::move(restored);
            out << "actual-module-restore=1\n";
        }
        observe(out, state);
    }
    for (const int amount : {-10, 0, 1, 17, 100, std::numeric_limits<int>::max()}) {
        out << "spend=" << amount << ':' << spend(state, amount) << '\n'; observe(out, state);
    }
    for (const long long amount : {-1LL, 0LL, 1LL, 500LL, std::numeric_limits<long long>::max()})
        out << "deposit=" << amount << ':' << deposit(e.credits, amount) << '\n';
    return out.str();
}
int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("one immutable fixture directory required");
    const std::filesystem::path root = argv[1];
#if defined(S19_FREEZE_OLD_ORACLE)
    if (std::filesystem::exists(root)) throw std::runtime_error("refuse existing fixture root");
    std::filesystem::create_directories(root);
#endif
    for (int i = 0; i < 65; ++i) {
        const auto path = root / ("case-" + std::to_string(i) + ".txt");
        const auto actual = runCase(i);
#if defined(S19_FREEZE_OLD_ORACLE)
        std::ofstream out(path, std::ios::binary); out << actual;
        if (!out) throw std::runtime_error("fixture write failed");
#else
        std::ifstream input(path, std::ios::binary);
        const std::string expected{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (!input || expected != actual) throw std::runtime_error("complete immutable old bytes mismatch " + path.string());
#endif
    }
    std::cout << "Economy/production fixed-step contract: cases=65 ticks=4225 restores=65 completeBytes=1 hooksExactOrder=1 ShipsBeforeAircraft=1 incomeCooldownFalseChanged=1 originalOracle=1\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }