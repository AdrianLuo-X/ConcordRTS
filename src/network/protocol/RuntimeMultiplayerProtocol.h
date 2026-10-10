#pragma once

#include "network/session/RuntimeLanTransport.h"
#include "network/session/RuntimeMultiplayerSessionSystem.h"
#include "network/maps/RuntimeMultiplayerMapTransfer.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

// The high byte of RuntimeNetworkPacket::type is the multiplayer message
// version. The low byte is the event identifier. This keeps an incompatible
// message schema outside the payload parser while leaving the LAN framing
// version independent.
inline constexpr std::uint8_t runtimeMultiplayerMessageVersion = 4;
inline constexpr std::size_t
    runtimeMultiplayerMaximumJoinRejectionReasonBytes = 256;
inline constexpr std::size_t
    runtimeMultiplayerMaximumLobbyResultReasonBytes = 256;
inline constexpr std::uint32_t
    runtimeMultiplayerMaximumReconnectGraceMilliseconds = 120000U;

enum class RuntimeMultiplayerMessageType : std::uint16_t {
    JoinRequest = 0x0401,
    Leave = 0x0402,
    Ready = 0x0403,
    Start = 0x0404,
    Command = 0x0405,
    StateHash = 0x0406,
    SaveLoadProposal = 0x0407,
    SaveLoadPrepare = 0x0408,
    // Source compatibility name for the former one-phase response.
    SaveLoadResponse = SaveLoadPrepare,
    RoomConfig = 0x0409,
    AdvanceAuthoritativeTick = 0x040a,
    SaveLoadAbort = 0x040b,
    SaveLoadCommit = 0x040c,
    SaveLoadApplied = 0x040d,
    SnapshotChunk = 0x040e,
    SnapshotChunkAck = 0x040f,

    // LAN admission/control messages use a separate low-byte range from the
    // deterministic session event sequence. Every v1/v2/v3 high-byte value
    // is intentionally incompatible with this schema.
    AdmissionChallenge = 0x0410,
    JoinAdmissionRequest = 0x0411,
    JoinAccepted = 0x0412,
    JoinRejected = 0x0413,
    LobbySnapshot = 0x0414,
    SessionAbort = 0x0415,
    LobbySlotOptionsRequest = 0x0416,
    LobbySlotOptionsResult = 0x0417,
    LobbyKicked = 0x0418,
    LobbyResumeOffer = 0x0419,
    LobbyResumeChunk = 0x041a,
    LobbyResumeAck = 0x041b,
    LobbyMapRequest = 0x041c,
    LobbyMapOffer = 0x041d,
    LobbyMapChunk = 0x041e,
    LobbyMapAck = 0x041f
};

struct RuntimeMultiplayerLeaveMessage {
    std::string peerId;

    bool operator==(const RuntimeMultiplayerLeaveMessage&) const = default;
};

struct RuntimeMultiplayerReadyMessage {
    std::string peerId;
    bool ready = false;

    bool operator==(const RuntimeMultiplayerReadyMessage&) const = default;
};

struct RuntimeMultiplayerStartMessage {
    std::string requestingPeerId;
    int startTick = -1;

    bool operator==(const RuntimeMultiplayerStartMessage&) const = default;
};

struct RuntimeMultiplayerAdvanceTickMessage {
    std::string requestingPeerId;
    int authoritativeTick = -1;
    std::uint64_t timelineEpoch = 1;

    bool operator==(
        const RuntimeMultiplayerAdvanceTickMessage&
    ) const = default;
};

// fingerprintMatches is deliberately not transmitted. It is derived by the
// authoritative session from observedFingerprint and the proposal.
struct RuntimeMultiplayerSaveLoadResponseMessage {
    std::uint64_t proposalId = 0;
    std::string peerId;
    bool approved = false;
    std::uint64_t observedFingerprint = 0;
    std::uint64_t timelineEpoch = 1;
    std::uint64_t observedAuthoritativeStateHash = 0;

    bool operator==(
        const RuntimeMultiplayerSaveLoadResponseMessage&
    ) const = default;
};

struct RuntimeMultiplayerSaveLoadAbortMessage {
    std::uint64_t proposalId = 0;
    std::string requestingPeerId;
    std::uint64_t timelineEpoch = 1;

    bool operator==(
        const RuntimeMultiplayerSaveLoadAbortMessage&
    ) const = default;
};

using RuntimeMultiplayerSaveLoadPrepareMessage =
    RuntimeMultiplayerSaveLoadResponseMessage;

struct RuntimeMultiplayerSaveLoadCommitMessage {
    std::uint64_t timelineEpoch = 1;
    std::uint64_t proposalId = 0;
    std::string requestingPeerId;

    bool operator==(
        const RuntimeMultiplayerSaveLoadCommitMessage&
    ) const = default;
};

struct RuntimeMultiplayerSaveLoadAppliedMessage {
    std::uint64_t timelineEpoch = 1;
    std::uint64_t proposalId = 0;
    std::string peerId;
    bool applied = true;
    std::uint64_t observedAuthoritativeStateHash = 0;

    bool operator==(
        const RuntimeMultiplayerSaveLoadAppliedMessage&
    ) const = default;
};

// A fresh challenge is issued for every TCP connection. A JoinRequest is
// admitted by the production LAN coordinator only when it is wrapped in a
// response carrying both values from this challenge.
struct RuntimeMultiplayerAdmissionChallengeMessage {
    std::uint64_t roomGeneration = 0;
    std::uint64_t admissionToken = 0;
    // Zero preserves the historical fail-fast behavior. A nonzero value lets
    // an already authenticated running combatant re-establish only its TCP
    // binding within this bounded grace period; deterministic play remains
    // paused until a coordinated Load snapshot completes.
    std::uint32_t reconnectGraceMilliseconds = 0;
    RuntimeMultiplayerSessionConfig config;

    bool operator==(
        const RuntimeMultiplayerAdmissionChallengeMessage&
    ) const = default;
};

struct RuntimeMultiplayerJoinAdmissionRequestMessage {
    std::uint64_t roomGeneration = 0;
    std::uint64_t admissionToken = 0;
    RuntimeMultiplayerJoinRequest request;
    bool reconnect = false;
    std::uint64_t reconnectTimelineEpoch = 0;
    int reconnectAuthoritativeTick = -1;
    std::uint64_t reconnectSessionStateHash = 0;

    bool operator==(
        const RuntimeMultiplayerJoinAdmissionRequestMessage&
    ) const = default;
};

struct RuntimeMultiplayerJoinAcceptedMessage {
    std::uint64_t roomGeneration = 0;
    std::string roomId;
    std::string peerId;
    bool reconnected = false;

    bool operator==(
        const RuntimeMultiplayerJoinAcceptedMessage&
    ) const = default;
};

struct RuntimeMultiplayerJoinRejectedMessage {
    std::string roomId;
    // Empty only when the malformed request could not be decoded far enough
    // to recover the claimed peer id.
    std::string peerId;
    std::string reason;

    bool operator==(
        const RuntimeMultiplayerJoinRejectedMessage&
    ) const = default;
};

struct RuntimeMultiplayerLobbySnapshotMessage {
    std::uint64_t roomGeneration = 0;
    RuntimeMultiplayerLobbySnapshot snapshot;

    bool operator==(
        const RuntimeMultiplayerLobbySnapshotMessage&
    ) const = default;
};

struct RuntimeMultiplayerSessionAbortMessage {
    std::uint64_t roomGeneration = 0;
    RuntimeMultiplayerSessionAbort abort;

    bool operator==(
        const RuntimeMultiplayerSessionAbortMessage&
    ) const = default;
};

struct RuntimeMultiplayerLobbySlotOptionsRequestMessage {
    std::string peerId;
    std::uint64_t requestId = 0;
    std::uint64_t expectedSetupRevision = 0;
    std::uint8_t slotIndex = 0;
    std::string countryId;
    std::string colorId;
    std::string teamId;
    std::string difficultyId;
    std::uint8_t startPosition = 0;

    bool operator==(
        const RuntimeMultiplayerLobbySlotOptionsRequestMessage&
    ) const = default;
};

struct RuntimeMultiplayerLobbySlotOptionsResultMessage {
    std::string peerId;
    std::uint64_t requestId = 0;
    bool accepted = false;
    std::uint64_t currentSetupRevision = 0;
    std::string reason;

    bool operator==(
        const RuntimeMultiplayerLobbySlotOptionsResultMessage&
    ) const = default;
};

struct RuntimeMultiplayerLobbyKickedMessage {
    std::uint64_t roomGeneration = 0;
    std::string peerId;
    std::string reason;

    bool operator==(
        const RuntimeMultiplayerLobbyKickedMessage&
    ) const = default;
};

using RuntimeMultiplayerMessageBody = std::variant<
    RuntimeMultiplayerJoinRequest,
    RuntimeMultiplayerLeaveMessage,
    RuntimeMultiplayerReadyMessage,
    RuntimeMultiplayerStartMessage,
    RuntimeMultiplayerAdvanceTickMessage,
    RuntimeMultiplayerCommandPacket,
    RuntimeMultiplayerStateHashReport,
    RuntimeMultiplayerSaveLoadProposal,
    RuntimeMultiplayerSaveLoadResponseMessage,
    RuntimeMultiplayerSaveLoadAbortMessage,
    RuntimeMultiplayerSaveLoadCommitMessage,
    RuntimeMultiplayerSaveLoadAppliedMessage,
    RuntimeMultiplayerSnapshotChunk,
    RuntimeMultiplayerSnapshotChunkAck,
    RuntimeMultiplayerSessionConfig,
    RuntimeMultiplayerAdmissionChallengeMessage,
    RuntimeMultiplayerJoinAdmissionRequestMessage,
    RuntimeMultiplayerJoinAcceptedMessage,
    RuntimeMultiplayerJoinRejectedMessage,
    RuntimeMultiplayerLobbySnapshotMessage,
    RuntimeMultiplayerSessionAbortMessage,
    RuntimeMultiplayerLobbySlotOptionsRequestMessage,
    RuntimeMultiplayerLobbySlotOptionsResultMessage,
    RuntimeMultiplayerLobbyKickedMessage,
    RuntimeMultiplayerSnapshotDescriptor,
    RuntimeMultiplayerMapRequest,
    RuntimeMultiplayerMapOffer
>;

struct RuntimeMultiplayerMessage {
    RuntimeMultiplayerMessageType type =
        RuntimeMultiplayerMessageType::JoinRequest;
    RuntimeMultiplayerMessageBody body = RuntimeMultiplayerJoinRequest{};

    bool operator==(const RuntimeMultiplayerMessage&) const = default;
};

// Converts one authoritative session event to or from a LAN packet. Integer
// fields are fixed-width big-endian. Strings and byte arrays use a big-endian
// uint32 length prefix. Both functions update their output only on success.
bool encodeRuntimeMultiplayerMessage(
    const RuntimeMultiplayerMessage& message,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
);

bool decodeRuntimeMultiplayerMessage(
    const RuntimeNetworkPacket& packet,
    RuntimeMultiplayerMessage& message,
    std::string& errorMessage
);

struct RuntimeMultiplayerMessageApplyResult {
    bool decoded = false;
    bool accepted = false;
    bool changed = false;
    bool duplicate = false;
    RuntimeMultiplayerMessageType type =
        RuntimeMultiplayerMessageType::JoinRequest;
    bool stateHashFrameComplete = false;
    bool desync = false;
    RuntimeMultiplayerSaveLoadStatus saveLoadStatus =
        RuntimeMultiplayerSaveLoadStatus::Transferring;
    std::string reason;
};

// Secure ingress path for packets received directly from a connection.
// Decoding and source binding checks complete before the session is touched.
// Leave/Ready/Command/StateHash/SaveLoadPrepare/SaveLoadApplied must name the
// peer already bound to that connection. During Running, Leave is valid only
// for a non-host spectator; the host then broadcasts that same event as the
// canonical participant-set transition. Start, AdvanceAuthoritativeTick,
// SaveLoadProposal, SaveLoadCommit, and SaveLoadAbort additionally require a
// host-bound connection. SnapshotChunk/Ack are coordinator-only and are
// rejected by both direct Session application paths. An unbound connection
// may pass an empty peer id for a new
// JoinRequest, but it cannot claim an existing room member. After an accepted
// join, the transport must permanently bind that connection to the request
// peer id before accepting any more packets. Only that bound connection may
// use the session's idempotent duplicate-join behavior.
RuntimeMultiplayerMessageApplyResult applyRuntimeMultiplayerMessage(
    const RuntimeNetworkPacket& packet,
    std::string_view authenticatedPeerId,
    RuntimeMultiplayerSessionState& session
);

// Applies an event that was already authenticated and canonically ordered by
// the host. This is for replicated host broadcasts only and must never be used
// on packets arriving directly from an untrusted client connection. Before an
// inactive peer accepts RoomConfig here, the caller must anchor the connection
// to the selected room/host and compare the advertised compatibility with its
// local protocol/content expectation; this function does not authenticate a
// raw TCP endpoint. Production admission still requires a room-generation
// nonce and a host-issued admission proof so first-join identity cannot remain
// trust-on-first-use.
RuntimeMultiplayerMessageApplyResult
applyTrustedCanonicalRuntimeMultiplayerMessage(
    const RuntimeNetworkPacket& packet,
    RuntimeMultiplayerSessionState& session
);
