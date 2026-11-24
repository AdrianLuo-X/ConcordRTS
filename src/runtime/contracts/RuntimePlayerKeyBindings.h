#pragma once

#include <string>
#include <utility>
#include <vector>

// Retail keyboardmd.ini uses virtual-key bits 0..7 followed by Shift/Ctrl/Alt.
inline constexpr int runtimePlayerShiftKey = 0x100;
inline constexpr int runtimePlayerControlKey = 0x200;
inline constexpr int runtimePlayerAltKey = 0x400;
inline int runtimePlayerKeyCode(int key, bool shift, bool control, bool alt) {
    return (key & 0xff) | (shift ? runtimePlayerShiftKey : 0)
        | (control ? runtimePlayerControlKey : 0) | (alt ? runtimePlayerAltKey : 0);
}
inline bool runtimePlayerValidKeyCode(int key) {
    return (key & 0xff) != 0 && (key & ~0x7ff) == 0;
}
inline std::vector<std::pair<std::string, int>> runtimePlayerCommandBindings() {
    std::vector<std::pair<std::string, int>> result{
        {"TypeSelect", 'T'}, {"CombatantSelect", 'P'}, {"VeterancyNav", 'Y'},
        {"HealthNav", 'U'}, {"PreviousObject", 'M'}, {"NextObject", 'N'},
        {"PlanningMode", 'Z'}, {"Follow", 'F'},
        {"CenterOnRadarEvent", 32}, {"CenterBase", 'H'},
        {"StructureTab", 'Q'}, {"DefenseTab", 'W'}, {"InfantryTab", 'E'}, {"UnitTab", 'R'},
        {"ToggleRepair", 'K'}, {"ToggleSell", 'L'}, {"ShowCoordinates", 'O'},
        {"Diplomacy", 9}, {"ToggleAlliance", 'A'}, {"PlaceBeacon", 'B'},
        {"DeleteBeacon", 46}, {"AllToCheer", 'C'}, {"ChatToAll", 13}, {"ChatToAllies", 36},
        {"SetPrimaryFactory", 'U' | runtimePlayerControlKey}, {"SetRallyPoint", 'J'}
    };
    for (int number = 1; number <= 10; ++number) {
        const std::string suffix = "_" + std::to_string(number);
        const int key = number == 10 ? '0' : '0' + number;
        result.emplace_back("TeamSelect" + suffix, key);
        result.emplace_back("TeamAddSelect" + suffix, key | runtimePlayerShiftKey);
        result.emplace_back("TeamCreate" + suffix, key | runtimePlayerControlKey);
        result.emplace_back("TeamCenter" + suffix, key | runtimePlayerAltKey);
    }
    for (int i = 1; i <= 4; ++i) {
        result.emplace_back("View" + std::to_string(i), 111 + i);
        result.emplace_back("SetView" + std::to_string(i), (111 + i) | runtimePlayerControlKey);
    }
    for (int i = 1; i <= 8; ++i)
        result.emplace_back("Taunt_" + std::to_string(i), 115 + i);
    return result;
}
