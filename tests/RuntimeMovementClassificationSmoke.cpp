#include "RuntimeMovementClassification.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <clocale>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <vector>

#include "RuntimeMovementClassificationContract.inl"

namespace {
bool countAllocations = false;
std::size_t allocationCount = 0;
}
void* operator new(std::size_t size) {
    if (countAllocations) ++allocationCount;
    if (void* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
std::string oldLower(std::string value) {
    for (char& character : value) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return value;
}
std::string oldTrim(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}
bool oldJumpjet(const UnitType& unit) {
    return unit.jumpJet || oldLower(oldTrim(unit.locomotor)) == "{92612c46-f71f-11d1-ac9f-006008055bb5}";
}
bool oldAir(const UnitType& unit) {
    const std::string speedType = oldLower(unit.speedType), movementZone = oldLower(unit.movementZone), locomotor = oldLower(unit.locomotor);
    return oldJumpjet(unit) || unit.consideredAircraft || movementZone.find("fly") != std::string::npos
        || speedType.find("winged") != std::string::npos || speedType.find("fly") != std::string::npos
        || locomotor.find("fly") != std::string::npos;
}
PathMovementMode oldMode(const UnitType& unit) {
    const std::string speedType = oldLower(unit.speedType), movementZone = oldLower(unit.movementZone);
    if (oldAir(unit)) return PathMovementMode::Air;
    if (speedType.find("amphib") != std::string::npos || speedType.find("hover") != std::string::npos
        || movementZone.find("amphib") != std::string::npos || movementZone.find("hover") != std::string::npos) return PathMovementMode::Amphibious;
    if (unit.naval || unit.waterBound || speedType.find("float") != std::string::npos
        || speedType.find("water") != std::string::npos || movementZone.find("water") != std::string::npos) return PathMovementMode::Naval;
    return PathMovementMode::Ground;
}
double oldLand(const LandType& landType, const UnitType& unit, bool infantry) {
    const std::string speedType = oldLower(unit.speedType), movementZone = oldLower(unit.movementZone);
    if (speedType.find("hover") != std::string::npos || movementZone.find("hover") != std::string::npos || movementZone.find("fly") != std::string::npos) return landType.hover;
    if (speedType.find("amphib") != std::string::npos || movementZone.find("amphib") != std::string::npos) return landType.amphibious;
    if (speedType.find("floatbeach") != std::string::npos || movementZone.find("floatbeach") != std::string::npos) return landType.floatBeach;
    if (speedType.find("float") != std::string::npos || speedType.find("water") != std::string::npos
        || movementZone.find("water") != std::string::npos || unit.naval || unit.waterBound) return landType.floatSpeed;
    if (speedType.find("track") != std::string::npos) return landType.track;
    if (speedType.find("wheel") != std::string::npos) return landType.wheel;
    if (infantry || speedType.find("foot") != std::string::npos || movementZone.find("infantry") != std::string::npos) return landType.foot;
    return unit.crusher ? landType.track : landType.wheel;
}
LandType land;
using Probe = std::uint64_t (*)(const UnitType&);
std::uint64_t originalJumpjet(const UnitType& u) { return oldJumpjet(u); }
std::uint64_t optimizedJumpjet(const UnitType& u) { return runtimeUnitJumpjetMovementProfile(u); }
std::uint64_t originalAir(const UnitType& u) { return oldAir(u); }
std::uint64_t optimizedAir(const UnitType& u) { return runtimeUnitAirMovementProfile(u); }
std::uint64_t originalMode(const UnitType& u) { return static_cast<std::uint64_t>(oldMode(u)); }
std::uint64_t optimizedMode(const UnitType& u) { return static_cast<std::uint64_t>(runtimeUnitMovementModeProfile(u)); }
std::uint64_t originalLand(const UnitType& u) { return std::bit_cast<std::uint64_t>(oldLand(land, u, u.id == "E1")); }
std::uint64_t optimizedLand(const UnitType& u) { return std::bit_cast<std::uint64_t>(runtimeUnitLandPercentProfile(land, u, u.id == "E1")); }

bool benchmark(const std::vector<UnitType>& units) {
    constexpr int iterations = 500000;
    for (const auto& pair : std::array<std::pair<Probe, Probe>, 4>{{
            {originalJumpjet, optimizedJumpjet}, {originalAir, optimizedAir},
            {originalMode, optimizedMode}, {originalLand, optimizedLand}}}) {
        const char* name = pair.first == originalJumpjet ? "jumpjet" : pair.first == originalAir ? "air" : pair.first == originalMode ? "mode" : "land";
        std::array<double, 3> oldTimes{}, newTimes{};
        const auto run = [&](Probe fn) {
            std::uint64_t checksum = 0;
            const auto begin = std::chrono::steady_clock::now();
            for (int index = 0; index < iterations; ++index) checksum += fn(units[static_cast<std::size_t>(index) % units.size()]);
            const auto end = std::chrono::steady_clock::now();
            return std::pair{std::chrono::duration<double, std::milli>(end - begin).count(), checksum};
        };
        for (int repetition = 0; repetition < 3; ++repetition) {
            const auto first = run(repetition % 2 ? pair.second : pair.first);
            const auto second = run(repetition % 2 ? pair.first : pair.second);
            if (first.second != second.second) return false;
            oldTimes[repetition] = repetition % 2 ? second.first : first.first;
            newTimes[repetition] = repetition % 2 ? first.first : second.first;
        }
        std::sort(oldTimes.begin(), oldTimes.end()); std::sort(newTimes.begin(), newTimes.end());
        std::cout << "Classification micro " << name << " calls=" << iterations << " oldMs=" << oldTimes[1]
            << " newMs=" << newTimes[1] << " ratio=" << newTimes[1] / oldTimes[1] << '\n';
    }
    return true;
}
}

int main(int argc, char** argv) {
    const std::string originalLocale = std::setlocale(LC_CTYPE, nullptr);
    for (const char* requested : {"C", "English_United States.1252", "Turkish_Turkey.1254"}) {
        const char* actual = std::setlocale(LC_CTYPE, requested);
        if (!actual) { std::cout << "Locale unavailable: " << requested << '\n'; continue; }
        std::cout << "Locale: " << actual << '\n';
        std::string error;
        if (!runRuntimeMovementClassificationContract(error)) { std::cerr << error << '\n'; return 1; }
    }
    std::setlocale(LC_CTYPE, originalLocale.c_str());
    std::vector<UnitType> units(64);
    for (std::size_t index = 0; index < units.size(); ++index) {
        auto& unit = units[index];
        unit.id = index % 4 ? "MTNK" : "E1";
        unit.speedType = index % 4 ? "Track" : "Foot";
        unit.movementZone = index % 4 ? "Normal" : "Infantry";
        unit.locomotor = index % 4 ? "{4A582741-9839-11d1-B709-00A024DDAFD1}" : "{4A582744-9839-11d1-B709-00A024DDAFD1}";
        unit.crusher = index % 4 != 0;
        if (index % 16 == 15) { unit.speedType = "FloatBeach"; unit.naval = true; }
        if (index % 16 == 14) unit.speedType = "Hover";
        if (index % 16 == 13) unit.movementZone = "Fly";
        if (index % 16 == 12) unit.locomotor = " \t{92612C46-F71F-11D1-AC9F-006008055BB5}\r\n";
    }
    std::uint64_t proof = 0;
    allocationCount = 0;
    countAllocations = true;
    for (int round = 0; round < 1000; ++round) for (const auto& unit : units) {
        proof += optimizedJumpjet(unit) + optimizedAir(unit) + optimizedMode(unit) + optimizedLand(unit);
    }
    countAllocations = false;
    if (allocationCount != 0) { std::cerr << "Classification allocated " << allocationCount << " times\n"; return 1; }
    std::cout << "Classification: zero heap allocations in 256000 profile queries; checksum=" << proof << '\n';
    if (argc > 1 && std::strcmp(argv[1], "--benchmark") == 0 && !benchmark(units)) return 1;
    return 0;
}
