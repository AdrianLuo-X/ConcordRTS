#pragma once

#include "data/rules/ProductionRules.h"

#include <string>
#include <vector>

class IniFile;

struct ProductionRulesLoadResult {
    ProductionRules values;
    // The legacy owner reads harvester cadence between these two groups.
    std::vector<std::string> buildupParseDiagnostics;
    std::vector<std::string> diagnostics;
};

// Reads synchronously; neither the INI nor any reference to it is retained.
ProductionRulesLoadResult loadProductionRules(const IniFile& ini);

// The existing object loader supplies its own section, default and upper bound.
// This contract deliberately differs from permissive integers and percentages.
double readProductionRuleCoefficient(
    const IniFile& ini,
    const std::string& section,
    const std::string& key,
    double defaultValue,
    double maximumValue,
    std::vector<std::string>& diagnostics
);
