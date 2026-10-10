#pragma once

#include "network/lockstep/RuntimeMultiplayerGameplayCommand.h"
#include "network/snapshots/RuntimeMultiplayerSnapshotTransfer.h"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

inline constexpr std::uint16_t runtimeMatchReplayFormatVersion = 1;
inline constexpr std::uint32_t runtimeMatchReplayMaximumRecordBytes = 128U * 1024U * 1024U;
inline constexpr std::uint64_t runtimeMatchReplayMaximumFileBytes = 8ULL * 1024 * 1024 * 1024;

struct RuntimeMatchReplayHeader {
    int initialTick = 0;
    int fixedTickHz = 30;
    std::uint64_t compatibility = 0;
    std::uint64_t initialHash = 0;
    std::string config;
    std::string initialState;
    bool operator==(const RuntimeMatchReplayHeader&) const = default;
};

enum class RuntimeMatchReplayRecordKind : std::uint8_t {
    Initial = 1, Command = 2, Step = 3, Reset = 4, Finish = 5, Selection = 6
};

struct RuntimeMatchReplayRecord {
    RuntimeMatchReplayRecordKind kind = RuntimeMatchReplayRecordKind::Step;
    int tick = 0;
    // A zero checkpoint hash on a Step defers verification to the next
    // checkpoint. Initial, Reset and Finish always carry a nonzero hash.
    std::uint64_t stateHash = 0;
    bool lightweight = false;
    bool applied = false;
    std::string owner;
    RuntimeMultiplayerGameplayCommand command;
    std::string config;
    std::string state;
    std::vector<std::int32_t> selection;
    bool operator==(const RuntimeMatchReplayRecord&) const = default;
};

// Records are streamed; the 128-event diagnostic ring is never involved.
// Every envelope is chained to its predecessor with SHA-256. A mandatory
// Finish record distinguishes a complete recording from an interrupted file.
class RuntimeMatchReplayWriter {
public:
    bool begin(std::ostream&, const RuntimeMatchReplayHeader&, std::string& error);
    bool append(const RuntimeMatchReplayRecord&, std::string& error);
    bool finished() const { return finished_; }
    std::uint64_t records() const { return sequence_; }
private:
    bool envelope(RuntimeMatchReplayRecordKind, const std::vector<std::uint8_t>&, std::string&);
    std::ostream* stream_ = nullptr;
    RuntimeMultiplayerSnapshotSha256 chain_{};
    std::uint64_t sequence_ = 0, bytes_ = 0;
    int tick_ = 0;
    bool failed_ = false, finished_ = false;
};

class RuntimeMatchReplayReader {
public:
    bool begin(std::istream&, RuntimeMatchReplayHeader&, std::string& error);
    bool next(RuntimeMatchReplayRecord&, std::string& error);
    bool finished() const { return finished_; }
    std::uint64_t records() const { return sequence_; }
private:
    bool envelope(RuntimeMatchReplayRecordKind&, std::vector<std::uint8_t>&, std::string&);
    std::istream* stream_ = nullptr;
    RuntimeMultiplayerSnapshotSha256 chain_{};
    std::uint64_t sequence_ = 0, bytes_ = 0;
    int tick_ = 0;
    bool failed_ = false, finished_ = false;
};

// Preflight consumes the stream through Finish and rejects trailing data.
// Callers rewind and begin a fresh reader before actually playing the match.
bool validateRuntimeMatchReplay(std::istream&, RuntimeMatchReplayHeader&,
    std::uint64_t& steps, std::uint64_t& commands, std::string& error);
