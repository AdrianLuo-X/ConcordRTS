#pragma once

#include "data/art/ArtDatabase.h"
#include "analysis/reports/ObjectReferenceValidator.h"
#include "data/rules/RulesDatabase.h"

#include <string>

bool exportRulesToJson(
    const std::string& outputPath,
    const RulesDatabase& rules,
    const DiagnosticReport& diagnostics,
    const ArtDatabase* artDatabase,
    std::string& errorMessage
);
