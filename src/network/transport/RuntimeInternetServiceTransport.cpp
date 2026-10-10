#include "network/transport/RuntimeInternetServiceTransport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <thread>
#include <type_traits>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <random>
#endif

namespace {

constexpr std::uint32_t serviceWireMagic = 0x43535731U; // CSW1
constexpr std::size_t wireHeaderBytes = 8U;
constexpr std::size_t wireLengthBytes = 4U;
constexpr std::size_t wireReadChunkBytes = 64U * 1024U;

enum class WireKind : std::uint8_t {
    AuthenticationRequest = 1,
    AuthenticationConfirmation = 2,
    AuthenticationRejection = 3,
    CoordinatorPacket = 4
};

void appendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void appendU64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>(
            (value >> static_cast<unsigned int>(shift)) & 0xffU
        ));
    }
}

bool appendBlob(
    std::vector<std::uint8_t>& bytes,
    std::span<const std::uint8_t> value,
    std::size_t maximum
) {
    if (value.size() > maximum
        || value.size() > (std::numeric_limits<std::uint16_t>::max)()) {
        return false;
    }
    appendU16(bytes, static_cast<std::uint16_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
    return true;
}

bool appendString(
    std::vector<std::uint8_t>& bytes,
    const std::string& value,
    std::size_t maximum
) {
    return appendBlob(
        bytes,
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(value.data()),
            value.size()
        ),
        maximum
    );
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    bool u8(std::uint8_t& value) {
        if (offset_ >= bytes_.size()) return false;
        value = bytes_[offset_++];
        return true;
    }

    bool u16(std::uint16_t& value) {
        if (offset_ + 2U > bytes_.size()) return false;
        value = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes_[offset_]) << 8U)
                | bytes_[offset_ + 1U]
        );
        offset_ += 2U;
        return true;
    }

    bool u32(std::uint32_t& value) {
        if (offset_ + 4U > bytes_.size()) return false;
        value = (static_cast<std::uint32_t>(bytes_[offset_]) << 24U)
            | (static_cast<std::uint32_t>(bytes_[offset_ + 1U]) << 16U)
            | (static_cast<std::uint32_t>(bytes_[offset_ + 2U]) << 8U)
            | static_cast<std::uint32_t>(bytes_[offset_ + 3U]);
        offset_ += 4U;
        return true;
    }

    bool u64(std::uint64_t& value) {
        if (offset_ + 8U > bytes_.size()) return false;
        value = 0;
        for (std::size_t index = 0; index < 8U; ++index) {
            value = (value << 8U) | bytes_[offset_ + index];
        }
        offset_ += 8U;
        return true;
    }

    bool fixed(std::span<std::uint8_t> value) {
        if (offset_ + value.size() > bytes_.size()) return false;
        std::copy_n(
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
            value.size(),
            value.begin()
        );
        offset_ += value.size();
        return true;
    }

    bool blob(std::vector<std::uint8_t>& value, std::size_t maximum) {
        std::uint16_t size = 0;
        if (!u16(size) || size > maximum
            || offset_ + size > bytes_.size()) {
            return false;
        }
        value.assign(
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + size)
        );
        offset_ += size;
        return true;
    }

    bool string(std::string& value, std::size_t maximum) {
        std::vector<std::uint8_t> encoded;
        if (!blob(encoded, maximum)) return false;
        if (encoded.empty()) {
            value.clear();
            return true;
        }
        value.assign(
            reinterpret_cast<const char*>(encoded.data()),
            encoded.size()
        );
        return true;
    }

    bool finished() const noexcept { return offset_ == bytes_.size(); }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

bool validChannel(RuntimeInternetServiceWireChannel channel) noexcept {
    return channel == RuntimeInternetServiceWireChannel::Coordinator
        || channel == RuntimeInternetServiceWireChannel::RelayTcp;
}

bool validRole(RuntimeInternetRole role) noexcept {
    return role == RuntimeInternetRole::Host
        || role == RuntimeInternetRole::Client
        || role == RuntimeInternetRole::Spectator;
}

bool validPacket(
    const RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    if (packet.type == 0
        || packet.payload.size() > runtimeLanMaximumPacketBytes) {
        errorMessage = "Internet service packet is invalid or too large";
        return false;
    }
    return true;
}

bool validRoute(
    const RuntimeInternetTcpRouteDescriptor& route,
    std::string& errorMessage
) {
    if (route.kind != RuntimeInternetTcpRouteKind::Relay
        || route.connectionId == 0 || route.serviceId.empty()
        || route.serviceId.size() > runtimeInternetMaximumIdentityBytes
        || route.roomId.empty()
        || route.roomId.size() > runtimeInternetMaximumRoomIdBytes
        || route.localPeerId.empty() || route.remotePeerId.empty()
        || route.localPeerId.size() > runtimeInternetMaximumIdentityBytes
        || route.remotePeerId.size() > runtimeInternetMaximumIdentityBytes
        || route.localPeerId == route.remotePeerId
        || !validRole(route.localRole) || !validRole(route.remoteRole)
        || route.endpoint.address.empty()
        || route.endpoint.address.size()
            > runtimeInternetMaximumEndpointAddressBytes
        || route.endpoint.port == 0) {
        errorMessage = "Internet service relay route is incomplete";
        return false;
    }
    const bool hostSide = route.localRole == RuntimeInternetRole::Host
        && (route.remoteRole == RuntimeInternetRole::Client
            || route.remoteRole == RuntimeInternetRole::Spectator);
    const bool remoteSide =
        (route.localRole == RuntimeInternetRole::Client
            || route.localRole == RuntimeInternetRole::Spectator)
        && route.remoteRole == RuntimeInternetRole::Host;
    if (!hostSide && !remoteSide) {
        errorMessage = "Internet service relay route roles are incompatible";
        return false;
    }
    return true;
}

bool validRelayBindForRoute(
    const RuntimeNetworkPacket& packet,
    const RuntimeInternetTcpRouteDescriptor& route,
    std::string& errorMessage
) {
    RuntimeInternetControlMessage control;
    std::string ignored;
    if (!decodeRuntimeInternetControlMessage(packet, control, ignored)
        || control.type != RuntimeInternetControlType::RelayBind
        || control.transactionId != route.connectionId
        || control.serviceId != route.serviceId
        || control.roomId != route.roomId
        || control.senderPeerId != route.localPeerId
        || control.targetPeerId != route.serviceId) {
        errorMessage = "relay bind packet does not match its route";
        return false;
    }
    const auto* bind = std::get_if<RuntimeInternetRelayBindBody>(
        &control.body
    );
    if (bind == nullptr || bind->connectionId != route.connectionId
        || bind->transport != RuntimeInternetRelayTransport::Tcp) {
        errorMessage = "relay bind packet is not a TCP bind for its route";
        return false;
    }
    return true;
}

bool nonceNonzero(const RuntimeInternetServiceNonce& nonce) noexcept {
    std::uint8_t combined = 0;
    for (const std::uint8_t byte : nonce) {
        combined = static_cast<std::uint8_t>(combined | byte);
    }
    return combined != 0;
}

bool validIdentityFields(
    const std::string& serviceId,
    const std::string& peerId,
    std::string& errorMessage
) {
    if (serviceId.empty() || peerId.empty()
        || serviceId.size() > runtimeInternetMaximumIdentityBytes
        || peerId.size() > runtimeInternetMaximumIdentityBytes) {
        errorMessage = "Internet service identity is incomplete";
        return false;
    }
    return true;
}

void appendPacket(
    std::vector<std::uint8_t>& bytes,
    const RuntimeNetworkPacket& packet
) {
    appendU16(bytes, packet.type);
    appendU32(bytes, static_cast<std::uint32_t>(packet.payload.size()));
    bytes.insert(bytes.end(), packet.payload.begin(), packet.payload.end());
}

bool readPacket(Reader& reader, RuntimeNetworkPacket& packet) {
    std::uint16_t type = 0;
    std::uint32_t size = 0;
    if (!reader.u16(type) || !reader.u32(size)
        || type == 0 || size > runtimeLanMaximumPacketBytes) {
        return false;
    }
    std::vector<std::uint8_t> payload;
    payload.resize(size);
    if (!reader.fixed(payload)) return false;
    packet.type = type;
    packet.payload = std::move(payload);
    return true;
}

void appendRoute(
    std::vector<std::uint8_t>& bytes,
    const RuntimeInternetTcpRouteDescriptor& route
) {
    bytes.push_back(static_cast<std::uint8_t>(route.kind));
    appendU64(bytes, route.connectionId);
    (void)appendString(
        bytes,
        route.serviceId,
        runtimeInternetMaximumIdentityBytes
    );
    (void)appendString(
        bytes,
        route.roomId,
        runtimeInternetMaximumRoomIdBytes
    );
    (void)appendString(
        bytes,
        route.localPeerId,
        runtimeInternetMaximumIdentityBytes
    );
    bytes.push_back(static_cast<std::uint8_t>(route.localRole));
    (void)appendString(
        bytes,
        route.remotePeerId,
        runtimeInternetMaximumIdentityBytes
    );
    bytes.push_back(static_cast<std::uint8_t>(route.remoteRole));
    (void)appendString(
        bytes,
        route.endpoint.address,
        runtimeInternetMaximumEndpointAddressBytes
    );
    appendU16(bytes, route.endpoint.port);
}

bool readRoute(Reader& reader, RuntimeInternetTcpRouteDescriptor& route) {
    std::uint8_t kind = 0;
    std::uint8_t localRole = 0;
    std::uint8_t remoteRole = 0;
    RuntimeInternetTcpRouteDescriptor parsed;
    if (!reader.u8(kind)
        || kind != static_cast<std::uint8_t>(
            RuntimeInternetTcpRouteKind::Relay
        )
        || !reader.u64(parsed.connectionId)
        || !reader.string(
            parsed.serviceId,
            runtimeInternetMaximumIdentityBytes
        )
        || !reader.string(parsed.roomId, runtimeInternetMaximumRoomIdBytes)
        || !reader.string(
            parsed.localPeerId,
            runtimeInternetMaximumIdentityBytes
        )
        || !reader.u8(localRole)
        || !reader.string(
            parsed.remotePeerId,
            runtimeInternetMaximumIdentityBytes
        )
        || !reader.u8(remoteRole)
        || !reader.string(
            parsed.endpoint.address,
            runtimeInternetMaximumEndpointAddressBytes
        )
        || !reader.u16(parsed.endpoint.port)) {
        return false;
    }
    parsed.kind = RuntimeInternetTcpRouteKind::Relay;
    parsed.localRole = static_cast<RuntimeInternetRole>(localRole);
    parsed.remoteRole = static_cast<RuntimeInternetRole>(remoteRole);
    std::string ignored;
    if (!validRoute(parsed, ignored)) return false;
    route = std::move(parsed);
    return true;
}

bool encodeWireBody(
    const RuntimeInternetServiceWireMessage& message,
    std::vector<std::uint8_t>& body,
    std::string& errorMessage
) {
    body.clear();
    body.reserve(256U);
    appendU32(body, serviceWireMagic);
    appendU16(body, runtimeInternetServiceWireVersion);
    body.push_back(0);
    body.push_back(0);

    bool valid = std::visit(
        [&](const auto& value) -> bool {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<
                Value,
                RuntimeInternetServiceAuthenticationRequest
            >) {
                body[6] = static_cast<std::uint8_t>(
                    WireKind::AuthenticationRequest
                );
                if (!validChannel(value.channel)
                    || !nonceNonzero(value.nonce)
                    || !validIdentityFields(
                        value.serviceId,
                        value.peerId,
                        errorMessage
                    )
                    || value.credential.size()
                        < runtimeInternetServiceMinimumCredentialBytes
                    || value.credential.size()
                        > runtimeInternetServiceMaximumCredentialBytes) {
                    if (errorMessage.empty()) {
                        errorMessage =
                            "Internet service authentication request is invalid";
                    }
                    return false;
                }
                body.push_back(static_cast<std::uint8_t>(value.channel));
                body.insert(body.end(), value.nonce.begin(), value.nonce.end());
                if (!appendString(
                        body,
                        value.serviceId,
                        runtimeInternetMaximumIdentityBytes
                    )
                    || !appendString(
                        body,
                        value.peerId,
                        runtimeInternetMaximumIdentityBytes
                    )
                    || !appendBlob(
                        body,
                        value.credential,
                        runtimeInternetServiceMaximumCredentialBytes
                    )) {
                    errorMessage =
                        "Internet service authentication request is too large";
                    return false;
                }
                if (value.channel
                    == RuntimeInternetServiceWireChannel::RelayTcp) {
                    if (!validRoute(value.route, errorMessage)
                        || !validPacket(value.relayBindPacket, errorMessage)) {
                        return false;
                    }
                    appendRoute(body, value.route);
                    appendPacket(body, value.relayBindPacket);
                }
                return true;
            } else if constexpr (std::is_same_v<
                Value,
                RuntimeInternetServiceAuthenticationConfirmation
            >) {
                body[6] = static_cast<std::uint8_t>(
                    WireKind::AuthenticationConfirmation
                );
                if (!validChannel(value.channel)
                    || !nonceNonzero(value.nonce)
                    || !validIdentityFields(
                        value.serviceId,
                        value.peerId,
                        errorMessage
                    )) {
                    if (errorMessage.empty()) {
                        errorMessage =
                            "Internet service authentication confirmation is invalid";
                    }
                    return false;
                }
                body.push_back(static_cast<std::uint8_t>(value.channel));
                body.insert(body.end(), value.nonce.begin(), value.nonce.end());
                if (!appendString(
                        body,
                        value.serviceId,
                        runtimeInternetMaximumIdentityBytes
                    )
                    || !appendString(
                        body,
                        value.peerId,
                        runtimeInternetMaximumIdentityBytes
                    )) {
                    errorMessage =
                        "Internet service authentication confirmation is too large";
                    return false;
                }
                if (value.channel
                    == RuntimeInternetServiceWireChannel::RelayTcp) {
                    if (!validRoute(value.route, errorMessage)) return false;
                    appendRoute(body, value.route);
                }
                return true;
            } else if constexpr (std::is_same_v<
                Value,
                RuntimeInternetServiceAuthenticationRejection
            >) {
                body[6] = static_cast<std::uint8_t>(
                    WireKind::AuthenticationRejection
                );
                if (!validChannel(value.channel)
                    || !nonceNonzero(value.nonce)) {
                    errorMessage =
                        "Internet service authentication rejection is invalid";
                    return false;
                }
                body.push_back(static_cast<std::uint8_t>(value.channel));
                body.insert(body.end(), value.nonce.begin(), value.nonce.end());
                return true;
            } else {
                body[6] = static_cast<std::uint8_t>(
                    WireKind::CoordinatorPacket
                );
                if (value.roomAdmissionProof.size()
                        > runtimeInternetCoordinatorMaximumAdmissionProofBytes
                    || !validPacket(value.packet, errorMessage)
                    || !appendBlob(
                        body,
                        value.roomAdmissionProof,
                        runtimeInternetCoordinatorMaximumAdmissionProofBytes
                    )) {
                    if (errorMessage.empty()) {
                        errorMessage = "Internet coordinator envelope is invalid";
                    }
                    return false;
                }
                appendPacket(body, value.packet);
                return true;
            }
        },
        message.body
    );
    if (!valid) {
        body.clear();
        return false;
    }
    if (body.size() < wireHeaderBytes
        || body.size() > runtimeInternetServiceMaximumWireFrameBytes) {
        errorMessage = "Internet service wire frame exceeds its size limit";
        body.clear();
        return false;
    }
    return true;
}

bool decodeWireBody(
    std::span<const std::uint8_t> body,
    RuntimeInternetServiceWireMessage& message,
    std::string& errorMessage
) {
    Reader reader(body);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint8_t kindValue = 0;
    std::uint8_t reserved = 0;
    if (!reader.u32(magic) || !reader.u16(version)
        || !reader.u8(kindValue) || !reader.u8(reserved)
        || magic != serviceWireMagic
        || version != runtimeInternetServiceWireVersion || reserved != 0) {
        errorMessage = "Internet service wire header is invalid";
        return false;
    }
    const WireKind kind = static_cast<WireKind>(kindValue);
    if (kind == WireKind::AuthenticationRequest) {
        RuntimeInternetServiceAuthenticationRequest parsed;
        std::uint8_t channel = 0;
        if (!reader.u8(channel)
            || !reader.fixed(parsed.nonce)
            || !reader.string(
                parsed.serviceId,
                runtimeInternetMaximumIdentityBytes
            )
            || !reader.string(
                parsed.peerId,
                runtimeInternetMaximumIdentityBytes
            )
            || !reader.blob(
                parsed.credential,
                runtimeInternetServiceMaximumCredentialBytes
            )) {
            errorMessage =
                "Internet service authentication request is malformed";
            return false;
        }
        parsed.channel = static_cast<RuntimeInternetServiceWireChannel>(
            channel
        );
        if (!validChannel(parsed.channel) || !nonceNonzero(parsed.nonce)
            || !validIdentityFields(
                parsed.serviceId,
                parsed.peerId,
                errorMessage
            )
            || parsed.credential.size()
                < runtimeInternetServiceMinimumCredentialBytes) {
            if (errorMessage.empty()) {
                errorMessage =
                    "Internet service authentication request is invalid";
            }
            return false;
        }
        if (parsed.channel == RuntimeInternetServiceWireChannel::RelayTcp
            && (!readRoute(reader, parsed.route)
                || !readPacket(reader, parsed.relayBindPacket))) {
            errorMessage = "Internet relay authentication route is malformed";
            return false;
        }
        if (!reader.finished()) {
            errorMessage =
                "Internet service authentication request has trailing bytes";
            return false;
        }
        message.body = std::move(parsed);
        return true;
    }
    if (kind == WireKind::AuthenticationConfirmation) {
        RuntimeInternetServiceAuthenticationConfirmation parsed;
        std::uint8_t channel = 0;
        if (!reader.u8(channel) || !reader.fixed(parsed.nonce)
            || !reader.string(
                parsed.serviceId,
                runtimeInternetMaximumIdentityBytes
            )
            || !reader.string(
                parsed.peerId,
                runtimeInternetMaximumIdentityBytes
            )) {
            errorMessage =
                "Internet service authentication confirmation is malformed";
            return false;
        }
        parsed.channel = static_cast<RuntimeInternetServiceWireChannel>(
            channel
        );
        if (!validChannel(parsed.channel) || !nonceNonzero(parsed.nonce)
            || !validIdentityFields(
                parsed.serviceId,
                parsed.peerId,
                errorMessage
            )) {
            if (errorMessage.empty()) {
                errorMessage =
                    "Internet service authentication confirmation is invalid";
            }
            return false;
        }
        if (parsed.channel == RuntimeInternetServiceWireChannel::RelayTcp
            && !readRoute(reader, parsed.route)) {
            errorMessage = "Internet relay confirmation route is malformed";
            return false;
        }
        if (!reader.finished()) {
            errorMessage =
                "Internet service authentication confirmation has trailing bytes";
            return false;
        }
        message.body = std::move(parsed);
        return true;
    }
    if (kind == WireKind::AuthenticationRejection) {
        RuntimeInternetServiceAuthenticationRejection parsed;
        std::uint8_t channel = 0;
        if (!reader.u8(channel) || !reader.fixed(parsed.nonce)
            || !reader.finished()) {
            errorMessage =
                "Internet service authentication rejection is malformed";
            return false;
        }
        parsed.channel = static_cast<RuntimeInternetServiceWireChannel>(
            channel
        );
        if (!validChannel(parsed.channel) || !nonceNonzero(parsed.nonce)) {
            errorMessage =
                "Internet service authentication rejection is invalid";
            return false;
        }
        message.body = parsed;
        return true;
    }
    if (kind == WireKind::CoordinatorPacket) {
        RuntimeInternetServiceCoordinatorPacket parsed;
        if (!reader.blob(
                parsed.roomAdmissionProof,
                runtimeInternetCoordinatorMaximumAdmissionProofBytes
            )
            || !readPacket(reader, parsed.packet) || !reader.finished()) {
            errorMessage = "Internet coordinator envelope is malformed";
            return false;
        }
        message.body = std::move(parsed);
        return true;
    }
    errorMessage = "Internet service wire message kind is unknown";
    return false;
}

void secureZero(std::vector<std::uint8_t>& bytes) noexcept {
    volatile std::uint8_t* data = bytes.data();
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        data[index] = 0;
    }
    bytes.clear();
}

bool fillNonce(RuntimeInternetServiceNonce& nonce) noexcept {
#ifdef _WIN32
    return BCryptGenRandom(
        nullptr,
        nonce.data(),
        static_cast<ULONG>(nonce.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    ) == 0 && nonceNonzero(nonce);
#else
    std::random_device random;
    for (std::uint8_t& byte : nonce) {
        byte = static_cast<std::uint8_t>(random());
    }
    return nonceNonzero(nonce);
#endif
}

std::uint64_t monotonicMilliseconds() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

std::uint64_t deadlineAfter(
    std::uint64_t now,
    std::uint32_t milliseconds
) noexcept {
    const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    return now > maximum - milliseconds ? maximum : now + milliseconds;
}

} // namespace

bool encodeRuntimeInternetServiceWireFrame(
    const RuntimeInternetServiceWireMessage& message,
    std::vector<std::uint8_t>& framedBytes,
    std::string& errorMessage
) {
    errorMessage.clear();
    std::vector<std::uint8_t> body;
    if (!encodeWireBody(message, body, errorMessage)) {
        framedBytes.clear();
        return false;
    }
    framedBytes.clear();
    framedBytes.reserve(wireLengthBytes + body.size());
    appendU32(framedBytes, static_cast<std::uint32_t>(body.size()));
    framedBytes.insert(framedBytes.end(), body.begin(), body.end());
    return true;
}

bool RuntimeInternetServiceWireDecoder::push(
    std::span<const std::uint8_t> bytes,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (bytes.empty()) return true;
    if (offset_ > 0 && (offset_ == bytes_.size() || offset_ >= 64U * 1024U)) {
        bytes_.erase(
            bytes_.begin(),
            bytes_.begin() + static_cast<std::ptrdiff_t>(offset_)
        );
        offset_ = 0;
    }
    if (bytes.size() > runtimeInternetServiceMaximumBufferedWireBytes
        || bufferedBytes()
            > runtimeInternetServiceMaximumBufferedWireBytes - bytes.size()) {
        errorMessage = "Internet service wire input exceeded its buffer limit";
        return false;
    }
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    return true;
}

bool RuntimeInternetServiceWireDecoder::pop(
    RuntimeInternetServiceWireMessage& message,
    bool& available,
    std::string& errorMessage
) {
    errorMessage.clear();
    available = false;
    const std::size_t remaining = bufferedBytes();
    if (remaining < wireLengthBytes) return true;
    const std::uint8_t* length = bytes_.data() + offset_;
    const std::uint32_t bodyBytes =
        (static_cast<std::uint32_t>(length[0]) << 24U)
        | (static_cast<std::uint32_t>(length[1]) << 16U)
        | (static_cast<std::uint32_t>(length[2]) << 8U)
        | static_cast<std::uint32_t>(length[3]);
    if (bodyBytes < wireHeaderBytes
        || bodyBytes > runtimeInternetServiceMaximumWireFrameBytes) {
        errorMessage = "Internet service wire frame length is invalid";
        return false;
    }
    const std::size_t frameBytes = wireLengthBytes + bodyBytes;
    if (remaining < frameBytes) return true;
    const std::span<const std::uint8_t> body(
        bytes_.data() + offset_ + wireLengthBytes,
        bodyBytes
    );
    RuntimeInternetServiceWireMessage parsed;
    if (!decodeWireBody(body, parsed, errorMessage)) return false;
    offset_ += frameBytes;
    message = std::move(parsed);
    available = true;
    return true;
}

std::vector<std::uint8_t>
RuntimeInternetServiceWireDecoder::takeBufferedBytes() {
    std::vector<std::uint8_t> result;
    result.assign(
        bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
        bytes_.end()
    );
    reset();
    return result;
}

std::size_t RuntimeInternetServiceWireDecoder::bufferedBytes() const noexcept {
    return bytes_.size() - offset_;
}

void RuntimeInternetServiceWireDecoder::reset() noexcept {
    secureZero(bytes_);
    offset_ = 0;
}

struct RuntimeInternetServiceNonceReplayWindow::Impl {
    std::size_t maximumEntries = 8192U;
    std::uint64_t retentionMilliseconds = 60000U;
    std::map<RuntimeInternetServiceNonce, std::uint64_t> entries;
};

RuntimeInternetServiceNonceReplayWindow::
RuntimeInternetServiceNonceReplayWindow(
    std::size_t maximumEntries,
    std::uint64_t retentionMilliseconds
) : impl_(std::make_unique<Impl>()) {
    impl_->maximumEntries = std::max<std::size_t>(1U, maximumEntries);
    impl_->retentionMilliseconds = std::max<std::uint64_t>(
        1U,
        retentionMilliseconds
    );
}

RuntimeInternetServiceNonceReplayWindow::~
RuntimeInternetServiceNonceReplayWindow() = default;

RuntimeInternetServiceNonceReplayWindow::
RuntimeInternetServiceNonceReplayWindow(
    RuntimeInternetServiceNonceReplayWindow&&
) noexcept = default;

RuntimeInternetServiceNonceReplayWindow&
RuntimeInternetServiceNonceReplayWindow::operator=(
    RuntimeInternetServiceNonceReplayWindow&&
) noexcept = default;

bool RuntimeInternetServiceNonceReplayWindow::accept(
    const RuntimeInternetServiceNonce& nonce,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (!impl_ || !nonceNonzero(nonce)) {
        errorMessage = "Internet service authentication nonce is invalid";
        return false;
    }
    prune(nowMilliseconds);
    if (impl_->entries.contains(nonce)) {
        errorMessage = "Internet service authentication nonce was replayed";
        return false;
    }
    if (impl_->entries.size() >= impl_->maximumEntries) {
        errorMessage = "Internet service authentication replay window is full";
        return false;
    }
    const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    const std::uint64_t expires = nowMilliseconds
            > maximum - impl_->retentionMilliseconds
        ? maximum
        : nowMilliseconds + impl_->retentionMilliseconds;
    impl_->entries.emplace(nonce, expires);
    return true;
}

void RuntimeInternetServiceNonceReplayWindow::prune(
    std::uint64_t nowMilliseconds
) noexcept {
    if (!impl_) return;
    for (auto iterator = impl_->entries.begin();
         iterator != impl_->entries.end();) {
        if (iterator->second <= nowMilliseconds) {
            iterator = impl_->entries.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

std::size_t RuntimeInternetServiceNonceReplayWindow::size() const noexcept {
    return impl_ ? impl_->entries.size() : 0;
}

namespace {

class BufferedAuthenticatedStream final
    : public RuntimeInternetAuthenticatedTcpStream {
public:
    BufferedAuthenticatedStream(
        std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> stream,
        std::vector<std::uint8_t> prefix
    )
        : stream_(std::move(stream)),
          identity_(stream_->identity()),
          prefix_(std::move(prefix)) {}

    ~BufferedAuthenticatedStream() override { close(); }

    const RuntimeInternetAuthenticatedTcpStreamIdentity& identity()
        const override {
        return identity_;
    }

    RuntimeInternetTcpStreamIoResult read(
        std::span<std::uint8_t> destination
    ) override {
        if (prefixOffset_ < prefix_.size() && !destination.empty()) {
            const std::size_t copied = std::min(
                destination.size(),
                prefix_.size() - prefixOffset_
            );
            std::copy_n(
                prefix_.begin()
                    + static_cast<std::ptrdiff_t>(prefixOffset_),
                copied,
                destination.begin()
            );
            prefixOffset_ += copied;
            if (prefixOffset_ == prefix_.size()) {
                secureZero(prefix_);
                prefixOffset_ = 0;
            }
            return {
                RuntimeInternetTcpStreamIoStatus::Progress,
                copied,
                {}
            };
        }
        if (!stream_) {
            return {RuntimeInternetTcpStreamIoStatus::Closed, 0, {}};
        }
        return stream_->read(destination);
    }

    RuntimeInternetTcpStreamIoResult write(
        std::span<const std::uint8_t> source
    ) override {
        if (!stream_) {
            return {RuntimeInternetTcpStreamIoStatus::Closed, 0, {}};
        }
        return stream_->write(source);
    }

    void close() noexcept override {
        secureZero(prefix_);
        prefixOffset_ = 0;
        if (stream_) stream_->close();
        stream_.reset();
    }

private:
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> stream_;
    RuntimeInternetAuthenticatedTcpStreamIdentity identity_;
    std::vector<std::uint8_t> prefix_;
    std::size_t prefixOffset_ = 0;
};

enum class ChannelReadKind : std::uint8_t {
    Message,
    WouldBlock,
    Closed,
    Failed
};

struct ChannelReadResult {
    ChannelReadKind kind = ChannelReadKind::WouldBlock;
    RuntimeInternetServiceWireMessage message;
    std::string error;
};

class FramedTlsChannel {
public:
    FramedTlsChannel(
        std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> stream,
        std::size_t maximumQueuedBytes
    )
        : stream_(std::move(stream)),
          maximumQueuedBytes_(maximumQueuedBytes) {}

    ~FramedTlsChannel() { close(); }
    FramedTlsChannel(FramedTlsChannel&&) noexcept = default;
    FramedTlsChannel& operator=(FramedTlsChannel&&) noexcept = default;
    FramedTlsChannel(const FramedTlsChannel&) = delete;
    FramedTlsChannel& operator=(const FramedTlsChannel&) = delete;

    bool queueMessage(
        const RuntimeInternetServiceWireMessage& message,
        std::string& errorMessage
    ) {
        std::vector<std::uint8_t> frame;
        if (!encodeRuntimeInternetServiceWireFrame(
                message,
                frame,
                errorMessage
            )) {
            return false;
        }
        return queueFrame(std::move(frame), errorMessage);
    }

    bool queueFrame(
        std::vector<std::uint8_t> frame,
        std::string& errorMessage
    ) {
        if (!stream_) {
            errorMessage = "authenticated Internet service channel is closed";
            secureZero(frame);
            return false;
        }
        if (frame.size() > maximumQueuedBytes_
            || queuedBytes_ > maximumQueuedBytes_ - frame.size()) {
            errorMessage = "authenticated Internet service send queue is full";
            secureZero(frame);
            return false;
        }
        queuedBytes_ += frame.size();
        output_.push_back(std::move(frame));
        return true;
    }

    bool pumpWrite(bool& progressed, std::string& errorMessage) {
        progressed = false;
        if (!stream_) {
            errorMessage = "authenticated Internet service channel is closed";
            return false;
        }
        for (int iteration = 0; iteration < 64 && !output_.empty();
             ++iteration) {
            std::vector<std::uint8_t>& frame = output_.front();
            const RuntimeInternetTcpStreamIoResult write = stream_->write(
                std::span<const std::uint8_t>(frame).subspan(outputOffset_)
            );
            if (write.status == RuntimeInternetTcpStreamIoStatus::WouldBlock) {
                return true;
            }
            if (write.status == RuntimeInternetTcpStreamIoStatus::Closed) {
                errorMessage = "authenticated Internet service channel closed";
                return false;
            }
            if (write.status == RuntimeInternetTcpStreamIoStatus::Failed) {
                errorMessage = write.error.empty()
                    ? "authenticated Internet service send failed"
                    : write.error;
                return false;
            }
            if (write.bytesTransferred == 0) return true;
            if (write.bytesTransferred > frame.size() - outputOffset_) {
                errorMessage =
                    "authenticated Internet service reported invalid write progress";
                return false;
            }
            outputOffset_ += write.bytesTransferred;
            queuedBytes_ -= write.bytesTransferred;
            progressed = true;
            if (outputOffset_ == frame.size()) {
                secureZero(frame);
                output_.pop_front();
                outputOffset_ = 0;
            }
        }
        return true;
    }

    ChannelReadResult readOne() {
        ChannelReadResult result;
        if (!stream_) {
            result.kind = ChannelReadKind::Closed;
            result.error = "authenticated Internet service channel is closed";
            return result;
        }
        for (int iteration = 0; iteration < 16; ++iteration) {
            bool available = false;
            std::string decodeError;
            if (!decoder_.pop(result.message, available, decodeError)) {
                result.kind = ChannelReadKind::Failed;
                result.error = std::move(decodeError);
                return result;
            }
            if (available) {
                result.kind = ChannelReadKind::Message;
                return result;
            }
            std::array<std::uint8_t, wireReadChunkBytes> bytes{};
            const RuntimeInternetTcpStreamIoResult read = stream_->read(bytes);
            if (read.status == RuntimeInternetTcpStreamIoStatus::WouldBlock) {
                result.kind = ChannelReadKind::WouldBlock;
                return result;
            }
            if (read.status == RuntimeInternetTcpStreamIoStatus::Closed) {
                result.kind = ChannelReadKind::Closed;
                result.error = "authenticated Internet service channel closed";
                return result;
            }
            if (read.status == RuntimeInternetTcpStreamIoStatus::Failed) {
                result.kind = ChannelReadKind::Failed;
                result.error = read.error.empty()
                    ? "authenticated Internet service receive failed"
                    : read.error;
                return result;
            }
            if (read.bytesTransferred == 0) {
                result.kind = ChannelReadKind::WouldBlock;
                return result;
            }
            if (read.bytesTransferred > bytes.size()
                || !decoder_.push(
                    std::span<const std::uint8_t>(
                        bytes.data(),
                        read.bytesTransferred
                    ),
                    decodeError
                )) {
                result.kind = ChannelReadKind::Failed;
                result.error = decodeError.empty()
                    ? "authenticated Internet service read overflow"
                    : std::move(decodeError);
                return result;
            }
        }
        result.kind = ChannelReadKind::WouldBlock;
        return result;
    }

    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> takeStream() {
        if (!stream_ || !output_.empty()) return {};
        std::vector<std::uint8_t> prefix = decoder_.takeBufferedBytes();
        auto stream = std::move(stream_);
        if (prefix.empty()) return stream;
        return std::make_unique<BufferedAuthenticatedStream>(
            std::move(stream),
            std::move(prefix)
        );
    }

    std::size_t queuedBytes() const noexcept { return queuedBytes_; }
    bool outputEmpty() const noexcept { return output_.empty(); }

    void close() noexcept {
        for (std::vector<std::uint8_t>& frame : output_) secureZero(frame);
        output_.clear();
        outputOffset_ = 0;
        queuedBytes_ = 0;
        decoder_.reset();
        if (stream_) stream_->close();
        stream_.reset();
    }

private:
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> stream_;
    std::size_t maximumQueuedBytes_ = 0;
    std::deque<std::vector<std::uint8_t>> output_;
    std::size_t outputOffset_ = 0;
    std::size_t queuedBytes_ = 0;
    RuntimeInternetServiceWireDecoder decoder_;
};

bool validTransportConfig(
    const RuntimeInternetServiceTransportConfig& config,
    std::string& errorMessage
) {
    if (!validIdentityFields(config.serviceId, config.peerId, errorMessage)) {
        return false;
    }
    if (config.credential.size()
            < runtimeInternetServiceMinimumCredentialBytes
        || config.credential.size()
            > runtimeInternetServiceMaximumCredentialBytes) {
        errorMessage = "Internet service credential length is invalid";
        return false;
    }
    if (config.coordinatorTls.endpoint.address.empty()
        || config.coordinatorTls.endpoint.port == 0
        || config.coordinatorTls.serverName.empty()
        || config.relayTls.serverName.empty()
        || config.applicationAuthenticationTimeoutMilliseconds == 0
        || config.maximumQueuedBytes
            < runtimeInternetServiceMaximumWireFrameBytes + wireLengthBytes
        || config.maximumQueuedBytes
            > runtimeInternetServiceMaximumBufferedWireBytes
        || config.maximumPendingRelays == 0
        || config.maximumPendingRelays > runtimeInternetTlsMaximumConnections) {
        errorMessage = "Internet service transport configuration is invalid";
        return false;
    }
    return true;
}

bool exactConfirmation(
    const RuntimeInternetServiceAuthenticationConfirmation& confirmation,
    RuntimeInternetServiceWireChannel channel,
    const RuntimeInternetServiceNonce& nonce,
    const std::string& serviceId,
    const std::string& peerId,
    const RuntimeInternetTcpRouteDescriptor* route
) {
    if (confirmation.channel != channel || confirmation.nonce != nonce
        || confirmation.serviceId != serviceId
        || confirmation.peerId != peerId) {
        return false;
    }
    if (channel == RuntimeInternetServiceWireChannel::RelayTcp) {
        return route != nullptr && confirmation.route == *route;
    }
    return true;
}

RuntimeInternetAuthenticatedTcpStreamIdentity streamIdentity(
    const std::string& serviceId,
    const std::string& peerId,
    RuntimeInternetTcpRouteDescriptor route = {}
) {
    RuntimeInternetAuthenticatedTcpStreamIdentity identity;
    identity.authenticatedServiceId = serviceId;
    identity.authenticatedPeerId = peerId;
    identity.route = std::move(route);
    return identity;
}

enum class CoordinatorStage : std::uint8_t {
    ConnectingTls,
    Authenticating,
    Ready,
    Failed,
    Closed
};

enum class RelayStage : std::uint8_t {
    ConnectingTls,
    Authenticating
};

} // namespace

struct RuntimeInternetServiceTransport::Impl {
    struct RelayConnection {
        RuntimeInternetServiceTcpOpenRequest request;
        RuntimeInternetTlsClient connector;
        std::unique_ptr<FramedTlsChannel> channel;
        RelayStage stage = RelayStage::ConnectingTls;
        RuntimeInternetServiceNonce nonce{};
        std::uint64_t authenticationDeadline = 0;
    };

    RuntimeInternetServiceTransportConfig config;
    bool opened = false;
    CoordinatorStage coordinatorStage = CoordinatorStage::Closed;
    RuntimeInternetTlsClient coordinatorConnector;
    std::unique_ptr<FramedTlsChannel> coordinatorChannel;
    RuntimeInternetServiceNonce coordinatorNonce{};
    std::uint64_t coordinatorAuthenticationDeadline = 0;
    std::deque<std::vector<std::uint8_t>> pendingCoordinatorFrames;
    std::size_t pendingCoordinatorBytes = 0;
    std::map<std::uint64_t, RelayConnection> relays;
    bool coordinatorFailureReported = false;

    RuntimeInternetServiceEvent failureEvent(
        std::string reason,
        std::uint64_t connectionId = 0,
        RuntimeInternetEndpoint source = {}
    ) const {
        RuntimeInternetServiceEvent event;
        event.kind = RuntimeInternetServiceEventKind::Failure;
        event.authenticatedServiceId = config.serviceId;
        event.authenticatedPeerId = config.peerId;
        event.source = std::move(source);
        event.connectionId = connectionId;
        event.reason = reason.empty()
            ? "authenticated Internet service transport failed"
            : std::move(reason);
        return event;
    }

    void failCoordinator(
        std::string reason,
        std::vector<RuntimeInternetServiceEvent>& events
    ) {
        if (coordinatorStage == CoordinatorStage::Failed
            || coordinatorStage == CoordinatorStage::Closed) {
            return;
        }
        coordinatorStage = CoordinatorStage::Failed;
        coordinatorConnector.close();
        if (coordinatorChannel) coordinatorChannel->close();
        coordinatorChannel.reset();
        for (auto& entry : relays) {
            entry.second.connector.close();
            if (entry.second.channel) entry.second.channel->close();
        }
        relays.clear();
        for (std::vector<std::uint8_t>& frame : pendingCoordinatorFrames) {
            secureZero(frame);
        }
        pendingCoordinatorFrames.clear();
        pendingCoordinatorBytes = 0;
        if (!coordinatorFailureReported) {
            events.push_back(failureEvent(std::move(reason)));
            coordinatorFailureReported = true;
        }
    }

    bool queueCoordinatorFrame(
        std::vector<std::uint8_t> frame,
        std::string& errorMessage
    ) {
        if (coordinatorStage == CoordinatorStage::Ready
            && coordinatorChannel) {
            return coordinatorChannel->queueFrame(
                std::move(frame),
                errorMessage
            );
        }
        if (coordinatorStage == CoordinatorStage::Failed
            || coordinatorStage == CoordinatorStage::Closed) {
            errorMessage = "authenticated coordinator channel is closed";
            secureZero(frame);
            return false;
        }
        if (frame.size() > config.maximumQueuedBytes
            || pendingCoordinatorBytes
                > config.maximumQueuedBytes - frame.size()) {
            errorMessage = "authenticated coordinator send queue is full";
            secureZero(frame);
            return false;
        }
        pendingCoordinatorBytes += frame.size();
        pendingCoordinatorFrames.push_back(std::move(frame));
        return true;
    }

    bool releasePendingCoordinatorFrames(std::string& errorMessage) {
        if (!coordinatorChannel) {
            errorMessage = "authenticated coordinator channel is missing";
            return false;
        }
        while (!pendingCoordinatorFrames.empty()) {
            std::vector<std::uint8_t> frame = std::move(
                pendingCoordinatorFrames.front()
            );
            pendingCoordinatorFrames.pop_front();
            pendingCoordinatorBytes -= frame.size();
            if (!coordinatorChannel->queueFrame(
                    std::move(frame),
                    errorMessage
                )) {
                return false;
            }
        }
        return true;
    }

    bool beginCoordinatorAuthentication(
        std::uint64_t now,
        std::string& errorMessage
    ) {
        auto stream = coordinatorConnector.takeStream(errorMessage);
        if (!stream || !fillNonce(coordinatorNonce)) {
            if (errorMessage.empty()) {
                errorMessage =
                    "could not create coordinator authentication nonce";
            }
            return false;
        }
        coordinatorChannel = std::make_unique<FramedTlsChannel>(
            std::move(stream),
            config.maximumQueuedBytes
        );
        RuntimeInternetServiceAuthenticationRequest authentication;
        authentication.channel =
            RuntimeInternetServiceWireChannel::Coordinator;
        authentication.nonce = coordinatorNonce;
        authentication.serviceId = config.serviceId;
        authentication.peerId = config.peerId;
        authentication.credential = config.credential;
        RuntimeInternetServiceWireMessage message;
        message.body = std::move(authentication);
        const bool queued = coordinatorChannel->queueMessage(
            message,
            errorMessage
        );
        auto& encodedAuthentication = std::get<
            RuntimeInternetServiceAuthenticationRequest
        >(message.body);
        secureZero(encodedAuthentication.credential);
        if (!queued) return false;
        coordinatorAuthenticationDeadline = deadlineAfter(
            now,
            config.applicationAuthenticationTimeoutMilliseconds
        );
        coordinatorStage = CoordinatorStage::Authenticating;
        return true;
    }

    bool stepCoordinator(
        std::uint64_t now,
        std::vector<RuntimeInternetServiceEvent>& events,
        bool& progressed
    ) {
        if (coordinatorStage == CoordinatorStage::ConnectingTls) {
            const RuntimeInternetTlsPollResult tls =
                coordinatorConnector.poll(now);
            progressed = progressed || tls.progressed;
            if (tls.state == RuntimeInternetTlsState::Failed
                || tls.state == RuntimeInternetTlsState::Closed) {
                failCoordinator(
                    tls.error.empty()
                        ? "coordinator TLS connection failed"
                        : tls.error,
                    events
                );
                return false;
            }
            if (tls.state == RuntimeInternetTlsState::Open) {
                std::string error;
                if (!beginCoordinatorAuthentication(now, error)) {
                    failCoordinator(std::move(error), events);
                    return false;
                }
                progressed = true;
            }
        }
        if (coordinatorStage != CoordinatorStage::Authenticating
            && coordinatorStage != CoordinatorStage::Ready) {
            return coordinatorStage != CoordinatorStage::Failed;
        }
        if (!coordinatorChannel) {
            failCoordinator(
                "authenticated coordinator channel disappeared",
                events
            );
            return false;
        }
        std::string error;
        bool wrote = false;
        if (!coordinatorChannel->pumpWrite(wrote, error)) {
            failCoordinator(std::move(error), events);
            return false;
        }
        progressed = progressed || wrote;
        if (coordinatorStage == CoordinatorStage::Authenticating
            && now >= coordinatorAuthenticationDeadline) {
            failCoordinator(
                "coordinator application authentication timed out",
                events
            );
            return false;
        }
        for (int iteration = 0; iteration < 32; ++iteration) {
            ChannelReadResult read = coordinatorChannel->readOne();
            if (read.kind == ChannelReadKind::WouldBlock) break;
            if (read.kind == ChannelReadKind::Closed
                || read.kind == ChannelReadKind::Failed) {
                failCoordinator(std::move(read.error), events);
                return false;
            }
            progressed = true;
            if (coordinatorStage == CoordinatorStage::Authenticating) {
                if (const auto* rejection = std::get_if<
                        RuntimeInternetServiceAuthenticationRejection
                    >(&read.message.body)) {
                    if (rejection->channel
                            != RuntimeInternetServiceWireChannel::Coordinator
                        || rejection->nonce != coordinatorNonce) {
                        failCoordinator(
                            "coordinator authentication rejection did not match its request",
                            events
                        );
                    } else {
                        failCoordinator(
                            "coordinator application authentication was rejected",
                            events
                        );
                    }
                    return false;
                }
                const auto* confirmation = std::get_if<
                    RuntimeInternetServiceAuthenticationConfirmation
                >(&read.message.body);
                if (confirmation == nullptr
                    || !exactConfirmation(
                        *confirmation,
                        RuntimeInternetServiceWireChannel::Coordinator,
                        coordinatorNonce,
                        config.serviceId,
                        config.peerId,
                        nullptr
                    )) {
                    failCoordinator(
                        "coordinator authentication confirmation did not match its request",
                        events
                    );
                    return false;
                }
                coordinatorStage = CoordinatorStage::Ready;
                RuntimeInternetServiceEvent authenticated;
                authenticated.kind = RuntimeInternetServiceEventKind::CoordinatorAuthenticated;
                authenticated.authenticatedServiceId = config.serviceId;
                authenticated.authenticatedPeerId = config.peerId;
                events.push_back(std::move(authenticated));
                if (!releasePendingCoordinatorFrames(error)) {
                    failCoordinator(std::move(error), events);
                    return false;
                }
                continue;
            }
            const auto* packet = std::get_if<
                RuntimeInternetServiceCoordinatorPacket
            >(&read.message.body);
            if (packet == nullptr || !packet->roomAdmissionProof.empty()) {
                failCoordinator(
                    "authenticated coordinator sent an unexpected wire message",
                    events
                );
                return false;
            }
            RuntimeInternetServiceEvent event;
            event.kind = RuntimeInternetServiceEventKind::CoordinatorPacket;
            event.authenticatedServiceId = config.serviceId;
            event.authenticatedPeerId = config.peerId;
            event.source = config.coordinatorTls.endpoint;
            event.packet = packet->packet;
            events.push_back(std::move(event));
        }
        return true;
    }

    bool beginRelayAuthentication(
        RelayConnection& relay,
        std::uint64_t now,
        std::string& errorMessage
    ) {
        auto stream = relay.connector.takeStream(errorMessage);
        if (!stream || !fillNonce(relay.nonce)) {
            if (errorMessage.empty()) {
                errorMessage = "could not create relay authentication nonce";
            }
            return false;
        }
        relay.channel = std::make_unique<FramedTlsChannel>(
            std::move(stream),
            config.maximumQueuedBytes
        );
        RuntimeInternetServiceAuthenticationRequest authentication;
        authentication.channel = RuntimeInternetServiceWireChannel::RelayTcp;
        authentication.nonce = relay.nonce;
        authentication.serviceId = config.serviceId;
        authentication.peerId = config.peerId;
        authentication.credential = config.credential;
        authentication.route = relay.request.route;
        authentication.relayBindPacket = relay.request.relayBindPacket;
        RuntimeInternetServiceWireMessage message;
        message.body = std::move(authentication);
        const bool queued = relay.channel->queueMessage(message, errorMessage);
        auto& encodedAuthentication = std::get<
            RuntimeInternetServiceAuthenticationRequest
        >(message.body);
        secureZero(encodedAuthentication.credential);
        if (!queued) return false;
        relay.authenticationDeadline = deadlineAfter(
            now,
            config.applicationAuthenticationTimeoutMilliseconds
        );
        relay.stage = RelayStage::Authenticating;
        return true;
    }

    bool stepRelay(
        RelayConnection& relay,
        std::uint64_t now,
        std::vector<RuntimeInternetServiceEvent>& events,
        bool& progressed,
        bool& completed
    ) {
        completed = false;
        if (relay.stage == RelayStage::ConnectingTls) {
            const RuntimeInternetTlsPollResult tls = relay.connector.poll(now);
            progressed = progressed || tls.progressed;
            if (tls.state == RuntimeInternetTlsState::Failed
                || tls.state == RuntimeInternetTlsState::Closed) {
                events.push_back(failureEvent(
                    tls.error.empty() ? "relay TLS connection failed" : tls.error,
                    relay.request.route.connectionId,
                    relay.request.endpoint
                ));
                completed = true;
                return false;
            }
            if (tls.state == RuntimeInternetTlsState::Open) {
                std::string error;
                if (!beginRelayAuthentication(relay, now, error)) {
                    events.push_back(failureEvent(
                        std::move(error),
                        relay.request.route.connectionId,
                        relay.request.endpoint
                    ));
                    completed = true;
                    return false;
                }
                progressed = true;
            }
        }
        if (relay.stage != RelayStage::Authenticating) return true;
        if (!relay.channel) {
            events.push_back(failureEvent(
                "authenticated relay channel disappeared",
                relay.request.route.connectionId,
                relay.request.endpoint
            ));
            completed = true;
            return false;
        }
        std::string error;
        bool wrote = false;
        if (!relay.channel->pumpWrite(wrote, error)) {
            events.push_back(failureEvent(
                std::move(error),
                relay.request.route.connectionId,
                relay.request.endpoint
            ));
            completed = true;
            return false;
        }
        progressed = progressed || wrote;
        if (now >= relay.authenticationDeadline) {
            events.push_back(failureEvent(
                "relay application authentication timed out",
                relay.request.route.connectionId,
                relay.request.endpoint
            ));
            completed = true;
            return false;
        }
        ChannelReadResult read = relay.channel->readOne();
        if (read.kind == ChannelReadKind::WouldBlock) return true;
        if (read.kind == ChannelReadKind::Closed
            || read.kind == ChannelReadKind::Failed) {
            events.push_back(failureEvent(
                std::move(read.error),
                relay.request.route.connectionId,
                relay.request.endpoint
            ));
            completed = true;
            return false;
        }
        progressed = true;
        if (const auto* rejection = std::get_if<
                RuntimeInternetServiceAuthenticationRejection
            >(&read.message.body)) {
            const bool matching = rejection->channel
                    == RuntimeInternetServiceWireChannel::RelayTcp
                && rejection->nonce == relay.nonce;
            events.push_back(failureEvent(
                matching
                    ? "relay application authentication was rejected"
                    : "relay authentication rejection did not match its request",
                relay.request.route.connectionId,
                relay.request.endpoint
            ));
            completed = true;
            return false;
        }
        const auto* confirmation = std::get_if<
            RuntimeInternetServiceAuthenticationConfirmation
        >(&read.message.body);
        if (confirmation == nullptr
            || !exactConfirmation(
                *confirmation,
                RuntimeInternetServiceWireChannel::RelayTcp,
                relay.nonce,
                config.serviceId,
                config.peerId,
                &relay.request.route
            )) {
            events.push_back(failureEvent(
                "relay authentication confirmation did not match its route",
                relay.request.route.connectionId,
                relay.request.endpoint
            ));
            completed = true;
            return false;
        }
        auto stream = relay.channel->takeStream();
        if (!stream) {
            events.push_back(failureEvent(
                "relay confirmation arrived before authentication output completed",
                relay.request.route.connectionId,
                relay.request.endpoint
            ));
            completed = true;
            return false;
        }
        const RuntimeInternetAuthenticatedTcpStreamIdentity& identity =
            stream->identity();
        if (identity.authenticatedServiceId != config.serviceId
            || identity.authenticatedPeerId != config.peerId
            || identity.route != relay.request.route) {
            stream->close();
            events.push_back(failureEvent(
                "relay TLS identity did not match its confirmed route",
                relay.request.route.connectionId,
                relay.request.endpoint
            ));
            completed = true;
            return false;
        }

        RuntimeInternetControlMessage boundMessage;
        boundMessage.type = RuntimeInternetControlType::RelayBound;
        boundMessage.transactionId = relay.request.route.connectionId;
        boundMessage.serviceId = relay.request.route.serviceId;
        boundMessage.roomId = relay.request.route.roomId;
        boundMessage.senderPeerId = relay.request.route.serviceId;
        boundMessage.targetPeerId = relay.request.route.localPeerId;
        boundMessage.body = RuntimeInternetRelayBoundBody{
            relay.request.route.connectionId,
            RuntimeInternetRelayTransport::Tcp
        };
        RuntimeNetworkPacket boundPacket;
        std::string boundError;
        if (!encodeRuntimeInternetControlMessage(
                boundMessage,
                boundPacket,
                boundError
            )) {
            stream->close();
            events.push_back(failureEvent(
                "could not construct confirmed relay binding event",
                relay.request.route.connectionId,
                relay.request.endpoint
            ));
            completed = true;
            return false;
        }

        // A relay authentication confirmation is emitted by the service only
        // after both route legs have been activated. Deliver its canonical
        // RelayBound evidence before exposing the authenticated byte stream so
        // RuntimeInternetLobbyFlow can transition out of BindingRelayTcp.
        RuntimeInternetServiceEvent boundEvent;
        boundEvent.kind = RuntimeInternetServiceEventKind::RelayPacket;
        boundEvent.authenticatedServiceId = config.serviceId;
        boundEvent.authenticatedPeerId = config.peerId;
        boundEvent.source = relay.request.endpoint;
        boundEvent.connectionId = relay.request.route.connectionId;
        boundEvent.packet = std::move(boundPacket);
        events.push_back(std::move(boundEvent));

        RuntimeInternetServiceEvent event;
        event.kind = RuntimeInternetServiceEventKind::TcpStreamOpened;
        event.authenticatedServiceId = config.serviceId;
        event.authenticatedPeerId = config.peerId;
        event.source = relay.request.endpoint;
        event.connectionId = relay.request.route.connectionId;
        event.tcpStream = std::move(stream);
        events.push_back(std::move(event));
        completed = true;
        return true;
    }

    void close() noexcept {
        coordinatorConnector.close();
        if (coordinatorChannel) coordinatorChannel->close();
        coordinatorChannel.reset();
        for (std::vector<std::uint8_t>& frame : pendingCoordinatorFrames) {
            secureZero(frame);
        }
        pendingCoordinatorFrames.clear();
        pendingCoordinatorBytes = 0;
        for (auto& entry : relays) {
            entry.second.connector.close();
            if (entry.second.channel) entry.second.channel->close();
        }
        relays.clear();
        secureZero(config.credential);
        coordinatorStage = CoordinatorStage::Closed;
        opened = false;
    }
};

RuntimeInternetServiceTransport::RuntimeInternetServiceTransport()
    : impl_(std::make_unique<Impl>()) {}

RuntimeInternetServiceTransport::~RuntimeInternetServiceTransport() {
    close();
}

RuntimeInternetServiceTransport::RuntimeInternetServiceTransport(
    RuntimeInternetServiceTransport&&
) noexcept = default;

RuntimeInternetServiceTransport& RuntimeInternetServiceTransport::operator=(
    RuntimeInternetServiceTransport&& other
) noexcept {
    if (this != &other) {
        close();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool RuntimeInternetServiceTransport::reauthenticateCoordinator(std::string& errorMessage) {
    if (!impl_ || !impl_->opened) {
        errorMessage = "Internet service transport is not open";
        return false;
    }
    std::vector<RuntimeInternetServiceEvent> ignored;
    impl_->failCoordinator("coordinator reconnect requested", ignored);
    impl_->coordinatorFailureReported = false;
    impl_->coordinatorStage = CoordinatorStage::Failed;
    if (!impl_->coordinatorConnector.begin(impl_->config.coordinatorTls,
            streamIdentity(impl_->config.serviceId, impl_->config.peerId),
            monotonicMilliseconds(), errorMessage)) return false;
    impl_->coordinatorStage = CoordinatorStage::ConnectingTls;
    errorMessage.clear();
    return true;
}

bool RuntimeInternetServiceTransport::open(
    RuntimeInternetServiceTransportConfig config,
    std::string& errorMessage
) {
    errorMessage.clear();
    close();
    impl_ = std::make_unique<Impl>();
    if (!validTransportConfig(config, errorMessage)) {
        secureZero(config.credential);
        return false;
    }
    impl_->config = std::move(config);
    RuntimeInternetAuthenticatedTcpStreamIdentity identity = streamIdentity(
        impl_->config.serviceId,
        impl_->config.peerId
    );
    if (!impl_->coordinatorConnector.begin(
            impl_->config.coordinatorTls,
            std::move(identity),
            monotonicMilliseconds(),
            errorMessage
        )) {
        secureZero(impl_->config.credential);
        return false;
    }
    impl_->opened = true;
    impl_->coordinatorStage = CoordinatorStage::ConnectingTls;
    return true;
}

std::string_view RuntimeInternetServiceTransport::authenticatedServiceId()
    const noexcept {
    return impl_ ? std::string_view(impl_->config.serviceId)
                 : std::string_view{};
}

std::string_view RuntimeInternetServiceTransport::authenticatedPeerId()
    const noexcept {
    return impl_ ? std::string_view(impl_->config.peerId)
                 : std::string_view{};
}

bool RuntimeInternetServiceTransport::sendCoordinator(
    const RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    return sendCoordinatorAuthenticated(packet, {}, errorMessage);
}

bool RuntimeInternetServiceTransport::sendCoordinatorWithAdmissionProof(
    const RuntimeNetworkPacket& packet,
    std::span<const std::uint8_t> roomAdmissionProof,
    std::string& errorMessage
) {
    return sendCoordinatorAuthenticated(
        packet,
        roomAdmissionProof,
        errorMessage
    );
}

bool RuntimeInternetServiceTransport::sendCoordinatorAuthenticated(
    const RuntimeNetworkPacket& packet,
    std::span<const std::uint8_t> roomAdmissionProof,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (!impl_ || !impl_->opened) {
        errorMessage = "Internet service transport is not open";
        return false;
    }
    if (roomAdmissionProof.size()
        > runtimeInternetCoordinatorMaximumAdmissionProofBytes) {
        errorMessage = "room admission proof exceeds its size limit";
        return false;
    }
    if (!roomAdmissionProof.empty()) {
        RuntimeInternetControlMessage control;
        const bool decoded = decodeRuntimeInternetControlMessage(
                packet,
                control,
                errorMessage
            );
        const auto* registration = decoded
            && control.type == RuntimeInternetControlType::RegisterRoom
            ? std::get_if<RuntimeInternetRegisterRoomBody>(&control.body)
            : nullptr;
        const bool proofAllowed = decoded
            && (control.type == RuntimeInternetControlType::JoinRoom
                || (registration != nullptr
                    && registration->room.passwordProtected));
        if (!proofAllowed) {
            errorMessage =
                "room password proof is allowed only on a protected RegisterRoom or JoinRoom request";
            return false;
        }
    }
    RuntimeInternetServiceCoordinatorPacket envelope;
    envelope.roomAdmissionProof.assign(
        roomAdmissionProof.begin(),
        roomAdmissionProof.end()
    );
    envelope.packet = packet;
    RuntimeInternetServiceWireMessage message;
    message.body = std::move(envelope);
    std::vector<std::uint8_t> frame;
    if (!encodeRuntimeInternetServiceWireFrame(
            message,
            frame,
            errorMessage
        )) {
        return false;
    }
    return impl_->queueCoordinatorFrame(std::move(frame), errorMessage);
}

bool RuntimeInternetServiceTransport::openAuthenticatedTcpRelay(
    const RuntimeInternetServiceTcpOpenRequest& request,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (!impl_ || !impl_->opened
        || impl_->coordinatorStage != CoordinatorStage::Ready) {
        errorMessage = "coordinator is not application-authenticated";
        return false;
    }
    if (!validRoute(request.route, errorMessage)
        || !validPacket(request.relayBindPacket, errorMessage)
        || !validRelayBindForRoute(
            request.relayBindPacket,
            request.route,
            errorMessage
        )
        || request.route.serviceId != impl_->config.serviceId
        || request.route.localPeerId != impl_->config.peerId
        || request.route.endpoint != request.endpoint) {
        if (errorMessage.empty()) {
            errorMessage = "relay request does not match the authenticated peer";
        }
        return false;
    }
    if (impl_->relays.size() >= impl_->config.maximumPendingRelays) {
        errorMessage = "too many pending authenticated relay connections";
        return false;
    }
    if (impl_->relays.contains(request.route.connectionId)) {
        errorMessage = "relay connection identifier is already pending";
        return false;
    }
    Impl::RelayConnection relay;
    relay.request = request;
    RuntimeInternetTlsClientConfig tls = impl_->config.relayTls;
    tls.endpoint = request.endpoint;
    RuntimeInternetAuthenticatedTcpStreamIdentity identity = streamIdentity(
        impl_->config.serviceId,
        impl_->config.peerId,
        request.route
    );
    if (!relay.connector.begin(
            std::move(tls),
            std::move(identity),
            monotonicMilliseconds(),
            errorMessage
        )) {
        return false;
    }
    impl_->relays.emplace(
        request.route.connectionId,
        std::move(relay)
    );
    return true;
}

std::vector<RuntimeInternetServiceEvent>
RuntimeInternetServiceTransport::poll(int timeoutMilliseconds) {
    std::vector<RuntimeInternetServiceEvent> events;
    if (!impl_ || !impl_->opened) return events;
    const std::uint64_t started = monotonicMilliseconds();
    const std::uint64_t deadline = deadlineAfter(
        started,
        static_cast<std::uint32_t>(std::max(0, timeoutMilliseconds))
    );
    do {
        bool progressed = false;
        const std::uint64_t now = monotonicMilliseconds();
        (void)impl_->stepCoordinator(now, events, progressed);
        if (impl_->coordinatorStage == CoordinatorStage::Failed) break;
        for (auto iterator = impl_->relays.begin();
             iterator != impl_->relays.end();) {
            bool completed = false;
            (void)impl_->stepRelay(
                iterator->second,
                now,
                events,
                progressed,
                completed
            );
            if (completed) {
                iterator = impl_->relays.erase(iterator);
            } else {
                ++iterator;
            }
        }
        if (!events.empty() || timeoutMilliseconds <= 0
            || monotonicMilliseconds() >= deadline) {
            break;
        }
        if (!progressed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } while (true);
    return events;
}

void RuntimeInternetServiceTransport::close() noexcept {
    if (impl_) impl_->close();
}

bool RuntimeInternetServiceTransport::coordinatorAuthenticated()
    const noexcept {
    return impl_ && impl_->coordinatorStage == CoordinatorStage::Ready;
}

std::size_t RuntimeInternetServiceTransport::pendingRelayCount()
    const noexcept {
    return impl_ ? impl_->relays.size() : 0;
}
