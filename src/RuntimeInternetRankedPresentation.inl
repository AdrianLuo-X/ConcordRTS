namespace {
std::string rankedHistoryUtc(std::uint64_t milliseconds) {
    if (milliseconds == 0) return "—";
    if (milliseconds > 253402300799000ULL) return std::to_string(milliseconds) + " ms UTC";
    const std::time_t seconds = static_cast<std::time_t>(milliseconds / 1000);
    std::tm time{};
#if defined(_WIN32)
    if (gmtime_s(&time, &seconds) != 0) return "时间不可用";
#else
    if (gmtime_r(&seconds, &time) == nullptr) return "时间不可用";
#endif
    std::ostringstream output;
    output << std::put_time(&time, "%Y-%m-%d %H:%M:%S") << " UTC";
    return output.str();
}
const char* rankedHistoryReport(RuntimeInternetRankedResult result) {
    switch (result) {
    case RuntimeInternetRankedResult::Win: return "胜";
    case RuntimeInternetRankedResult::Loss: return "负";
    default: return "未提交";
    }
}
}

std::vector<std::string> projectRuntimeInternetRankedHistoryRows(
    std::span<const RuntimeInternetRankedRecord> records, std::string_view localPeerId) {
    std::vector<std::string> rows;
    for (const auto& record : records) {
        const auto& opponent = record.peerIds[record.peerIds[0] == localPeerId ? 1 : 0];
        rows.push_back(rankedHistoryUtc(record.authorizedUtcMilliseconds) + " | " + opponent + " | "
            + runtimeInternetRankedStatusText(record, localPeerId));
    }
    return rows;
}

std::string projectRuntimeInternetRankedHistoryDetail(const RuntimeInternetRankedRecord& record, std::string_view localPeerId) {
    std::string detail = runtimeInternetRankedStatusText(record, localPeerId);
    detail += runtimeInternetRankedScored(record.status) ? "（已计分）" : "（未计分）";
    detail += "\r\n对局：" + record.matchId + "   房间：" + record.roomId;
    detail += "\r\n开始：" + rankedHistoryUtc(record.startedUtcMilliseconds);
    detail += runtimeInternetRankedTerminal(record.status)
        ? "   结案：" + rankedHistoryUtc(record.finalizedUtcMilliseconds)
        : "   截止：" + rankedHistoryUtc(record.deadlineUtcMilliseconds);
    for (std::size_t i = 0; i < record.peerIds.size(); ++i)
        detail += "\r\n" + record.peerIds[i] + "：首次 " + rankedHistoryReport(record.firstReports[i])
            + "，最后 " + rankedHistoryReport(record.reports[i]);
    if (record.conflictObserved) detail += "   曾出现报告冲突";
    if (!record.departingPeerId.empty()) detail += "\r\n退出／断线方：" + record.departingPeerId;
    return detail;
}
