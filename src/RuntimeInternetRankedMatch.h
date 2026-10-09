#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <span>
#include <vector>

inline constexpr std::size_t runtimeInternetRankedRecordMaximumBytes = 1024;

enum class RuntimeInternetRankedResult : std::uint8_t { None = 0, Win = 1, Loss = 2 };

enum class RuntimeInternetRankedStatus : std::uint8_t {
    Assigned = 1,
    Running = 2,
    AwaitingReport = 3,
    ConflictingReports = 4,
    Confirmed = 5,
    Forfeited = 6,
    VoidMissingReport = 7,
    VoidConflict = 8,
    VoidDisconnected = 9,
    VoidNotStarted = 10,
    VoidNoReports = 11,
    VoidServiceRestart = 12
};

// This is the durable, participant-visible audit record. Wall-clock times are
// for display; live deadlines use the separately held monotonic deadline.
struct RuntimeInternetRankedRecord {
    std::string matchId;
    std::string roomId;
    std::array<std::string, 2> peerIds;
    RuntimeInternetRankedStatus status = RuntimeInternetRankedStatus::Assigned;
    std::array<RuntimeInternetRankedResult, 2> firstReports{};
    std::array<RuntimeInternetRankedResult, 2> reports{};
    bool conflictObserved = false;
    std::string winnerPeerId;
    std::string departingPeerId;
    std::uint64_t authorizedUtcMilliseconds = 0;
    std::uint64_t startedUtcMilliseconds = 0;
    std::uint64_t updatedUtcMilliseconds = 0;
    std::uint64_t deadlineUtcMilliseconds = 0;
    std::uint64_t finalizedUtcMilliseconds = 0;
    bool operator==(const RuntimeInternetRankedRecord&) const = default;
};

struct RuntimeInternetRankedPolicy {
    std::uint32_t assignmentTimeoutMilliseconds = 300000;
    std::uint32_t reportTimeoutMilliseconds = 120000;
    std::uint32_t maximumMatchMilliseconds = 6 * 60 * 60 * 1000;
};

struct RuntimeInternetRankedTransition {
    bool accepted = false;
    bool changed = false;
    bool duplicate = false;
    std::string reason;
};

bool runtimeInternetRankedTerminal(RuntimeInternetRankedStatus);
bool runtimeInternetRankedScored(RuntimeInternetRankedStatus);
bool validateRuntimeInternetRankedPolicy(const RuntimeInternetRankedPolicy&, std::string&);
bool validateRuntimeInternetRankedRecord(const RuntimeInternetRankedRecord&, std::string&);
bool encodeRuntimeInternetRankedRecord(const RuntimeInternetRankedRecord&, std::vector<std::uint8_t>&, std::string&);
bool decodeRuntimeInternetRankedRecord(std::span<const std::uint8_t>, RuntimeInternetRankedRecord&, std::string&);
std::string runtimeInternetRankedStatusText(const RuntimeInternetRankedRecord&, std::string_view localPeerId);

// Pure adjudication. Authentication/authorization and atomic persistence with
// player/Clan points belong to the service. A caller awards points only when
// a transition changes an unresolved record to a scored terminal record.
class RuntimeInternetRankedMatch {
public:
    RuntimeInternetRankedRecord record;
    std::uint64_t deadlineMilliseconds = 0;

    static bool authorize(std::string matchId, std::string roomId,
        std::array<std::string, 2> peerIds, const RuntimeInternetRankedPolicy&,
        std::uint64_t now, std::uint64_t utc, RuntimeInternetRankedMatch& output, std::string& error);
    RuntimeInternetRankedTransition start(const RuntimeInternetRankedPolicy&, std::uint64_t now, std::uint64_t utc);
    RuntimeInternetRankedTransition report(std::string_view peerId, RuntimeInternetRankedResult,
        const RuntimeInternetRankedPolicy&, std::uint64_t now, std::uint64_t utc);
    RuntimeInternetRankedTransition forfeit(std::string_view peerId, std::uint64_t now, std::uint64_t utc);
    bool disconnected(std::string_view peerId, std::uint64_t utc);
    bool expire(std::uint64_t now, std::uint64_t utc);
    bool serviceRestarted(std::uint64_t utc);
private:
    void finish(RuntimeInternetRankedStatus, std::uint64_t utc);
};
