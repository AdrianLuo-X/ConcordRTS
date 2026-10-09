#pragma once

#include "RuntimeMultiplayerProtocol.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

enum class RuntimeMultiplayerLanRole : std::uint8_t {
    None,
    Host,
    Client
};

enum class RuntimeMultiplayerLanPhase : std::uint8_t {
    Inactive,
    Browsing,
    Joining,
    Lobby,
    Running,
    Reconnecting,
    Aborted,
    JoinRejected,
    Kicked,
    Disconnected
};

// A client stops canonical application while a simulation TickAdvance is
// deferred, but it continues draining the bounded transport receive queue.
// These limits bound the retained FIFO independently of SessionState command
// history and the TCP transport's own send/receive bounds.
inline constexpr std::size_t
    runtimeMultiplayerLanMaximumDeferredCanonicalPackets = 1024U;
inline constexpr std::size_t
    runtimeMultiplayerLanMaximumDeferredCanonicalBytes =
        4U * 1024U * 1024U;
inline constexpr std::uint32_t
    runtimeMultiplayerLanDefaultReconnectGraceMilliseconds = 10000U;
inline constexpr std::uint32_t
    runtimeMultiplayerLanReconnectRetryMilliseconds = 50U;

enum class RuntimeMultiplayerLanEventKind : std::uint8_t {
    RoomsUpdated,
    TransportConnected,
    JoinAccepted,
    JoinRejected,
    LobbyChanged,
    MemberLeft,
    ReadyChanged,
    Started,
    CommandAccepted,
    AuthoritativeTickAdvanced,
    StateHashAccepted,
    SaveLoadUpdated,
    ReconnectPending,
    Reconnected,
    ReconnectFailed,
    SessionAborted,
    Disconnected,
    ProtocolRejected,
    TransportError,
    LobbySlotOptionsRequested,
    LobbySlotOptionsResult,
    Kicked
};

struct RuntimeMultiplayerLanEvent {
    RuntimeMultiplayerLanEventKind kind =
        RuntimeMultiplayerLanEventKind::TransportError;
    RuntimeMultiplayerMessageType messageType =
        RuntimeMultiplayerMessageType::JoinRequest;
    std::string peerId;
    std::string reason;
    int tick = -1;
    std::uint64_t sequence = 0;
    std::optional<RuntimeMultiplayerLobbySlotOptionsRequestMessage>
        lobbySlotOptionsRequest;
    std::optional<RuntimeMultiplayerLobbySlotOptionsResultMessage>
        lobbySlotOptionsResult;

    bool operator==(const RuntimeMultiplayerLanEvent&) const = default;
};

struct RuntimeMultiplayerLanPollResult {
    bool succeeded = true;
    // True when a canonical TickAdvance is retained because simulation input
    // or application is not ready. This is normal back-pressure, not error.
    bool simulationDeferred = false;
    std::vector<RuntimeMultiplayerLanEvent> events;
    std::string error;
};

struct RuntimeMultiplayerLanOperationResult {
    bool succeeded = false;
    // Host operations are applied immediately. Client operations are queued
    // and become applied only after the canonical host echo is received.
    bool applied = false;
    bool queued = false;
    bool duplicate = false;
    bool deferred = false;
    std::uint64_t requestId = 0;
    std::uint64_t setupRevision = 0;
    std::string reason;
};

struct RuntimeMultiplayerLanHostOptions {
    RuntimeMultiplayerSessionConfig session;
    // Role-aware admission limits are enforced before JoinAccepted. They are
    // host transport policy rather than deterministic running-world state.
    // maximumMembers remains the hard combined roster ceiling.
    std::size_t maximumCombatants = runtimeMultiplayerMaximumCombatants;
    std::size_t maximumSpectators = runtimeMultiplayerMaximumSpectators;
    std::string roomName;
    std::uint16_t discoveryPort = runtimeLanDefaultDiscoveryPort;
    // Zero requests an ephemeral TCP port and publishes the bound port.
    std::uint16_t sessionPort = runtimeLanDefaultSessionPort;
    bool passwordProtected = false;
    // Present only while startHost validates and derives the room verifier;
    // the raw password is cleared before any socket is opened.
    std::string password;
    // Internet transports can keep the session server private and publish a
    // separately authenticated tunnel endpoint. LAN remains discoverable and
    // listens on all IPv4 interfaces by default.
    bool enableDiscovery = true;
    RuntimeTcpBindScope sessionBindScope = RuntimeTcpBindScope::AnyIpv4;
    // Tests and restored rooms may supply a stable nonzero generation. A
    // fresh unpredictable value is generated when this is zero.
    std::uint64_t roomGeneration = 0;
    // Running combatants keep their canonical roster entry for this bounded
    // transport-only grace period. Zero restores immediate fail-fast abort.
    // A successful socket rebind is still paused until a coordinated Load
    // snapshot changes the timeline epoch for every participant.
    std::uint32_t reconnectGraceMilliseconds =
        runtimeMultiplayerLanDefaultReconnectGraceMilliseconds;
};

struct RuntimeMultiplayerLanJoinOptions {
    RuntimeLanRoomAdvertisement room;
    std::string peerId;
    std::string displayName;
    RuntimeMultiplayerParticipationRole participation =
        RuntimeMultiplayerParticipationRole::Combatant;
    RuntimeMultiplayerCompatibility compatibility;
    int connectTimeoutMilliseconds = 5000;
    // Used only for ordinary LAN/direct admission. Authenticated Internet
    // loopback proxies set localProxyPrelude and have already proved access at
    // the coordinator, so they deliberately bypass this second challenge.
    std::string password;
    // Same-machine Internet proxy authentication. Empty for ordinary LAN and
    // direct TCP. These bytes are stripped by the loopback proxy before the
    // first RuntimeNetworkPacket frame reaches the remote host.
    std::vector<std::uint8_t> localProxyPrelude;
};

// Optional deterministic, side-effect-free application validation for the
// otherwise opaque command payload. LAN bounds the envelope before invoking
// this callback, and invokes it independently on every receiving peer.
using RuntimeMultiplayerLanCommandPayloadValidator = std::function<bool(
    const RuntimeMultiplayerCommandPacket&,
    std::string&
)>;

enum class RuntimeMultiplayerLanSimulationAdvanceDecision : std::uint8_t {
    Ready,
    Deferred,
    Rejected
};

// Installed as one complete set before hosting/joining. Hooks run synchronously
// in canonical packet order, including when several packets arrive in one
// transport poll. Callbacks must be deterministic and transaction-safe.
struct RuntimeMultiplayerLanSimulationHooks {
    // Runs immediately after Start enters SessionState::Running and before the
    // next canonical packet, allowing a read-only lockstep driver to bind.
    std::function<bool(
        const RuntimeMultiplayerSessionState&,
        std::string&
    )> onStarted;
    // Runs after the exact command envelope is admitted, while it is still
    // retained by SessionState, and before any following canonical packet.
    std::function<bool(
        const RuntimeMultiplayerCommandPacket&,
        std::string&
    )> onCommandAdmitted;
    // completedTick is authoritativeTick - 1. LAN first proves the complete
    // Session transition on a copy, then calls this before committing it.
    // Deferred retains the packet without changing SessionState and stops
    // later canonical packet handling.
    std::function<RuntimeMultiplayerLanSimulationAdvanceDecision(
        int completedTick,
        std::string&
    )> prepareAuthoritativeAdvance;
    // Runs immediately after SessionState advances. It must perform only the
    // infallible/idempotent acknowledgement of the already prepared advance
    // (for example, driver ack). False or an exception is fatal: the peer
    // aborts instead of rolling SessionState back behind an applied world tick.
    std::function<bool(int completedTick, std::string&)>
        onAuthoritativeAdvance;

    bool empty() const;
    bool complete() const;
};

// Installed as one complete set before hosting/joining. Snapshot bytes are
// never exposed until the transfer assembler has verified the descriptor's
// exact byte count, fingerprint, and SHA-256 digest.
struct RuntimeMultiplayerLanSnapshotHooks {
    std::function<bool(
        const RuntimeMultiplayerSnapshotDescriptor&,
        std::span<const std::uint8_t>,
        std::uint64_t& observedAuthoritativeStateHash,
        std::string&
    )> stageVerifiedBytes;
    std::function<bool(
        const RuntimeMultiplayerSnapshotDescriptor&,
        std::uint64_t& observedAuthoritativeStateHash,
        std::string&
    )> commitStaged;
    // Invoked after a successful Load Session transition. It is the explicit
    // lockstep rebind seam and is not invoked for Save.
    std::function<bool(
        const RuntimeMultiplayerSnapshotDescriptor&,
        const RuntimeMultiplayerSessionState&,
        std::string&
    )> postSessionCommit;
    std::function<void(
        const RuntimeMultiplayerSnapshotDescriptor&
    )> discardStaged;

    bool empty() const;
    bool complete() const;
};

// Production LAN orchestration for one room participant. It owns discovery,
// TCP transport, admission/binding, and the authoritative or replicated
// RuntimeMultiplayerSessionState. Payload interpretation and simulation remain
// optional callbacks, while their ordering relative to canonical admission is
// enforced here when validators/hooks are installed.
class RuntimeMultiplayerLanSession {
public:
    bool setLobbyMap(RuntimeMultiplayerMatchMap map, std::vector<std::uint8_t> bytes, std::string& error);
    bool requestLobbyMap(const RuntimeMultiplayerMatchMap& map, std::string& error);
    const RuntimeMultiplayerMapOffer* lobbyMapOffer() const;
    const std::vector<std::uint8_t>& lobbyMapBytes() const;
    bool lobbyMapTransfersComplete() const;
    RuntimeMultiplayerLanSession();
    ~RuntimeMultiplayerLanSession();
    RuntimeMultiplayerLanSession(RuntimeMultiplayerLanSession&&) noexcept;
    RuntimeMultiplayerLanSession& operator=(
        RuntimeMultiplayerLanSession&&
    ) noexcept;
    RuntimeMultiplayerLanSession(
        const RuntimeMultiplayerLanSession&
    ) = delete;
    RuntimeMultiplayerLanSession& operator=(
        const RuntimeMultiplayerLanSession&
    ) = delete;

    // Configure before hosting or joining. An empty callback restores opaque
    // transport behavior. The validator persists across close/start cycles.
    bool setCommandPayloadValidator(
        RuntimeMultiplayerLanCommandPayloadValidator validator,
        std::string& errorMessage
    );
    // Configure before hosting/joining. Empty restores opaque orchestration;
    // otherwise all four callbacks are required. Like the payload validator,
    // hooks persist across close/start cycles until explicitly replaced.
    bool setSimulationHooks(
        RuntimeMultiplayerLanSimulationHooks hooks,
        std::string& errorMessage
    );
    bool setSnapshotHooks(
        RuntimeMultiplayerLanSnapshotHooks hooks,
        std::string& errorMessage
    );

    bool startHost(
        RuntimeMultiplayerLanHostOptions options,
        std::string& errorMessage
    );
    bool startBrowsing(
        std::uint16_t discoveryPort,
        std::string& errorMessage
    );
    bool sendDiscoveryProbe(
        const std::string& targetAddress,
        std::string& errorMessage
    );
    std::vector<RuntimeLanRoomAdvertisement> discoveredRooms(
        std::uint64_t maximumAgeMilliseconds = 5000
    ) const;
    bool join(
        RuntimeMultiplayerLanJoinOptions options,
        std::string& errorMessage
    );

    RuntimeMultiplayerLanPollResult poll(int timeoutMilliseconds = 0);

    RuntimeMultiplayerLanOperationResult setMatchSetup(
        RuntimeMultiplayerMatchSetup setup
    );
    RuntimeMultiplayerLanOperationResult requestLobbySlotOptions(
        std::size_t slotIndex,
        std::string countryId,
        std::string colorId,
        std::string teamId,
        std::string difficultyId,
        std::uint8_t startPosition
    );
    // Low-level request entry retained for protocol/security tests and
    // non-UI adapters. The host still binds peerId to the authenticated TCP
    // connection and enforces request monotonicity and slot ownership.
    RuntimeMultiplayerLanOperationResult sendLobbySlotOptionsRequest(
        RuntimeMultiplayerLobbySlotOptionsRequestMessage request
    );
    RuntimeMultiplayerLanOperationResult resolveLobbySlotOptionsRequest(
        const std::string& peerId,
        std::uint64_t requestId,
        bool accepted,
        std::string reason
    );
    RuntimeMultiplayerLanOperationResult setReady(bool ready);
    RuntimeMultiplayerLanOperationResult startMatch(int startTick);
    RuntimeMultiplayerLanOperationResult submitCommand(
        int tick,
        std::uint64_t sequence,
        std::vector<std::uint8_t> payload
    );
    RuntimeMultiplayerLanOperationResult advanceAuthoritativeTick(int tick);
    RuntimeMultiplayerLanOperationResult submitStateHash(
        int tick,
        std::uint64_t stateHash
    );
    RuntimeMultiplayerLanOperationResult proposeSaveLoad(
        RuntimeMultiplayerSaveLoadProposal proposal
    );
    RuntimeMultiplayerLanOperationResult proposeSaveLoad(
        RuntimeMultiplayerSaveLoadProposal proposal,
        std::vector<std::uint8_t> bytes
    );
    RuntimeMultiplayerLanOperationResult respondToSaveLoad(
        std::uint64_t proposalId,
        bool approved,
        std::uint64_t observedFingerprint
    );
    RuntimeMultiplayerLanOperationResult abortSaveLoad(
        std::uint64_t proposalId
    );
    // Lobby clients may leave normally. During a running match this is
    // intentionally limited to spectators; combatant loss remains a
    // deterministic host-coordinated session abort.
    RuntimeMultiplayerLanOperationResult leave();
    // A running host may evict only a spectator. The host reuses the same
    // canonical Leave/reconciliation path as an observed spectator
    // disconnect; combatants can never be removed through this API.
    RuntimeMultiplayerLanOperationResult dropRunningSpectator(
        const std::string& peerId,
        std::string reason = "spectator was removed by the host"
    );
    RuntimeMultiplayerLanOperationResult abortRunningSession(
        const std::string& affectedPeerId,
        RuntimeMultiplayerSessionAbortReason reason
    );
    RuntimeMultiplayerLanOperationResult kickPeer(
        const std::string& peerId,
        std::string reason = "removed by the host"
    );

    // The normal path enters reconnect automatically after an unexpected TCP
    // loss. This explicit interruption is a deterministic test/control seam;
    // it closes only the client transport and preserves canonical state.
    bool interruptClientTransportForReconnect(
        std::string& errorMessage
    );
    // True while the host is retaining a disconnected combatant, or after a
    // socket rebind while the required coordinated recovery Load is pending.
    bool reconnectPending() const;
    // Only the authenticated carrier may replace this one-shot loopback
    // endpoint. The canonical room, player and resume ticket remain intact.
    bool replaceAuthenticatedReconnectEndpoint(
        std::uint16_t loopbackPort, std::vector<std::uint8_t> prelude, std::string& errorMessage);
    bool reconnectReadyForRecovery() const;
    bool setLobbyResumeSnapshot(RuntimeMultiplayerSnapshotDescriptor descriptor,
        std::vector<std::uint8_t> bytes, std::string& errorMessage);
    const RuntimeMultiplayerSnapshotDescriptor* lobbyResumeDescriptor() const;
    bool lobbyResumeSnapshotReady() const;
    bool lobbyResumeAllPeersReady() const;
    const std::vector<std::uint8_t>& lobbyResumeSnapshotBytes() const;
    bool markReconnectRecoveryProposal(
        std::uint64_t proposalId,
        std::string& errorMessage
    );

    void close();

    RuntimeMultiplayerLanRole role() const;
    RuntimeMultiplayerLanPhase phase() const;
    const std::string& localPeerId() const;
    std::uint64_t roomGeneration() const;
    std::uint16_t discoveryPort() const;
    std::uint16_t sessionPort() const;
    const RuntimeMultiplayerSessionState& session() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
