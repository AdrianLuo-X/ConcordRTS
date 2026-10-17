#include "core/results/RuntimeMatchScore.h"
#include "core/io/IniFile.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <ostream>
#include <sstream>
#include <utility>

namespace {

// Keep the original host's cctype/locale behavior, including all isspace bytes.
std::string trimOwner(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}

std::string canonicalOwner(const std::string& owner) {
    auto value = trimOwner(owner);
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

int parseIntOrDefault(const std::string& value, int fallback) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(trimOwner(value), &consumed);
        if (consumed != trimOwner(value).size()) return fallback;
        return parsed;
    } catch (...) {
        return fallback;
    }
}

void addSaturated(int& value, int amount) {
    value = static_cast<int>(std::min<long long>(
        static_cast<long long>(value) + std::max(0, amount), std::numeric_limits<int>::max()));
}

void mix(std::uint64_t& hash, long long value) {
    hash ^= static_cast<std::uint64_t>(value) + 0x9e3779b97f4a7c15ull
        + (hash << 6) + (hash >> 2);
}

void mixString(std::uint64_t& hash, const std::string& value) {
    std::uint64_t stringHash = 1469598103934665603ull;
    for (unsigned char character : value) {
        stringHash ^= static_cast<std::uint64_t>(character);
        stringHash *= 1099511628211ull;
    }
    mix(hash, static_cast<long long>(stringHash));
}

} // namespace

void recordRuntimeMatchScoreBuild(RuntimeMatchScoreState& scores, const RuntimeMatchScoreBuildEvent& event) {
    if (event.count <= 0 || trimOwner(event.owner).empty()) return;
    addSaturated(scores[canonicalOwner(event.owner)].built, event.count);
}

void recordRuntimeMatchScoreDestruction(RuntimeMatchScoreState& scores, const RuntimeMatchScoreDestructionEvent& event) {
    auto& lost = scores[canonicalOwner(event.lostOwner)];
    addSaturated(event.structure ? lost.structuresLost : lost.unitsLost, 1);
    if (event.creditedOwner.empty()) return;
    auto& killed = scores[canonicalOwner(event.creditedOwner)];
    addSaturated(event.structure ? killed.structuresDestroyed : killed.unitsDestroyed, 1);
    // The score is the destroyed enemy's authored base Cost, as before.
    addSaturated(killed.score, event.cost);
}
void writeRuntimeMatchScore(const RuntimeMatchScoreState& scores, std::ostream& output) {
    const bool hasBuilds = std::any_of(scores.begin(), scores.end(),
        [](const auto& entry) { return entry.second.built != 0; });
    output << "\n[RuntimeMatchScore]\nSchema=" << (hasBuilds ? 2 : 1)
        << "\nCount=" << scores.size() << '\n';
    int index = 0;
    for (const auto& [owner, score] : scores) {
        output << index++ << '=' << iniHexEncode(owner) << ' '
            << score.unitsDestroyed << ' ' << score.structuresDestroyed << ' '
            << score.unitsLost << ' ' << score.structuresLost << ' ' << score.score;
        if (hasBuilds) output << ' ' << score.built;
        output << '\n';
    }
}

bool loadRuntimeMatchScore(RuntimeMatchScoreState& scores, const IniFile& ini, std::string& error) {
    std::map<std::string, RuntimeMatchScoreEntry> loaded;
    if (ini.getSection("RuntimeMatchScore") == nullptr) {
        scores.clear();
        return true;
    }
    const int count = parseIntOrDefault(ini.getValue("RuntimeMatchScore", "Count", "-1"), -1);
    const int schema = parseIntOrDefault(ini.getValue("RuntimeMatchScore", "Schema", ""), 0);
    if ((schema != 1 && schema != 2) || count < 0 || count > 4096) {
        error = "invalid match score schema/count";
        return false;
    }
    for (int index = 0; index < count; ++index) {
        std::istringstream record(ini.getValue("RuntimeMatchScore", std::to_string(index), ""));
        std::string encodedOwner, owner, trailing;
        RuntimeMatchScoreEntry score;
        if (!(record >> encodedOwner >> score.unitsDestroyed >> score.structuresDestroyed
                >> score.unitsLost >> score.structuresLost >> score.score)
            || (schema == 2 && !(record >> score.built))
            || (record >> trailing) || !iniHexDecode(encodedOwner, owner, 1024)
            || trimOwner(owner).empty() || score.unitsDestroyed < 0 || score.structuresDestroyed < 0
            || score.unitsLost < 0 || score.structuresLost < 0 || score.score < 0 || score.built < 0
            || !loaded.emplace(canonicalOwner(owner), score).second) {
            error = "invalid match score record " + std::to_string(index);
            return false;
        }
    }
    scores = std::move(loaded);
    return true;
}

void hashRuntimeMatchScore(const RuntimeMatchScoreState& scores, std::uint64_t& hash) {
    // Leave legacy empty-score snapshots' authoritative digest unchanged.
    if (scores.empty()) return;
    mixString(hash, "match-score-v1");
    mix(hash, static_cast<long long>(scores.size()));
    for (const auto& [owner, score] : scores) {
        mixString(hash, owner);
        mix(hash, score.unitsDestroyed);
        mix(hash, score.structuresDestroyed);
        mix(hash, score.unitsLost);
        mix(hash, score.structuresLost);
        mix(hash, score.score);
    }
    // Preserve existing snapshots' digest when they have no build facts.
    for (const auto& [owner, score] : scores) {
        if (score.built == 0) continue;
        mixString(hash, "match-built-v2");
        mixString(hash, owner);
        mix(hash, score.built);
    }
}
