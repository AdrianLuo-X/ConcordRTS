#pragma once
#include "data/rules/RulesDatabase.h"

inline std::vector<std::string> buildingSuperWeapons(const BuildingType& building) {
    std::vector<std::string> result;
    const auto add = [&](const std::string& value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return;
        const auto id = value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
        const auto key = modExtensionKey(id);
        if (key == "none" || key == "<none>" || key == "") return;
        for (const auto& existing : result) if (modExtensionKey(existing) == key) return;
        result.push_back(id);
    };
    add(building.superWeapon); add(building.superWeapon2);
    for (const auto& id : building.extraSuperWeapons) add(id);
    return result;
}
