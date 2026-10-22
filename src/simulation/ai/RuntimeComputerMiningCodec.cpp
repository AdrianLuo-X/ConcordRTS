#include "simulation/ai/RuntimeComputerMiningCodec.h"

#include "core/io/IniFile.h"

#include <cctype>
#include <ostream>
#include <utility>
#include <vector>

namespace {
std::string toLowerAsciiLocal(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))
        );
    }
    return value;
}

std::string trimAsciiLocal(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size()
        && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first
        && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }

    return value.substr(first, last - first);
}

std::vector<std::string> splitRuntimeSaveFields(const std::string& value) {
    std::vector<std::string> fields;
    std::string current;
    for (char character : value) {
        if (character == '|') {
            fields.push_back(trimAsciiLocal(current));
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    fields.push_back(trimAsciiLocal(current));
    return fields;
}

std::string runtimeSaveField(std::string value) {
    for (char& character : value) {
        if (character == '|' || character == '\r' || character == '\n') {
            character = ' ';
        }
    }
    return value;
}

bool parseRuntimeInt(const std::string& value, int& output) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(trimAsciiLocal(value), &consumed);
        if (consumed != trimAsciiLocal(value).size()) {
            return false;
        }
        output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

std::uint64_t runtimeStableHashString(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char character : value) {
        hash ^= static_cast<std::uint64_t>(character);
        hash *= 1099511628211ull;
    }
    return hash;
}

void runtimeStableHashMix(std::uint64_t& hash, long long value) {
    hash ^= static_cast<std::uint64_t>(value) + 0x9e3779b97f4a7c15ull
        + (hash << 6)
        + (hash >> 2);
}

void runtimeStableHashMixString(
    std::uint64_t& hash,
    const std::string& value
) {
    runtimeStableHashMix(
        hash,
        static_cast<long long>(runtimeStableHashString(value))
    );
}
} // namespace

void writeRuntimeComputerMining(std::ostream& output, const std::map<std::string, RuntimeComputerMiningState>& miningByOwner) {
    output << "\n[ComputerMining]\nSchema=1\nCount="
           << miningByOwner.size() << '\n';
    std::size_t index = 0;
    for (const auto& [owner, mining] : miningByOwner) {
        output << index++ << '=' << runtimeSaveField(owner) << '|'
            << mining.storedResourceUnits << '|' << mining.storedResourceValue << '|'
            << mining.storedResourceGemUnits << '|' << mining.resourceTrips << '|'
            << mining.harvesterTeleports << '|' << mining.slaveMinerLoads << '\n';
    }
}

bool readRuntimeComputerMining(const IniFile& ini,
    std::map<std::string, RuntimeComputerMiningState>& output, std::string& error) {
    std::map<std::string, RuntimeComputerMiningState> candidate;
    if (ini.getSection("ComputerMining") == nullptr) {
        output.clear();
        return true; // Old saves had real cargo, but no computer ore storage.
    }
    int schema = 0, count = -1;
    if (!parseRuntimeInt(ini.getValue("ComputerMining", "Schema", ""), schema)
        || schema != 1
        || !parseRuntimeInt(ini.getValue("ComputerMining", "Count", ""), count)
        || count < 0 || count > 10000) {
        error = "invalid computer mining header"; return false;
    }
    for (int index = 0; index < count; ++index) {
        const auto fields = splitRuntimeSaveFields(
            ini.getValue("ComputerMining", std::to_string(index), ""));
        if (fields.size() != 7U || trimAsciiLocal(fields[0]).empty()) {
            error = "invalid computer mining record"; return false;
        }
        RuntimeComputerMiningState mining;
        int* values[] = {&mining.storedResourceUnits, &mining.storedResourceValue,
            &mining.storedResourceGemUnits, &mining.resourceTrips,
            &mining.harvesterTeleports, &mining.slaveMinerLoads};
        for (int i = 0; i < 6; ++i) {
            if (!parseRuntimeInt(fields[i + 1], *values[i]) || *values[i] < 0) {
                error = "invalid computer mining value"; return false;
            }
        }
        if (mining.storedResourceGemUnits > mining.storedResourceUnits
            || !candidate.emplace(toLowerAsciiLocal(trimAsciiLocal(fields[0])), mining).second) {
            error = "invalid computer mining owner or cargo"; return false;
        }
    }
    output = std::move(candidate);
    return true;
}

void mixRuntimeComputerMiningHash(std::uint64_t& hash,
    const std::map<std::string, RuntimeComputerMiningState>& miningByOwner) {
    if (!miningByOwner.empty()) {
        runtimeStableHashMixString(hash, "ComputerMining/1");
        for (const auto& [owner, mining] : miningByOwner) {
            runtimeStableHashMixString(hash, owner);
            runtimeStableHashMix(hash, mining.storedResourceUnits);
            runtimeStableHashMix(hash, mining.storedResourceValue);
            runtimeStableHashMix(hash, mining.storedResourceGemUnits);
            runtimeStableHashMix(hash, mining.resourceTrips);
            runtimeStableHashMix(hash, mining.harvesterTeleports);
            runtimeStableHashMix(hash, mining.slaveMinerLoads);
        }
    }
}
