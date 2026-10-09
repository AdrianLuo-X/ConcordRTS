#pragma once

#include "ArtDatabase.h"
#include "IniFile.h"
#include "MapDatabase.h"
#include "RulesDatabase.h"

#include <iosfwd>
#include <string>
#include <vector>

struct CampaignDatabase;

enum class EngineSubsystemState {
    Missing,
    Partial,
    Ready
};

struct EngineSubsystemStatus {
    std::string name;
    EngineSubsystemState state = EngineSubsystemState::Missing;
    std::vector<std::string> evidence;
    std::vector<std::string> gaps;
};

struct EngineStatusInput {
    std::string projectRootPath = ".";
    std::string assetRootPath;
    std::string rulesPath;
    std::string artPath;
    std::string mapPath;
    std::string soundPath;
    std::string evaPath;
    const RulesDatabase* rules = nullptr;
    const ArtDatabase* art = nullptr;
    const MapDatabase* map = nullptr;
    const IniFile* soundIni = nullptr;
    const IniFile* evaIni = nullptr;
    const CampaignDatabase* campaign = nullptr;
};

struct EngineStatusReport {
    int readyCount = 0;
    int partialCount = 0;
    int missingCount = 0;
    int totalCount = 0;
    std::vector<EngineSubsystemStatus> subsystems;
};

EngineStatusReport buildEngineStatusReport(const EngineStatusInput& input);
void printEngineStatusReport(std::ostream& output, const EngineStatusReport& report);
