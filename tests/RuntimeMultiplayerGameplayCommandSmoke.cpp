#include "RuntimeMultiplayerGameplayCommand.h"
#include "RuntimePlayerCommands.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

template <typename T>
concept CarriesOwner = requires(T value) {
    value.owner;
};

static_assert(!CarriesOwner<RuntimeMultiplayerGameplayCommand>);
static_assert(!CarriesOwner<RuntimeMultiplayerGameplayTargetOrderCommand>);
static_assert(!CarriesOwner<RuntimeMultiplayerGameplayStopCommand>);
static_assert(!CarriesOwner<RuntimeMultiplayerGameplaySecondaryActionCommand>);
static_assert(!CarriesOwner<RuntimeMultiplayerGameplayQueueProductionCommand>);
static_assert(!CarriesOwner<RuntimeMultiplayerGameplayCancelProductionCommand>);
static_assert(!CarriesOwner<
    RuntimeMultiplayerGameplaySetProductionHoldCommand>);
static_assert(!CarriesOwner<RuntimeMultiplayerGameplayPlaceBuildingCommand>);
static_assert(!CarriesOwner<RuntimeMultiplayerGameplaySellCommand>);
static_assert(!CarriesOwner<RuntimeMultiplayerGameplayRepairCommand>);
static_assert(!CarriesOwner<RuntimeMultiplayerGameplayFireSuperWeaponCommand>);
static_assert(!CarriesOwner<RuntimeMultiplayerGameplayGuardCommand>);
static_assert(!CarriesOwner<RuntimeMultiplayerGameplayScatterCommand>);

static_assert(runtimeMultiplayerGameplayCommandVersion == 20U);
static_assert(static_cast<std::uint8_t>(
    RuntimeMultiplayerGameplaySecondaryActionKind::Contextual) == 1U);
static_assert(static_cast<std::uint8_t>(
    RuntimeMultiplayerGameplaySecondaryActionKind::DeployToggle) == 2U);
static_assert(static_cast<std::uint8_t>(
    RuntimeMultiplayerGameplaySecondaryActionKind::DeployAll) == 3U);
static_assert(static_cast<std::uint8_t>(
    RuntimeMultiplayerGameplaySecondaryActionKind::UndeployAll) == 4U);
static_assert(static_cast<std::uint16_t>(
    RuntimeMultiplayerGameplayCommandType::TargetOrder) == 1U);
static_assert(static_cast<std::uint16_t>(
    RuntimeMultiplayerGameplayCommandType::Stop) == 2U);
static_assert(static_cast<std::uint16_t>(
    RuntimeMultiplayerGameplayCommandType::SecondaryAction) == 3U);
static_assert(static_cast<std::uint16_t>(
    RuntimeMultiplayerGameplayCommandType::QueueProduction) == 4U);
static_assert(static_cast<std::uint16_t>(
    RuntimeMultiplayerGameplayCommandType::CancelProduction) == 5U);
static_assert(static_cast<std::uint16_t>(
    RuntimeMultiplayerGameplayCommandType::PlaceBuilding) == 6U);
static_assert(static_cast<std::uint16_t>(
    RuntimeMultiplayerGameplayCommandType::Sell) == 7U);
static_assert(static_cast<std::uint16_t>(
    RuntimeMultiplayerGameplayCommandType::Repair) == 8U);
static_assert(static_cast<std::uint16_t>(
    RuntimeMultiplayerGameplayCommandType::FireSuperWeapon) == 9U);
static_assert(static_cast<std::uint16_t>(
    RuntimeMultiplayerGameplayCommandType::Guard) == 10U);
static_assert(static_cast<std::uint16_t>(
    RuntimeMultiplayerGameplayCommandType::Scatter) == 11U);
static_assert(static_cast<std::uint16_t>(
    RuntimeMultiplayerGameplayCommandType::SetProductionHold) == 12U);
static_assert(static_cast<std::uint8_t>(
    RuntimeMultiplayerGameplayTargetOrderKind::Move) == 1U);
static_assert(static_cast<std::uint8_t>(
    RuntimeMultiplayerGameplayTargetOrderKind::Attack) == 2U);
static_assert(static_cast<std::uint8_t>(
    RuntimeMultiplayerGameplayTargetOrderKind::Primary) == 3U);
static_assert(static_cast<std::uint8_t>(
    RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell) == 4U);

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr
            << "runtime multiplayer gameplay command smoke failure: "
            << message << '\n';
    }
    return condition;
}

template <typename Body>
RuntimeMultiplayerGameplayCommand command(Body body) {
    RuntimeMultiplayerGameplayCommand result;
    result.body = std::move(body);
    return result;
}

RuntimeMultiplayerGameplayCellTarget cellTarget(
    std::int32_t x,
    std::int32_t y
) {
    return RuntimeMultiplayerGameplayCellTarget{{x, y}};
}

RuntimeMultiplayerGameplayObjectTarget objectTarget(std::int32_t objectId) {
    return RuntimeMultiplayerGameplayObjectTarget{objectId};
}

RuntimeMultiplayerGameplaySuperWeaponTarget superWeaponTarget(
    std::int32_t x,
    std::int32_t y,
    std::optional<std::int32_t> objectId = std::nullopt
) {
    return RuntimeMultiplayerGameplaySuperWeaponTarget{{x, y}, objectId};
}

std::vector<RuntimeMultiplayerGameplayCommand> representativeCommands() {
    RuntimeMultiplayerGameplayTargetOrderCommand move;
    move.order = RuntimeMultiplayerGameplayTargetOrderKind::Move;
    move.objectIds = {1, 3, 7};
    move.target = cellTarget(40, 81);

    RuntimeMultiplayerGameplayTargetOrderCommand attack;
    attack.order = RuntimeMultiplayerGameplayTargetOrderKind::Attack;
    attack.objectIds = {2, 4};
    attack.target = objectTarget(99);

    RuntimeMultiplayerGameplayTargetOrderCommand forceFire;
    forceFire.order =
        RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell;
    forceFire.objectIds = {5, 6};
    forceFire.target = cellTarget(22, 33);

    RuntimeMultiplayerGameplayStopCommand stop;
    stop.objectIds = {1, 2, 3};

    RuntimeMultiplayerGameplaySecondaryActionCommand secondary;
    secondary.action =
        RuntimeMultiplayerGameplaySecondaryActionKind::Contextual;
    secondary.objectIds = {8, 9, 10};

    RuntimeMultiplayerGameplayFireSuperWeaponCommand superWeapon;
    superWeapon.superWeaponId = "ChronoSphereSpecial";
    superWeapon.targets = {
        superWeaponTarget(10, 20),
        superWeaponTarget(30, 40, 72)
    };

    return {
        command(move),
        command(attack),
        command(forceFire),
        command(stop),
        command(RuntimeMultiplayerGameplayGuardCommand{{4, 8, 15}}),
        command(RuntimeMultiplayerGameplayScatterCommand{{16, 23, 42}}),
        command(secondary),
        command(RuntimeMultiplayerGameplaySecondaryActionCommand{
            RuntimeMultiplayerGameplaySecondaryActionKind::DeployToggle, {8, 9, 10}}),
        command(RuntimeMultiplayerGameplaySecondaryActionCommand{
            RuntimeMultiplayerGameplaySecondaryActionKind::DeployAll, {8, 9, 10}}),
        command(RuntimeMultiplayerGameplaySecondaryActionCommand{
            RuntimeMultiplayerGameplaySecondaryActionKind::UndeployAll, {8, 9, 10}}),
        command(RuntimeMultiplayerGameplayQueueProductionCommand{"MTNK"}),
        command(RuntimeMultiplayerGameplayCancelProductionCommand{
            RuntimeMultiplayerGameplayProductionLane::Vehicles,
            17
        }),
        command(RuntimeMultiplayerGameplaySetProductionHoldCommand{
            RuntimeMultiplayerGameplayProductionLane::Vehicles,
            17,
            true
        }),
        command(RuntimeMultiplayerGameplaySetProductionHoldCommand{
            RuntimeMultiplayerGameplayProductionLane::Vehicles,
            17,
            false
        }),
        command(RuntimeMultiplayerGameplayPlaceBuildingCommand{
            RuntimeMultiplayerGameplayProductionLane::Structures,
            18,
            "GAPOWR",
            {44, 55}
        }),
        command(RuntimeMultiplayerGameplaySellCommand{101}),
        command(RuntimeMultiplayerGameplayRepairCommand{102}),
        command(RuntimeMultiplayerGameplaySetPrimaryFactoryCommand{102}),
        command(RuntimeMultiplayerGameplaySetRallyPointCommand{102, {33, 44}}),
        command(RuntimeMultiplayerGameplayTargetOrderCommand{
            RuntimeMultiplayerGameplayTargetOrderKind::Follow, {4, 7},
            RuntimeMultiplayerGameplayObjectTarget{8}, true}),
        command(RuntimeMultiplayerGameplayTargetOrderCommand{
            RuntimeMultiplayerGameplayTargetOrderKind::AttackMove, {4, 7},
            RuntimeMultiplayerGameplayCellTarget{{8, 9}}, false}),
        command(RuntimeMultiplayerGameplayTargetOrderCommand{
            RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination, {4, 7},
            RuntimeMultiplayerGameplayCellTarget{{8, 9}}, true}),
        command(RuntimeMultiplayerGameplayPlayerCommunicationCommand{RuntimePlayerCommunicationKind::ChatAll,"hello"}),
        command(RuntimeMultiplayerGameplayPlayerCommunicationCommand{RuntimePlayerCommunicationKind::ChatAllies,"中文"}),
        command(RuntimeMultiplayerGameplayPlayerCommunicationCommand{RuntimePlayerCommunicationKind::PlaceBeacon,"",{12,13}}),
        command(RuntimeMultiplayerGameplayPlayerCommunicationCommand{RuntimePlayerCommunicationKind::DeleteBeacon,"",{12,13}}),
        command(RuntimeMultiplayerGameplayPlayerCommunicationCommand{RuntimePlayerCommunicationKind::BeaconText,"从这里进攻",{12,13}}),
        command(RuntimeMultiplayerGameplayPlayerCommunicationCommand{RuntimePlayerCommunicationKind::BeaconText,"",{12,13}}),
        command(RuntimeMultiplayerGameplayPlayerCommunicationCommand{RuntimePlayerCommunicationKind::Alliance,"bravo",{},1}),
        command(RuntimeMultiplayerGameplayPlayerCommunicationCommand{RuntimePlayerCommunicationKind::Cheer}),
        command(RuntimeMultiplayerGameplayPlayerCommunicationCommand{RuntimePlayerCommunicationKind::Taunt,"",{},8}),
        command(RuntimeMultiplayerGameplayWaypointCommand{RuntimePlayerWaypointOperation::Begin, 0}),
        command(RuntimeMultiplayerGameplayWaypointCommand{RuntimePlayerWaypointOperation::Commit, 0}),
        command(RuntimeMultiplayerGameplayWaypointCommand{RuntimePlayerWaypointOperation::CloseLoop, 7}),
        command(RuntimeMultiplayerGameplayWaypointCommand{RuntimePlayerWaypointOperation::DeletePoint, 7}),
        command(superWeapon),
        command(RuntimeMultiplayerGameplayFireSuperWeaponCommand{
            "PsychicRevealSpecial",
            {}
        })
    };
}

bool encode(
    const RuntimeMultiplayerGameplayCommand& source,
    std::vector<std::uint8_t>& bytes,
    const std::string& label
) {
    std::string error;
    return expect(
        encodeRuntimeMultiplayerGameplayCommand(source, bytes, error),
        label + " encodes: " + error
    );
}

bool testRoundTripsAndAllOperations() {
    bool success = true;
    std::set<RuntimeMultiplayerGameplayCommandType> types;
    for (const RuntimeMultiplayerGameplayCommand& source :
            representativeCommands()) {
        std::vector<std::uint8_t> bytes;
        if (!encode(source, bytes, "representative command")) {
            success = false;
            continue;
        }
        RuntimeMultiplayerGameplayCommand decoded;
        decoded.body = RuntimeMultiplayerGameplayRepairCommand{777};
        std::string error;
        const bool decodedOk = decodeRuntimeMultiplayerGameplayCommand(
            bytes,
            decoded,
            error
        );
        std::vector<std::uint8_t> replayBytes;
        const bool replayEncoded = decodedOk
            && encodeRuntimeMultiplayerGameplayCommand(
                decoded,
                replayBytes,
                error
            );
        success &= expect(
            decodedOk && decoded == source
                && replayEncoded && replayBytes == bytes
                && bytes.size()
                    <= runtimeMultiplayerGameplayMaximumPayloadBytes,
            "representative command round-trips to one stable replay payload: "
                + error
        );
        types.insert(runtimeMultiplayerGameplayCommandType(source));
    }
    success &= expect(
        types.size() == 16
            && types.contains(
                RuntimeMultiplayerGameplayCommandType::TargetOrder)
            && types.contains(RuntimeMultiplayerGameplayCommandType::Stop)
            && types.contains(
                RuntimeMultiplayerGameplayCommandType::SecondaryAction)
            && types.contains(
                RuntimeMultiplayerGameplayCommandType::QueueProduction)
            && types.contains(
                RuntimeMultiplayerGameplayCommandType::CancelProduction)
            && types.contains(
                RuntimeMultiplayerGameplayCommandType::PlaceBuilding)
            && types.contains(RuntimeMultiplayerGameplayCommandType::Sell)
            && types.contains(RuntimeMultiplayerGameplayCommandType::Repair)
            && types.contains(
                RuntimeMultiplayerGameplayCommandType::FireSuperWeapon)
            && types.contains(RuntimeMultiplayerGameplayCommandType::Guard)
            && types.contains(RuntimeMultiplayerGameplayCommandType::Scatter)
            && types.contains(
                RuntimeMultiplayerGameplayCommandType::SetProductionHold),
        "all sixteen gameplay command operations are represented"
    );
    return success;
}

bool testGoldenWireFormatAndCanonicalization() {
    bool success = true;
    const RuntimeMultiplayerGameplayCommand placement = command(
        RuntimeMultiplayerGameplayPlaceBuildingCommand{
            RuntimeMultiplayerGameplayProductionLane::Structures,
            0x0102030405060708ULL,
            "GACNST",
            {0x0102, 0x0304}
        }
    );
    std::vector<std::uint8_t> placementBytes;
    success &= encode(placement, placementBytes, "golden placement");
    const std::vector<std::uint8_t> expectedPlacement{
        0x43U, 0x47U, 0x43U, 0x4dU,
        0x00U, 0x14U,
        0x00U, 0x06U,
        0x00U,
        0x01U, 0x02U, 0x03U, 0x04U,
        0x05U, 0x06U, 0x07U, 0x08U,
        0x00U, 0x06U,
        'G', 'A', 'C', 'N', 'S', 'T',
        0x00U, 0x00U, 0x01U, 0x02U,
        0x00U, 0x00U, 0x03U, 0x04U
    };
    success &= expect(
        placementBytes == expectedPlacement,
        "wire header, serial, string length, and coordinates are big-endian"
    );

    const RuntimeMultiplayerGameplayCommand hold = command(
        RuntimeMultiplayerGameplaySetProductionHoldCommand{
            RuntimeMultiplayerGameplayProductionLane::Aircraft,
            0x0102030405060708ULL,
            true
        }
    );
    std::vector<std::uint8_t> holdBytes;
    success &= encode(hold, holdBytes, "golden production hold");
    const std::vector<std::uint8_t> expectedHold{
        0x43U, 0x47U, 0x43U, 0x4dU,
        0x00U, 0x14U,
        0x00U, 0x0cU,
        0x04U,
        0x01U, 0x02U, 0x03U, 0x04U,
        0x05U, 0x06U, 0x07U, 0x08U,
        0x01U
    };
    success &= expect(
        holdBytes == expectedHold,
        "production hold wire uses lane, active serial, and canonical boolean"
    );

    const RuntimeMultiplayerGameplayCommand unordered = command(
        RuntimeMultiplayerGameplayStopCommand{{9, 1, 9, 3, 1}}
    );
    const RuntimeMultiplayerGameplayCommand canonical = command(
        RuntimeMultiplayerGameplayStopCommand{{1, 3, 9}}
    );
    std::vector<std::uint8_t> unorderedBytes;
    std::vector<std::uint8_t> canonicalBytes;
    success &= encode(unordered, unorderedBytes, "unordered object set");
    success &= encode(canonical, canonicalBytes, "canonical object set");
    success &= expect(
        unorderedBytes == canonicalBytes
            && std::get<RuntimeMultiplayerGameplayStopCommand>(
                unordered.body
            ).objectIds == std::vector<std::int32_t>({9, 1, 9, 3, 1}),
        "encode canonicalizes a copy without mutating the caller"
    );

    RuntimeMultiplayerGameplayCommand normalized = unordered;
    std::string error;
    success &= expect(
        canonicalizeRuntimeMultiplayerGameplayCommand(normalized, error)
            && normalized == canonical,
        "public canonicalization sorts and deduplicates object ids: " + error
    );

    const RuntimeMultiplayerGameplayCommand unorderedGuard = command(
        RuntimeMultiplayerGameplayGuardCommand{{9, 1, 9, 3, 1}}
    );
    const RuntimeMultiplayerGameplayCommand canonicalGuard = command(
        RuntimeMultiplayerGameplayGuardCommand{{1, 3, 9}}
    );
    const RuntimeMultiplayerGameplayCommand unorderedScatter = command(
        RuntimeMultiplayerGameplayScatterCommand{{8, 2, 8, 5, 2}}
    );
    const RuntimeMultiplayerGameplayCommand canonicalScatter = command(
        RuntimeMultiplayerGameplayScatterCommand{{2, 5, 8}}
    );
    std::vector<std::uint8_t> unorderedGuardBytes;
    std::vector<std::uint8_t> canonicalGuardBytes;
    std::vector<std::uint8_t> unorderedScatterBytes;
    std::vector<std::uint8_t> canonicalScatterBytes;
    success &= encode(
        unorderedGuard,
        unorderedGuardBytes,
        "unordered guard object set"
    );
    success &= encode(
        canonicalGuard,
        canonicalGuardBytes,
        "canonical guard object set"
    );
    success &= encode(
        unorderedScatter,
        unorderedScatterBytes,
        "unordered scatter object set"
    );
    success &= encode(
        canonicalScatter,
        canonicalScatterBytes,
        "canonical scatter object set"
    );
    success &= expect(
        unorderedGuardBytes == canonicalGuardBytes
            && unorderedScatterBytes == canonicalScatterBytes
            && std::get<RuntimeMultiplayerGameplayGuardCommand>(
                unorderedGuard.body
            ).objectIds == std::vector<std::int32_t>({9, 1, 9, 3, 1})
            && std::get<RuntimeMultiplayerGameplayScatterCommand>(
                unorderedScatter.body
            ).objectIds == std::vector<std::int32_t>({8, 2, 8, 5, 2}),
        "Guard and Scatter sort and deduplicate selected object ids"
    );
    return success;
}

bool rejectedWithoutChangingOutput(
    const RuntimeMultiplayerGameplayCommand& invalid,
    const std::string& label
) {
    std::vector<std::uint8_t> output{0xdeU, 0xadU};
    const std::vector<std::uint8_t> before = output;
    std::string error;
    return expect(
        !encodeRuntimeMultiplayerGameplayCommand(invalid, output, error)
            && output == before && !error.empty(),
        label + " is rejected transactionally"
    );
}

bool testStrictSourceValidation() {
    bool success = true;
    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplayStopCommand{}),
        "empty selected object set"
    );
    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplayStopCommand{{0, 2}}),
        "zero object id"
    );
    RuntimeMultiplayerGameplayStopCommand tooMany;
    tooMany.objectIds.resize(
        runtimeMultiplayerGameplayMaximumObjectIds + 1,
        1
    );
    success &= rejectedWithoutChangingOutput(
        command(std::move(tooMany)),
        "oversized raw object-id set"
    );
    RuntimeMultiplayerGameplayGuardCommand tooManyGuard;
    tooManyGuard.objectIds.resize(
        runtimeMultiplayerGameplayMaximumObjectIds + 1U,
        1
    );
    success &= rejectedWithoutChangingOutput(
        command(std::move(tooManyGuard)),
        "oversized Guard object-id set"
    );
    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplayScatterCommand{{0, 2}}),
        "Scatter zero object id"
    );

    RuntimeMultiplayerGameplayTargetOrderCommand badOrder;
    badOrder.order = static_cast<
        RuntimeMultiplayerGameplayTargetOrderKind>(0xffU);
    badOrder.objectIds = {1};
    badOrder.target = cellTarget(2, 3);
    success &= rejectedWithoutChangingOutput(
        command(badOrder),
        "unknown target order"
    );
    badOrder.order = RuntimeMultiplayerGameplayTargetOrderKind::Move;
    badOrder.target = objectTarget(2);
    success &= rejectedWithoutChangingOutput(
        command(badOrder),
        "move with object target"
    );
    badOrder.target = cellTarget(-1, 3);
    success &= rejectedWithoutChangingOutput(
        command(badOrder),
        "negative coordinate"
    );
    badOrder.target = cellTarget(
        runtimeMultiplayerGameplayMaximumCellCoordinate + 1,
        3
    );
    success &= rejectedWithoutChangingOutput(
        command(badOrder),
        "coordinate above protocol bound"
    );
    badOrder.order = RuntimeMultiplayerGameplayTargetOrderKind::Attack;
    badOrder.target = cellTarget(2, 3);
    success &= rejectedWithoutChangingOutput(
        command(badOrder),
        "attack with cell target"
    );
    badOrder.order =
        RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell;
    badOrder.target = objectTarget(2);
    success &= rejectedWithoutChangingOutput(
        command(badOrder),
        "force fire with object target"
    );

    RuntimeMultiplayerGameplaySecondaryActionCommand badSecondary;
    badSecondary.action = static_cast<
        RuntimeMultiplayerGameplaySecondaryActionKind>(0xffU);
    badSecondary.objectIds = {1};
    success &= rejectedWithoutChangingOutput(
        command(badSecondary),
        "unknown secondary action"
    );
    badSecondary.action =
        RuntimeMultiplayerGameplaySecondaryActionKind::Contextual;
    badSecondary.objectIds.clear();
    success &= rejectedWithoutChangingOutput(
        command(badSecondary),
        "empty secondary action selection"
    );

    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplayQueueProductionCommand{
            "MTNK",
            static_cast<RuntimeMultiplayerGameplayProductionInsertMode>(0xffU)
        }),
        "unknown production insert mode"
    );

    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplayQueueProductionCommand{"BAD\nID"}),
        "identifier containing a control byte"
    );
    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplayQueueProductionCommand{
            std::string(
                runtimeMultiplayerGameplayMaximumIdentifierBytes + 1,
                'A'
            )
        }),
        "oversized production identifier"
    );
    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplayCancelProductionCommand{}),
        "zero production serial"
    );
    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplayCancelProductionCommand{
            static_cast<RuntimeMultiplayerGameplayProductionLane>(0xffU),
            1
        }),
        "unknown production lane"
    );
    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplayCancelProductionCommand{
            RuntimeMultiplayerGameplayProductionLane::Structures,
            std::numeric_limits<std::uint64_t>::max()
        }),
        "unallocatable maximum production serial"
    );
    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplaySetProductionHoldCommand{}),
        "zero active production serial"
    );
    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplaySetProductionHoldCommand{
            static_cast<RuntimeMultiplayerGameplayProductionLane>(0xffU),
            1,
            true
        }),
        "unknown production hold lane"
    );
    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplaySetProductionHoldCommand{
            RuntimeMultiplayerGameplayProductionLane::Structures,
            std::numeric_limits<std::uint64_t>::max(),
            false
        }),
        "unallocatable maximum active production serial"
    );
    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplayPlaceBuildingCommand{
            RuntimeMultiplayerGameplayProductionLane::Structures,
            1,
            "GACNST",
            {-1, 2}
        }),
        "invalid placement cell"
    );
    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplaySellCommand{}),
        "zero sell object id"
    );
    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplayRepairCommand{-1}),
        "negative repair object id"
    );
    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplayFireSuperWeaponCommand{"", {}}),
        "empty superweapon id"
    );
    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplayFireSuperWeaponCommand{
            "ChronoSphereSpecial",
            {
                superWeaponTarget(1, 1),
                superWeaponTarget(2, 2),
                superWeaponTarget(3, 3)
            }
        }),
        "more than two superweapon targets"
    );
    success &= rejectedWithoutChangingOutput(
        command(RuntimeMultiplayerGameplayFireSuperWeaponCommand{
            "IronCurtainSpecial",
            {superWeaponTarget(1, 1, 0)}
        }),
        "invalid superweapon object target"
    );
    return success;
}

bool testLegalProtocolBoundaries() {
    bool success = true;
    RuntimeMultiplayerGameplayStopCommand maximumSelection;
    maximumSelection.objectIds.reserve(
        runtimeMultiplayerGameplayMaximumObjectIds
    );
    for (std::size_t index = 0;
        index < runtimeMultiplayerGameplayMaximumObjectIds;
        ++index) {
        maximumSelection.objectIds.push_back(
            static_cast<std::int32_t>(index + 1)
        );
    }

    RuntimeMultiplayerGameplayTargetOrderCommand primaryCell;
    primaryCell.order = RuntimeMultiplayerGameplayTargetOrderKind::Primary;
    primaryCell.objectIds = {std::numeric_limits<std::int32_t>::max()};
    primaryCell.target = cellTarget(
        runtimeMultiplayerGameplayMaximumCellCoordinate,
        runtimeMultiplayerGameplayMaximumCellCoordinate
    );

    RuntimeMultiplayerGameplayTargetOrderCommand primaryObject;
    primaryObject.order = RuntimeMultiplayerGameplayTargetOrderKind::Primary;
    primaryObject.objectIds = {1};
    primaryObject.target = objectTarget(
        std::numeric_limits<std::int32_t>::max()
    );

    const std::string maximumIdentifier(
        runtimeMultiplayerGameplayMaximumIdentifierBytes,
        'M'
    );
    const std::uint64_t maximumAllocatedSerial =
        std::numeric_limits<std::uint64_t>::max() - 1U;
    const std::vector<RuntimeMultiplayerGameplayCommand> boundaries{
        command(maximumSelection),
        command(RuntimeMultiplayerGameplayGuardCommand{
            maximumSelection.objectIds
        }),
        command(RuntimeMultiplayerGameplayScatterCommand{
            maximumSelection.objectIds
        }),
        command(primaryCell),
        command(primaryObject),
        command(RuntimeMultiplayerGameplayQueueProductionCommand{
            maximumIdentifier,
            RuntimeMultiplayerGameplayProductionInsertMode::AfterActive
        }),
        command(RuntimeMultiplayerGameplayCancelProductionCommand{
            RuntimeMultiplayerGameplayProductionLane::Ships,
            maximumAllocatedSerial
        }),
        command(RuntimeMultiplayerGameplaySetProductionHoldCommand{
            RuntimeMultiplayerGameplayProductionLane::Ships,
            maximumAllocatedSerial,
            false
        }),
        command(RuntimeMultiplayerGameplayPlaceBuildingCommand{
            RuntimeMultiplayerGameplayProductionLane::Armory,
            maximumAllocatedSerial,
            maximumIdentifier,
            {
                runtimeMultiplayerGameplayMaximumCellCoordinate,
                runtimeMultiplayerGameplayMaximumCellCoordinate
            }
        }),
        command(RuntimeMultiplayerGameplaySellCommand{
            std::numeric_limits<std::int32_t>::max()
        }),
        command(RuntimeMultiplayerGameplayFireSuperWeaponCommand{
            maximumIdentifier,
            {
                superWeaponTarget(999, 999),
                superWeaponTarget(
                    998,
                    997,
                    std::numeric_limits<std::int32_t>::max()
                )
            }
        })
    };

    std::size_t largestPayload = 0;
    for (const RuntimeMultiplayerGameplayCommand& source : boundaries) {
        std::vector<std::uint8_t> bytes;
        std::string error;
        RuntimeMultiplayerGameplayCommand decoded;
        const bool encoded = encodeRuntimeMultiplayerGameplayCommand(
            source,
            bytes,
            error
        );
        const bool decodedOk = encoded
            && decodeRuntimeMultiplayerGameplayCommand(
                bytes,
                decoded,
                error
            );
        success &= expect(
            decodedOk && decoded == source,
            "legal maximum protocol value round-trips: " + error
        );
        largestPayload = std::max(largestPayload, bytes.size());
    }
    success &= expect(
        largestPayload <= runtimeMultiplayerGameplayMaximumPayloadBytes,
        "the payload ceiling admits maximum selection and 256-byte item keys"
    );
    return success;
}

void writeU16(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint16_t value
) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 1] = static_cast<std::uint8_t>(value);
}

void writeU32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value
) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 3] = static_cast<std::uint8_t>(value);
}

bool malformedRejected(
    const std::vector<std::uint8_t>& bytes,
    const std::string& label
) {
    RuntimeMultiplayerGameplayCommand output = command(
        RuntimeMultiplayerGameplayRepairCommand{777}
    );
    const RuntimeMultiplayerGameplayCommand before = output;
    std::string error;
    return expect(
        !decodeRuntimeMultiplayerGameplayCommand(bytes, output, error)
            && output == before && !error.empty(),
        label + " is rejected without changing decode output"
    );
}

bool testMalformedWireIsAtomic() {
    bool success = true;
    std::vector<std::uint8_t> stopBytes;
    success &= encode(
        command(RuntimeMultiplayerGameplayStopCommand{{1, 2}}),
        stopBytes,
        "malformed stop seed"
    );

    for (std::size_t size = 0; size < stopBytes.size(); ++size) {
        success &= malformedRejected(
            std::vector<std::uint8_t>(
                stopBytes.begin(),
                stopBytes.begin() + static_cast<std::ptrdiff_t>(size)
            ),
            "truncated prefix " + std::to_string(size)
        );
    }

    std::vector<std::uint8_t> malformed = stopBytes;
    malformed.push_back(0xffU);
    success &= malformedRejected(malformed, "trailing byte");

    malformed = stopBytes;
    malformed[0] ^= 0xffU;
    success &= malformedRejected(malformed, "bad magic");

    malformed = stopBytes;
    writeU16(malformed, 4, 19);
    success &= malformedRejected(malformed, "old version-19 per-actor deployment commands");

    malformed = stopBytes;
    writeU16(malformed, 4, 9);
    success &= malformedRejected(malformed, "old version-9 radiation deployment timing");
    malformed = stopBytes;
    writeU16(malformed, 4, 10);
    success &= malformedRejected(malformed, "old version-10 nuclear flight state");
    malformed = stopBytes;
    writeU16(malformed, 4, 11);
    success &= malformedRejected(malformed, "old version-11 superweapon alliance damage");
    malformed = stopBytes;
    writeU16(malformed, 4, 12);
    success &= malformedRejected(malformed, "old version-12 scattered force-fire damage");
    malformed = stopBytes;
    writeU16(malformed, 4, 13);
    success &= malformedRejected(malformed, "old version-13 missile smoke cadence");
    malformed = stopBytes;
    writeU16(malformed, 4, 14);
    success &= malformedRejected(malformed, "old version-14 squid host behavior");

    malformed = stopBytes;
    writeU16(malformed, 4, 8);
    success &= malformedRejected(malformed, "old version-8 superweapon recharge timing");

    malformed = stopBytes;
    writeU16(malformed, 4, 7);
    success &= malformedRejected(malformed, "old version-7 vehicle particle placement");

    malformed = stopBytes;
    writeU16(malformed, 4, 6);
    success &= malformedRejected(malformed, "old version-6 factory exit semantics");

    malformed = stopBytes;
    writeU16(malformed, 4, 5);
    success &= malformedRejected(malformed, "old version-5 mining command semantics");

    malformed = stopBytes;
    writeU16(malformed, 4, 4);
    success &= malformedRejected(malformed, "old version-4 packet");

    malformed = stopBytes;
    writeU16(malformed, 4, 3);
    success &= malformedRejected(malformed, "old version-3 packet");

    malformed = stopBytes;
    writeU16(malformed, 4, 2);
    success &= malformedRejected(malformed, "old version-2 packet");

    malformed = stopBytes;
    writeU16(malformed, 4, 1);
    success &= malformedRejected(malformed, "old version-1 packet");

    malformed = stopBytes;
    writeU16(malformed, 6, 0xffffU);
    success &= malformedRejected(malformed, "unknown command type");

    malformed = stopBytes;
    // Stop layout: header, uint16 count, then big-endian uint32 ids.
    writeU32(malformed, 10, 2);
    writeU32(malformed, 14, 1);
    success &= malformedRejected(malformed, "unsorted wire object ids");

    malformed = stopBytes;
    writeU32(malformed, 10, 1);
    writeU32(malformed, 14, 1);
    success &= malformedRejected(malformed, "duplicate wire object ids");

    std::vector<std::uint8_t> guardBytes;
    success &= encode(
        command(RuntimeMultiplayerGameplayGuardCommand{{1, 2}}),
        guardBytes,
        "malformed Guard seed"
    );
    malformed = guardBytes;
    writeU32(malformed, 10, 2);
    writeU32(malformed, 14, 1);
    success &= malformedRejected(malformed, "unsorted Guard wire object ids");

    std::vector<std::uint8_t> scatterBytes;
    success &= encode(
        command(RuntimeMultiplayerGameplayScatterCommand{{1, 2}}),
        scatterBytes,
        "malformed Scatter seed"
    );
    malformed = scatterBytes;
    writeU32(malformed, 10, 1);
    writeU32(malformed, 14, 1);
    success &= malformedRejected(
        malformed,
        "duplicate Scatter wire object ids"
    );

    malformed.assign(stopBytes.begin(), stopBytes.begin() + 10);
    writeU16(
        malformed,
        8,
        static_cast<std::uint16_t>(
            runtimeMultiplayerGameplayMaximumObjectIds + 1
        )
    );
    success &= malformedRejected(malformed, "oversized wire object-id count");

    malformed.assign(stopBytes.begin(), stopBytes.begin() + 10);
    writeU16(malformed, 8, 0);
    success &= malformedRejected(malformed, "empty wire object-id set");

    std::vector<std::uint8_t> moveBytes;
    RuntimeMultiplayerGameplayTargetOrderCommand move;
    move.objectIds = {1};
    move.target = cellTarget(2, 3);
    success &= encode(command(move), moveBytes, "malformed move seed");

    malformed = moveBytes;
    malformed[8] = 0xffU;
    success &= malformedRejected(malformed, "unknown wire target order");

    malformed = moveBytes;
    malformed[15] = 0xffU;
    success &= malformedRejected(malformed, "unknown wire target kind");

    RuntimeMultiplayerGameplayTargetOrderCommand attack;
    attack.order = RuntimeMultiplayerGameplayTargetOrderKind::Attack;
    attack.objectIds = {1};
    attack.target = objectTarget(2);
    std::vector<std::uint8_t> attackBytes;
    success &= encode(command(attack), attackBytes, "malformed force-fire seed");
    malformed = attackBytes;
    malformed[8] = static_cast<std::uint8_t>(
        RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell
    );
    success &= malformedRejected(
        malformed,
        "wire force-fire with object target"
    );

    malformed = moveBytes;
    writeU32(
        malformed,
        16,
        static_cast<std::uint32_t>(
            runtimeMultiplayerGameplayMaximumCellCoordinate
        ) + 1U
    );
    success &= malformedRejected(malformed, "out-of-range wire coordinate");

    std::vector<std::uint8_t> queueBytes;
    success &= encode(
        command(RuntimeMultiplayerGameplayQueueProductionCommand{"MTNK"}),
        queueBytes,
        "malformed queue seed"
    );
    malformed = queueBytes;
    // Queue layout: header, insert-mode byte, uint16 string length, bytes.
    malformed[11] = 0x1fU;
    success &= malformedRejected(malformed, "invalid wire identifier");

    malformed.assign(queueBytes.begin(), queueBytes.begin() + 11);
    writeU16(malformed, 9, 0);
    success &= malformedRejected(malformed, "empty wire identifier");

    malformed = queueBytes;
    malformed[8] = 0xffU;
    success &= malformedRejected(malformed, "unknown wire insert mode");

    malformed = queueBytes;
    writeU16(
        malformed,
        9,
        static_cast<std::uint16_t>(
            runtimeMultiplayerGameplayMaximumIdentifierBytes + 1
        )
    );
    success &= malformedRejected(malformed, "oversized wire identifier length");

    std::vector<std::uint8_t> holdBytes;
    success &= encode(
        command(RuntimeMultiplayerGameplaySetProductionHoldCommand{
            RuntimeMultiplayerGameplayProductionLane::Vehicles,
            7,
            true
        }),
        holdBytes,
        "malformed production hold seed"
    );

    malformed = holdBytes;
    malformed.pop_back();
    success &= malformedRejected(malformed, "truncated production hold flag");

    malformed = holdBytes;
    malformed[8] = 0xffU;
    success &= malformedRejected(malformed, "unknown wire production hold lane");

    malformed = holdBytes;
    std::fill(
        malformed.begin() + 9,
        malformed.begin() + 17,
        static_cast<std::uint8_t>(0)
    );
    success &= malformedRejected(malformed, "zero wire active production serial");

    malformed = holdBytes;
    std::fill(
        malformed.begin() + 9,
        malformed.begin() + 17,
        static_cast<std::uint8_t>(0xffU)
    );
    success &= malformedRejected(
        malformed,
        "unallocatable maximum wire active production serial"
    );

    malformed = holdBytes;
    malformed[17] = 2U;
    success &= malformedRejected(
        malformed,
        "non-canonical wire production hold boolean"
    );

    std::vector<std::uint8_t> sellBytes;
    success &= encode(
        command(RuntimeMultiplayerGameplaySellCommand{
            std::numeric_limits<std::int32_t>::max()
        }),
        sellBytes,
        "malformed sell seed"
    );
    malformed = sellBytes;
    writeU32(malformed, 8, 0x80000000U);
    success &= malformedRejected(malformed, "wire object id above INT32_MAX");

    std::vector<std::uint8_t> superWeaponBytes;
    success &= encode(
        command(RuntimeMultiplayerGameplayFireSuperWeaponCommand{
            "ForceShieldSpecial",
            {superWeaponTarget(50, 60, 321)}
        }),
        superWeaponBytes,
        "malformed compound superweapon seed"
    );
    // Header + uint16 id length + id + target count + cell = presence flag.
    const std::size_t presenceOffset = 8U + 2U
        + std::string("ForceShieldSpecial").size() + 1U + 8U;
    malformed = superWeaponBytes;
    malformed[presenceOffset] = 2U;
    success &= malformedRejected(
        malformed,
        "invalid superweapon object-presence flag"
    );

    malformed.assign(
        runtimeMultiplayerGameplayMaximumPayloadBytes + 1,
        0
    );
    success &= malformedRejected(malformed, "oversized wire payload");
    return success;
}

bool testFixedGroupSecondaryIntents() {
    bool success = true;
    for (const auto action : {
            RuntimeMultiplayerGameplaySecondaryActionKind::DeployAll,
            RuntimeMultiplayerGameplaySecondaryActionKind::UndeployAll}) {
        const auto source = command(RuntimeMultiplayerGameplaySecondaryActionCommand{
            action, {9, 1, 9, 3}});
        const auto canonical = command(RuntimeMultiplayerGameplaySecondaryActionCommand{
            action, {1, 3, 9}});
        auto normalized = source;
        std::string error;
        success &= expect(canonicalizeRuntimeMultiplayerGameplayCommand(normalized, error)
                && normalized == canonical,
            "group deployment direction survives ID canonicalization: " + error);
        std::vector<std::uint8_t> bytes;
        if (!encode(source, bytes, "fixed-direction group command")) {
            success = false;
            continue;
        }
        const std::vector<std::uint8_t> golden{
            0x43U, 0x47U, 0x43U, 0x4dU, 0x00U, 0x14U, 0x00U, 0x03U,
            static_cast<std::uint8_t>(action), 0x00U, 0x03U,
            0x00U, 0x00U, 0x00U, 0x01U,
            0x00U, 0x00U, 0x00U, 0x03U,
            0x00U, 0x00U, 0x00U, 0x09U};
        RuntimeMultiplayerGameplayCommand decoded;
        success &= expect(bytes == golden
                && decodeRuntimeMultiplayerGameplayCommand(bytes, decoded, error)
                && decoded == canonical
                && std::get<RuntimeMultiplayerGameplaySecondaryActionCommand>(source.body).objectIds
                    == std::vector<std::int32_t>{9, 1, 9, 3},
            "version20 preserves the explicit direction and every canonical actor without changing its caller");
        success &= rejectedWithoutChangingOutput(
            command(RuntimeMultiplayerGameplaySecondaryActionCommand{action, {}}),
            "empty fixed-direction group");
        success &= rejectedWithoutChangingOutput(
            command(RuntimeMultiplayerGameplaySecondaryActionCommand{action, {0, 1}}),
            "zero ID in fixed-direction group");

        RuntimeMultiplayerGameplaySecondaryActionCommand maximum{action, {}};
        for (std::size_t i = 0; i < runtimeMultiplayerGameplayMaximumObjectIds; ++i)
            maximum.objectIds.push_back(static_cast<std::int32_t>(i + 1));
        std::vector<std::uint8_t> maximumBytes;
        if (!encode(command(maximum), maximumBytes, "256-actor fixed-direction fragment")) {
            success = false;
            continue;
        }
        success &= expect(maximumBytes.size() == 11U + 4U * 256U
                && decodeRuntimeMultiplayerGameplayCommand(maximumBytes, decoded, error)
                && decoded == command(maximum),
            "all 256 actors and their common direction round-trip in one legal fragment");
        maximum.objectIds.push_back(257);
        success &= rejectedWithoutChangingOutput(command(maximum), "257-actor fixed-direction group");

        auto malformed = bytes;
        malformed.resize(11U);
        writeU16(malformed, 9U, 0U);
        success &= malformedRejected(malformed, "empty fixed-direction wire selection");
        malformed = bytes;
        writeU32(malformed, 11U, 0U);
        success &= malformedRejected(malformed, "zero fixed-direction wire actor");
        malformed = bytes;
        writeU32(malformed, 11U, 3U);
        success &= malformedRejected(malformed, "duplicate fixed-direction wire actor");
        malformed = maximumBytes;
        writeU16(malformed, 9U, 257U);
        malformed.insert(malformed.end(), {0x00U, 0x00U, 0x01U, 0x01U});
        success &= malformedRejected(malformed, "257 fixed-direction wire actors");

        // Unknown tags must not degrade to contextual per-actor toggling.
        for (unsigned int tag = 0; tag <= 255U; ++tag) {
            if (tag >= 1U && tag <= 4U) continue;
            const auto unknown = static_cast<RuntimeMultiplayerGameplaySecondaryActionKind>(tag);
            success &= rejectedWithoutChangingOutput(
                command(RuntimeMultiplayerGameplaySecondaryActionCommand{unknown, {1}}),
                "unknown group action source tag " + std::to_string(tag));
            malformed = bytes;
            malformed[8] = static_cast<std::uint8_t>(tag);
            success &= malformedRejected(malformed,
                "unknown group action wire tag " + std::to_string(tag));
        }
    }
    return success;
}

bool testPlayerCommandState() {
    bool ok = true;
    RuntimePlayerCommandState state;
    RuntimeMultiplayerGameplayTargetOrderCommand first{
        RuntimeMultiplayerGameplayTargetOrderKind::Move, {1, 2},
        RuntimeMultiplayerGameplayCellTarget{{7, 8}}, true};
    ok &= expect(state.append("player", first), "route accepts a selected formation");
    state.following[3] = {"player", 4, {-1, -1}};
    state.primaryFactories[{"player", "UNITTYPE:LAND"}] = 9;
    state.rallyPoints[9] = {"player", {4, 5}};
    state.travelling[1] = {"player", {7, 8}, RuntimeMultiplayerGameplayTargetOrderKind::AttackMove, false, 15};
    state.travelling[5] = {"player", {9, 8}, RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination, true, 15};
    std::string error;
    RuntimePlayerCommandState restored;
    ok &= expect(restored.restore(state.serialize(), error)
        && restored.serialize() == state.serialize()
        && restored.stateHash() == state.stateHash(), "route/follow/factory state round trip");
    const auto previous = restored.serialize();
    ok &= expect(!restored.restore(previous + " trailing", error)
        && restored.serialize() == previous, "invalid route save rejected atomically");
    for (std::size_t i = 1; i < RuntimePlayerCommandState::maximumOrdersPerObject; ++i)
        ok &= expect(state.append("player", first), "bounded route capacity");
    const auto full = state.serialize();
    ok &= expect(!state.append("player", first) && state.serialize() == full,
        "overflow rejects the entire route atomically");
    state.cancel({1, 3});
    ok &= expect(!state.queues.contains(1) && !state.following.contains(3) && !state.travelling.contains(1)
        && state.travelling.contains(5)
        && state.queues.contains(2), "replacement cancels only selected actors");
    RuntimePlayerLocalCommands local;
    auto alive = [](int id) { return id > 0 && id != 2; };
    local.teamCreate(0, {3, 2, 1, 1}, alive);
    ok &= expect(local.teamSelect(0, {4}, true, alive) == std::vector<int>({1, 3, 4}),
        "teams filter dead actors and merge selection");
    ok &= expect(runtimePlayerKeyCode('1', false, true, false) == 561
        && runtimePlayerKeyCode('1', true, false, false) == 305
        && runtimePlayerKeyCode(112, false, true, false) == 624,
        "modifier encoding matches retail keyboardmd.ini");
    RuntimeMultiplayerGameplayCommand encodedCommand; encodedCommand.body = first;
    std::vector<std::uint8_t> bytes;
    ok &= expect(encodeRuntimeMultiplayerGameplayCommand(encodedCommand, bytes, error), "append encoding");
    bytes.back() = 2;
    ok &= expect(!decodeRuntimeMultiplayerGameplayCommand(bytes, encodedCommand, error),
        "noncanonical append boolean is rejected");
    return ok;
}

bool testPlayerCommunicationValidation() {
    bool ok = true; std::string error;
    for (auto kind : {RuntimePlayerCommunicationKind::ChatAll,
            RuntimePlayerCommunicationKind::ChatAllies}) {
        RuntimeMultiplayerGameplayPlayerCommunicationCommand body;
        body.kind = kind; body.text = "recipient bitmap";
        std::vector<std::uint8_t> broadcast;
        if (!encode(command(body), broadcast, "legacy chat broadcast")) {
            ok = false; continue;
        }
        // Includes legacy 0, self-only slots 0/7 (1/128), and all slots (255).
        // This schema cannot infer the authenticated sender or enabled slots.
        for (int value = 0; value <= 255; ++value) {
            body.value = static_cast<std::uint8_t>(value);
            const auto source = command(body);
            std::vector<std::uint8_t> output;
            if (!encode(source, output, "chat recipient byte")) {
                ok = false; continue;
            }
            auto expected = broadcast;
            expected.back() = body.value;
            RuntimeMultiplayerGameplayCommand decoded;
            std::vector<std::uint8_t> replay;
            ok &= expect(output == expected
                    && decodeRuntimeMultiplayerGameplayCommand(output, decoded, error)
                    && decoded == source
                    && encodeRuntimeMultiplayerGameplayCommand(decoded, replay, error)
                    && replay == output,
                "chat bitmap round-trips unchanged in the existing byte: "
                    + std::to_string(value));
        }
        body.value = 255; body.cell = {1, 0};
        std::vector<std::uint8_t> unchanged{7, 8, 9};
        ok &= expect(!encodeRuntimeMultiplayerGameplayCommand(command(body), unchanged, error)
                && unchanged == std::vector<std::uint8_t>({7, 8, 9}),
            "chat recipient bitmap cannot bypass the empty-cell rule");
    }
    for (auto kind : {RuntimePlayerCommunicationKind::Alliance,
            RuntimePlayerCommunicationKind::PlaceBeacon,
            RuntimePlayerCommunicationKind::DeleteBeacon,
            RuntimePlayerCommunicationKind::BeaconText,
            RuntimePlayerCommunicationKind::Cheer,
            RuntimePlayerCommunicationKind::Taunt}) {
        RuntimeMultiplayerGameplayPlayerCommunicationCommand body;
        body.kind = kind;
        if (kind == RuntimePlayerCommunicationKind::Alliance) body.text = "bravo";
        if (kind == RuntimePlayerCommunicationKind::PlaceBeacon
            || kind == RuntimePlayerCommunicationKind::DeleteBeacon
            || kind == RuntimePlayerCommunicationKind::BeaconText) body.cell = {12, 13};
        if (kind == RuntimePlayerCommunicationKind::BeaconText) body.text = "from here";
        if (kind == RuntimePlayerCommunicationKind::Taunt) body.value = 1;
        std::vector<std::uint8_t> validBytes;
        if (!encode(command(body), validBytes, "unchanged non-chat value rule")) {
            ok = false; continue;
        }
        for (int value = 0; value <= 255; ++value) {
            body.value = static_cast<std::uint8_t>(value);
            const bool allowed = kind == RuntimePlayerCommunicationKind::Alliance
                ? value <= 1 : kind == RuntimePlayerCommunicationKind::Taunt
                ? value >= 1 && value <= 8 : value == 0;
            ok &= expect(validateRuntimeMultiplayerGameplayCommand(command(body), error) == allowed,
                "non-chat value domain unchanged for kind "
                    + std::to_string(static_cast<int>(kind)) + ": " + std::to_string(value));
            if (allowed) continue;
            std::vector<std::uint8_t> unchanged{7, 8, 9};
            auto malformed = validBytes;
            malformed.back() = body.value;
            const auto original = command(RuntimeMultiplayerGameplayRepairCommand{777});
            auto decoded = original;
            ok &= expect(!encodeRuntimeMultiplayerGameplayCommand(command(body), unchanged, error)
                    && unchanged == std::vector<std::uint8_t>({7, 8, 9})
                    && !decodeRuntimeMultiplayerGameplayCommand(malformed, decoded, error)
                    && decoded == original,
                "non-chat illegal value rejects atomically in both directions");
        }
    }
    for (const auto& text : std::vector<std::string>{"",std::string(257,'a'),std::string("a\nb"),
            std::string("\xc0\xaf"),std::string("\xed\xa0\x80"),std::string("\xf4\x90\x80\x80")}) {
        RuntimeMultiplayerGameplayPlayerCommunicationCommand body;
        body.text = text;
        std::vector<std::uint8_t> output{7,8,9};
        ok &= expect(!encodeRuntimeMultiplayerGameplayCommand(command(body),output,error)
            && output==std::vector<std::uint8_t>({7,8,9}),"invalid chat rejected atomically");
    }
    for (int value : {0,9,255}) {
        RuntimeMultiplayerGameplayPlayerCommunicationCommand body;
        body.kind=RuntimePlayerCommunicationKind::Taunt; body.value=static_cast<std::uint8_t>(value);
        ok &= expect(!validateRuntimeMultiplayerGameplayCommand(command(body),error),"invalid taunt rejected");
    }
    RuntimePlayerCommandState old;
    RuntimeMultiplayerGameplayTargetOrderCommand move{RuntimeMultiplayerGameplayTargetOrderKind::Move,
        {1},RuntimeMultiplayerGameplayCellTarget{{4,5}},false};
    std::vector<std::uint8_t> bytes; encodeRuntimeMultiplayerGameplayCommand(command(move),bytes,error);
    RuntimeMultiplayerGameplayCommand decoded;
    for (int version : {15, 16, 17, 18}) {
        bytes[5] = static_cast<std::uint8_t>(version);
        std::string hex; constexpr char digits[]="0123456789abcdef";
        for (auto byte:bytes) { hex+=digits[byte>>4];hex+=digits[byte&15]; }
        ok &= expect(old.restore("1 1 1 \"player\" 1 " + hex + " 0 0 0",error)
            && old.queues.at(1).orders.front().target==move.target,"old queued save migrates to v18");
        ok &= expect(!decodeRuntimeMultiplayerGameplayCommand(bytes,decoded,error),"old network payload stays incompatible");
    }
    for (auto kind : {RuntimeMultiplayerGameplayTargetOrderKind::AttackMove,
            RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination}) {
        move.order = kind; move.target = RuntimeMultiplayerGameplayObjectTarget{3};
        ok &= expect(!validateRuntimeMultiplayerGameplayCommand(command(move), error),
            "travel orders cannot smuggle an object target");
    }
    return ok;
}

bool testWaypointPlanningState() {
    bool ok = true; std::string error;
    RuntimePlayerCommandState state;
    auto& plans = state.waypoints;
    plans.planningOwners.insert("player");
    RuntimeMultiplayerGameplayTargetOrderCommand order{RuntimeMultiplayerGameplayTargetOrderKind::Move,
        {1, 2}, RuntimeMultiplayerGameplayCellTarget{{4, 5}}, true};
    for (int i = 0; i < 3; ++i) {
        order.target = RuntimeMultiplayerGameplayCellTarget{{4 + i * 5, 5}};
        ok &= expect(plans.append("player", order), "waypoint formation added");
    }
    plans.routes.at(1).loopStart = plans.routes.at(1).points[1].id;
    plans.routes.at(1).loopCommitted = true;
    plans.routes.at(1).cursor = 2;
    plans.routes.at(1).active = true;
    plans.routes.at(1).nextAttemptTick = 71;
    RuntimePlayerCommandState restored;
    ok &= expect(restored.restore(state.serialize(), error)
        && restored.serialize() == state.serialize() && restored.stateHash() == state.stateHash(),
        "held groups, loop cursor and active leg survive save/restore");
    ok &= expect(plans.ownsPoint("player", 2) && !plans.ownsPoint("enemy", 2), "point ownership is scoped");
    plans.routes.at(2).erasePoint(2);
    ok &= expect(plans.routes.at(2).points.size() == 2 && plans.routes.at(2).points[1].id == 3,
        "deleting only the middle point connects the two neighbors");
    plans.routes.at(1).finishPoint();
    ok &= expect(plans.routes.at(1).cursor == 1 && plans.routes.at(1).points.size() == 3,
        "patrol wraps to the chosen loop origin");
    state.cancel({1});
    ok &= expect(!plans.routes.contains(1) && plans.routes.contains(2), "Stop cancels only the selected patrol");
    for (std::size_t i = plans.routes.at(2).points.size(); i < RuntimePlayerWaypointState::maximumPoints; ++i) {
        order.objectIds = {2}; plans.append("player", order);
    }
    const auto full = state.serialize(); order.objectIds = {2, 3};
    ok &= expect(!plans.append("player", order) && state.serialize() == full,
        "full waypoint route rejects the complete formation without partial points");
    ok &= expect(restored.restore("3 0 0 0 0 0 0", error) && restored.waypoints.routes.empty(),
        "pre-waypoint save remains readable");
    for (const auto bad : {RuntimeMultiplayerGameplayWaypointCommand{RuntimePlayerWaypointOperation::Begin, 1},
            RuntimeMultiplayerGameplayWaypointCommand{RuntimePlayerWaypointOperation::DeletePoint, 0},
            RuntimeMultiplayerGameplayWaypointCommand{static_cast<RuntimePlayerWaypointOperation>(255), 1}})
        ok &= expect(!validateRuntimeMultiplayerGameplayCommand(command(bad), error), "malformed planning intent rejected");
    return ok;
}

} // namespace

int main() {
    bool success = true;
    success &= testRoundTripsAndAllOperations();
    success &= testGoldenWireFormatAndCanonicalization();
    success &= testStrictSourceValidation();
    success &= testLegalProtocolBoundaries();
    success &= testMalformedWireIsAtomic();
    success &= testFixedGroupSecondaryIntents();
    success &= testPlayerCommandState();
    success &= testPlayerCommunicationValidation();
    success &= testWaypointPlanningState();
    if (!success) return 1;

    std::cout
        << "Runtime multiplayer gameplay command smoke: version=20"
        << " operations=16 endian=big canonicalIds=yes"
        << " ownerOnWire=no validation=strict\n";
    return 0;
}
