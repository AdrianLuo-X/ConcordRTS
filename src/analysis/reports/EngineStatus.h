#pragma once

#include "analysis/reports/OfflineReportEvidence.h"
#include "data/art/ArtDatabase.h"
#include "core/io/IniFile.h"
#include "data/maps/MapDatabase.h"
#include "data/rules/RulesDatabase.h"

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
    // Parallel to evidence; legacy strings and state retain their old meaning.
    std::vector<std::string> evidenceSources;
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
    OfflineReportEvidence provenance;
};

EngineStatusReport buildEngineStatusReport(const EngineStatusInput& input);
void printEngineStatusReport(std::ostream& output, const EngineStatusReport& report);

void writeEngineStatusReportJson(std::ostream& output, const EngineStatusReport& report);
