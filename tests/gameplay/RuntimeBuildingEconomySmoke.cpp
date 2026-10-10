#include "simulation/economy/RuntimeBuildingEconomy.h"
#include "simulation/production/RuntimeFactoryDeliveryState.h"
#include "core/io/IniFile.h"
#include <iostream>
#include <sstream>

int main() {
    bool ok = true;
    const auto check = [&](bool value, const char* description) {
        if (!value) { std::cerr << description << '\n'; ok = false; }
    };
    IniFile ini;
    ini.loadFromString("[General]\nRepairRate=.025\nRepairPercent=25%\nRepairStep=8\nRefundPercent=40%\n"
        "BallisticScatter=2\nTurboBoost=2.5\nOpenToppedRangeBonus=3\nOpenToppedDamageMultiplier=1.4\n"
        "[BuildingTypes]\n0=FACTORY\n1=UNREPAIRABLE\n[FACTORY]\nStrength=1000\nCost=2000\n"
        "PsychicDetectionRadius=15\n[UNREPAIRABLE]\nStrength=100\nCost=100\nClickRepairable=no\n"
        "[VehicleTypes]\n0=TRANSPORT\n[TRANSPORT]\nOpenTopped=yes\nOpportunityFire=yes\n"
        "DistributedFire=yes\nRadialFireSegments=10\nPrimary=GUN\n[GUN]\nDamage=10\nBurst=3\n"
        "BurstDelay=4\nBurstDelays=2,5\nTurboBoost=yes\nProjectile=SHELL\n[SHELL]\nInaccurate=yes\nFlakScatter=yes\n");
    RulesDatabase rules;
    rules.loadFromIni(ini);
    const auto* building = rules.findBuildingType("FACTORY");
    check(building != nullptr, "building parsed");
    if (building) {
        const auto& repair = rules.getSpecialMechanismRules();
        check(runtimeBuildingRepairInterval(repair, 40) == 60, "authored repair interval uses simulation tick rate");
        auto charge = runtimeBuildingRepairCharge(building, repair, 900, 1000, 1);
        check(charge.healed == 2 && charge.cost == 1, "low credit cannot buy full repair step");
        charge = runtimeBuildingRepairCharge(building, repair, 999, 1000, 100);
        check(charge.healed == 1 && charge.cost == 1, "final step only restores missing health");
        charge = runtimeBuildingRepairCharge(building, repair, 900, 1000, 0);
        check(charge.healed == 0 && charge.cost == 0, "no credit pauses paid repair");
        auto freeRepair = repair; freeRepair.repairPercent = 0;
        check(runtimeBuildingRepairCharge(building, freeRepair, 900, 1000, 0).healed == 8,
            "authored free repair works without credits");
        check(runtimeBuildingSaleRefund(building, rules.getRefundPercent()) == 800, "nondefault refund percent");
        auto overrideBuilding = *building; overrideBuilding.soylent = 300;
        check(runtimeBuildingSaleRefund(&overrideBuilding, .4) == 300, "fixed Soylent overrides sale refund ratio");
        check(building->psychicDetectionRadius == 15, "psychic detection parsed");
    }
    check(runtimeBuildingRepairCharge(rules.findBuildingType("UNREPAIRABLE"),
        rules.getSpecialMechanismRules(), 1, 100, 100).healed == 0, "ClickRepairable veto");
    const auto* transport = rules.findUnitType("Vehicle", "TRANSPORT");
    // RulesDatabase uses Unit for vehicle object lookup.
    if (!transport) transport = rules.findUnitType("Unit", "TRANSPORT");
    check(transport && transport->openTopped && transport->opportunityFire
        && transport->distributedFire && transport->radialFireSegments == 10, "transport fire flags parsed");
    const auto* weapon = rules.findWeaponType("GUN");
    check(weapon && weapon->turboBoost && weapon->burstDelay == 4
        && weapon->burstDelays == std::vector<int>({2,5}), "burst delays and turbo parsed");
    const auto* projectile = rules.findProjectileType("SHELL");
    check(projectile && projectile->inaccurate && projectile->flakScatter, "projectile scatter parsed");
    check(rules.getCombatDetailRules().openToppedRangeBonus == 3
        && rules.getCombatDetailRules().ballisticScatter == 2, "global combat details parsed");
    RuntimeFactoryDeliveryState pending;
    check(pending.deferFreeUnit({9,"House A","FACTORY","MINER"}), "blocked gift retained");
    check(!pending.deferFreeUnit({9,"House A","FACTORY","MINER"}), "gift cannot be queued twice");
    std::ostringstream saved; pending.save(saved);
    IniFile save; save.loadFromString(saved.str());
    RuntimeFactoryDeliveryState restored;
    std::string error;
    check(restored.load(save,&error) && restored.stateHash() == pending.stateHash(), "gift roundtrip and hash");
    auto corruptText = saved.str();
    const auto countAt = corruptText.find("FreeUnitCount=1");
    if (countAt != std::string::npos) corruptText.replace(countAt, 15, "FreeUnitCount=-1");
    IniFile corrupt; corrupt.loadFromString(corruptText);
    check(!restored.load(corrupt,&error) && restored.stateHash() == pending.stateHash(), "corrupt load atomic");
    restored.releaseByHost(9);
    check(restored.pendingFreeUnits().empty(), "host sold or destroyed cancels gift");
    restored = pending; restored.completeFreeUnit(9); restored.completeFreeUnit(9);
    check(restored.pendingFreeUnits().empty(), "successful gift consumed once");
    std::cout << (ok ? "building economy details passed\n" : "building economy details FAILED\n");
    return ok ? 0 : 1;
}
