#include "RuntimeInternetMatchmakingProtocol.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace {

constexpr std::uint32_t matchmakingWireMagic = 0x434d4d51U; // CMMQ
constexpr std::uint8_t matchmakingMinimumPlayers = 2U;
constexpr std::uint8_t matchmakingMaximumPlayers = 8U;
constexpr std::int32_t matchmakingMinimumRating = 0;
constexpr std::int32_t matchmakingMaximumRating = 10000;

class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void u16(std::uint16_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value >> 8U));
        bytes_.push_back(static_cast<std::uint8_t>(value));
    }
    void u32(std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void u64(std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void boolean(bool value) { u8(value ? 1U : 0U); }
    void string(std::string_view value) {
        u16(static_cast<std::uint16_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }
    void blob(std::span<const std::uint8_t> value) {
        u16(static_cast<std::uint16_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    bool u8(std::uint8_t& value) {
        if (remaining() < 1U) return false;
        value = bytes_[offset_++];
        return true;
    }
    bool u16(std::uint16_t& value) {
        if (remaining() < 2U) return false;
        value = static_cast<std::uint16_t>(bytes_[offset_]) << 8U;
        value = static_cast<std::uint16_t>(
            value | bytes_[offset_ + 1U]
        );
        offset_ += 2U;
        return true;
    }
    bool u32(std::uint32_t& value) {
        if (remaining() < 4U) return false;
        value = 0U;
        for (int index = 0; index < 4; ++index) {
            value = (value << 8U) | bytes_[offset_++];
        }
        return true;
    }
    bool u64(std::uint64_t& value) {
        if (remaining() < 8U) return false;
        value = 0U;
        for (int index = 0; index < 8; ++index) {
            value = (value << 8U) | bytes_[offset_++];
        }
        return true;
    }
    bool boolean(bool& value) {
        std::uint8_t raw = 0U;
        if (!u8(raw) || raw > 1U) return false;
        value = raw != 0U;
        return true;
    }
    bool string(std::string& value, std::size_t maximum) {
        std::uint16_t size = 0U;
        if (!u16(size) || size > maximum || remaining() < size) return false;
        value.assign(
            reinterpret_cast<const char*>(bytes_.data() + offset_),
            size
        );
        offset_ += size;
        return true;
    }
    bool blob(std::vector<std::uint8_t>& value, std::size_t maximum) {
        std::uint16_t size = 0U;
        if (!u16(size) || size > maximum || remaining() < size) return false;
        value.assign(
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + size)
        );
        offset_ += size;
        return true;
    }
    bool finished() const { return offset_ == bytes_.size(); }

private:
    std::size_t remaining() const { return bytes_.size() - offset_; }
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0U;
};

bool validIdentifier(std::string_view value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) return false;
    return std::all_of(value.begin(), value.end(), [](char raw) {
        const unsigned char valueCharacter = static_cast<unsigned char>(raw);
        return (valueCharacter >= static_cast<unsigned char>('a')
                && valueCharacter <= static_cast<unsigned char>('z'))
            || (valueCharacter >= static_cast<unsigned char>('A')
                && valueCharacter <= static_cast<unsigned char>('Z'))
            || (valueCharacter >= static_cast<unsigned char>('0')
                && valueCharacter <= static_cast<unsigned char>('9'))
            || valueCharacter == static_cast<unsigned char>('.')
            || valueCharacter == static_cast<unsigned char>('_')
            || valueCharacter == static_cast<unsigned char>('-');
    });
}

bool validPrintable(std::string_view value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) return false;
    return std::all_of(value.begin(), value.end(), [](char raw) {
        const unsigned char valueCharacter = static_cast<unsigned char>(raw);
        return valueCharacter >= 0x21U && valueCharacter <= 0x7eU;
    });
}

bool validDisplayText(std::string_view value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) return false;
    return std::all_of(value.begin(), value.end(), [](char raw) {
        const unsigned char valueCharacter = static_cast<unsigned char>(raw);
        return valueCharacter >= 0x20U && valueCharacter <= 0x7eU;
    });
}

bool validOperation(RuntimeInternetMatchmakingOperation operation) {
    return operation >= RuntimeInternetMatchmakingOperation::Enqueue
        && operation <= RuntimeInternetMatchmakingOperation::ActivateMatch;
}

std::uint64_t saturatingAdd(
    std::uint64_t value,
    std::uint64_t increment
) {
    return increment > std::numeric_limits<std::uint64_t>::max() - value
        ? std::numeric_limits<std::uint64_t>::max()
        : value + increment;
}

bool validRequest(const RuntimeInternetMatchmakingRequest& request) {
    return request.requestId != 0U
        && validIdentifier(
            request.queueId,
            runtimeInternetMatchmakingMaximumQueueIdBytes
        )
        && validIdentifier(
            request.regionId,
            runtimeInternetMatchmakingMaximumRegionIdBytes
        )
        && validPrintable(
            request.contentFingerprint,
            runtimeInternetMatchmakingMaximumContentFingerprintBytes
        )
        && validDisplayText(
            request.displayName,
            runtimeInternetMatchmakingMaximumDisplayNameBytes
        )
        && request.playerCount >= matchmakingMinimumPlayers
        && request.playerCount <= matchmakingMaximumPlayers
        && request.skillRating >= matchmakingMinimumRating
        && request.skillRating <= matchmakingMaximumRating;
}

bool uniquePeers(const std::vector<std::string>& peers) {
    for (std::size_t index = 0U; index < peers.size(); ++index) {
        if (!validIdentifier(
                peers[index],
                runtimeInternetMatchmakingMaximumPeerIdBytes
            )) {
            return false;
        }
        if (std::find(
                peers.begin() + static_cast<std::ptrdiff_t>(index + 1U),
                peers.end(),
                peers[index]
            ) != peers.end()) {
            return false;
        }
    }
    return true;
}

bool validAssignment(
    const RuntimeInternetMatchmakingAssignment& assignment
) {
    return assignment.requestId != 0U
        && validIdentifier(
            assignment.matchId,
            runtimeInternetMatchmakingMaximumQueueIdBytes
        )
        && validIdentifier(
            assignment.roomId,
            runtimeInternetMatchmakingMaximumQueueIdBytes
        )
        && validIdentifier(
            assignment.peerId,
            runtimeInternetMatchmakingMaximumPeerIdBytes
        )
        && validIdentifier(
            assignment.hostPeerId,
            runtimeInternetMatchmakingMaximumPeerIdBytes
        )
        && validDisplayText(
            assignment.hostDisplayName,
            runtimeInternetMatchmakingMaximumDisplayNameBytes
        )
        && assignment.peerIds.size() >= matchmakingMinimumPlayers
        && assignment.peerIds.size() <= matchmakingMaximumPlayers
        && uniquePeers(assignment.peerIds)
        && std::find(
            assignment.peerIds.begin(),
            assignment.peerIds.end(),
            assignment.peerId
        ) != assignment.peerIds.end()
        && std::find(
            assignment.peerIds.begin(),
            assignment.peerIds.end(),
            assignment.hostPeerId
        ) != assignment.peerIds.end()
        && assignment.admissionProof.size() >= 16U
        && assignment.admissionProof.size()
            <= runtimeInternetMatchmakingMaximumProofBytes
        && assignment.expiresAtMilliseconds != 0U;
}

bool requestType(RuntimeInternetMatchmakingMessageType type) {
    return type == RuntimeInternetMatchmakingMessageType::Enqueue
        || type == RuntimeInternetMatchmakingMessageType::Heartbeat
        || type == RuntimeInternetMatchmakingMessageType::Cancel
        || type == RuntimeInternetMatchmakingMessageType::CloseMatch
        || type == RuntimeInternetMatchmakingMessageType::ActivateMatch;
}

bool validEnvelope(const RuntimeInternetMatchmakingMessage& message) {
    if (message.transactionId == 0U
        || !validIdentifier(
            message.serviceId,
            runtimeInternetMatchmakingMaximumPeerIdBytes
        )
        || !validIdentifier(
            message.senderPeerId,
            runtimeInternetMatchmakingMaximumPeerIdBytes
        )) {
        return false;
    }
    if (requestType(message.type)) return message.targetPeerId.empty();
    return validIdentifier(
        message.targetPeerId,
        runtimeInternetMatchmakingMaximumPeerIdBytes
    );
}

void writeRequest(
    Writer& writer,
    const RuntimeInternetMatchmakingRequest& request
) {
    writer.u64(request.requestId);
    writer.string(request.queueId);
    writer.string(request.regionId);
    writer.string(request.contentFingerprint);
    writer.string(request.displayName);
    writer.u8(request.playerCount);
    writer.u32(static_cast<std::uint32_t>(request.skillRating));
}

bool readRequest(
    Reader& reader,
    RuntimeInternetMatchmakingRequest& request
) {
    std::uint32_t rating = 0U;
    if (!reader.u64(request.requestId)
        || !reader.string(
            request.queueId,
            runtimeInternetMatchmakingMaximumQueueIdBytes
        )
        || !reader.string(
            request.regionId,
            runtimeInternetMatchmakingMaximumRegionIdBytes
        )
        || !reader.string(
            request.contentFingerprint,
            runtimeInternetMatchmakingMaximumContentFingerprintBytes
        )
        || !reader.string(
            request.displayName,
            runtimeInternetMatchmakingMaximumDisplayNameBytes
        )
        || !reader.u8(request.playerCount)
        || !reader.u32(rating)
        || rating > static_cast<std::uint32_t>(
            std::numeric_limits<std::int32_t>::max()
        )) {
        return false;
    }
    request.skillRating = static_cast<std::int32_t>(rating);
    return validRequest(request);
}

void writeAssignment(
    Writer& writer,
    const RuntimeInternetMatchmakingAssignment& assignment
) {
    writer.u64(assignment.requestId);
    writer.string(assignment.matchId);
    writer.string(assignment.roomId);
    writer.string(assignment.peerId);
    writer.string(assignment.hostPeerId);
    writer.string(assignment.hostDisplayName);
    writer.u8(static_cast<std::uint8_t>(assignment.peerIds.size()));
    for (const std::string& peerId : assignment.peerIds) {
        writer.string(peerId);
    }
    writer.blob(assignment.admissionProof);
    writer.u64(assignment.expiresAtMilliseconds);
}

bool readAssignment(
    Reader& reader,
    RuntimeInternetMatchmakingAssignment& assignment
) {
    std::uint8_t peerCount = 0U;
    if (!reader.u64(assignment.requestId)
        || !reader.string(
            assignment.matchId,
            runtimeInternetMatchmakingMaximumQueueIdBytes
        )
        || !reader.string(
            assignment.roomId,
            runtimeInternetMatchmakingMaximumQueueIdBytes
        )
        || !reader.string(
            assignment.peerId,
            runtimeInternetMatchmakingMaximumPeerIdBytes
        )
        || !reader.string(
            assignment.hostPeerId,
            runtimeInternetMatchmakingMaximumPeerIdBytes
        )
        || !reader.string(
            assignment.hostDisplayName,
            runtimeInternetMatchmakingMaximumDisplayNameBytes
        )
        || !reader.u8(peerCount)
        || peerCount < matchmakingMinimumPlayers
        || peerCount > matchmakingMaximumPlayers) {
        return false;
    }
    assignment.peerIds.resize(peerCount);
    for (std::string& peerId : assignment.peerIds) {
        if (!reader.string(
                peerId,
                runtimeInternetMatchmakingMaximumPeerIdBytes
            )) {
            return false;
        }
    }
    return reader.blob(
            assignment.admissionProof,
            runtimeInternetMatchmakingMaximumProofBytes
        )
        && reader.u64(assignment.expiresAtMilliseconds)
        && validAssignment(assignment);
}

bool encodeChecked(
    const RuntimeInternetMatchmakingMessage& message,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    if (!validEnvelope(message)) {
        errorMessage = "invalid matchmaking message envelope";
        return false;
    }
    Writer writer;
    writer.u32(matchmakingWireMagic);
    writer.u16(runtimeInternetMatchmakingWireVersion);
    writer.u8(static_cast<std::uint8_t>(message.type));
    writer.u64(message.transactionId);
    writer.string(message.serviceId);
    writer.string(message.senderPeerId);
    writer.string(message.targetPeerId);

    switch (message.type) {
    case RuntimeInternetMatchmakingMessageType::Enqueue: {
        const auto* body = std::get_if<RuntimeInternetMatchmakingRequest>(
            &message.body
        );
        if (!body || !validRequest(*body)
            || body->requestId != message.transactionId) {
            errorMessage = "invalid matchmaking enqueue message";
            return false;
        }
        writeRequest(writer, *body);
        break;
    }
    case RuntimeInternetMatchmakingMessageType::Heartbeat:
    case RuntimeInternetMatchmakingMessageType::Cancel: {
        const auto* body =
            std::get_if<RuntimeInternetMatchmakingRequestIdBody>(
                &message.body
            );
        if (!body || body->requestId == 0U
            || body->requestId != message.transactionId) {
            errorMessage = "invalid matchmaking request-id message";
            return false;
        }
        writer.u64(body->requestId);
        break;
    }
    case RuntimeInternetMatchmakingMessageType::CloseMatch:
    case RuntimeInternetMatchmakingMessageType::ActivateMatch: {
        const auto* body = std::get_if<RuntimeInternetMatchmakingCloseBody>(
            &message.body
        );
        if (!body || !validIdentifier(
                body->matchId,
                runtimeInternetMatchmakingMaximumQueueIdBytes
            )) {
            errorMessage = "invalid matchmaking close message";
            return false;
        }
        writer.string(body->matchId);
        break;
    }
    case RuntimeInternetMatchmakingMessageType::OperationAck: {
        const auto* body = std::get_if<RuntimeInternetMatchmakingAckBody>(
            &message.body
        );
        if (!body || !validOperation(body->operation)
            || body->requestId == 0U
            || body->requestId != message.transactionId
            || (body->operation == RuntimeInternetMatchmakingOperation::Enqueue
                && body->leaseMilliseconds == 0U)
            || (body->operation != RuntimeInternetMatchmakingOperation::Enqueue
                && body->leaseMilliseconds != 0U)) {
            errorMessage = "invalid matchmaking acknowledgement";
            return false;
        }
        writer.u8(static_cast<std::uint8_t>(body->operation));
        writer.u64(body->requestId);
        writer.boolean(body->changed);
        writer.boolean(body->duplicate);
        writer.u32(body->leaseMilliseconds);
        break;
    }
    case RuntimeInternetMatchmakingMessageType::Assignment: {
        const auto* body =
            std::get_if<RuntimeInternetMatchmakingAssignment>(&message.body);
        if (!body || !validAssignment(*body)
            || body->requestId != message.transactionId
            || body->peerId != message.targetPeerId) {
            errorMessage = "invalid matchmaking assignment";
            return false;
        }
        writeAssignment(writer, *body);
        break;
    }
    case RuntimeInternetMatchmakingMessageType::Error: {
        const auto* body = std::get_if<RuntimeInternetMatchmakingErrorBody>(
            &message.body
        );
        if (!body || !validOperation(body->operation)
            || body->requestId == 0U
            || body->requestId != message.transactionId
            || !validDisplayText(
                body->reason,
                runtimeInternetMatchmakingMaximumErrorBytes
            )) {
            errorMessage = "invalid matchmaking error";
            return false;
        }
        writer.u8(static_cast<std::uint8_t>(body->operation));
        writer.u64(body->requestId);
        writer.string(body->reason);
        break;
    }
    default:
        errorMessage = "unknown matchmaking message type";
        return false;
    }

    std::vector<std::uint8_t> payload = writer.take();
    if (payload.size() > runtimeInternetMatchmakingMaximumPacketBytes) {
        errorMessage = "matchmaking message exceeds the size limit";
        return false;
    }
    RuntimeNetworkPacket candidate;
    candidate.type = runtimeInternetMatchmakingPacketType;
    candidate.payload = std::move(payload);
    packet = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool decodeBody(
    Reader& reader,
    RuntimeInternetMatchmakingMessage& message
) {
    switch (message.type) {
    case RuntimeInternetMatchmakingMessageType::Enqueue: {
        RuntimeInternetMatchmakingRequest body;
        if (!readRequest(reader, body)) return false;
        message.body = std::move(body);
        return true;
    }
    case RuntimeInternetMatchmakingMessageType::Heartbeat:
    case RuntimeInternetMatchmakingMessageType::Cancel: {
        RuntimeInternetMatchmakingRequestIdBody body;
        if (!reader.u64(body.requestId)) return false;
        message.body = body;
        return true;
    }
    case RuntimeInternetMatchmakingMessageType::CloseMatch:
    case RuntimeInternetMatchmakingMessageType::ActivateMatch: {
        RuntimeInternetMatchmakingCloseBody body;
        if (!reader.string(
                body.matchId,
                runtimeInternetMatchmakingMaximumQueueIdBytes
            )) {
            return false;
        }
        message.body = std::move(body);
        return true;
    }
    case RuntimeInternetMatchmakingMessageType::OperationAck: {
        RuntimeInternetMatchmakingAckBody body;
        std::uint8_t operation = 0U;
        if (!reader.u8(operation)
            || !reader.u64(body.requestId)
            || !reader.boolean(body.changed)
            || !reader.boolean(body.duplicate)
            || !reader.u32(body.leaseMilliseconds)) {
            return false;
        }
        body.operation =
            static_cast<RuntimeInternetMatchmakingOperation>(operation);
        message.body = body;
        return true;
    }
    case RuntimeInternetMatchmakingMessageType::Assignment: {
        RuntimeInternetMatchmakingAssignment body;
        if (!readAssignment(reader, body)) return false;
        message.body = std::move(body);
        return true;
    }
    case RuntimeInternetMatchmakingMessageType::Error: {
        RuntimeInternetMatchmakingErrorBody body;
        std::uint8_t operation = 0U;
        if (!reader.u8(operation)
            || !reader.u64(body.requestId)
            || !reader.string(
                body.reason,
                runtimeInternetMatchmakingMaximumErrorBytes
            )) {
            return false;
        }
        body.operation =
            static_cast<RuntimeInternetMatchmakingOperation>(operation);
        message.body = std::move(body);
        return true;
    }
    default:
        return false;
    }
}

RuntimeInternetMatchmakingOperation operationFor(
    RuntimeInternetMatchmakingMessageType type
) {
    switch (type) {
    case RuntimeInternetMatchmakingMessageType::Heartbeat:
        return RuntimeInternetMatchmakingOperation::Heartbeat;
    case RuntimeInternetMatchmakingMessageType::Cancel:
        return RuntimeInternetMatchmakingOperation::Cancel;
    case RuntimeInternetMatchmakingMessageType::CloseMatch:
        return RuntimeInternetMatchmakingOperation::CloseMatch;
    case RuntimeInternetMatchmakingMessageType::ActivateMatch:
        return RuntimeInternetMatchmakingOperation::ActivateMatch;
    default:
        return RuntimeInternetMatchmakingOperation::Enqueue;
    }
}

} // namespace

bool encodeRuntimeInternetMatchmakingMessage(
    const RuntimeInternetMatchmakingMessage& message,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    return encodeChecked(message, packet, errorMessage);
}

bool decodeRuntimeInternetMatchmakingMessage(
    const RuntimeNetworkPacket& packet,
    RuntimeInternetMatchmakingMessage& message,
    std::string& errorMessage
) {
    if (packet.type != runtimeInternetMatchmakingPacketType
        || packet.payload.empty()
        || packet.payload.size()
            > runtimeInternetMatchmakingMaximumPacketBytes) {
        errorMessage = "wrong matchmaking packet type or length";
        return false;
    }
    Reader reader(packet.payload);
    std::uint32_t magic = 0U;
    std::uint16_t version = 0U;
    std::uint8_t type = 0U;
    RuntimeInternetMatchmakingMessage decoded;
    if (!reader.u32(magic) || magic != matchmakingWireMagic
        || !reader.u16(version)
        || version != runtimeInternetMatchmakingWireVersion
        || !reader.u8(type)
        || type < static_cast<std::uint8_t>(
            RuntimeInternetMatchmakingMessageType::Enqueue
        )
        || type > static_cast<std::uint8_t>(
            RuntimeInternetMatchmakingMessageType::ActivateMatch
        )
        || !reader.u64(decoded.transactionId)
        || !reader.string(
            decoded.serviceId,
            runtimeInternetMatchmakingMaximumPeerIdBytes
        )
        || !reader.string(
            decoded.senderPeerId,
            runtimeInternetMatchmakingMaximumPeerIdBytes
        )
        || !reader.string(
            decoded.targetPeerId,
            runtimeInternetMatchmakingMaximumPeerIdBytes
        )) {
        errorMessage = "invalid matchmaking message header";
        return false;
    }
    decoded.type = static_cast<RuntimeInternetMatchmakingMessageType>(type);
    if (!decodeBody(reader, decoded) || !reader.finished()) {
        errorMessage = "truncated or trailing matchmaking message bytes";
        return false;
    }
    RuntimeNetworkPacket canonical;
    std::string validationError;
    if (!encodeChecked(decoded, canonical, validationError)
        || canonical.payload != packet.payload) {
        errorMessage = validationError.empty()
            ? "non-canonical matchmaking message"
            : std::move(validationError);
        return false;
    }
    message = std::move(decoded);
    errorMessage.clear();
    return true;
}

struct RuntimeInternetMatchmakingService::Impl {
    RuntimeInternetMatchmaking authority;
    std::string serviceId;
    std::uint32_t queueLeaseMilliseconds = 0U;
    bool open = false;

    bool makeResponse(
        std::string targetPeerId,
        RuntimeInternetMatchmakingMessageType type,
        std::uint64_t transactionId,
        RuntimeInternetMatchmakingMessageBody body,
        RuntimeInternetMatchmakingServiceAction& action,
        std::string& errorMessage
    ) const {
        RuntimeInternetMatchmakingMessage message;
        message.type = type;
        message.transactionId = transactionId;
        message.serviceId = serviceId;
        message.senderPeerId = serviceId;
        message.targetPeerId = targetPeerId;
        message.body = std::move(body);
        RuntimeInternetMatchmakingServiceAction candidate;
        candidate.targetPeerId = std::move(targetPeerId);
        if (!encodeRuntimeInternetMatchmakingMessage(
                message,
                candidate.packet,
                errorMessage
            )) {
            return false;
        }
        action = std::move(candidate);
        return true;
    }

    bool appendOperationResponse(
        std::string targetPeerId,
        RuntimeInternetMatchmakingOperation operation,
        std::uint64_t transactionId,
        const RuntimeInternetMatchmakingOperationResult& result,
        std::vector<RuntimeInternetMatchmakingServiceAction>& actions,
        std::string& errorMessage
    ) const {
        RuntimeInternetMatchmakingServiceAction action;
        if (result.accepted) {
            RuntimeInternetMatchmakingAckBody body;
            body.operation = operation;
            body.requestId = transactionId;
            body.changed = result.changed;
            body.duplicate = result.duplicate;
            body.leaseMilliseconds =
                operation == RuntimeInternetMatchmakingOperation::Enqueue
                ? queueLeaseMilliseconds
                : 0U;
            if (!makeResponse(
                    std::move(targetPeerId),
                    RuntimeInternetMatchmakingMessageType::OperationAck,
                    transactionId,
                    body,
                    action,
                    errorMessage
                )) {
                return false;
            }
        } else {
            RuntimeInternetMatchmakingErrorBody body;
            body.operation = operation;
            body.requestId = transactionId;
            body.reason = result.reason.empty()
                ? "matchmaking operation was rejected"
                : result.reason.substr(
                    0U,
                    runtimeInternetMatchmakingMaximumErrorBytes
                );
            if (!makeResponse(
                    std::move(targetPeerId),
                    RuntimeInternetMatchmakingMessageType::Error,
                    transactionId,
                    std::move(body),
                    action,
                    errorMessage
                )) {
                return false;
            }
        }
        actions.push_back(std::move(action));
        return true;
    }

    bool appendAssignments(
        std::vector<RuntimeInternetMatchmakingAction> assignments,
        std::vector<RuntimeInternetMatchmakingServiceAction>& actions,
        std::string& errorMessage
    ) const {
        std::vector<RuntimeInternetMatchmakingServiceAction> encoded;
        encoded.reserve(assignments.size());
        for (RuntimeInternetMatchmakingAction& assignment : assignments) {
            RuntimeInternetMatchmakingServiceAction action;
            if (!makeResponse(
                    assignment.targetPeerId,
                    RuntimeInternetMatchmakingMessageType::Assignment,
                    assignment.assignment.requestId,
                    std::move(assignment.assignment),
                    action,
                    errorMessage
                )) {
                return false;
            }
            encoded.push_back(std::move(action));
        }
        actions.insert(
            actions.end(),
            std::make_move_iterator(encoded.begin()),
            std::make_move_iterator(encoded.end())
        );
        return true;
    }
};

RuntimeInternetMatchmakingService::RuntimeInternetMatchmakingService()
    : impl_(std::make_unique<Impl>()) {}

RuntimeInternetMatchmakingService::~RuntimeInternetMatchmakingService() =
    default;

RuntimeInternetMatchmakingService::RuntimeInternetMatchmakingService(
    RuntimeInternetMatchmakingService&&
) noexcept = default;

RuntimeInternetMatchmakingService&
RuntimeInternetMatchmakingService::operator=(
    RuntimeInternetMatchmakingService&&
) noexcept = default;

bool RuntimeInternetMatchmakingService::open(
    RuntimeInternetMatchmakingConfig config,
    std::string& errorMessage
) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    RuntimeInternetMatchmaking authority;
    RuntimeInternetMatchmakingConfig authorityConfig = config;
    if (!authority.open(std::move(authorityConfig), errorMessage)) {
        return false;
    }
    impl_->authority = std::move(authority);
    impl_->serviceId = std::move(config.serviceId);
    impl_->queueLeaseMilliseconds = config.queueLeaseMilliseconds;
    impl_->open = true;
    errorMessage.clear();
    return true;
}

bool RuntimeInternetMatchmakingService::receive(
    const RuntimeInternetMatchmakingServiceEnvelope& envelope,
    std::vector<RuntimeInternetMatchmakingServiceAction>& actions,
    std::string& errorMessage
) {
    actions.clear();
    if (!impl_ || !impl_->open) {
        errorMessage = "matchmaking wire service is not open";
        return false;
    }
    RuntimeInternetMatchmakingMessage message;
    if (!decodeRuntimeInternetMatchmakingMessage(
            envelope.packet,
            message,
            errorMessage
        )) {
        return false;
    }
    if (envelope.authenticatedServiceId != impl_->serviceId
        || message.serviceId != impl_->serviceId
        || envelope.authenticatedPeerId != message.senderPeerId
        || !message.targetPeerId.empty()
        || !requestType(message.type)) {
        errorMessage =
            "matchmaking authenticated identity or request direction mismatch";
        return false;
    }

    RuntimeInternetMatchmakingOperationResult operationResult;
    std::vector<RuntimeInternetMatchmakingAction> releasedAssignments;
    switch (message.type) {
    case RuntimeInternetMatchmakingMessageType::Enqueue:
        operationResult = impl_->authority.enqueue(
            envelope.authenticatedServiceId,
            envelope.authenticatedPeerId,
            std::get<RuntimeInternetMatchmakingRequest>(message.body)
        );
        break;
    case RuntimeInternetMatchmakingMessageType::Heartbeat:
        operationResult = impl_->authority.heartbeat(
            envelope.authenticatedServiceId,
            envelope.authenticatedPeerId,
            std::get<RuntimeInternetMatchmakingRequestIdBody>(
                message.body
            ).requestId
        );
        break;
    case RuntimeInternetMatchmakingMessageType::Cancel:
        operationResult = impl_->authority.cancel(
            envelope.authenticatedServiceId,
            envelope.authenticatedPeerId,
            std::get<RuntimeInternetMatchmakingRequestIdBody>(
                message.body
            ).requestId
        );
        break;
    case RuntimeInternetMatchmakingMessageType::CloseMatch:
        operationResult = impl_->authority.closeMatch(
            envelope.authenticatedServiceId,
            envelope.authenticatedPeerId,
            std::get<RuntimeInternetMatchmakingCloseBody>(
                message.body
            ).matchId
        );
        break;
    case RuntimeInternetMatchmakingMessageType::ActivateMatch:
        operationResult = impl_->authority.activateMatch(
            envelope.authenticatedServiceId,
            envelope.authenticatedPeerId,
            std::get<RuntimeInternetMatchmakingCloseBody>(
                message.body
            ).matchId,
            releasedAssignments
        );
        break;
    default:
        errorMessage = "unexpected matchmaking request type";
        return false;
    }
    if (!impl_->appendOperationResponse(
            envelope.authenticatedPeerId,
            operationFor(message.type),
            message.transactionId,
            operationResult,
            actions,
            errorMessage
        )) {
        return false;
    }
    return !operationResult.accepted
        || impl_->appendAssignments(
            std::move(releasedAssignments),
            actions,
            errorMessage
        );
}

std::vector<RuntimeInternetMatchmakingServiceAction>
RuntimeInternetMatchmakingService::poll(std::string& errorMessage) {
    std::vector<RuntimeInternetMatchmakingServiceAction> result;
    if (!impl_ || !impl_->open) {
        errorMessage = "matchmaking wire service is not open";
        return result;
    }
    std::vector<RuntimeInternetMatchmakingAction> assignments =
        impl_->authority.poll(errorMessage);
    if (!errorMessage.empty()) return result;
    if (!impl_->appendAssignments(
            std::move(assignments),
            result,
            errorMessage
        )) {
        result.clear();
        return result;
    }
    errorMessage.clear();
    return result;
}

bool RuntimeInternetMatchmakingService::authorizeAdmission(
    std::string_view roomId,
    std::string_view authenticatedPeerId,
    std::span<const std::uint8_t> admissionProof
) const {
    return impl_ && impl_->open
        && impl_->authority.authorizeAdmission(
            roomId,
            authenticatedPeerId,
            admissionProof
        );
}

RuntimeInternetMatchmakingOperationResult
RuntimeInternetMatchmakingService::authenticatedPeerDisconnected(
    std::string_view authenticatedServiceId,
    std::string_view authenticatedPeerId
) {
    if (!impl_ || !impl_->open) {
        RuntimeInternetMatchmakingOperationResult result;
        result.reason = "matchmaking wire service is not open";
        return result;
    }
    return impl_->authority.authenticatedPeerDisconnected(
        authenticatedServiceId,
        authenticatedPeerId
    );
}

bool RuntimeInternetMatchmakingService::isOpen() const {
    return impl_ && impl_->open;
}

std::size_t RuntimeInternetMatchmakingService::queuedPeerCount() const {
    return impl_ ? impl_->authority.queuedPeerCount() : 0U;
}

std::size_t RuntimeInternetMatchmakingService::activeMatchCount() const {
    return impl_ ? impl_->authority.activeMatchCount() : 0U;
}

void RuntimeInternetMatchmakingService::close() {
    if (!impl_) return;
    impl_->authority.close();
    impl_->serviceId.clear();
    impl_->queueLeaseMilliseconds = 0U;
    impl_->open = false;
}

bool RuntimeInternetMatchmakingClient::makeRequest(
    RuntimeInternetMatchmakingMessageType type,
    std::uint64_t transactionId,
    RuntimeInternetMatchmakingMessageBody body,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) const {
    RuntimeInternetMatchmakingMessage message;
    message.type = type;
    message.transactionId = transactionId;
    message.serviceId = config_.serviceId;
    message.senderPeerId = config_.localPeerId;
    message.body = std::move(body);
    return encodeRuntimeInternetMatchmakingMessage(
        message,
        packet,
        errorMessage
    );
}

bool RuntimeInternetMatchmakingClient::begin(
    RuntimeInternetMatchmakingClientConfig config,
    RuntimeInternetMatchmakingRequest request,
    std::uint64_t,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    if (state_ != RuntimeInternetMatchmakingClientState::Idle
        && state_ != RuntimeInternetMatchmakingClientState::Closed
        && state_ != RuntimeInternetMatchmakingClientState::Cancelled
        && state_ != RuntimeInternetMatchmakingClientState::Failed) {
        errorMessage = "matchmaking client is already active";
        return false;
    }
    if (!validIdentifier(
            config.serviceId,
            runtimeInternetMatchmakingMaximumPeerIdBytes
        )
        || !validIdentifier(
            config.localPeerId,
            runtimeInternetMatchmakingMaximumPeerIdBytes
        )
        || config.heartbeatMilliseconds == 0U
        || !validRequest(request)) {
        errorMessage = "invalid matchmaking client configuration or request";
        return false;
    }
    RuntimeInternetMatchmakingClient candidate;
    candidate.config_ = std::move(config);
    candidate.request_ = std::move(request);
    if (!candidate.makeRequest(
            RuntimeInternetMatchmakingMessageType::Enqueue,
            candidate.request_.requestId,
            candidate.request_,
            packet,
            errorMessage
        )) {
        return false;
    }
    candidate.state_ = RuntimeInternetMatchmakingClientState::Enqueueing;
    *this = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool RuntimeInternetMatchmakingClient::receive(
    const RuntimeNetworkPacket& packet,
    std::string_view authenticatedServiceId,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    if (state_ == RuntimeInternetMatchmakingClientState::Idle
        || state_ == RuntimeInternetMatchmakingClientState::Closed) {
        errorMessage = "matchmaking client is not receiving";
        return false;
    }
    RuntimeInternetMatchmakingMessage message;
    if (!decodeRuntimeInternetMatchmakingMessage(
            packet,
            message,
            errorMessage
        )) {
        return false;
    }
    if (authenticatedServiceId != config_.serviceId
        || message.serviceId != config_.serviceId
        || message.senderPeerId != config_.serviceId
        || message.targetPeerId != config_.localPeerId
        || requestType(message.type)) {
        errorMessage =
            "matchmaking response authenticated identity or direction mismatch";
        return false;
    }

    if (message.type == RuntimeInternetMatchmakingMessageType::Assignment) {
        const RuntimeInternetMatchmakingAssignment& assignment =
            std::get<RuntimeInternetMatchmakingAssignment>(message.body);
        if (message.transactionId != request_.requestId
            || assignment.requestId != request_.requestId
            || assignment.peerId != config_.localPeerId) {
            errorMessage = "matchmaking assignment does not match the request";
            return false;
        }
        if (state_ == RuntimeInternetMatchmakingClientState::Assigned) {
            if (assignment_ != assignment) {
                errorMessage = "conflicting matchmaking assignment replay";
                return false;
            }
            errorMessage.clear();
            return true;
        }
        if (state_ != RuntimeInternetMatchmakingClientState::Enqueueing
            && state_ != RuntimeInternetMatchmakingClientState::Queued) {
            errorMessage = "matchmaking assignment is invalid in this state";
            return false;
        }
        assignment_ = assignment;
        state_ = RuntimeInternetMatchmakingClientState::Assigned;
        matchActivated_ = false;
        negotiatedLeaseMilliseconds_ = 0U;
        nextHeartbeatMilliseconds_ = 0U;
        failureReason_.clear();
        errorMessage.clear();
        return true;
    }

    if (message.type == RuntimeInternetMatchmakingMessageType::Error) {
        const RuntimeInternetMatchmakingErrorBody& body =
            std::get<RuntimeInternetMatchmakingErrorBody>(message.body);
        const bool supersededQueueResponse =
            state_ == RuntimeInternetMatchmakingClientState::Assigned
            && body.requestId == request_.requestId
            && message.transactionId == request_.requestId
            && (body.operation
                    == RuntimeInternetMatchmakingOperation::Enqueue
                || body.operation
                    == RuntimeInternetMatchmakingOperation::Heartbeat);
        if (supersededQueueResponse) {
            // Assignment is the stronger authoritative outcome. A queued
            // heartbeat may already be in flight when the service atomically
            // removes the peer from the queue, so its delayed Ack or Error
            // cannot invalidate the assigned match.
            errorMessage.clear();
            return true;
        }
        const bool controlError =
            (body.operation == RuntimeInternetMatchmakingOperation::CloseMatch
                && closeTransactionId_ != 0U
                && body.requestId == closeTransactionId_)
            || (body.operation
                    == RuntimeInternetMatchmakingOperation::ActivateMatch
                && activationTransactionId_ != 0U
                && body.requestId == activationTransactionId_);
        if ((!controlError && body.requestId != request_.requestId)
            || body.requestId != message.transactionId) {
            errorMessage = "stale matchmaking error response";
            return false;
        }
        fail(body.reason);
        errorMessage.clear();
        return true;
    }

    if (message.type != RuntimeInternetMatchmakingMessageType::OperationAck) {
        errorMessage = "unexpected matchmaking response type";
        return false;
    }
    const RuntimeInternetMatchmakingAckBody& body =
        std::get<RuntimeInternetMatchmakingAckBody>(message.body);
    if (body.requestId != message.transactionId) {
        errorMessage = "mismatched matchmaking acknowledgement";
        return false;
    }
    if (state_ == RuntimeInternetMatchmakingClientState::Assigned
        && body.requestId == request_.requestId
        && (body.operation == RuntimeInternetMatchmakingOperation::Enqueue
            || body.operation
                == RuntimeInternetMatchmakingOperation::Heartbeat)) {
        errorMessage.clear();
        return true;
    }
    if (body.operation
        == RuntimeInternetMatchmakingOperation::ActivateMatch) {
        if (activationTransactionId_ == 0U
            || body.requestId != activationTransactionId_
            || state_ != RuntimeInternetMatchmakingClientState::Assigned) {
            errorMessage = "stale matchmaking activation acknowledgement";
            return false;
        }
        activationTransactionId_ = 0U;
        matchActivated_ = true;
        errorMessage.clear();
        return true;
    }
    if (body.operation == RuntimeInternetMatchmakingOperation::CloseMatch) {
        if (closeTransactionId_ == 0U
            || body.requestId != closeTransactionId_
            || state_ != RuntimeInternetMatchmakingClientState::Assigned) {
            errorMessage = "stale matchmaking close acknowledgement";
            return false;
        }
        closeTransactionId_ = 0U;
        state_ = RuntimeInternetMatchmakingClientState::Closed;
        errorMessage.clear();
        return true;
    }
    if (body.requestId != request_.requestId) {
        errorMessage = "stale matchmaking acknowledgement";
        return false;
    }
    if (body.operation == RuntimeInternetMatchmakingOperation::Enqueue) {
        if ((state_ != RuntimeInternetMatchmakingClientState::Enqueueing
                && state_ != RuntimeInternetMatchmakingClientState::Queued)
            || body.leaseMilliseconds == 0U) {
            errorMessage = "enqueue acknowledgement is invalid in this state";
            return false;
        }
        negotiatedLeaseMilliseconds_ = body.leaseMilliseconds;
        const std::uint32_t heartbeatDelay = std::max<std::uint32_t>(
            1U,
            std::min(
                config_.heartbeatMilliseconds,
                negotiatedLeaseMilliseconds_ / 2U
            )
        );
        nextHeartbeatMilliseconds_ = saturatingAdd(
            nowMilliseconds,
            heartbeatDelay
        );
        state_ = RuntimeInternetMatchmakingClientState::Queued;
    } else if (body.operation
        == RuntimeInternetMatchmakingOperation::Heartbeat) {
        if (state_ != RuntimeInternetMatchmakingClientState::Queued) {
            errorMessage = "heartbeat acknowledgement is invalid in this state";
            return false;
        }
        nextHeartbeatMilliseconds_ = saturatingAdd(
            nowMilliseconds,
            config_.heartbeatMilliseconds
        );
    } else if (body.operation
        == RuntimeInternetMatchmakingOperation::Cancel) {
        if (state_ != RuntimeInternetMatchmakingClientState::Cancelling) {
            errorMessage = "cancel acknowledgement is invalid in this state";
            return false;
        }
        state_ = RuntimeInternetMatchmakingClientState::Cancelled;
        negotiatedLeaseMilliseconds_ = 0U;
        nextHeartbeatMilliseconds_ = 0U;
    } else {
        errorMessage = "unexpected matchmaking acknowledgement operation";
        return false;
    }
    failureReason_.clear();
    errorMessage.clear();
    return true;
}

bool RuntimeInternetMatchmakingClient::poll(
    std::uint64_t nowMilliseconds,
    RuntimeNetworkPacket& heartbeatPacket,
    bool& hasHeartbeat,
    std::string& errorMessage
) {
    hasHeartbeat = false;
    if (state_ != RuntimeInternetMatchmakingClientState::Queued) {
        errorMessage.clear();
        return true;
    }
    if (nowMilliseconds < nextHeartbeatMilliseconds_) {
        errorMessage.clear();
        return true;
    }
    if (!makeRequest(
            RuntimeInternetMatchmakingMessageType::Heartbeat,
            request_.requestId,
            RuntimeInternetMatchmakingRequestIdBody{request_.requestId},
            heartbeatPacket,
            errorMessage
        )) {
        return false;
    }
    hasHeartbeat = true;
    nextHeartbeatMilliseconds_ = saturatingAdd(
        nowMilliseconds,
        config_.heartbeatMilliseconds
    );
    errorMessage.clear();
    return true;
}

bool RuntimeInternetMatchmakingClient::requestCancel(
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    if (state_ != RuntimeInternetMatchmakingClientState::Enqueueing
        && state_ != RuntimeInternetMatchmakingClientState::Queued) {
        errorMessage = "matchmaking request cannot be cancelled in this state";
        return false;
    }
    if (!makeRequest(
            RuntimeInternetMatchmakingMessageType::Cancel,
            request_.requestId,
            RuntimeInternetMatchmakingRequestIdBody{request_.requestId},
            packet,
            errorMessage
        )) {
        return false;
    }
    state_ = RuntimeInternetMatchmakingClientState::Cancelling;
    return true;
}

bool RuntimeInternetMatchmakingClient::requestCloseMatch(
    std::uint64_t transactionId,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    if (state_ != RuntimeInternetMatchmakingClientState::Assigned
        || assignment_.hostPeerId != config_.localPeerId
        || transactionId == 0U || closeTransactionId_ != 0U
        || activationTransactionId_ != 0U) {
        errorMessage = "only the assigned host may close this match";
        return false;
    }
    if (!makeRequest(
            RuntimeInternetMatchmakingMessageType::CloseMatch,
            transactionId,
            RuntimeInternetMatchmakingCloseBody{assignment_.matchId},
            packet,
            errorMessage
        )) {
        return false;
    }
    closeTransactionId_ = transactionId;
    return true;
}

bool RuntimeInternetMatchmakingClient::requestActivateMatch(
    std::uint64_t transactionId,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    if (state_ != RuntimeInternetMatchmakingClientState::Assigned
        || assignment_.hostPeerId != config_.localPeerId
        || transactionId == 0U || activationTransactionId_ != 0U
        || closeTransactionId_ != 0U || matchActivated_) {
        errorMessage = "only the assigned host may activate this match";
        return false;
    }
    if (!makeRequest(
            RuntimeInternetMatchmakingMessageType::ActivateMatch,
            transactionId,
            RuntimeInternetMatchmakingCloseBody{assignment_.matchId},
            packet,
            errorMessage
        )) {
        return false;
    }
    activationTransactionId_ = transactionId;
    return true;
}

RuntimeInternetMatchmakingClientState
RuntimeInternetMatchmakingClient::state() const {
    return state_;
}

const RuntimeInternetMatchmakingAssignment*
RuntimeInternetMatchmakingClient::assignment() const {
    return state_ == RuntimeInternetMatchmakingClientState::Assigned
        ? &assignment_
        : nullptr;
}

const std::string& RuntimeInternetMatchmakingClient::failureReason() const {
    return failureReason_;
}

void RuntimeInternetMatchmakingClient::fail(std::string reason) {
    state_ = RuntimeInternetMatchmakingClientState::Failed;
    failureReason_ = std::move(reason);
    negotiatedLeaseMilliseconds_ = 0U;
    nextHeartbeatMilliseconds_ = 0U;
    closeTransactionId_ = 0U;
    activationTransactionId_ = 0U;
    matchActivated_ = false;
}

void RuntimeInternetMatchmakingClient::close() {
    config_ = {};
    request_ = {};
    state_ = RuntimeInternetMatchmakingClientState::Closed;
    negotiatedLeaseMilliseconds_ = 0U;
    nextHeartbeatMilliseconds_ = 0U;
    assignment_ = {};
    closeTransactionId_ = 0U;
    activationTransactionId_ = 0U;
    matchActivated_ = false;
    failureReason_.clear();
}
