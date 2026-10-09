#pragma once

#include "IniFile.h"
#include "RulesDatabase.h"

#include <string>
#include <vector>

struct ModCompatibilityScanOptions {
    bool hasArtIni = false;
    bool hasMapIni = false;
};

struct ModCompatibilityLevelResult {
    int level = 0;
    std::string name;
    bool achieved = false;
    std::string detail;
};

struct ModCompatibilityFinding {
    std::string severity;
    std::string source;
    std::string section;
    std::string key;
    std::string value;
    std::string message;
};

struct ModCompatibilityTagHit {
    std::string family;
    std::string section;
    std::string key;
    std::string value;
    bool implemented = false;
    std::string supportDetail;
};

struct ModCompatibilityReport {
    bool success = false;
    std::string errorMessage;
    int achievedLevel = 0;
    int infantryTypes = 0;
    int vehicleTypes = 0;
    int aircraftTypes = 0;
    int buildingTypes = 0;
    int weaponTypes = 0;
    int projectileTypes = 0;
    int warheadTypes = 0;
    int artSections = 0;
    int mapSections = 0;
    int customObjectCount = 0;
    int aresTagCount = 0;
    int phobosTagCount = 0;
    int unknownTagCount = 0;
    std::vector<ModCompatibilityLevelResult> levels;
    std::vector<ModCompatibilityTagHit> tagHits;
    std::vector<ModCompatibilityFinding> findings;
};

class ModCompatibilityScanner {
public:
    ModCompatibilityReport scan(
        const IniFile& rulesIni,
        const RulesDatabase& rules,
        const IniFile* artIni,
        const IniFile* mapIni,
        const ModCompatibilityScanOptions& options
    ) const;
};
