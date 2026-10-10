#include "network/internet/RuntimeInternetRankedMatch.h"
#include <iostream>
#include <limits>

namespace {
int failures = 0;
void expect(bool condition, const char* reason) { if (!condition) { ++failures; std::cerr << "FAIL: " << reason << '\n'; } }
const RuntimeInternetRankedPolicy policy{100, 20, 1000};
RuntimeInternetRankedMatch match(bool started = true) {
    RuntimeInternetRankedMatch value;
    std::string error;
    expect(RuntimeInternetRankedMatch::authorize("ranked-1", "room-1", {"bob", "alice"}, policy, 10, 10010, value, error), "authorize sorted participants");
    if (started) expect(value.start(policy, 20, 10020).accepted, "service starts assigned match");
    return value;
}
void valid(const RuntimeInternetRankedMatch& value) {
    std::string error;
    const bool accepted = validateRuntimeInternetRankedRecord(value.record, error);
    expect(accepted, error.c_str());
}
}

int main() {
    using Result = RuntimeInternetRankedResult;
    using Status = RuntimeInternetRankedStatus;
    auto normal = match(false);
    const auto unstarted = normal.record;
    expect(!normal.report("alice", Result::Win, policy, 12, 10012).accepted && normal.record == unstarted,
        "no result can be claimed before service start");
    expect(normal.start(policy, 20, 10020).accepted, "authenticated service start");
    const auto before = normal.record;
    expect(!normal.report("outsider", Result::Win, policy, 22, 10022).accepted && normal.record == before,
        "unassigned reporter cannot change a match");
    expect(!normal.forfeit("outsider", 23, 10023).accepted && normal.record == before, "cannot concede another player's match");
    expect(normal.report("alice", Result::Win, policy, 30, 10030).changed, "first report retained");
    const auto deadline = normal.deadlineMilliseconds;
    expect(normal.report("alice", Result::Win, policy, 42, 10042).duplicate && normal.deadlineMilliseconds == deadline,
        "duplicate reports cannot extend the deadline");
    expect(normal.start(policy, 43, 10043).duplicate && normal.deadlineMilliseconds == deadline,
        "duplicate room seal cannot extend the deadline");
    expect(normal.report("bob", Result::Loss, policy, 49, 10049).accepted && normal.record.status == Status::Confirmed
        && normal.record.winnerPeerId == "alice", "opposite reports finalize an authenticated winner");
    const auto final = normal.record;
    expect(!normal.report("alice", Result::Loss, policy, 50, 10050).accepted && !normal.forfeit("alice", 51, 10051).accepted
        && !normal.disconnected("alice", 10052) && !normal.serviceRestarted(20000) && normal.record == final,
        "a final match cannot change or be awarded twice"); valid(normal);

    auto missing = match(); missing.report("alice", Result::Win, policy, 30, 10030);
    expect(!missing.expire(49, 10049), "missing report remains eligible immediately before cutoff");
    expect(missing.report("bob", Result::Loss, policy, 50, 10050).changed && missing.record.status == Status::VoidMissingReport
        && missing.record.winnerPeerId.empty(), "report at cutoff is too late and cannot award an unverified win"); valid(missing);
    auto conflict = match(); conflict.report("alice", Result::Win, policy, 30, 10030);
    conflict.report("bob", Result::Win, policy, 40, 10040);
    expect(conflict.record.status == Status::ConflictingReports && conflict.record.conflictObserved && conflict.deadlineMilliseconds == 50,
        "conflict retains both reports and the original cutoff");
    auto corrected = conflict;
    corrected.report("bob", Result::Loss, policy, 49, 10049);
    expect(corrected.record.status == Status::Confirmed && corrected.record.firstReports[1] == Result::Win
        && corrected.record.reports[1] == Result::Loss && corrected.record.conflictObserved,
        "timely correction records both original evidence and accepted result"); valid(corrected);
    expect(conflict.expire(50, 10050) && conflict.record.status == Status::VoidConflict && conflict.record.winnerPeerId.empty(),
        "unresolved conflict receives a definitive unscored outcome"); valid(conflict);

    auto inactive = match(false);
    expect(inactive.expire(110, 10110) && inactive.record.status == Status::VoidNotStarted, "unstarted assignments cannot wait forever"); valid(inactive);
    auto noReports = match();
    expect(noReports.expire(1020, 11020) && noReports.record.status == Status::VoidNoReports, "silent matches reach a hard final cutoff"); valid(noReports);
    auto nearLimit = match(); nearLimit.report("alice", Result::Win, policy, 1019, 11019);
    expect(nearLimit.deadlineMilliseconds == 1020 && nearLimit.expire(1020, 11020), "late first report cannot extend the hard match limit"); valid(nearLimit);

    auto quit = match();
    expect(quit.forfeit("alice", 40, 10040).accepted && quit.record.status == Status::Forfeited
        && quit.record.departingPeerId == "alice" && quit.record.winnerPeerId == "bob", "only the conceding authenticated player takes the loss"); valid(quit);
    auto cancelled = match(false);
    expect(cancelled.forfeit("alice", 15, 10015).accepted && cancelled.record.status == Status::VoidNotStarted
        && cancelled.record.winnerPeerId.empty(), "leaving before start cancels without points"); valid(cancelled);
    auto disconnected = match(); disconnected.report("alice", Result::Win, policy, 30, 10030);
    expect(disconnected.disconnected("bob", 10040) && disconnected.record.status == Status::VoidDisconnected
        && disconnected.record.winnerPeerId.empty(), "a service-observed final disconnect is not proof of the opponent's win"); valid(disconnected);
    auto restarted = match();
    expect(restarted.serviceRestarted(9000) && restarted.record.status == Status::VoidServiceRestart
        && restarted.record.finalizedUtcMilliseconds >= restarted.record.authorizedUtcMilliseconds,
        "server restart finalizes abandoned authority even if wall clock moved backward"); valid(restarted);

    auto forged = quit.record; forged.winnerPeerId = forged.departingPeerId;
    std::string error;
    expect(!validateRuntimeInternetRankedRecord(forged, error), "persisted forfeit cannot award the departing player");
    forged = missing.record; forged.winnerPeerId = "alice";
    expect(!validateRuntimeInternetRankedRecord(forged, error), "void records cannot smuggle in a scored winner");
    forged = normal.record; forged.reports[1] = Result::Win;
    expect(!validateRuntimeInternetRankedRecord(forged, error), "confirmed record must carry matching report evidence");
    RuntimeInternetRankedPolicy invalidPolicy = policy; invalidPolicy.reportTimeoutMilliseconds = 0;
    auto unchanged = match(); const auto validRecord = unchanged.record;
    expect(!unchanged.report("alice", Result::Win, invalidPolicy, 30, 10030).accepted && unchanged.record == validRecord,
        "invalid deadline policy is transactional");
    RuntimeInternetRankedMatch saturated;
    expect(RuntimeInternetRankedMatch::authorize("edge", "edge-room", {"alice", "bob"}, policy,
        (std::numeric_limits<std::uint64_t>::max)() - 5, 1000, saturated, error)
        && saturated.deadlineMilliseconds == (std::numeric_limits<std::uint64_t>::max)(), "deadline arithmetic saturates without wraparound");
    for (const auto& record : {normal.record, missing.record, conflict.record, corrected.record, inactive.record,
            noReports.record, quit.record, cancelled.record, disconnected.record, restarted.record}) {
        std::vector<std::uint8_t> bytes;
        RuntimeInternetRankedRecord decoded;
        expect(encodeRuntimeInternetRankedRecord(record, bytes, error)
            && decodeRuntimeInternetRankedRecord(bytes, decoded, error) && decoded == record, "canonical audit record round trip");
        const auto sentinel = decoded;
        bytes.push_back(0);
        expect(!decodeRuntimeInternetRankedRecord(bytes, decoded, error) && decoded == sentinel, "trailing audit data is rejected transactionally");
        bytes.pop_back(); bytes.pop_back();
        expect(!decodeRuntimeInternetRankedRecord(bytes, decoded, error) && decoded == sentinel, "truncated audit data is rejected transactionally");
    }
    if (failures) return 1;
    std::cout << "Ranked adjudication core passed: authorized starts, bounded reports/conflicts, forfeit, disconnect, restart and immutable outcomes.\n";
    return 0;
}
