#include "simulation/combat/targeting/RuntimeCombatDetails.h"

#include <iostream>
#include <set>

int main() {
    auto check = [](bool condition, const char* message) {
        if (!condition) std::cerr << message << '\n';
        return condition;
    };
    if (!check(runtimeCombatBurstDelayTicks(3, {}, 1, 30) == 6,
        "burst delay must convert authored frames to fixed ticks")) return 1;
    if (!check(runtimeCombatBurstDelayTicks(3, {2, 4}, 1, 15) == 2
        && runtimeCombatBurstDelayTicks(3, {2, 4}, 2, 15) == 4
        && runtimeCombatBurstDelayTicks(3, {2, 4}, 255, 15) == 4,
        "per-round delays and final-delay extension")) return 1;
    if (!check(runtimeCombatBurstDelayTicks(std::numeric_limits<int>::max(), {}, 1,
        std::numeric_limits<int>::max()) == 2400000, "burst delay overflow guard")) return 1;

    std::set<std::pair<int, int>> scatterPoints;
    for (std::uint64_t seed = 0; seed < 4096; ++seed) {
        const auto offset = runtimeCombatScatterOffset(seed, 1.0);
        const auto repeat = runtimeCombatScatterOffset(seed, 1.0);
        if (!check(offset.xLeptons == repeat.xLeptons && offset.yLeptons == repeat.yLeptons,
                "scatter must replay identically")
            || !check(offset.xLeptons * offset.xLeptons + offset.yLeptons * offset.yLeptons <= 65536,
                "scatter exceeded authored radius")) return 1;
        scatterPoints.emplace(offset.xLeptons, offset.yLeptons);
    }
    if (!check(scatterPoints.size() > 3500, "scatter collapsed to repeated offsets")) return 1;
    if (!check(runtimeCombatScatterOffset(1, 0).xLeptons == 0
        && runtimeCombatScatterOffset(1, std::numeric_limits<double>::infinity()).yLeptons == 0,
        "invalid scatter input guard")) return 1;
    if (!check(runtimeCombatRadialFacing(0, 10, 0) == 64
        && runtimeCombatRadialFacing(0, 10, 9) == 205
        && runtimeCombatRadialFacing(0, 10, 10) == 64,
        "radial arc must use ten native half-circle segments and wrap")) return 1;
    if (!check(runtimeCombatProjectileSpeedMultiplier(true, true, 1.5) == 1.5
        && runtimeCombatProjectileSpeedMultiplier(true, false, 1.5) == 1.0
        && runtimeCombatProjectileSpeedMultiplier(false, true, 1.5) == 1.0,
        "turbo boost must be gated by weapon and target domain")) return 1;

    RuntimeCombatDetailsState state;
    RuntimePendingWeaponBurst burst;
    burst.attackerObjectId = 12;
    burst.sourceAnchorObjectId = 11;
    burst.targetObjectId = 27;
    burst.targetX = 22; burst.targetY = 19;
    burst.weaponSlot = 9;
    burst.nextShotIndex = 2; burst.roundsRemaining = 2;
    burst.nextFireTick = 107;
    burst.outgoingDamageMultiplier = 1.2;
    burst.independentFire = true;
    burst.owner = "House A";
    burst.sourceType = "E1";
    burst.weaponId = "Mod|Weapon;with spaces";
    state.bursts.emplace(12, burst);
    state.fireCursors.emplace(12, RuntimeCombatFireCursor{27, 3, 11});
    state.legacyProjectileRounds.emplace(3, 3);
    std::ostringstream saved;
    state.save(saved);
    IniFile ini;
    if (!check(ini.loadFromString(saved.str()), "serialized INI rejected")) return 1;
    RuntimeCombatDetailsState restored;
    std::string error;
    if (!check(restored.load(ini, error), error.c_str())
        || !check(restored.deterministicHash() == state.deterministicHash(),
            "mid-burst save/load must retain all authoritative state")
        || !check(restored.bursts.at(12).weaponId == burst.weaponId,
            "weapon IDs must survive INI comment/delimiter characters")) return 1;
    const auto baseline = restored.deterministicHash();
    std::string invalid = saved.str();
    const auto countAt = invalid.find("BurstCount=1");
    invalid.replace(countAt, std::string("BurstCount=1").size(), "BurstCount=2");
    IniFile bad;
    bad.loadFromString(invalid);
    if (!check(!restored.load(bad, error) && restored.deterministicHash() == baseline,
        "invalid load must preserve the previous combat state atomically")) return 1;
    std::string invalidLegacy = saved.str();
    const auto legacyAt = invalidLegacy.find("Legacy0=3 3");
    invalidLegacy.replace(legacyAt, std::string("Legacy0=3 3").size(), "Legacy0=3 1");
    bad.loadFromString(invalidLegacy);
    if (!check(!restored.load(bad, error) && restored.deterministicHash() == baseline,
        "invalid aggregate round count must be rejected atomically")) return 1;
    burst.outgoingDamageMultiplier = std::numeric_limits<double>::quiet_NaN();
    if (!check(!RuntimeCombatDetailsState::validBurst(burst), "NaN burst state must be rejected")) return 1;
    burst.outgoingDamageMultiplier = 1;
    burst.roundsRemaining = std::numeric_limits<int>::max();
    if (!check(!RuntimeCombatDetailsState::validBurst(burst), "overflowing round count must be rejected")) return 1;
    IniFile legacy;
    legacy.loadFromString("[Game]\nTick=5\n");
    if (!check(restored.load(legacy, error) && restored.bursts.empty() && restored.fireCursors.empty()
        && restored.legacyProjectileRounds.empty(),
        "legacy saves must initialize empty burst state")) return 1;
    std::cout << "Combat details: deterministic scatter, burst cadence, radial arc, turbo gating, atomic save/load passed\n";
    return 0;
}
