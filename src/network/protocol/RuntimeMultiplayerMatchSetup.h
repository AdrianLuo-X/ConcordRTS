#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::uint32_t runtimeMultiplayerMatchSetupMagic =
    0x434d5354U; // "CMST"
inline constexpr std::uint16_t runtimeMultiplayerMatchSetupVersion = 3;
inline constexpr std::size_t runtimeMultiplayerMatchMaximumMembers = 16;
inline constexpr std::size_t runtimeMultiplayerMatchMaximumSlots = 8;
inline constexpr std::size_t runtimeMultiplayerMatchMaximumPayloadBytes = 8192;
inline constexpr std::size_t runtimeMultiplayerMatchMaximumPeerIdBytes = 64;
inline constexpr std::size_t
    runtimeMultiplayerMatchMaximumDisplayNameBytes = 96;
inline constexpr std::size_t
    runtimeMultiplayerMatchMaximumCanonicalMapIdBytes = 256;
inline constexpr std::size_t
    runtimeMultiplayerMatchMaximumGameModeBytes = 64;
inline constexpr std::size_t
    runtimeMultiplayerMatchMaximumRulesOverrideBytes = 256;
inline constexpr std::size_t
    runtimeMultiplayerMatchMaximumSlotOptionBytes = 64;
inline constexpr std::int32_t
    runtimeMultiplayerMatchMaximumFixedTickHz = 240;
inline constexpr std::int32_t
    runtimeMultiplayerMatchMaximumStartingCredits = 2000000000;
inline constexpr std::int32_t
    runtimeMultiplayerMatchMaximumStartingUnits = 1000;
inline constexpr std::int32_t runtimeMultiplayerMatchMaximumTechLevel = 10;
inline constexpr std::int32_t runtimeMultiplayerMatchMaximumGameSpeed = 6;

enum class RuntimeMultiplayerMatchMemberIdentity : std::uint8_t {
    Combatant = 1,
    Spectator = 2
};

enum class RuntimeMultiplayerMatchSlotController : std::uint8_t {
    Human = 1,
    Computer = 2,
    Closed = 3
};

struct RuntimeMultiplayerMatchMember {
    std::string peerId;
    std::string displayName;
    RuntimeMultiplayerMatchMemberIdentity identity =
        RuntimeMultiplayerMatchMemberIdentity::Combatant;

    bool operator==(const RuntimeMultiplayerMatchMember&) const = default;
};

struct RuntimeMultiplayerMatchSlot {
    // Slot indices are zero-based and canonical setups contain the contiguous
    // range 0..slots.size()-1. This makes an authored Closed slot distinct
    // from a map whose lobby exposes fewer slots.
    std::uint8_t slotIndex = 0;
    RuntimeMultiplayerMatchSlotController controller =
        RuntimeMultiplayerMatchSlotController::Closed;

    // Required only for Human and empty for Computer/Closed. It names the
    // stable room-member peer identity, never a display name or connection id.
    std::string peerId;

    // Active slots require canonical option identifiers. Closed slots require
    // all option fields to be empty and startPosition to be zero.
    std::string countryId;
    std::string colorId;
    std::string teamId;
    std::string difficultyId;
    // Zero requests deterministic random placement. Values 1..8 reserve an
    // explicit map start and must be unique among active slots.
    std::uint8_t startPosition = 0;

    bool operator==(const RuntimeMultiplayerMatchSlot&) const = default;
};

struct RuntimeMultiplayerMatchMap {
    // Canonical resource id, not a machine-local path. Canonicalization trims
    // it, converts ASCII to lower case, and normalizes '\\' to '/'.
    std::string canonicalId;
    std::uint64_t contentFingerprint = 0;

    bool operator==(const RuntimeMultiplayerMatchMap&) const = default;
};

// The selected original cooperative mission travels with the room setup.
// Country lists retain authored order, including its default first country.
struct RuntimeMultiplayerCooperativeMission {
    std::string mapId;
    std::uint64_t scenarioFingerprint = 0;
    std::vector<std::string> playerCountries;
    std::vector<std::string> enemyCountries;

    bool operator==(const RuntimeMultiplayerCooperativeMission&) const = default;
};

struct RuntimeMultiplayerMatchRules {
    std::string gameMode;
    std::string rulesOverride;
    std::uint64_t randomSeed = 0;
    std::int32_t fixedTickHz = 30;
    std::int32_t startingCredits = 10000;
    std::int32_t startingUnits = 10;
    std::int32_t techLevel = 10;
    std::int32_t gameSpeed = 1;
    bool shortGame = true;
    bool mcvRepacks = true;
    bool crates = true;
    bool superweapons = false;
    bool buildOffAlly = false;
    RuntimeMultiplayerCooperativeMission cooperativeMission;

    bool operator==(const RuntimeMultiplayerMatchRules&) const = default;
};

struct RuntimeMultiplayerMatchSetup {
    // Increased by the authoritative host for every accepted lobby change.
    std::uint64_t revision = 0;
    std::string hostPeerId;
    RuntimeMultiplayerMatchMap map;
    RuntimeMultiplayerMatchRules rules;
    std::vector<RuntimeMultiplayerMatchMember> members;
    std::vector<RuntimeMultiplayerMatchSlot> slots;

    bool operator==(const RuntimeMultiplayerMatchSetup&) const = default;
};

// RuntimeShell-independent projection used by a match bootstrap. Closed slots
// remain present so slot indices and the lobby's exposed capacity are retained.
struct RuntimeMultiplayerMatchLaunchSlot {
    std::uint8_t slotIndex = 0;
    RuntimeMultiplayerMatchSlotController controller =
        RuntimeMultiplayerMatchSlotController::Closed;
    bool enabled = false;
    bool human = false;
    bool local = false;
    std::string peerId;
    std::string displayName;
    std::string countryId;
    std::string colorId;
    std::string teamId;
    std::string difficultyId;
    std::uint8_t startPosition = 0;

    bool operator==(const RuntimeMultiplayerMatchLaunchSlot&) const = default;
};

struct RuntimeMultiplayerMatchLaunchDescription {
    std::uint64_t setupRevision = 0;
    std::uint64_t setupHash = 0;
    std::string hostPeerId;
    bool localPeerIsHost = false;
    bool localPeerIsSpectator = false;
    RuntimeMultiplayerMatchMap map;
    RuntimeMultiplayerMatchRules rules;
    std::vector<RuntimeMultiplayerMatchLaunchSlot> slots;

    bool operator==(
        const RuntimeMultiplayerMatchLaunchDescription&
    ) const = default;
};

// Canonicalization is transactional. It normalizes canonical resource/option
// identifiers, trims display text, sorts members by peer id and slots by slot
// index, then applies the complete strict validator.
bool canonicalizeRuntimeMultiplayerMatchSetup(
    RuntimeMultiplayerMatchSetup& setup,
    std::string& errorMessage
);

// Validation accepts canonical form only. In particular, list order,
// normalized identifiers, controller-specific empty fields, and every
// cross-reference must already satisfy the wire contract.
bool validateRuntimeMultiplayerMatchSetup(
    const RuntimeMultiplayerMatchSetup& setup,
    std::string& errorMessage
);

// Fixed-width integers use big-endian byte order and strings use a big-endian
// uint16 byte length. Encode canonicalizes a copy; decode requires canonical
// wire form. Both leave their output unchanged on failure.
bool encodeRuntimeMultiplayerMatchSetup(
    const RuntimeMultiplayerMatchSetup& setup,
    std::vector<std::uint8_t>& payload,
    std::string& errorMessage
);

bool decodeRuntimeMultiplayerMatchSetup(
    std::span<const std::uint8_t> payload,
    RuntimeMultiplayerMatchSetup& setup,
    std::string& errorMessage
);

// FNV-1a over the canonical versioned wire image. The hash output is updated
// only after successful canonicalization and encoding.
bool runtimeMultiplayerMatchSetupHash(
    const RuntimeMultiplayerMatchSetup& setup,
    std::uint64_t& hash,
    std::string& errorMessage
);

// Stable gameplay-only fingerprint used by portable multiplayer snapshots.
// The setup is canonicalized and fully validated first, but room revision,
// host/member identities, display names, and human-slot peer bindings are not
// mixed into the result. This lets a saved deterministic match be rebound to
// different authenticated peers without changing its gameplay identity.
// Map identity/content, every authored rule, slot shape/controller/options,
// and start positions remain covered. The output is transactional.
bool runtimeMultiplayerGameplaySetupFingerprint(
    const RuntimeMultiplayerMatchSetup& setup,
    std::uint64_t& fingerprint,
    std::string& errorMessage
);

// localPeerId must name a room member. Spectating hosts are valid and simply
// produce no local combat slot. The output is transactional.
bool makeRuntimeMultiplayerMatchLaunchDescription(
    const RuntimeMultiplayerMatchSetup& setup,
    std::string_view localPeerId,
    RuntimeMultiplayerMatchLaunchDescription& launch,
    std::string& errorMessage
);
