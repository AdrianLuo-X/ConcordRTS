#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

// Versioned payload carried inside RuntimeMultiplayerCommandPacket::payload.
// Player/house ownership is deliberately absent: the authoritative host must
// derive the issuing slot from the authenticated connection before applying a
// decoded command.
inline constexpr std::uint32_t runtimeMultiplayerGameplayCommandMagic =
    0x4347434dU; // "CGCM"
// Version 20 carries a fixed direction for each fragment of a group D command.
inline constexpr std::uint16_t runtimeMultiplayerGameplayCommandVersion = 20;
inline constexpr std::size_t
    runtimeMultiplayerGameplayMaximumPayloadBytes = 2048;
inline constexpr std::size_t
    runtimeMultiplayerGameplayMaximumObjectIds = 256;
inline constexpr std::size_t
    runtimeMultiplayerGameplayMaximumIdentifierBytes = 256;
inline constexpr std::size_t
    runtimeMultiplayerGameplayMaximumSuperWeaponTargets = 2;
// RuntimeShell's canonical cell key is y * 1000 + x. Bounding both authored
// axes to 0..999 prevents x from aliasing the next row and keeps multiplication
// inside int32 for every payload accepted by this codec.
inline constexpr std::int32_t
    runtimeMultiplayerGameplayMaximumCellCoordinate = 999;

enum class RuntimeMultiplayerGameplayCommandType : std::uint16_t {
    TargetOrder = 1,
    Stop = 2,
    SecondaryAction = 3,
    QueueProduction = 4,
    CancelProduction = 5,
    PlaceBuilding = 6,
    Sell = 7,
    Repair = 8,
    FireSuperWeapon = 9,
    Guard = 10,
    Scatter = 11,
    SetProductionHold = 12,
    SetPrimaryFactory = 13,
    SetRallyPoint = 14,
    PlayerCommunication = 15,
    WaypointPlanning = 16
};

enum class RuntimeMultiplayerGameplayTargetOrderKind : std::uint8_t {
    // Move maps to issueRuntimeMoveCommand and therefore requires a cell.
    Move = 1,
    // Attack maps to issueRuntimeAttackCommand and therefore requires an
    // explicit target object. This is the Ctrl force-attack order; contextual
    // attacks continue to use Primary and its normal relationship checks.
    Attack = 2,
    // Primary maps to issueRuntimeCommand and may carry either a cell or an
    // object, allowing the runtime to resolve enter/capture/repair/attack.
    Primary = 3,
    // ForceFireCell maps to authored force-fire on terrain and therefore
    // always carries a cell, never an object target.
    ForceFireCell = 4,
    Follow = 5,
    AttackMove = 6,
    GuardDestination = 7
};

enum class RuntimeMultiplayerGameplaySecondaryActionKind : std::uint8_t {
    // Applies executeRuntimeSecondaryAction to each explicit object.
    Contextual = 1,
    // Applies to explicit objects which currently expose deploy/undeploy.
    DeployToggle = 2,
    // All fragments of one D selection preserve this deployment direction.
    // Non-bistable secondary abilities retain their contextual action.
    DeployAll = 3,
    UndeployAll = 4
};

// Numeric values intentionally mirror RuntimeProductionLane without pulling
// RulesDatabase into this lightweight wire module.
enum class RuntimeMultiplayerGameplayProductionLane : std::uint8_t {
    Structures = 0,
    Armory = 1,
    Infantry = 2,
    Vehicles = 3,
    Aircraft = 4,
    Ships = 5
};

enum class RuntimeMultiplayerGameplayProductionInsertMode : std::uint8_t {
    Back = 1,
    AfterActive = 2
};

struct RuntimeMultiplayerGameplayCell {
    std::int32_t x = 0;
    std::int32_t y = 0;

    bool operator==(const RuntimeMultiplayerGameplayCell&) const = default;
};

struct RuntimeMultiplayerGameplayCellTarget {
    RuntimeMultiplayerGameplayCell cell;

    bool operator==(
        const RuntimeMultiplayerGameplayCellTarget&
    ) const = default;
};

struct RuntimeMultiplayerGameplayObjectTarget {
    std::int32_t objectId = 0;

    bool operator==(
        const RuntimeMultiplayerGameplayObjectTarget&
    ) const = default;
};

using RuntimeMultiplayerGameplayTarget = std::variant<
    RuntimeMultiplayerGameplayCellTarget,
    RuntimeMultiplayerGameplayObjectTarget
>;

struct RuntimeMultiplayerGameplayTargetOrderCommand {
    RuntimeMultiplayerGameplayTargetOrderKind order =
        RuntimeMultiplayerGameplayTargetOrderKind::Move;
    std::vector<std::int32_t> objectIds;
    RuntimeMultiplayerGameplayTarget target =
        RuntimeMultiplayerGameplayCellTarget{};
    // Append after the actor's current order; false replaces queued orders.
    bool append = false;

    bool operator==(
        const RuntimeMultiplayerGameplayTargetOrderCommand&
    ) const = default;
};

struct RuntimeMultiplayerGameplayStopCommand {
    std::vector<std::int32_t> objectIds;

    bool operator==(
        const RuntimeMultiplayerGameplayStopCommand&
    ) const = default;
};

// Guard and Scatter are explicit simulation orders. They intentionally carry
// only the canonical selected-object set; selection and other UI state never
// cross the gameplay wire.
struct RuntimeMultiplayerGameplayGuardCommand {
    std::vector<std::int32_t> objectIds;

    bool operator==(
        const RuntimeMultiplayerGameplayGuardCommand&
    ) const = default;
};

struct RuntimeMultiplayerGameplayScatterCommand {
    std::vector<std::int32_t> objectIds;

    bool operator==(
        const RuntimeMultiplayerGameplayScatterCommand&
    ) const = default;
};

struct RuntimeMultiplayerGameplaySecondaryActionCommand {
    RuntimeMultiplayerGameplaySecondaryActionKind action =
        RuntimeMultiplayerGameplaySecondaryActionKind::Contextual;
    // Canonical selected IDs, bounded by the common 256-object payload limit.
    // A large selection uses multiple commands with the same fixed direction.
    std::vector<std::int32_t> objectIds;

    bool operator==(
        const RuntimeMultiplayerGameplaySecondaryActionCommand&
    ) const = default;
};

// One QueueProduction command represents one authored queue click. Repeating
// the network command repeats the queue operation and keeps partial batch
// acceptance out of the deterministic protocol.
struct RuntimeMultiplayerGameplayQueueProductionCommand {
    std::string typeId;
    RuntimeMultiplayerGameplayProductionInsertMode insertMode =
        RuntimeMultiplayerGameplayProductionInsertMode::Back;

    bool operator==(
        const RuntimeMultiplayerGameplayQueueProductionCommand&
    ) const = default;
};

struct RuntimeMultiplayerGameplayCancelProductionCommand {
    RuntimeMultiplayerGameplayProductionLane lane =
        RuntimeMultiplayerGameplayProductionLane::Structures;
    // Valid runtime lane serials are 1..UINT64_MAX-1. UINT64_MAX cannot be
    // allocated because nextSerial would overflow after accepting it.
    std::uint64_t productionSerial = 0;

    bool operator==(
        const RuntimeMultiplayerGameplayCancelProductionCommand&
    ) const = default;
};

// Hold and resume are the same authoritative operation. The active serial
// makes a delayed command harmless after the lane advances to another item.
struct RuntimeMultiplayerGameplaySetProductionHoldCommand {
    RuntimeMultiplayerGameplayProductionLane lane =
        RuntimeMultiplayerGameplayProductionLane::Structures;
    // Valid runtime lane serials are 1..UINT64_MAX-1.
    std::uint64_t activeProductionSerial = 0;
    bool onHold = false;

    bool operator==(
        const RuntimeMultiplayerGameplaySetProductionHoldCommand&
    ) const = default;
};

struct RuntimeMultiplayerGameplayPlaceBuildingCommand {
    RuntimeMultiplayerGameplayProductionLane lane =
        RuntimeMultiplayerGameplayProductionLane::Structures;
    std::uint64_t productionSerial = 0;
    std::string buildingTypeId;
    RuntimeMultiplayerGameplayCell cell;

    bool operator==(
        const RuntimeMultiplayerGameplayPlaceBuildingCommand&
    ) const = default;
};

struct RuntimeMultiplayerGameplaySellCommand {
    std::int32_t objectId = 0;

    bool operator==(
        const RuntimeMultiplayerGameplaySellCommand&
    ) const = default;
};

struct RuntimeMultiplayerGameplayRepairCommand {
    std::int32_t objectId = 0;

    bool operator==(
        const RuntimeMultiplayerGameplayRepairCommand&
    ) const = default;
};

struct RuntimeMultiplayerGameplaySetPrimaryFactoryCommand {
    std::int32_t objectId = 0;
    bool operator==(const RuntimeMultiplayerGameplaySetPrimaryFactoryCommand&) const = default;
};

struct RuntimeMultiplayerGameplaySetRallyPointCommand {
    std::int32_t objectId = 0;
    RuntimeMultiplayerGameplayCell cell;
    bool operator==(const RuntimeMultiplayerGameplaySetRallyPointCommand&) const = default;
};

enum class RuntimePlayerCommunicationKind : std::uint8_t {
    ChatAll = 1, ChatAllies, PlaceBeacon, DeleteBeacon, Alliance, Cheer, Taunt, BeaconText
};

enum class RuntimePlayerWaypointOperation : std::uint8_t {
    Begin = 1, Commit, CloseLoop, DeletePoint
};

// Point identifiers come from authoritative route state, never UI indices.
struct RuntimeMultiplayerGameplayWaypointCommand {
    RuntimePlayerWaypointOperation operation = RuntimePlayerWaypointOperation::Begin;
    std::uint64_t waypointId = 0;
    bool operator==(const RuntimeMultiplayerGameplayWaypointCommand&) const = default;
};

// The sender is always derived from the authenticated input frame, never text.
struct RuntimeMultiplayerGameplayPlayerCommunicationCommand {
    RuntimePlayerCommunicationKind kind = RuntimePlayerCommunicationKind::ChatAll;
    std::string text; // UTF-8 chat/beacon annotation, or the canonical diplomacy target.
    RuntimeMultiplayerGameplayCell cell;
    // Chat: 0 preserves broadcast; otherwise bits select stable config slots
    // 0..7, including the sender's own slot (checked by the runtime).
    // Other kinds: alliance on/off, or taunt number 1..8; otherwise 0.
    std::uint8_t value = 0;
    bool operator==(const RuntimeMultiplayerGameplayPlayerCommunicationCommand&) const = default;
};

// SuperWeaponSystem consumes the clicked cell and optional object id together;
// the object may move later, so neither value can be reconstructed from the
// other without changing authored targeting semantics.
struct RuntimeMultiplayerGameplaySuperWeaponTarget {
    RuntimeMultiplayerGameplayCell cell;
    std::optional<std::int32_t> objectId;

    bool operator==(
        const RuntimeMultiplayerGameplaySuperWeaponTarget&
    ) const = default;
};

// Target order is meaningful for authored two-stage super weapons, so it must
// not be sorted. Zero targets covers no-target and whole-map super weapons.
struct RuntimeMultiplayerGameplayFireSuperWeaponCommand {
    std::string superWeaponId;
    std::vector<RuntimeMultiplayerGameplaySuperWeaponTarget> targets;

    bool operator==(
        const RuntimeMultiplayerGameplayFireSuperWeaponCommand&
    ) const = default;
};

using RuntimeMultiplayerGameplayCommandBody = std::variant<
    RuntimeMultiplayerGameplayTargetOrderCommand,
    RuntimeMultiplayerGameplayStopCommand,
    RuntimeMultiplayerGameplaySecondaryActionCommand,
    RuntimeMultiplayerGameplayQueueProductionCommand,
    RuntimeMultiplayerGameplayCancelProductionCommand,
    RuntimeMultiplayerGameplayPlaceBuildingCommand,
    RuntimeMultiplayerGameplaySellCommand,
    RuntimeMultiplayerGameplayRepairCommand,
    RuntimeMultiplayerGameplayFireSuperWeaponCommand,
    RuntimeMultiplayerGameplayGuardCommand,
    RuntimeMultiplayerGameplayScatterCommand,
    RuntimeMultiplayerGameplaySetProductionHoldCommand,
    RuntimeMultiplayerGameplaySetPrimaryFactoryCommand,
    RuntimeMultiplayerGameplaySetRallyPointCommand,
    RuntimeMultiplayerGameplayPlayerCommunicationCommand,
    RuntimeMultiplayerGameplayWaypointCommand
>;

struct RuntimeMultiplayerGameplayCommand {
    RuntimeMultiplayerGameplayCommandBody body =
        RuntimeMultiplayerGameplayTargetOrderCommand{};

    bool operator==(
        const RuntimeMultiplayerGameplayCommand&
    ) const = default;
};

RuntimeMultiplayerGameplayCommandType runtimeMultiplayerGameplayCommandType(
    const RuntimeMultiplayerGameplayCommand& command
);

// Canonicalization is transactional. It sorts and deduplicates every selected
// object-id set, then applies the same strict validation as the wire decoder.
bool canonicalizeRuntimeMultiplayerGameplayCommand(
    RuntimeMultiplayerGameplayCommand& command,
    std::string& errorMessage
);

bool validateRuntimeMultiplayerGameplayCommand(
    const RuntimeMultiplayerGameplayCommand& command,
    std::string& errorMessage
);

// Encoding uses fixed-width big-endian integers. Encode canonicalizes a copy,
// while decode requires the received bytes to already be canonical. Both leave
// their output unchanged on failure.
bool encodeRuntimeMultiplayerGameplayCommand(
    const RuntimeMultiplayerGameplayCommand& command,
    std::vector<std::uint8_t>& payload,
    std::string& errorMessage
);

bool decodeRuntimeMultiplayerGameplayCommand(
    std::span<const std::uint8_t> payload,
    RuntimeMultiplayerGameplayCommand& command,
    std::string& errorMessage
);
