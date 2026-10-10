template<class AdapterFactory>
bool runInternetRankedTlsSmoke(SmokeContext& context, RuntimeInternetServiceHost& service,
    AdapterFactory&& makeAdapter, const std::filesystem::path& mapPath, std::uint64_t fingerprint) {
    std::string error;
    const auto fail = [&](const std::string& reason) { context.expect(false, reason + ": " + error); return false; };
    using Status = RuntimeInternetRankedStatus;
    const auto findRecord = [](const RuntimeInternetLobbyFlow& flow, const std::string& id) -> const RuntimeInternetRankedRecord* {
        for (const auto& record : flow.communityMatchHistory()) if (record.matchId == id) return &record;
        return nullptr;
    };
    const auto cleanup = [&]() {
        const auto deadline = nowMilliseconds() + 35000;
        while (nowMilliseconds() < deadline) {
            if (!pollHost(service, error)) return false;
            if (service.stats().coordinatorConnections == 0 && service.stats().rooms == 0 && service.stats().activeRelayPairs == 0) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        error = "ranked route cleanup deadline"; return false;
    };
    for (int scenario = 0; scenario < 5; ++scenario) {
        context.stage = "TLS ranked case " + std::to_string(scenario);
        RuntimeMultiplayerLockstepConfig lockstep; lockstep.inputDelayTicks = 0;
        RuntimeInternetLobbyFlow first(makeAdapter("host-peer"), lockstep), second(makeAdapter("client-peer"), lockstep);
        const std::uint64_t base = 60000 + static_cast<std::uint64_t>(scenario) * 100;
        const auto options = [&](const std::string& peer, std::uint64_t id) {
            RuntimeInternetLobbyMatchmakingOptions result;
            result.matchmakingClient.serviceId = std::string(serviceId); result.matchmakingClient.localPeerId = peer;
            result.matchmakingClient.heartbeatMilliseconds = 1000;
            result.request.requestId = id; result.request.queueId = "ranked-tls"; result.request.regionId = "loopback";
            result.request.contentFingerprint = std::string(contentFingerprint); result.request.displayName = peer;
            result.request.playerCount = 2; result.request.skillRating = 1000;
            result.activationTransactionId = id + 1;
            auto room = roomRecord(); room.roomId = "ranked-template-" + peer; room.hostPeerId = peer;
            result.hostTemplate = hostOptions(room, fingerprint);
            result.hostTemplate.connection.localPeerId = peer; result.hostTemplate.connection.transactionId = id + 2;
            result.hostTemplate.network.session.hostPeerId = peer; result.hostTemplate.network.session.hostDisplayName = peer;
            result.hostTemplate.lobby.hostMember.peerId = peer; result.hostTemplate.lobby.hostMember.displayName = peer;
            result.hostTemplate.lobby.slotCount = 2; result.hostTemplate.lobby.computerSlotIndices.clear();
            result.joinTemplate = joinOptions(peer, peer, RuntimeInternetRole::Client, room, id + 3, 42001);
            return result;
        };
        if (!first.beginMatchmaking(options("host-peer", base), nowMilliseconds(), error)
            || !second.beginMatchmaking(options("client-peer", base + 10), nowMilliseconds(), error)) return fail("matchmaking");
        if (!first.requestCommunityRegistration(base + 20, "RankedHost", error)
            || !second.requestCommunityRegistration(base + 21, "RankedClient", error)) return fail("ranked profiles");
        std::array<RuntimeInternetLobbyFlow*, 2> flows{&first, &second};
        if (!pumpFlows(service, flows, [&] {
            return first.mode() == RuntimeInternetLobbyFlowMode::Lobby && second.mode() == RuntimeInternetLobbyFlowMode::Lobby
                && first.lobbyFlow()->lobby().setup().members.size() == 2 && second.lobbyFlow()->lobby().setup().members.size() == 2
                && first.communitySnapshot().registeredProfile && second.communitySnapshot().registeredProfile;
        }, context, "ranked assignment and admission")) return false;
        const std::string matchId = first.matchAssignment()->matchId;
        auto* host = first.matchAssignment()->hostPeerId == "host-peer" ? &first : &second;
        // Even a real authenticated player cannot submit a win before SealRoom.
        if (!first.requestCommunityQuickMatchReport(base + 22, matchId, "client-peer", RuntimeInternetCommunityQuickMatchResult::Win, error))
            return fail("prestart report send");
        if (!pumpFlows(service, flows, [&] { const auto* record = findRecord(first, matchId); return record && record->status == Status::Assigned; },
                context, "prestart report rejected")) return false;
        if (!first.lobbyFlow()->verifyLocalMap(mapPath, error) || !second.lobbyFlow()->verifyLocalMap(mapPath, error)
            || !first.lobbyFlow()->setReady(true).succeeded || !second.lobbyFlow()->setReady(true).succeeded) return fail("ranked ready");
        if (!pumpFlows(service, flows, [&] { std::string why; return host->lobbyFlow()->canStart(why); }, context, "ranked ready consensus")) return false;
        if (!host->lobbyFlow()->startMatch(1).succeeded) return fail("ranked start");
        if (!pumpFlows(service, flows, [&] { const auto* record = findRecord(first, matchId);
            return first.mode() == RuntimeInternetLobbyFlowMode::Running && second.mode() == RuntimeInternetLobbyFlowMode::Running
                && record && record->status == Status::Running;
        }, context, "trusted seal starts ranked record")) return false;
        const auto priorWins = second.communitySnapshot().profile.wins;
        const auto priorLosses = second.communitySnapshot().profile.losses;
        auto bridge = first.takeRunningBridge(error);
        if (!bridge) return fail("ranked running handoff");
        bool submitted = false;
        if (scenario <= 2) {
            if (!bridge->reportRankedMatchResult(true, submitted, error) || !submitted) return fail("running result report");
            if (scenario != 1 && !second.requestCommunityQuickMatchReport(base + 23, matchId, "host-peer",
                    scenario == 0 ? RuntimeInternetCommunityQuickMatchResult::Loss : RuntimeInternetCommunityQuickMatchResult::Win, error))
                return fail("other report");
        } else if (scenario == 3) {
            // The production exit waits for the durable receipt. Poll the
            // service on one temporary thread while that synchronous exit runs.
            std::atomic<bool> done{false}; std::string serviceError;
            std::thread runner([&] {
                while (!done.load()) {
                    if (!pollHost(service, serviceError)) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            });
            const bool abandoned = bridge->reportRankedMatchAbandon(submitted, error);
            done.store(true); runner.join();
            if (!abandoned || !submitted || !serviceError.empty()) return fail("authenticated departure receipt " + serviceError);
        }
        if (scenario != 4) {
            const Status expected = scenario == 0 ? Status::Confirmed : scenario == 1 ? Status::VoidMissingReport
                : scenario == 2 ? Status::VoidConflict : Status::Forfeited;
            std::array<RuntimeInternetLobbyFlow*, 1> observer{&second};
            if (!pumpFlows(service, observer, [&] { const auto* record = findRecord(second, matchId); return record && record->status == expected; },
                    context, "durable final record", 5000)) return false;
            const auto& profile = second.communitySnapshot().profile;
            if (profile.wins != priorWins + (scenario == 3 ? 1 : 0)
                || profile.losses != priorLosses + (scenario == 0 ? 1 : 0)) return fail("ranked scoring policy");
        }
        bridge.reset(); second.close(); first.close();
        if (!cleanup()) return fail("final disconnection cleanup");
        RuntimeInternetLobbyFlow history(makeAdapter("host-peer"), lockstep);
        RuntimeInternetLobbyQueryOptions query; query.connection = queryConfig("host-peer", base + 30);
        if (!history.beginRoomQuery(query, nowMilliseconds(), error)
            || !history.requestCommunityLogin(base + 31, error)) return fail("history login");
        std::array<RuntimeInternetLobbyFlow*, 1> historyFlows{&history};
        if (!pumpFlows(service, historyFlows, [&] { const auto* record = findRecord(history, matchId);
            return record && runtimeInternetRankedTerminal(record->status);
        }, context, "history after reconnect")) return false;
        const auto* recorded = findRecord(history, matchId);
        if (scenario == 4 && recorded->status != Status::VoidDisconnected) return fail("network failure must finish void after grace");
        if (scenario == 3 && (recorded->status != Status::Forfeited || recorded->departingPeerId != "host-peer"
                || recorded->winnerPeerId != "client-peer")) return fail("departure must remain final after transport closes");
        history.close(); if (!cleanup()) return fail("history connection cleanup");
        std::cout << "TLS ranked case " << scenario << ": start authority, authenticated final state, score policy and reconnect history passed.\n";
    }
    std::cout << "TLS ranked finalization: normal, missing, conflict, explicit departure receipt and network grace passed.\n";
    return true;
}
