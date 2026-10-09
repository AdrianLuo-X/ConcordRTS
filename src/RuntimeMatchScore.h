#pragma once

// Cumulative match facts, indexed by canonical house owner in RuntimeWindowState.
// Presentation clocks and the local player's point of view are kept separately.
struct RuntimeMatchScoreEntry {
    int unitsDestroyed = 0;
    int structuresDestroyed = 0;
    int unitsLost = 0;
    int structuresLost = 0;
    int score = 0;
    int built = 0;

    bool operator==(const RuntimeMatchScoreEntry&) const = default;
};
