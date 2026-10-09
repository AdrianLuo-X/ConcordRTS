#include "IniFile.h"
#include "RulesDatabase.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <string>

namespace {

bool fail(const std::string& message) {
    std::cerr << "Overlay identity: " << message << '\n';
    return false;
}

bool checkFixture(const RulesDatabase& rules, const char* phase) {
    struct Expected { const char* id; const char* image; int listIndex; bool legalTarget; };
    // List keys are deliberately sparse: they are not OverlayPack ordinals.
    // Missing sections retain slots; duplicate IDs retain their first slot.
    constexpr std::array<Expected, 6> expected{{
        {"MISSING_HEAD", "MISSING_HEAD", 2, false},
        {"BRIDGE1", "BRIDGE", 7, true},
        {"MISSING_MIDDLE", "MISSING_MIDDLE", 25, false},
        {"GAWALL", "WALLART", 30, false},
        {"BRIDGE2", "BRIDGE", 50, true},
        {"MISSING_TAIL", "MISSING_TAIL", 75, false}
    }};
    const auto& overlays = rules.getOverlayTypes();
    if (overlays.size() != expected.size())
        return fail(std::string(phase) + " wrong placeholder/duplicate count");
    for (std::size_t ordinal = 0; ordinal < expected.size(); ++ordinal) {
        const auto& actual = overlays[ordinal];
        const auto& wanted = expected[ordinal];
        if (actual.id != wanted.id || actual.image != wanted.image
            || actual.listIndex != wanted.listIndex || actual.legalTarget != wanted.legalTarget) {
            return fail(std::string(phase) + " ordinal=" + std::to_string(ordinal)
                + " expected=" + wanted.id + "/" + wanted.image + "/" + std::to_string(wanted.listIndex)
                + " actual=" + actual.id + "/" + actual.image + "/" + std::to_string(actual.listIndex));
        }
    }
    return true;
}

bool fixtureContract() {
    IniFile ini;
    if (!ini.loadFromString(R"INI(
[OverlayTypes]
2=MISSING_HEAD
7=BRIDGE1
11=bridge1
25=MISSING_MIDDLE
30=GAWALL
31=missing_head
50=BRIDGE2
75=MISSING_TAIL
[BRIDGE1]
Image=BRIDGE
Name=First bridge
LegalTarget=yes
[GAWALL]
Image=WALLART
LegalTarget=no
[BRIDGE2]
Image=BRIDGE
Name=Second bridge
LegalTarget=yes
)INI")) return fail("fixture INI");
    RulesDatabase rules;
    if (!rules.loadFromIni(ini) || !checkFixture(rules, "load")) return false;
    RulesDatabase copied = rules;
    if (!checkFixture(copied, "copy")) return false;
    if (!rules.loadFromIni(ini) || !checkFixture(rules, "reload")
        || !checkFixture(copied, "copy after reload")) return false;
    std::cout << "Overlay identity: missing head/middle/tail, case-insensitive duplicates, "
        "sparse keys, fields, copy and reload passed\n";
    return true;
}

bool stockContract(const std::string& path) {
    IniFile ini;
    RulesDatabase rules;
    if (!ini.loadFromFile(path) || !rules.loadFromIni(ini)) return fail("stock rules load: " + path);
    // These independent retail constants distinguish the native ordinal from
    // its authored numeric key and catch GAWALL being renamed to BRIDGE1.
    if (ini.getValue("OverlayTypes", "25", "") != "BRIDGE1"
        || ini.getValue("BRIDGE1", "Image", "") != "BRIDGE")
        return fail("stock source is not the expected BRIDGE1 registry");
    const auto& overlays = rules.getOverlayTypes();
    if (overlays.size() <= 24) return fail("stock overlay registry too short");
    const auto& bridge = overlays[24];
    if (bridge.id != "BRIDGE1" || bridge.image != "BRIDGE" || bridge.listIndex != 25 || !bridge.legalTarget)
        return fail("stock ordinal 24 expected BRIDGE1/BRIDGE/key25, actual="
            + bridge.id + "/" + bridge.image + "/" + std::to_string(bridge.listIndex));
    if (overlays[2].id != "GAWALL" || overlays[2].image != "GAWALL" || overlays[2].name != "Allied Wall")
        return fail("stock GAWALL identity/fields moved from ordinal 2");
    if (std::count_if(overlays.begin(), overlays.end(), [](const OverlayType& overlay) {
            return overlay.id == "BRIDGE1";
        }) != 1) return fail("stock BRIDGE1 identity is not unique");
    std::cout << "Overlay identity: stock BRIDGE1 ordinal=24 listKey=25 image=BRIDGE; "
        "GAWALL ordinal=2; registry=" << overlays.size() << " passed\n";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 1 && (argc != 3 || std::string(argv[1]) != "--stock")) {
        std::cerr << "Usage: concord_rules_overlay_identity_smoke [--stock rulesmd.ini]\n";
        return 2;
    }
    if (!fixtureContract()) return 1;
    if (argc == 3 && !stockContract(argv[2])) return 1;
    return 0;
}
