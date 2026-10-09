#include "RuntimeInternetConnectivity.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

constexpr std::uint32_t controlMagic = 0x434e4554U; // CNET

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
    void string(const std::string& value) {
        u16(static_cast<std::uint16_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }
    void blob(const std::vector<std::uint8_t>& value) {
        u16(static_cast<std::uint16_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }
private:
    std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
    bool u8(std::uint8_t& value) {
        if (remaining() < 1) return false;
        value = bytes_[offset_++];
        return true;
    }
    bool u16(std::uint16_t& value) {
        if (remaining() < 2) return false;
        value = static_cast<std::uint16_t>(bytes_[offset_]) << 8U;
        value |= bytes_[offset_ + 1];
        offset_ += 2;
        return true;
    }
    bool u32(std::uint32_t& value) {
        if (remaining() < 4) return false;
        value = 0;
        for (int index = 0; index < 4; ++index) {
            value = (value << 8U) | bytes_[offset_++];
        }
        return true;
    }
    bool u64(std::uint64_t& value) {
        if (remaining() < 8) return false;
        value = 0;
        for (int index = 0; index < 8; ++index) {
            value = (value << 8U) | bytes_[offset_++];
        }
        return true;
    }
    bool boolean(bool& value) {
        std::uint8_t raw = 0;
        if (!u8(raw) || raw > 1U) return false;
        value = raw != 0;
        return true;
    }
    bool string(std::string& value, std::size_t maximum) {
        std::uint16_t size = 0;
        if (!u16(size) || size > maximum || remaining() < size) return false;
        value.assign(
            reinterpret_cast<const char*>(bytes_.data() + offset_),
            size
        );
        offset_ += size;
        return true;
    }
    bool blob(std::vector<std::uint8_t>& value, std::size_t maximum) {
        std::uint16_t size = 0;
        if (!u16(size) || size > maximum || remaining() < size) return false;
        value.assign(bytes_.begin() + offset_, bytes_.begin() + offset_ + size);
        offset_ += size;
        return true;
    }
    bool finished() const { return offset_ == bytes_.size(); }
private:
    std::size_t remaining() const { return bytes_.size() - offset_; }
    const std::vector<std::uint8_t>& bytes_;
    std::size_t offset_ = 0;
};

bool validText(const std::string& value, std::size_t maximum, bool allowEmpty) {
    if ((!allowEmpty && value.empty()) || value.size() > maximum) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch >= 0x20U && ch != 0x7fU;
    });
}

bool validRole(RuntimeInternetRole role) {
    return role == RuntimeInternetRole::Host
        || role == RuntimeInternetRole::Client
        || role == RuntimeInternetRole::Spectator;
}

bool validRelay(RuntimeInternetRelayTransport transport) {
    return transport == RuntimeInternetRelayTransport::Udp
        || transport == RuntimeInternetRelayTransport::Tcp;
}

bool validEndpoint(const RuntimeInternetEndpoint& endpoint) {
    return endpoint.port != 0
        && validText(
            endpoint.address,
            runtimeInternetMaximumEndpointAddressBytes,
            false
        );
}

bool validCandidate(const RuntimeInternetCandidate& candidate) {
    return (candidate.kind == RuntimeInternetCandidateKind::Host
            || candidate.kind == RuntimeInternetCandidateKind::ServerReflexive)
        && validEndpoint(candidate.endpoint);
}

bool validCandidates(const std::vector<RuntimeInternetCandidate>& candidates) {
    return !candidates.empty()
        && candidates.size() <= runtimeInternetMaximumCandidates
        && std::all_of(candidates.begin(), candidates.end(), validCandidate);
}

bool validRoom(const RuntimeInternetRoomRecord& room) {
    return validText(room.roomId, runtimeInternetMaximumRoomIdBytes, false)
        && validText(room.roomName, runtimeInternetMaximumRoomNameBytes, false)
        && validText(room.hostPeerId, runtimeInternetMaximumIdentityBytes, false)
        && validText(
            room.contentFingerprint,
            runtimeInternetMaximumFingerprintBytes,
            false
        )
        && room.maximumPlayers >= 2
        && room.playerCount >= 1
        && room.playerCount <= room.maximumPlayers;
}

bool validToken(const std::vector<std::uint8_t>& value) {
    return value.size() >= runtimeInternetMinimumConnectionTokenBytes
        && value.size() <= runtimeInternetMaximumConnectionTokenBytes;
}

void writeEndpoint(Writer& writer, const RuntimeInternetEndpoint& endpoint) {
    writer.string(endpoint.address);
    writer.u16(endpoint.port);
}

bool readEndpoint(Reader& reader, RuntimeInternetEndpoint& endpoint) {
    return reader.string(endpoint.address, runtimeInternetMaximumEndpointAddressBytes)
        && reader.u16(endpoint.port)
        && validEndpoint(endpoint);
}

void writeCandidate(Writer& writer, const RuntimeInternetCandidate& candidate) {
    writer.u8(static_cast<std::uint8_t>(candidate.kind));
    writeEndpoint(writer, candidate.endpoint);
    writer.u32(candidate.priority);
}

bool readCandidate(Reader& reader, RuntimeInternetCandidate& candidate) {
    std::uint8_t kind = 0;
    if (!reader.u8(kind)) return false;
    candidate.kind = static_cast<RuntimeInternetCandidateKind>(kind);
    return readEndpoint(reader, candidate.endpoint)
        && reader.u32(candidate.priority)
        && validCandidate(candidate);
}

void writeCandidates(
    Writer& writer,
    const std::vector<RuntimeInternetCandidate>& candidates
) {
    writer.u8(static_cast<std::uint8_t>(candidates.size()));
    for (const auto& candidate : candidates) writeCandidate(writer, candidate);
}

bool readCandidates(
    Reader& reader,
    std::vector<RuntimeInternetCandidate>& candidates
) {
    std::uint8_t count = 0;
    if (!reader.u8(count) || count == 0 || count > runtimeInternetMaximumCandidates) {
        return false;
    }
    std::vector<RuntimeInternetCandidate> decoded(count);
    for (auto& candidate : decoded) {
        if (!readCandidate(reader, candidate)) return false;
    }
    candidates = std::move(decoded);
    return true;
}

void writeRoom(Writer& writer, const RuntimeInternetRoomRecord& room) {
    writer.string(room.roomId);
    writer.string(room.roomName);
    writer.string(room.hostPeerId);
    writer.string(room.contentFingerprint);
    writer.u8(room.playerCount);
    writer.u8(room.maximumPlayers);
    writer.boolean(room.passwordProtected);
}

bool readRoom(Reader& reader, RuntimeInternetRoomRecord& room) {
    return reader.string(room.roomId, runtimeInternetMaximumRoomIdBytes)
        && reader.string(room.roomName, runtimeInternetMaximumRoomNameBytes)
        && reader.string(room.hostPeerId, runtimeInternetMaximumIdentityBytes)
        && reader.string(
            room.contentFingerprint,
            runtimeInternetMaximumFingerprintBytes
        )
        && reader.u8(room.playerCount)
        && reader.u8(room.maximumPlayers)
        && reader.boolean(room.passwordProtected)
        && validRoom(room);
}

bool validEnvelope(const RuntimeInternetControlMessage& message) {
    if (message.transactionId == 0
        || !validText(
            message.serviceId,
            runtimeInternetMaximumIdentityBytes,
            false
        )) {
        return false;
    }
    const bool roomOptional = message.type
            == RuntimeInternetControlType::RoomListRequest
        || message.type == RuntimeInternetControlType::RoomListResult
        || message.type
            == RuntimeInternetControlType::CoordinatorHeartbeat
        || message.type
            == RuntimeInternetControlType::CoordinatorHeartbeatAck;
    return validText(
            message.roomId,
            runtimeInternetMaximumRoomIdBytes,
            roomOptional
        )
        && validText(
            message.senderPeerId,
            runtimeInternetMaximumIdentityBytes,
            false
        )
        && validText(
            message.targetPeerId,
            runtimeInternetMaximumIdentityBytes,
            true
        );
}

bool sameNetworkPacket(
    const RuntimeNetworkPacket& left,
    const RuntimeNetworkPacket& right
) noexcept {
    return left.type == right.type && left.payload == right.payload;
}

template<typename Body>
const Body* bodyAs(
    const RuntimeInternetControlMessage& message,
    RuntimeInternetControlType type
) {
    if (message.type != type) return nullptr;
    return std::get_if<Body>(&message.body);
}

bool encodeChecked(
    const RuntimeInternetControlMessage& message,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    if (!validEnvelope(message)) {
        errorMessage = "invalid Internet control envelope";
        return false;
    }
    Writer writer;
    writer.u32(controlMagic);
    writer.u16(runtimeInternetControlProtocolVersion);
    writer.u8(static_cast<std::uint8_t>(message.type));
    writer.u64(message.transactionId);
    writer.string(message.serviceId);
    writer.string(message.roomId);
    writer.string(message.senderPeerId);
    writer.string(message.targetPeerId);

    switch (message.type) {
    case RuntimeInternetControlType::RegisterRoom: {
        const auto* body = std::get_if<RuntimeInternetRegisterRoomBody>(&message.body);
        if (!body || !validRoom(body->room) || !validCandidates(body->candidates)
            || body->room.roomId != message.roomId
            || body->room.hostPeerId != message.senderPeerId
            || !message.targetPeerId.empty()) {
            errorMessage = "invalid room registration";
            return false;
        }
        writeRoom(writer, body->room);
        writeCandidates(writer, body->candidates);
        break;
    }
    case RuntimeInternetControlType::RegisterAccepted: {
        const auto* body = std::get_if<RuntimeInternetRegisterAcceptedBody>(&message.body);
        if (!body || body->leaseMilliseconds < 1000U || message.targetPeerId.empty()) {
            errorMessage = "invalid registration acknowledgement";
            return false;
        }
        writer.u32(body->leaseMilliseconds);
        break;
    }
    case RuntimeInternetControlType::RoomListRequest: {
        const auto* body = std::get_if<RuntimeInternetRoomListRequestBody>(&message.body);
        if (!body || !message.roomId.empty() || !message.targetPeerId.empty()
            || !validText(
                body->contentFingerprint,
                runtimeInternetMaximumFingerprintBytes,
                true
            )) {
            errorMessage = "invalid room list request";
            return false;
        }
        writer.string(body->contentFingerprint);
        break;
    }
    case RuntimeInternetControlType::RoomListResult: {
        const auto* body = std::get_if<RuntimeInternetRoomListResultBody>(&message.body);
        if (!body || !message.roomId.empty() || message.targetPeerId.empty()
            || body->rooms.size() > runtimeInternetMaximumRooms
            || !std::all_of(body->rooms.begin(), body->rooms.end(), validRoom)) {
            errorMessage = "invalid room list result";
            return false;
        }
        writer.u16(static_cast<std::uint16_t>(body->rooms.size()));
        for (const auto& room : body->rooms) writeRoom(writer, room);
        break;
    }
    case RuntimeInternetControlType::JoinRoom: {
        const auto* body = std::get_if<RuntimeInternetJoinRoomBody>(&message.body);
        if (!body || (body->role != RuntimeInternetRole::Client
                && body->role != RuntimeInternetRole::Spectator)
            || !validText(
                body->contentFingerprint,
                runtimeInternetMaximumFingerprintBytes,
                false
            )
            || !validCandidates(body->candidates)
            || !message.targetPeerId.empty()) {
            errorMessage = "invalid join-room request";
            return false;
        }
        writer.u8(static_cast<std::uint8_t>(body->role));
        writer.string(body->contentFingerprint);
        writeCandidates(writer, body->candidates);
        break;
    }
    case RuntimeInternetControlType::PeerCandidates: {
        const auto* body = std::get_if<RuntimeInternetPeerCandidatesBody>(&message.body);
        if (!body || body->connectionId == 0 || body->remotePeerId.empty()
            || !validRole(body->remoteRole) || !validCandidates(body->candidates)
            || !validToken(body->connectionToken) || message.targetPeerId.empty()
            || body->resumesConnectionId == body->connectionId) {
            errorMessage = "invalid peer candidates";
            return false;
        }
        writer.u64(body->connectionId);
        writer.string(body->remotePeerId);
        writer.u8(static_cast<std::uint8_t>(body->remoteRole));
        writeCandidates(writer, body->candidates);
        writer.blob(body->connectionToken);
        writer.u64(body->resumesConnectionId);
        break;
    }
    case RuntimeInternetControlType::ResumeRoute: {
        const auto* body = std::get_if<RuntimeInternetResumeRouteBody>(&message.body);
        if (!body || body->connectionId == 0 || !validToken(body->connectionToken)
            || message.transactionId != body->connectionId
            || message.targetPeerId != message.serviceId) {
            errorMessage = "invalid Internet route resumption request";
            return false;
        }
        writer.u64(body->connectionId);
        writer.blob(body->connectionToken);
        break;
    }
    case RuntimeInternetControlType::PunchProbe:
    case RuntimeInternetControlType::PunchAck: {
        const auto* body = std::get_if<RuntimeInternetPunchBody>(&message.body);
        if (!body || body->connectionId == 0 || !validToken(body->connectionToken)
            || message.targetPeerId.empty()) {
            errorMessage = "invalid NAT punch message";
            return false;
        }
        writer.u64(body->connectionId);
        writer.blob(body->connectionToken);
        break;
    }
    case RuntimeInternetControlType::RelayRequest: {
        const auto* body = std::get_if<RuntimeInternetRelayRequestBody>(&message.body);
        if (!body || body->connectionId == 0 || !validRelay(body->transport)
            || !validToken(body->connectionToken) || !message.targetPeerId.empty()) {
            errorMessage = "invalid relay request";
            return false;
        }
        writer.u64(body->connectionId);
        writer.u8(static_cast<std::uint8_t>(body->transport));
        writer.blob(body->connectionToken);
        break;
    }
    case RuntimeInternetControlType::RelayLease: {
        const auto* body = std::get_if<RuntimeInternetRelayLeaseBody>(&message.body);
        if (!body || body->connectionId == 0 || !validRelay(body->transport)
            || !validEndpoint(body->endpoint) || body->ticket.size() < 16
            || body->ticket.size() > runtimeInternetMaximumRelayTicketBytes
            || message.targetPeerId.empty()) {
            errorMessage = "invalid relay lease";
            return false;
        }
        writer.u64(body->connectionId);
        writer.u8(static_cast<std::uint8_t>(body->transport));
        writeEndpoint(writer, body->endpoint);
        writer.blob(body->ticket);
        break;
    }
    case RuntimeInternetControlType::RelayBind: {
        const auto* body = std::get_if<RuntimeInternetRelayBindBody>(&message.body);
        if (!body || body->connectionId == 0 || !validRelay(body->transport)
            || body->ticket.size() < 16
            || body->ticket.size() > runtimeInternetMaximumRelayTicketBytes
            || message.targetPeerId.empty()) {
            errorMessage = "invalid relay bind";
            return false;
        }
        writer.u64(body->connectionId);
        writer.u8(static_cast<std::uint8_t>(body->transport));
        writer.blob(body->ticket);
        break;
    }
    case RuntimeInternetControlType::RelayBound: {
        const auto* body = std::get_if<RuntimeInternetRelayBoundBody>(&message.body);
        if (!body || body->connectionId == 0 || !validRelay(body->transport)
            || message.targetPeerId.empty()) {
            errorMessage = "invalid relay acknowledgement";
            return false;
        }
        writer.u64(body->connectionId);
        writer.u8(static_cast<std::uint8_t>(body->transport));
        break;
    }
    case RuntimeInternetControlType::SealRoom: {
        const auto* body = std::get_if<RuntimeInternetSealRoomBody>(
            &message.body
        );
        if (!body || !message.targetPeerId.empty()) {
            errorMessage = "invalid room seal request";
            return false;
        }
        break;
    }
    case RuntimeInternetControlType::RoomSealed: {
        const auto* body = std::get_if<RuntimeInternetRoomSealedBody>(
            &message.body
        );
        if (!body || message.targetPeerId.empty()) {
            errorMessage = "invalid room seal acknowledgement";
            return false;
        }
        break;
    }
    case RuntimeInternetControlType::CoordinatorHeartbeat: {
        const auto* body = std::get_if<
            RuntimeInternetCoordinatorHeartbeatBody
        >(&message.body);
        if (!body || body->sequence == 0U
            || !message.targetPeerId.empty()) {
            errorMessage = "invalid coordinator heartbeat";
            return false;
        }
        writer.u64(body->sequence);
        break;
    }
    case RuntimeInternetControlType::CoordinatorHeartbeatAck: {
        const auto* body = std::get_if<
            RuntimeInternetCoordinatorHeartbeatAckBody
        >(&message.body);
        if (!body || body->sequence == 0U
            || message.targetPeerId.empty()) {
            errorMessage = "invalid coordinator heartbeat acknowledgement";
            return false;
        }
        writer.u64(body->sequence);
        break;
    }
    case RuntimeInternetControlType::Error: {
        const auto* body = std::get_if<RuntimeInternetErrorBody>(&message.body);
        if (!body || !validText(body->reason, runtimeInternetMaximumErrorBytes, false)
            || message.targetPeerId.empty()) {
            errorMessage = "invalid Internet control error";
            return false;
        }
        writer.string(body->reason);
        break;
    }
    default:
        errorMessage = "unknown Internet control message type";
        return false;
    }

    std::vector<std::uint8_t> payload = writer.take();
    if (payload.size() > runtimeInternetMaximumControlBytes) {
        errorMessage = "Internet control message exceeds size limit";
        return false;
    }
    RuntimeNetworkPacket encoded;
    encoded.type = runtimeInternetControlPacketType;
    encoded.payload = std::move(payload);
    packet = std::move(encoded);
    errorMessage.clear();
    return true;
}

bool decodeBody(
    Reader& reader,
    RuntimeInternetControlMessage& message,
    std::string& errorMessage
) {
    switch (message.type) {
    case RuntimeInternetControlType::RegisterRoom: {
        RuntimeInternetRegisterRoomBody body;
        if (!readRoom(reader, body.room) || !readCandidates(reader, body.candidates)) {
            break;
        }
        message.body = std::move(body);
        return true;
    }
    case RuntimeInternetControlType::RegisterAccepted: {
        RuntimeInternetRegisterAcceptedBody body;
        if (!reader.u32(body.leaseMilliseconds)) break;
        message.body = body;
        return true;
    }
    case RuntimeInternetControlType::RoomListRequest: {
        RuntimeInternetRoomListRequestBody body;
        if (!reader.string(
                body.contentFingerprint,
                runtimeInternetMaximumFingerprintBytes
            )) break;
        message.body = std::move(body);
        return true;
    }
    case RuntimeInternetControlType::RoomListResult: {
        RuntimeInternetRoomListResultBody body;
        std::uint16_t count = 0;
        if (!reader.u16(count) || count > runtimeInternetMaximumRooms) break;
        body.rooms.resize(count);
        for (auto& room : body.rooms) {
            if (!readRoom(reader, room)) {
                errorMessage = "invalid room list payload";
                return false;
            }
        }
        message.body = std::move(body);
        return true;
    }
    case RuntimeInternetControlType::JoinRoom: {
        RuntimeInternetJoinRoomBody body;
        std::uint8_t role = 0;
        if (!reader.u8(role)) break;
        body.role = static_cast<RuntimeInternetRole>(role);
        if (!reader.string(
                body.contentFingerprint,
                runtimeInternetMaximumFingerprintBytes
            ) || !readCandidates(reader, body.candidates)) break;
        message.body = std::move(body);
        return true;
    }
    case RuntimeInternetControlType::PeerCandidates: {
        RuntimeInternetPeerCandidatesBody body;
        std::uint8_t role = 0;
        if (!reader.u64(body.connectionId)
            || !reader.string(body.remotePeerId, runtimeInternetMaximumIdentityBytes)
            || !reader.u8(role)) break;
        body.remoteRole = static_cast<RuntimeInternetRole>(role);
        if (!readCandidates(reader, body.candidates)
            || !reader.blob(
                body.connectionToken,
                runtimeInternetMaximumConnectionTokenBytes
            ) || !reader.u64(body.resumesConnectionId)) break;
        message.body = std::move(body);
        return true;
    }
    case RuntimeInternetControlType::ResumeRoute: {
        RuntimeInternetResumeRouteBody body;
        if (!reader.u64(body.connectionId)
            || !reader.blob(body.connectionToken, runtimeInternetMaximumConnectionTokenBytes)) break;
        message.body = std::move(body);
        return true;
    }
    case RuntimeInternetControlType::PunchProbe:
    case RuntimeInternetControlType::PunchAck: {
        RuntimeInternetPunchBody body;
        if (!reader.u64(body.connectionId)
            || !reader.blob(
                body.connectionToken,
                runtimeInternetMaximumConnectionTokenBytes
            )) break;
        message.body = std::move(body);
        return true;
    }
    case RuntimeInternetControlType::RelayRequest: {
        RuntimeInternetRelayRequestBody body;
        std::uint8_t transport = 0;
        if (!reader.u64(body.connectionId) || !reader.u8(transport)
            || !reader.blob(
                body.connectionToken,
                runtimeInternetMaximumConnectionTokenBytes
            )) break;
        body.transport = static_cast<RuntimeInternetRelayTransport>(transport);
        message.body = std::move(body);
        return true;
    }
    case RuntimeInternetControlType::RelayLease: {
        RuntimeInternetRelayLeaseBody body;
        std::uint8_t transport = 0;
        if (!reader.u64(body.connectionId) || !reader.u8(transport)) break;
        body.transport = static_cast<RuntimeInternetRelayTransport>(transport);
        if (!readEndpoint(reader, body.endpoint)
            || !reader.blob(body.ticket, runtimeInternetMaximumRelayTicketBytes)) break;
        message.body = std::move(body);
        return true;
    }
    case RuntimeInternetControlType::RelayBind: {
        RuntimeInternetRelayBindBody body;
        std::uint8_t transport = 0;
        if (!reader.u64(body.connectionId) || !reader.u8(transport)
            || !reader.blob(body.ticket, runtimeInternetMaximumRelayTicketBytes)) break;
        body.transport = static_cast<RuntimeInternetRelayTransport>(transport);
        message.body = std::move(body);
        return true;
    }
    case RuntimeInternetControlType::RelayBound: {
        RuntimeInternetRelayBoundBody body;
        std::uint8_t transport = 0;
        if (!reader.u64(body.connectionId) || !reader.u8(transport)) break;
        body.transport = static_cast<RuntimeInternetRelayTransport>(transport);
        message.body = body;
        return true;
    }
    case RuntimeInternetControlType::SealRoom:
        message.body = RuntimeInternetSealRoomBody{};
        return true;
    case RuntimeInternetControlType::RoomSealed:
        message.body = RuntimeInternetRoomSealedBody{};
        return true;
    case RuntimeInternetControlType::CoordinatorHeartbeat: {
        RuntimeInternetCoordinatorHeartbeatBody body;
        if (!reader.u64(body.sequence)) break;
        message.body = body;
        return true;
    }
    case RuntimeInternetControlType::CoordinatorHeartbeatAck: {
        RuntimeInternetCoordinatorHeartbeatAckBody body;
        if (!reader.u64(body.sequence)) break;
        message.body = body;
        return true;
    }
    case RuntimeInternetControlType::Error: {
        RuntimeInternetErrorBody body;
        if (!reader.string(body.reason, runtimeInternetMaximumErrorBytes)) break;
        message.body = std::move(body);
        return true;
    }
    default:
        break;
    }
    errorMessage = "truncated or invalid Internet control payload";
    return false;
}

bool endpointInCandidates(
    const RuntimeInternetEndpoint& endpoint,
    const std::vector<RuntimeInternetCandidate>& candidates
) {
    return std::any_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
        return candidate.endpoint == endpoint;
    });
}

RuntimeInternetControlMessage baseMessage(
    const RuntimeInternetConnectionConfig& config,
    RuntimeInternetControlType type,
    std::string target = {}
) {
    RuntimeInternetControlMessage message;
    message.type = type;
    message.transactionId = config.transactionId;
    message.serviceId = config.serviceId;
    message.roomId = config.roomId;
    message.senderPeerId = config.localPeerId;
    message.targetPeerId = std::move(target);
    return message;
}

} // namespace

bool encodeRuntimeInternetControlMessage(
    const RuntimeInternetControlMessage& message,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    return encodeChecked(message, packet, errorMessage);
}

bool decodeRuntimeInternetControlMessage(
    const RuntimeNetworkPacket& packet,
    RuntimeInternetControlMessage& message,
    std::string& errorMessage
) {
    if (packet.type != runtimeInternetControlPacketType) {
        errorMessage = "wrong Internet control packet type or version";
        return false;
    }
    if (packet.payload.empty()
        || packet.payload.size() > runtimeInternetMaximumControlBytes) {
        errorMessage = "invalid Internet control packet length";
        return false;
    }
    Reader reader(packet.payload);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint8_t type = 0;
    RuntimeInternetControlMessage decoded;
    if (!reader.u32(magic) || magic != controlMagic
        || !reader.u16(version) || version != runtimeInternetControlProtocolVersion
        || !reader.u8(type)
        || type < static_cast<std::uint8_t>(RuntimeInternetControlType::RegisterRoom)
        || type > static_cast<std::uint8_t>(
            RuntimeInternetControlType::ResumeRoute
        )
        || !reader.u64(decoded.transactionId)
        || !reader.string(decoded.serviceId, runtimeInternetMaximumIdentityBytes)
        || !reader.string(decoded.roomId, runtimeInternetMaximumRoomIdBytes)
        || !reader.string(decoded.senderPeerId, runtimeInternetMaximumIdentityBytes)
        || !reader.string(decoded.targetPeerId, runtimeInternetMaximumIdentityBytes)) {
        errorMessage = "invalid Internet control header";
        return false;
    }
    decoded.type = static_cast<RuntimeInternetControlType>(type);
    if (!decodeBody(reader, decoded, errorMessage) || !reader.finished()) {
        if (errorMessage.empty()) errorMessage = "trailing Internet control bytes";
        return false;
    }
    RuntimeNetworkPacket canonicalPacket;
    std::string validationError;
    if (!encodeChecked(decoded, canonicalPacket, validationError)
        || canonicalPacket.payload != packet.payload) {
        errorMessage = validationError.empty()
            ? "non-canonical Internet control message"
            : validationError;
        return false;
    }
    message = std::move(decoded);
    errorMessage.clear();
    return true;
}

bool RuntimeInternetConnection::start(
    RuntimeInternetConnectionConfig config,
    RuntimeInternetConnectionState initialState,
    RuntimeInternetControlMessage request,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    if (state_ != RuntimeInternetConnectionState::Idle
        && state_ != RuntimeInternetConnectionState::Closed
        && state_ != RuntimeInternetConnectionState::Failed) {
        errorMessage = "Internet connection is already active";
        return false;
    }
    if (!validText(config.serviceId, runtimeInternetMaximumIdentityBytes, false)
        || !validText(config.localPeerId, runtimeInternetMaximumIdentityBytes, false)
        || config.transactionId == 0
        || config.coordinatorHeartbeatMilliseconds < 1000U
        || config.retryMilliseconds == 0
        || config.coordinatorTimeoutMilliseconds < config.retryMilliseconds
        || config.punchTimeoutMilliseconds < config.retryMilliseconds
        || config.relayTimeoutMilliseconds < config.retryMilliseconds
        || config.maximumRetries == 0) {
        errorMessage = "invalid Internet connection configuration";
        return false;
    }
    RuntimeNetworkPacket packet;
    if (!encodeRuntimeInternetControlMessage(request, packet, errorMessage)) return false;
    config_ = std::move(config);
    state_ = initialState;
    pendingCoordinatorPacket_ = packet;
    stateStartedMilliseconds_ = nowMilliseconds;
    nextRetryMilliseconds_ = nowMilliseconds + config_.retryMilliseconds;
    retryCount_ = 0;
    sealRoomPacket_ = {};
    sealStartedMilliseconds_ = 0U;
    nextSealRetryMilliseconds_ = 0U;
    sealRetryCount_ = 0U;
    sealRequested_ = false;
    roomSealed_ = false;
    coordinatorHeartbeatPacket_ = {};
    heartbeatStartedMilliseconds_ = 0U;
    nextHeartbeatMilliseconds_ = nowMilliseconds
        + config_.coordinatorHeartbeatMilliseconds;
    nextHeartbeatRetryMilliseconds_ = 0U;
    heartbeatRetryCount_ = 0U;
    awaitingHeartbeatAcknowledgement_ = false;
    nextHeartbeatSequence_ = 1U;
    awaitingHeartbeatSequence_ = 0U;
    acknowledgedHeartbeatSequence_ = 0U;
    acceptedRegistrationResponse_.reset();
    acceptedRoomListResponse_.reset();
    acceptedPeerCandidatesResponse_.reset();
    acceptedRelayLeaseUdpResponse_.reset();
    acceptedRelayLeaseTcpResponse_.reset();
    acceptedRoomSealedResponse_.reset();
    acceptedRelayBoundUdpResponse_.reset();
    acceptedRelayBoundTcpResponse_.reset();
    failureReason_.clear();
    rooms_.clear();
    remotePeerId_.clear();
    tcpRoute_.reset();
    queuedActions_.push_back({RuntimeInternetActionKind::SendCoordinator, {}, packet});
    errorMessage.clear();
    return true;
}

bool RuntimeInternetConnection::beginHost(
    RuntimeInternetConnectionConfig config,
    RuntimeInternetRoomRecord room,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    if (config.role != RuntimeInternetRole::Host
        || room.roomId != config.roomId
        || room.hostPeerId != config.localPeerId
        || room.contentFingerprint != config.contentFingerprint
        || !validRoom(room) || !validCandidates(config.localCandidates)) {
        errorMessage = "invalid Internet host registration";
        return false;
    }
    RuntimeInternetControlMessage message = baseMessage(
        config,
        RuntimeInternetControlType::RegisterRoom
    );
    message.body = RuntimeInternetRegisterRoomBody{room, config.localCandidates};
    hostedRoom_ = room;
    return start(
        std::move(config),
        RuntimeInternetConnectionState::RegisteringRoom,
        std::move(message),
        nowMilliseconds,
        errorMessage
    );
}

bool RuntimeInternetConnection::beginJoin(
    RuntimeInternetConnectionConfig config,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    if ((config.role != RuntimeInternetRole::Client
            && config.role != RuntimeInternetRole::Spectator)
        || !validText(config.roomId, runtimeInternetMaximumRoomIdBytes, false)
        || !validText(
            config.contentFingerprint,
            runtimeInternetMaximumFingerprintBytes,
            false
        )
        || !validCandidates(config.localCandidates)) {
        errorMessage = "invalid Internet room join";
        return false;
    }
    RuntimeInternetControlMessage message = baseMessage(
        config,
        RuntimeInternetControlType::JoinRoom
    );
    message.body = RuntimeInternetJoinRoomBody{
        config.role,
        config.contentFingerprint,
        config.localCandidates
    };
    return start(
        std::move(config),
        RuntimeInternetConnectionState::JoiningRoom,
        std::move(message),
        nowMilliseconds,
        errorMessage
    );
}

bool RuntimeInternetConnection::beginRoomQuery(
    RuntimeInternetConnectionConfig config,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    if (!config.roomId.empty()
        || !validText(
            config.contentFingerprint,
            runtimeInternetMaximumFingerprintBytes,
            true
        )) {
        errorMessage = "invalid Internet room query";
        return false;
    }
    RuntimeInternetControlMessage message = baseMessage(
        config,
        RuntimeInternetControlType::RoomListRequest
    );
    message.body = RuntimeInternetRoomListRequestBody{config.contentFingerprint};
    return start(
        std::move(config),
        RuntimeInternetConnectionState::QueryingRooms,
        std::move(message),
        nowMilliseconds,
        errorMessage
    );
}

bool RuntimeInternetConnection::beginPeerRouteFromCoordinator(
    RuntimeInternetConnectionConfig config,
    const RuntimeNetworkPacket& candidatePacket,
    const std::string& authenticatedServiceId,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    if (state_ != RuntimeInternetConnectionState::Idle
        && state_ != RuntimeInternetConnectionState::Closed
        && state_ != RuntimeInternetConnectionState::Failed) {
        errorMessage = "Internet peer route is already active";
        return false;
    }
    if (!validText(config.serviceId, runtimeInternetMaximumIdentityBytes, false)
        || !validText(config.localPeerId, runtimeInternetMaximumIdentityBytes, false)
        || !validText(config.roomId, runtimeInternetMaximumRoomIdBytes, false)
        || !validRole(config.role) || !validCandidates(config.localCandidates)
        || config.transactionId == 0
        || config.coordinatorHeartbeatMilliseconds < 1000U
        || config.retryMilliseconds == 0
        || config.coordinatorTimeoutMilliseconds < config.retryMilliseconds
        || config.punchTimeoutMilliseconds < config.retryMilliseconds
        || config.relayTimeoutMilliseconds < config.retryMilliseconds
        || config.maximumRetries == 0) {
        errorMessage = "invalid Internet peer-route configuration";
        return false;
    }
    RuntimeInternetControlMessage message;
    if (!decodeRuntimeInternetControlMessage(candidatePacket, message, errorMessage)) {
        return false;
    }
    const auto* body = bodyAs<RuntimeInternetPeerCandidatesBody>(
        message,
        RuntimeInternetControlType::PeerCandidates
    );
    if (!body || authenticatedServiceId != config.serviceId
        || message.serviceId != config.serviceId
        || message.senderPeerId != config.serviceId
        || message.targetPeerId != config.localPeerId
        || message.roomId != config.roomId) {
        errorMessage = "peer-route coordinator identity, room, or target mismatch";
        return false;
    }
    config_ = std::move(config);
    failureReason_.clear();
    rooms_.clear();
    queuedActions_.clear();
    coordinatorHeartbeatPacket_ = {};
    heartbeatStartedMilliseconds_ = 0U;
    nextHeartbeatMilliseconds_ = nowMilliseconds
        + config_.coordinatorHeartbeatMilliseconds;
    nextHeartbeatRetryMilliseconds_ = 0U;
    heartbeatRetryCount_ = 0U;
    awaitingHeartbeatAcknowledgement_ = false;
    nextHeartbeatSequence_ = 1U;
    awaitingHeartbeatSequence_ = 0U;
    acknowledgedHeartbeatSequence_ = 0U;
    acceptedRegistrationResponse_.reset();
    acceptedRoomListResponse_.reset();
    acceptedPeerCandidatesResponse_.reset();
    acceptedRelayLeaseUdpResponse_.reset();
    acceptedRelayLeaseTcpResponse_.reset();
    acceptedRoomSealedResponse_.reset();
    acceptedRelayBoundUdpResponse_.reset();
    acceptedRelayBoundTcpResponse_.reset();
    return acceptCandidates(message, *body, nowMilliseconds, errorMessage);
}

bool RuntimeInternetConnection::requestRoomSeal(
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    if (config_.role != RuntimeInternetRole::Host
        || state_ != RuntimeInternetConnectionState::WaitingForPeer) {
        errorMessage =
            "room seal requires a waiting host registration carrier";
        return false;
    }
    if (roomSealed_ || sealRequested_) {
        errorMessage.clear();
        return true;
    }
    RuntimeInternetControlMessage message = baseMessage(
        config_,
        RuntimeInternetControlType::SealRoom
    );
    message.body = RuntimeInternetSealRoomBody{};
    RuntimeNetworkPacket packet;
    if (!encodeRuntimeInternetControlMessage(
            message,
            packet,
            errorMessage
        )) {
        return false;
    }
    sealRoomPacket_ = packet;
    sealRequested_ = true;
    roomSealed_ = false;
    sealStartedMilliseconds_ = nowMilliseconds;
    nextSealRetryMilliseconds_ = nowMilliseconds
        + config_.retryMilliseconds;
    sealRetryCount_ = 0U;
    queuedActions_.push_back({
        RuntimeInternetActionKind::SendCoordinator,
        {},
        std::move(packet)
    });
    errorMessage.clear();
    return true;
}

std::vector<RuntimeInternetAction> RuntimeInternetConnection::poll(
    std::uint64_t nowMilliseconds
) {
    std::vector<RuntimeInternetAction> actions;
    actions.swap(queuedActions_);
    const bool queuedCoordinatorAction = std::any_of(
        actions.begin(),
        actions.end(),
        [](const RuntimeInternetAction& action) {
            return action.kind == RuntimeInternetActionKind::SendCoordinator;
        }
    );
    const auto timedOut = [&](std::uint32_t duration) {
        return nowMilliseconds - stateStartedMilliseconds_ >= duration;
    };
    const auto retryCoordinator = [&]() {
        if (queuedCoordinatorAction) return;
        if (nowMilliseconds < nextRetryMilliseconds_) return;
        if (retryCount_ >= config_.maximumRetries) {
            fail("coordinator retry limit exceeded");
            return;
        }
        actions.push_back({
            RuntimeInternetActionKind::SendCoordinator,
            {},
            pendingCoordinatorPacket_
        });
        ++retryCount_;
        nextRetryMilliseconds_ = nowMilliseconds + config_.retryMilliseconds;
    };

    switch (state_) {
    case RuntimeInternetConnectionState::RegisteringRoom:
    case RuntimeInternetConnectionState::QueryingRooms:
    case RuntimeInternetConnectionState::JoiningRoom:
        if (timedOut(config_.coordinatorTimeoutMilliseconds)) {
            fail("coordinator request timed out");
        } else {
            retryCoordinator();
        }
        break;
    case RuntimeInternetConnectionState::WaitingForPeer:
        if (hostLeaseRefreshMilliseconds_ != 0
            && nowMilliseconds >= hostLeaseRefreshMilliseconds_) {
            actions.push_back({
                RuntimeInternetActionKind::SendCoordinator,
                {},
                pendingCoordinatorPacket_
            });
            hostLeaseRefreshMilliseconds_ = nowMilliseconds
                + std::max<std::uint32_t>(1000U, config_.coordinatorTimeoutMilliseconds / 2U);
        }
        if (sealRequested_ && !roomSealed_
            && nowMilliseconds >= nextSealRetryMilliseconds_) {
            actions.push_back({
                RuntimeInternetActionKind::SendCoordinator,
                {},
                sealRoomPacket_
            });
            ++sealRetryCount_;
            if (sealRetryCount_ >= config_.maximumRetries
                || nowMilliseconds - sealStartedMilliseconds_
                    >= config_.coordinatorTimeoutMilliseconds) {
                // Losing a seal acknowledgement must not stop the host's room
                // lease carrier. Retry at a bounded, slower cadence until the
                // authoritative idempotent acknowledgement arrives.
                sealRetryCount_ = 0U;
                sealStartedMilliseconds_ = nowMilliseconds;
                nextSealRetryMilliseconds_ = nowMilliseconds
                    + config_.coordinatorTimeoutMilliseconds;
            } else {
                nextSealRetryMilliseconds_ = nowMilliseconds
                    + config_.retryMilliseconds;
            }
        }
        break;
    case RuntimeInternetConnectionState::Punching:
        if (timedOut(config_.punchTimeoutMilliseconds)) {
            // Multiplayer uses a reliable, ordered TCP byte stream. A failed
            // UDP reachability probe therefore falls directly back to TCP
            // relay instead of selecting an unusable UDP game route.
            requestRelay(RuntimeInternetRelayTransport::Tcp, nowMilliseconds);
            actions.insert(actions.end(), queuedActions_.begin(), queuedActions_.end());
            queuedActions_.clear();
        } else if (nowMilliseconds >= nextRetryMilliseconds_) {
            RuntimeInternetControlMessage probe = baseMessage(
                config_,
                RuntimeInternetControlType::PunchProbe,
                remotePeerId_
            );
            probe.transactionId = connectionId_;
            probe.body = RuntimeInternetPunchBody{connectionId_, connectionToken_};
            RuntimeNetworkPacket packet;
            std::string ignored;
            if (encodeRuntimeInternetControlMessage(probe, packet, ignored)) {
                for (const auto& candidate : remoteCandidates_) {
                    actions.push_back({
                        RuntimeInternetActionKind::SendPeerUdp,
                        candidate.endpoint,
                        packet
                    });
                }
            }
            nextRetryMilliseconds_ = nowMilliseconds + config_.retryMilliseconds;
        }
        break;
    case RuntimeInternetConnectionState::RequestingRelayUdp:
    case RuntimeInternetConnectionState::RequestingRelayTcp:
        if (timedOut(config_.coordinatorTimeoutMilliseconds)) {
            if (state_ == RuntimeInternetConnectionState::RequestingRelayUdp) {
                requestRelay(RuntimeInternetRelayTransport::Tcp, nowMilliseconds);
                actions.insert(actions.end(), queuedActions_.begin(), queuedActions_.end());
                queuedActions_.clear();
            } else {
                fail("TCP relay lease request timed out");
            }
        } else {
            retryCoordinator();
        }
        break;
    case RuntimeInternetConnectionState::BindingRelayUdp:
    case RuntimeInternetConnectionState::BindingRelayTcp:
        if (timedOut(config_.relayTimeoutMilliseconds)) {
            if (state_ == RuntimeInternetConnectionState::BindingRelayUdp) {
                requestRelay(RuntimeInternetRelayTransport::Tcp, nowMilliseconds);
                actions.insert(actions.end(), queuedActions_.begin(), queuedActions_.end());
                queuedActions_.clear();
            } else {
                fail("TCP relay binding timed out");
            }
        } else if (nowMilliseconds >= nextRetryMilliseconds_) {
            actions.push_back({
                state_ == RuntimeInternetConnectionState::BindingRelayUdp
                    ? RuntimeInternetActionKind::OpenRelayUdp
                    : RuntimeInternetActionKind::OpenRelayTcp,
                relayEndpoint_,
                relayBindPacket_
            });
            nextRetryMilliseconds_ = nowMilliseconds + config_.retryMilliseconds;
        }
        break;
    default:
        break;
    }

    const bool liveState = state_ != RuntimeInternetConnectionState::Idle
        && state_ != RuntimeInternetConnectionState::RegisteringRoom
        && state_ != RuntimeInternetConnectionState::QueryingRooms
        && state_ != RuntimeInternetConnectionState::JoiningRoom
        && state_ != RuntimeInternetConnectionState::Failed
        && state_ != RuntimeInternetConnectionState::Closed;
    const bool ownsCoordinatorHeartbeat = liveState
        && (config_.role != RuntimeInternetRole::Host
            || state_ == RuntimeInternetConnectionState::WaitingForPeer);
    if (ownsCoordinatorHeartbeat) {
        if (awaitingHeartbeatAcknowledgement_) {
            if (nowMilliseconds - heartbeatStartedMilliseconds_
                    >= config_.coordinatorTimeoutMilliseconds
                || heartbeatRetryCount_ >= config_.maximumRetries) {
                actions.clear();
                fail("coordinator heartbeat acknowledgement timed out");
            } else if (nowMilliseconds
                >= nextHeartbeatRetryMilliseconds_) {
                actions.push_back({
                    RuntimeInternetActionKind::SendCoordinator,
                    {},
                    coordinatorHeartbeatPacket_
                });
                ++heartbeatRetryCount_;
                nextHeartbeatRetryMilliseconds_ = nowMilliseconds
                    + config_.retryMilliseconds;
            }
        } else if (nowMilliseconds >= nextHeartbeatMilliseconds_) {
            RuntimeInternetControlMessage heartbeat = baseMessage(
                config_,
                RuntimeInternetControlType::CoordinatorHeartbeat
            );
            awaitingHeartbeatSequence_ = nextHeartbeatSequence_;
            ++nextHeartbeatSequence_;
            if (nextHeartbeatSequence_ == 0U) {
                nextHeartbeatSequence_ = 1U;
            }
            heartbeat.body = RuntimeInternetCoordinatorHeartbeatBody{
                awaitingHeartbeatSequence_
            };
            std::string encodeError;
            if (!encodeRuntimeInternetControlMessage(
                    heartbeat,
                    coordinatorHeartbeatPacket_,
                    encodeError
                )) {
                actions.clear();
                fail("coordinator heartbeat could not be encoded");
            } else {
                actions.push_back({
                    RuntimeInternetActionKind::SendCoordinator,
                    {},
                    coordinatorHeartbeatPacket_
                });
                awaitingHeartbeatAcknowledgement_ = true;
                heartbeatStartedMilliseconds_ = nowMilliseconds;
                nextHeartbeatRetryMilliseconds_ = nowMilliseconds
                    + config_.retryMilliseconds;
                heartbeatRetryCount_ = 0U;
            }
        }
    }
    return actions;
}

bool RuntimeInternetConnection::acceptCandidates(
    const RuntimeInternetControlMessage& message,
    const RuntimeInternetPeerCandidatesBody& body,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    const bool localIsHost = config_.role == RuntimeInternetRole::Host;
    if (message.transactionId != config_.transactionId
        || body.connectionId == 0 || body.remotePeerId == config_.localPeerId
        || !validCandidates(body.candidates) || !validToken(body.connectionToken)
        || (localIsHost && body.remoteRole == RuntimeInternetRole::Host)
        || (!localIsHost && body.remoteRole != RuntimeInternetRole::Host)) {
        errorMessage = "candidate identity or role does not match connection";
        return false;
    }
    connectionId_ = body.connectionId;
    remotePeerId_ = body.remotePeerId;
    remoteRole_ = body.remoteRole;
    remoteCandidates_ = body.candidates;
    connectionToken_ = body.connectionToken;
    tcpRoute_.reset();
    state_ = RuntimeInternetConnectionState::Punching;
    stateStartedMilliseconds_ = nowMilliseconds;
    nextRetryMilliseconds_ = nowMilliseconds;
    retryCount_ = 0;
    errorMessage.clear();
    return true;
}

bool RuntimeInternetConnection::receiveCoordinator(
    const RuntimeNetworkPacket& packet,
    const std::string& authenticatedServiceId,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    RuntimeInternetControlMessage message;
    if (!decodeRuntimeInternetControlMessage(packet, message, errorMessage)) return false;
    if (authenticatedServiceId != config_.serviceId
        || message.serviceId != config_.serviceId
        || message.senderPeerId != config_.serviceId
        || message.targetPeerId != config_.localPeerId
        || (message.type != RuntimeInternetControlType::RoomListResult
            && message.roomId != config_.roomId)) {
        errorMessage = "coordinator identity, room, or target mismatch";
        return false;
    }
    if (message.type
        == RuntimeInternetControlType::CoordinatorHeartbeatAck) {
        const auto* body = bodyAs<
            RuntimeInternetCoordinatorHeartbeatAckBody
        >(
            message,
            RuntimeInternetControlType::CoordinatorHeartbeatAck
        );
        if (!body || message.transactionId != config_.transactionId
            || body->sequence == 0U) {
            errorMessage = "unexpected coordinator heartbeat acknowledgement";
            return false;
        }
        if (body->sequence == acknowledgedHeartbeatSequence_) {
            errorMessage.clear();
            return true;
        }
        if (!awaitingHeartbeatAcknowledgement_
            || body->sequence != awaitingHeartbeatSequence_) {
            errorMessage = "stale coordinator heartbeat acknowledgement";
            return false;
        }
        awaitingHeartbeatAcknowledgement_ = false;
        acknowledgedHeartbeatSequence_ = body->sequence;
        awaitingHeartbeatSequence_ = 0U;
        coordinatorHeartbeatPacket_ = {};
        heartbeatRetryCount_ = 0U;
        nextHeartbeatMilliseconds_ = nowMilliseconds
            + config_.coordinatorHeartbeatMilliseconds;
        errorMessage.clear();
        return true;
    }
    const auto priorResponse = [&packet, &errorMessage](
        const std::optional<RuntimeNetworkPacket>& accepted,
        std::string_view responseName
    ) {
        if (!accepted.has_value()) return 0;
        if (sameNetworkPacket(*accepted, packet)) return 1;
        errorMessage = std::string(responseName)
            + " replay changed previously accepted content";
        return -1;
    };
    int duplicate = 0;
    switch (message.type) {
    case RuntimeInternetControlType::RegisterAccepted:
        duplicate = priorResponse(
            acceptedRegistrationResponse_,
            "registration response"
        );
        // An exact lease acknowledgement refreshes the carrier's next
        // heartbeat below; only changed content is rejected here.
        if (duplicate < 0) return false;
        duplicate = 0;
        break;
    case RuntimeInternetControlType::RoomListResult:
        // A room list is a dynamic snapshot. A retried request can therefore
        // produce a different, equally valid response for the same
        // transaction. The first valid response deterministically wins; any
        // later response for that completed transaction is ignored without
        // replacing the caller-visible snapshot.
        if (acceptedRoomListResponse_.has_value()
            && message.transactionId == config_.transactionId) {
            duplicate = 1;
        }
        break;
    case RuntimeInternetControlType::PeerCandidates:
        duplicate = priorResponse(
            acceptedPeerCandidatesResponse_,
            "peer-candidates response"
        );
        break;
    case RuntimeInternetControlType::RelayLease: {
        const auto* lease = std::get_if<RuntimeInternetRelayLeaseBody>(
            &message.body
        );
        if (lease != nullptr) {
            duplicate = priorResponse(
                lease->transport == RuntimeInternetRelayTransport::Udp
                    ? acceptedRelayLeaseUdpResponse_
                    : acceptedRelayLeaseTcpResponse_,
                "relay-lease response"
            );
        }
        break;
    }
    case RuntimeInternetControlType::RoomSealed:
        duplicate = priorResponse(
            acceptedRoomSealedResponse_,
            "room-sealed response"
        );
        break;
    default:
        break;
    }
    if (duplicate > 0) {
        errorMessage.clear();
        return true;
    }
    if (duplicate < 0) return false;
    if (message.type == RuntimeInternetControlType::Error) {
        if (message.transactionId != config_.transactionId
            && message.transactionId != connectionId_) {
            errorMessage = "stale coordinator error";
            return false;
        }
        fail(std::get<RuntimeInternetErrorBody>(message.body).reason);
        errorMessage.clear();
        return true;
    }
    if (state_ == RuntimeInternetConnectionState::RegisteringRoom) {
        const auto* body = bodyAs<RuntimeInternetRegisterAcceptedBody>(
            message,
            RuntimeInternetControlType::RegisterAccepted
        );
        if (!body || message.transactionId != config_.transactionId) {
            errorMessage = "unexpected registration response";
            return false;
        }
        state_ = RuntimeInternetConnectionState::WaitingForPeer;
        acceptedRegistrationResponse_ = packet;
        hostLeaseRefreshMilliseconds_ = nowMilliseconds
            + std::max<std::uint32_t>(1000U, body->leaseMilliseconds / 2U);
        errorMessage.clear();
        return true;
    }
    if (state_ == RuntimeInternetConnectionState::WaitingForPeer
        && message.type == RuntimeInternetControlType::RoomSealed) {
        const auto* body = bodyAs<RuntimeInternetRoomSealedBody>(
            message,
            RuntimeInternetControlType::RoomSealed
        );
        if (!body || message.transactionId != config_.transactionId
            || !sealRequested_) {
            errorMessage = "unexpected room seal acknowledgement";
            return false;
        }
        roomSealed_ = true;
        acceptedRoomSealedResponse_ = packet;
        sealRoomPacket_ = {};
        errorMessage.clear();
        return true;
    }
    if (state_ == RuntimeInternetConnectionState::WaitingForPeer
        && message.type == RuntimeInternetControlType::RegisterAccepted) {
        const auto* body = bodyAs<RuntimeInternetRegisterAcceptedBody>(
            message,
            RuntimeInternetControlType::RegisterAccepted
        );
        if (!body || message.transactionId != config_.transactionId) {
            errorMessage = "unexpected registration lease acknowledgement";
            return false;
        }
        hostLeaseRefreshMilliseconds_ = nowMilliseconds
            + std::max<std::uint32_t>(1000U, body->leaseMilliseconds / 2U);
        if (!acceptedRegistrationResponse_.has_value()) {
            acceptedRegistrationResponse_ = packet;
        }
        errorMessage.clear();
        return true;
    }
    if (state_ == RuntimeInternetConnectionState::QueryingRooms) {
        const auto* body = bodyAs<RuntimeInternetRoomListResultBody>(
            message,
            RuntimeInternetControlType::RoomListResult
        );
        if (!body || message.transactionId != config_.transactionId) {
            errorMessage = "unexpected room-list response";
            return false;
        }
        rooms_ = body->rooms;
        acceptedRoomListResponse_ = packet;
        state_ = RuntimeInternetConnectionState::RoomListReady;
        errorMessage.clear();
        return true;
    }
    if (state_ == RuntimeInternetConnectionState::WaitingForPeer
        || state_ == RuntimeInternetConnectionState::JoiningRoom) {
        const auto* body = bodyAs<RuntimeInternetPeerCandidatesBody>(
            message,
            RuntimeInternetControlType::PeerCandidates
        );
        if (!body) {
            errorMessage = "unexpected coordinator candidate response";
            return false;
        }
        if (!acceptCandidates(
                message,
                *body,
                nowMilliseconds,
                errorMessage
            )) {
            return false;
        }
        acceptedPeerCandidatesResponse_ = packet;
        return true;
    }
    if (state_ == RuntimeInternetConnectionState::RequestingRelayUdp
        || state_ == RuntimeInternetConnectionState::RequestingRelayTcp) {
        const auto* body = bodyAs<RuntimeInternetRelayLeaseBody>(
            message,
            RuntimeInternetControlType::RelayLease
        );
        if (!body || message.transactionId != connectionId_
            || body->connectionId != connectionId_
            || body->transport != requestedRelay_) {
            errorMessage = "unexpected or mismatched relay lease";
            return false;
        }
        relayEndpoint_ = body->endpoint;
        relayTicket_ = body->ticket;
        if (body->transport == RuntimeInternetRelayTransport::Tcp) {
            tcpRoute_ = RuntimeInternetTcpRouteDescriptor{
                RuntimeInternetTcpRouteKind::Relay,
                connectionId_,
                config_.serviceId,
                config_.roomId,
                config_.localPeerId,
                config_.role,
                remotePeerId_,
                remoteRole_,
                relayEndpoint_
            };
        }
        RuntimeInternetControlMessage bind = baseMessage(
            config_,
            RuntimeInternetControlType::RelayBind,
            config_.serviceId
        );
        bind.transactionId = connectionId_;
        bind.body = RuntimeInternetRelayBindBody{
            connectionId_,
            body->transport,
            relayTicket_
        };
        if (!encodeRuntimeInternetControlMessage(bind, relayBindPacket_, errorMessage)) {
            fail("coordinator supplied an unusable relay lease");
            return false;
        }
        state_ = body->transport == RuntimeInternetRelayTransport::Udp
            ? RuntimeInternetConnectionState::BindingRelayUdp
            : RuntimeInternetConnectionState::BindingRelayTcp;
        stateStartedMilliseconds_ = nowMilliseconds;
        nextRetryMilliseconds_ = nowMilliseconds + config_.retryMilliseconds;
        queuedActions_.push_back({
            body->transport == RuntimeInternetRelayTransport::Udp
                ? RuntimeInternetActionKind::OpenRelayUdp
                : RuntimeInternetActionKind::OpenRelayTcp,
            relayEndpoint_,
            relayBindPacket_
        });
        if (body->transport == RuntimeInternetRelayTransport::Udp) {
            acceptedRelayLeaseUdpResponse_ = packet;
        } else {
            acceptedRelayLeaseTcpResponse_ = packet;
        }
        errorMessage.clear();
        return true;
    }
    errorMessage = "coordinator message is invalid in current state";
    return false;
}

bool RuntimeInternetConnection::receivePeerUdp(
    const RuntimeNetworkPacket& packet,
    const RuntimeInternetEndpoint& source,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    if (state_ != RuntimeInternetConnectionState::Punching) {
        errorMessage = "peer datagram is invalid in current state";
        return false;
    }
    RuntimeInternetControlMessage message;
    if (!decodeRuntimeInternetControlMessage(packet, message, errorMessage)) return false;
    const auto* body = std::get_if<RuntimeInternetPunchBody>(&message.body);
    if (!body || (message.type != RuntimeInternetControlType::PunchProbe
            && message.type != RuntimeInternetControlType::PunchAck)
        || message.transactionId != connectionId_
        || body->connectionId != connectionId_
        || body->connectionToken != connectionToken_
        || message.serviceId != config_.serviceId
        || message.roomId != config_.roomId
        || message.senderPeerId != remotePeerId_
        || message.targetPeerId != config_.localPeerId
        || !endpointInCandidates(source, remoteCandidates_)) {
        errorMessage = "unauthenticated or mismatched NAT punch datagram";
        return false;
    }
    if (message.type == RuntimeInternetControlType::PunchProbe) {
        RuntimeInternetControlMessage ack = baseMessage(
            config_,
            RuntimeInternetControlType::PunchAck,
            remotePeerId_
        );
        ack.transactionId = connectionId_;
        ack.body = RuntimeInternetPunchBody{connectionId_, connectionToken_};
        RuntimeNetworkPacket ackPacket;
        if (!encodeRuntimeInternetControlMessage(ack, ackPacket, errorMessage)) return false;
        queuedActions_.push_back({
            RuntimeInternetActionKind::SendPeerUdp,
            source,
            ackPacket
        });
    }
    // This proves only a UDP candidate. It cannot open or authenticate the
    // TCP stream used by RuntimeMultiplayerLanSession. Preserve the ACK action
    // above, then immediately negotiate the explicit TCP relay route.
    state_ = RuntimeInternetConnectionState::UdpCandidateVerified;
    requestRelay(RuntimeInternetRelayTransport::Tcp, nowMilliseconds);
    errorMessage.clear();
    return true;
}

void RuntimeInternetConnection::requestRelay(
    RuntimeInternetRelayTransport transport,
    std::uint64_t nowMilliseconds
) {
    requestedRelay_ = transport;
    RuntimeInternetControlMessage request = baseMessage(
        config_,
        RuntimeInternetControlType::RelayRequest
    );
    request.transactionId = connectionId_;
    request.body = RuntimeInternetRelayRequestBody{
        connectionId_,
        transport,
        connectionToken_
    };
    std::string error;
    if (!encodeRuntimeInternetControlMessage(request, pendingCoordinatorPacket_, error)) {
        fail("could not encode relay request");
        return;
    }
    state_ = transport == RuntimeInternetRelayTransport::Udp
        ? RuntimeInternetConnectionState::RequestingRelayUdp
        : RuntimeInternetConnectionState::RequestingRelayTcp;
    stateStartedMilliseconds_ = nowMilliseconds;
    nextRetryMilliseconds_ = nowMilliseconds + config_.retryMilliseconds;
    retryCount_ = 0;
    queuedActions_.push_back({
        RuntimeInternetActionKind::SendCoordinator,
        {},
        pendingCoordinatorPacket_
    });
}

bool RuntimeInternetConnection::receiveRelay(
    const RuntimeNetworkPacket& packet,
    const RuntimeInternetEndpoint& source,
    const std::string& authenticatedRelayServiceId,
    std::uint64_t,
    std::string& errorMessage
) {
    if (source != relayEndpoint_
        || authenticatedRelayServiceId != config_.serviceId) {
        errorMessage = "relay source or service mismatch";
        return false;
    }
    RuntimeInternetControlMessage message;
    if (!decodeRuntimeInternetControlMessage(packet, message, errorMessage)) return false;
    const auto* body = bodyAs<RuntimeInternetRelayBoundBody>(
        message,
        RuntimeInternetControlType::RelayBound
    );
    if (body != nullptr && validRelay(body->transport)) {
        const std::optional<RuntimeNetworkPacket>& accepted =
            body->transport == RuntimeInternetRelayTransport::Udp
            ? acceptedRelayBoundUdpResponse_
            : acceptedRelayBoundTcpResponse_;
        if (accepted.has_value()) {
            if (sameNetworkPacket(*accepted, packet)) {
                errorMessage.clear();
                return true;
            }
            errorMessage =
                "relay-bound replay changed previously accepted content";
            return false;
        }
    }
    const bool bindingUdp = state_
        == RuntimeInternetConnectionState::BindingRelayUdp;
    const bool bindingTcp = state_
        == RuntimeInternetConnectionState::BindingRelayTcp;
    if (!bindingUdp && !bindingTcp) {
        errorMessage = "relay binding acknowledgement is invalid in current state";
        return false;
    }
    const RuntimeInternetRelayTransport expected = bindingUdp
        ? RuntimeInternetRelayTransport::Udp
        : RuntimeInternetRelayTransport::Tcp;
    if (!body || message.transactionId != connectionId_
        || body->connectionId != connectionId_ || body->transport != expected
        || message.serviceId != config_.serviceId
        || message.roomId != config_.roomId
        || message.senderPeerId != config_.serviceId
        || message.targetPeerId != config_.localPeerId) {
        errorMessage = "invalid relay binding acknowledgement";
        return false;
    }
    state_ = bindingUdp
        ? RuntimeInternetConnectionState::ConnectedRelayUdp
        : RuntimeInternetConnectionState::TcpTunnelBound;
    if (bindingUdp) {
        acceptedRelayBoundUdpResponse_ = packet;
    } else {
        acceptedRelayBoundTcpResponse_ = packet;
    }
    errorMessage.clear();
    return true;
}

bool RuntimeInternetConnection::reportRelayOpenFailure(
    RuntimeInternetRelayTransport transport,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    if (transport == RuntimeInternetRelayTransport::Udp
        && state_ == RuntimeInternetConnectionState::BindingRelayUdp) {
        requestRelay(RuntimeInternetRelayTransport::Tcp, nowMilliseconds);
        errorMessage.clear();
        return true;
    }
    if (transport == RuntimeInternetRelayTransport::Tcp
        && state_ == RuntimeInternetConnectionState::BindingRelayTcp) {
        fail("TCP relay connection failed");
        errorMessage.clear();
        return true;
    }
    errorMessage = "relay failure does not match active relay attempt";
    return false;
}

bool RuntimeInternetConnection::reportGameplaySessionJoined(
    std::uint64_t connectionId,
    std::string& errorMessage
) {
    if (state_ == RuntimeInternetConnectionState::GameplaySessionConnected
        && tcpRoute_.has_value()
        && connectionId != 0U
        && tcpRoute_->connectionId == connectionId) {
        errorMessage.clear();
        return true;
    }
    if (state_ != RuntimeInternetConnectionState::TcpTunnelBound
        || !tcpRoute_.has_value()
        || connectionId == 0
        || tcpRoute_->connectionId != connectionId) {
        errorMessage =
            "gameplay admission does not match a bound TCP tunnel";
        return false;
    }
    state_ = RuntimeInternetConnectionState::GameplaySessionConnected;
    errorMessage.clear();
    return true;
}

bool RuntimeInternetConnection::makeResumeRouteRequest(
    RuntimeNetworkPacket& packet, std::string& errorMessage) const {
    if (connectionId_ == 0 || !validToken(connectionToken_)
        || config_.role == RuntimeInternetRole::Spectator
        || state_ == RuntimeInternetConnectionState::Idle
        || state_ == RuntimeInternetConnectionState::Closed) {
        errorMessage = "Internet resume requires an admitted combatant route";
        return false;
    }
    auto message = baseMessage(config_, RuntimeInternetControlType::ResumeRoute);
    message.transactionId = connectionId_;
    message.targetPeerId = config_.serviceId;
    message.body = RuntimeInternetResumeRouteBody{connectionId_, connectionToken_};
    return encodeRuntimeInternetControlMessage(message, packet, errorMessage);
}

void RuntimeInternetConnection::coordinatorReauthenticated(std::uint64_t nowMilliseconds) {
    if (state_ == RuntimeInternetConnectionState::Failed && config_.role == RuntimeInternetRole::Host && roomSealed_) {
        state_ = RuntimeInternetConnectionState::WaitingForPeer;
        failureReason_.clear();
    }
    // An outstanding acknowledgement may already have reached the old TLS
    // socket. Treat that sequence as retired and start a fresh heartbeat.
    if (awaitingHeartbeatAcknowledgement_)
        acknowledgedHeartbeatSequence_ = awaitingHeartbeatSequence_;
    awaitingHeartbeatAcknowledgement_ = false;
    awaitingHeartbeatSequence_ = 0;
    coordinatorHeartbeatPacket_ = {};
    heartbeatRetryCount_ = 0;
    nextHeartbeatMilliseconds_ = nowMilliseconds;
    hostLeaseRefreshMilliseconds_ = nowMilliseconds;
}

void RuntimeInternetConnection::fail(std::string reason) {
    state_ = RuntimeInternetConnectionState::Failed;
    failureReason_ = std::move(reason);
    queuedActions_.clear();
}

void RuntimeInternetConnection::close() {
    state_ = RuntimeInternetConnectionState::Closed;
    queuedActions_.clear();
    pendingCoordinatorPacket_ = {};
    relayBindPacket_ = {};
    sealRoomPacket_ = {};
    coordinatorHeartbeatPacket_ = {};
    awaitingHeartbeatAcknowledgement_ = false;
    awaitingHeartbeatSequence_ = 0U;
    acceptedRegistrationResponse_.reset();
    acceptedRoomListResponse_.reset();
    acceptedPeerCandidatesResponse_.reset();
    acceptedRelayLeaseUdpResponse_.reset();
    acceptedRelayLeaseTcpResponse_.reset();
    acceptedRoomSealedResponse_.reset();
    acceptedRelayBoundUdpResponse_.reset();
    acceptedRelayBoundTcpResponse_.reset();
    sealRequested_ = false;
    roomSealed_ = false;
    tcpRoute_.reset();
}

RuntimeInternetConnectionState RuntimeInternetConnection::state() const {
    return state_;
}

RuntimeInternetRole RuntimeInternetConnection::role() const {
    return config_.role;
}

const std::string& RuntimeInternetConnection::remotePeerId() const {
    return remotePeerId_;
}

std::optional<RuntimeInternetTcpRouteDescriptor>
RuntimeInternetConnection::tcpRoute() const {
    return tcpRoute_;
}

bool RuntimeInternetConnection::tcpTunnelBound() const {
    return state_ == RuntimeInternetConnectionState::TcpTunnelBound
        || state_ == RuntimeInternetConnectionState::GameplaySessionConnected;
}

bool RuntimeInternetConnection::gameplaySessionConnected() const {
    return state_ == RuntimeInternetConnectionState::GameplaySessionConnected;
}

bool RuntimeInternetConnection::roomSealRequested() const {
    return sealRequested_;
}

bool RuntimeInternetConnection::roomSealed() const {
    return roomSealed_;
}

const std::vector<RuntimeInternetRoomRecord>& RuntimeInternetConnection::rooms() const {
    return rooms_;
}

const std::string& RuntimeInternetConnection::failureReason() const {
    return failureReason_;
}
