#include "RuntimeInternetRankedMatch.h"
#include <algorithm>
#include <limits>

namespace {
bool fail(std::string& error, const char* reason) { error = reason; return false; }
bool token(std::string_view value) {
    return !value.empty() && value.size() <= 128 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    });
}
bool resultValue(RuntimeInternetRankedResult result) {
    return result == RuntimeInternetRankedResult::None || result == RuntimeInternetRankedResult::Win || result == RuntimeInternetRankedResult::Loss;
}
std::uint64_t after(std::uint64_t now, std::uint32_t duration) {
    return now > (std::numeric_limits<std::uint64_t>::max)() - duration ? (std::numeric_limits<std::uint64_t>::max)() : now + duration;
}
std::uint64_t stamp(const RuntimeInternetRankedRecord& record, std::uint64_t utc) {
    return std::max({std::uint64_t{1}, utc, record.updatedUtcMilliseconds});
}
RuntimeInternetRankedTransition rejected(const char* reason) { return {false, false, false, reason}; }
}

bool runtimeInternetRankedTerminal(RuntimeInternetRankedStatus status) {
    return status >= RuntimeInternetRankedStatus::Confirmed && status <= RuntimeInternetRankedStatus::VoidServiceRestart;
}
bool runtimeInternetRankedScored(RuntimeInternetRankedStatus status) {
    return status == RuntimeInternetRankedStatus::Confirmed || status == RuntimeInternetRankedStatus::Forfeited;
}
bool validateRuntimeInternetRankedPolicy(const RuntimeInternetRankedPolicy& policy, std::string& error) {
    if (!policy.assignmentTimeoutMilliseconds || policy.assignmentTimeoutMilliseconds > 3600000
        || !policy.reportTimeoutMilliseconds || policy.reportTimeoutMilliseconds > 3600000
        || !policy.maximumMatchMilliseconds || policy.maximumMatchMilliseconds > 86400000)
        return fail(error, "ranked adjudication deadlines are outside their bounded limits");
    error.clear(); return true;
}
bool validateRuntimeInternetRankedRecord(const RuntimeInternetRankedRecord& record, std::string& error) {
    if (!token(record.matchId) || !token(record.roomId) || !token(record.peerIds[0]) || !token(record.peerIds[1])
        || record.peerIds[0] >= record.peerIds[1] || record.status < RuntimeInternetRankedStatus::Assigned
        || record.status > RuntimeInternetRankedStatus::VoidServiceRestart
        || !record.authorizedUtcMilliseconds || record.updatedUtcMilliseconds < record.authorizedUtcMilliseconds
        || (record.startedUtcMilliseconds && (record.startedUtcMilliseconds < record.authorizedUtcMilliseconds
            || record.startedUtcMilliseconds > record.updatedUtcMilliseconds)))
        return fail(error, "ranked record identity, state or timestamps are invalid");
    const bool terminal = runtimeInternetRankedTerminal(record.status);
    if (terminal != (record.finalizedUtcMilliseconds != 0)
        || (terminal && (record.finalizedUtcMilliseconds != record.updatedUtcMilliseconds || record.deadlineUtcMilliseconds))
        || (!terminal && !record.deadlineUtcMilliseconds)) return fail(error, "ranked record finalization fields are inconsistent");
    for (std::size_t index = 0; index < 2; ++index)
        if (!resultValue(record.reports[index]) || !resultValue(record.firstReports[index])
            || ((record.reports[index] == RuntimeInternetRankedResult::None) != (record.firstReports[index] == RuntimeInternetRankedResult::None)))
            return fail(error, "ranked record reports are inconsistent");
    const bool hasReports = record.reports[0] != RuntimeInternetRankedResult::None || record.reports[1] != RuntimeInternetRankedResult::None;
    if ((hasReports || record.status == RuntimeInternetRankedStatus::Running || runtimeInternetRankedScored(record.status))
        && !record.startedUtcMilliseconds) return fail(error, "ranked record has gameplay results before service start");
    if (record.status == RuntimeInternetRankedStatus::Assigned && (hasReports || record.startedUtcMilliseconds))
        return fail(error, "assigned match already contains gameplay state");
    if (record.status == RuntimeInternetRankedStatus::Running && hasReports) return fail(error, "running match omitted its report state");
    if (record.status == RuntimeInternetRankedStatus::VoidNotStarted && (record.startedUtcMilliseconds || hasReports))
        return fail(error, "unstarted cancellation contains gameplay state");
    if (record.status == RuntimeInternetRankedStatus::VoidNoReports && (!record.startedUtcMilliseconds || hasReports))
        return fail(error, "no-report expiration contains a report or has not started");
    if (record.status == RuntimeInternetRankedStatus::VoidDisconnected && (!record.startedUtcMilliseconds || record.departingPeerId.empty()))
        return fail(error, "disconnected match has no started participant departure");
    if (record.status == RuntimeInternetRankedStatus::AwaitingReport
        && ((record.reports[0] == RuntimeInternetRankedResult::None) == (record.reports[1] == RuntimeInternetRankedResult::None)))
        return fail(error, "pending ranked record must contain exactly one report");
    if (record.status == RuntimeInternetRankedStatus::VoidMissingReport
        && ((record.reports[0] == RuntimeInternetRankedResult::None) == (record.reports[1] == RuntimeInternetRankedResult::None)))
        return fail(error, "missing-report expiration must retain exactly one report");
    if ((record.status == RuntimeInternetRankedStatus::ConflictingReports || record.status == RuntimeInternetRankedStatus::VoidConflict)
        && (!record.conflictObserved || record.reports[0] == RuntimeInternetRankedResult::None || record.reports[0] != record.reports[1]))
        return fail(error, "conflicted ranked record has no conflicting reports");
    if (!record.departingPeerId.empty() && record.departingPeerId != record.peerIds[0] && record.departingPeerId != record.peerIds[1])
        return fail(error, "departing player does not belong to the ranked match");
    if (runtimeInternetRankedScored(record.status)) {
        if (record.winnerPeerId != record.peerIds[0] && record.winnerPeerId != record.peerIds[1]) return fail(error, "ranked winner is not a participant");
        if (record.status == RuntimeInternetRankedStatus::Confirmed) {
            if (record.reports[0] == RuntimeInternetRankedResult::None || record.reports[1] == RuntimeInternetRankedResult::None
                || record.reports[0] == record.reports[1]
                || record.winnerPeerId != record.peerIds[record.reports[0] == RuntimeInternetRankedResult::Win ? 0 : 1])
                return fail(error, "confirmed winner is not supported by opposite reports");
        } else if (record.departingPeerId.empty() || record.departingPeerId == record.winnerPeerId)
            return fail(error, "forfeit winner must be the other authenticated participant");
    } else if (!record.winnerPeerId.empty()) return fail(error, "unscored ranked record has a winner");
    error.clear(); return true;
}

bool RuntimeInternetRankedMatch::authorize(std::string matchId, std::string roomId,
    std::array<std::string, 2> peerIds, const RuntimeInternetRankedPolicy& policy,
    std::uint64_t now, std::uint64_t utc, RuntimeInternetRankedMatch& output, std::string& error) {
    if (!validateRuntimeInternetRankedPolicy(policy, error)) return false;
    std::sort(peerIds.begin(), peerIds.end());
    RuntimeInternetRankedMatch candidate;
    candidate.record.matchId = std::move(matchId); candidate.record.roomId = std::move(roomId); candidate.record.peerIds = std::move(peerIds);
    candidate.record.authorizedUtcMilliseconds = candidate.record.updatedUtcMilliseconds = std::max(std::uint64_t{1}, utc);
    candidate.record.deadlineUtcMilliseconds = after(candidate.record.updatedUtcMilliseconds, policy.assignmentTimeoutMilliseconds);
    candidate.deadlineMilliseconds = after(now, policy.assignmentTimeoutMilliseconds);
    if (!validateRuntimeInternetRankedRecord(candidate.record, error)) return false;
    output = std::move(candidate); error.clear(); return true;
}

void RuntimeInternetRankedMatch::finish(RuntimeInternetRankedStatus status, std::uint64_t utc) {
    record.status = status; record.updatedUtcMilliseconds = stamp(record, utc);
    record.finalizedUtcMilliseconds = record.updatedUtcMilliseconds;
    record.deadlineUtcMilliseconds = 0; deadlineMilliseconds = 0;
}

RuntimeInternetRankedTransition RuntimeInternetRankedMatch::start(const RuntimeInternetRankedPolicy& policy, std::uint64_t now, std::uint64_t utc) {
    std::string error;
    if (!validateRuntimeInternetRankedPolicy(policy, error)) return {false, false, false, error};
    if (runtimeInternetRankedTerminal(record.status)) return rejected("ranked match is already final");
    if (expire(now, utc)) return {false, true, false, "ranked assignment expired before start"};
    if (record.status != RuntimeInternetRankedStatus::Assigned) return {true, false, true, {}};
    record.status = RuntimeInternetRankedStatus::Running;
    record.startedUtcMilliseconds = record.updatedUtcMilliseconds = stamp(record, utc);
    deadlineMilliseconds = after(now, policy.maximumMatchMilliseconds);
    record.deadlineUtcMilliseconds = after(record.updatedUtcMilliseconds, policy.maximumMatchMilliseconds);
    return {true, true, false, {}};
}

RuntimeInternetRankedTransition RuntimeInternetRankedMatch::report(std::string_view peerId, RuntimeInternetRankedResult result,
    const RuntimeInternetRankedPolicy& policy, std::uint64_t now, std::uint64_t utc) {
    std::string error;
    if (!validateRuntimeInternetRankedPolicy(policy, error)) return {false, false, false, error};
    if ((peerId != record.peerIds[0] && peerId != record.peerIds[1])
        || (result != RuntimeInternetRankedResult::Win && result != RuntimeInternetRankedResult::Loss))
        return rejected("ranked report does not name an authorized participant/result");
    if (runtimeInternetRankedTerminal(record.status)) return rejected("ranked match is already final");
    if (record.status == RuntimeInternetRankedStatus::Assigned) return rejected("ranked match has not been started by the service");
    if (expire(now, utc)) return {false, true, false, "ranked report arrived after its fixed deadline"};
    const std::size_t index = peerId == record.peerIds[0] ? 0 : 1;
    if (record.reports[index] == result) return {true, false, true, {}};
    const bool firstReport = record.status == RuntimeInternetRankedStatus::Running;
    if (record.firstReports[index] == RuntimeInternetRankedResult::None) record.firstReports[index] = result;
    record.reports[index] = result; record.updatedUtcMilliseconds = stamp(record, utc);
    if (firstReport) {
        // Reporting never extends a shorter hard match deadline, and duplicate
        // or conflicting reports cannot keep a match pending indefinitely.
        const auto proposed = after(now, policy.reportTimeoutMilliseconds);
        if (proposed < deadlineMilliseconds) {
            deadlineMilliseconds = proposed;
            record.deadlineUtcMilliseconds = after(record.updatedUtcMilliseconds, policy.reportTimeoutMilliseconds);
        }
    }
    if (record.reports[1 - index] == RuntimeInternetRankedResult::None) record.status = RuntimeInternetRankedStatus::AwaitingReport;
    else if (record.reports[0] == record.reports[1]) {
        record.status = RuntimeInternetRankedStatus::ConflictingReports; record.conflictObserved = true;
    } else {
        record.winnerPeerId = record.peerIds[record.reports[0] == RuntimeInternetRankedResult::Win ? 0 : 1];
        finish(RuntimeInternetRankedStatus::Confirmed, utc);
    }
    return {true, true, false, {}};
}

RuntimeInternetRankedTransition RuntimeInternetRankedMatch::forfeit(std::string_view peerId, std::uint64_t now, std::uint64_t utc) {
    if (peerId != record.peerIds[0] && peerId != record.peerIds[1]) return rejected("forfeit does not name an authorized participant");
    if (runtimeInternetRankedTerminal(record.status)) return rejected("ranked match is already final");
    if (expire(now, utc)) return {false, true, false, "ranked match expired before departure"};
    record.departingPeerId = peerId;
    if (record.status == RuntimeInternetRankedStatus::Assigned) finish(RuntimeInternetRankedStatus::VoidNotStarted, utc);
    else {
        record.winnerPeerId = record.peerIds[peerId == record.peerIds[0] ? 1 : 0];
        finish(RuntimeInternetRankedStatus::Forfeited, utc);
    }
    return {true, true, false, {}};
}

bool RuntimeInternetRankedMatch::disconnected(std::string_view peerId, std::uint64_t utc) {
    if (runtimeInternetRankedTerminal(record.status) || (peerId != record.peerIds[0] && peerId != record.peerIds[1])) return false;
    record.departingPeerId = peerId;
    finish(record.startedUtcMilliseconds ? RuntimeInternetRankedStatus::VoidDisconnected : RuntimeInternetRankedStatus::VoidNotStarted, utc);
    return true;
}
bool RuntimeInternetRankedMatch::expire(std::uint64_t now, std::uint64_t utc) {
    if (runtimeInternetRankedTerminal(record.status) || now < deadlineMilliseconds) return false;
    switch (record.status) {
    case RuntimeInternetRankedStatus::Assigned: finish(RuntimeInternetRankedStatus::VoidNotStarted, utc); break;
    case RuntimeInternetRankedStatus::Running: finish(RuntimeInternetRankedStatus::VoidNoReports, utc); break;
    case RuntimeInternetRankedStatus::AwaitingReport: finish(RuntimeInternetRankedStatus::VoidMissingReport, utc); break;
    case RuntimeInternetRankedStatus::ConflictingReports: finish(RuntimeInternetRankedStatus::VoidConflict, utc); break;
    default: return false;
    }
    return true;
}
bool RuntimeInternetRankedMatch::serviceRestarted(std::uint64_t utc) {
    if (runtimeInternetRankedTerminal(record.status)) return false;
    finish(RuntimeInternetRankedStatus::VoidServiceRestart, utc); return true;
}

std::string runtimeInternetRankedStatusText(const RuntimeInternetRankedRecord& record, std::string_view localPeerId) {
    switch (record.status) {
    case RuntimeInternetRankedStatus::Assigned: return "等待开局";
    case RuntimeInternetRankedStatus::Running: return "比赛进行中";
    case RuntimeInternetRankedStatus::AwaitingReport: return "等待另一方报告";
    case RuntimeInternetRankedStatus::ConflictingReports: return "报告冲突，等待更正";
    case RuntimeInternetRankedStatus::Confirmed: return record.winnerPeerId == localPeerId ? "胜利，已计分" : "失败，已计分";
    case RuntimeInternetRankedStatus::Forfeited: return record.winnerPeerId == localPeerId ? "对手主动退出，胜利" : "主动退出，判负";
    case RuntimeInternetRankedStatus::VoidMissingReport: return "作废：超时缺少报告";
    case RuntimeInternetRankedStatus::VoidConflict: return "作废：报告冲突未解决";
    case RuntimeInternetRankedStatus::VoidDisconnected: return "作废：网络中断未恢复";
    case RuntimeInternetRankedStatus::VoidNotStarted: return "作废：未完成开局";
    case RuntimeInternetRankedStatus::VoidNoReports: return "作废：比赛超时且无结果";
    case RuntimeInternetRankedStatus::VoidServiceRestart: return "作废：服务重启中断比赛";
    }
    return "未知排位状态";
}

bool encodeRuntimeInternetRankedRecord(const RuntimeInternetRankedRecord& record, std::vector<std::uint8_t>& output, std::string& error) {
    if (!validateRuntimeInternetRankedRecord(record, error)) return false;
    std::vector<std::uint8_t> bytes{'C', 'R', 'N', 'K', 0, 1};
    const auto text = [&](const std::string& value) {
        bytes.push_back(static_cast<std::uint8_t>(value.size() >> 8)); bytes.push_back(static_cast<std::uint8_t>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    };
    text(record.matchId); text(record.roomId); for (const auto& peer : record.peerIds) text(peer);
    bytes.push_back(static_cast<std::uint8_t>(record.status));
    for (const auto result : record.firstReports) bytes.push_back(static_cast<std::uint8_t>(result));
    for (const auto result : record.reports) bytes.push_back(static_cast<std::uint8_t>(result));
    bytes.push_back(record.conflictObserved ? 1 : 0);
    text(record.winnerPeerId); text(record.departingPeerId);
    for (auto value : {record.authorizedUtcMilliseconds, record.startedUtcMilliseconds, record.updatedUtcMilliseconds,
            record.deadlineUtcMilliseconds, record.finalizedUtcMilliseconds})
        for (int shift = 56; shift >= 0; shift -= 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    if (bytes.size() > runtimeInternetRankedRecordMaximumBytes) return fail(error, "ranked record exceeds its encoded limit");
    output = std::move(bytes); error.clear(); return true;
}

bool decodeRuntimeInternetRankedRecord(std::span<const std::uint8_t> bytes, RuntimeInternetRankedRecord& output, std::string& error) {
    const std::array<std::uint8_t, 6> header{'C', 'R', 'N', 'K', 0, 1};
    if (bytes.size() < header.size() || bytes.size() > runtimeInternetRankedRecordMaximumBytes
        || !std::equal(header.begin(), header.end(), bytes.begin())) return fail(error, "ranked record header or size is invalid");
    std::size_t offset = header.size();
    const auto text = [&](std::string& value) {
        if (bytes.size() - offset < 2) return false;
        const auto size = static_cast<std::size_t>(bytes[offset]) * 256 + bytes[offset + 1]; offset += 2;
        if (size > 128 || size > bytes.size() - offset) return false;
        value.assign(reinterpret_cast<const char*>(bytes.data() + offset), size); offset += size; return true;
    };
    RuntimeInternetRankedRecord record;
    if (!text(record.matchId) || !text(record.roomId) || !text(record.peerIds[0]) || !text(record.peerIds[1])
        || bytes.size() - offset < 6) return fail(error, "ranked record identities are truncated");
    record.status = static_cast<RuntimeInternetRankedStatus>(bytes[offset++]);
    for (auto& result : record.firstReports) result = static_cast<RuntimeInternetRankedResult>(bytes[offset++]);
    for (auto& result : record.reports) result = static_cast<RuntimeInternetRankedResult>(bytes[offset++]);
    if (bytes[offset] > 1) return fail(error, "ranked conflict flag is noncanonical");
    record.conflictObserved = bytes[offset++] != 0;
    if (!text(record.winnerPeerId) || !text(record.departingPeerId) || bytes.size() - offset != 40)
        return fail(error, "ranked record outcome or timestamps are truncated");
    for (auto* value : {&record.authorizedUtcMilliseconds, &record.startedUtcMilliseconds, &record.updatedUtcMilliseconds,
            &record.deadlineUtcMilliseconds, &record.finalizedUtcMilliseconds}) {
        for (int byte = 0; byte < 8; ++byte) *value = (*value << 8) | bytes[offset++];
    }
    if (!validateRuntimeInternetRankedRecord(record, error)) return false;
    output = std::move(record); error.clear(); return true;
}
