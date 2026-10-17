#pragma once

#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>

class IniFile;

// Cumulative facts, ordered by canonical house owner. Presentation stays outside.
struct RuntimeMatchScoreEntry {
    int unitsDestroyed = 0;
    int structuresDestroyed = 0;
    int unitsLost = 0;
    int structuresLost = 0;
    int score = 0;
    int built = 0;

    bool operator==(const RuntimeMatchScoreEntry&) const = default;
};

using RuntimeMatchScoreState = std::map<std::string, RuntimeMatchScoreEntry>;

struct RuntimeMatchScoreBuildEvent {
    std::string owner;
    int count = 1;
};

// The host filters campaign/finished/neutral/DontScore exits and resolves allies.
// An empty creditedOwner records only the loss. Events are cumulative, not deduped.
struct RuntimeMatchScoreDestructionEvent {
    std::string lostOwner;
    std::string creditedOwner;
    bool structure = false;
    int cost = 0;
};

void recordRuntimeMatchScoreBuild(RuntimeMatchScoreState& scores, const RuntimeMatchScoreBuildEvent& event);
void recordRuntimeMatchScoreDestruction(RuntimeMatchScoreState& scores, const RuntimeMatchScoreDestructionEvent& event);
void writeRuntimeMatchScore(const RuntimeMatchScoreState& scores, std::ostream& output);
bool loadRuntimeMatchScore(RuntimeMatchScoreState& scores, const IniFile& ini, std::string& error);
void hashRuntimeMatchScore(const RuntimeMatchScoreState& scores, std::uint64_t& hash);
