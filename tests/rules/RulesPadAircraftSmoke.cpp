#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "PadAircraft smoke failure: " << message << '\n';
    }
    return condition;
}

std::size_t warningCount(
    const RulesDatabase& rules,
    const std::string& fragment
) {
    std::size_t count = 0;
    for (const std::string& warning : rules.getWarnings()) {
        if (warning.find(fragment) != std::string::npos) {
            ++count;
        }
    }
    return count;
}

bool runFixture(const std::string& path) {
    IniFile ini;
    bool success = expect(ini.loadFromFile(path), "fixture did not load");
    if (!success) {
        return false;
    }

    RulesDatabase rules;
    success &= expect(rules.loadFromIni(ini), "fixture rules did not load");
    const std::vector<std::string> expected{
        "OrcA", "beAg", "UnknownPlane", "VtolTank"
    };
    success &= expect(
        rules.getPadAircraft() == expected,
        "CSV trim, canonical de-duplication, or first-spelling order changed"
    );
    success &= expect(
        rules.isPadAircraft("orca")
            && rules.isPadAircraft(" BEAG ")
            && rules.isPadAircraft("unkNOWnplane")
            && rules.isPadAircraft("VTOLTANK")
            && !rules.isPadAircraft("")
            && !rules.isPadAircraft("not-listed"),
        "case-insensitive membership helper"
    );
    success &= expect(
        warningCount(rules, "[General] PadAircraft") == 1U
            && warningCount(rules, "UnknownPlane") == 1U,
        "unknown IDs should be retained and warned exactly once"
    );

    IniFile excessiveDocks;
    success &= expect(
        excessiveDocks.loadFromString(
            "[General]\n"
            "[InfantryTypes]\n[VehicleTypes]\n[AircraftTypes]\n"
            "[BuildingTypes]\n0=HUGEPAD\n"
            "[HUGEPAD]\nFactory=AircraftType\nNumberOfDocks=5000\n"
            "[OverlayTypes]\n[Tiberiums]\n[TerrainTypes]\n[Warheads]\n"
        ),
        "excessive dock rules text did not parse"
    );
    RulesDatabase dockRules;
    success &= expect(
        dockRules.loadFromIni(excessiveDocks),
        "excessive dock rules did not load"
    );
    const BuildingType* hugePad = dockRules.findBuildingType("HUGEPAD");
    success &= expect(
        hugePad != nullptr && hugePad->numberOfDocks == 4096
            && warningCount(dockRules, "NumberOfDocks exceeds") == 1U,
        "NumberOfDocks was not clamped to the production authority limit"
    );

    IniFile reload;
    success &= expect(
        reload.loadFromString(
            "[General]\n"
            "[InfantryTypes]\n[VehicleTypes]\n[AircraftTypes]\n"
            "[BuildingTypes]\n[OverlayTypes]\n[Tiberiums]\n"
            "[TerrainTypes]\n[Warheads]\n"
        ),
        "reload rules text did not parse"
    );
    success &= expect(rules.loadFromIni(reload), "reload failed");
    success &= expect(
        rules.getPadAircraft().empty()
            && !rules.isPadAircraft("ORCA")
            && warningCount(rules, "[General] PadAircraft") == 0U,
        "reload did not clear PadAircraft state and diagnostics"
    );
    return success;
}

bool runStock(const std::string& path) {
    IniFile ini;
    bool success = expect(ini.loadFromFile(path), "stock rulesmd did not load");
    if (!success) {
        return false;
    }

    RulesDatabase rules;
    success &= expect(rules.loadFromIni(ini), "stock RulesDatabase load failed");
    const std::vector<std::string> expected{"ORCA", "BEAG"};
    success &= expect(
        rules.getPadAircraft() == expected,
        "stock PadAircraft should be ORCA,BEAG in source order"
    );
    success &= expect(
        rules.isPadAircraft("orca") && rules.isPadAircraft("BeAg"),
        "stock PadAircraft membership should be case-insensitive"
    );
    success &= expect(
        warningCount(rules, "[General] PadAircraft") == 0U,
        "stock PadAircraft IDs should resolve through AircraftTypes/VehicleTypes"
    );
    return success;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: concord_rules_pad_aircraft_smoke "
                     "--fixture|--stock <rules>\n";
        return 2;
    }

    const std::string mode = argv[1];
    if (mode == "--fixture") {
        return runFixture(argv[2]) ? 0 : 1;
    }
    if (mode == "--stock") {
        return runStock(argv[2]) ? 0 : 1;
    }
    std::cerr << "unknown mode: " << mode << '\n';
    return 2;
}
