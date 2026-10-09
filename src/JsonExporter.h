#pragma once

#include "ArtDatabase.h"
#include "ObjectReferenceValidator.h"
#include "RulesDatabase.h"

#include <string>

bool exportRulesToJson(
    const std::string& outputPath,
    const RulesDatabase& rules,
    const DiagnosticReport& diagnostics,
    const ArtDatabase* artDatabase,
    std::string& errorMessage
);
