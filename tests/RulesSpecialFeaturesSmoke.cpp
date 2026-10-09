#include "IniFile.h"
#include "RulesDatabase.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "rules special-feature smoke failure: " << message << '\n';
    }
    return condition;
}

bool closeTo(double actual, double expected) {
    return std::abs(actual - expected) < 0.000001;
}

bool hasWarning(const RulesDatabase& rules, const std::string& fragment) {
    for (const std::string& warning : rules.getWarnings()) {
        if (warning.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool hasAbility(
    const std::vector<VeteranAbility>& abilities,
    VeteranAbility expected
) {
    return std::find(abilities.begin(), abilities.end(), expected)
        != abilities.end();
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--stock") {
        IniFile stockIni;
        bool stockSuccess = expect(
            stockIni.loadFromFile(argv[2]),
            "stock rules did not load"
        );
        RulesDatabase stockRules;
        stockSuccess &= expect(
            stockRules.loadFromIni(stockIni),
            "stock RulesDatabase load failed"
        );
        const SpecialMechanismRules& stock =
            stockRules.getSpecialMechanismRules();
        const auto* meleeDeath = stockRules.findWarheadType("Battering");
        const auto* mutationDeath = stockRules.findWarheadType("Mutate");
        stockSuccess &= expect(meleeDeath && mutationDeath
            && stockRules.getInfantryDeathAnim(9) == "GENDEATH"
            && stockRules.getInfantryDeathAnim(10) == "BRUTDIE"
            && meleeDeath->infDeath == 10 && mutationDeath->infDeath == 9,
            "Brute melee and genetic mutation must retain their distinct retail victim animations");
        const auto* chronoCommando = stockRules.findUnitType("Infantry", "CCOMAND");
        const auto* chronoIvan = stockRules.findUnitType("Infantry", "CIVAN");
        const auto* psiCommando = stockRules.findUnitType("Infantry", "PTROOP");
        stockSuccess &= expect(chronoCommando && chronoIvan && psiCommando
            && chronoCommando->requiresStolenAlliedTech && !chronoCommando->requiresStolenThirdTech
            && chronoIvan->requiresStolenSovietTech && !chronoIvan->requiresStolenAlliedTech
            && psiCommando->requiresStolenThirdTech && !psiCommando->requiresStolenSovietTech,
            "stock infiltration-only infantry must retain their stolen technology requirements");
        stockSuccess &= expect(closeTo(stock.bunkerDamageMultiplier, 1.3)
            && closeTo(stock.bunkerRofMultiplier, 1.3)
            && closeTo(stock.bunkerWeaponRangeBonus, 2.0), "stock tank bunker bonuses");
        stockSuccess &= expect(stockRules.findUnitType("UnitType", "MTNK")->bunkerable
            && !stockRules.findUnitType("UnitType", "HARV")->bunkerable
            && !stockRules.findUnitType("InfantryType", "E1")->bunkerable,
            "vehicle Bunkerable default and original exclusions");
        const SpecialBuildingRules& stockSpecialBuildings =
            stockRules.getSpecialBuildingRules();
        const ElevationModelRules& stockElevation =
            stockRules.getElevationModelRules();
        stockSuccess &= expect(
            closeTo(stock.conditionRed, 0.25)
                && closeTo(stock.conditionYellow, 0.5),
            "stock ConditionRed/ConditionYellow values"
        );
        stockSuccess &= expect(
            stock.aiSuperDefense.probabilityPercent[0] == 90
                && stock.aiSuperDefense.probabilityPercent[1] == 50
                && stock.aiSuperDefense.probabilityPercent[2] == 10
                && stock.aiSuperDefense.alertFrames == 50
                && stock.aiSuperDefense.distanceCells == 12,
            "stock AI superweapon-defense values"
        );
        stockSuccess &= expect(
            stock.prismSupport.prismType == "ATESLA"
                && closeTo(stock.prismSupport.supportModifier, 1.5)
                && stock.prismSupport.supportMax == 8
                && stock.prismSupport.supportDelayFrames == 45
                && stock.prismSupport.supportDurationFrames == 15
                && stock.prismSupport.supportHeightLeptons == 420,
            "stock Prism Tower support values"
        );
        stockSuccess &= expect(
            std::count_if(
                stockRules.getSmudgeTypes().begin(),
                stockRules.getSmudgeTypes().end(),
                [](const SmudgeType& type) { return type.crater; }
            ) == 12
                && std::count_if(
                    stockRules.getSmudgeTypes().begin(),
                    stockRules.getSmudgeTypes().end(),
                    [](const SmudgeType& type) { return type.burn; }
                ) == 12,
            "stock crater and burn SmudgeType pools"
        );
        stockSuccess &= expect(
            stockSpecialBuildings.selfHealInfantryFrames == 50
                && stockSpecialBuildings.selfHealInfantryAmount == 20
                && stockSpecialBuildings.selfHealUnitFrames == 75
                && stockSpecialBuildings.selfHealUnitAmount == 5
                && stockSpecialBuildings.secretInfantry
                    == std::vector<std::string>{
                        "SNIPE", "TERROR", "DESO", "YURI"
                    }
                && stockSpecialBuildings.secretUnits
                    == std::vector<std::string>{"TNKD", "TTNK", "DTRUCK"}
                && stockSpecialBuildings.secretBuildings
                    == std::vector<std::string>{"GTGCAN"},
            "stock self-heal cadence and Secret Lab candidate pools"
        );
        stockSuccess &= expect(
            stockElevation.increment == 4
                && closeTo(stockElevation.incrementBonus, 2.0)
                && closeTo(stockElevation.bonusCap, 2.0),
            "stock ElevationModel values"
        );
        const WeaponType* stockPsiWave =
            stockRules.findWeaponType("PsiWave");
        const WeaponLifecycleProfile* stockPsiWaveLifecycle =
            stockRules.findWeaponLifecycleProfile("PsiWave");
        const ProjectileType* stockTerrainProjectile =
            stockRules.findProjectileType("InvisibleLow");
        const ProjectileType* stockV3AirburstProjectile =
            stockRules.findProjectileType("V3AirburstP");
        const SpawnedMissileProfile* stockV3MissileProfile =
            stockRules.findSpawnedMissileProfile("V3ROCKET");
        const WeaponType* stockV3ClusterWeapon =
            stockRules.findWeaponType("V3Cluster");
        const WarheadType* stockSmallArms =
            stockRules.findWarheadType("SA");
        const WarheadType* stockMirageWarhead =
            stockRules.findWarheadType("MirageWH");
        const UnitType* stockRocketeer =
            stockRules.findUnitType("infantry", "JUMPJET");
        const UnitType* stockTanya =
            stockRules.findUnitType("infantry", "TANY");
        const UnitType* stockApocalypse =
            stockRules.findUnitType("vehicle", "APOC");
        const UnitType* stockKirov =
            stockRules.findUnitType("vehicle", "ZEP");
        const UnitType* stockDemolitionTruck =
            stockRules.findUnitType("vehicle", "DTRUCK");
        const SuperWeaponRule* stockNuke =
            stockRules.findSuperWeaponType("NukeSpecial");
        const SuperWeaponRule* stockForceShield =
            stockRules.findSuperWeaponType("ForceShieldSpecial");
        const SuperWeaponRule* stockChronosphere =
            stockRules.findSuperWeaponType("ChronoSphereSpecial");
        const SuperWeaponRule* stockChronoWarp =
            stockRules.findSuperWeaponType("ChronoWarpSpecial");
        const SuperWeaponRule* stockSpyPlane =
            stockRules.findSuperWeaponType("SpyPlaneSpecial");
        stockSuccess &= expect(
            stockPsiWave != nullptr && stockPsiWave->areaFire
                && stockPsiWave->fireOnce
                && stockPsiWave->profile.areaFire
                && stockPsiWave->profile.fireOnce
                && stockPsiWaveLifecycle != nullptr
                && stockPsiWaveLifecycle->fireOnce,
            "stock AreaFire/FireOnce weapon and cached profiles"
        );
        stockSuccess &= expect(
            stockTerrainProjectile != nullptr
                && stockTerrainProjectile->subjectToCliffs
                && stockTerrainProjectile->subjectToElevation
                && stockTerrainProjectile->subjectToWalls
                && stockTerrainProjectile->profile.subjectToCliffs
                && stockTerrainProjectile->profile.subjectToElevation
                && stockTerrainProjectile->profile.subjectToWalls,
            "stock terrain-constrained projectile profile"
        );
        stockSuccess &= expect(
            stockV3AirburstProjectile != nullptr
                && stockV3MissileProfile != nullptr
                && stockV3MissileProfile->terminalWeapon == "V3Airburst"
                && stockV3AirburstProjectile->dropping
                && stockV3AirburstProjectile->airburst
                && stockV3AirburstProjectile->cluster == 9
                && stockV3AirburstProjectile->airburstWeapon == "V3Cluster"
                && stockV3ClusterWeapon != nullptr,
            "stock Dropping/Airburst child-weapon chain"
        );
        stockSuccess &= expect(
            stockSmallArms != nullptr
                && stockSmallArms->proneDamage == "70%"
                && closeTo(stockSmallArms->proneDamageMultiplier, 0.7)
                && stockSmallArms->profile.proneDamage == "70%"
                && closeTo(
                    stockSmallArms->profile.proneDamageMultiplier,
                    0.7
                )
                && stockRocketeer != nullptr && stockRocketeer->fearless,
            "stock ProneDamage profile and Fearless infantry flag"
        );
        stockSuccess &= expect(
            stockTanya != nullptr && stockTanya->selfHealing
                && stockApocalypse != nullptr && stockApocalypse->selfHealing
                && stockKirov != nullptr && stockKirov->selfHealing
                && stockDemolitionTruck != nullptr
                && !stockDemolitionTruck->selfHealing,
            "stock SelfHealing unit flags"
        );
        stockSuccess &= expect(
            stockMirageWarhead != nullptr
                && stockMirageWarhead->bright
                && stockMirageWarhead->clDisableGreen
                && stockMirageWarhead->clDisableBlue
                && stockMirageWarhead->profile.clDisableGreen
                && stockMirageWarhead->profile.clDisableBlue,
            "stock combat-light channel fields"
        );
        stockSuccess &= expect(
            stockNuke != nullptr
                && closeTo(stockNuke->rechargeTimeMinutes, 10.0)
                && stockNuke->rechargeVoice == "00-I154"
                && stockNuke->chargingVoice.empty()
                && stockNuke->impatientVoice.empty()
                && stockNuke->suspendVoice.empty()
                && stockNuke->lineMultiplier == 2
                && stockNuke->flashSidebarTabFrames == -1
                && stockNuke->showTimer
                && stockNuke->aiDefendAgainst,
            "stock Nuke superweapon auxiliary fields"
        );
        stockSuccess &= expect(
            stockForceShield != nullptr
                && stockForceShield->flashSidebarTabFrames == 120
                && !stockForceShield->showTimer
                && stockChronosphere != nullptr
                && stockChronosphere->preClick
                && !stockChronosphere->postClick
                && stockChronoWarp != nullptr
                && !stockChronoWarp->preClick
                && stockChronoWarp->postClick
                && stockChronoWarp->preDependent == "ChronoSphere"
                && stockSpyPlane != nullptr
                && stockSpyPlane->flashSidebarTabFrames == 120
                && !stockSpyPlane->showTimer,
            "stock staged and sidebar-flash superweapon fields"
        );
        const UnitType* stockDisk = stockRules.findUnitType("vehicle", "DISK");
        const UnitType* stockMind = stockRules.findUnitType("vehicle", "MIND");
        const UnitType* stockGrizzly = stockRules.findUnitType("vehicle", "MTNK");
        const UnitType* stockMirage = stockRules.findUnitType("vehicle", "MGTK");
        stockSuccess &= expect(stockMind != nullptr && stockGrizzly != nullptr && stockMirage != nullptr
            && stockMind->damageSmokeOffset == std::array<int, 3>{100, 100, 275}
            && stockGrizzly->damageSmokeOffset == std::array<int, 3>{0, 0, 0}
            && stockGrizzly->damageParticleSystems == std::vector<std::string>{"SparkSys", "SmallGreySSys"}
            && stockMirage->damageParticleSystems == std::vector<std::string>{"SparkSys"},
            "stock vehicle damage smoke offsets, original particle systems and Mirage exception");
        stockSuccess &= expect(stock.overloadCount == std::vector<int>{3, 6, 10, 50}
            && stock.overloadDamage == std::vector<int>{0, 50, 100, 500}
            && stock.overloadFrames == std::vector<int>{30, 60, 60, 60}
            && stock.masterMindOverloadDeathSound == "MasterMindOverloadVoice",
            "stock Mastermind overload thresholds, damage, cadence and sound");
        stockSuccess &= expect(stockDisk != nullptr && stockDisk->turretSpins,
            "stock floating disk spinning turret");
        for (const char* id : {"DiskLaser", "DiskLaserE"}) {
            const WeaponType* diskLaser = stockRules.findWeaponType(id);
            stockSuccess &= expect(diskLaser != nullptr && diskLaser->diskLaser
                && !diskLaser->laser && diskLaser->laserInnerColor == "216,0,184"
                && diskLaser->laserOuterColor == "80,0,88"
                && diskLaser->laserOuterSpread == "0,0,0" && diskLaser->laserDuration == 15
                && diskLaser->profile.diskLaser
                && diskLaser->profile.laserInnerColor == diskLaser->laserInnerColor
                && diskLaser->profile.laserOuterColor == diskLaser->laserOuterColor,
                std::string("stock dedicated disk laser and cached colors: ") + id);
        }
        stockSuccess &= expect(stock.diskLaserChargeUp == "FloatingDiscChargeUp"
            && stockRules.findWeaponType("DiskDrain") != nullptr
            && !stockRules.findWeaponType("DiskDrain")->diskLaser,
            "stock disk charge audio and separate drain weapon");
        const BuildingType* stockGap = stockRules.findBuildingType("GAGAP");
        const BuildingType* stockReactor = stockRules.findBuildingType("YAPOWR");
        stockSuccess &= expect(
            stockReactor != nullptr && stockReactor->infantryAbsorb
                && stockReactor->power == 150 && stockReactor->extraPower == 100
                && stockReactor->passengers == 5 && stockReactor->sizeLimit == 15
                && stockReactor->transportProfile.passengers == 5
                && stockReactor->transportProfile.canTransport
                && stockReactor->buildingCapability.extraPower == 100
                && stockReactor->buildingCapability.passengers == 5
                && stockReactor->buildingCapability.sizeLimit == 15,
            "stock Bio Reactor power, infantry capacity and capability propagation"
        );
        const BuildingType* stockCloning =
            stockRules.findBuildingType("NACLON");
        const BuildingType* stockOil = stockRules.findBuildingType("CAOILD");
        const BuildingType* stockMachineShop =
            stockRules.findBuildingType("CAMACH");
        const BuildingType* stockHospital =
            stockRules.findBuildingType("CATHOSP");
        const BuildingType* stockSecretLab =
            stockRules.findBuildingType("CASLAB");
        const BuildingType* stockRefinery =
            stockRules.findBuildingType("GAREFN");
        const BuildingType* stockRepairDepot =
            stockRules.findBuildingType("GADEPT");
        const BuildingType* stockWarFactory =
            stockRules.findBuildingType("GAWEAP");
        stockSuccess &= expect(
            stockGap != nullptr && stockGap->gapGenerator
                && stockGap->gapRadiusInCells == 10
                && stockGap->superGapRadiusInCells == 10
                && stockCloning != nullptr && stockCloning->cloning
                && stockOil != nullptr
                && stockOil->produceCashStartup == 1000
                && stockOil->produceCashAmount == 20
                && stockOil->produceCashDelay == 100
                && stockMachineShop != nullptr
                && stockMachineShop->unitsGainSelfHeal == 1
                && stockHospital != nullptr
                && stockHospital->infantryGainSelfHeal == 1
                && stockSecretLab != nullptr && stockSecretLab->secretLab,
            "stock Gap Generator, Cloning Vats, Oil Derrick, self-heal, "
            "and Secret Lab building rules"
        );
        stockSuccess &= expect(
            stockRefinery != nullptr
                && stockRefinery->numberImpassableRows == 3
                && stockRepairDepot != nullptr
                && stockRepairDepot->numberImpassableRows == 1
                && stockWarFactory != nullptr
                && stockWarFactory->numberImpassableRows == 1,
            "stock refinery, repair-depot, and war-factory drive-row rules"
        );
        return stockSuccess ? 0 : 1;
    }
    if (argc != 2) {
        std::cerr << "usage: concord_rules_special_features_smoke <rules>\n"
                  << "   or: concord_rules_special_features_smoke --stock "
                     "<rulesmd.ini>\n";
        return 2;
    }

    IniFile ini;
    bool success = expect(ini.loadFromFile(argv[1]), "fixture did not load");
    if (!success) {
        return 1;
    }

    RulesDatabase rules;
    success &= expect(rules.loadFromIni(ini), "RulesDatabase load failed");

    const SpecialMechanismRules& global = rules.getSpecialMechanismRules();
    const SpecialBuildingRules& specialBuildings =
        rules.getSpecialBuildingRules();
    const ElevationModelRules& elevation = rules.getElevationModelRules();
    const SuperWeaponRule* primarySuperWeapon =
        rules.findSuperWeaponType("TESTPRIMARYSW");
    const SuperWeaponRule* postSuperWeapon =
        rules.findSuperWeaponType("TESTPOSTSW");
    success &= expect(
        closeTo(global.conditionRed, 0.30)
            && closeTo(global.conditionYellow, 0.55)
            && closeTo(global.ambientChangeRate, 0.02)
            && closeTo(global.ambientChangeStep, 0.125)
            && closeTo(global.occupyDamageMultiplier, 1.35)
            && closeTo(global.occupyRofMultiplier, 1.4)
            && global.occupyWeaponRange == 6
            && global.cloakingStages == 6,
        "ConditionRed/ConditionYellow and urban-combat global rules"
    );
    success &= expect(
        global.aiSuperDefense.probabilityPercent[0] == 81
            && global.aiSuperDefense.probabilityPercent[1] == 52
            && global.aiSuperDefense.probabilityPercent[2] == 23
            && global.aiSuperDefense.alertFrames == 73
            && global.aiSuperDefense.distanceCells == 14,
        "General AI superweapon-defense values"
    );
    success &= expect(
        global.prismSupport.prismType == "TESTPRISM"
            && closeTo(global.prismSupport.supportModifier, 1.25)
            && global.prismSupport.supportMax == 3
            && global.prismSupport.supportDelayFrames == 7
            && global.prismSupport.supportDurationFrames == 4
            && global.prismSupport.supportHeightLeptons == 384,
        "General Prism Tower support values"
    );
    success &= expect(
        primarySuperWeapon != nullptr
            && closeTo(primarySuperWeapon->rechargeTimeMinutes, 2.5)
            && primarySuperWeapon->rechargeVoice == "TestRechargeVoice"
            && primarySuperWeapon->chargingVoice == "TestChargingVoice"
            && primarySuperWeapon->impatientVoice == "TestImpatientVoice"
            && primarySuperWeapon->suspendVoice == "TestSuspendVoice"
            && primarySuperWeapon->lineMultiplier == 0
            && primarySuperWeapon->flashSidebarTabFrames == 45
            && primarySuperWeapon->preClick
            && !primarySuperWeapon->postClick
            && primarySuperWeapon->preDependent.empty()
            && primarySuperWeapon->showTimer
            && primarySuperWeapon->aiDefendAgainst,
        "primary superweapon auxiliary fields"
    );
    success &= expect(
        postSuperWeapon != nullptr
            && closeTo(postSuperWeapon->rechargeTimeMinutes, 0.75)
            && postSuperWeapon->rechargeVoice == "TestPostRechargeVoice"
            && postSuperWeapon->chargingVoice == "TestPostChargingVoice"
            && postSuperWeapon->impatientVoice == "TestPostImpatientVoice"
            && postSuperWeapon->suspendVoice == "TestPostSuspendVoice"
            && postSuperWeapon->lineMultiplier == 3
            && postSuperWeapon->flashSidebarTabFrames == 0
            && !postSuperWeapon->preClick
            && postSuperWeapon->postClick
            && postSuperWeapon->preDependent == "TESTPRIMARYSW"
            && !postSuperWeapon->showTimer
            && !postSuperWeapon->aiDefendAgainst,
        "post-click superweapon auxiliary fields"
    );
    success &= expect(
        rules.getDamageFireTypes().size() == 2
            && rules.getDamageFireTypes()[0] == "MODFIRE_A"
            && rules.getDamageFireTypes()[1] == "MODFIRE_B",
        "General DamageFireTypes order and case-insensitive deduplication"
    );
    success &= expect(
        elevation.increment == 5
            && closeTo(elevation.incrementBonus, 2.5)
            && closeTo(elevation.bonusCap, 3.5),
        "ElevationModel custom values"
    );
    success &= expect(
        specialBuildings.selfHealInfantryFrames == 49
            && specialBuildings.selfHealInfantryAmount == 17
            && specialBuildings.selfHealUnitFrames == 73
            && specialBuildings.selfHealUnitAmount == 6
            && specialBuildings.secretInfantry
                == std::vector<std::string>{"SNIPE", "DESO"}
            && specialBuildings.secretUnits
                == std::vector<std::string>{"TTNK", "MGTK"}
            && specialBuildings.secretBuildings
                == std::vector<std::string>{"GTGCAN", "NABNKR"},
        "General self-heal cadence and deduplicated Secret Lab pools"
    );

    IniFile stockDefaultIni;
    success &= expect(
        stockDefaultIni.loadFromString(
            "[InfantryTypes]\n[VehicleTypes]\n[AircraftTypes]\n"
            "[BuildingTypes]\n[OverlayTypes]\n[Tiberiums]\n"
            "[TerrainTypes]\n[Warheads]\n"
            "[SuperWeaponTypes]\n0=DEFAULTSUPER\n"
            "[DEFAULTSUPER]\nType=SpyPlane\n"
        ),
        "stock-default rules text did not parse"
    );
    RulesDatabase stockDefaultRules;
    success &= expect(
        stockDefaultRules.loadFromIni(stockDefaultIni),
        "stock-default RulesDatabase load failed"
    );
    const SpecialMechanismRules& stockDefaults =
        stockDefaultRules.getSpecialMechanismRules();
    const SpecialBuildingRules& stockBuildingDefaults =
        stockDefaultRules.getSpecialBuildingRules();
    const ElevationModelRules& stockElevationDefaults =
        stockDefaultRules.getElevationModelRules();
    const SuperWeaponRule* stockDefaultSuperWeapon =
        stockDefaultRules.findSuperWeaponType("DEFAULTSUPER");
    success &= expect(
        closeTo(stockDefaults.conditionRed, 0.25)
            && closeTo(stockDefaults.conditionYellow, 0.5)
            && closeTo(stockDefaults.ambientChangeRate, 0.0)
            && closeTo(stockDefaults.ambientChangeStep, 0.0)
            && closeTo(stockDefaults.occupyDamageMultiplier, 1.2)
            && closeTo(stockDefaults.occupyRofMultiplier, 1.2)
            && stockDefaults.occupyWeaponRange == 5
            && stockDefaults.cloakingStages == 9
            && stockDefaults.prismSupport.prismType.empty()
            && closeTo(stockDefaults.prismSupport.supportModifier, 1.0)
            && stockDefaults.prismSupport.supportMax == 8
            && stockDefaults.prismSupport.supportDelayFrames == 100
            && stockDefaults.prismSupport.supportDurationFrames == 15
            && stockDefaults.prismSupport.supportHeightLeptons == 300,
        "urban-combat stock defaults"
    );
    success &= expect(
        stockDefaults.aiSuperDefense.probabilityPercent[0] == 90
            && stockDefaults.aiSuperDefense.probabilityPercent[1] == 50
            && stockDefaults.aiSuperDefense.probabilityPercent[2] == 10
            && stockDefaults.aiSuperDefense.alertFrames == 50
            && stockDefaults.aiSuperDefense.distanceCells == 12,
        "AI superweapon-defense stock defaults"
    );
    success &= expect(
        stockDefaultSuperWeapon != nullptr
            && stockDefaultSuperWeapon->lineMultiplier == 0
            && stockDefaultSuperWeapon->flashSidebarTabFrames == -1,
        "superweapon line and sidebar-flash stock defaults"
    );
    success &= expect(
        stockDefaultRules.getDamageFireTypes()
            == std::vector<std::string>{"FIRE01", "FIRE02", "FIRE03"},
        "DamageFireTypes stock defaults"
    );
    success &= expect(
        stockElevationDefaults.increment == 4
            && closeTo(stockElevationDefaults.incrementBonus, 2.0)
            && closeTo(stockElevationDefaults.bonusCap, 2.0),
        "ElevationModel stock defaults"
    );
    success &= expect(
        stockBuildingDefaults.selfHealInfantryFrames == 50
            && stockBuildingDefaults.selfHealInfantryAmount == 20
            && stockBuildingDefaults.selfHealUnitFrames == 75
            && stockBuildingDefaults.selfHealUnitAmount == 5
            && stockBuildingDefaults.secretInfantry.empty()
            && stockBuildingDefaults.secretUnits.empty()
            && stockBuildingDefaults.secretBuildings.empty(),
        "special-building stock defaults"
    );
    IniFile invalidAmbientIni;
    success &= expect(
        invalidAmbientIni.loadFromString(
            "[AudioVisual]\n"
            "ConditionRed=125%\n"
            "AmbientChangeRate=not-a-number\n"
            "AmbientChangeStep=-.5\n"
            "[General]\n"
            "ConditionYellow=nan\n"
            "CloakingStages=not-a-number\n"
            "SelfHealInfantryFrames=-1\n"
            "SelfHealInfantryAmount=-2\n"
            "SelfHealUnitFrames=-3\n"
            "SelfHealUnitAmount=-4\n"
            "[InfantryTypes]\n[VehicleTypes]\n[AircraftTypes]\n"
            "[BuildingTypes]\n[OverlayTypes]\n[Tiberiums]\n"
            "[TerrainTypes]\n[Warheads]\n"
        ),
        "invalid ambient fixture did not parse"
    );
    RulesDatabase invalidAmbientRules;
    success &= expect(
        invalidAmbientRules.loadFromIni(invalidAmbientIni)
            && closeTo(
                invalidAmbientRules.getSpecialMechanismRules()
                    .ambientChangeRate,
                0.0
            )
            && closeTo(
                invalidAmbientRules.getSpecialMechanismRules()
                    .ambientChangeStep,
                0.0
            )
            && invalidAmbientRules.getSpecialMechanismRules()
                .cloakingStages == 9
            && invalidAmbientRules.getSpecialBuildingRules()
                .selfHealInfantryFrames == 0
            && invalidAmbientRules.getSpecialBuildingRules()
                .selfHealInfantryAmount == 0
            && invalidAmbientRules.getSpecialBuildingRules()
                .selfHealUnitFrames == 0
            && invalidAmbientRules.getSpecialBuildingRules()
                .selfHealUnitAmount == 0
            && closeTo(
                invalidAmbientRules.getSpecialMechanismRules().conditionRed,
                0.25
            )
            && closeTo(
                invalidAmbientRules.getSpecialMechanismRules()
                    .conditionYellow,
                0.5
            )
            && hasWarning(invalidAmbientRules, "ConditionRed")
            && hasWarning(invalidAmbientRules, "ConditionYellow")
            && hasWarning(invalidAmbientRules, "AmbientChangeRate")
            && hasWarning(invalidAmbientRules, "AmbientChangeStep")
            && hasWarning(
                invalidAmbientRules,
                "Invalid integer: [General] CloakingStages=not-a-number"
            ),
        "invalid global values did not warn and use defaults"
    );
    success &= expect(
        global.alliedDisguise == "E1"
            && global.sovietDisguise == "E2"
            && global.thirdDisguise == "INIT",
        "General disguise defaults"
    );
    success &= expect(
        global.spyPowerBlackoutFrames == 900
            && closeTo(global.spyMoneyStealPercent, 0.35)
            && !global.attackCursorOnDisguise,
        "General spy settings"
    );
    success &= expect(
        closeTo(global.reloadRateMinutes, 0.25)
            && closeTo(global.repairPercent, 0.15)
            && global.repairStep == 9
            && closeTo(global.unitRepairRateMinutes, 0.03)
            && closeTo(global.infantryRepairRateMinutes, 0.004)
            && global.infantryRepairStep == 21
            && closeTo(rules.getVeterancyRules().selfHealRateMinutes, 0.02),
        "General repair rates"
    );
    success &= expect(
        global.repairBayTypes.size() == 2
            && global.repairBayTypes[0] == "DEPOT"
            && global.repairBayTypes[1] == "MISSING_DEPOT",
        "RepairBay list preserves order"
    );
    success &= expect(
        closeTo(global.c4DelayMinutes, 0.04)
            && global.c4Warhead == "Super"
            && global.ivanWarhead == "IvanWH"
            && global.ivanDamage == 475
            && global.ivanTimedDelayFrames == 460
            && global.canDetonateTimeBomb
            && !global.canDetonateDeathBomb
            && global.ivanIconFlickerRateFrames == 9,
        "CombatDamage C4/Ivan settings"
    );
    success &= expect(
        global.defaultDeathWeapon == "DefaultDeathWeapon"
            && closeTo(global.fallingDamageMultiplier, 0.75)
            && !global.currentStrengthDamage,
        "CombatDamage death/Magnetron settings"
    );
    success &= expect(
        rules.getAudioVisualValue("GateUp") == "GateRise"
            && rules.getAudioVisualValue("bombattachsound") == "BombAttach"
            && rules.getAudioVisualValue("MagnaBeamColor") == "255,200,255",
        "AudioVisual values remain in the shared catalog"
    );

    const UnitType* spy = rules.findUnitType("infantry", "SPY");
    success &= expect(
        spy != nullptr && spy->agent && spy->infiltrate
            && spy->canDisguise && spy->permaDisguise
            && !spy->disguiseWhenStill && spy->detectDisguise
            && spy->detectDisguiseRange == 6,
        "Spy Agent/Infiltrate/disguise fields"
    );

    const UnitType* dog = rules.findUnitType("infantry", "DOG");
    const WeaponType* droneJump = rules.findWeaponType("DroneJump");
    const WarheadType* parasite = rules.findWarheadType("Parasite");
    success &= expect(
        dog != nullptr && dog->bombable && dog->bombSight == 4
            && dog->reselectIfLimboed && dog->rejoinTeamIfLimboed,
        "limbo selection fields"
    );
    success &= expect(
        droneJump != nullptr && droneJump->limboLaunch
            && droneJump->profile.limboLaunch,
        "LimboLaunch weapon and cached profile"
    );
    success &= expect(
        parasite != nullptr && parasite->parasite && parasite->culling
            && parasite->paralyzes == 32767
            && parasite->profile.parasite && parasite->profile.culling
            && parasite->profile.paralyzes == 32767,
        "Parasite warhead and cached profile"
    );

    const UnitType* chaos = rules.findUnitType("infantry", "CAOS");
    const WarheadType* psychGas = rules.findWarheadType("PsychGasCreate");
    success &= expect(
        chaos != nullptr && chaos->berserkFriendly
            && psychGas != nullptr && psychGas->psychedelic
            && psychGas->profile.psychedelic,
        "Psychedelic/BerserkFriendly fields"
    );

    const UnitType* fieldTest = rules.findUnitType("infantry", "FIELDTEST");
    const WeaponType* fieldWeapon =
        rules.findWeaponType("FieldConstraintWeapon");
    const WeaponLifecycleProfile* fieldWeaponLifecycle =
        rules.findWeaponLifecycleProfile("FieldConstraintWeapon");
    const ProjectileType* terrainDrop =
        rules.findProjectileType("TerrainDropP");
    const WarheadType* proneWarhead = rules.findWarheadType("ProneWH");
    success &= expect(
        fieldTest != nullptr && fieldTest->fearless
            && fieldTest->veterancy.veteranAbilities
                == std::vector<VeteranAbility>{
                    VeteranAbility::Faster,
                    VeteranAbility::Fearless
                }
            && fieldTest->veterancy.eliteAbilities
                == std::vector<VeteranAbility>{
                    VeteranAbility::Faster,
                    VeteranAbility::Fearless,
                    VeteranAbility::SelfHeal
                }
            && hasAbility(
                fieldTest->veterancy.veteranAbilities,
                VeteranAbility::Fearless
            ),
        "Fearless unit field and cumulative veterancy profile"
    );
    success &= expect(
        fieldWeapon != nullptr && fieldWeapon->areaFire
            && fieldWeapon->fireOnce
            && fieldWeapon->profile.areaFire
            && fieldWeapon->profile.fireOnce
            && fieldWeaponLifecycle != nullptr
            && fieldWeaponLifecycle->fireOnce,
        "AreaFire/FireOnce weapon and cached profiles"
    );
    success &= expect(
        terrainDrop != nullptr && terrainDrop->dropping
            && !terrainDrop->vertical
            && terrainDrop->subjectToCliffs
            && terrainDrop->subjectToElevation
            && terrainDrop->subjectToWalls
            && terrainDrop->profile.dropping
            && !terrainDrop->profile.vertical
            && terrainDrop->profile.subjectToCliffs
            && terrainDrop->profile.subjectToElevation
            && terrainDrop->profile.subjectToWalls,
        "Dropping and terrain-constrained projectile profile"
    );
    success &= expect(
        proneWarhead != nullptr && proneWarhead->proneDamage == "300%"
            && closeTo(proneWarhead->proneDamageMultiplier, 3.0)
            && proneWarhead->profile.proneDamage == "300%"
            && closeTo(proneWarhead->profile.proneDamageMultiplier, 3.0),
        "ProneDamage raw value and numeric warhead profile"
    );

    const UnitType* boris = rules.findUnitType("infantry", "BORIS");
    const WarheadType* airstrike = rules.findWarheadType("AirstrikeFlare");
    success &= expect(
        boris != nullptr && boris->selfHealing && boris->airstrikeTeam == 2
            && boris->eliteAirstrikeTeam == 4
            && boris->airstrikeTeamType == "BPLN"
            && boris->eliteAirstrikeTeamType == "BPLN"
            && boris->airstrikeRechargeTime == 100
            && boris->eliteAirstrikeRechargeTime == 50
            && airstrike != nullptr && airstrike->airstrike
            && airstrike->profile.airstrike,
        "Boris airstrike caller and flare warhead"
    );

    const UnitType* ivan = rules.findUnitType("infantry", "IVAN");
    const UnitType* seal = rules.findUnitType("infantry", "SEAL");
    const WarheadType* ivanBomb = rules.findWarheadType("IvanBomb");
    const WarheadType* bombDisarm = rules.findWarheadType("BombDisarm");
    success &= expect(
        ivan != nullptr && ivan->ivan && !ivan->bombable
            && seal != nullptr && seal->c4
            && ivanBomb != nullptr && ivanBomb->ivanBomb
            && ivanBomb->profile.ivanBomb
            && bombDisarm != nullptr && bombDisarm->bombDisarm,
        "Ivan/C4 marker fields"
    );

    const UnitType* magnetron = rules.findUnitType("vehicle", "MAGNETRON");
    const WeaponType* magneticBeam = rules.findWeaponType("MagneticBeam");
    const WarheadType* locomotorBeam = rules.findWarheadType("LocomotorBeam");
    success &= expect(
        magnetron != nullptr && !magnetron->selfHealing
            && closeTo(magnetron->weight, 0.5)
            && magneticBeam != nullptr && magneticBeam->magBeam
            && magneticBeam->profile.magBeam
            && locomotorBeam != nullptr && locomotorBeam->isLocomotor
            && locomotorBeam->profile.isLocomotor
            && locomotorBeam->locomotor
                == "{92612C46-F71F-11d1-AC9F-006008055BB5}",
        "MagBeam/locomotor/weight fields"
    );

    const UnitType* mirage = rules.findUnitType("vehicle", "MIRAGE");
    const WeaponType* mirageGun = rules.findWeaponType("MirageGun");
    const WarheadType* snapshot = rules.findWarheadType("Snapshot");
    const WarheadType* mirageWarhead = rules.findWarheadType("MirageWH");
    success &= expect(
        mirage != nullptr && mirage->canDisguise && mirage->disguiseWhenStill
            && mirageGun != nullptr && mirageGun->disguiseFireOnly
            && mirageGun->profile.disguiseFireOnly
            && snapshot != nullptr && snapshot->makesDisguise
            && snapshot->profile.makesDisguise,
        "Mirage disguise weapon/warhead fields"
    );
    success &= expect(
        mirageWarhead != nullptr && mirageWarhead->bright
            && closeTo(mirageWarhead->combatLightSize, 0.4)
            && !mirageWarhead->clDisableRed
            && mirageWarhead->clDisableGreen
            && mirageWarhead->clDisableBlue
            && closeTo(mirageWarhead->profile.combatLightSize, 0.4)
            && mirageWarhead->profile.clDisableGreen
            && mirageWarhead->profile.clDisableBlue,
        "CombatLightSize ratio and channel-disable profile"
    );

    const UnitType* deathUnit = rules.findUnitType("vehicle", "DEATHUNIT");
    const UnitType* deathPlane = rules.findUnitType("aircraft", "BPLN");
    const BuildingType* deathBuilding = rules.findBuildingType("DEATHBUILD");
    success &= expect(
        deathUnit != nullptr && deathUnit->explodes
            && deathUnit->deathWeapon == "UnitDeathWeapon"
            && closeTo(deathUnit->deathWeaponDamageModifier, 0.25)
            && !deathUnit->parasiteable && deathUnit->suppressionThreshold == 250,
        "unit DeathWeapon and parasite target fields"
    );
    success &= expect(
        deathPlane != nullptr && deathPlane->selfHealing
            && deathPlane->deathWeapon == "PlaneDeathWeapon"
            && closeTo(deathPlane->deathWeaponDamageModifier, 0.1)
            && deathBuilding != nullptr && deathBuilding->explodes
            && deathBuilding->deathWeapon == "BuildingDeathWeapon"
            && closeTo(deathBuilding->deathWeaponDamageModifier, 0.5),
        "aircraft/building DeathWeapon fields"
    );
    success &= expect(
        rules.findWeaponType("DefaultDeathWeapon") != nullptr
            && rules.findWeaponType("UnitDeathWeapon") != nullptr
            && rules.findWeaponType("PlaneDeathWeapon") != nullptr
            && rules.findWeaponType("BuildingDeathWeapon") != nullptr,
        "DeathWeapon-only references are discovered"
    );

    const BuildingType* spyBuilding = rules.findBuildingType("SPYBUILD");
    const BuildingType* depot = rules.findBuildingType("DEPOT");
    const BuildingType* gate = rules.findBuildingType("GATEHOUSE");
    const BuildingType* specialBuilding =
        rules.findBuildingType("SPECIALBUILD");
    const BuildingType* brokenSpecial =
        rules.findBuildingType("BROKENSPECIAL");
    success &= expect(
        spyBuilding != nullptr && spyBuilding->spyable
            && spyBuilding->selfHealing
            && !spyBuilding->canC4 && !spyBuilding->bombable
            && spyBuilding->detectDisguise
            && spyBuilding->detectDisguiseRange == 15,
        "Spyable/CanC4 building fields"
    );
    success &= expect(
        depot != nullptr && depot->unitRepair && depot->unitReload
            && depot->unitSell && depot->numberOfDocks == 1
            && depot->numberImpassableRows == 1,
        "repair depot fields and implicit dock normalization"
    );
    success &= expect(
        gate != nullptr && gate->gate && closeTo(gate->gateCloseDelay, 0.2)
            && closeTo(gate->deployTime, 0.044)
            && gate->deployFacing == 3
            && gate->deployProfile.deployFacing == 3,
        "gate and building DeployFacing fields"
    );
    success &= expect(
        specialBuilding != nullptr && specialBuilding->hospital
            && specialBuilding->armory && specialBuilding->cloning
            && specialBuilding->infantryGainSelfHeal == 2
            && specialBuilding->unitsGainSelfHeal == 3
            && specialBuilding->gapGenerator
            && specialBuilding->gapRadiusInCells == 12
            && specialBuilding->superGapRadiusInCells == 18
            && specialBuilding->secretLab
            && specialBuilding->secretInfantry
                == std::vector<std::string>{"VIRUS", "SNIPE", "CLEG"}
            && specialBuilding->secretUnits
                == std::vector<std::string>{"TTNK", "MGTK"}
            && specialBuilding->secretBuildings
                == std::vector<std::string>{"GTGCAN", "NABNKR"}
            && specialBuilding->produceCashStartup == 1234
            && specialBuilding->produceCashAmount == 45
            && specialBuilding->produceCashDelay == 67,
        "special-building flags, merged overrides, and cash production"
    );
    success &= expect(
        specialBuilding != nullptr
            && specialBuilding->buildingCapability.infantryGainSelfHeal == 2
            && specialBuilding->buildingCapability.unitsGainSelfHeal == 3
            && specialBuilding->buildingCapability.gapGenerator
            && specialBuilding->buildingCapability.gapRadiusInCells == 12
            && specialBuilding->buildingCapability.superGapRadiusInCells == 18
            && specialBuilding->buildingCapability.secretLab
            && specialBuilding->buildingCapability.secretInfantry
                == specialBuilding->secretInfantry
            && specialBuilding->buildingCapability.secretUnits
                == specialBuilding->secretUnits
            && specialBuilding->buildingCapability.secretBuildings
                == specialBuilding->secretBuildings
            && specialBuilding->buildingCapability.produceCashStartup == 1234
            && specialBuilding->buildingCapability.produceCashAmount == 45
            && specialBuilding->buildingCapability.produceCashDelay == 67,
        "special-building capability cache mirrors parsed rules"
    );
    success &= expect(
        brokenSpecial != nullptr
            && brokenSpecial->infantryGainSelfHeal == 0
            && brokenSpecial->unitsGainSelfHeal == 0
            && brokenSpecial->gapGenerator
            && brokenSpecial->gapRadiusInCells == 0
            && brokenSpecial->superGapRadiusInCells == 512
            && brokenSpecial->produceCashStartup == 0
            && brokenSpecial->produceCashAmount == 0
            && brokenSpecial->produceCashDelay == 0,
        "special-building numeric fields clamp to safe ranges"
    );

    const UnitType* defaultUnit = rules.findUnitType("vehicle", "DEFAULTUNIT");
    const UnitType* defaultInfantry = rules.findUnitType(
        "infantry",
        "DEFAULTINF"
    );
    const UnitType* brokenUnit = rules.findUnitType("infantry", "BROKEN");
    const BuildingType* defaultBuilding = rules.findBuildingType("DEFAULTBUILD");
    const BuildingType* cloakingBuilding = rules.findBuildingType("SPYBUILD");
    success &= expect(
        defaultUnit != nullptr && defaultUnit->bombable
            && !defaultUnit->selfHealing
            && defaultUnit->parasiteable && closeTo(defaultUnit->weight, 1.0)
            && closeTo(defaultUnit->deathWeaponDamageModifier, 1.0)
            && defaultUnit->cloakingSpeed == 1
            && magnetron != nullptr && magnetron->cloakingSpeed == 4
            && defaultInfantry != nullptr
            && !defaultInfantry->selfHealing
            && defaultInfantry->cloakingSpeed == 7
            && brokenUnit != nullptr && !brokenUnit->selfHealing,
        "unit stock defaults"
    );
    success &= expect(
        defaultBuilding != nullptr && !defaultBuilding->spyable
            && !defaultBuilding->selfHealing
            && defaultBuilding->canC4 && defaultBuilding->bombable
            && !defaultBuilding->gate
            && defaultBuilding->infantryGainSelfHeal == 0
            && defaultBuilding->unitsGainSelfHeal == 0
            && !defaultBuilding->gapGenerator
            && defaultBuilding->gapRadiusInCells == 0
            && defaultBuilding->superGapRadiusInCells == 0
            && !defaultBuilding->secretLab
            && defaultBuilding->secretInfantry.empty()
            && defaultBuilding->secretUnits.empty()
            && defaultBuilding->secretBuildings.empty()
            && defaultBuilding->produceCashStartup == 0
            && defaultBuilding->produceCashAmount == 0
            && defaultBuilding->produceCashDelay == 0
            && closeTo(defaultBuilding->deathWeaponDamageModifier, 1.0)
            && defaultBuilding->cloakingSpeed == 10
            && cloakingBuilding != nullptr
            && cloakingBuilding->cloakingSpeed == 5,
        "building stock defaults"
    );
    success &= expect(
        hasWarning(rules, "Invalid boolean: [BROKEN] Agent=perhaps")
            && hasWarning(rules, "Invalid boolean: [BROKEN] SelfHealing=perhaps")
            && hasWarning(rules, "Invalid integer: [BROKEN] AirstrikeTeam=not-a-number")
            && hasWarning(rules, "Invalid integer: [BROKEN] CloakingSpeed=not-a-number")
            && hasWarning(rules, "RepairBay references missing building type [MISSING_DEPOT]")
            && hasWarning(rules, "EliteAirstrikeTeamType references missing aircraft type [NO_SUCH_PLANE]"),
        "malformed and unknown values emit warnings"
    );

    return success ? 0 : 1;
}
