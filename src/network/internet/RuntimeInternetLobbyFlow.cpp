#include "network/internet/RuntimeInternetLobbyFlow.h"
#include "network/internet/RuntimeInternetCoordinator.h"
#include "network/lobby/RuntimeRoomPassword.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <exception>
#include <limits>
#include <optional>
#include <thread>
#include <utility>

namespace {

bool failWith(std::string& errorMessage, std::string reason) {
    errorMessage = std::move(reason);
    return false;
}

std::string contentFingerprintText(std::uint64_t fingerprint) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(16U, '0');
    for (std::size_t index = 0; index < result.size(); ++index) {
        const unsigned int shift = static_cast<unsigned int>(
            (result.size() - index - 1U) * 4U
        );
        result[index] = digits[(fingerprint >> shift) & 0x0fU];
    }
    return result;
}

std::string matchmakingRoomName(std::string_view matchId) {
    return "Quick Match " + std::string(matchId);
}

bool roleMatchesParticipation(
    RuntimeInternetRole role,
    RuntimeMultiplayerParticipationRole participation
) {
    return (role == RuntimeInternetRole::Client
            && participation == RuntimeMultiplayerParticipationRole::Combatant)
        || (role == RuntimeInternetRole::Spectator
            && participation == RuntimeMultiplayerParticipationRole::Spectator);
}

std::optional<std::size_t> authoredCombatantCapacity(
    const RuntimeMultiplayerLobbyHostCreateOptions& options
) {
    if (options.slotCount == 0U) return std::nullopt;
    std::vector<std::size_t> computerSlots = options.computerSlotIndices;
    std::sort(computerSlots.begin(), computerSlots.end());
    if ((!computerSlots.empty()
            && computerSlots.back() >= options.slotCount)
        || std::adjacent_find(
            computerSlots.begin(),
            computerSlots.end()
        ) != computerSlots.end()) {
        return std::nullopt;
    }
    return options.slotCount - computerSlots.size();
}

bool sameInternetAction(
    const RuntimeInternetAction& left,
    const RuntimeInternetAction& right
) {
    return left.kind == right.kind && left.endpoint == right.endpoint
        && left.packet.type == right.packet.type
        && left.packet.payload == right.packet.payload;
}

std::optional<std::uint64_t> messageConnectionId(
    const RuntimeInternetControlMessage& message
) {
    if (const auto* body = std::get_if<RuntimeInternetPeerCandidatesBody>(
            &message.body
        )) {
        return body->connectionId;
    }
    if (const auto* body = std::get_if<RuntimeInternetRelayLeaseBody>(
            &message.body
        )) {
        return body->connectionId;
    }
    if (const auto* body = std::get_if<RuntimeInternetRelayBindBody>(
            &message.body
        )) {
        return body->connectionId;
    }
    if (const auto* body = std::get_if<RuntimeInternetRelayBoundBody>(
            &message.body
        )) {
        return body->connectionId;
    }
    return std::nullopt;
}

} // namespace

struct RuntimeInternetLobbyFlow::Impl {
    struct RunningOrderedTransport;

    struct CompletedRoomQuery {
        std::string serviceId;
        std::string peerId;
        std::uint64_t transactionId = 0;
    };

    static constexpr std::size_t maximumCompletedRoomQueries = 32U;
    static constexpr std::uint64_t reconnectGraceMilliseconds = 30000U;

    struct RouteContext {
        RuntimeInternetConnection connection;
        RuntimeInternetTcpTunnelProxy proxy;
        std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> pendingStream;
        RuntimeInternetTcpTunnelProxyConfig proxyConfig;
        std::uint64_t connectionId = 0;
        std::string remotePeerId;
        RuntimeInternetRole remoteRole = RuntimeInternetRole::Client;
        bool tcpOpenRequested = false;
        bool proxyStarted = false;
        bool tunnelEventSent = false;
        bool gameplayReported = false;
        bool spectatorShutdownPending = false;
        std::uint64_t recoveryDeadline = 0;
        std::uint64_t nextResumeAttempt = 0;
        bool awaitingResume = false;
        unsigned resumeAttempts = 0;
        bool recoverySawLanPause = false;
        RuntimeNetworkPacket resumeRequest;
        std::optional<RuntimeInternetPeerCandidatesBody> resumedOffer;
    };

    explicit Impl(
        std::unique_ptr<RuntimeInternetServiceAdapter> adapter,
        RuntimeMultiplayerLockstepConfig lockstepConfig
    ) : service(std::move(adapter)), lobby(std::move(lockstepConfig)) {
        if (!service) {
            communityInitializationError =
                "Internet authenticated service adapter is missing";
            return;
        }
        RuntimeInternetCommunityClientConfig communityConfig;
        communityConfig.serviceId = std::string(
            service->authenticatedServiceId()
        );
        communityConfig.localPeerId = std::string(
            service->authenticatedPeerId()
        );
        (void)community.open(
            std::move(communityConfig),
            communityInitializationError
        );
    }

    std::unique_ptr<RuntimeInternetServiceAdapter> service;
    RuntimeMultiplayerLobbyFlow lobby;
    RuntimeInternetLobbyFlowMode mode = RuntimeInternetLobbyFlowMode::Idle;
    RuntimeInternetConnection directoryConnection;
    RuntimeInternetConnectionConfig activeConfig;
    std::deque<CompletedRoomQuery> completedRoomQueries;
    RuntimeInternetConnectionConfig hostRouteConfig;
    RuntimeInternetTcpTunnelProxyConfig hostProxyConfig;
    RuntimeInternetLobbyJoinOptions joinOptions;
    RuntimeInternetRoomRecord hostedRoom;
    std::vector<RuntimeInternetRoomRecord> roomCache;
    std::vector<std::unique_ptr<RouteContext>> hostRoutes;
    std::unique_ptr<RouteContext> joinRoute;
    bool roomRegistered = false;
    std::vector<std::uint8_t> hostRegistrationProof;
    RuntimeInternetMatchmakingClient matchmaking;
    RuntimeInternetCommunityClient community;
    std::string communityInitializationError;
    std::optional<RuntimeInternetLobbyMatchmakingOptions> matchmakingOptions;
    std::vector<std::uint8_t> joinAdmissionProof;
    bool preserveMatchmakingTransition = false;
    bool matchTransitionPending = false;
    bool matchActivationRequested = false;
    bool hostMatchStartPending = false;
    bool matchStartPublished = false;
    RuntimeMultiplayerRuntimeBridge* detachedRunningBridge = nullptr;
    std::uint64_t lastNowMilliseconds = 0;
    std::uint64_t runningClockBaseMilliseconds = 0;
    std::chrono::steady_clock::time_point runningClockBaseSteady{};
    bool runningClockInitialized = false;
    bool coordinatorReconnecting = false;
    bool coordinatorAttemptPending = false;
    std::uint64_t coordinatorRecoveryDeadline = 0;
    std::uint64_t nextCoordinatorAttempt = 0;
    std::deque<std::uint64_t> retiredRouteIds;

    RuntimeMultiplayerRuntimeBridge* activeRuntimeBridge() {
        return detachedRunningBridge ? detachedRunningBridge : lobby.bridge();
    }

    const RuntimeMultiplayerRuntimeBridge* activeRuntimeBridge() const {
        return detachedRunningBridge ? detachedRunningBridge : lobby.bridge();
    }

    void clearMatchmaking() {
        matchmaking.close();
        matchmakingOptions.reset();
        runtimeRoomPasswordSecureClear(joinAdmissionProof);
        preserveMatchmakingTransition = false;
        matchTransitionPending = false;
        matchActivationRequested = false;
    }

    void resetActive(bool keepRooms) {
        directoryConnection.close();
        for (auto& route : hostRoutes) {
            route->proxy.close();
            route->connection.close();
            if (route->pendingStream) route->pendingStream->close();
        }
        hostRoutes.clear();
        if (joinRoute) {
            joinRoute->proxy.close();
            joinRoute->connection.close();
            if (joinRoute->pendingStream) joinRoute->pendingStream->close();
        }
        joinRoute.reset();
        lobby.close();
        activeConfig = {};
        hostRouteConfig = {};
        hostProxyConfig = {};
        joinOptions = {};
        hostedRoom = {};
        runtimeRoomPasswordSecureClear(hostRegistrationProof);
        if (!preserveMatchmakingTransition) {
            runtimeRoomPasswordSecureClear(joinAdmissionProof);
        }
        roomRegistered = false;
        hostMatchStartPending = false;
        matchStartPublished = false;
        detachedRunningBridge = nullptr;
        runningClockInitialized = false;
        coordinatorReconnecting = false;
        coordinatorAttemptPending = false;
        coordinatorRecoveryDeadline = 0;
        retiredRouteIds.clear();
        if (!keepRooms) roomCache.clear();
        mode = RuntimeInternetLobbyFlowMode::Idle;
    }

    bool validateAdapter(
        const RuntimeInternetConnectionConfig& config,
        std::string& errorMessage
    ) const {
        if (!service) {
            return failWith(
                errorMessage,
                "Internet authenticated service adapter is missing"
            );
        }
        if (service->authenticatedServiceId() != config.serviceId
            || service->authenticatedPeerId() != config.localPeerId) {
            return failWith(
                errorMessage,
                "Internet service adapter identity does not match the local connection"
            );
        }
        return true;
    }

    bool sendCommunityPacket(
        RuntimeNetworkPacket packet,
        std::string& errorMessage
    ) {
        if (!communityInitializationError.empty()) {
            return failWith(errorMessage, communityInitializationError);
        }
        if (!service || !community.isOpen()) {
            return failWith(
                errorMessage,
                "Internet community client is unavailable"
            );
        }
        return service->sendCoordinator(packet, errorMessage);
    }

    RouteContext* findRoute(std::uint64_t connectionId) {
        if (joinRoute && joinRoute->connectionId == connectionId) {
            return joinRoute.get();
        }
        const auto found = std::find_if(
            hostRoutes.begin(),
            hostRoutes.end(),
            [&](const std::unique_ptr<RouteContext>& route) {
                return route->connectionId == connectionId;
            }
        );
        return found == hostRoutes.end() ? nullptr : found->get();
    }

    const RouteContext* findRoute(std::uint64_t connectionId) const {
        return const_cast<Impl*>(this)->findRoute(connectionId);
    }

    RouteContext* findRouteByPeer(std::string_view peerId) {
        if (joinRoute && activeConfig.localPeerId == peerId) {
            return joinRoute.get();
        }
        const auto found = std::find_if(
            hostRoutes.begin(),
            hostRoutes.end(),
            [&](const std::unique_ptr<RouteContext>& route) {
                return route->remotePeerId == peerId;
            }
        );
        return found == hostRoutes.end() ? nullptr : found->get();
    }

    void removeHostRoute(std::string_view peerId) {
        const auto found = std::find_if(
            hostRoutes.begin(),
            hostRoutes.end(),
            [peerId](const std::unique_ptr<RouteContext>& route) {
                return route->remotePeerId == peerId;
            }
        );
        if (found == hostRoutes.end()) return;
        (*found)->proxy.close();
        (*found)->connection.close();
        if ((*found)->pendingStream) (*found)->pendingStream->close();
        hostRoutes.erase(found);
    }

    bool hostRouteIsRunningMember(const RouteContext& route) const {
        if (activeConfig.role != RuntimeInternetRole::Host
            || mode != RuntimeInternetLobbyFlowMode::Running
            || !route.gameplayReported) {
            return false;
        }
        const RuntimeMultiplayerRuntimeBridge* bridge = activeRuntimeBridge();
        if (bridge == nullptr) return false;
        const RuntimeMultiplayerSessionState& session =
            bridge->orderedTransportPumpSessionView();
        return session.active()
            && std::any_of(
                session.members().begin(),
                session.members().end(),
                [&](const RuntimeMultiplayerRoomMember& member) {
                    return member.peerId == route.remotePeerId;
                }
            );
    }

    bool runningSpectatorRoute(const RouteContext& route) const {
        return activeConfig.role == RuntimeInternetRole::Host
            && mode == RuntimeInternetLobbyFlowMode::Running
            && route.gameplayReported
            && route.remoteRole == RuntimeInternetRole::Spectator;
    }

    void beginSpectatorShutdown(RouteContext& route) {
        if (route.spectatorShutdownPending) return;
        route.spectatorShutdownPending = true;
        // Closing the loopback proxy first makes the canonical LAN host
        // observe EOF and publish the spectator's departure. The Internet
        // route object remains until that member has disappeared.
        route.proxy.close();
        route.proxyStarted = false;
    }

    void publishPendingHostMatchStart(
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        if (!hostMatchStartPending || matchStartPublished
            || !directoryConnection.roomSealed()) {
            return;
        }
        hostMatchStartPending = false;
        matchStartPublished = true;
        result.matchStarted = true;
        mode = RuntimeInternetLobbyFlowMode::Running;
        appendEvent(
            result,
            RuntimeInternetLobbyFlowEventKind::MatchStarted
        );
    }

    bool isRetiredRoute(std::uint64_t id) const {
        return std::find(retiredRouteIds.begin(), retiredRouteIds.end(), id) != retiredRouteIds.end();
    }

    bool canRecoverRoute(const RouteContext& route) const {
        return mode == RuntimeInternetLobbyFlowMode::Running && route.gameplayReported
            && activeConfig.role != RuntimeInternetRole::Spectator
            && route.remoteRole != RuntimeInternetRole::Spectator
            && (activeConfig.role != RuntimeInternetRole::Host || hostRouteIsRunningMember(route));
    }

    bool beginRouteRecovery(RouteContext& route, RuntimeInternetLobbyFlowPollResult& result) {
        if (!canRecoverRoute(route)) return terminalFailure(result, "Internet route is not eligible for combatant reconnect");
        if (route.awaitingResume) return true;
        std::string error;
        if (!route.connection.makeResumeRouteRequest(route.resumeRequest, error))
            return terminalFailure(result, std::move(error));
        if (route.recoveryDeadline == 0) {
            route.recoveryDeadline = lastNowMilliseconds + reconnectGraceMilliseconds;
            route.recoverySawLanPause = false;
        }
        route.proxy.close();
        if (route.pendingStream) route.pendingStream->close();
        route.pendingStream.reset();
        route.proxyStarted = false;
        route.tcpOpenRequested = false;
        route.awaitingResume = true;
        route.resumeAttempts = 0;
        route.nextResumeAttempt = lastNowMilliseconds;
        return true;
    }

    bool beginCoordinatorRecovery(RuntimeInternetLobbyFlowPollResult& result) {
        if (mode != RuntimeInternetLobbyFlowMode::Running || activeConfig.role == RuntimeInternetRole::Spectator)
            return terminalFailure(result, "authenticated Internet coordinator disconnected");
        if (!coordinatorReconnecting) {
            coordinatorReconnecting = true;
            coordinatorRecoveryDeadline = lastNowMilliseconds + reconnectGraceMilliseconds;
        }
        coordinatorAttemptPending = false;
        nextCoordinatorAttempt = lastNowMilliseconds + 250U;
        if (joinRoute && !beginRouteRecovery(*joinRoute, result)) return false;
        for (auto& route : hostRoutes) {
            if (runningSpectatorRoute(*route)) beginSpectatorShutdown(*route);
            else if (canRecoverRoute(*route) && !beginRouteRecovery(*route, result)) return false;
        }
        return true;
    }

    bool receiveResumedRoute(const RuntimeInternetControlMessage& message,
        const RuntimeNetworkPacket& packet, std::uint64_t nowMilliseconds,
        RuntimeInternetLobbyFlowPollResult& result) {
        const auto& offer = std::get<RuntimeInternetPeerCandidatesBody>(message.body);
        if (message.serviceId != activeConfig.serviceId || message.senderPeerId != activeConfig.serviceId
            || message.targetPeerId != activeConfig.localPeerId || message.roomId != activeConfig.roomId
            || message.transactionId != activeConfig.transactionId)
            return terminalFailure(result, "resumed Internet offer changed authenticated routing identity");
        if (offer.resumesConnectionId == 0) return true; // Sealed matches reject late admissions.
        if (auto* existing = findRoute(offer.connectionId)) {
            auto canonicalOffer = offer;
            if (existing->resumedOffer) canonicalOffer.resumesConnectionId = existing->resumedOffer->resumesConnectionId;
            if (!existing->resumedOffer || canonicalOffer != *existing->resumedOffer)
                return terminalFailure(result, "resumed Internet route changed canonical identity");
            return true; // Idempotent response to either participant's request.
        }
        auto* route = findRoute(offer.resumesConnectionId);
        if (!route) {
            if (isRetiredRoute(offer.resumesConnectionId)) return true;
            return terminalFailure(result, "resumed Internet route has no retained canonical member");
        }
        if (!canRecoverRoute(*route) || offer.remotePeerId != route->remotePeerId || offer.remoteRole != route->remoteRole)
            return terminalFailure(result, "resumed Internet route changed canonical membership");
        if (!beginRouteRecovery(*route, result)) return false;
        RuntimeInternetConnection replacement;
        std::string error;
        if (!replacement.beginPeerRouteFromCoordinator(
                activeConfig.role == RuntimeInternetRole::Host ? hostRouteConfig : activeConfig,
                packet, activeConfig.serviceId, nowMilliseconds, error))
            return terminalFailure(result, std::move(error));
        retiredRouteIds.push_back(route->connectionId);
        if (retiredRouteIds.size() > 256U) retiredRouteIds.pop_front();
        route->connection = std::move(replacement);
        route->connectionId = offer.connectionId;
        route->resumedOffer = offer;
        route->awaitingResume = false;
        route->tunnelEventSent = false;
        return true;
    }

    bool processRouteFailure(
        RuntimeInternetServiceEvent& event,
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        const std::string reason = event.reason.empty()
            ? "authenticated Internet relay failed"
            : event.reason;
        if (event.connectionId == 0U) {
            if (mode == RuntimeInternetLobbyFlowMode::Running) return beginCoordinatorRecovery(result);
            return terminalFailure(result, reason);
        }

        RouteContext* route = findRoute(event.connectionId);
        // Route-specific failures can arrive after the canonical LAN side has
        // already removed that route. They are stale cleanup notifications,
        // not failures of the shared coordinator connection.
        if (!route) return true;
        if (canRecoverRoute(*route)) return beginRouteRecovery(*route, result);

        if (activeConfig.role != RuntimeInternetRole::Host) {
            // A client or spectator owns only its one route to the host, so a
            // failure of that route ends its own join/lobby/match flow.
            return terminalFailure(result, reason);
        }
        if (runningSpectatorRoute(*route)) {
            beginSpectatorShutdown(*route);
            return true;
        }
        if (hostRouteIsRunningMember(*route)) {
            // Losing an admitted lockstep participant during a running match
            // is fatal. An unadmitted or already-departed route is isolated.
            return terminalFailure(result, reason);
        }

        const std::string peerId = route->remotePeerId;
        removeHostRoute(peerId);
        return true;
    }

    void appendEvent(
        RuntimeInternetLobbyFlowPollResult& result,
        RuntimeInternetLobbyFlowEventKind kind,
        const RouteContext* route = nullptr,
        std::string reason = {}
    ) const {
        RuntimeInternetLobbyFlowEvent event;
        event.kind = kind;
        if (route) {
            event.peerId = route->remotePeerId;
            event.role = route->remoteRole;
            event.connectionId = route->connectionId;
        }
        event.reason = std::move(reason);
        result.events.push_back(std::move(event));
    }

    bool terminalFailure(
        RuntimeInternetLobbyFlowPollResult& result,
        std::string reason
    ) {
        mode = RuntimeInternetLobbyFlowMode::Failed;
        result.succeeded = false;
        result.error = std::move(reason);
        appendEvent(
            result,
            RuntimeInternetLobbyFlowEventKind::Failed,
            nullptr,
            result.error
        );
        return false;
    }

    bool eventIdentityValid(
        const RuntimeInternetServiceEvent& event,
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        const std::string_view expectedService = service
            ? service->authenticatedServiceId()
            : std::string_view{};
        const std::string_view expectedPeer = service
            ? service->authenticatedPeerId()
            : std::string_view{};
        if (event.authenticatedServiceId != expectedService
            || event.authenticatedPeerId != expectedPeer
            || (!activeConfig.serviceId.empty()
                && event.authenticatedServiceId != activeConfig.serviceId)
            || (!activeConfig.localPeerId.empty()
                && event.authenticatedPeerId != activeConfig.localPeerId)) {
            return terminalFailure(
                result,
                "Internet service event was delivered under the wrong authenticated identity"
            );
        }
        return true;
    }

    bool receiveCommunityEvent(
        RuntimeInternetServiceEvent& event,
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        std::string error;
        if (!community.receive(
                event.packet,
                event.authenticatedServiceId,
                error
            )) {
            return terminalFailure(result, std::move(error));
        }
        std::vector<RuntimeInternetCommunityClientEvent> events =
            community.takeEvents();
        if (!events.empty()) result.communityChanged = true;
        result.communityEvents.insert(
            result.communityEvents.end(),
            std::make_move_iterator(events.begin()),
            std::make_move_iterator(events.end())
        );
        return true;
    }

    bool recentlyCompletedRoomQuery(
        const RuntimeInternetConnectionConfig& config
    ) const {
        return std::any_of(
            completedRoomQueries.begin(),
            completedRoomQueries.end(),
            [&config](const CompletedRoomQuery& query) {
                return query.serviceId == config.serviceId
                    && query.peerId == config.localPeerId
                    && query.transactionId == config.transactionId;
            }
        );
    }

    bool isCompletedRoomQueryResponse(
        const RuntimeInternetControlMessage& message,
        const RuntimeInternetServiceEvent& event
    ) const {
        if (message.type != RuntimeInternetControlType::RoomListResult) {
            return false;
        }
        return std::any_of(
            completedRoomQueries.begin(),
            completedRoomQueries.end(),
            [&message, &event](const CompletedRoomQuery& query) {
                return query.transactionId == message.transactionId
                    && query.serviceId == event.authenticatedServiceId
                    && query.peerId == event.authenticatedPeerId
                    && message.serviceId == query.serviceId
                    && message.senderPeerId == query.serviceId
                    && message.targetPeerId == query.peerId
                    && message.roomId.empty();
            }
        );
    }

    void rememberCompletedRoomQuery() {
        if (recentlyCompletedRoomQuery(activeConfig)) return;
        if (completedRoomQueries.size() >= maximumCompletedRoomQueries) {
            completedRoomQueries.pop_front();
        }
        completedRoomQueries.push_back({
            activeConfig.serviceId,
            activeConfig.localPeerId,
            activeConfig.transactionId
        });
    }

    bool addHostRoute(
        const RuntimeInternetControlMessage& message,
        const RuntimeNetworkPacket& packet,
        std::uint64_t nowMilliseconds,
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        const auto* body = std::get_if<RuntimeInternetPeerCandidatesBody>(
            &message.body
        );
        if (!body || body->connectionId == 0) {
            return terminalFailure(result, "invalid host peer-route offer");
        }
        if (RouteContext* existing = findRoute(body->connectionId)) {
            if (existing->remotePeerId == body->remotePeerId
                && existing->remoteRole == body->remoteRole) {
                return true;
            }
            return terminalFailure(
                result,
                "coordinator reused an Internet connection identity"
            );
        }
        if (findRouteByPeer(body->remotePeerId)) {
            return terminalFailure(
                result,
                "coordinator offered a duplicate Internet peer route"
            );
        }
        auto route = std::make_unique<RouteContext>();
        std::string error;
        if (!route->connection.beginPeerRouteFromCoordinator(
                hostRouteConfig,
                packet,
                activeConfig.serviceId,
                nowMilliseconds,
                error
            )) {
            return terminalFailure(result, std::move(error));
        }
        route->connectionId = body->connectionId;
        route->remotePeerId = body->remotePeerId;
        route->remoteRole = body->remoteRole;
        route->proxyConfig = hostProxyConfig;
        RouteContext* appended = route.get();
        hostRoutes.push_back(std::move(route));
        appendEvent(
            result,
            RuntimeInternetLobbyFlowEventKind::PeerRouteOffered,
            appended
        );
        return true;
    }

    bool receiveCoordinatorEvent(
        RuntimeInternetServiceEvent& event,
        std::uint64_t nowMilliseconds,
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        RuntimeInternetControlMessage message;
        std::string error;
        if (!decodeRuntimeInternetControlMessage(
                event.packet,
                message,
                error
            )) {
            return terminalFailure(result, std::move(error));
        }
        // A completed directory request is remembered across flow mode and
        // RuntimeInternetConnection replacement. This drops only a fully
        // authenticated, canonical RoomListResult for that old transaction;
        // state-changing coordinator responses keep their strict routing and
        // replay checks.
        if (isCompletedRoomQueryResponse(message, event)) return true;
        if (mode == RuntimeInternetLobbyFlowMode::Running) {
            if (message.type == RuntimeInternetControlType::PeerCandidates)
                return receiveResumedRoute(message, event.packet, nowMilliseconds, result);
            const auto routedId = messageConnectionId(message);
            if ((routedId && isRetiredRoute(*routedId))
                || (message.type == RuntimeInternetControlType::Error && isRetiredRoute(message.transactionId))) return true;
        }
        if (mode == RuntimeInternetLobbyFlowMode::Hosting
            || (mode == RuntimeInternetLobbyFlowMode::Lobby
                && activeConfig.role == RuntimeInternetRole::Host)
            || (mode == RuntimeInternetLobbyFlowMode::Running
                && activeConfig.role == RuntimeInternetRole::Host)) {
            if (message.type == RuntimeInternetControlType::PeerCandidates) {
                // Once the canonical room has started it is no longer a
                // joinable directory entry. Existing authenticated routes
                // remain owned by their RouteContext, but no late route may
                // be added to the running match.
                if (mode == RuntimeInternetLobbyFlowMode::Running) {
                    return true;
                }
                return addHostRoute(
                    message,
                    event.packet,
                    nowMilliseconds,
                    result
                );
            }
            const std::optional<std::uint64_t> connectionId =
                messageConnectionId(message);
            if (connectionId.has_value()) {
                RouteContext* route = findRoute(*connectionId);
                if (!route) {
                    return terminalFailure(
                        result,
                        "coordinator response references an unknown Internet route"
                    );
                }
                if (!route->connection.receiveCoordinator(
                        event.packet,
                        event.authenticatedServiceId,
                        nowMilliseconds,
                        error
                    )) {
                    return terminalFailure(result, std::move(error));
                }
                return true;
            }
            if (message.type == RuntimeInternetControlType::Error) {
                RouteContext* route = findRoute(message.transactionId);
                if (route) {
                    if (!route->connection.receiveCoordinator(
                            event.packet,
                            event.authenticatedServiceId,
                            nowMilliseconds,
                            error
                        )) {
                        return terminalFailure(result, std::move(error));
                    }
                    return true;
                }
            }
            if (!directoryConnection.receiveCoordinator(
                    event.packet,
                    event.authenticatedServiceId,
                    nowMilliseconds,
                    error
                )) {
                return terminalFailure(result, std::move(error));
            }
            if (message.type == RuntimeInternetControlType::RoomSealed) {
                publishPendingHostMatchStart(result);
            }
            if (!roomRegistered
                && directoryConnection.state()
                    == RuntimeInternetConnectionState::WaitingForPeer) {
                roomRegistered = true;
                mode = RuntimeInternetLobbyFlowMode::Lobby;
                appendEvent(
                    result,
                    RuntimeInternetLobbyFlowEventKind::RoomRegistered
                );
                const RuntimeInternetMatchmakingAssignment* assignment =
                    matchmaking.assignment();
                if (assignment
                    && assignment->hostPeerId == activeConfig.localPeerId
                    && matchmakingOptions
                    && !matchActivationRequested) {
                    RuntimeNetworkPacket activation;
                    if (!matchmaking.requestActivateMatch(
                            matchmakingOptions->activationTransactionId,
                            activation,
                            error
                        )
                        || !service->sendCoordinator(activation, error)) {
                        return terminalFailure(result, std::move(error));
                    }
                    matchActivationRequested = true;
                }
            }
            return true;
        }
        if (mode == RuntimeInternetLobbyFlowMode::QueryingRooms
            || mode == RuntimeInternetLobbyFlowMode::RoomListReady) {
            if (!directoryConnection.receiveCoordinator(
                    event.packet,
                    event.authenticatedServiceId,
                    nowMilliseconds,
                    error
                )) {
                return terminalFailure(result, std::move(error));
            }
            if (message.type == RuntimeInternetControlType::RoomListResult
                && directoryConnection.state()
                    == RuntimeInternetConnectionState::RoomListReady) {
                roomCache = directoryConnection.rooms();
                rememberCompletedRoomQuery();
                mode = RuntimeInternetLobbyFlowMode::RoomListReady;
                result.roomsChanged = true;
                appendEvent(
                    result,
                    RuntimeInternetLobbyFlowEventKind::RoomsUpdated
                );
            }
            return true;
        }
        if (joinRoute && (mode == RuntimeInternetLobbyFlowMode::Joining
                || mode == RuntimeInternetLobbyFlowMode::Lobby
                || mode == RuntimeInternetLobbyFlowMode::Running)) {
            if (!joinRoute->connection.receiveCoordinator(
                    event.packet,
                    event.authenticatedServiceId,
                    nowMilliseconds,
                    error
                )) {
                return terminalFailure(result, std::move(error));
            }
            if (joinRoute->connectionId == 0) {
                const auto connectionId = messageConnectionId(message);
                if (!connectionId.has_value()) {
                    return terminalFailure(
                        result,
                        "join response omitted the Internet connection identity"
                    );
                }
                joinRoute->connectionId = *connectionId;
                joinRoute->remotePeerId = joinRoute->connection.remotePeerId();
                joinRoute->remoteRole = RuntimeInternetRole::Host;
                appendEvent(
                    result,
                    RuntimeInternetLobbyFlowEventKind::PeerRouteOffered,
                    joinRoute.get()
                );
                runtimeRoomPasswordSecureClear(joinAdmissionProof);
            }
            return true;
        }
        return terminalFailure(
            result,
            "coordinator packet arrived without an active Internet request"
        );
    }

    bool receiveMatchmakingEvent(
        RuntimeInternetServiceEvent& event,
        std::uint64_t nowMilliseconds,
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        RuntimeInternetMatchmakingMessage message;
        std::string error;
        if (!decodeRuntimeInternetMatchmakingMessage(
                event.packet,
                message,
                error
            )
            || !matchmaking.receive(
                event.packet,
                event.authenticatedServiceId,
                nowMilliseconds,
                error
            )) {
            return terminalFailure(result, std::move(error));
        }
        if (matchmaking.state()
            == RuntimeInternetMatchmakingClientState::Failed) {
            return terminalFailure(
                result,
                matchmaking.failureReason().empty()
                    ? "Internet matchmaking failed"
                    : matchmaking.failureReason()
            );
        }
        if (message.type
            == RuntimeInternetMatchmakingMessageType::Assignment) {
            if (mode == RuntimeInternetLobbyFlowMode::Matchmaking) {
                matchTransitionPending = true;
            }
            appendEvent(
                result,
                RuntimeInternetLobbyFlowEventKind::MatchAssigned,
                nullptr,
                std::get<RuntimeInternetMatchmakingAssignment>(
                    message.body
                ).matchId
            );
        } else if (message.type
            == RuntimeInternetMatchmakingMessageType::OperationAck) {
            const RuntimeInternetMatchmakingAckBody& ack =
                std::get<RuntimeInternetMatchmakingAckBody>(message.body);
            if (ack.operation
                == RuntimeInternetMatchmakingOperation::Enqueue) {
                appendEvent(
                    result,
                    RuntimeInternetLobbyFlowEventKind::MatchQueued
                );
            } else if (ack.operation
                == RuntimeInternetMatchmakingOperation::ActivateMatch) {
                appendEvent(
                    result,
                    RuntimeInternetLobbyFlowEventKind::MatchActivated
                );
            }
        }
        return true;
    }

    bool receiveRelayEvent(
        RuntimeInternetServiceEvent& event,
        std::uint64_t nowMilliseconds,
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        RuntimeInternetControlMessage message;
        std::string error;
        if (!decodeRuntimeInternetControlMessage(event.packet, message, error)) {
            return terminalFailure(result, std::move(error));
        }
        const std::optional<std::uint64_t> connectionId =
            messageConnectionId(message);
        RouteContext* route = connectionId.has_value()
            ? findRoute(*connectionId)
            : nullptr;
        if (!route && connectionId && isRetiredRoute(*connectionId)) return true;
        if (!route || !route->connection.receiveRelay(
                event.packet,
                event.source,
                event.authenticatedServiceId,
                nowMilliseconds,
                error
            )) {
            return terminalFailure(
                result,
                error.empty()
                    ? "relay acknowledgement references an unknown Internet route"
                    : std::move(error)
            );
        }
        return true;
    }

    bool receiveStreamEvent(
        RuntimeInternetServiceEvent& event,
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        RouteContext* route = findRoute(event.connectionId);
        if (!route && isRetiredRoute(event.connectionId)) {
            if (event.tcpStream) event.tcpStream->close();
            return true;
        }
        if (!route || !event.tcpStream) {
            return terminalFailure(
                result,
                "authenticated TCP stream references an unknown Internet route"
            );
        }
        const auto& identity = event.tcpStream->identity();
        if (identity.authenticatedServiceId != activeConfig.serviceId
            || identity.authenticatedPeerId != activeConfig.localPeerId
            || identity.route.connectionId != event.connectionId) {
            event.tcpStream->close();
            return terminalFailure(
                result,
                "authenticated TCP stream identity does not match its Internet route"
            );
        }
        if (route->pendingStream || route->proxyStarted) {
            event.tcpStream->close();
            return terminalFailure(
                result,
                "duplicate authenticated TCP stream was supplied for one route"
            );
        }
        route->pendingStream = std::move(event.tcpStream);
        return true;
    }

    bool processServiceEvents(
        std::vector<RuntimeInternetServiceEvent> events,
        std::uint64_t nowMilliseconds,
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        for (RuntimeInternetServiceEvent& event : events) {
            if (!eventIdentityValid(event, result)) return false;
            switch (event.kind) {
            case RuntimeInternetServiceEventKind::CoordinatorAuthenticated:
                if (coordinatorReconnecting) {
                    coordinatorReconnecting = false;
                    coordinatorAttemptPending = false;
                    coordinatorRecoveryDeadline = 0;
                    directoryConnection.coordinatorReauthenticated(nowMilliseconds);
                    if (joinRoute) joinRoute->resumeAttempts = 0;
                    for (auto& route : hostRoutes) route->resumeAttempts = 0;
                }
                break;
            case RuntimeInternetServiceEventKind::CoordinatorPacket:
                if (event.packet.type == runtimeInternetCommunityPacketType
                    ? !receiveCommunityEvent(event, result)
                    : event.packet.type
                            == runtimeInternetMatchmakingPacketType
                    ? !receiveMatchmakingEvent(
                        event,
                        nowMilliseconds,
                        result
                    )
                    : !receiveCoordinatorEvent(
                        event,
                        nowMilliseconds,
                        result
                    )) {
                    return false;
                }
                break;
            case RuntimeInternetServiceEventKind::RelayPacket:
                if (!receiveRelayEvent(event, nowMilliseconds, result)) {
                    return false;
                }
                break;
            case RuntimeInternetServiceEventKind::TcpStreamOpened:
                if (!receiveStreamEvent(event, result)) return false;
                break;
            case RuntimeInternetServiceEventKind::Failure:
                if (!processRouteFailure(event, result)) return false;
                break;
            }
        }
        return true;
    }

    bool dispatchConnectionActions(
        RouteContext* route,
        RuntimeInternetConnection& connection,
        std::uint64_t nowMilliseconds,
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        if (coordinatorReconnecting || (route && route->awaitingResume)) return true;
        std::vector<RuntimeInternetAction> actions = connection.poll(
            nowMilliseconds
        );
        for (std::size_t actionIndex = 0; actionIndex < actions.size();
             ++actionIndex) {
            const RuntimeInternetAction& action = actions[actionIndex];
            if (std::any_of(
                    actions.begin(),
                    actions.begin()
                        + static_cast<std::ptrdiff_t>(actionIndex),
                    [&](const RuntimeInternetAction& earlier) {
                        return sameInternetAction(earlier, action);
                    }
                )) {
                continue;
            }
            std::string error;
            switch (action.kind) {
            case RuntimeInternetActionKind::SendCoordinator: {
                RuntimeInternetControlMessage control;
                std::string decodeError;
                const bool decoded = decodeRuntimeInternetControlMessage(
                    action.packet,
                    control,
                    decodeError
                );
                const std::span<const std::uint8_t> proof =
                    route == nullptr && !hostRegistrationProof.empty()
                        && decoded
                        && control.type
                            == RuntimeInternetControlType::RegisterRoom
                    ? std::span<const std::uint8_t>(hostRegistrationProof)
                    : route == joinRoute.get()
                        && !joinAdmissionProof.empty()
                        && decoded
                        && control.type == RuntimeInternetControlType::JoinRoom
                    ? std::span<const std::uint8_t>(joinAdmissionProof)
                    : std::span<const std::uint8_t>{};
                if (!proof.empty()) {
                    if (!service->sendCoordinatorWithAdmissionProof(
                            action.packet,
                            proof,
                            error
                        )) {
                        return terminalFailure(result, std::move(error));
                    }
                    break;
                }
                if (!service->sendCoordinator(action.packet, error)) {
                    if (mode == RuntimeInternetLobbyFlowMode::Running) return beginCoordinatorRecovery(result);
                    return terminalFailure(result, std::move(error));
                }
                break;
            }
            case RuntimeInternetActionKind::SendPeerUdp:
                // Intentionally bypassed.  Candidate probing is advisory;
                // multiplayer requires an ordered TCP stream and the state
                // machine will request TCP relay at punch timeout.
                break;
            case RuntimeInternetActionKind::OpenRelayUdp:
                if (!connection.reportRelayOpenFailure(
                        RuntimeInternetRelayTransport::Udp,
                        nowMilliseconds,
                        error
                    )) {
                    return terminalFailure(result, std::move(error));
                }
                break;
            case RuntimeInternetActionKind::OpenRelayTcp: {
                if (!route) {
                    return terminalFailure(
                        result,
                        "TCP relay action has no Internet peer route"
                    );
                }
                if (route->tcpOpenRequested) break;
                const std::optional<RuntimeInternetTcpRouteDescriptor>
                    descriptor = connection.tcpRoute();
                if (!descriptor.has_value()) {
                    return terminalFailure(
                        result,
                        "TCP relay action omitted its authenticated route"
                    );
                }
                RuntimeInternetServiceTcpOpenRequest request;
                request.route = *descriptor;
                request.endpoint = action.endpoint;
                request.relayBindPacket = action.packet;
                if (!service->openAuthenticatedTcpRelay(request, error)) {
                    if (canRecoverRoute(*route)) return beginRouteRecovery(*route, result);
                    std::string ignored;
                    (void)connection.reportRelayOpenFailure(
                        RuntimeInternetRelayTransport::Tcp,
                        nowMilliseconds,
                        ignored
                    );
                    return terminalFailure(
                        result,
                        error.empty()
                            ? "authenticated TCP relay could not be opened"
                            : std::move(error)
                    );
                }
                route->tcpOpenRequested = true;
                break;
            }
            }
        }
        if (connection.state() == RuntimeInternetConnectionState::Failed) {
            if (mode == RuntimeInternetLobbyFlowMode::Running) {
                if (!route || connection.failureReason().find("coordinator") != std::string::npos)
                    return beginCoordinatorRecovery(result);
                if (canRecoverRoute(*route)) return beginRouteRecovery(*route, result);
            }
            return terminalFailure(result, connection.failureReason());
        }
        return true;
    }

    bool dispatchAllActions(
        std::uint64_t nowMilliseconds,
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        if (mode == RuntimeInternetLobbyFlowMode::QueryingRooms
            || mode == RuntimeInternetLobbyFlowMode::RoomListReady
            || activeConfig.role == RuntimeInternetRole::Host) {
            if (!dispatchConnectionActions(
                    nullptr,
                    directoryConnection,
                    nowMilliseconds,
                    result
                )) {
                return false;
            }
        }
        if (joinRoute && !dispatchConnectionActions(
                joinRoute.get(),
                joinRoute->connection,
                nowMilliseconds,
                result
            )) {
            return false;
        }
        for (auto& route : hostRoutes) {
            if (!dispatchConnectionActions(
                    route.get(),
                    route->connection,
                    nowMilliseconds,
                    result
                )) {
                return false;
            }
        }
        return true;
    }

    bool tryStartProxy(
        RouteContext& route,
        std::uint64_t nowMilliseconds,
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        if (route.proxyStarted || !route.pendingStream
            || !route.connection.tcpTunnelBound()) {
            return true;
        }
        route.proxyConfig.heartbeatMilliseconds = 1000U;
        route.proxyConfig.heartbeatTimeoutMilliseconds = 5000U;
        const std::optional<RuntimeInternetTcpRouteDescriptor> descriptor =
            route.connection.tcpRoute();
        if (!descriptor.has_value()) {
            return terminalFailure(
                result,
                "bound TCP tunnel omitted its Internet route"
            );
        }
        std::string error;
        if (descriptor->localRole == RuntimeInternetRole::Host) {
            RuntimeMultiplayerRuntimeBridge* bridge = activeRuntimeBridge();
            if (!bridge
                || bridge->orderedTransportPumpSessionPort() == 0
                || !route.proxy.startHost(
                    route.proxyConfig,
                    *descriptor,
                    std::move(route.pendingStream),
                    bridge->orderedTransportPumpSessionPort(),
                    nowMilliseconds,
                    error
                )) {
                return terminalFailure(
                    result,
                    error.empty()
                        ? "private host lobby endpoint is unavailable"
                        : std::move(error)
                );
            }
        } else {
            if (!route.proxy.startClient(
                    route.proxyConfig,
                    *descriptor,
                    std::move(route.pendingStream),
                    joinOptions.canonicalRoom,
                    nowMilliseconds,
                    error
                )) {
                return terminalFailure(result, std::move(error));
            }
            RuntimeInternetTcpTunnelClientEndpoint endpoint =
                route.proxy.clientEndpoint();
            RuntimeMultiplayerLanJoinOptions network = joinOptions.network;
            network.room = std::move(endpoint.room);
            network.localProxyPrelude = std::move(
                endpoint.localProxyPrelude
            );
            const bool connected = mode == RuntimeInternetLobbyFlowMode::Running
                ? activeRuntimeBridge() && activeRuntimeBridge()->orderedTransportPumpReplaceReconnectEndpoint(
                    network.room.sessionPort, std::move(network.localProxyPrelude), error)
                : lobby.join(std::move(network), error);
            if (!connected) {
                route.proxy.close();
                return terminalFailure(result, std::move(error));
            }
        }
        route.proxyStarted = true;
        if (!route.tunnelEventSent) {
            appendEvent(
                result,
                RuntimeInternetLobbyFlowEventKind::TcpTunnelBound,
                &route
            );
            route.tunnelEventSent = true;
        }
        return true;
    }

    bool startReadyProxies(
        std::uint64_t nowMilliseconds,
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        if (joinRoute && !tryStartProxy(
                *joinRoute,
                nowMilliseconds,
                result
            )) {
            return false;
        }
        for (auto& route : hostRoutes) {
            if (!tryStartProxy(*route, nowMilliseconds, result)) return false;
        }
        return true;
    }

    bool pollProxies(
        std::uint64_t nowMilliseconds,
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        std::vector<std::string> departedHostPeers;
        const auto pollOne = [&](RouteContext& route) {
            if (route.spectatorShutdownPending) {
                if (!hostRouteIsRunningMember(route)) {
                    departedHostPeers.push_back(route.remotePeerId);
                }
                return true;
            }
            if (!route.proxyStarted) return true;
            const auto hostMayIsolateRoute = [&]() {
                return activeConfig.role == RuntimeInternetRole::Host
                    && !hostRouteIsRunningMember(route);
            };
            const RuntimeInternetTcpTunnelPollResult polled = route.proxy.poll(
                nowMilliseconds
            );
            if (!polled.succeeded) {
                if (canRecoverRoute(route)) return beginRouteRecovery(route, result);
                if (runningSpectatorRoute(route)) {
                    beginSpectatorShutdown(route);
                    return true;
                }
                if (hostMayIsolateRoute()) {
                    route.proxy.close();
                    route.gameplayReported = false;
                    departedHostPeers.push_back(route.remotePeerId);
                    return true;
                }
                return terminalFailure(result, polled.error);
            }
            if (polled.closed) {
                route.proxyStarted = false;
                if (canRecoverRoute(route)) return beginRouteRecovery(route, result);
                if (runningSpectatorRoute(route)) {
                    beginSpectatorShutdown(route);
                } else if (hostMayIsolateRoute()) {
                    route.gameplayReported = false;
                    departedHostPeers.push_back(route.remotePeerId);
                } else if (mode == RuntimeInternetLobbyFlowMode::Running) {
                    return terminalFailure(
                        result,
                        "authenticated Internet TCP tunnel closed during the running match"
                    );
                }
            }
            return true;
        };
        if (joinRoute && !pollOne(*joinRoute)) return false;
        for (auto& route : hostRoutes) {
            if (!pollOne(*route)) return false;
        }
        for (const std::string& peerId : departedHostPeers) {
            removeHostRoute(peerId);
        }
        return true;
    }

    void translateLobbyEvent(
        const RuntimeMultiplayerLanEvent& event,
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        RouteContext* route = findRouteByPeer(event.peerId);
        switch (event.kind) {
        case RuntimeMultiplayerLanEventKind::JoinAccepted: {
            if (!route) {
                terminalFailure(
                    result,
                    "lobby admitted a peer without an authenticated Internet route"
                );
                return;
            }
            std::string error;
            if (!route->gameplayReported
                && !route->connection.reportGameplaySessionJoined(
                    route->connectionId,
                    error
                )) {
                terminalFailure(result, std::move(error));
                return;
            }
            route->gameplayReported = true;
            mode = RuntimeInternetLobbyFlowMode::Lobby;
            appendEvent(
                result,
                RuntimeInternetLobbyFlowEventKind::JoinAccepted,
                route
            );
            break;
        }
        case RuntimeMultiplayerLanEventKind::JoinRejected:
            appendEvent(
                result,
                RuntimeInternetLobbyFlowEventKind::JoinRejected,
                route,
                event.reason
            );
            if (activeConfig.role != RuntimeInternetRole::Host) {
                mode = RuntimeInternetLobbyFlowMode::Failed;
            }
            break;
        case RuntimeMultiplayerLanEventKind::LobbyChanged:
            result.lobbyChanged = true;
            appendEvent(
                result,
                RuntimeInternetLobbyFlowEventKind::LobbyChanged,
                route
            );
            break;
        case RuntimeMultiplayerLanEventKind::ReadyChanged:
            appendEvent(
                result,
                RuntimeInternetLobbyFlowEventKind::ReadyChanged,
                route
            );
            break;
        case RuntimeMultiplayerLanEventKind::Started:
            if (activeConfig.role == RuntimeInternetRole::Host) {
                std::string error;
                if (!directoryConnection.requestRoomSeal(
                        lastNowMilliseconds,
                        error
                    )) {
                    terminalFailure(result, std::move(error));
                    return;
                }
                hostMatchStartPending = true;
                break;
            }
            result.matchStarted = true;
            matchStartPublished = true;
            mode = RuntimeInternetLobbyFlowMode::Running;
            appendEvent(
                result,
                RuntimeInternetLobbyFlowEventKind::MatchStarted,
                route
            );
            break;
        case RuntimeMultiplayerLanEventKind::Kicked:
            appendEvent(
                result,
                RuntimeInternetLobbyFlowEventKind::Kicked,
                route,
                event.reason.empty()
                    ? "removed from the Internet lobby by the host"
                    : event.reason
            );
            if (activeConfig.role != RuntimeInternetRole::Host) {
                mode = RuntimeInternetLobbyFlowMode::Kicked;
                if (joinRoute) {
                    joinRoute->proxy.close();
                    joinRoute->connection.close();
                    if (joinRoute->pendingStream) {
                        joinRoute->pendingStream->close();
                    }
                    joinRoute->gameplayReported = false;
                }
            }
            break;
        case RuntimeMultiplayerLanEventKind::Disconnected:
        case RuntimeMultiplayerLanEventKind::TransportError:
            appendEvent(
                result,
                RuntimeInternetLobbyFlowEventKind::PeerDisconnected,
                route,
                event.reason
            );
            if (activeConfig.role == RuntimeInternetRole::Host) {
                removeHostRoute(event.peerId);
            }
            break;
        case RuntimeMultiplayerLanEventKind::ProtocolRejected:
            if (activeConfig.role != RuntimeInternetRole::Host
                && joinRoute && !joinRoute->gameplayReported) {
                appendEvent(
                    result,
                    RuntimeInternetLobbyFlowEventKind::JoinRejected,
                    joinRoute.get(),
                    event.reason
                );
                mode = RuntimeInternetLobbyFlowMode::Failed;
            } else {
                appendEvent(
                    result,
                    RuntimeInternetLobbyFlowEventKind::PeerDisconnected,
                    route,
                    event.reason
                );
            }
            break;
        default:
            break;
        }
    }

    bool pollLobby(
        int timeoutMilliseconds,
        RuntimeInternetLobbyFlowPollResult& result
    ) {
        if (!lobby.initialized()) return true;
        RuntimeMultiplayerLobbyFlowPollResult polled = lobby.poll(
            timeoutMilliseconds
        );
        result.lobbyChanged = result.lobbyChanged || polled.lobbyChanged;
        result.lobbyEvents = polled.events;
        bool canonicalJoinRejected = false;
        bool canonicalKicked = false;
        for (const RuntimeMultiplayerLanEvent& event : result.lobbyEvents) {
            canonicalJoinRejected = canonicalJoinRejected
                || event.kind == RuntimeMultiplayerLanEventKind::JoinRejected
                || (event.kind
                        == RuntimeMultiplayerLanEventKind::ProtocolRejected
                    && activeConfig.role != RuntimeInternetRole::Host);
            canonicalKicked = canonicalKicked
                || event.kind == RuntimeMultiplayerLanEventKind::Kicked;
            translateLobbyEvent(event, result);
            if (!result.succeeded) return false;
        }
        if (!polled.succeeded) {
            // RuntimeMultiplayerLobbyFlow cannot build a canonical lobby model
            // after admission is rejected, so it reports its inactive room as
            // an error.  At this layer the authenticated JoinRejected event is
            // the expected, user-visible outcome and must not be rewritten as
            // a transport failure.
            if (canonicalJoinRejected
                && activeConfig.role != RuntimeInternetRole::Host) {
                result.succeeded = true;
                result.error.clear();
                return true;
            }
            if (canonicalKicked
                && activeConfig.role != RuntimeInternetRole::Host) {
                result.succeeded = true;
                result.error.clear();
                return true;
            }
            return terminalFailure(result, std::move(polled.error));
        }
        return true;
    }

    std::uint64_t runningNowMilliseconds() const {
        if (!runningClockInitialized) return lastNowMilliseconds;
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::milliseconds
        >(std::chrono::steady_clock::now() - runningClockBaseSteady).count();
        if (elapsed <= 0) return runningClockBaseMilliseconds;
        const std::uint64_t elapsedMilliseconds =
            static_cast<std::uint64_t>(elapsed);
        if (elapsedMilliseconds
            > std::numeric_limits<std::uint64_t>::max()
                - runningClockBaseMilliseconds) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return runningClockBaseMilliseconds + elapsedMilliseconds;
    }

    bool pumpRecovery(std::uint64_t nowMilliseconds, RuntimeInternetLobbyFlowPollResult& result) {
        if (coordinatorReconnecting) {
            if (nowMilliseconds >= coordinatorRecoveryDeadline)
                return terminalFailure(result, "Internet coordinator reconnect grace period expired");
            if (!coordinatorAttemptPending && nowMilliseconds >= nextCoordinatorAttempt) {
                std::string error;
                coordinatorAttemptPending = service->reauthenticateCoordinator(error);
                nextCoordinatorAttempt = nowMilliseconds + 1000U;
            }
        }
        const auto advance = [&](RouteContext& route) {
            if (route.recoveryDeadline == 0) return true;
            auto* bridge = activeRuntimeBridge();
            const bool paused = bridge && bridge->orderedTransportPumpReconnectPending();
            route.recoverySawLanPause = route.recoverySawLanPause || paused;
            if (!coordinatorReconnecting && !route.awaitingResume && route.proxyStarted
                && route.connection.tcpTunnelBound() && route.recoverySawLanPause && !paused) {
                std::string error;
                if (!route.connection.reportGameplaySessionJoined(route.connectionId, error))
                    return terminalFailure(result, std::move(error));
                route.recoveryDeadline = 0;
                route.resumeRequest = {};
                return true;
            }
            if (nowMilliseconds >= route.recoveryDeadline)
                return terminalFailure(result, "Internet combatant reconnect grace period expired");
            if (!coordinatorReconnecting && route.awaitingResume && nowMilliseconds >= route.nextResumeAttempt) {
                if (route.resumeAttempts >= 3U) return beginCoordinatorRecovery(result);
                std::string error;
                if (!service->sendCoordinator(route.resumeRequest, error)) return beginCoordinatorRecovery(result);
                ++route.resumeAttempts;
                route.nextResumeAttempt = nowMilliseconds + 1000U;
            }
            return true;
        };
        if (joinRoute && !advance(*joinRoute)) return false;
        for (auto& route : hostRoutes) if (!advance(*route)) return false;
        return true;
    }

    bool pumpRunningTransport(
        RuntimeMultiplayerOrderedTransportPumpStage stage,
        int transportTimeoutMilliseconds,
        std::string& errorMessage
    ) {
        if (mode != RuntimeInternetLobbyFlowMode::Running
            || !detachedRunningBridge) {
            return failWith(
                errorMessage,
                "Internet running transport is not active"
            );
        }
        if (!service) {
            return failWith(
                errorMessage,
                "Internet authenticated service adapter is missing"
            );
        }

        const std::uint64_t nowMilliseconds = runningNowMilliseconds();
        lastNowMilliseconds = nowMilliseconds;
        RuntimeInternetLobbyFlowPollResult result;
        const int serviceTimeoutMilliseconds =
            stage == RuntimeMultiplayerOrderedTransportPumpStage::BeforeLanPoll
            ? std::max(0, transportTimeoutMilliseconds)
            : 0;
        std::vector<RuntimeInternetServiceEvent> serviceEvents =
            service->poll(serviceTimeoutMilliseconds);
        if (!processServiceEvents(
                std::move(serviceEvents),
                nowMilliseconds,
                result
            )
            || !pumpRecovery(nowMilliseconds, result)
            || !dispatchAllActions(nowMilliseconds, result)
            || !startReadyProxies(nowMilliseconds, result)
            || !pollProxies(nowMilliseconds, result)) {
            errorMessage = result.error.empty()
                ? "Internet running transport failed"
                : std::move(result.error);
            return false;
        }
        errorMessage.clear();
        return true;
    }

    void shutdownRunningTransport() noexcept {
        try {
            directoryConnection.close();
            for (auto& route : hostRoutes) {
                route->proxy.close();
                route->connection.close();
                if (route->pendingStream) route->pendingStream->close();
            }
            hostRoutes.clear();
            if (joinRoute) {
                joinRoute->proxy.close();
                joinRoute->connection.close();
                if (joinRoute->pendingStream) joinRoute->pendingStream->close();
            }
            joinRoute.reset();
            lobby.close();
            clearMatchmaking();
            if (service) service->close();
        } catch (...) {
        }
        detachedRunningBridge = nullptr;
        runningClockInitialized = false;
        mode = RuntimeInternetLobbyFlowMode::Closed;
    }
};

struct RuntimeInternetLobbyFlow::Impl::RunningOrderedTransport final
    : RuntimeMultiplayerExternalOrderedTransport {
    explicit RunningOrderedTransport(std::unique_ptr<Impl> state)
        : state_(std::move(state)) {}

    ~RunningOrderedTransport() override { close(); }
    std::uint32_t reconnectRecoveryTimeoutMilliseconds() const noexcept override {
        return static_cast<std::uint32_t>(Impl::reconnectGraceMilliseconds);
    }

    bool supportsReconnectRecovery() const noexcept override {
        if (closed_ || !state_ || state_->coordinatorReconnecting) return false;
        const auto ready = [](const auto& route) {
            return route->remoteRole == RuntimeInternetRole::Spectator
                || (!route->awaitingResume && route->proxyStarted && route->connection.tcpTunnelBound());
        };
        return (!state_->joinRoute || ready(state_->joinRoute))
            && std::all_of(state_->hostRoutes.begin(), state_->hostRoutes.end(), ready);
    }

    bool pump(
        RuntimeMultiplayerOrderedTransportPumpStage stage,
        int transportTimeoutMilliseconds,
        std::string& errorMessage
    ) override {
        if (closed_ || !state_) {
            return failWith(
                errorMessage,
                "Internet running transport is closed"
            );
        }
        return state_->pumpRunningTransport(
            stage,
            transportTimeoutMilliseconds,
            errorMessage
        );
    }

    bool reportRankedMatchResult(
        bool localVictory,
        bool& submitted,
        std::string& errorMessage
    ) override {
        return sendRankedReport(localVictory, false, submitted, errorMessage);
    }

    bool reportRankedMatchAbandon(bool& submitted, std::string& errorMessage) override {
        return sendRankedReport(false, true, submitted, errorMessage);
    }

    bool sendRankedReport(bool localVictory, bool abandon, bool& submitted, std::string& errorMessage) {
        submitted = false;
        if (closed_ || !state_) {
            return failWith(
                errorMessage,
                "Internet running transport is closed"
            );
        }
        const RuntimeInternetMatchmakingAssignment* assignment =
            state_->matchmaking.assignment();
        if (assignment == nullptr) {
            errorMessage.clear();
            return true;
        }
        if (!state_->community.snapshot().registeredProfile) {
            return failWith(
                errorMessage,
                "Quick Match result requires a registered community profile"
            );
        }
        if (!state_->service || assignment->peerIds.size() != 2U) {
            return failWith(
                errorMessage,
                "Quick Match result assignment is incomplete"
            );
        }
        const std::string localPeerId(
            state_->service->authenticatedPeerId()
        );
        const auto local = std::find(
            assignment->peerIds.begin(),
            assignment->peerIds.end(),
            localPeerId
        );
        if (local == assignment->peerIds.end()) {
            errorMessage.clear();
            return true;
        }
        const auto opponent = std::find_if(
            assignment->peerIds.begin(),
            assignment->peerIds.end(),
            [&localPeerId](const std::string& peerId) {
                return peerId != localPeerId;
            }
        );
        if (opponent == assignment->peerIds.end()) {
            return failWith(
                errorMessage,
                "Quick Match result opponent is missing"
            );
        }

        // The match id is service-generated and unique. Folding it together
        // with the per-peer assignment id yields a stable one-shot community
        // transaction if delivery is observed more than once by the service.
        std::uint64_t transactionId = 1469598103934665603ULL;
        const auto mixByte = [&transactionId](std::uint8_t byte) {
            transactionId ^= byte;
            transactionId *= 1099511628211ULL;
        };
        for (const unsigned char byte : assignment->matchId) mixByte(byte);
        for (const unsigned char byte : localPeerId) mixByte(byte);
        if (abandon) mixByte(0xa6U);
        for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
            mixByte(static_cast<std::uint8_t>(
                assignment->requestId >> shift
            ));
        }
        if (transactionId == 0U) {
            transactionId = 0x574f4c52414e4b31ULL;
        }

        RuntimeNetworkPacket packet;
        const bool encoded = abandon
            ? state_->community.makeAbandonQuickMatchRequest(transactionId, assignment->matchId, *opponent, packet, errorMessage)
            : state_->community.makeReportQuickMatchRequest(
                transactionId,
                assignment->matchId,
                *opponent,
                localVictory
                    ? RuntimeInternetCommunityQuickMatchResult::Win
                    : RuntimeInternetCommunityQuickMatchResult::Loss,
                packet,
                errorMessage
            );
        if (!encoded || !state_->sendCommunityPacket(
                std::move(packet),
                errorMessage
            )) {
            return false;
        }
        if (abandon) {
            const std::string rankedMatchId = assignment->matchId;
            // Keep the coordinator alive until the service's durable final
            // receipt arrives. A broken connection falls back to the normal
            // disconnect arbitration, without fabricating a successful forfeit.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            do {
                if (!state_->pumpRunningTransport(RuntimeMultiplayerOrderedTransportPumpStage::AfterLanPoll, 0, errorMessage)) return false;
                for (const auto& record : state_->community.matchHistory()) {
                    if (record.matchId == rankedMatchId && runtimeInternetRankedTerminal(record.status)) {
                        submitted = true;
                        errorMessage.clear();
                        return true;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            } while (std::chrono::steady_clock::now() < deadline);
            return failWith(errorMessage, "Ranked departure receipt unavailable; the service will finalize after the disconnect deadline.");
        }
        submitted = true;
        // Flush the TLS coordinator queue immediately; the following normal
        // bridge pumps will receive the updated profile/ladder snapshot.
        return state_->pumpRunningTransport(
            RuntimeMultiplayerOrderedTransportPumpStage::AfterLanPoll,
            0,
            errorMessage
        );
    }

    void close() noexcept override {
        if (closed_) return;
        closed_ = true;
        if (state_) state_->shutdownRunningTransport();
    }

private:
    std::unique_ptr<Impl> state_;
    bool closed_ = false;
};

RuntimeInternetLobbyFlow::RuntimeInternetLobbyFlow(
    std::unique_ptr<RuntimeInternetServiceAdapter> serviceAdapter,
    RuntimeMultiplayerLockstepConfig lockstepConfig
) : impl_(std::make_unique<Impl>(
        std::move(serviceAdapter),
        std::move(lockstepConfig)
    )) {}

RuntimeInternetLobbyFlow::~RuntimeInternetLobbyFlow() = default;
RuntimeInternetLobbyFlow::RuntimeInternetLobbyFlow(
    RuntimeInternetLobbyFlow&&
) noexcept = default;
RuntimeInternetLobbyFlow& RuntimeInternetLobbyFlow::operator=(
    RuntimeInternetLobbyFlow&&
) noexcept = default;

bool RuntimeInternetLobbyFlow::beginRoomQuery(
    RuntimeInternetLobbyQueryOptions options,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    if (!impl_) {
        return failWith(
            errorMessage,
            "Internet lobby ownership has transferred to the running match"
        );
    }
    impl_->lastNowMilliseconds = nowMilliseconds;
    impl_->clearMatchmaking();
    impl_->resetActive(false);
    if (!impl_->validateAdapter(options.connection, errorMessage)) return false;
    if (impl_->recentlyCompletedRoomQuery(options.connection)) {
        return failWith(
            errorMessage,
            "Internet room query transaction was recently completed"
        );
    }
    if (!impl_->directoryConnection.beginRoomQuery(
            options.connection,
            nowMilliseconds,
            errorMessage
        )) {
        return false;
    }
    impl_->activeConfig = std::move(options.connection);
    impl_->mode = RuntimeInternetLobbyFlowMode::QueryingRooms;
    errorMessage.clear();
    return true;
}

bool RuntimeInternetLobbyFlow::beginMatchmaking(
    RuntimeInternetLobbyMatchmakingOptions options,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    if (!impl_) {
        return failWith(
            errorMessage,
            "Internet lobby ownership has transferred to the running match"
        );
    }
    impl_->lastNowMilliseconds = nowMilliseconds;
    impl_->clearMatchmaking();
    impl_->resetActive(false);
    const std::string hostFingerprint = contentFingerprintText(
        options.hostTemplate.network.session.compatibility.contentFingerprint
    );
    const std::string joinFingerprint = contentFingerprintText(
        options.joinTemplate.network.compatibility.contentFingerprint
    );
    if (options.activationTransactionId == 0U
        || options.activationTransactionId == options.request.requestId
        || options.matchmakingClient.serviceId.empty()
        || options.matchmakingClient.localPeerId.empty()
        || options.hostTemplate.connection.serviceId
            != options.matchmakingClient.serviceId
        || options.joinTemplate.connection.serviceId
            != options.matchmakingClient.serviceId
        || options.hostTemplate.connection.localPeerId
            != options.matchmakingClient.localPeerId
        || options.joinTemplate.connection.localPeerId
            != options.matchmakingClient.localPeerId
        || options.hostTemplate.network.session.hostPeerId
            != options.matchmakingClient.localPeerId
        || options.hostTemplate.lobby.hostMember.peerId
            != options.matchmakingClient.localPeerId
        || options.joinTemplate.network.peerId
            != options.matchmakingClient.localPeerId
        || options.joinTemplate.network.participation
            != RuntimeMultiplayerParticipationRole::Combatant
        || options.request.displayName
            != options.hostTemplate.network.session.hostDisplayName
        || options.request.displayName
            != options.hostTemplate.lobby.hostMember.displayName
        || options.request.displayName
            != options.joinTemplate.network.displayName
        || hostFingerprint != joinFingerprint
        || options.request.contentFingerprint != hostFingerprint
        || options.request.playerCount
            > options.hostTemplate.network.session.maximumMembers) {
        return failWith(
            errorMessage,
            "Internet matchmaking templates do not share one authenticated player and compatibility"
        );
    }
    RuntimeInternetConnectionConfig identity =
        options.hostTemplate.connection;
    identity.serviceId = options.matchmakingClient.serviceId;
    identity.localPeerId = options.matchmakingClient.localPeerId;
    if (!impl_->validateAdapter(identity, errorMessage)) return false;

    RuntimeNetworkPacket enqueue;
    if (!impl_->matchmaking.begin(
            options.matchmakingClient,
            options.request,
            nowMilliseconds,
            enqueue,
            errorMessage
        )
        || !impl_->service->sendCoordinator(enqueue, errorMessage)) {
        impl_->clearMatchmaking();
        return false;
    }
    impl_->activeConfig = std::move(identity);
    impl_->matchmakingOptions = std::move(options);
    impl_->mode = RuntimeInternetLobbyFlowMode::Matchmaking;
    errorMessage.clear();
    return true;
}

bool RuntimeInternetLobbyFlow::startHost(
    RuntimeInternetLobbyHostOptions options,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    if (!impl_) {
        return failWith(
            errorMessage,
            "Internet lobby ownership has transferred to the running match"
        );
    }
    impl_->lastNowMilliseconds = nowMilliseconds;
    if (!impl_->preserveMatchmakingTransition) {
        impl_->clearMatchmaking();
    }
    impl_->resetActive(false);
    if (!impl_->validateAdapter(options.connection, errorMessage)) {
        runtimeRoomPasswordSecureClear(options.roomPasswordVerifier);
        runtimeRoomPasswordSecureClear(options.network.password);
        return false;
    }
    const bool unexpectedPrivateLanPassword =
        !options.network.password.empty();
    runtimeRoomPasswordSecureClear(options.network.password);
    const std::string fingerprint = contentFingerprintText(
        options.network.session.compatibility.contentFingerprint
    );
    const std::optional<std::size_t> combatantCapacity = options.resumeSave
        ? std::optional<std::size_t>(std::count_if(options.resumeSave->setup.slots.begin(), options.resumeSave->setup.slots.end(),
            [](const auto& slot) { return slot.controller == RuntimeMultiplayerMatchSlotController::Human; }))
        : authoredCombatantCapacity(options.lobby);
    const bool validPasswordVerifier =
        options.roomPasswordVerifier.empty()
        || (options.room.passwordProtected
            && options.roomPasswordVerifier.size()
                == runtimeRoomPasswordVerifierBytes);
    if (options.connection.role != RuntimeInternetRole::Host
        || options.connection.roomId != options.room.roomId
        || options.connection.localPeerId != options.room.hostPeerId
        || options.connection.contentFingerprint != fingerprint
        || options.room.contentFingerprint != fingerprint
        || options.network.session.roomId != options.room.roomId
        || options.network.session.hostPeerId
            != options.connection.localPeerId
        || options.network.roomName != options.room.roomName
        || options.network.passwordProtected
        || unexpectedPrivateLanPassword
        || !validPasswordVerifier
        || !combatantCapacity.has_value()
        || options.room.maximumPlayers != *combatantCapacity
        || options.network.session.maximumMembers < *combatantCapacity
        || options.network.session.maximumMembers
            > runtimeMultiplayerMaximumMembers
        || options.lobby.hostMember.peerId
            != options.connection.localPeerId) {
        runtimeRoomPasswordSecureClear(options.roomPasswordVerifier);
        return failWith(
            errorMessage,
            "Internet room registration does not match the private canonical lobby"
        );
    }
    std::vector<std::uint8_t> registrationProof =
        std::move(options.roomPasswordVerifier);
    options.network.enableDiscovery = false;
    options.network.discoveryPort = 0;
    options.network.sessionPort = 0;
    options.network.sessionBindScope = RuntimeTcpBindScope::LoopbackIpv4;
    options.network.reconnectGraceMilliseconds = static_cast<std::uint32_t>(Impl::reconnectGraceMilliseconds);
    if (!impl_->lobby.startHost(
            options.network,
            options.lobby,
            errorMessage,
            options.resumeSave.get()
        )) {
        runtimeRoomPasswordSecureClear(registrationProof);
        return false;
    }
    if (!impl_->directoryConnection.beginHost(
            options.connection,
            options.room,
            nowMilliseconds,
            errorMessage
        )) {
        impl_->lobby.close();
        runtimeRoomPasswordSecureClear(registrationProof);
        return false;
    }
    impl_->activeConfig = options.connection;
    impl_->hostRouteConfig = options.connection;
    impl_->hostProxyConfig = options.proxy;
    impl_->hostedRoom = options.room;
    impl_->hostRegistrationProof = std::move(registrationProof);
    impl_->mode = RuntimeInternetLobbyFlowMode::Hosting;
    errorMessage.clear();
    return true;
}

bool RuntimeInternetLobbyFlow::join(
    RuntimeInternetLobbyJoinOptions options,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    if (!impl_) {
        return failWith(
            errorMessage,
            "Internet lobby ownership has transferred to the running match"
        );
    }
    impl_->lastNowMilliseconds = nowMilliseconds;
    if (!impl_->preserveMatchmakingTransition) {
        impl_->clearMatchmaking();
    }
    impl_->resetActive(true);
    if (!impl_->validateAdapter(options.connection, errorMessage)) {
        runtimeRoomPasswordSecureClear(options.roomAdmissionProof);
        runtimeRoomPasswordSecureClear(options.network.password);
        return false;
    }
    const bool unexpectedPrivateLanPassword =
        !options.network.password.empty();
    runtimeRoomPasswordSecureClear(options.network.password);
    const std::string fingerprint = contentFingerprintText(
        options.network.compatibility.contentFingerprint
    );
    const std::size_t effectiveProofSize =
        !options.roomAdmissionProof.empty()
        ? options.roomAdmissionProof.size()
        : impl_->joinAdmissionProof.size();
    const bool validAdmissionProof = options.room.passwordProtected
        ? effectiveProofSize > 0U
            && effectiveProofSize
                <= runtimeInternetCoordinatorMaximumAdmissionProofBytes
        : effectiveProofSize == 0U;
    if ((options.connection.role != RuntimeInternetRole::Client
            && options.connection.role != RuntimeInternetRole::Spectator)
        || options.connection.roomId != options.room.roomId
        || options.connection.contentFingerprint
            != options.room.contentFingerprint
        || options.room.contentFingerprint != fingerprint
        || options.canonicalRoom.roomId != options.room.roomId
        || options.canonicalRoom.roomName != options.room.roomName
        || options.canonicalRoom.contentFingerprint
            != options.room.contentFingerprint
        || options.canonicalRoom.protocolVersion != runtimeLanProtocolVersion
        || options.canonicalRoom.playerCount != options.room.playerCount
        || options.canonicalRoom.maximumPlayers
            != options.room.maximumPlayers
        || options.canonicalRoom.passwordProtected
            != options.room.passwordProtected
        || options.canonicalRoom.hostName.empty()
        || options.network.peerId != options.connection.localPeerId
        || unexpectedPrivateLanPassword
        || !validAdmissionProof
        || !roleMatchesParticipation(
            options.connection.role,
            options.network.participation
        )) {
        runtimeRoomPasswordSecureClear(options.roomAdmissionProof);
        return failWith(
            errorMessage,
            "Internet join identity, role, room, or compatibility is inconsistent"
        );
    }
    auto route = std::make_unique<Impl::RouteContext>();
    route->proxyConfig = options.proxy;
    if (!route->connection.beginJoin(
            options.connection,
            nowMilliseconds,
            errorMessage
        )) {
        runtimeRoomPasswordSecureClear(options.roomAdmissionProof);
        return false;
    }
    if (!options.roomAdmissionProof.empty()) {
        runtimeRoomPasswordSecureClear(impl_->joinAdmissionProof);
        impl_->joinAdmissionProof = std::move(options.roomAdmissionProof);
    }
    impl_->activeConfig = options.connection;
    impl_->joinOptions = std::move(options);
    impl_->joinRoute = std::move(route);
    impl_->mode = RuntimeInternetLobbyFlowMode::Joining;
    errorMessage.clear();
    return true;
}

bool RuntimeInternetLobbyFlow::requestCommunityLogin(
    std::uint64_t transactionId,
    std::string& errorMessage
) {
    if (!impl_) return failWith(errorMessage, "Internet lobby ownership transferred");
    RuntimeNetworkPacket packet;
    return impl_->community.makeLoginRequest(
            transactionId,
            packet,
            errorMessage
        )
        && impl_->sendCommunityPacket(std::move(packet), errorMessage);
}

bool RuntimeInternetLobbyFlow::requestCommunityRegistration(
    std::uint64_t transactionId,
    std::string displayName,
    std::string& errorMessage
) {
    if (!impl_) return failWith(errorMessage, "Internet lobby ownership transferred");
    RuntimeNetworkPacket packet;
    return impl_->community.makeRegisterProfileRequest(
            transactionId,
            std::move(displayName),
            packet,
            errorMessage
        )
        && impl_->sendCommunityPacket(std::move(packet), errorMessage);
}

bool RuntimeInternetLobbyFlow::requestCommunityRefresh(
    std::uint64_t transactionId,
    std::string& errorMessage
) {
    if (!impl_) return failWith(errorMessage, "Internet lobby ownership transferred");
    RuntimeNetworkPacket packet;
    return impl_->community.makeRefreshRequest(
            transactionId,
            packet,
            errorMessage
        )
        && impl_->sendCommunityPacket(std::move(packet), errorMessage);
}

bool RuntimeInternetLobbyFlow::requestCommunityMatchHistory(std::uint64_t transactionId, std::string& errorMessage) {
    if (!impl_) return failWith(errorMessage, "Internet lobby ownership transferred");
    RuntimeNetworkPacket packet;
    return impl_->community.makeQuickMatchHistoryRequest(transactionId, packet, errorMessage)
        && impl_->sendCommunityPacket(std::move(packet), errorMessage);
}

bool RuntimeInternetLobbyFlow::requestCommunityChannelJoin(
    std::uint64_t transactionId,
    std::string channelId,
    std::string& errorMessage
) {
    if (!impl_) return failWith(errorMessage, "Internet lobby ownership transferred");
    RuntimeNetworkPacket packet;
    return impl_->community.makeJoinChannelRequest(
            transactionId,
            std::move(channelId),
            packet,
            errorMessage
        )
        && impl_->sendCommunityPacket(std::move(packet), errorMessage);
}

bool RuntimeInternetLobbyFlow::requestCommunityChannelMessage(
    std::uint64_t transactionId,
    std::string text,
    std::string& errorMessage
) {
    if (!impl_) return failWith(errorMessage, "Internet lobby ownership transferred");
    RuntimeNetworkPacket packet;
    return impl_->community.makeSendChannelMessageRequest(
            transactionId,
            std::move(text),
            packet,
            errorMessage
        )
        && impl_->sendCommunityPacket(std::move(packet), errorMessage);
}

bool RuntimeInternetLobbyFlow::requestCommunityFriendAdd(
    std::uint64_t transactionId,
    std::string handleOrPeerId,
    std::string& errorMessage
) {
    if (!impl_) return failWith(errorMessage, "Internet lobby ownership transferred");
    RuntimeNetworkPacket packet;
    return impl_->community.makeAddFriendRequest(
            transactionId,
            std::move(handleOrPeerId),
            packet,
            errorMessage
        )
        && impl_->sendCommunityPacket(std::move(packet), errorMessage);
}

bool RuntimeInternetLobbyFlow::requestCommunityFriendRemove(
    std::uint64_t transactionId,
    std::string peerId,
    std::string& errorMessage
) {
    if (!impl_) return failWith(errorMessage, "Internet lobby ownership transferred");
    RuntimeNetworkPacket packet;
    return impl_->community.makeRemoveFriendRequest(
            transactionId,
            std::move(peerId),
            packet,
            errorMessage
        )
        && impl_->sendCommunityPacket(std::move(packet), errorMessage);
}

bool RuntimeInternetLobbyFlow::requestCommunityClanCreate(
    std::uint64_t transactionId,
    std::string clanName,
    std::string clanTag,
    std::string& errorMessage
) {
    if (!impl_) return failWith(errorMessage, "Internet lobby ownership transferred");
    RuntimeNetworkPacket packet;
    return impl_->community.makeCreateClanRequest(
            transactionId,
            std::move(clanName),
            std::move(clanTag),
            packet,
            errorMessage
        )
        && impl_->sendCommunityPacket(std::move(packet), errorMessage);
}

bool RuntimeInternetLobbyFlow::requestCommunityClanInvite(
    std::uint64_t transactionId,
    std::string handleOrPeerId,
    std::string& errorMessage
) {
    if (!impl_) return failWith(errorMessage, "Internet lobby ownership transferred");
    RuntimeNetworkPacket packet;
    return impl_->community.makeInviteClanMemberRequest(
            transactionId,
            std::move(handleOrPeerId),
            packet,
            errorMessage
        )
        && impl_->sendCommunityPacket(std::move(packet), errorMessage);
}

bool RuntimeInternetLobbyFlow::requestCommunityClanInvitationAccept(
    std::uint64_t transactionId,
    std::string clanId,
    std::string& errorMessage
) {
    if (!impl_) return failWith(errorMessage, "Internet lobby ownership transferred");
    RuntimeNetworkPacket packet;
    return impl_->community.makeAcceptClanInvitationRequest(
            transactionId,
            std::move(clanId),
            packet,
            errorMessage
        )
        && impl_->sendCommunityPacket(std::move(packet), errorMessage);
}

bool RuntimeInternetLobbyFlow::requestCommunityClanMemberRemove(
    std::uint64_t transactionId,
    std::string peerId,
    std::string& errorMessage
) {
    if (!impl_) return failWith(errorMessage, "Internet lobby ownership transferred");
    RuntimeNetworkPacket packet;
    return impl_->community.makeRemoveClanMemberRequest(
            transactionId,
            std::move(peerId),
            packet,
            errorMessage
        )
        && impl_->sendCommunityPacket(std::move(packet), errorMessage);
}

bool RuntimeInternetLobbyFlow::requestCommunityClanLeave(
    std::uint64_t transactionId,
    std::string& errorMessage
) {
    if (!impl_) return failWith(errorMessage, "Internet lobby ownership transferred");
    RuntimeNetworkPacket packet;
    return impl_->community.makeLeaveClanRequest(
            transactionId,
            packet,
            errorMessage
        )
        && impl_->sendCommunityPacket(std::move(packet), errorMessage);
}

bool RuntimeInternetLobbyFlow::requestCommunityClanOwnershipTransfer(
    std::uint64_t transactionId,
    std::string peerId,
    std::string& errorMessage
) {
    if (!impl_) return failWith(errorMessage, "Internet lobby ownership transferred");
    RuntimeNetworkPacket packet;
    return impl_->community.makeTransferClanOwnershipRequest(
            transactionId,
            std::move(peerId),
            packet,
            errorMessage
        )
        && impl_->sendCommunityPacket(std::move(packet), errorMessage);
}

bool RuntimeInternetLobbyFlow::requestCommunityQuickMatchReport(
    std::uint64_t transactionId,
    std::string matchId,
    std::string opponentPeerId,
    RuntimeInternetCommunityQuickMatchResult result,
    std::string& errorMessage
) {
    if (!impl_) return failWith(errorMessage, "Internet lobby ownership transferred");
    RuntimeNetworkPacket packet;
    return impl_->community.makeReportQuickMatchRequest(
            transactionId,
            std::move(matchId),
            std::move(opponentPeerId),
            result,
            packet,
            errorMessage
        )
        && impl_->sendCommunityPacket(std::move(packet), errorMessage);
}

RuntimeInternetLobbyFlowPollResult RuntimeInternetLobbyFlow::poll(
    std::uint64_t nowMilliseconds,
    int transportTimeoutMilliseconds
) {
    RuntimeInternetLobbyFlowPollResult result;
    if (!impl_) {
        result.succeeded = false;
        result.error =
            "Internet lobby ownership has transferred to the running match";
        return result;
    }
    impl_->lastNowMilliseconds = nowMilliseconds;
    if (!impl_->service) {
        impl_->terminalFailure(
            result,
            "Internet authenticated service adapter is missing"
        );
        return result;
    }
    if (impl_->mode == RuntimeInternetLobbyFlowMode::Idle
        || impl_->mode == RuntimeInternetLobbyFlowMode::Closed) {
        impl_->terminalFailure(result, "Internet lobby flow is not active");
        return result;
    }
    if (impl_->mode == RuntimeInternetLobbyFlowMode::Kicked) {
        return result;
    }
    if (impl_->mode == RuntimeInternetLobbyFlowMode::Failed) {
        impl_->terminalFailure(result, "Internet lobby flow has failed");
        return result;
    }
    if (impl_->mode == RuntimeInternetLobbyFlowMode::Matchmaking) {
        RuntimeNetworkPacket heartbeat;
        bool hasHeartbeat = false;
        std::string heartbeatError;
        if (!impl_->matchmaking.poll(
                nowMilliseconds,
                heartbeat,
                hasHeartbeat,
                heartbeatError
            )
            || (hasHeartbeat
                && !impl_->service->sendCoordinator(
                    heartbeat,
                    heartbeatError
                ))) {
            impl_->terminalFailure(result, std::move(heartbeatError));
            return result;
        }
    }
    std::vector<RuntimeInternetServiceEvent> serviceEvents =
        impl_->service->poll(transportTimeoutMilliseconds);
    if (!impl_->processServiceEvents(
            std::move(serviceEvents),
            nowMilliseconds,
            result
        )) {
        return result;
    }
    if (impl_->matchTransitionPending) {
        const RuntimeInternetMatchmakingAssignment* assignment =
            impl_->matchmaking.assignment();
        if (!assignment || !impl_->matchmakingOptions) {
            impl_->terminalFailure(
                result,
                "matchmaking assignment transition lost its templates"
            );
            return result;
        }
        RuntimeInternetLobbyMatchmakingOptions templates =
            *impl_->matchmakingOptions;
        const std::string roomName = matchmakingRoomName(
            assignment->matchId
        );
        const std::uint8_t maximumPlayers = static_cast<std::uint8_t>(
            assignment->peerIds.size()
        );
        bool transitioned = false;
        impl_->preserveMatchmakingTransition = true;
        if (assignment->hostPeerId
            == templates.matchmakingClient.localPeerId) {
            RuntimeInternetLobbyHostOptions host =
                std::move(templates.hostTemplate);
            host.connection.serviceId =
                templates.matchmakingClient.serviceId;
            host.connection.localPeerId =
                templates.matchmakingClient.localPeerId;
            host.connection.roomId = assignment->roomId;
            host.connection.role = RuntimeInternetRole::Host;
            host.connection.contentFingerprint =
                templates.request.contentFingerprint;
            host.room.roomId = assignment->roomId;
            host.room.roomName = roomName;
            host.room.hostPeerId = assignment->hostPeerId;
            host.room.contentFingerprint =
                templates.request.contentFingerprint;
            host.room.playerCount = 1U;
            host.room.maximumPlayers = maximumPlayers;
            host.room.passwordProtected = true;
            host.network.session.roomId = assignment->roomId;
            host.network.session.hostPeerId = assignment->hostPeerId;
            host.network.session.hostDisplayName =
                assignment->hostDisplayName;
            host.network.session.maximumMembers = assignment->peerIds.size();
            host.network.roomName = roomName;
            host.network.passwordProtected = false;
            host.lobby.hostMember.peerId = assignment->hostPeerId;
            host.lobby.hostMember.displayName =
                assignment->hostDisplayName;
            host.lobby.hostMember.role =
                RuntimeMultiplayerRoomMemberRole::Host;
            host.lobby.hostMember.participation =
                RuntimeMultiplayerParticipationRole::Combatant;
            transitioned = startHost(
                std::move(host),
                nowMilliseconds,
                result.error
            );
        } else {
            RuntimeInternetLobbyJoinOptions joining =
                std::move(templates.joinTemplate);
            joining.connection.serviceId =
                templates.matchmakingClient.serviceId;
            joining.connection.localPeerId =
                templates.matchmakingClient.localPeerId;
            joining.connection.roomId = assignment->roomId;
            joining.connection.role = RuntimeInternetRole::Client;
            joining.connection.contentFingerprint =
                templates.request.contentFingerprint;
            joining.room.roomId = assignment->roomId;
            joining.room.roomName = roomName;
            joining.room.hostPeerId = assignment->hostPeerId;
            joining.room.contentFingerprint =
                templates.request.contentFingerprint;
            joining.room.playerCount = 1U;
            joining.room.maximumPlayers = maximumPlayers;
            joining.room.passwordProtected = true;
            joining.canonicalRoom.roomId = assignment->roomId;
            joining.canonicalRoom.roomName = roomName;
            joining.canonicalRoom.hostName =
                assignment->hostDisplayName;
            joining.canonicalRoom.hostAddress = "authenticated-relay";
            joining.canonicalRoom.contentFingerprint =
                templates.request.contentFingerprint;
            joining.canonicalRoom.protocolVersion = runtimeLanProtocolVersion;
            joining.canonicalRoom.sessionPort = 1U;
            joining.canonicalRoom.playerCount = 1U;
            joining.canonicalRoom.maximumPlayers = maximumPlayers;
            joining.canonicalRoom.passwordProtected = true;
            joining.network.room = joining.canonicalRoom;
            joining.network.peerId =
                templates.matchmakingClient.localPeerId;
            joining.network.participation =
                RuntimeMultiplayerParticipationRole::Combatant;
            impl_->joinAdmissionProof = assignment->admissionProof;
            transitioned = join(
                std::move(joining),
                nowMilliseconds,
                result.error
            );
        }
        impl_->preserveMatchmakingTransition = false;
        if (!transitioned) {
            impl_->terminalFailure(
                result,
                result.error.empty()
                    ? "matchmaking assignment could not enter its room"
                    : result.error
            );
            return result;
        }
        impl_->matchTransitionPending = false;
    }
    if ((impl_->mode == RuntimeInternetLobbyFlowMode::Running && !impl_->pumpRecovery(nowMilliseconds, result))
        || !impl_->dispatchAllActions(nowMilliseconds, result)
        || !impl_->startReadyProxies(nowMilliseconds, result)
        || !impl_->pollProxies(nowMilliseconds, result)
        || !impl_->pollLobby(0, result)) {
        return result;
    }
    // Canonical Started is observed during pollLobby(), after the normal
    // coordinator dispatch phase. Dispatch once more so SealRoom leaves in
    // this same public poll; Running remains gated on its later RoomSealed.
    if (!impl_->dispatchAllActions(nowMilliseconds, result)) return result;
    return result;
}

RuntimeInternetLobbyFlowMode RuntimeInternetLobbyFlow::mode() const {
    return impl_ ? impl_->mode : RuntimeInternetLobbyFlowMode::Closed;
}

const std::vector<RuntimeInternetRoomRecord>&
RuntimeInternetLobbyFlow::rooms() const {
    static const std::vector<RuntimeInternetRoomRecord> empty;
    return impl_ ? impl_->roomCache : empty;
}

std::size_t RuntimeInternetLobbyFlow::routeCount() const {
    return impl_
        ? impl_->hostRoutes.size() + (impl_->joinRoute ? 1U : 0U)
        : 0U;
}

std::size_t RuntimeInternetLobbyFlow::gameplaySessionCount() const {
    if (!impl_) return 0U;
    std::size_t count = 0;
    if (impl_->joinRoute && impl_->joinRoute->gameplayReported) ++count;
    count += static_cast<std::size_t>(std::count_if(
        impl_->hostRoutes.begin(),
        impl_->hostRoutes.end(),
        [](const std::unique_ptr<Impl::RouteContext>& route) {
            return route->gameplayReported;
        }
    ));
    return count;
}

RuntimeInternetConnectionState
RuntimeInternetLobbyFlow::primaryConnectionState() const {
    if (!impl_) return RuntimeInternetConnectionState::Closed;
    return impl_->joinRoute
        ? impl_->joinRoute->connection.state()
        : impl_->directoryConnection.state();
}

const RuntimeInternetMatchmakingAssignment*
RuntimeInternetLobbyFlow::matchAssignment() const {
    return impl_ ? impl_->matchmaking.assignment() : nullptr;
}

const RuntimeInternetCommunitySnapshot&
RuntimeInternetLobbyFlow::communitySnapshot() const {
    static const RuntimeInternetCommunitySnapshot empty;
    return impl_ ? impl_->community.snapshot() : empty;
}

const std::vector<RuntimeInternetRankedRecord>& RuntimeInternetLobbyFlow::communityMatchHistory() const {
    static const std::vector<RuntimeInternetRankedRecord> empty;
    return impl_ ? impl_->community.matchHistory() : empty;
}

RuntimeMultiplayerLobbyFlow* RuntimeInternetLobbyFlow::lobbyFlow() {
    return impl_ ? &impl_->lobby : nullptr;
}

const RuntimeMultiplayerLobbyFlow* RuntimeInternetLobbyFlow::lobbyFlow() const {
    return impl_ ? &impl_->lobby : nullptr;
}

std::unique_ptr<RuntimeMultiplayerRuntimeBridge>
RuntimeInternetLobbyFlow::takeRunningBridge(std::string& errorMessage) {
    if (!impl_) {
        failWith(
            errorMessage,
            "Internet lobby ownership has already transferred to the running match"
        );
        return nullptr;
    }
    if (impl_->mode != RuntimeInternetLobbyFlowMode::Running) {
        failWith(
            errorMessage,
            "Internet runtime bridge is available only after the match starts"
        );
        return nullptr;
    }

    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> bridge =
        impl_->lobby.takeRunningBridge(errorMessage);
    if (!bridge) return nullptr;

    impl_->detachedRunningBridge = bridge.get();
    impl_->runningClockBaseMilliseconds = impl_->lastNowMilliseconds;
    impl_->runningClockBaseSteady = std::chrono::steady_clock::now();
    impl_->runningClockInitialized = true;
    auto carrier = std::make_unique<Impl::RunningOrderedTransport>(
        std::move(impl_)
    );
    if (!bridge->adoptExternalOrderedTransport(
            std::move(carrier),
            errorMessage
        )) {
        bridge->lan().close();
        return nullptr;
    }
    errorMessage.clear();
    return bridge;
}

void RuntimeInternetLobbyFlow::close() noexcept {
    if (!impl_) return;
    try {
        impl_->resetActive(false);
        impl_->clearMatchmaking();
        impl_->community.close();
        if (impl_->service) impl_->service->close();
        impl_->mode = RuntimeInternetLobbyFlowMode::Closed;
    } catch (...) {
        impl_->mode = RuntimeInternetLobbyFlowMode::Closed;
    }
}
