#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
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

bool hasDefaultProductionRules(const RulesDatabase& rules) {
    const ProductionRules& production = rules.getProductionRules();
    return closeTo(production.buildSpeed, 0.7)
        && closeTo(production.multipleFactory, 0.8)
        && closeTo(production.wallBuildSpeedCoefficient, 3.0)
        && closeTo(production.lowPowerPenaltyModifier, 1.0)
        && closeTo(production.minLowPowerProductionSpeed, 0.5)
        && closeTo(production.maxLowPowerProductionSpeed, 0.8);
}

bool runFixtureSmoke() {
    bool success = true;
    IniFile validIni;
    success &= expect(
        validIni.loadFromString(
            "[General]\n"
            "BuildSpeed=.25\n"
            "MultipleFactory=.6\n"
            "WallBuildSpeedCoefficient=4.5\n"
            "LowPowerPenaltyModifier=2\n"
            "MinLowPowerProductionSpeed=.4\n"
            "MaxLowPowerProductionSpeed=.75\n"
            "[InfantryTypes]\n"
            "[VehicleTypes]\n"
            "0=CUSTOMUNIT\n"
            "1=DEFAULTUNIT\n"
            "[AircraftTypes]\n"
            "[BuildingTypes]\n"
            "0=CUSTOMBUILD\n"
            "1=DEFAULTBUILD\n"
            "[OverlayTypes]\n[Tiberiums]\n[TerrainTypes]\n[Warheads]\n"
            "[CUSTOMUNIT]\n"
            "BuildTimeMultiplier=1.25\n"
            "[DEFAULTUNIT]\n"
            "[CUSTOMBUILD]\n"
            "BuildTimeMultiplier=1.75\n"
            "[DEFAULTBUILD]\n"
        ),
        "valid production fixture did not parse"
    );

    RulesDatabase rules;
    success &= expect(rules.loadFromIni(validIni), "valid rules did not load");
    const ProductionRules& production = rules.getProductionRules();
    success &= expect(
        closeTo(production.buildSpeed, 0.25)
            && closeTo(production.multipleFactory, 0.6)
            && closeTo(production.wallBuildSpeedCoefficient, 4.5)
            && closeTo(production.lowPowerPenaltyModifier, 2.0)
            && closeTo(production.minLowPowerProductionSpeed, 0.4)
            && closeTo(production.maxLowPowerProductionSpeed, 0.75),
        "valid General production values were not preserved"
    );
    const UnitType* customUnit = rules.findUnitType("vehicle", "CUSTOMUNIT");
    const UnitType* defaultUnit = rules.findUnitType("vehicle", "DEFAULTUNIT");
    const BuildingType* customBuilding = rules.findBuildingType("CUSTOMBUILD");
    const BuildingType* defaultBuilding = rules.findBuildingType("DEFAULTBUILD");
    success &= expect(
        customUnit != nullptr
            && closeTo(customUnit->buildTimeMultiplier, 1.25)
            && defaultUnit != nullptr
            && closeTo(defaultUnit->buildTimeMultiplier, 1.0),
        "UnitType BuildTimeMultiplier explicit/default values"
    );
    success &= expect(
        customBuilding != nullptr
            && closeTo(customBuilding->buildTimeMultiplier, 1.75)
            && defaultBuilding != nullptr
            && closeTo(defaultBuilding->buildTimeMultiplier, 1.0),
        "BuildingType BuildTimeMultiplier explicit/default values"
    );

    IniFile invalidIni;
    success &= expect(
        invalidIni.loadFromString(
            "[General]\n"
            "BuildSpeed=nan\n"
            "MultipleFactory=0\n"
            "WallBuildSpeedCoefficient=101\n"
            "LowPowerPenaltyModifier=inf\n"
            "MinLowPowerProductionSpeed=.9\n"
            "MaxLowPowerProductionSpeed=.2\n"
            "[InfantryTypes]\n"
            "[VehicleTypes]\n"
            "0=BROKENUNIT\n"
            "[AircraftTypes]\n"
            "[BuildingTypes]\n"
            "0=BROKENBUILD\n"
            "[OverlayTypes]\n[Tiberiums]\n[TerrainTypes]\n[Warheads]\n"
            "[BROKENUNIT]\n"
            "BuildTimeMultiplier=-1\n"
            "[BROKENBUILD]\n"
            "BuildTimeMultiplier=100.1\n"
        ),
        "invalid production fixture did not parse"
    );
    RulesDatabase invalidRules;
    success &= expect(
        invalidRules.loadFromIni(invalidIni),
        "invalid production rules did not load"
    );
    success &= expect(
        hasDefaultProductionRules(invalidRules),
        "invalid General values did not fall back to safe defaults"
    );
    const UnitType* brokenUnit = invalidRules.findUnitType(
        "vehicle",
        "BROKENUNIT"
    );
    const BuildingType* brokenBuilding = invalidRules.findBuildingType(
        "BROKENBUILD"
    );
    success &= expect(
        brokenUnit != nullptr
            && closeTo(brokenUnit->buildTimeMultiplier, 1.0)
            && brokenBuilding != nullptr
            && closeTo(brokenBuilding->buildTimeMultiplier, 1.0),
        "invalid object BuildTimeMultiplier values did not use defaults"
    );
    success &= expect(
        hasWarning(invalidRules, "BuildSpeed=nan")
            && hasWarning(invalidRules, "MultipleFactory=0")
            && hasWarning(invalidRules, "WallBuildSpeedCoefficient=101")
            && hasWarning(invalidRules, "LowPowerPenaltyModifier=inf")
            && hasWarning(invalidRules, "MinLowPowerProductionSpeed must not exceed")
            && hasWarning(invalidRules, "[BROKENUNIT] BuildTimeMultiplier=-1")
            && hasWarning(invalidRules, "[BROKENBUILD] BuildTimeMultiplier=100.1"),
        "invalid production diagnostics were incomplete"
    );

    IniFile reloadIni;
    success &= expect(
        reloadIni.loadFromString(
            "[InfantryTypes]\n[VehicleTypes]\n[AircraftTypes]\n"
            "[BuildingTypes]\n[OverlayTypes]\n[Tiberiums]\n"
            "[TerrainTypes]\n[Warheads]\n"
        ),
        "reload fixture did not parse"
    );
    success &= expect(
        invalidRules.loadFromIni(reloadIni)
            && hasDefaultProductionRules(invalidRules)
            && invalidRules.findUnitType("vehicle", "BROKENUNIT") == nullptr
            && invalidRules.findBuildingType("BROKENBUILD") == nullptr
            && invalidRules.getWarnings().empty(),
        "reload did not clear production values, objects, and diagnostics"
    );
    return success;
}

bool runStockSmoke(const std::string& path) {
    IniFile ini;
    bool success = expect(ini.loadFromFile(path), "stock rules did not load");
    if (!success) {
        return false;
    }

    RulesDatabase rules;
    success &= expect(
        rules.loadFromIni(ini),
        "stock RulesDatabase load failed"
    );
    const ProductionRules& production = rules.getProductionRules();
    success &= expect(
        closeTo(production.buildSpeed, 0.7)
            && closeTo(production.multipleFactory, 0.8)
            && closeTo(production.wallBuildSpeedCoefficient, 3.0)
            && closeTo(production.lowPowerPenaltyModifier, 1.0)
            && closeTo(production.minLowPowerProductionSpeed, 0.5)
            && closeTo(production.maxLowPowerProductionSpeed, 0.8),
        "stock General production values"
    );
    const UnitType* mediumTank = rules.findUnitType("vehicle", "MTNK");
    const UnitType* robotTank = rules.findUnitType("vehicle", "ROBO");
    success &= expect(
        mediumTank != nullptr
            && closeTo(mediumTank->buildTimeMultiplier, 1.5)
            && robotTank != nullptr
            && closeTo(robotTank->buildTimeMultiplier, 1.3),
        "stock MTNK/ROBO BuildTimeMultiplier values"
    );
    return success;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        return runFixtureSmoke() ? 0 : 1;
    }
    if (argc == 3 && std::string(argv[1]) == "--stock") {
        return runStockSmoke(argv[2]) ? 0 : 1;
    }

    std::cerr << "usage: concord_rules_production_smoke "
                 "[--stock <rulesmd.ini>]\n";
    return 2;
}
