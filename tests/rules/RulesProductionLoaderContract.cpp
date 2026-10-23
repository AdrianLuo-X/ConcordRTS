#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/production/RuntimeProductionTiming.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void need(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}
std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }
const std::string base =
    "[General]\nBuildSpeed=.7\nBuildupTime=.05\nMultipleFactory=.8\n"
    "WallBuildSpeedCoefficient=3\nLowPowerPenaltyModifier=1\n"
    "MinLowPowerProductionSpeed=.5\nMaxLowPowerProductionSpeed=.8\n"
    "[InfantryTypes]\n0=INF\n[VehicleTypes]\n0=VEH\n[AircraftTypes]\n0=AIR\n"
    "[BuildingTypes]\n0=BUILD\n[OverlayTypes]\n[Tiberiums]\n[TerrainTypes]\n[Warheads]\n"
    "[INF]\nCost=1000\n[VEH]\nCost=1000\n[AIR]\nCost=1000\n[BUILD]\nCost=1000\n";

void emit(const RulesDatabase& rules, std::size_t id, bool reload) {
    const auto& p = rules.getProductionRules();
    const auto& h = rules.getHarvesterCadenceRules();
    std::cout << "rules " << id << ' ' << reload << ' '
        << bits(p.buildSpeed) << ' ' << bits(p.buildupTime) << ' '
        << bits(p.multipleFactory) << ' ' << bits(p.wallBuildSpeedCoefficient) << ' '
        << bits(p.lowPowerPenaltyModifier) << ' ' << bits(p.minLowPowerProductionSpeed)
        << ' ' << bits(p.maxLowPowerProductionSpeed) << ' '
        << rules.getMaximumQueuedObjects() << ' ' << h.loadRate << ' '
        << bits(h.dumpRateMinutes) << ' ' << bits(rules.getRefundPercent());
    for (const auto& kind : {"infantry", "vehicle", "aircraft"}) {
        const char* name = std::string(kind) == "infantry" ? "INF"
            : std::string(kind) == "vehicle" ? "VEH" : "AIR";
        const auto* u = rules.findUnitType(kind, name);
        std::cout << ' ' << (u != nullptr) << ' ' << (u ? bits(u->buildTimeMultiplier) : 0);
    }
    const auto* building = rules.findBuildingType("BUILD");
    std::cout << ' ' << (building != nullptr) << ' '
        << (building ? bits(building->buildTimeMultiplier) : 0) << '\n';
    std::cout << "warnings " << rules.getWarnings().size() << '\n';
    for (const auto& warning : rules.getWarnings()) {
        std::cout << warning.size() << ':' << iniHexEncode(warning) << '\n';
    }
    for (int variant = 0; variant < 6; ++variant) {
        RuntimeProductionTimingInput input;
        input.rules = p;
        input.cost = variant == 5 ? 1 : 1000;
        input.buildTimeMultiplier = building ? building->buildTimeMultiplier : 1.0;
        input.wall = variant == 1;
        input.poweredCompatibleFactoryCount = variant >= 2 ? 4 : 1;
        input.powerProduced = variant >= 3 ? 0.0 : 100.0;
        input.powerUsed = 100.0;
        input.fixedTickHz = variant == 4 ? 15 : 30;
        const auto r = runtimeCalculateProductionTiming(input);
        std::cout << "timing " << variant << ' ' << r.valid << ' '
            << static_cast<unsigned>(r.veto) << ' ' << iniHexEncode(r.reason) << ' '
            << bits(r.multipleFactorySpeed) << ' ' << bits(r.lowPowerSpeed) << ' '
            << bits(r.dynamicSpeed) << ' ' << bits(r.base.rawLegacyTicks) << ' '
            << r.base.legacyTicks << ' ' << r.base.fixedTicks << ' '
            << bits(r.effective.rawLegacyTicks) << ' ' << r.effective.legacyTicks << ' '
            << r.effective.fixedTicks << ' ' << r.productionRate.numerator << ' '
            << r.productionRate.denominator << ' ' << r.saturated << '\n';
    }
}
void scenario(std::size_t id, const std::vector<std::string>& layers) {
    IniFile ini;
    need(ini.loadFromString(base), "base parse");
    for (const auto& text : layers) {
        IniFile overlay;
        need(overlay.loadFromString(text), "overlay parse");
        ini.appendFrom(overlay);
    }
    RulesDatabase rules;
    need(rules.loadFromIni(ini), "public RulesDatabase load");
    emit(rules, id, false);
    IniFile empty;
    need(empty.loadFromString("[InfantryTypes]\n[VehicleTypes]\n[AircraftTypes]\n"
        "[BuildingTypes]\n[OverlayTypes]\n[Tiberiums]\n[TerrainTypes]\n[Warheads]\n"), "reload parse");
    need(rules.loadFromIni(empty), "reload public loader");
    need(rules.getWarnings().empty(), "reload clears diagnostics");
    emit(rules, id, true);
}
} // namespace

int main() {
    try {
        std::cout << "layout " << sizeof(ProductionRules) << ' ' << alignof(ProductionRules)
            << ' ' << offsetof(ProductionRules, buildSpeed) << ' '
            << offsetof(ProductionRules, buildupTime) << ' '
            << offsetof(ProductionRules, multipleFactory) << ' '
            << offsetof(ProductionRules, wallBuildSpeedCoefficient) << ' '
            << offsetof(ProductionRules, lowPowerPenaltyModifier) << ' '
            << offsetof(ProductionRules, minLowPowerProductionSpeed) << ' '
            << offsetof(ProductionRules, maxLowPowerProductionSpeed) << ' '
            << sizeof(RulesDatabase) << ' ' << alignof(RulesDatabase) << ' '
            << sizeof(UnitType) << ' ' << sizeof(BuildingType) << '\n';
        const std::array<std::string, 28> tokens{
            "", "   \t", "600tail", "  .25  ", "50%", "1e999", "NaN", "Inf",
            "-Inf", "-1", "0", "-0", ".001", "1", "100", "100.00000001",
            "1.00000001", "1e-9999", "+.75", "0x1p-1", "1 2", ".5tail",
            "2147483648", "-2147483649", ".5 ; comment", ".5 // comment",
            "0.5000000000000001", "1,0"};
        const std::array<std::pair<std::string, std::string>, 15> fields{{
            {"General", "BuildSpeed"}, {"General", "BuildupTime"},
            {"General", "MultipleFactory"}, {"General", "WallBuildSpeedCoefficient"},
            {"General", "LowPowerPenaltyModifier"}, {"General", "MinLowPowerProductionSpeed"},
            {"General", "MaxLowPowerProductionSpeed"}, {"General", "MaximumQueuedObjects"},
            {"General", "HarvesterLoadRate"}, {"General", "HarvesterDumpRate"},
            {"General", "RefundPercent"}, {"INF", "BuildTimeMultiplier"},
            {"VEH", "BuildTimeMultiplier"}, {"AIR", "BuildTimeMultiplier"},
            {"BUILD", "BuildTimeMultiplier"}}};
        std::size_t id = 0;
        scenario(id++, {});
        for (const auto& [section, key] : fields) {
            for (const auto& token : tokens) {
                scenario(id++, {"[" + section + "]\n" + key + "=" + token + "\n"});
            }
        }
        scenario(id++, {"[General]\nBuildSpeed=.4\nBuildSpeed=.6\n"});
        scenario(id++, {"[general]\nbUiLdSpEeD=.3\n", "[GENERAL]\nBuildSpeed=.2\n"});
        scenario(id++, {"[General]\nBuildSpeed=.4\n", "[General]\nBuildSpeed=\n"});
        scenario(id++, {"[General]\nBuildSpeed=.4\n", "[General]\nBuildSpeed=600tail\n"});
        scenario(id++, {"[General]\nMinLowPowerProductionSpeed=.9\nMaxLowPowerProductionSpeed=.2\n"});
        scenario(id++, {"[General]\nMinLowPowerProductionSpeed=.5\nMaxLowPowerProductionSpeed=.5\n"});
        scenario(id++, {"[General]\nBuildupTime=bad\nHarvesterLoadRate=bad\nHarvesterDumpRate=bad\nBuildSpeed=bad\n"});
        scenario(id++, {"[General]\nBuildupTime=-1\nHarvesterLoadRate=bad\nHarvesterDumpRate=bad\nBuildSpeed=bad\n"});
        scenario(id++, {"[General]\nBuildSpeed=.6\n[BUILD]\nBuildTimeMultiplier=2\n",
            "[General]\nMultipleFactory=.7\n[BUILD]\nBuildTimeMultiplier=3\n",
            "[General]\nBuildSpeed=.2\n[BUILD]\nBuildTimeMultiplier=.5\n"});
        scenario(id++, {"[General]\nBuildSpeed=nan\nBuildupTime=nan\nMultipleFactory=nan\n"
            "WallBuildSpeedCoefficient=nan\nLowPowerPenaltyModifier=nan\n"
            "MinLowPowerProductionSpeed=.9\nMaxLowPowerProductionSpeed=.1\n"
            "[INF]\nBuildTimeMultiplier=nan\n[VEH]\nBuildTimeMultiplier=inf\n"
            "[AIR]\nBuildTimeMultiplier=600tail\n[BUILD]\nBuildTimeMultiplier=50%\n"});
        std::cout << "complete scenarios=" << id << " fullRuleSnapshots=" << id * 2U
            << " fullTimingResults=" << id * 12U << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
