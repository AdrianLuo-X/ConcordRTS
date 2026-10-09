#include "IniFile.h"
#include "RulesDatabase.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "crate rules smoke failure: " << message << '\n';
    }
    return condition;
}

bool closeTo(double actual, double expected) {
    return std::abs(actual - expected) < 0.000001;
}

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

const CratePowerupRule* findPowerup(
    const CrateRules& rules,
    const std::string& type
) {
    const std::string key = lowerAscii(type);
    const auto found = std::find_if(
        rules.powerups.begin(),
        rules.powerups.end(),
        [&key](const CratePowerupRule& powerup) {
            return lowerAscii(powerup.type) == key;
        }
    );
    return found == rules.powerups.end() ? nullptr : &*found;
}

const OverlayType* findOverlay(
    const RulesDatabase& rules,
    const std::string& type
) {
    const std::string key = lowerAscii(type);
    const auto found = std::find_if(
        rules.getOverlayTypes().begin(),
        rules.getOverlayTypes().end(),
        [&key](const OverlayType& overlay) {
            return lowerAscii(overlay.id) == key;
        }
    );
    return found == rules.getOverlayTypes().end() ? nullptr : &*found;
}

int positiveWeight(const CrateRules& rules, bool waterOnly) {
    int total = 0;
    for (const CratePowerupRule& powerup : rules.powerups) {
        if (powerup.weight > 0
            && (!waterOnly || powerup.availableOverWater)) {
            total += powerup.weight;
        }
    }
    return total;
}

std::size_t positivePowerupCount(const CrateRules& rules) {
    return static_cast<std::size_t>(std::count_if(
        rules.powerups.begin(),
        rules.powerups.end(),
        [](const CratePowerupRule& powerup) {
            return powerup.weight > 0;
        }
    ));
}

bool expectPowerup(
    const CrateRules& rules,
    const std::string& type,
    int weight,
    const std::string& animation,
    bool availableOverWater,
    double data
) {
    const CratePowerupRule* powerup = findPowerup(rules, type);
    return expect(
        powerup != nullptr
            && powerup->weight == weight
            && powerup->animation == animation
            && powerup->availableOverWater == availableOverWater
            && closeTo(powerup->data, data),
        type + " powerup fields"
    );
}

bool runFixture(const std::string& path) {
    IniFile ini;
    bool success = expect(ini.loadFromFile(path), "fixture did not load");
    if (!success) {
        return false;
    }

    RulesDatabase rules;
    success &= expect(rules.loadFromIni(ini), "fixture rules did not load");
    const CrateRules& crate = rules.getCrateRules();
    success &= expect(
        crate.maximum == 12
            && crate.minimum == 3
            && closeTo(crate.radius, 4.5)
            && closeTo(crate.regenerationMinutes, 2.25)
            && crate.silverReward == "Veteran"
            && crate.soloMoney == 6789
            && crate.unitType == "GOODIE"
            && crate.woodReward == "Money"
            && crate.waterReward == "Reveal"
            && crate.healSound == "FixtureHeal"
            && crate.woodImage == "WOODBOX"
            && crate.image == "LANDBOX"
            && crate.waterImage == "WATERBOX"
            && !crate.freeMcv,
        "fixture CrateRules fields"
    );
    success &= expect(
        crate.powerups.size() == 9U
            && positivePowerupCount(crate) == 8U
            && positiveWeight(crate, false) == 84
            && positiveWeight(crate, true) == 70,
        "fixture powerup count and land/water weights"
    );
    success &= expectPowerup(crate, "Armor", 7, "ARMORFX", true, 1.75);
    success &= expectPowerup(
        crate,
        "Firepower",
        8,
        "FIREPOWERFX",
        true,
        2.25
    );
    success &= expectPowerup(
        crate,
        "HealBase",
        9,
        "HEALBASEFX",
        true,
        0.0
    );
    success &= expectPowerup(crate, "Money", 10, "MONEYFX", true, 4321.0);
    success &= expectPowerup(crate, "Reveal", 11, "REVEALFX", true, 0.0);
    success &= expectPowerup(crate, "Speed", 12, "SPEEDFX", true, 1.4);
    success &= expectPowerup(
        crate,
        "Veteran",
        13,
        "VETERANFX",
        true,
        2.0
    );
    success &= expectPowerup(crate, "Unit", 14, "<none>", false, 0.0);
    success &= expectPowerup(crate, "Disabled", 0, "<none>", true, 99.0);

    const OverlayType* woodBox = findOverlay(rules, "WOODBOX");
    const OverlayType* landBox = findOverlay(rules, "LANDBOX");
    const OverlayType* waterBox = findOverlay(rules, "WATERBOX");
    const OverlayType* decoration = findOverlay(rules, "DECORATION");
    success &= expect(
        woodBox != nullptr && woodBox->crate && woodBox->crateTrigger
            && landBox != nullptr && landBox->crate
            && waterBox != nullptr && waterBox->crate
            && decoration != nullptr && !decoration->crate,
        "OverlayType Crate/CrateTrigger fields"
    );
    success &= expect(
        rules.getSpecialMechanismRules().flameDamageWarhead
            == "FixtureFire",
        "CombatDamage FlameDamage field"
    );
    const WarheadType* fixtureFire = rules.findWarheadType("FixtureFire");
    success &= expect(
        fixtureFire != nullptr
            && fixtureFire->animList == "FIXTUREFIRE"
            && fixtureFire->bright,
        "CombatDamage FlameDamage warhead discovery"
    );

    const UnitType* goodie = rules.findUnitType("vehicle", "goodie");
    const UnitType* ordinary = rules.findUnitType("vehicle", "ORDINARY");
    success &= expect(
        goodie != nullptr && goodie->crateGoodie
            && ordinary != nullptr && !ordinary->crateGoodie,
        "UnitType CrateGoodie explicit yes/no values"
    );

    IniFile defaultsIni;
    success &= expect(
        defaultsIni.loadFromString(
            "[InfantryTypes]\n[VehicleTypes]\n[AircraftTypes]\n"
            "[BuildingTypes]\n[OverlayTypes]\n[Tiberiums]\n"
            "[TerrainTypes]\n[Warheads]\n"
        ),
        "default rules text did not parse"
    );
    success &= expect(rules.loadFromIni(defaultsIni), "default rules reload failed");
    const CrateRules& defaults = rules.getCrateRules();
    success &= expect(
        defaults.maximum == 255
            && defaults.minimum == 1
            && closeTo(defaults.radius, 3.0)
            && closeTo(defaults.regenerationMinutes, 3.0)
            && defaults.silverReward == "HealBase"
            && defaults.soloMoney == 5000
            && defaults.unitType == "none"
            && defaults.woodReward == "Money"
            && defaults.waterReward == "Money"
            && defaults.healSound == "HealCrate"
            && defaults.woodImage == "CRATE"
            && defaults.image == "CRATE"
            && defaults.waterImage == "WCRATE"
            && defaults.freeMcv
            && defaults.powerups.empty()
            && rules.findUnitType("vehicle", "GOODIE") == nullptr,
        "missing crate sections use defaults and reload clears prior state"
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
    const CrateRules& crate = rules.getCrateRules();
    success &= expect(
        crate.maximum == 255
            && crate.minimum == 1
            && closeTo(crate.radius, 3.0)
            && closeTo(crate.regenerationMinutes, 3.0)
            && crate.silverReward == "HealBase"
            && crate.soloMoney == 5000
            && lowerAscii(crate.unitType) == "none"
            && crate.woodReward == "Money"
            && crate.waterReward == "Money"
            && crate.healSound == "HealCrate"
            && crate.woodImage == "CRATE"
            && crate.image == "CRATE"
            && crate.waterImage == "WCRATE"
            && crate.freeMcv,
        "stock CrateRules values and CRATE/WCRATE images"
    );
    success &= expect(
        positivePowerupCount(crate) == 8U
            && positiveWeight(crate, false) == 110
            && positiveWeight(crate, true) == 90,
        "stock positive powerups and land/water weights"
    );
    success &= expectPowerup(crate, "Armor", 10, "ARMOR", true, 1.5);
    success &= expectPowerup(
        crate,
        "Firepower",
        10,
        "FIREPOWR",
        true,
        2.0
    );
    success &= expectPowerup(crate, "HealBase", 10, "HEALALL", true, 0.0);
    success &= expectPowerup(crate, "Money", 20, "MONEY", true, 2000.0);
    success &= expectPowerup(crate, "Reveal", 10, "REVEAL", true, 0.0);
    success &= expectPowerup(crate, "Speed", 10, "SPEED", true, 1.2);
    success &= expectPowerup(crate, "Veteran", 20, "VETERAN", true, 1.0);
    success &= expectPowerup(crate, "Unit", 20, "<none>", false, 0.0);

    const UnitType* mirageTank = rules.findUnitType("vehicle", "MGTK");
    const UnitType* robotTank = rules.findUnitType("vehicle", "ROBO");
    success &= expect(
        mirageTank != nullptr && mirageTank->crateGoodie
            && robotTank != nullptr && !robotTank->crateGoodie,
        "stock UnitType CrateGoodie values"
    );
    return success;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: concord_rules_crate_smoke "
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
