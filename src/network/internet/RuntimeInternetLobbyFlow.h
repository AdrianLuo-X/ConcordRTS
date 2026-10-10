#pragma once

#include "network/internet/RuntimeInternetCommunity.h"
#include "network/transport/RuntimeInternetMatchmakingProtocol.h"
#include "network/transport/RuntimeInternetTcpTunnelProxy.h"
#include "network/lobby/RuntimeMultiplayerLobbyFlow.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The authenticated service adapter is deliberately narrower than a socket
// abstraction.  The Internet gameplay path is a reliable TCP byte stream, so
// the flow never asks an embedding application to open an unauthenticated UDP
// gameplay route.
struct RuntimeInternetServiceTcpOpenRequest {
    RuntimeInternetTcpRouteDescriptor route;
    RuntimeInternetEndpoint endpoint;
    RuntimeNetworkPacket relayBindPacket;
};

enum class RuntimeInternetServiceEventKind : std::uint8_t {
    CoordinatorPacket,
    RelayPacket,
    TcpStreamOpened,
    Failure,
    CoordinatorAuthenticated
};

struct RuntimeInternetServiceEvent {
    RuntimeInternetServiceEventKind kind =
        RuntimeInternetServiceEventKind::Failure;
    // These identities must come from the authenticated channel, never from
    // fields inside packet.  authenticatedPeerId is the local account to
    // which the event was delivered.
    std::string authenticatedServiceId;
    std::string authenticatedPeerId;
    RuntimeInternetEndpoint source;
    RuntimeNetworkPacket packet;
    std::uint64_t connectionId = 0;
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> tcpStream;
    std::string reason;

    RuntimeInternetServiceEvent() = default;
    RuntimeInternetServiceEvent(RuntimeInternetServiceEvent&&) noexcept =
        default;
    RuntimeInternetServiceEvent& operator=(
        RuntimeInternetServiceEvent&&
    ) noexcept = default;
    RuntimeInternetServiceEvent(const RuntimeInternetServiceEvent&) = delete;
    RuntimeInternetServiceEvent& operator=(
        const RuntimeInternetServiceEvent&
    ) = delete;
};

class RuntimeInternetServiceAdapter {
public:
    virtual ~RuntimeInternetServiceAdapter() = default;

    virtual std::string_view authenticatedServiceId() const noexcept = 0;
    virtual std::string_view authenticatedPeerId() const noexcept = 0;
    virtual bool sendCoordinator(
        const RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) = 0;
    // Host password verifiers and password/matchmaking join proofs are
    // authenticated outer-channel metadata, not part of the legacy packet.
    // Existing adapters may keep only sendCoordinator(); protected ordinary
    // registration/join then fails closed until this is overridden.
    virtual bool sendCoordinatorWithAdmissionProof(
        const RuntimeNetworkPacket& packet,
        std::span<const std::uint8_t> roomAdmissionProof,
        std::string& errorMessage
    ) {
        if (!roomAdmissionProof.empty()) {
            errorMessage =
                "Internet service adapter does not support room admission proof metadata";
            return false;
        }
        return sendCoordinator(packet, errorMessage);
    }
    virtual bool openAuthenticatedTcpRelay(
        const RuntimeInternetServiceTcpOpenRequest& request,
        std::string& errorMessage
    ) = 0;
    // Reuses the configured account, but performs a fresh TLS handshake and
    // nonce-bound application authentication. Existing adapters fail closed.
    virtual bool reauthenticateCoordinator(std::string& errorMessage) {
        errorMessage = "Internet adapter does not support coordinator reconnect";
        return false;
    }
    virtual std::vector<RuntimeInternetServiceEvent> poll(
        int timeoutMilliseconds
    ) = 0;
    virtual void close() noexcept = 0;
};

enum class RuntimeInternetLobbyFlowMode : std::uint8_t {
    Idle,
    Matchmaking,
    QueryingRooms,
    RoomListReady,
    Hosting,
    Joining,
    Lobby,
    Running,
    Kicked,
    Failed,
    Closed
};

enum class RuntimeInternetLobbyFlowEventKind : std::uint8_t {
    RoomsUpdated,
    MatchQueued,
    MatchAssigned,
    MatchActivated,
    RoomRegistered,
    PeerRouteOffered,
    TcpTunnelBound,
    JoinAccepted,
    JoinRejected,
    LobbyChanged,
    ReadyChanged,
    MatchStarted,
    Kicked,
    PeerDisconnected,
    Failed
};

struct RuntimeInternetLobbyFlowEvent {
    RuntimeInternetLobbyFlowEventKind kind =
        RuntimeInternetLobbyFlowEventKind::Failed;
    std::string peerId;
    RuntimeInternetRole role = RuntimeInternetRole::Client;
    std::uint64_t connectionId = 0;
    std::string reason;
};

struct RuntimeInternetLobbyFlowPollResult {
    bool succeeded = true;
    bool roomsChanged = false;
    bool lobbyChanged = false;
    bool matchStarted = false;
    bool communityChanged = false;
    std::vector<RuntimeInternetLobbyFlowEvent> events;
    std::vector<RuntimeInternetCommunityClientEvent> communityEvents;
    std::vector<RuntimeMultiplayerLanEvent> lobbyEvents;
    std::string error;
};

struct RuntimeInternetLobbyQueryOptions {
    RuntimeInternetConnectionConfig connection;
};

struct RuntimeInternetLobbyHostOptions {
    RuntimeInternetConnectionConfig connection;
    RuntimeInternetRoomRecord room;
    // Optional 32-byte password verifier, sent only as authenticated TLS
    // metadata on RegisterRoom. Matchmaking rooms keep this empty and use the
    // service's existing assignment authority instead.
    std::vector<std::uint8_t> roomPasswordVerifier;
    RuntimeMultiplayerLanHostOptions network;
    RuntimeMultiplayerLobbyHostCreateOptions lobby;
    RuntimeInternetTcpTunnelProxyConfig proxy;
    std::shared_ptr<const RuntimeMultiplayerResumeSave> resumeSave;
};

struct RuntimeInternetLobbyJoinOptions {
    RuntimeInternetConnectionConfig connection;
    RuntimeInternetRoomRecord room;
    // Password proof bound to room, authenticated peer, and join transaction.
    // It is never encoded in the public directory/control packet.
    std::vector<std::uint8_t> roomAdmissionProof;
    // Directory records intentionally omit LAN protocol/display metadata.
    // The authenticated matchmaking service (or signed room metadata) must
    // provide this canonical record; the flow only replaces address/port with
    // its one-shot loopback proxy endpoint.
    RuntimeLanRoomAdvertisement canonicalRoom;
    RuntimeMultiplayerLanJoinOptions network;
    RuntimeInternetTcpTunnelProxyConfig proxy;
};

struct RuntimeInternetLobbyMatchmakingOptions {
    RuntimeInternetMatchmakingClientConfig matchmakingClient;
    RuntimeInternetMatchmakingRequest request;
    std::uint64_t activationTransactionId = 0;
    // Both templates are supplied because host selection is authoritative and
    // unknown until the service assigns the match. Room/host/role identities
    // are overwritten from the authenticated assignment before validation.
    RuntimeInternetLobbyHostOptions hostTemplate;
    RuntimeInternetLobbyJoinOptions joinTemplate;
};

// High-level Internet room/browser orchestration.  Coordinator and relay I/O
// are injected, while canonical lobby authority remains in the existing LAN
// session layer behind one loopback TCP proxy per authenticated remote route.
class RuntimeInternetLobbyFlow {
public:
    explicit RuntimeInternetLobbyFlow(
        std::unique_ptr<RuntimeInternetServiceAdapter> serviceAdapter,
        RuntimeMultiplayerLockstepConfig lockstepConfig = {}
    );
    ~RuntimeInternetLobbyFlow();

    RuntimeInternetLobbyFlow(RuntimeInternetLobbyFlow&&) noexcept;
    RuntimeInternetLobbyFlow& operator=(
        RuntimeInternetLobbyFlow&&
    ) noexcept;
    RuntimeInternetLobbyFlow(const RuntimeInternetLobbyFlow&) = delete;
    RuntimeInternetLobbyFlow& operator=(
        const RuntimeInternetLobbyFlow&
    ) = delete;

    bool beginRoomQuery(
        RuntimeInternetLobbyQueryOptions options,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    bool beginMatchmaking(
        RuntimeInternetLobbyMatchmakingOptions options,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    bool startHost(
        RuntimeInternetLobbyHostOptions options,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    bool join(
        RuntimeInternetLobbyJoinOptions options,
        std::uint64_t nowMilliseconds,
        std::string& errorMessage
    );
    bool requestCommunityLogin(
        std::uint64_t transactionId,
        std::string& errorMessage
    );
    bool requestCommunityRegistration(
        std::uint64_t transactionId,
        std::string displayName,
        std::string& errorMessage
    );
    bool requestCommunityRefresh(
        std::uint64_t transactionId,
        std::string& errorMessage
    );
    bool requestCommunityChannelJoin(
        std::uint64_t transactionId,
        std::string channelId,
        std::string& errorMessage
    );
    bool requestCommunityChannelMessage(
        std::uint64_t transactionId,
        std::string text,
        std::string& errorMessage
    );
    bool requestCommunityFriendAdd(
        std::uint64_t transactionId,
        std::string handleOrPeerId,
        std::string& errorMessage
    );
    bool requestCommunityFriendRemove(
        std::uint64_t transactionId,
        std::string peerId,
        std::string& errorMessage
    );
    bool requestCommunityClanCreate(
        std::uint64_t transactionId,
        std::string clanName,
        std::string clanTag,
        std::string& errorMessage
    );
    bool requestCommunityClanInvite(
        std::uint64_t transactionId,
        std::string handleOrPeerId,
        std::string& errorMessage
    );
    bool requestCommunityClanInvitationAccept(
        std::uint64_t transactionId,
        std::string clanId,
        std::string& errorMessage
    );
    bool requestCommunityClanMemberRemove(
        std::uint64_t transactionId,
        std::string peerId,
        std::string& errorMessage
    );
    bool requestCommunityClanLeave(
        std::uint64_t transactionId,
        std::string& errorMessage
    );
    bool requestCommunityClanOwnershipTransfer(
        std::uint64_t transactionId,
        std::string peerId,
        std::string& errorMessage
    );
    bool requestCommunityQuickMatchReport(
        std::uint64_t transactionId,
        std::string matchId,
        std::string opponentPeerId,
        RuntimeInternetCommunityQuickMatchResult result,
        std::string& errorMessage
    );
    RuntimeInternetLobbyFlowPollResult poll(
        std::uint64_t nowMilliseconds,
        int transportTimeoutMilliseconds = 0
    );

    RuntimeInternetLobbyFlowMode mode() const;
    const std::vector<RuntimeInternetRoomRecord>& rooms() const;
    std::size_t routeCount() const;
    std::size_t gameplaySessionCount() const;
    RuntimeInternetConnectionState primaryConnectionState() const;
    const RuntimeInternetMatchmakingAssignment* matchAssignment() const;
    const RuntimeInternetCommunitySnapshot& communitySnapshot() const;
    const std::vector<RuntimeInternetRankedRecord>& communityMatchHistory() const;
    bool requestCommunityMatchHistory(std::uint64_t transactionId, std::string& errorMessage);

    RuntimeMultiplayerLobbyFlow* lobbyFlow();
    const RuntimeMultiplayerLobbyFlow* lobbyFlow() const;
    // Moves the canonical LAN session together with the authenticated service,
    // TCP relay streams, and loopback tunnel proxies into the runtime bridge.
    // The flow becomes Closed and may be destroyed immediately afterward.
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> takeRunningBridge(
        std::string& errorMessage
    );

    void close() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
