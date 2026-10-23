#include "data/rules/ProductionRulesLoader.h"
#include "core/io/IniFile.h"

#include <cmath>
#include <stdexcept>

namespace {
constexpr double maximumProductionCoefficient = 100.0;
constexpr double maximumProductionRatio = 1.0;

std::string trim(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

bool parseFiniteDouble(const std::string& value, double& result) {
    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(trim(value), &consumed);
        if (consumed != trim(value).size() || !std::isfinite(parsed)) {
            return false;
        }
        result = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

double readBuildupMinutes(const IniFile& ini, std::vector<std::string>& diagnostics) {
    const std::string value = trim(ini.getValue("General", "BuildupTime", ""));
    if (value.empty()) {
        return 0.05;
    }
    double parsed = 0.0;
    if (parseFiniteDouble(value, parsed)) {
        return parsed;
    }
    diagnostics.push_back("Invalid number: [General] BuildupTime=" + value);
    return 0.05;
}
} // namespace

double readProductionRuleCoefficient(
    const IniFile& ini,
    const std::string& section,
    const std::string& key,
    double defaultValue,
    double maximumValue,
    std::vector<std::string>& diagnostics
) {
    const std::string value = trim(ini.getValue(section, key, ""));
    if (value.empty()) {
        return defaultValue;
    }

    double parsed = 0.0;
    if (!parseFiniteDouble(value, parsed)
        || parsed <= 0.0
        || parsed > maximumValue) {
        diagnostics.push_back(
            "Invalid production value: [" + section + "] "
            + key + "=" + value
        );
        return defaultValue;
    }
    return parsed;
}

ProductionRulesLoadResult loadProductionRules(const IniFile& ini) {
    ProductionRulesLoadResult result;
    auto& rules = result.values;
    const double buildupMinutes = readBuildupMinutes(ini, result.buildupParseDiagnostics);
    rules.buildupTime = std::isfinite(buildupMinutes)
        && buildupMinutes >= 0.0 && buildupMinutes <= 100.0 ? buildupMinutes : 0.05;
    if (rules.buildupTime != buildupMinutes) {
        result.diagnostics.push_back("Invalid buildup value: [General] BuildupTime; using 0.05");
    }
    rules.buildSpeed = readProductionRuleCoefficient(
        ini, "General", "BuildSpeed", 0.7, maximumProductionCoefficient, result.diagnostics);
    rules.multipleFactory = readProductionRuleCoefficient(
        ini, "General", "MultipleFactory", 0.8, maximumProductionRatio, result.diagnostics);
    rules.wallBuildSpeedCoefficient = readProductionRuleCoefficient(
        ini, "General", "WallBuildSpeedCoefficient", 3.0, maximumProductionCoefficient,
        result.diagnostics);
    rules.lowPowerPenaltyModifier = readProductionRuleCoefficient(
        ini, "General", "LowPowerPenaltyModifier", 1.0, maximumProductionCoefficient,
        result.diagnostics);
    rules.minLowPowerProductionSpeed = readProductionRuleCoefficient(
        ini, "General", "MinLowPowerProductionSpeed", 0.5, maximumProductionRatio,
        result.diagnostics);
    rules.maxLowPowerProductionSpeed = readProductionRuleCoefficient(
        ini, "General", "MaxLowPowerProductionSpeed", 0.8, maximumProductionRatio,
        result.diagnostics);
    if (rules.minLowPowerProductionSpeed > rules.maxLowPowerProductionSpeed) {
        result.diagnostics.push_back(
            "Invalid production range: [General] "
            "MinLowPowerProductionSpeed must not exceed "
            "MaxLowPowerProductionSpeed"
        );
        rules.minLowPowerProductionSpeed = 0.5;
        rules.maxLowPowerProductionSpeed = 0.8;
    }
    return result;
}
