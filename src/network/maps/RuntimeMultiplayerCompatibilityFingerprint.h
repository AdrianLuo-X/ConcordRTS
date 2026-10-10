#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// Bump for incompatible gameplay as well as wire changes. Version 160 persists
// the automatic factory-rally order origin in player-command state.
inline constexpr std::uint16_t
    runtimeMultiplayerCompatibilityManifestVersion = 160U;
inline constexpr std::uint16_t
    runtimeMultiplayerContentFingerprintVersion = 1U;
inline constexpr std::size_t
    runtimeMultiplayerContentMaximumCanonicalResourceIdBytes = 512U;
inline constexpr std::size_t runtimeMultiplayerContentMaximumComponents =
    4096U;
inline constexpr std::size_t
    runtimeMultiplayerContentDefaultMaximumComponentBytes =
        64U * 1024U * 1024U;
inline constexpr std::size_t
    runtimeMultiplayerContentHardMaximumComponentBytes =
        128U * 1024U * 1024U;

// Stable numeric ids, rather than C++ object layout, define the manifest's
// canonical order. Every value is represented as an explicit uint64_t so a
// 32-bit and a 64-bit build hash the same protocol limits identically.
enum class RuntimeMultiplayerProtocolManifestField : std::uint16_t {
    CompatibilityManifestVersion = 1U,
    ContentFingerprintVersion = 2U,
    ContentMaximumCanonicalResourceIdBytes = 3U,
    ContentMaximumComponents = 4U,
    ContentDefaultMaximumComponentBytes = 5U,
    ContentHardMaximumComponentBytes = 6U,

    LanFramingVersion = 100U,
    LanMaximumPacketBytes = 101U,
    LanMaximumQueuedBytesPerPeer = 102U,

    MultiplayerMessageVersion = 200U,
    MultiplayerMaximumJoinRejectionReasonBytes = 201U,

    SessionProtocolVersion = 300U,
    SessionMaximumMembers = 301U,
    SessionMaximumPeerIdBytes = 302U,
    SessionMaximumDisplayNameBytes = 303U,
    SessionMaximumRoomIdBytes = 304U,
    SessionMaximumCommandPayloadBytes = 305U,
    SessionMaximumBufferedCommandBytes = 306U,
    SessionMaximumBufferedCommands = 307U,
    SessionMaximumBufferedCommandBytesPerPeer = 308U,
    SessionMaximumBufferedCommandsPerPeer = 309U,
    SessionMaximumStateHashFrames = 310U,
    SessionMaximumSaveLoadHandshakes = 311U,
    SessionMaximumSnapshotIdBytes = 312U,
    SessionMaximumTick = 313U,
    SessionMaximumCommandLeadWindowTicks = 314U,
    SessionMaximumStateHashLagWindowTicks = 315U,
    SessionCommandSequenceReorderWindow = 316U,

    MatchSetupMagic = 400U,
    MatchSetupVersion = 401U,
    MatchMaximumMembers = 402U,
    MatchMaximumSlots = 403U,
    MatchMaximumPayloadBytes = 404U,
    MatchMaximumPeerIdBytes = 405U,
    MatchMaximumDisplayNameBytes = 406U,
    MatchMaximumCanonicalMapIdBytes = 407U,
    MatchMaximumGameModeBytes = 408U,
    MatchMaximumRulesOverrideBytes = 409U,
    MatchMaximumSlotOptionBytes = 410U,
    MatchMaximumFixedTickHz = 411U,
    MatchMaximumStartingCredits = 412U,
    MatchMaximumStartingUnits = 413U,
    MatchMaximumTechLevel = 414U,
    MatchMaximumGameSpeed = 415U,

    GameplayCommandMagic = 500U,
    GameplayCommandVersion = 501U,
    GameplayMaximumPayloadBytes = 502U,
    GameplayMaximumObjectIds = 503U,
    GameplayMaximumIdentifierBytes = 504U,
    GameplayMaximumSuperWeaponTargets = 505U,
    GameplayMaximumCellCoordinate = 506U,

    LockstepFrameMagic = 600U,
    LockstepFrameVersion = 601U,
    LockstepFrameHeaderBytes = 602U,
    LockstepMaximumCommandsPerFrame = 603U,
    LockstepMaximumFrameBytes = 604U,
    LockstepMaximumInputDelayTicks = 605U,

    SnapshotDescriptorMagic = 700U,
    SnapshotChunkMagic = 701U,
    SnapshotAckMagic = 702U,
    SnapshotTransferVersion = 703U,
    SnapshotSha256Bytes = 704U,
    SnapshotMaximumTokenBytes = 705U,
    SnapshotChunkBytes = 706U,
    SnapshotDefaultMaximumBytes = 707U,
    SnapshotHardMaximumBytes = 708U,
    SnapshotMaximumChunks = 709U,
    SnapshotMaximumTick = 710U
};

struct RuntimeMultiplayerProtocolManifestValue {
    RuntimeMultiplayerProtocolManifestField field =
        RuntimeMultiplayerProtocolManifestField::CompatibilityManifestVersion;
    std::uint64_t value = 0U;

    bool operator==(
        const RuntimeMultiplayerProtocolManifestValue&
    ) const = default;
};

// Builds the production manifest directly from the public constants owned by
// the LAN, message, session, setup, gameplay-command, lockstep, and snapshot
// modules. Output is unchanged on failure.
bool runtimeMultiplayerCurrentProtocolManifest(
    std::vector<RuntimeMultiplayerProtocolManifestValue>& manifest,
    std::string& errorMessage
);

// Domain-separated FNV-1a over a canonical sequence of (uint16 field id,
// uint64 value) records. Input order is irrelevant. Missing, duplicate,
// unknown, or zero values are rejected; output is changed only on success.
bool runtimeMultiplayerProtocolManifestFingerprint(
    std::span<const RuntimeMultiplayerProtocolManifestValue> manifest,
    std::uint64_t& fingerprint,
    std::string& errorMessage
);

bool runtimeMultiplayerCurrentProtocolFingerprint(
    std::uint64_t& fingerprint,
    std::string& errorMessage
);

struct RuntimeMultiplayerContentComponent {
    // This is an authority-safe resource id, never a local filesystem path.
    std::string canonicalResourceId;
    std::vector<std::uint8_t> bytes;

    bool operator==(const RuntimeMultiplayerContentComponent&) const = default;
};

// Transactionally trims surrounding ASCII whitespace, converts ASCII to lower
// case, converts '\\' to '/', rejects unsafe/ambiguous path forms, and sorts
// by canonical id. Empty component bytes are valid; an empty component set is
// not. Duplicate ids after normalization are rejected.
bool canonicalizeRuntimeMultiplayerContentComponents(
    std::vector<RuntimeMultiplayerContentComponent>& components,
    std::string& errorMessage,
    std::size_t maximumComponentBytes =
        runtimeMultiplayerContentDefaultMaximumComponentBytes
);

// Domain-separated FNV-1a with fixed-width component count and explicit id
// and byte lengths. Caller order and legal id spelling variations therefore
// cannot affect the result or create concatenation ambiguity.
bool runtimeMultiplayerContentFingerprint(
    const std::vector<RuntimeMultiplayerContentComponent>& components,
    std::uint64_t& fingerprint,
    std::string& errorMessage,
    std::size_t maximumComponentBytes =
        runtimeMultiplayerContentDefaultMaximumComponentBytes
);

// Reads exactly one explicitly supplied local file into an independently
// supplied canonical resource id. The local path is never retained in or
// hashed into the returned authority data. Empty regular files are valid.
bool loadRuntimeMultiplayerContentComponentFile(
    const std::filesystem::path& filePath,
    std::string canonicalResourceId,
    RuntimeMultiplayerContentComponent& component,
    std::string& errorMessage,
    std::size_t maximumComponentBytes =
        runtimeMultiplayerContentDefaultMaximumComponentBytes
);
