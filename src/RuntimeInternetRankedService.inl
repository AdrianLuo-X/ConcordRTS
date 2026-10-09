bool RuntimeInternetCommunityService::authorizeQuickMatch(std::string matchId,
    std::span<const std::string> peerIds, std::string& error, std::string roomId) {
    if (!impl_ || !impl_->opened) return fail(error, "community service is not open");
    if (peerIds.size() != 2) return fail(error, "ranked authorization requires exactly two players");
    if (roomId.empty()) roomId = matchId;
    RuntimeInternetRankedMatch candidate;
    if (!RuntimeInternetRankedMatch::authorize(matchId, std::move(roomId), {peerIds[0], peerIds[1]},
            impl_->config.rankedPolicy, impl_->now(), impl_->utc(), candidate, error)) return false;
    const auto existing = impl_->authorizedMatches.find(matchId);
    if (existing != impl_->authorizedMatches.end()) {
        if (existing->second.record.peerIds != candidate.record.peerIds || existing->second.record.roomId != candidate.record.roomId)
            return fail(error, "ranked match id was reused for another room or players");
        error.clear(); return true;
    }
    if (std::any_of(impl_->authorizedMatches.begin(), impl_->authorizedMatches.end(), [&](const auto& entry) {
        return entry.second.record.roomId == candidate.record.roomId;
    })) return fail(error, "ranked room id was reused for another match");
    const auto oldOrder = impl_->authorizedMatchOrder;
    std::optional<std::pair<std::string, RuntimeInternetRankedMatch>> pruned;
    if (impl_->authorizedMatches.size() >= impl_->config.maximumAuthorizedQuickMatches) {
        const auto oldest = std::find_if(impl_->authorizedMatchOrder.begin(), impl_->authorizedMatchOrder.end(), [&](const auto& id) {
            return runtimeInternetRankedTerminal(impl_->authorizedMatches.at(id).record.status);
        });
        if (oldest == impl_->authorizedMatchOrder.end()) return fail(error, "ranked capacity is full of unresolved matches");
        pruned = {*oldest, impl_->authorizedMatches.at(*oldest)};
        impl_->authorizedMatches.erase(*oldest); impl_->authorizedMatchOrder.erase(oldest);
    }
    impl_->authorizedMatches.emplace(matchId, std::move(candidate)); impl_->authorizedMatchOrder.push_back(matchId);
    if (!impl_->saveState(error)) {
        impl_->authorizedMatches.erase(matchId); impl_->authorizedMatchOrder = oldOrder;
        if (pruned) impl_->authorizedMatches.emplace(std::move(pruned->first), std::move(pruned->second));
        return false;
    }
    error.clear(); return true;
}

bool RuntimeInternetCommunityService::poll(std::vector<RuntimeInternetCommunityServiceAction>& actions, std::string& error) {
    if (!impl_ || !impl_->opened) return fail(error, "community service is not open");
    const auto now = impl_->now(), utc = impl_->utc();
    std::vector<std::pair<std::string, RuntimeInternetRankedMatch>> changed;
    std::set<std::string> peers;
    for (auto& [id, match] : impl_->authorizedMatches) {
        const auto old = match;
        if (!match.expire(now, utc)) continue;
        changed.emplace_back(id, old); peers.insert(match.record.peerIds.begin(), match.record.peerIds.end());
    }
    if (!changed.empty() && !impl_->saveState(error)) {
        for (auto& [id, previous] : changed) impl_->authorizedMatches.at(id) = std::move(previous);
        return false;
    }
    if (!impl_->queueRankedHistories(peers, std::max<std::uint64_t>(1, now), actions, error)) return false;
    error.clear(); return true;
}

bool RuntimeInternetCommunityService::rankedRoomStarted(std::string_view roomId,
    std::vector<RuntimeInternetCommunityServiceAction>& actions, std::string& error) {
    if (!poll(actions, error)) return false;
    for (const auto& [id, match] : impl_->authorizedMatches) {
        if (match.record.roomId != roomId || runtimeInternetRankedTerminal(match.record.status)) continue;
        auto candidate = match;
        const auto now = impl_->now();
        const auto started = candidate.start(impl_->config.rankedPolicy, now, impl_->utc());
        if (started.changed && !impl_->commitRankedMatch(id, std::move(candidate), std::max<std::uint64_t>(1, now), actions, error)) return false;
        break;
    }
    error.clear(); return true;
}

bool RuntimeInternetCommunityService::authenticatedPeerDisconnected(std::string_view serviceId, std::string_view peerId,
    bool unexpected, std::vector<RuntimeInternetCommunityServiceAction>& actions, std::string& error) {
    if (!impl_ || !impl_->opened || serviceId != impl_->config.serviceId || !validToken(peerId, maximumIdentityBytes))
        return fail(error, "community disconnection identity is invalid");
    const std::string peer(peerId);
    const bool wasOnline = impl_->online.contains(peer);
    const auto account = impl_->accounts.find(peer);
    std::optional<Impl::Account> oldAccount;
    if (account != impl_->accounts.end()) oldAccount = account->second;
    std::optional<std::deque<std::uint64_t>> oldReplay;
    const auto replay = impl_->replayTransactions.find(peer);
    if (replay != impl_->replayTransactions.end()) oldReplay = replay->second;
    impl_->online.erase(peer); impl_->replayTransactions.erase(peer);
    const auto now = impl_->now(), utc = impl_->utc();
    std::vector<std::pair<std::string, RuntimeInternetRankedMatch>> changed;
    std::set<std::string> affected;
    bool rankedInterruption = false;
    for (auto& [id, match] : impl_->authorizedMatches) {
        if (match.record.peerIds[0] != peer && match.record.peerIds[1] != peer) continue;
        const auto previous = match;
        if (!match.expire(now, utc) && !match.disconnected(peer, utc)) continue;
        changed.emplace_back(id, previous);
        rankedInterruption |= match.record.status == RuntimeInternetRankedStatus::VoidDisconnected;
        affected.insert(match.record.peerIds.begin(), match.record.peerIds.end());
    }
    if (oldAccount) {
        account->second.profile.online = false; account->second.profile.lastSeenMilliseconds = now;
        if (wasOnline && (unexpected || rankedInterruption) && account->second.profile.disconnects < (std::numeric_limits<std::uint32_t>::max)())
            ++account->second.profile.disconnects;
        impl_->leaveChannel(account->second);
    }
    if ((oldAccount || !changed.empty()) && !impl_->saveState(error)) {
        for (auto& [id, previous] : changed) impl_->authorizedMatches.at(id) = std::move(previous);
        if (oldAccount) {
            account->second = *oldAccount;
            if (wasOnline && !oldAccount->channelId.empty()) impl_->channels[oldAccount->channelId].members.insert(peer);
        }
        if (wasOnline) impl_->online.insert(peer);
        if (oldReplay) impl_->replayTransactions.emplace(peer, std::move(*oldReplay));
        return false;
    }
    const auto transactionId = std::max<std::uint64_t>(1, now);
    if (!impl_->queueRankedHistories(affected, transactionId, actions, error)) return false;
    if (oldAccount) {
        if (!impl_->queueBuddyPresenceSnapshots(peer, transactionId, actions, error)) return false;
        if (!account->second.clanId.empty()) {
            const auto clan = impl_->clans.find(account->second.clanId);
            if (clan != impl_->clans.end() && !impl_->queueSnapshotsForPeers(clan->second.members,
                    transactionId, "Clan member presence updated.", actions, error)) return false;
        }
    }
    error.clear(); return true;
}
