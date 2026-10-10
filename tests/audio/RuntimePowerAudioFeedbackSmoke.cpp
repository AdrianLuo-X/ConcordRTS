#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"
#include "audio/events/RuntimePowerAudioFeedback.h"

#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "power audio feedback smoke failure: "
                  << message << '\n';
    }
    return condition;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr
            << "usage: concord_power_audio_feedback_smoke <rulesmd.ini>\n";
        return 2;
    }

    IniFile ini;
    RulesDatabase rules;
    bool ok = expect(ini.loadFromFile(argv[1]), "stock rules did not load");
    ok &= expect(rules.loadFromIni(ini), "stock rules did not parse");
    if (!ok) {
        return 1;
    }

    const UnitType* robot = rules.findUnitType("vehicle", "ROBO");
    ok &= expect(robot != nullptr, "ROBO rules were not found");
    if (robot != nullptr) {
        ok &= expect(robot->poweredUnit, "ROBO is not marked PoweredUnit");
        ok &= expect(
            runtimePoweredUnitTransitionSound(
                *robot,
                true,
                true,
                true,
                false
            ) == "RobotTankOffline",
            "ROBO online-to-offline did not select DeactivateSound"
        );
        ok &= expect(
            runtimePoweredUnitTransitionSound(
                *robot,
                true,
                true,
                false,
                true
            ) == "RobotTankOnline",
            "ROBO offline-to-online did not select ActivateSound"
        );
        ok &= expect(
            runtimePoweredUnitTransitionSound(
                *robot,
                false,
                true,
                true,
                false
            ).empty(),
            "initial ROBO power observation emitted feedback"
        );
        ok &= expect(
            runtimePoweredUnitTransitionSound(
                *robot,
                true,
                false,
                true,
                false
            ).empty(),
            "remote ROBO transition emitted local feedback"
        );
        ok &= expect(
            runtimePoweredUnitTransitionSound(
                *robot,
                true,
                true,
                false,
                false
            ).empty(),
            "unchanged ROBO power state emitted feedback"
        );
    }

    const BuildingType* sam = rules.findBuildingType("NASAM");
    ok &= expect(sam != nullptr, "NASAM rules were not found");
    if (sam != nullptr) {
        ok &= expect(sam->powered, "NASAM is not marked Powered");
        ok &= expect(
            runtimeBuildingPowerTransitionSound(
                *sam,
                true,
                true,
                true,
                false
            ) == "PowerOff",
            "NASAM online-to-offline did not select NotWorkingSound"
        );
        ok &= expect(
            runtimeBuildingPowerTransitionSound(
                *sam,
                true,
                true,
                false,
                true
            ).empty(),
            "NASAM recovery incorrectly replayed NotWorkingSound"
        );
        ok &= expect(
            runtimeBuildingPowerTransitionSound(
                *sam,
                false,
                true,
                true,
                false
            ).empty(),
            "initial NASAM power observation emitted feedback"
        );
        ok &= expect(
            runtimeBuildingPowerTransitionSound(
                *sam,
                true,
                false,
                true,
                false
            ).empty(),
            "remote NASAM transition emitted local feedback"
        );
    }

    if (!ok) {
        return 1;
    }
    std::cout << "power audio feedback smoke passed\n";
    return 0;
}
