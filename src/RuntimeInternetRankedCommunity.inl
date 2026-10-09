// Members of RuntimeInternetCommunityService::Impl.
bool queueMatchHistory(std::string_view peerId, std::uint64_t transactionId,
    std::vector<RuntimeInternetCommunityServiceAction>& actions, std::string& error) const {
    RuntimeInternetCommunityResponse response;
    response.kind = RuntimeInternetCommunityResponseKind::MatchHistory;
    response.transactionId = transactionId;
    response.serviceId = response.senderPeerId = config.serviceId;
    response.targetPeerId = peerId;
    response.status = "Ranked match history updated.";
    for (auto it = authorizedMatchOrder.rbegin(); it != authorizedMatchOrder.rend(); ++it) {
        const auto found = authorizedMatches.find(*it);
        if (found == authorizedMatches.end()) continue;
        const auto& record = found->second.record;
        if (record.peerIds[0] != peerId && record.peerIds[1] != peerId) continue;
        response.matchHistory.push_back(record);
        if (response.matchHistory.size() == runtimeInternetCommunityMaximumMatchHistory) break;
    }
    return queueResponse(std::move(response), actions, error);
}

bool queueRankedHistories(const std::set<std::string>& peers, std::uint64_t transactionId,
    std::vector<RuntimeInternetCommunityServiceAction>& actions, std::string& error) const {
    for (const auto& peer : peers)
        if (online.contains(peer) && accounts.contains(peer) && !queueMatchHistory(peer, transactionId, actions, error)) return false;
    return true;
}

bool commitRankedMatch(const std::string& matchId, RuntimeInternetRankedMatch candidate, std::uint64_t transactionId,
    std::vector<RuntimeInternetCommunityServiceAction>& actions, std::string& error) {
    const auto found = authorizedMatches.find(matchId);
    if (found == authorizedMatches.end() || !validateRuntimeInternetRankedRecord(candidate.record, error)
        || candidate.record.matchId != matchId || candidate.record.peerIds != found->second.record.peerIds
        || candidate.record.roomId != found->second.record.roomId || runtimeInternetRankedTerminal(found->second.record.status))
        return fail(error, "ranked transition does not match unresolved service authority");
    const auto oldMatch = found->second;
    const bool score = runtimeInternetRankedScored(candidate.record.status);
    std::string winnerId, loserId, winnerClanId, loserClanId;
    std::optional<RuntimeInternetCommunityProfile> oldWinner, oldLoser;
    std::optional<RuntimeInternetCommunityClan> oldWinnerClan, oldLoserClan;
    std::set<std::string> recipients(candidate.record.peerIds.begin(), candidate.record.peerIds.end());
    if (score) {
        winnerId = candidate.record.winnerPeerId;
        loserId = candidate.record.peerIds[winnerId == candidate.record.peerIds[0] ? 1 : 0];
        if (!accounts.contains(winnerId) || !accounts.contains(loserId)) return fail(error, "ranked players must own registered profiles");
        auto& winner = accounts.at(winnerId); auto& loser = accounts.at(loserId);
        oldWinner = winner.profile; oldLoser = loser.profile;
        winnerClanId = winner.clanId; loserClanId = loser.clanId;
        if (winner.profile.wins < (std::numeric_limits<std::uint32_t>::max)()) ++winner.profile.wins;
        if (loser.profile.losses < (std::numeric_limits<std::uint32_t>::max)()) ++loser.profile.losses;
        winner.profile.points = static_cast<std::int32_t>(std::min(static_cast<std::int64_t>(winner.profile.points) + 32,
            static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::max)())));
        loser.profile.points = std::max(0, loser.profile.points - 16);
        if (!winnerClanId.empty() && !loserClanId.empty() && winnerClanId != loserClanId
            && clans.contains(winnerClanId) && clans.contains(loserClanId)) {
            auto& winnerClan = clans.at(winnerClanId); auto& loserClan = clans.at(loserClanId);
            oldWinnerClan = winnerClan.projection; oldLoserClan = loserClan.projection;
            if (winnerClan.projection.wins < (std::numeric_limits<std::uint32_t>::max)()) ++winnerClan.projection.wins;
            if (loserClan.projection.losses < (std::numeric_limits<std::uint32_t>::max)()) ++loserClan.projection.losses;
            winnerClan.projection.points = static_cast<std::int32_t>(std::min(static_cast<std::int64_t>(winnerClan.projection.points) + 32,
                static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::max)())));
            loserClan.projection.points = std::max(0, loserClan.projection.points - 16);
            recipients.insert(winnerClan.members.begin(), winnerClan.members.end());
            recipients.insert(loserClan.members.begin(), loserClan.members.end());
        }
    }
    found->second = std::move(candidate);
    // Final state and every affected score are committed together. A failed
    // write leaves the previous in-memory and on-disk authority available.
    if (!saveState(error)) {
        found->second = oldMatch;
        if (oldWinner) accounts.at(winnerId).profile = *oldWinner;
        if (oldLoser) accounts.at(loserId).profile = *oldLoser;
        if (oldWinnerClan) clans.at(winnerClanId).projection = *oldWinnerClan;
        if (oldLoserClan) clans.at(loserClanId).projection = *oldLoserClan;
        return false;
    }
    if (score && !queueSnapshotsForPeers(recipients, transactionId,
            "Quick Match finalized; player and Clan standings updated.", actions, error)) return false;
    return queueRankedHistories(recipients, transactionId, actions, error);
}

bool handleQuickMatchReport(const RuntimeInternetCommunityRequest& request,
    std::vector<RuntimeInternetCommunityServiceAction>& actions, std::string& error) {
    const auto found = authorizedMatches.find(request.matchId);
    if (found == authorizedMatches.end() || request.senderPeerId == request.target
        || (found->second.record.peerIds[0] != request.senderPeerId && found->second.record.peerIds[1] != request.senderPeerId)
        || (found->second.record.peerIds[0] != request.target && found->second.record.peerIds[1] != request.target))
        return queueError(request.senderPeerId, request.transactionId,
            "The ranked result does not match a service-authorized Quick Match.", actions, error);
    if (!accounts.contains(request.senderPeerId) || !accounts.contains(request.target))
        return queueError(request.senderPeerId, request.transactionId,
            "Both Quick Match players must own registered profiles.", actions, error);
    const bool abandon = request.operation == RuntimeInternetCommunityOperation::AbandonQuickMatch;
    if (abandon && request.quickMatchResult != RuntimeInternetCommunityQuickMatchResult::None)
        return queueError(request.senderPeerId, request.transactionId, "Departure cannot claim a game result.", actions, error);
    auto candidate = found->second;
    const auto result = abandon ? candidate.forfeit(request.senderPeerId, now(), utc())
        : candidate.report(request.senderPeerId, static_cast<RuntimeInternetRankedResult>(request.quickMatchResult), config.rankedPolicy, now(), utc());
    if (result.changed && !commitRankedMatch(request.matchId, std::move(candidate), request.transactionId, actions, error)) return false;
    if (!result.accepted) {
        if (!result.changed && !queueMatchHistory(request.senderPeerId, request.transactionId, actions, error)) return false;
        return queueError(request.senderPeerId, request.transactionId, result.reason, actions, error);
    }
    if (found->second.record.status == RuntimeInternetRankedStatus::ConflictingReports)
        return queueError(request.senderPeerId, request.transactionId,
            "Quick Match reports conflict; correct before the fixed deadline or the match will be voided.", actions, error);
    if (!result.changed && !queueMatchHistory(request.senderPeerId, request.transactionId, actions, error)) return false;
    if (!runtimeInternetRankedScored(found->second.record.status))
        return queueSnapshot(request.senderPeerId, request.transactionId,
            runtimeInternetRankedStatusText(found->second.record, request.senderPeerId), actions, error);
    return true;
}

bool loadRankedRecord(const std::vector<std::string>& fields, std::string& error) {
    if (fields.size() != 2 || fields[1].size() > runtimeInternetRankedRecordMaximumBytes * 2
        || authorizedMatches.size() >= config.maximumAuthorizedQuickMatches) return fail(error, "ranked state record exceeds its bounds");
    const auto decoded = hexDecode(fields[1]);
    RuntimeInternetRankedMatch match;
    if (!decoded || !decodeRuntimeInternetRankedRecord(std::span(reinterpret_cast<const std::uint8_t*>(decoded->data()), decoded->size()), match.record, error))
        return fail(error, "ranked state record is malformed");
    if (std::any_of(authorizedMatches.begin(), authorizedMatches.end(), [&](const auto& entry) {
            return entry.second.record.roomId == match.record.roomId;
        })) return fail(error, "ranked state contains a duplicate room id");
    if (!authorizedMatches.emplace(match.record.matchId, match).second) return fail(error, "ranked state contains a duplicate match id");
    authorizedMatchOrder.push_back(match.record.matchId); return true;
}

bool writeRankedRecords(std::ostream& output, std::string& error) const {
    for (const auto& id : authorizedMatchOrder) {
        const auto found = authorizedMatches.find(id);
        if (found == authorizedMatches.end()) return fail(error, "ranked audit order contains a missing match");
        std::vector<std::uint8_t> bytes;
        if (!encodeRuntimeInternetRankedRecord(found->second.record, bytes, error)) return false;
        output << "R\t" << hexEncode(std::string(bytes.begin(), bytes.end())) << '\n';
    }
    return output.good();
}
