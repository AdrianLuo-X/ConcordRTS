#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <clocale>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
// The pre-optimization canonicalizer, independent of the production helper.
std::string oldCanonicalIdKey(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    std::string key;
    key.reserve(last - first + 1);
    for (std::size_t index = first; index <= last; ++index)
        key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value[index]))));
    return key;
}

struct LookupOracle {
    using Index = std::unordered_map<std::string, const UnitType*>;
    Index infantry, vehicle, aircraft;
    explicit LookupOracle(const RulesDatabase& rules) {
        const auto capture = [](const auto& types, auto& index) {
            for (const UnitType& type : types) {
                if (type.id.size() > 256) continue;
                const std::string key = oldCanonicalIdKey(type.id);
                if (!key.empty()) index.emplace(key, &type);
            }
        };
        // The index keeps the locale used at load time, even if later queries
        // change LC_CTYPE. Rebuilding the oracle per query would hide that case.
        capture(rules.getInfantryTypes(), infantry);
        capture(rules.getVehicleTypes(), vehicle);
        capture(rules.getAircraftTypes(), aircraft);
    }
    const UnitType* find(const std::string& kind, const std::string& id) const {
        const std::string normalized = oldCanonicalIdKey(kind);
        const Index* index = nullptr;
        if (normalized == "infantry" || normalized == "infantrytype") index = &infantry;
        else if (normalized == "vehicle" || normalized == "unit" || normalized == "unittype") index = &vehicle;
        else if (normalized == "aircraft" || normalized == "aircrafttype") index = &aircraft;
        if (!index) return nullptr;
        const std::string key = oldCanonicalIdKey(id);
        if (key.empty()) return nullptr;
        const auto found = index->find(key);
        return found == index->end() ? nullptr : found->second;
    }
};

bool loadFixture(RulesDatabase& rules, int strengthOffset = 0) {
    const std::string longId(64, 'X');
    const std::string excessiveId(257, 'Y');
    const std::string fixture =
        "[General]\n[InfantryTypes]\n0=E1\n1=SHARED\n2=IUNIT\n3=" + longId
        + "\n4=" + excessiveId + "\n[VehicleTypes]\n0=MTNK\n1=SHARED\n2=mtnk\n"
        "[AircraftTypes]\n0=ORCA\n1=SHARED\n[BuildingTypes]\n"
        "[OverlayTypes]\n[Tiberiums]\n[TerrainTypes]\n[Warheads]\n"
        "[E1]\nStrength=" + std::to_string(125 + strengthOffset)
        + "\n[MTNK]\nStrength=400\n[SHARED]\nStrength=180\n[ORCA]\nStrength=200\n"
        "[IUNIT]\nStrength=90\n[" + longId + "]\nStrength=70\n["
        + excessiveId + "]\nStrength=80\n";
    IniFile ini;
    return ini.loadFromString(fixture) && rules.loadFromIni(ini);
}

bool compare(const RulesDatabase& rules, const LookupOracle& oracle,
    const std::string& kind, const std::string& id, std::size_t& checks) {
    ++checks;
    const auto* expected = oracle.find(kind, id);
    const auto* actual = rules.findUnitType(kind, id);
    if (actual == expected) return true;
    std::cerr << "Lookup dispatch mismatch: kindBytes=" << kind.size()
        << " idBytes=" << id.size() << " expected=" << (expected ? expected->id : "<null>")
        << " actual=" << (actual ? actual->id : "<null>") << '\n';
    return false;
}

bool checkLocale(const RulesDatabase& rules, const LookupOracle& oracle, std::size_t& checks) {
    const std::vector<std::string> ids{
        "E1", "e1", " \tE1\r\n", "MTNK", "mtnk", "ORCA", "SHARED", "IUNIT", "iunit",
        "", " \t\r\n", "missing", "\vE1", "E1\f", std::string("E1\0", 3),
        std::string(64, 'X'), std::string(64, 'x'), std::string(257, 'Y')
    };
    for (const std::string& base : {"InfantryType", "UnitType", "AircraftType", "infantry", "vehicle", "unit", "aircraft"}) {
        // All case variants include the three optimized exact literals and
        // every mixed/lowercase spelling that must retain scalar dispatch.
        for (std::size_t mask = 0; mask < (std::size_t{1} << base.size()); ++mask) {
            std::string kind = base;
            for (std::size_t index = 0; index < kind.size(); ++index) {
                const char lower = base[index] >= 'A' && base[index] <= 'Z' ? base[index] + ('a' - 'A') : base[index];
                kind[index] = (mask & (std::size_t{1} << index)) ? lower - ('a' - 'A') : lower;
            }
            for (const auto& id : ids) if (!compare(rules, oracle, kind, id, checks)) return false;
        }
        for (const char* prefix : {" ", "\t", "\r", "\n", "\v", "\f"})
            for (const auto& id : ids)
                if (!compare(rules, oracle, std::string(prefix) + base + prefix, id, checks)) return false;
    }
    for (unsigned int byte = 0; byte < 256; ++byte) {
        const std::string character(1, static_cast<char>(byte));
        for (const std::string& kind : {"InfantryType", "UnitType", "AircraftType"}) {
            if (!compare(rules, oracle, character + kind, "SHARED", checks)
                || !compare(rules, oracle, kind + character, "SHARED", checks)
                || !compare(rules, oracle, kind, character + "E1", checks)
                || !compare(rules, oracle, kind, "E1" + character, checks)) return false;
        }
    }
    for (const std::string& kind : {"", " \t\r\n", "BuildingType", "VehicleType", "InfantryTypes", "units"})
        for (const auto& id : ids) if (!compare(rules, oracle, kind, id, checks)) return false;
    return true;
}

void benchmark(const RulesDatabase& rules) {
    using Query = std::pair<std::string, std::string>;
    const std::array<std::vector<Query>, 3> workloads{{
        {{"InfantryType", "E1"}, {"UnitType", "MTNK"}, {"AircraftType", "ORCA"}},
        {{"infantry", "e1"}, {" vehicle ", " MTNK "}, {"AIRCRAFT", "orca"}, {"unknown", "E1"}},
        {{"InfantryType", "SHARED"}, {"UnitType", "missing"}, {"AircraftType", "SHARED"},
            {"InfantryType", std::string(64, 'X')}, {"UnitType", " \tmTnK\r\n"}}
    }};
    constexpr int calls = 500000;
    for (std::size_t workload = 0; workload < workloads.size(); ++workload) {
        std::array<double, 3> elapsed{};
        std::uint64_t checksum = 0;
        for (std::size_t round = 0; round < elapsed.size(); ++round) {
            checksum = 0;
            const auto started = std::chrono::steady_clock::now();
            for (int index = 0; index < calls; ++index) {
                const auto& query = workloads[workload][static_cast<std::size_t>(index) % workloads[workload].size()];
                const auto* result = rules.findUnitType(query.first, query.second);
                checksum += result ? static_cast<std::uint64_t>(result->strength) : 1;
            }
            elapsed[round] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        }
        std::sort(elapsed.begin(), elapsed.end());
        std::cout << "Rules lookup micro: workload=" << workload << " calls=" << calls
            << " medianMs=" << elapsed[1] << " checksum=" << checksum << '\n';
    }
}
}

int main(int argc, char** argv) {
    const std::string originalLocale = std::setlocale(LC_CTYPE, nullptr);
    if (!std::setlocale(LC_CTYPE, "C")) return 1;
    RulesDatabase rules;
    if (!loadFixture(rules)) { std::cerr << "Rules lookup fixture failed to load\n"; return 1; }
    LookupOracle oracle(rules);
    std::size_t checks = 0;
    for (const char* locale : {"C", "English_United States.1252", "Turkish", "tr-TR", "C"}) {
        const char* actual = std::setlocale(LC_CTYPE, locale);
        if (!actual) { std::cout << "Locale unavailable: " << locale << '\n'; continue; }
        std::cout << "Lookup locale: " << actual << " lowerI=" << std::tolower('I') << '\n';
        if (!checkLocale(rules, oracle, checks)) return 1;
    }
    // Public copy/reload behavior stays valid without persistent pointer caches.
    RulesDatabase copied = rules;
    LookupOracle copiedOracle(copied);
    if (!compare(copied, copiedOracle, "InfantryType", "E1", checks)) return 1;
    if (!loadFixture(rules, 10)) return 1;
    LookupOracle reloaded(rules);
    if (!compare(rules, reloaded, "InfantryType", "E1", checks)
        || !compare(copied, copiedOracle, "InfantryType", "E1", checks)) return 1;
    std::cout << "Rules lookup dispatch equivalence passed: checks=" << checks << '\n';
    if (argc > 1 && std::string(argv[1]) == "--benchmark") benchmark(rules);
    std::setlocale(LC_CTYPE, originalLocale.c_str());
    return 0;
}
