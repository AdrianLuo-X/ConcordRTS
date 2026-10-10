#include "network/lockstep/RuntimeMultiplayerGameplayCommand.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

class ByteWriter {
public:
    void writeU8(std::uint8_t value) {
        bytes_.push_back(value);
    }

    void writeU16(std::uint16_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value >> 8U));
        bytes_.push_back(static_cast<std::uint8_t>(value));
    }

    void writeU32(std::uint32_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value >> 24U));
        bytes_.push_back(static_cast<std::uint8_t>(value >> 16U));
        bytes_.push_back(static_cast<std::uint8_t>(value >> 8U));
        bytes_.push_back(static_cast<std::uint8_t>(value));
    }

    void writeU64(std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            bytes_.push_back(
                static_cast<std::uint8_t>(value >> shift)
            );
        }
    }

    void writeString(const std::string& value) {
        writeU16(static_cast<std::uint16_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    const std::vector<std::uint8_t>& bytes() const {
        return bytes_;
    }

private:
    std::vector<std::uint8_t> bytes_;
};

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> bytes)
        : bytes_(bytes) {}

    bool readU8(std::uint8_t& value) {
        if (!require(1)) return false;
        value = bytes_[offset_++];
        return true;
    }

    bool readU16(std::uint16_t& value) {
        if (!require(2)) return false;
        value = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes_[offset_]) << 8U)
            | static_cast<std::uint16_t>(bytes_[offset_ + 1])
        );
        offset_ += 2;
        return true;
    }

    bool readU32(std::uint32_t& value) {
        if (!require(4)) return false;
        value =
            (static_cast<std::uint32_t>(bytes_[offset_]) << 24U)
            | (static_cast<std::uint32_t>(bytes_[offset_ + 1]) << 16U)
            | (static_cast<std::uint32_t>(bytes_[offset_ + 2]) << 8U)
            | static_cast<std::uint32_t>(bytes_[offset_ + 3]);
        offset_ += 4;
        return true;
    }

    bool readU64(std::uint64_t& value) {
        if (!require(8)) return false;
        value = 0;
        for (int index = 0; index < 8; ++index) {
            value = (value << 8U)
                | static_cast<std::uint64_t>(bytes_[offset_ + index]);
        }
        offset_ += 8;
        return true;
    }

    bool readString(
        std::size_t maximumBytes,
        std::string& value,
        const char* fieldName
    ) {
        std::uint16_t length = 0;
        if (!readU16(length)) return false;
        if (static_cast<std::size_t>(length) > maximumBytes) {
            return fail(
                std::string("gameplay command ") + fieldName
                    + " exceeds the size limit"
            );
        }
        if (!require(static_cast<std::size_t>(length))) return false;
        value.assign(
            reinterpret_cast<const char*>(bytes_.data() + offset_),
            static_cast<std::size_t>(length)
        );
        offset_ += static_cast<std::size_t>(length);
        return true;
    }

    bool finished() const {
        return offset_ == bytes_.size();
    }

    bool fail(std::string error) {
        if (error_.empty()) error_ = std::move(error);
        return false;
    }

    const std::string& error() const {
        return error_;
    }

private:
    bool require(std::size_t count) {
        if (count <= bytes_.size() - offset_) return true;
        return fail("gameplay command payload is truncated");
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
    std::string error_;
};

bool fail(std::string& errorMessage, std::string message) {
    errorMessage = std::move(message);
    return false;
}

bool validIdentifier(const std::string& value) {
    if (value.empty()
        || value.size() > runtimeMultiplayerGameplayMaximumIdentifierBytes) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char byte) {
        // Production item keys are opaque INI identifiers and the existing
        // lane core permits spaces and high bytes. Preserve that contract,
        // while rejecting embedded controls which have no stable text form.
        return byte >= 0x20U && byte != 0x7fU;
    });
}

bool validPlayerChatText(const std::string& text) {
    if (text.empty() || text.size() > runtimeMultiplayerGameplayMaximumIdentifierBytes) return false;
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        std::uint32_t value = first, minimum = 0; int count = 0;
        if (first >= 0xc2 && first <= 0xdf) { count=1; value=first & 31; minimum=0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count=2; value=first & 15; minimum=0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count=3; value=first & 7; minimum=0x10000; }
        else if (first >= 0x80) return false;
        while (count--) {
            if (i == text.size()) return false;
            const auto tail = static_cast<unsigned char>(text[i++]);
            if ((tail & 0xc0) != 0x80) return false;
            value = (value << 6) | (tail & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)
            || value < 32 || (value >= 127 && value <= 159)) return false;
    }
    return true;
}

bool validObjectId(std::int32_t objectId) {
    return objectId > 0;
}

bool validProductionSerial(std::uint64_t serial) {
    // RuntimeProductionLanes reserves zero for "no order" and cannot allocate
    // UINT64_MAX because advancing nextSerial would overflow. Every serial it
    // can actually produce, including UINT64_MAX-1, remains representable.
    return serial != 0
        && serial != std::numeric_limits<std::uint64_t>::max();
}

bool validateCell(
    const RuntimeMultiplayerGameplayCell& cell,
    std::string& errorMessage
) {
    if (cell.x < 0 || cell.y < 0
        || cell.x > runtimeMultiplayerGameplayMaximumCellCoordinate
        || cell.y > runtimeMultiplayerGameplayMaximumCellCoordinate) {
        return fail(errorMessage, "gameplay command cell is out of range");
    }
    return true;
}

bool validateTarget(
    const RuntimeMultiplayerGameplayTarget& target,
    std::string& errorMessage
) {
    if (const auto* cell =
            std::get_if<RuntimeMultiplayerGameplayCellTarget>(&target)) {
        return validateCell(cell->cell, errorMessage);
    }
    const auto* object =
        std::get_if<RuntimeMultiplayerGameplayObjectTarget>(&target);
    if (object == nullptr || !validObjectId(object->objectId)) {
        return fail(errorMessage, "gameplay command target object id is invalid");
    }
    return true;
}

bool validateSuperWeaponTarget(
    const RuntimeMultiplayerGameplaySuperWeaponTarget& target,
    std::string& errorMessage
) {
    if (!validateCell(target.cell, errorMessage)) return false;
    if (target.objectId.has_value()
        && !validObjectId(*target.objectId)) {
        return fail(
            errorMessage,
            "gameplay command superweapon target object id is invalid"
        );
    }
    return true;
}

bool validateObjectIds(
    const std::vector<std::int32_t>& objectIds,
    std::string& errorMessage
) {
    if (objectIds.empty()) {
        return fail(errorMessage, "gameplay command object-id set is empty");
    }
    if (objectIds.size() > runtimeMultiplayerGameplayMaximumObjectIds) {
        return fail(
            errorMessage,
            "gameplay command object-id set exceeds the size limit"
        );
    }
    std::int32_t previous = 0;
    for (std::int32_t objectId : objectIds) {
        if (!validObjectId(objectId)) {
            return fail(errorMessage, "gameplay command object id is invalid");
        }
        if (objectId <= previous) {
            return fail(
                errorMessage,
                "gameplay command object ids are not canonical"
            );
        }
        previous = objectId;
    }
    return true;
}

bool canonicalizeObjectIds(
    std::vector<std::int32_t>& objectIds,
    std::string& errorMessage
) {
    if (objectIds.size() > runtimeMultiplayerGameplayMaximumObjectIds) {
        return fail(
            errorMessage,
            "gameplay command object-id set exceeds the size limit"
        );
    }
    std::sort(objectIds.begin(), objectIds.end());
    objectIds.erase(
        std::unique(objectIds.begin(), objectIds.end()),
        objectIds.end()
    );
    return validateObjectIds(objectIds, errorMessage);
}

bool validTargetOrder(RuntimeMultiplayerGameplayTargetOrderKind order) {
    switch (order) {
    case RuntimeMultiplayerGameplayTargetOrderKind::Move:
    case RuntimeMultiplayerGameplayTargetOrderKind::Attack:
    case RuntimeMultiplayerGameplayTargetOrderKind::Primary:
    case RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell:
    case RuntimeMultiplayerGameplayTargetOrderKind::Follow:
    case RuntimeMultiplayerGameplayTargetOrderKind::AttackMove:
    case RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination:
        return true;
    }
    return false;
}

bool validSecondaryAction(
    RuntimeMultiplayerGameplaySecondaryActionKind action
) {
    switch (action) {
    case RuntimeMultiplayerGameplaySecondaryActionKind::Contextual:
    case RuntimeMultiplayerGameplaySecondaryActionKind::DeployToggle:
    case RuntimeMultiplayerGameplaySecondaryActionKind::DeployAll:
    case RuntimeMultiplayerGameplaySecondaryActionKind::UndeployAll:
        return true;
    }
    return false;
}

bool validProductionLane(RuntimeMultiplayerGameplayProductionLane lane) {
    switch (lane) {
    case RuntimeMultiplayerGameplayProductionLane::Structures:
    case RuntimeMultiplayerGameplayProductionLane::Armory:
    case RuntimeMultiplayerGameplayProductionLane::Infantry:
    case RuntimeMultiplayerGameplayProductionLane::Vehicles:
    case RuntimeMultiplayerGameplayProductionLane::Aircraft:
    case RuntimeMultiplayerGameplayProductionLane::Ships:
        return true;
    }
    return false;
}

bool validProductionInsertMode(
    RuntimeMultiplayerGameplayProductionInsertMode mode
) {
    switch (mode) {
    case RuntimeMultiplayerGameplayProductionInsertMode::Back:
    case RuntimeMultiplayerGameplayProductionInsertMode::AfterActive:
        return true;
    }
    return false;
}

bool validateIdentifier(
    const std::string& identifier,
    const char* fieldName,
    std::string& errorMessage
) {
    if (validIdentifier(identifier)) return true;
    return fail(
        errorMessage,
        std::string("gameplay command ") + fieldName + " is invalid"
    );
}

void writeObjectIds(
    ByteWriter& writer,
    const std::vector<std::int32_t>& objectIds
) {
    writer.writeU16(static_cast<std::uint16_t>(objectIds.size()));
    for (std::int32_t objectId : objectIds) {
        writer.writeU32(static_cast<std::uint32_t>(objectId));
    }
}

bool readObjectIds(
    ByteReader& reader,
    std::vector<std::int32_t>& objectIds
) {
    std::uint16_t count = 0;
    if (!reader.readU16(count)) return false;
    if (static_cast<std::size_t>(count)
        > runtimeMultiplayerGameplayMaximumObjectIds) {
        return reader.fail(
            "gameplay command object-id set exceeds the size limit"
        );
    }
    objectIds.clear();
    objectIds.reserve(static_cast<std::size_t>(count));
    for (std::uint16_t index = 0; index < count; ++index) {
        std::uint32_t objectId = 0;
        if (!reader.readU32(objectId)) return false;
        if (objectId
            > static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max()
            )) {
            return reader.fail("gameplay command object id is out of range");
        }
        objectIds.push_back(static_cast<std::int32_t>(objectId));
    }
    return true;
}

void writeCell(
    ByteWriter& writer,
    const RuntimeMultiplayerGameplayCell& cell
) {
    writer.writeU32(static_cast<std::uint32_t>(cell.x));
    writer.writeU32(static_cast<std::uint32_t>(cell.y));
}

bool readCell(
    ByteReader& reader,
    RuntimeMultiplayerGameplayCell& cell
) {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    if (!reader.readU32(x) || !reader.readU32(y)) return false;
    if (x > static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max())
        || y > static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max())) {
        return reader.fail("gameplay command cell is out of range");
    }
    cell.x = static_cast<std::int32_t>(x);
    cell.y = static_cast<std::int32_t>(y);
    return true;
}

void writeTarget(
    ByteWriter& writer,
    const RuntimeMultiplayerGameplayTarget& target
) {
    if (const auto* cell =
            std::get_if<RuntimeMultiplayerGameplayCellTarget>(&target)) {
        writer.writeU8(1);
        writeCell(writer, cell->cell);
        return;
    }
    writer.writeU8(2);
    writer.writeU32(
        static_cast<std::uint32_t>(
            std::get<RuntimeMultiplayerGameplayObjectTarget>(target).objectId
        )
    );
}

bool readTarget(
    ByteReader& reader,
    RuntimeMultiplayerGameplayTarget& target
) {
    std::uint8_t kind = 0;
    if (!reader.readU8(kind)) return false;
    if (kind == 1) {
        RuntimeMultiplayerGameplayCellTarget cell;
        if (!readCell(reader, cell.cell)) return false;
        target = cell;
        return true;
    }
    if (kind == 2) {
        std::uint32_t objectId = 0;
        if (!reader.readU32(objectId)) return false;
        if (objectId
            > static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max()
            )) {
            return reader.fail(
                "gameplay command target object id is out of range"
            );
        }
        target = RuntimeMultiplayerGameplayObjectTarget{
            static_cast<std::int32_t>(objectId)
        };
        return true;
    }
    return reader.fail("gameplay command target kind is unknown");
}

void writeSuperWeaponTarget(
    ByteWriter& writer,
    const RuntimeMultiplayerGameplaySuperWeaponTarget& target
) {
    writeCell(writer, target.cell);
    writer.writeU8(target.objectId.has_value() ? 1U : 0U);
    if (target.objectId.has_value()) {
        writer.writeU32(static_cast<std::uint32_t>(*target.objectId));
    }
}

bool readSuperWeaponTarget(
    ByteReader& reader,
    RuntimeMultiplayerGameplaySuperWeaponTarget& target
) {
    if (!readCell(reader, target.cell)) return false;
    std::uint8_t hasObject = 0;
    if (!reader.readU8(hasObject)) return false;
    if (hasObject > 1U) {
        return reader.fail(
            "gameplay command superweapon object presence is invalid"
        );
    }
    target.objectId.reset();
    if (hasObject == 0U) return true;
    std::uint32_t objectId = 0;
    if (!reader.readU32(objectId)) return false;
    if (objectId > static_cast<std::uint32_t>(
            std::numeric_limits<std::int32_t>::max())) {
        return reader.fail(
            "gameplay command superweapon target object id is out of range"
        );
    }
    target.objectId = static_cast<std::int32_t>(objectId);
    return true;
}

bool decodeCommandType(
    std::uint16_t encoded,
    RuntimeMultiplayerGameplayCommandType& type,
    std::string& errorMessage
) {
    switch (encoded) {
    case static_cast<std::uint16_t>(
            RuntimeMultiplayerGameplayCommandType::TargetOrder):
    case static_cast<std::uint16_t>(
            RuntimeMultiplayerGameplayCommandType::Stop):
    case static_cast<std::uint16_t>(
            RuntimeMultiplayerGameplayCommandType::SecondaryAction):
    case static_cast<std::uint16_t>(
            RuntimeMultiplayerGameplayCommandType::QueueProduction):
    case static_cast<std::uint16_t>(
            RuntimeMultiplayerGameplayCommandType::CancelProduction):
    case static_cast<std::uint16_t>(
            RuntimeMultiplayerGameplayCommandType::PlaceBuilding):
    case static_cast<std::uint16_t>(
            RuntimeMultiplayerGameplayCommandType::Sell):
    case static_cast<std::uint16_t>(
            RuntimeMultiplayerGameplayCommandType::Repair):
    case static_cast<std::uint16_t>(
            RuntimeMultiplayerGameplayCommandType::FireSuperWeapon):
    case static_cast<std::uint16_t>(
            RuntimeMultiplayerGameplayCommandType::Guard):
    case static_cast<std::uint16_t>(
            RuntimeMultiplayerGameplayCommandType::Scatter):
    case static_cast<std::uint16_t>(
            RuntimeMultiplayerGameplayCommandType::SetProductionHold):
    case static_cast<std::uint16_t>(
            RuntimeMultiplayerGameplayCommandType::SetPrimaryFactory):
    case static_cast<std::uint16_t>(
            RuntimeMultiplayerGameplayCommandType::SetRallyPoint):
    case static_cast<std::uint16_t>(
            RuntimeMultiplayerGameplayCommandType::PlayerCommunication):
    case static_cast<std::uint16_t>(
            RuntimeMultiplayerGameplayCommandType::WaypointPlanning):
        type = static_cast<RuntimeMultiplayerGameplayCommandType>(encoded);
        return true;
    default:
        return fail(errorMessage, "gameplay command type is unknown");
    }
}

} // namespace

RuntimeMultiplayerGameplayCommandType runtimeMultiplayerGameplayCommandType(
    const RuntimeMultiplayerGameplayCommand& command
) {
    return std::visit(
        [](const auto& body) {
            using Body = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayTargetOrderCommand>) {
                return RuntimeMultiplayerGameplayCommandType::TargetOrder;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayStopCommand>) {
                return RuntimeMultiplayerGameplayCommandType::Stop;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplaySecondaryActionCommand>) {
                return RuntimeMultiplayerGameplayCommandType::SecondaryAction;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayQueueProductionCommand>) {
                return RuntimeMultiplayerGameplayCommandType::QueueProduction;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayCancelProductionCommand>) {
                return RuntimeMultiplayerGameplayCommandType::CancelProduction;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayPlaceBuildingCommand>) {
                return RuntimeMultiplayerGameplayCommandType::PlaceBuilding;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplaySellCommand>) {
                return RuntimeMultiplayerGameplayCommandType::Sell;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayRepairCommand>) {
                return RuntimeMultiplayerGameplayCommandType::Repair;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayFireSuperWeaponCommand>) {
                return RuntimeMultiplayerGameplayCommandType::FireSuperWeapon;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayGuardCommand>) {
                return RuntimeMultiplayerGameplayCommandType::Guard;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayScatterCommand>) {
                return RuntimeMultiplayerGameplayCommandType::Scatter;
            } else if constexpr (std::is_same_v<Body,
                    RuntimeMultiplayerGameplaySetPrimaryFactoryCommand>) {
                return RuntimeMultiplayerGameplayCommandType::SetPrimaryFactory;
            } else if constexpr (std::is_same_v<Body,
                    RuntimeMultiplayerGameplaySetRallyPointCommand>) {
                return RuntimeMultiplayerGameplayCommandType::SetRallyPoint;
            } else if constexpr (std::is_same_v<Body,
                    RuntimeMultiplayerGameplayPlayerCommunicationCommand>) {
                return RuntimeMultiplayerGameplayCommandType::PlayerCommunication;
            } else if constexpr (std::is_same_v<Body, RuntimeMultiplayerGameplayWaypointCommand>) {
                return RuntimeMultiplayerGameplayCommandType::WaypointPlanning;
            } else {
                static_assert(std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplaySetProductionHoldCommand>);
                return RuntimeMultiplayerGameplayCommandType::
                    SetProductionHold;
            }
        },
        command.body
    );
}

bool validateRuntimeMultiplayerGameplayCommand(
    const RuntimeMultiplayerGameplayCommand& command,
    std::string& errorMessage
) {
    std::string validationError;
    const bool valid = std::visit(
        [&validationError](const auto& body) {
            using Body = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayTargetOrderCommand>) {
                if (!validTargetOrder(body.order)) {
                    return fail(
                        validationError,
                        "gameplay command target order is unknown"
                    );
                }
                if (!validateObjectIds(body.objectIds, validationError)
                    || !validateTarget(body.target, validationError)) {
                    return false;
                }
                const bool requiresCell = body.order
                        == RuntimeMultiplayerGameplayTargetOrderKind::Move
                    || body.order == RuntimeMultiplayerGameplayTargetOrderKind::AttackMove
                    || body.order == RuntimeMultiplayerGameplayTargetOrderKind::GuardDestination
                    || body.order
                        == RuntimeMultiplayerGameplayTargetOrderKind::
                            ForceFireCell;
                if (requiresCell
                    && !std::holds_alternative<
                        RuntimeMultiplayerGameplayCellTarget>(body.target)) {
                    return fail(
                        validationError,
                        "gameplay movement/force-fire command requires a cell target"
                    );
                }
                if ((body.order
                        == RuntimeMultiplayerGameplayTargetOrderKind::Attack
                    || body.order == RuntimeMultiplayerGameplayTargetOrderKind::Follow)
                    && !std::holds_alternative<
                        RuntimeMultiplayerGameplayObjectTarget>(body.target)) {
                    return fail(
                        validationError,
                        "gameplay attack/follow command requires an object target"
                    );
                }
                return true;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayStopCommand>
                || std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayGuardCommand>
                || std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayScatterCommand>) {
                return validateObjectIds(body.objectIds, validationError);
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplaySecondaryActionCommand>) {
                if (!validSecondaryAction(body.action)) {
                    return fail(
                        validationError,
                        "gameplay command secondary action is unknown"
                    );
                }
                return validateObjectIds(body.objectIds, validationError);
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayQueueProductionCommand>) {
                if (!validProductionInsertMode(body.insertMode)) {
                    return fail(
                        validationError,
                        "gameplay command production insert mode is unknown"
                    );
                }
                return validateIdentifier(
                    body.typeId,
                    "production type id",
                    validationError
                );
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayCancelProductionCommand>) {
                if (!validProductionLane(body.lane)) {
                    return fail(
                        validationError,
                        "gameplay command production lane is unknown"
                    );
                }
                if (!validProductionSerial(body.productionSerial)) {
                    return fail(
                        validationError,
                        "gameplay command production serial is invalid"
                    );
                }
                return true;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplaySetProductionHoldCommand>) {
                if (!validProductionLane(body.lane)) {
                    return fail(
                        validationError,
                        "gameplay command production lane is unknown"
                    );
                }
                if (!validProductionSerial(body.activeProductionSerial)) {
                    return fail(
                        validationError,
                        "gameplay command active production serial is invalid"
                    );
                }
                return true;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayPlaceBuildingCommand>) {
                if (!validProductionLane(body.lane)) {
                    return fail(
                        validationError,
                        "gameplay command production lane is unknown"
                    );
                }
                if (!validProductionSerial(body.productionSerial)) {
                    return fail(
                        validationError,
                        "gameplay command production serial is invalid"
                    );
                }
                return validateIdentifier(
                        body.buildingTypeId,
                        "building type id",
                        validationError
                    )
                    && validateCell(body.cell, validationError);
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplaySellCommand>) {
                if (!validObjectId(body.objectId)) {
                    return fail(
                        validationError,
                        "gameplay command sell object id is invalid"
                    );
                }
                return true;
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayRepairCommand>) {
                if (!validObjectId(body.objectId)) {
                    return fail(
                        validationError,
                        "gameplay command repair object id is invalid"
                    );
                }
                return true;
            } else if constexpr (std::is_same_v<Body,
                    RuntimeMultiplayerGameplaySetPrimaryFactoryCommand>) {
                return validObjectId(body.objectId)
                    || fail(validationError, "primary factory object id is invalid");
            } else if constexpr (std::is_same_v<Body,
                    RuntimeMultiplayerGameplaySetRallyPointCommand>) {
                return (validObjectId(body.objectId)
                    || fail(validationError, "rally factory object id is invalid"))
                    && validateCell(body.cell, validationError);
            } else if constexpr (std::is_same_v<Body,
                    RuntimeMultiplayerGameplayPlayerCommunicationCommand>) {
                const bool emptyCell = body.cell == RuntimeMultiplayerGameplayCell{};
                const bool hasText = validIdentifier(body.text);
                bool valid = false;
                switch (body.kind) {
                case RuntimePlayerCommunicationKind::ChatAll:
                case RuntimePlayerCommunicationKind::ChatAllies:
                    // The byte is a legacy broadcast (0) or a recipient-slot
                    // bitmap. Sender/slot membership is checked by the runtime.
                    valid = validPlayerChatText(body.text) && emptyCell; break;
                case RuntimePlayerCommunicationKind::Alliance:
                    valid = hasText && emptyCell && body.value <= 1; break;
                case RuntimePlayerCommunicationKind::PlaceBeacon:
                case RuntimePlayerCommunicationKind::DeleteBeacon:
                    valid = body.text.empty() && body.value == 0
                        && validateCell(body.cell, validationError); break;
                case RuntimePlayerCommunicationKind::BeaconText:
                    valid = (body.text.empty() || validPlayerChatText(body.text)) && body.value == 0
                        && validateCell(body.cell, validationError); break;
                case RuntimePlayerCommunicationKind::Cheer:
                    valid = body.text.empty() && emptyCell && body.value == 0; break;
                case RuntimePlayerCommunicationKind::Taunt:
                    valid = body.text.empty() && emptyCell && body.value >= 1 && body.value <= 8; break;
                default: break;
                }
                return valid || fail(validationError, "invalid player communication payload");
            } else if constexpr (std::is_same_v<Body, RuntimeMultiplayerGameplayWaypointCommand>) {
                const bool session = body.operation == RuntimePlayerWaypointOperation::Begin
                    || body.operation == RuntimePlayerWaypointOperation::Commit;
                const bool point = body.operation == RuntimePlayerWaypointOperation::CloseLoop
                    || body.operation == RuntimePlayerWaypointOperation::DeletePoint;
                return ((session && body.waypointId == 0) || (point && body.waypointId > 0))
                    || fail(validationError, "invalid waypoint planning payload");
            } else {
                static_assert(std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayFireSuperWeaponCommand>);
                if (!validateIdentifier(
                        body.superWeaponId,
                        "superweapon id",
                        validationError
                    )) {
                    return false;
                }
                if (body.targets.size()
                    > runtimeMultiplayerGameplayMaximumSuperWeaponTargets) {
                    return fail(
                        validationError,
                        "gameplay command has too many superweapon targets"
                    );
                }
                for (const auto& target : body.targets) {
                    if (!validateSuperWeaponTarget(target, validationError)) {
                        return false;
                    }
                }
                return true;
            }
        },
        command.body
    );
    if (!valid) {
        errorMessage = std::move(validationError);
        return false;
    }
    errorMessage.clear();
    return true;
}

bool canonicalizeRuntimeMultiplayerGameplayCommand(
    RuntimeMultiplayerGameplayCommand& command,
    std::string& errorMessage
) {
    RuntimeMultiplayerGameplayCommand canonical = command;
    std::string canonicalError;
    const bool canonicalized = std::visit(
        [&canonicalError](auto& body) {
            using Body = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayTargetOrderCommand>
                || std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayStopCommand>
                || std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayGuardCommand>
                || std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayScatterCommand>
                || std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplaySecondaryActionCommand>) {
                return canonicalizeObjectIds(
                    body.objectIds,
                    canonicalError
                );
            } else {
                return true;
            }
        },
        canonical.body
    );
    if (!canonicalized
        || !validateRuntimeMultiplayerGameplayCommand(
            canonical,
            canonicalError
        )) {
        errorMessage = std::move(canonicalError);
        return false;
    }
    command = std::move(canonical);
    errorMessage.clear();
    return true;
}

bool encodeRuntimeMultiplayerGameplayCommand(
    const RuntimeMultiplayerGameplayCommand& command,
    std::vector<std::uint8_t>& payload,
    std::string& errorMessage
) {
    RuntimeMultiplayerGameplayCommand canonical = command;
    std::string encodeError;
    if (!canonicalizeRuntimeMultiplayerGameplayCommand(
            canonical,
            encodeError
        )) {
        errorMessage = std::move(encodeError);
        return false;
    }

    ByteWriter writer;
    writer.writeU32(runtimeMultiplayerGameplayCommandMagic);
    writer.writeU16(runtimeMultiplayerGameplayCommandVersion);
    writer.writeU16(static_cast<std::uint16_t>(
        runtimeMultiplayerGameplayCommandType(canonical)
    ));
    std::visit(
        [&writer](const auto& body) {
            using Body = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayTargetOrderCommand>) {
                writer.writeU8(static_cast<std::uint8_t>(body.order));
                writeObjectIds(writer, body.objectIds);
                writeTarget(writer, body.target);
                writer.writeU8(body.append ? 1U : 0U);
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayStopCommand>
                || std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayGuardCommand>
                || std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayScatterCommand>) {
                writeObjectIds(writer, body.objectIds);
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplaySecondaryActionCommand>) {
                writer.writeU8(static_cast<std::uint8_t>(body.action));
                writeObjectIds(writer, body.objectIds);
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayQueueProductionCommand>) {
                writer.writeU8(static_cast<std::uint8_t>(body.insertMode));
                writer.writeString(body.typeId);
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayCancelProductionCommand>) {
                writer.writeU8(static_cast<std::uint8_t>(body.lane));
                writer.writeU64(body.productionSerial);
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplaySetProductionHoldCommand>) {
                writer.writeU8(static_cast<std::uint8_t>(body.lane));
                writer.writeU64(body.activeProductionSerial);
                writer.writeU8(body.onHold ? 1U : 0U);
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayPlaceBuildingCommand>) {
                writer.writeU8(static_cast<std::uint8_t>(body.lane));
                writer.writeU64(body.productionSerial);
                writer.writeString(body.buildingTypeId);
                writeCell(writer, body.cell);
            } else if constexpr (std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplaySellCommand>
                || std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayRepairCommand>) {
                writer.writeU32(static_cast<std::uint32_t>(body.objectId));
            } else if constexpr (std::is_same_v<Body,
                    RuntimeMultiplayerGameplaySetPrimaryFactoryCommand>) {
                writer.writeU32(static_cast<std::uint32_t>(body.objectId));
            } else if constexpr (std::is_same_v<Body,
                    RuntimeMultiplayerGameplaySetRallyPointCommand>) {
                writer.writeU32(static_cast<std::uint32_t>(body.objectId));
                writeCell(writer, body.cell);
            } else if constexpr (std::is_same_v<Body,
                    RuntimeMultiplayerGameplayPlayerCommunicationCommand>) {
                writer.writeU8(static_cast<std::uint8_t>(body.kind));
                writer.writeString(body.text);
                writeCell(writer, body.cell);
                writer.writeU8(body.value);
            } else if constexpr (std::is_same_v<Body, RuntimeMultiplayerGameplayWaypointCommand>) {
                writer.writeU8(static_cast<std::uint8_t>(body.operation));
                writer.writeU64(body.waypointId);
            } else {
                static_assert(std::is_same_v<
                    Body,
                    RuntimeMultiplayerGameplayFireSuperWeaponCommand>);
                writer.writeString(body.superWeaponId);
                writer.writeU8(static_cast<std::uint8_t>(body.targets.size()));
                for (const auto& target : body.targets) {
                    writeSuperWeaponTarget(writer, target);
                }
            }
        },
        canonical.body
    );

    if (writer.bytes().size()
        > runtimeMultiplayerGameplayMaximumPayloadBytes) {
        errorMessage = "gameplay command payload exceeds the size limit";
        return false;
    }
    payload = writer.bytes();
    errorMessage.clear();
    return true;
}

bool decodeRuntimeMultiplayerGameplayCommand(
    std::span<const std::uint8_t> payload,
    RuntimeMultiplayerGameplayCommand& command,
    std::string& errorMessage
) {
    if (payload.size() > runtimeMultiplayerGameplayMaximumPayloadBytes) {
        errorMessage = "gameplay command payload exceeds the size limit";
        return false;
    }

    ByteReader reader(payload);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t encodedType = 0;
    if (!reader.readU32(magic)
        || !reader.readU16(version)
        || !reader.readU16(encodedType)) {
        errorMessage = reader.error();
        return false;
    }
    if (magic != runtimeMultiplayerGameplayCommandMagic) {
        errorMessage = "gameplay command magic is invalid";
        return false;
    }
    if (version != runtimeMultiplayerGameplayCommandVersion) {
        errorMessage = "gameplay command version " + std::to_string(version)
            + " is unsupported; expected "
            + std::to_string(runtimeMultiplayerGameplayCommandVersion);
        return false;
    }

    RuntimeMultiplayerGameplayCommandType type =
        RuntimeMultiplayerGameplayCommandType::TargetOrder;
    if (!decodeCommandType(encodedType, type, errorMessage)) return false;

    RuntimeMultiplayerGameplayCommand parsed;
    switch (type) {
    case RuntimeMultiplayerGameplayCommandType::TargetOrder: {
        RuntimeMultiplayerGameplayTargetOrderCommand body;
        std::uint8_t order = 0;
        std::uint8_t append = 0;
        if (!reader.readU8(order)
            || !readObjectIds(reader, body.objectIds)
            || !readTarget(reader, body.target)
            || !reader.readU8(append)) {
            errorMessage = reader.error();
            return false;
        }
        body.order =
            static_cast<RuntimeMultiplayerGameplayTargetOrderKind>(order);
        if (append > 1U) {
            errorMessage = "gameplay command append flag is invalid";
            return false;
        }
        body.append = append != 0U;
        parsed.body = std::move(body);
        break;
    }
    case RuntimeMultiplayerGameplayCommandType::Stop: {
        RuntimeMultiplayerGameplayStopCommand body;
        if (!readObjectIds(reader, body.objectIds)) {
            errorMessage = reader.error();
            return false;
        }
        parsed.body = std::move(body);
        break;
    }
    case RuntimeMultiplayerGameplayCommandType::Guard: {
        RuntimeMultiplayerGameplayGuardCommand body;
        if (!readObjectIds(reader, body.objectIds)) {
            errorMessage = reader.error();
            return false;
        }
        parsed.body = std::move(body);
        break;
    }
    case RuntimeMultiplayerGameplayCommandType::Scatter: {
        RuntimeMultiplayerGameplayScatterCommand body;
        if (!readObjectIds(reader, body.objectIds)) {
            errorMessage = reader.error();
            return false;
        }
        parsed.body = std::move(body);
        break;
    }
    case RuntimeMultiplayerGameplayCommandType::SecondaryAction: {
        RuntimeMultiplayerGameplaySecondaryActionCommand body;
        std::uint8_t action = 0;
        if (!reader.readU8(action)
            || !readObjectIds(reader, body.objectIds)) {
            errorMessage = reader.error();
            return false;
        }
        body.action = static_cast<
            RuntimeMultiplayerGameplaySecondaryActionKind>(action);
        parsed.body = std::move(body);
        break;
    }
    case RuntimeMultiplayerGameplayCommandType::QueueProduction: {
        RuntimeMultiplayerGameplayQueueProductionCommand body;
        std::uint8_t insertMode = 0;
        if (!reader.readU8(insertMode)
            || !reader.readString(
                runtimeMultiplayerGameplayMaximumIdentifierBytes,
                body.typeId,
                "production type id"
            )) {
            errorMessage = reader.error();
            return false;
        }
        body.insertMode = static_cast<
            RuntimeMultiplayerGameplayProductionInsertMode>(insertMode);
        parsed.body = std::move(body);
        break;
    }
    case RuntimeMultiplayerGameplayCommandType::CancelProduction: {
        RuntimeMultiplayerGameplayCancelProductionCommand body;
        std::uint8_t lane = 0;
        if (!reader.readU8(lane)
            || !reader.readU64(body.productionSerial)) {
            errorMessage = reader.error();
            return false;
        }
        body.lane = static_cast<
            RuntimeMultiplayerGameplayProductionLane>(lane);
        parsed.body = body;
        break;
    }
    case RuntimeMultiplayerGameplayCommandType::SetProductionHold: {
        RuntimeMultiplayerGameplaySetProductionHoldCommand body;
        std::uint8_t lane = 0;
        std::uint8_t onHold = 0;
        if (!reader.readU8(lane)
            || !reader.readU64(body.activeProductionSerial)
            || !reader.readU8(onHold)) {
            errorMessage = reader.error();
            return false;
        }
        if (onHold > 1U) {
            errorMessage =
                "gameplay command production hold flag is invalid";
            return false;
        }
        body.lane = static_cast<
            RuntimeMultiplayerGameplayProductionLane>(lane);
        body.onHold = onHold != 0U;
        parsed.body = body;
        break;
    }
    case RuntimeMultiplayerGameplayCommandType::PlaceBuilding: {
        RuntimeMultiplayerGameplayPlaceBuildingCommand body;
        std::uint8_t lane = 0;
        if (!reader.readU8(lane)
            || !reader.readU64(body.productionSerial)
            || !reader.readString(
                runtimeMultiplayerGameplayMaximumIdentifierBytes,
                body.buildingTypeId,
                "building type id"
            )
            || !readCell(reader, body.cell)) {
            errorMessage = reader.error();
            return false;
        }
        body.lane = static_cast<
            RuntimeMultiplayerGameplayProductionLane>(lane);
        parsed.body = std::move(body);
        break;
    }
    case RuntimeMultiplayerGameplayCommandType::Sell: {
        std::uint32_t objectId = 0;
        if (!reader.readU32(objectId)
            || objectId > static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max())) {
            errorMessage = reader.error().empty()
                ? "gameplay command sell object id is out of range"
                : reader.error();
            return false;
        }
        parsed.body = RuntimeMultiplayerGameplaySellCommand{
            static_cast<std::int32_t>(objectId)
        };
        break;
    }
    case RuntimeMultiplayerGameplayCommandType::Repair: {
        std::uint32_t objectId = 0;
        if (!reader.readU32(objectId)
            || objectId > static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max())) {
            errorMessage = reader.error().empty()
                ? "gameplay command repair object id is out of range"
                : reader.error();
            return false;
        }
        parsed.body = RuntimeMultiplayerGameplayRepairCommand{
            static_cast<std::int32_t>(objectId)
        };
        break;
    }
    case RuntimeMultiplayerGameplayCommandType::SetPrimaryFactory:
    case RuntimeMultiplayerGameplayCommandType::SetRallyPoint: {
        std::uint32_t objectId = 0;
        if (!reader.readU32(objectId) || objectId == 0U
            || objectId > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
            errorMessage = "gameplay factory object id is invalid";
            return false;
        }
        if (type == RuntimeMultiplayerGameplayCommandType::SetPrimaryFactory) {
            parsed.body = RuntimeMultiplayerGameplaySetPrimaryFactoryCommand{
                static_cast<std::int32_t>(objectId)};
        } else {
            RuntimeMultiplayerGameplaySetRallyPointCommand body;
            body.objectId = static_cast<std::int32_t>(objectId);
            if (!readCell(reader, body.cell)) {
                errorMessage = reader.error();
                return false;
            }
            parsed.body = body;
        }
        break;
    }
    case RuntimeMultiplayerGameplayCommandType::WaypointPlanning: {
        RuntimeMultiplayerGameplayWaypointCommand body;
        std::uint8_t operation = 0;
        if (!reader.readU8(operation) || !reader.readU64(body.waypointId)) {
            errorMessage = reader.error(); return false;
        }
        body.operation = static_cast<RuntimePlayerWaypointOperation>(operation);
        parsed.body = body;
        break;
    }
    case RuntimeMultiplayerGameplayCommandType::PlayerCommunication: {
        RuntimeMultiplayerGameplayPlayerCommunicationCommand body;
        std::uint8_t kind = 0;
        if (!reader.readU8(kind)
            || !reader.readString(runtimeMultiplayerGameplayMaximumIdentifierBytes, body.text, "chat text")
            || !readCell(reader, body.cell) || !reader.readU8(body.value)) {
            errorMessage = reader.error(); return false;
        }
        body.kind = static_cast<RuntimePlayerCommunicationKind>(kind);
        parsed.body = std::move(body);
        break;
    }
    case RuntimeMultiplayerGameplayCommandType::FireSuperWeapon: {
        RuntimeMultiplayerGameplayFireSuperWeaponCommand body;
        std::uint8_t targetCount = 0;
        if (!reader.readString(
                runtimeMultiplayerGameplayMaximumIdentifierBytes,
                body.superWeaponId,
                "superweapon id"
            )
            || !reader.readU8(targetCount)) {
            errorMessage = reader.error();
            return false;
        }
        if (static_cast<std::size_t>(targetCount)
            > runtimeMultiplayerGameplayMaximumSuperWeaponTargets) {
            errorMessage =
                "gameplay command has too many superweapon targets";
            return false;
        }
        body.targets.reserve(static_cast<std::size_t>(targetCount));
        for (std::uint8_t index = 0; index < targetCount; ++index) {
            RuntimeMultiplayerGameplaySuperWeaponTarget target;
            if (!readSuperWeaponTarget(reader, target)) {
                errorMessage = reader.error();
                return false;
            }
            body.targets.push_back(std::move(target));
        }
        parsed.body = std::move(body);
        break;
    }
    }

    if (!reader.finished()) {
        errorMessage = "gameplay command payload has trailing bytes";
        return false;
    }
    std::string validationError;
    if (!validateRuntimeMultiplayerGameplayCommand(
            parsed,
            validationError
        )) {
        errorMessage = std::move(validationError);
        return false;
    }
    command = std::move(parsed);
    errorMessage.clear();
    return true;
}
