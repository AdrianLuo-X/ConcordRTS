#include "network/session/RuntimeLanTransport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using RuntimeNativeSocket = SOCKET;
using RuntimeSocketLength = int;
constexpr RuntimeNativeSocket runtimeInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using RuntimeNativeSocket = int;
using RuntimeSocketLength = socklen_t;
constexpr RuntimeNativeSocket runtimeInvalidSocket = -1;
#endif

namespace {

constexpr std::uint32_t packetMagic = 0x43525453U; // CRTS
constexpr std::size_t packetHeaderBytes = 12;
constexpr std::uint32_t discoveryMagic = 0x434C414EU; // CLAN
constexpr std::uint8_t discoveryProbeKind = 1;
constexpr std::uint8_t discoveryAdvertisementKind = 2;
constexpr std::size_t discoveryHeaderBytes = 8;
constexpr std::size_t maximumDiscoveryDatagramBytes = 1400;
constexpr std::size_t maximumRoomIdBytes = 64;
constexpr std::size_t maximumRoomNameBytes = 96;
constexpr std::size_t maximumHostNameBytes = 64;
constexpr std::size_t maximumFingerprintBytes = 256;
constexpr std::size_t maximumDatagramsPerPoll = 256;
constexpr std::size_t maximumAcceptsPerPoll = 64;
constexpr std::size_t maximumPacketEventsPerPeerPerPoll = 256;
constexpr std::size_t maximumPacketEventsPerServerPoll = 4096;
constexpr std::size_t maximumDiscoveryRepliesPerSecond = 64;
constexpr std::uint64_t retainedRoomMilliseconds = 30000;
constexpr std::size_t maximumWriteBytesPerPeerPerPoll = 256U * 1024U;

#ifdef _WIN32
constexpr int runtimeSendFlags = 0;
#elif defined(MSG_NOSIGNAL)
constexpr int runtimeSendFlags = MSG_NOSIGNAL;
#else
constexpr int runtimeSendFlags = 0;
#endif

std::uint64_t monotonicMilliseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

class RuntimeSocketSystem {
public:
    RuntimeSocketSystem() {
#ifdef _WIN32
        WSADATA data{};
        initialized_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        initialized_ = true;
#endif
    }

    ~RuntimeSocketSystem() {
#ifdef _WIN32
        if (initialized_) WSACleanup();
#endif
    }

    bool initialized() const { return initialized_; }

private:
    bool initialized_ = false;
};

int lastSocketError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool socketWouldBlock(int code) {
#ifdef _WIN32
    return code == WSAEWOULDBLOCK || code == WSAEINPROGRESS;
#else
    return code == EAGAIN || code == EWOULDBLOCK || code == EINPROGRESS;
#endif
}

bool socketInterrupted(int code) {
#ifdef _WIN32
    return code == WSAEINTR;
#else
    return code == EINTR;
#endif
}

bool socketDatagramTooLarge(int code) {
#ifdef _WIN32
    return code == WSAEMSGSIZE;
#else
    (void)code;
    return false;
#endif
}

bool socketUdpReset(int code) {
#ifdef _WIN32
    // Winsock surfaces an ICMP port-unreachable from an earlier UDP send as
    // WSAECONNRESET on recvfrom. It belongs to one datagram/probe and must not
    // tear down the discovery service.
    return code == WSAECONNRESET;
#else
    (void)code;
    return false;
#endif
}

std::string socketErrorText(const char* action, int code) {
    std::ostringstream output;
    output << action << " failed (socket error " << code << ")";
    return output.str();
}

void closeSocket(RuntimeNativeSocket& socket) {
    if (socket == runtimeInvalidSocket) return;
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
    socket = runtimeInvalidSocket;
}

bool socketFitsSelect(RuntimeNativeSocket socket) {
#ifdef _WIN32
    return socket != runtimeInvalidSocket;
#else
    return socket >= 0 && socket < FD_SETSIZE;
#endif
}

bool setNonBlocking(RuntimeNativeSocket socket, std::string& errorMessage) {
#ifdef _WIN32
    u_long enabled = 1;
    if (ioctlsocket(socket, FIONBIO, &enabled) == SOCKET_ERROR) {
        errorMessage = socketErrorText("ioctlsocket(FIONBIO)", lastSocketError());
        return false;
    }
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        errorMessage = socketErrorText("fcntl(O_NONBLOCK)", lastSocketError());
        return false;
    }
#endif
    return true;
}

void setTcpNoDelay(RuntimeNativeSocket socket) {
    const int enabled = 1;
    (void)setsockopt(
        socket,
        IPPROTO_TCP,
        TCP_NODELAY,
        reinterpret_cast<const char*>(&enabled),
        static_cast<RuntimeSocketLength>(sizeof(enabled))
    );
}

void setNoSigPipe(RuntimeNativeSocket socket) {
#if !defined(_WIN32) && !defined(MSG_NOSIGNAL) && defined(SO_NOSIGPIPE)
    const int enabled = 1;
    (void)setsockopt(
        socket,
        SOL_SOCKET,
        SO_NOSIGPIPE,
        reinterpret_cast<const char*>(&enabled),
        static_cast<RuntimeSocketLength>(sizeof(enabled))
    );
#else
    (void)socket;
#endif
}

timeval timeoutValue(int milliseconds) {
    const int bounded = std::max(0, milliseconds);
    timeval value{};
    value.tv_sec = bounded / 1000;
    value.tv_usec = (bounded % 1000) * 1000;
    return value;
}

int selectSockets(
    RuntimeNativeSocket maximum,
    fd_set* reads,
    fd_set* writes,
    int timeoutMilliseconds
) {
    timeval timeout = timeoutValue(timeoutMilliseconds);
#ifdef _WIN32
    (void)maximum;
    return select(0, reads, writes, nullptr, &timeout);
#else
    return select(maximum + 1, reads, writes, nullptr, &timeout);
#endif
}

bool waitForConnect(
    RuntimeNativeSocket socket,
    int timeoutMilliseconds,
    std::string& errorMessage
) {
    fd_set writes;
    fd_set exceptions;
    FD_ZERO(&writes);
    FD_ZERO(&exceptions);
    FD_SET(socket, &writes);
    FD_SET(socket, &exceptions);
    timeval timeout = timeoutValue(timeoutMilliseconds);
#ifdef _WIN32
    const int selected = select(0, nullptr, &writes, &exceptions, &timeout);
#else
    const int selected = select(
        socket + 1,
        nullptr,
        &writes,
        &exceptions,
        &timeout
    );
#endif
    if (selected < 0) {
        errorMessage = socketErrorText("select(connect)", lastSocketError());
        return false;
    }
    if (selected == 0) {
        errorMessage = "socket connect timed out";
        return false;
    }
    if (!FD_ISSET(socket, &writes) && !FD_ISSET(socket, &exceptions)) {
        errorMessage = "socket connect readiness was not reported";
        return false;
    }
    return true;
}

void appendU16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void appendU32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

bool readU16(
    const std::vector<std::uint8_t>& input,
    std::size_t& offset,
    std::uint16_t& value
) {
    if (offset + 2 > input.size()) return false;
    value = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(input[offset]) << 8)
        | input[offset + 1]
    );
    offset += 2;
    return true;
}

bool readU32(
    const std::vector<std::uint8_t>& input,
    std::size_t& offset,
    std::uint32_t& value
) {
    if (offset + 4 > input.size()) return false;
    value = (static_cast<std::uint32_t>(input[offset]) << 24)
        | (static_cast<std::uint32_t>(input[offset + 1]) << 16)
        | (static_cast<std::uint32_t>(input[offset + 2]) << 8)
        | static_cast<std::uint32_t>(input[offset + 3]);
    offset += 4;
    return true;
}

bool validBoundedText(
    const std::string& value,
    std::size_t maximum,
    bool allowEmpty = false
) {
    return (allowEmpty || !value.empty())
        && value.size() <= maximum
        && std::none_of(value.begin(), value.end(), [](unsigned char character) {
            return character == '\0' || character == '\r' || character == '\n';
        });
}

bool validAdvertisement(
    const RuntimeLanRoomAdvertisement& advertisement,
    std::string& errorMessage
) {
    if (!validBoundedText(advertisement.roomId, maximumRoomIdBytes)
        || !validBoundedText(advertisement.roomName, maximumRoomNameBytes)
        || !validBoundedText(advertisement.hostName, maximumHostNameBytes)
        || !validBoundedText(
            advertisement.contentFingerprint,
            maximumFingerprintBytes
        )) {
        errorMessage = "LAN advertisement contains an invalid text field";
        return false;
    }
    if (advertisement.protocolVersion == 0
        || advertisement.sessionPort == 0
        || advertisement.maximumPlayers == 0
        || advertisement.playerCount == 0
        || advertisement.playerCount > advertisement.maximumPlayers) {
        errorMessage = "LAN advertisement contains invalid protocol, port, or player counts";
        return false;
    }
    return true;
}

bool appendString(
    std::vector<std::uint8_t>& output,
    const std::string& value,
    std::size_t maximum
) {
    if (value.size() > maximum
        || value.size() > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }
    appendU16(output, static_cast<std::uint16_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
    return true;
}

bool readString(
    const std::vector<std::uint8_t>& input,
    std::size_t& offset,
    std::size_t maximum,
    std::string& value
) {
    std::uint16_t length = 0;
    if (!readU16(input, offset, length)
        || length > maximum
        || offset + length > input.size()) {
        return false;
    }
    value.assign(
        reinterpret_cast<const char*>(input.data() + offset),
        static_cast<std::size_t>(length)
    );
    offset += length;
    return true;
}

std::vector<std::uint8_t> discoveryProbe() {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(discoveryHeaderBytes);
    appendU32(bytes, discoveryMagic);
    appendU16(bytes, runtimeLanProtocolVersion);
    bytes.push_back(discoveryProbeKind);
    bytes.push_back(0);
    return bytes;
}

bool isDiscoveryProbe(const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    return bytes.size() == discoveryHeaderBytes
        && readU32(bytes, offset, magic)
        && readU16(bytes, offset, version)
        && magic == discoveryMagic
        && version == runtimeLanProtocolVersion
        && bytes[offset++] == discoveryProbeKind
        && bytes[offset] == 0;
}

bool encodeAdvertisement(
    const RuntimeLanRoomAdvertisement& advertisement,
    std::vector<std::uint8_t>& bytes,
    std::string& errorMessage
) {
    if (!validAdvertisement(advertisement, errorMessage)) return false;
    bytes.clear();
    appendU32(bytes, discoveryMagic);
    appendU16(bytes, runtimeLanProtocolVersion);
    bytes.push_back(discoveryAdvertisementKind);
    bytes.push_back(0);
    appendU16(bytes, advertisement.protocolVersion);
    appendU16(bytes, advertisement.sessionPort);
    bytes.push_back(advertisement.playerCount);
    bytes.push_back(advertisement.maximumPlayers);
    bytes.push_back(advertisement.passwordProtected ? 1 : 0);
    bytes.push_back(0);
    if (!appendString(bytes, advertisement.roomId, maximumRoomIdBytes)
        || !appendString(bytes, advertisement.roomName, maximumRoomNameBytes)
        || !appendString(bytes, advertisement.hostName, maximumHostNameBytes)
        || !appendString(
            bytes,
            advertisement.contentFingerprint,
            maximumFingerprintBytes
        )
        || bytes.size() > maximumDiscoveryDatagramBytes) {
        errorMessage = "LAN advertisement is too large";
        bytes.clear();
        return false;
    }
    return true;
}

bool decodeAdvertisement(
    const std::vector<std::uint8_t>& bytes,
    RuntimeLanRoomAdvertisement& advertisement
) {
    std::size_t offset = 0;
    std::uint32_t magic = 0;
    std::uint16_t envelopeVersion = 0;
    std::uint16_t protocolVersion = 0;
    std::uint16_t sessionPort = 0;
    if (!readU32(bytes, offset, magic)
        || !readU16(bytes, offset, envelopeVersion)
        || magic != discoveryMagic
        || envelopeVersion != runtimeLanProtocolVersion
        || offset + 2 > bytes.size()
        || bytes[offset++] != discoveryAdvertisementKind
        || bytes[offset++] != 0
        || !readU16(bytes, offset, protocolVersion)
        || !readU16(bytes, offset, sessionPort)
        || offset + 4 > bytes.size()) {
        return false;
    }
    RuntimeLanRoomAdvertisement parsed;
    parsed.protocolVersion = protocolVersion;
    parsed.sessionPort = sessionPort;
    parsed.playerCount = bytes[offset++];
    parsed.maximumPlayers = bytes[offset++];
    parsed.passwordProtected = bytes[offset++] != 0;
    if (bytes[offset++] != 0
        || !readString(bytes, offset, maximumRoomIdBytes, parsed.roomId)
        || !readString(bytes, offset, maximumRoomNameBytes, parsed.roomName)
        || !readString(bytes, offset, maximumHostNameBytes, parsed.hostName)
        || !readString(
            bytes,
            offset,
            maximumFingerprintBytes,
            parsed.contentFingerprint
        )
        || offset != bytes.size()) {
        return false;
    }
    std::string ignored;
    if (!validAdvertisement(parsed, ignored)) return false;
    advertisement = std::move(parsed);
    return true;
}

std::string numericAddress(const sockaddr* address, RuntimeSocketLength length) {
    std::array<char, NI_MAXHOST> host{};
    if (getnameinfo(
            address,
            length,
            host.data(),
            static_cast<RuntimeSocketLength>(host.size()),
            nullptr,
            0,
            NI_NUMERICHOST
        ) != 0) {
        return {};
    }
    return host.data();
}

std::uint16_t boundPort(RuntimeNativeSocket socket) {
    sockaddr_storage address{};
    RuntimeSocketLength length = static_cast<RuntimeSocketLength>(sizeof(address));
    if (getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return 0;
    }
    if (address.ss_family == AF_INET) {
        return ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port);
    }
    if (address.ss_family == AF_INET6) {
        return ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port);
    }
    return 0;
}

bool bindIpv4Socket(
    RuntimeNativeSocket socket,
    std::uint16_t port,
    RuntimeTcpBindScope bindScope,
    std::string& errorMessage
) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(
        bindScope == RuntimeTcpBindScope::LoopbackIpv4
            ? INADDR_LOOPBACK
            : INADDR_ANY
    );
    address.sin_port = htons(port);
    if (::bind(
            socket,
            reinterpret_cast<const sockaddr*>(&address),
            static_cast<RuntimeSocketLength>(sizeof(address))
        ) != 0) {
        errorMessage = socketErrorText("bind", lastSocketError());
        return false;
    }
    return true;
}

bool bindIpv4Socket(
    RuntimeNativeSocket socket,
    std::uint16_t port,
    std::string& errorMessage
) {
    return bindIpv4Socket(
        socket,
        port,
        RuntimeTcpBindScope::AnyIpv4,
        errorMessage
    );
}

RuntimeNativeSocket createIpv4Socket(
    int type,
    int protocol,
    std::string& errorMessage
) {
    RuntimeNativeSocket socket = ::socket(AF_INET, type, protocol);
    if (socket == runtimeInvalidSocket) {
        errorMessage = socketErrorText("socket", lastSocketError());
    }
    return socket;
}

struct RuntimeTcpPeerState {
    RuntimeNativeSocket socket = runtimeInvalidSocket;
    std::string address;
    std::vector<std::uint8_t> receiveBuffer;
    std::deque<std::vector<std::uint8_t>> sendQueue;
    std::size_t sendOffset = 0;
    std::size_t queuedBytes = 0;
};

bool decodeRuntimeNetworkPacketView(
    const std::uint8_t* bytes,
    std::size_t byteCount,
    RuntimeNetworkPacket& packet,
    std::size_t& consumed,
    bool& complete,
    std::string& errorMessage
) {
    consumed = 0;
    complete = false;
    errorMessage.clear();
    if (byteCount < packetHeaderBytes) return true;

    const std::uint32_t magic =
        (static_cast<std::uint32_t>(bytes[0]) << 24U)
        | (static_cast<std::uint32_t>(bytes[1]) << 16U)
        | (static_cast<std::uint32_t>(bytes[2]) << 8U)
        | static_cast<std::uint32_t>(bytes[3]);
    const std::uint16_t version = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[4]) << 8U)
        | static_cast<std::uint16_t>(bytes[5])
    );
    const std::uint16_t type = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[6]) << 8U)
        | static_cast<std::uint16_t>(bytes[7])
    );
    const std::uint32_t payloadSize =
        (static_cast<std::uint32_t>(bytes[8]) << 24U)
        | (static_cast<std::uint32_t>(bytes[9]) << 16U)
        | (static_cast<std::uint32_t>(bytes[10]) << 8U)
        | static_cast<std::uint32_t>(bytes[11]);
    if (magic != packetMagic) {
        errorMessage = "network packet magic is invalid";
        return false;
    }
    if (version != runtimeLanProtocolVersion) {
        errorMessage = "network packet envelope version is incompatible";
        return false;
    }
    if (type == 0) {
        errorMessage = "network packet type zero is reserved";
        return false;
    }
    if (payloadSize > runtimeLanMaximumPacketBytes) {
        errorMessage = "network packet payload exceeds the size limit";
        return false;
    }
    const std::size_t frameSize = packetHeaderBytes
        + static_cast<std::size_t>(payloadSize);
    if (byteCount < frameSize) return true;

    RuntimeNetworkPacket parsed;
    parsed.type = type;
    parsed.payload.assign(bytes + packetHeaderBytes, bytes + frameSize);
    packet = std::move(parsed);
    consumed = frameSize;
    complete = true;
    return true;
}

void appendFramingError(
    std::uint64_t peerId,
    const RuntimeTcpPeerState& peer,
    std::string message,
    std::vector<RuntimeTcpEvent>& events
) {
    RuntimeTcpEvent event;
    event.kind = RuntimeTcpEventKind::Error;
    event.peerId = peerId;
    event.peerAddress = peer.address;
    event.message = std::move(message);
    events.push_back(std::move(event));
}

void appendDisconnectedEvent(
    std::uint64_t peerId,
    const RuntimeTcpPeerState& peer,
    std::string message,
    std::vector<RuntimeTcpEvent>& events
) {
    RuntimeTcpEvent event;
    event.kind = RuntimeTcpEventKind::Disconnected;
    event.peerId = peerId;
    event.peerAddress = peer.address;
    event.message = std::move(message);
    events.push_back(std::move(event));
}

std::size_t parseReceivedPackets(
    std::uint64_t peerId,
    RuntimeTcpPeerState& peer,
    std::vector<RuntimeTcpEvent>& events,
    std::size_t packetBudget,
    bool& disconnected
) {
    disconnected = false;
    std::size_t cursor = 0;
    std::size_t decodedPackets = 0;
    while (cursor < peer.receiveBuffer.size()
        && decodedPackets < packetBudget) {
        RuntimeNetworkPacket packet;
        std::size_t consumed = 0;
        bool complete = false;
        std::string decodeError;
        if (!decodeRuntimeNetworkPacketView(
                peer.receiveBuffer.data() + cursor,
                peer.receiveBuffer.size() - cursor,
                packet,
                consumed,
                complete,
                decodeError
            )) {
            appendFramingError(
                peerId,
                peer,
                std::move(decodeError),
                events
            );
            appendDisconnectedEvent(
                peerId,
                peer,
                "peer disconnected after a framing error",
                events
            );
            disconnected = true;
            break;
        }
        if (!complete) break;
        RuntimeTcpEvent event;
        event.kind = RuntimeTcpEventKind::Packet;
        event.peerId = peerId;
        event.peerAddress = peer.address;
        event.packet = std::move(packet);
        events.push_back(std::move(event));
        cursor += consumed;
        ++decodedPackets;
    }
    if (cursor != 0) {
        peer.receiveBuffer.erase(
            peer.receiveBuffer.begin(),
            peer.receiveBuffer.begin() + static_cast<std::ptrdiff_t>(cursor)
        );
    }
    // Check only the incomplete suffix. The buffer may briefly contain one
    // complete maximum-size frame followed by bytes from the next frame.
    if (!disconnected
        && peer.receiveBuffer.size()
            > runtimeLanMaximumPacketBytes + packetHeaderBytes) {
        appendFramingError(
            peerId,
            peer,
            "peer exceeded the maximum framed packet size",
            events
        );
        appendDisconnectedEvent(
            peerId,
            peer,
            "peer disconnected after exceeding the frame limit",
            events
        );
        disconnected = true;
    }
    return decodedPackets;
}

std::size_t appendReceivedPackets(
    std::uint64_t peerId,
    RuntimeTcpPeerState& peer,
    std::vector<RuntimeTcpEvent>& events,
    std::size_t packetBudget,
    bool& disconnected
) {
    disconnected = false;
    std::array<std::uint8_t, 16384> buffer{};
    const int received = recv(
        peer.socket,
        reinterpret_cast<char*>(buffer.data()),
        static_cast<int>(buffer.size()),
        0
    );
    if (received == 0) {
        if (!peer.receiveBuffer.empty()) {
            appendFramingError(
                peerId,
                peer,
                "peer closed with an incomplete framed packet",
                events
            );
        }
        appendDisconnectedEvent(
            peerId,
            peer,
            "peer closed the connection",
            events
        );
        disconnected = true;
        return 0;
    }
    if (received < 0) {
        const int code = lastSocketError();
        if (socketWouldBlock(code) || socketInterrupted(code)) return 0;
        RuntimeTcpEvent event;
        event.kind = RuntimeTcpEventKind::Error;
        event.peerId = peerId;
        event.peerAddress = peer.address;
        event.message = socketErrorText("recv", code);
        events.push_back(std::move(event));
        appendDisconnectedEvent(
            peerId,
            peer,
            "peer disconnected after a receive failure",
            events
        );
        disconnected = true;
        return 0;
    }

    peer.receiveBuffer.insert(
        peer.receiveBuffer.end(),
        buffer.begin(),
        buffer.begin() + received
    );
    return parseReceivedPackets(
        peerId,
        peer,
        events,
        packetBudget,
        disconnected
    );
}

bool queueFramedBytes(
    RuntimeTcpPeerState& peer,
    std::vector<std::uint8_t> bytes,
    std::string& errorMessage
) {
    if (bytes.size() > runtimeLanMaximumQueuedBytesPerPeer
        || peer.queuedBytes
            > runtimeLanMaximumQueuedBytesPerPeer - bytes.size()) {
        errorMessage = "TCP peer send queue limit reached";
        return false;
    }
    peer.queuedBytes += bytes.size();
    peer.sendQueue.push_back(std::move(bytes));
    return true;
}

bool queueFramedPacket(
    RuntimeTcpPeerState& peer,
    const RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    std::vector<std::uint8_t> bytes;
    if (!encodeRuntimeNetworkPacket(packet, bytes, errorMessage)) return false;
    return queueFramedBytes(peer, std::move(bytes), errorMessage);
}

bool flushQueuedPackets(
    std::uint64_t peerId,
    RuntimeTcpPeerState& peer,
    std::vector<RuntimeTcpEvent>& events,
    bool& disconnected
) {
    disconnected = false;
    std::size_t writtenThisPoll = 0;
    while (!peer.sendQueue.empty()
        && writtenThisPoll < maximumWriteBytesPerPeerPerPoll) {
        const std::vector<std::uint8_t>& front = peer.sendQueue.front();
        if (peer.sendOffset >= front.size()) {
            peer.sendQueue.pop_front();
            peer.sendOffset = 0;
            continue;
        }
        const std::size_t remaining = front.size() - peer.sendOffset;
        const std::size_t budget = maximumWriteBytesPerPeerPerPoll
            - writtenThisPoll;
        const int request = static_cast<int>(std::min(remaining, budget));
        const int result = ::send(
            peer.socket,
            reinterpret_cast<const char*>(front.data() + peer.sendOffset),
            request,
            runtimeSendFlags
        );
        if (result > 0) {
            const std::size_t count = static_cast<std::size_t>(result);
            peer.sendOffset += count;
            peer.queuedBytes -= count;
            writtenThisPoll += count;
            if (peer.sendOffset == front.size()) {
                peer.sendQueue.pop_front();
                peer.sendOffset = 0;
            }
            continue;
        }
        if (result < 0) {
            const int code = lastSocketError();
            if (socketInterrupted(code)) continue;
            if (socketWouldBlock(code)) return true;
            RuntimeTcpEvent event;
            event.kind = RuntimeTcpEventKind::Error;
            event.peerId = peerId;
            event.peerAddress = peer.address;
            event.message = socketErrorText("send", code);
            events.push_back(std::move(event));
        } else {
            RuntimeTcpEvent event;
            event.kind = RuntimeTcpEventKind::Error;
            event.peerId = peerId;
            event.peerAddress = peer.address;
            event.message = "socket closed while flushing queued packets";
            events.push_back(std::move(event));
        }
        disconnected = true;
        return false;
    }
    return true;
}

} // namespace

bool encodeRuntimeNetworkPacket(
    const RuntimeNetworkPacket& packet,
    std::vector<std::uint8_t>& bytes,
    std::string& errorMessage
) {
    errorMessage.clear();
    bytes.clear();
    if (packet.type == 0) {
        errorMessage = "network packet type zero is reserved";
        return false;
    }
    if (packet.payload.size() > runtimeLanMaximumPacketBytes) {
        errorMessage = "network packet payload exceeds the size limit";
        return false;
    }
    bytes.reserve(packetHeaderBytes + packet.payload.size());
    appendU32(bytes, packetMagic);
    appendU16(bytes, runtimeLanProtocolVersion);
    appendU16(bytes, packet.type);
    appendU32(bytes, static_cast<std::uint32_t>(packet.payload.size()));
    bytes.insert(bytes.end(), packet.payload.begin(), packet.payload.end());
    return true;
}

bool decodeRuntimeNetworkPacket(
    const std::vector<std::uint8_t>& bytes,
    RuntimeNetworkPacket& packet,
    std::size_t& consumed,
    bool& complete,
    std::string& errorMessage
) {
    return decodeRuntimeNetworkPacketView(
        bytes.data(),
        bytes.size(),
        packet,
        consumed,
        complete,
        errorMessage
    );
}

struct RuntimeLanDiscoveryHost::Impl {
    RuntimeSocketSystem sockets;
    RuntimeNativeSocket socket = runtimeInvalidSocket;
    RuntimeLanRoomAdvertisement advertisement;
    std::uint16_t boundPort = 0;
    std::uint64_t lastReplyTokenRefillMilliseconds = 0;
    double replyTokens = static_cast<double>(maximumDiscoveryRepliesPerSecond);
};

RuntimeLanDiscoveryHost::RuntimeLanDiscoveryHost()
    : impl_(std::make_unique<Impl>()) {}
RuntimeLanDiscoveryHost::~RuntimeLanDiscoveryHost() { close(); }
RuntimeLanDiscoveryHost::RuntimeLanDiscoveryHost(RuntimeLanDiscoveryHost&&) noexcept = default;
RuntimeLanDiscoveryHost& RuntimeLanDiscoveryHost::operator=(
    RuntimeLanDiscoveryHost&& other
) noexcept {
    if (this != &other) {
        close();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool RuntimeLanDiscoveryHost::open(
    std::uint16_t discoveryPort,
    RuntimeLanRoomAdvertisement advertisement,
    std::string& errorMessage
) {
    close();
    if (!impl_) impl_ = std::make_unique<Impl>();
    errorMessage.clear();
    if (!impl_->sockets.initialized()) {
        errorMessage = "socket runtime initialization failed";
        return false;
    }
    if (!validAdvertisement(advertisement, errorMessage)) return false;
    RuntimeNativeSocket socket = createIpv4Socket(SOCK_DGRAM, IPPROTO_UDP, errorMessage);
    if (socket == runtimeInvalidSocket) return false;
    if (!socketFitsSelect(socket)) {
        errorMessage = "discovery socket exceeds select descriptor capacity";
        closeSocket(socket);
        return false;
    }
    const int enabled = 1;
    (void)setsockopt(
        socket,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&enabled),
        static_cast<RuntimeSocketLength>(sizeof(enabled))
    );
    if (!bindIpv4Socket(socket, discoveryPort, errorMessage)
        || !setNonBlocking(socket, errorMessage)) {
        closeSocket(socket);
        return false;
    }
    impl_->socket = socket;
    impl_->boundPort = ::boundPort(socket);
    impl_->advertisement = std::move(advertisement);
    impl_->lastReplyTokenRefillMilliseconds = monotonicMilliseconds();
    impl_->replyTokens = static_cast<double>(maximumDiscoveryRepliesPerSecond);
    return true;
}

bool RuntimeLanDiscoveryHost::updateAdvertisement(
    RuntimeLanRoomAdvertisement advertisement,
    std::string& errorMessage
) {
    if (!isOpen()) {
        errorMessage = "LAN discovery host is not open";
        return false;
    }
    if (!validAdvertisement(advertisement, errorMessage)) return false;
    impl_->advertisement = std::move(advertisement);
    errorMessage.clear();
    return true;
}

int RuntimeLanDiscoveryHost::poll(
    int timeoutMilliseconds,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (!isOpen()) {
        errorMessage = "LAN discovery host is not open";
        return -1;
    }
    fd_set reads;
    FD_ZERO(&reads);
    FD_SET(impl_->socket, &reads);
    const int selected = selectSockets(
        impl_->socket,
        &reads,
        nullptr,
        timeoutMilliseconds
    );
    if (selected < 0) {
        errorMessage = socketErrorText("select(discovery host)", lastSocketError());
        return -1;
    }
    if (selected == 0) return 0;
    std::vector<std::uint8_t> response;
    if (!encodeAdvertisement(impl_->advertisement, response, errorMessage)) {
        return -1;
    }
    int replies = 0;
    for (std::size_t processed = 0;
         processed < maximumDatagramsPerPoll;
         ++processed) {
        std::array<std::uint8_t, maximumDiscoveryDatagramBytes> buffer{};
        sockaddr_storage source{};
        RuntimeSocketLength sourceLength = static_cast<RuntimeSocketLength>(sizeof(source));
        const int received = recvfrom(
            impl_->socket,
            reinterpret_cast<char*>(buffer.data()),
            static_cast<int>(buffer.size()),
            0,
            reinterpret_cast<sockaddr*>(&source),
            &sourceLength
        );
        if (received < 0) {
            const int code = lastSocketError();
            if (socketWouldBlock(code)) break;
            if (socketInterrupted(code)) continue;
            if (socketDatagramTooLarge(code)) continue;
            if (socketUdpReset(code)) continue;
            errorMessage = socketErrorText("recvfrom(discovery host)", code);
            return -1;
        }
        const std::vector<std::uint8_t> request(
            buffer.begin(),
            buffer.begin() + received
        );
        if (!isDiscoveryProbe(request)) continue;
        const std::uint64_t now = monotonicMilliseconds();
        if (now < impl_->lastReplyTokenRefillMilliseconds) {
            impl_->replyTokens = static_cast<double>(
                maximumDiscoveryRepliesPerSecond
            );
        } else {
            const std::uint64_t elapsed =
                now - impl_->lastReplyTokenRefillMilliseconds;
            impl_->replyTokens = std::min(
                static_cast<double>(maximumDiscoveryRepliesPerSecond),
                impl_->replyTokens
                    + static_cast<double>(elapsed)
                        * static_cast<double>(maximumDiscoveryRepliesPerSecond)
                        / 1000.0
            );
        }
        impl_->lastReplyTokenRefillMilliseconds = now;
        if (impl_->replyTokens < 1.0) continue;
        const int sent = sendto(
            impl_->socket,
            reinterpret_cast<const char*>(response.data()),
            static_cast<int>(response.size()),
            0,
            reinterpret_cast<const sockaddr*>(&source),
            sourceLength
        );
        if (sent == static_cast<int>(response.size())) {
            ++replies;
            impl_->replyTokens -= 1.0;
        }
    }
    return replies;
}

void RuntimeLanDiscoveryHost::close() {
    if (!impl_) return;
    closeSocket(impl_->socket);
    impl_->boundPort = 0;
    impl_->lastReplyTokenRefillMilliseconds = 0;
    impl_->replyTokens = static_cast<double>(maximumDiscoveryRepliesPerSecond);
}
bool RuntimeLanDiscoveryHost::isOpen() const {
    return impl_ && impl_->socket != runtimeInvalidSocket;
}
std::uint16_t RuntimeLanDiscoveryHost::port() const {
    return impl_ ? impl_->boundPort : 0;
}

struct RuntimeLanDiscoveryBrowser::Impl {
    RuntimeSocketSystem sockets;
    RuntimeNativeSocket socket = runtimeInvalidSocket;
    std::uint16_t discoveryPort = 0;
    std::map<std::string, RuntimeLanRoomAdvertisement> rooms;
};

RuntimeLanDiscoveryBrowser::RuntimeLanDiscoveryBrowser()
    : impl_(std::make_unique<Impl>()) {}
RuntimeLanDiscoveryBrowser::~RuntimeLanDiscoveryBrowser() { close(); }
RuntimeLanDiscoveryBrowser::RuntimeLanDiscoveryBrowser(RuntimeLanDiscoveryBrowser&&) noexcept = default;
RuntimeLanDiscoveryBrowser& RuntimeLanDiscoveryBrowser::operator=(
    RuntimeLanDiscoveryBrowser&& other
) noexcept {
    if (this != &other) {
        close();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool RuntimeLanDiscoveryBrowser::open(
    std::uint16_t discoveryPort,
    std::string& errorMessage
) {
    close();
    if (!impl_) impl_ = std::make_unique<Impl>();
    errorMessage.clear();
    if (!impl_->sockets.initialized()) {
        errorMessage = "socket runtime initialization failed";
        return false;
    }
    if (discoveryPort == 0) {
        errorMessage = "LAN discovery target port must be nonzero";
        return false;
    }
    RuntimeNativeSocket socket = createIpv4Socket(SOCK_DGRAM, IPPROTO_UDP, errorMessage);
    if (socket == runtimeInvalidSocket) return false;
    if (!socketFitsSelect(socket)) {
        errorMessage = "discovery socket exceeds select descriptor capacity";
        closeSocket(socket);
        return false;
    }
    const int enabled = 1;
    if (setsockopt(
            socket,
            SOL_SOCKET,
            SO_BROADCAST,
            reinterpret_cast<const char*>(&enabled),
            static_cast<RuntimeSocketLength>(sizeof(enabled))
        ) != 0
        || !bindIpv4Socket(socket, 0, errorMessage)
        || !setNonBlocking(socket, errorMessage)) {
        if (errorMessage.empty()) {
            errorMessage = socketErrorText("setsockopt(SO_BROADCAST)", lastSocketError());
        }
        closeSocket(socket);
        return false;
    }
    impl_->socket = socket;
    impl_->discoveryPort = discoveryPort;
    return true;
}

bool RuntimeLanDiscoveryBrowser::sendProbe(
    const std::string& targetAddress,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (!isOpen()) {
        errorMessage = "LAN discovery browser is not open";
        return false;
    }
    if (targetAddress.empty()) {
        errorMessage = "LAN discovery target address is empty";
        return false;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* results = nullptr;
    const std::string portText = std::to_string(impl_->discoveryPort);
    const int lookup = getaddrinfo(
        targetAddress.c_str(),
        portText.c_str(),
        &hints,
        &results
    );
    if (lookup != 0 || results == nullptr) {
        errorMessage = "LAN discovery target address could not be resolved";
        if (results != nullptr) freeaddrinfo(results);
        return false;
    }
    const std::vector<std::uint8_t> probe = discoveryProbe();
    bool sent = false;
    for (addrinfo* result = results; result != nullptr; result = result->ai_next) {
        const int count = sendto(
            impl_->socket,
            reinterpret_cast<const char*>(probe.data()),
            static_cast<int>(probe.size()),
            0,
            result->ai_addr,
            static_cast<RuntimeSocketLength>(result->ai_addrlen)
        );
        if (count == static_cast<int>(probe.size())) {
            sent = true;
            break;
        }
    }
    freeaddrinfo(results);
    if (!sent) {
        errorMessage = socketErrorText("sendto(discovery probe)", lastSocketError());
    }
    return sent;
}

int RuntimeLanDiscoveryBrowser::poll(
    int timeoutMilliseconds,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (!isOpen()) {
        errorMessage = "LAN discovery browser is not open";
        return -1;
    }
    fd_set reads;
    FD_ZERO(&reads);
    FD_SET(impl_->socket, &reads);
    const int selected = selectSockets(
        impl_->socket,
        &reads,
        nullptr,
        timeoutMilliseconds
    );
    if (selected < 0) {
        errorMessage = socketErrorText("select(discovery browser)", lastSocketError());
        return -1;
    }
    if (selected == 0) return 0;
    int receivedAdvertisements = 0;
    const std::uint64_t now = monotonicMilliseconds();
    for (auto room = impl_->rooms.begin(); room != impl_->rooms.end();) {
        if (now >= room->second.lastSeenMilliseconds
            && now - room->second.lastSeenMilliseconds
                > retainedRoomMilliseconds) {
            room = impl_->rooms.erase(room);
        } else {
            ++room;
        }
    }
    for (std::size_t processed = 0;
         processed < maximumDatagramsPerPoll;
         ++processed) {
        std::array<std::uint8_t, maximumDiscoveryDatagramBytes> buffer{};
        sockaddr_storage source{};
        RuntimeSocketLength sourceLength = static_cast<RuntimeSocketLength>(sizeof(source));
        const int received = recvfrom(
            impl_->socket,
            reinterpret_cast<char*>(buffer.data()),
            static_cast<int>(buffer.size()),
            0,
            reinterpret_cast<sockaddr*>(&source),
            &sourceLength
        );
        if (received < 0) {
            const int code = lastSocketError();
            if (socketWouldBlock(code)) break;
            if (socketInterrupted(code)) continue;
            if (socketDatagramTooLarge(code)) continue;
            if (socketUdpReset(code)) continue;
            errorMessage = socketErrorText("recvfrom(discovery browser)", code);
            return -1;
        }
        RuntimeLanRoomAdvertisement advertisement;
        const std::vector<std::uint8_t> datagram(
            buffer.begin(),
            buffer.begin() + received
        );
        if (!decodeAdvertisement(datagram, advertisement)) continue;
        advertisement.hostAddress = numericAddress(
            reinterpret_cast<const sockaddr*>(&source),
            sourceLength
        );
        if (advertisement.hostAddress.empty()) continue;
        advertisement.lastSeenMilliseconds = now;
        // roomId is stable across a host's session-port rebinding. Source IP
        // prevents an unrelated machine from overwriting the same room id.
        const std::string key = advertisement.roomId + "\n"
            + advertisement.hostAddress;
        if (impl_->rooms.find(key) == impl_->rooms.end()
            && impl_->rooms.size() >= runtimeLanMaximumDiscoveredRooms) {
            const auto oldest = std::min_element(
                impl_->rooms.begin(),
                impl_->rooms.end(),
                [](const auto& left, const auto& right) {
                    return left.second.lastSeenMilliseconds
                        < right.second.lastSeenMilliseconds;
                }
            );
            if (oldest != impl_->rooms.end()) impl_->rooms.erase(oldest);
        }
        impl_->rooms[key] = std::move(advertisement);
        ++receivedAdvertisements;
    }
    return receivedAdvertisements;
}

std::vector<RuntimeLanRoomAdvertisement> RuntimeLanDiscoveryBrowser::rooms(
    std::uint64_t maximumAgeMilliseconds
) const {
    std::vector<RuntimeLanRoomAdvertisement> result;
    if (!impl_) return result;
    const std::uint64_t now = monotonicMilliseconds();
    for (const auto& [key, room] : impl_->rooms) {
        (void)key;
        if (now >= room.lastSeenMilliseconds
            && now - room.lastSeenMilliseconds <= maximumAgeMilliseconds) {
            result.push_back(room);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.roomName != right.roomName) return left.roomName < right.roomName;
        if (left.hostAddress != right.hostAddress) {
            return left.hostAddress < right.hostAddress;
        }
        return left.sessionPort < right.sessionPort;
    });
    return result;
}

void RuntimeLanDiscoveryBrowser::close() {
    if (!impl_) return;
    closeSocket(impl_->socket);
    impl_->discoveryPort = 0;
    impl_->rooms.clear();
}
bool RuntimeLanDiscoveryBrowser::isOpen() const {
    return impl_ && impl_->socket != runtimeInvalidSocket;
}

struct RuntimeTcpServer::Impl {
    RuntimeSocketSystem sockets;
    RuntimeNativeSocket listener = runtimeInvalidSocket;
    std::uint16_t boundPort = 0;
    std::size_t maximumClients = 0;
    std::uint64_t nextPeerId = 1;
    std::map<std::uint64_t, RuntimeTcpPeerState> peers;
    std::vector<RuntimeTcpEvent> pendingEvents;
};

RuntimeTcpServer::RuntimeTcpServer() : impl_(std::make_unique<Impl>()) {}
RuntimeTcpServer::~RuntimeTcpServer() { close(); }
RuntimeTcpServer::RuntimeTcpServer(RuntimeTcpServer&&) noexcept = default;
RuntimeTcpServer& RuntimeTcpServer::operator=(RuntimeTcpServer&& other) noexcept {
    if (this != &other) {
        close();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool RuntimeTcpServer::open(
    std::uint16_t port,
    std::size_t maximumClients,
    std::string& errorMessage
) {
    return open(
        port,
        maximumClients,
        RuntimeTcpBindScope::AnyIpv4,
        errorMessage
    );
}

bool RuntimeTcpServer::open(
    std::uint16_t port,
    std::size_t maximumClients,
    RuntimeTcpBindScope bindScope,
    std::string& errorMessage
) {
    close();
    if (!impl_) impl_ = std::make_unique<Impl>();
    errorMessage.clear();
    if (!impl_->sockets.initialized()) {
        errorMessage = "socket runtime initialization failed";
        return false;
    }
    if (maximumClients == 0
        || maximumClients > runtimeLanMaximumTcpClients) {
        errorMessage = "TCP server maximum client count must be between 1 and "
            + std::to_string(runtimeLanMaximumTcpClients);
        return false;
    }
    RuntimeNativeSocket listener = createIpv4Socket(SOCK_STREAM, IPPROTO_TCP, errorMessage);
    if (listener == runtimeInvalidSocket) return false;
    if (!socketFitsSelect(listener)) {
        errorMessage = "TCP listener exceeds select descriptor capacity";
        closeSocket(listener);
        return false;
    }
    const int enabled = 1;
    (void)setsockopt(
        listener,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&enabled),
        static_cast<RuntimeSocketLength>(sizeof(enabled))
    );
    if (!bindIpv4Socket(listener, port, bindScope, errorMessage)
        || ::listen(listener, static_cast<int>(maximumClients)) != 0
        || !setNonBlocking(listener, errorMessage)) {
        if (errorMessage.empty()) {
            errorMessage = socketErrorText("listen", lastSocketError());
        }
        closeSocket(listener);
        return false;
    }
    impl_->listener = listener;
    impl_->boundPort = ::boundPort(listener);
    impl_->maximumClients = maximumClients;
    impl_->nextPeerId = 1;
    return true;
}

std::vector<RuntimeTcpEvent> RuntimeTcpServer::poll(
    int timeoutMilliseconds,
    std::string& errorMessage
) {
    std::vector<RuntimeTcpEvent> events;
    errorMessage.clear();
    if (!isOpen()) {
        errorMessage = "TCP server is not open";
        return events;
    }
    events.swap(impl_->pendingEvents);
    std::map<std::uint64_t, std::size_t> packetCounts;
    std::size_t totalPacketEvents = 0;
    const std::size_t fairPacketBudget = std::min(
        maximumPacketEventsPerPeerPerPoll,
        maximumPacketEventsPerServerPoll
            / std::max<std::size_t>(1, impl_->peers.size())
    );
    std::vector<std::uint64_t> disconnected;
    for (auto& [peerId, peer] : impl_->peers) {
        if (peer.receiveBuffer.empty()
            || totalPacketEvents >= maximumPacketEventsPerServerPoll) {
            continue;
        }
        const std::size_t budget = std::min(
            fairPacketBudget,
            maximumPacketEventsPerServerPoll - totalPacketEvents
        );
        bool peerDisconnected = false;
        const std::size_t decoded = parseReceivedPackets(
            peerId,
            peer,
            events,
            budget,
            peerDisconnected
        );
        packetCounts[peerId] = decoded;
        totalPacketEvents += decoded;
        if (peerDisconnected) disconnected.push_back(peerId);
    }
    for (std::uint64_t peerId : disconnected) disconnect(peerId);
    disconnected.clear();

    fd_set reads;
    fd_set writes;
    FD_ZERO(&reads);
    FD_ZERO(&writes);
    FD_SET(impl_->listener, &reads);
    RuntimeNativeSocket maximum = impl_->listener;
    for (const auto& [peerId, peer] : impl_->peers) {
        (void)peerId;
        FD_SET(peer.socket, &reads);
        if (!peer.sendQueue.empty()) FD_SET(peer.socket, &writes);
        maximum = std::max(maximum, peer.socket);
    }
    const int selected = selectSockets(
        maximum,
        &reads,
        &writes,
        events.empty() ? timeoutMilliseconds : 0
    );
    if (selected < 0) {
        errorMessage = socketErrorText("select(TCP server)", lastSocketError());
        return events;
    }
    if (selected == 0) return events;

    if (FD_ISSET(impl_->listener, &reads)) {
        for (std::size_t acceptedCount = 0;
             acceptedCount < maximumAcceptsPerPoll;
             ++acceptedCount) {
            sockaddr_storage address{};
            RuntimeSocketLength addressLength = static_cast<RuntimeSocketLength>(sizeof(address));
            RuntimeNativeSocket accepted = ::accept(
                impl_->listener,
                reinterpret_cast<sockaddr*>(&address),
                &addressLength
            );
            if (accepted == runtimeInvalidSocket) {
                const int code = lastSocketError();
                if (socketWouldBlock(code)) break;
                if (socketInterrupted(code)) continue;
                errorMessage = socketErrorText("accept", code);
                return events;
            }
            if (impl_->peers.size() >= impl_->maximumClients) {
                closeSocket(accepted);
                continue;
            }
            if (!socketFitsSelect(accepted)) {
                closeSocket(accepted);
                RuntimeTcpEvent event;
                event.kind = RuntimeTcpEventKind::Error;
                event.message = "accepted socket exceeds select descriptor capacity";
                events.push_back(std::move(event));
                continue;
            }
            std::string nonBlockingError;
            if (!setNonBlocking(accepted, nonBlockingError)) {
                closeSocket(accepted);
                RuntimeTcpEvent event;
                event.kind = RuntimeTcpEventKind::Error;
                event.message = std::move(nonBlockingError);
                events.push_back(std::move(event));
                continue;
            }
            setNoSigPipe(accepted);
            setTcpNoDelay(accepted);
            const std::uint64_t peerId = impl_->nextPeerId++;
            RuntimeTcpPeerState peer;
            peer.socket = accepted;
            peer.address = numericAddress(
                reinterpret_cast<const sockaddr*>(&address),
                addressLength
            );
            impl_->peers.emplace(peerId, std::move(peer));
            RuntimeTcpEvent event;
            event.kind = RuntimeTcpEventKind::Connected;
            event.peerId = peerId;
            event.peerAddress = impl_->peers.at(peerId).address;
            event.message = "peer connected";
            events.push_back(std::move(event));
        }
    }

    for (auto& [peerId, peer] : impl_->peers) {
        if (!FD_ISSET(peer.socket, &reads)) continue;
        std::size_t& peerPacketCount = packetCounts[peerId];
        if (peerPacketCount >= fairPacketBudget
            || totalPacketEvents >= maximumPacketEventsPerServerPoll) {
            continue;
        }
        const std::size_t budget = std::min(
            fairPacketBudget - peerPacketCount,
            maximumPacketEventsPerServerPoll - totalPacketEvents
        );
        bool peerDisconnected = false;
        const std::size_t decoded = appendReceivedPackets(
            peerId,
            peer,
            events,
            budget,
            peerDisconnected
        );
        peerPacketCount += decoded;
        totalPacketEvents += decoded;
        if (peerDisconnected) disconnected.push_back(peerId);
    }
    for (auto& [peerId, peer] : impl_->peers) {
        if (!FD_ISSET(peer.socket, &writes)
            || std::find(disconnected.begin(), disconnected.end(), peerId)
                != disconnected.end()) {
            continue;
        }
        bool peerDisconnected = false;
        flushQueuedPackets(peerId, peer, events, peerDisconnected);
        if (peerDisconnected) {
            RuntimeTcpEvent event;
            event.kind = RuntimeTcpEventKind::Disconnected;
            event.peerId = peerId;
            event.peerAddress = peer.address;
            event.message = "peer disconnected after a send failure";
            events.push_back(std::move(event));
            disconnected.push_back(peerId);
        }
    }
    for (std::uint64_t peerId : disconnected) disconnect(peerId);
    return events;
}

bool RuntimeTcpServer::send(
    std::uint64_t peerId,
    const RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (!isOpen()) {
        errorMessage = "TCP server is not open";
        return false;
    }
    std::vector<std::uint8_t> bytes;
    if (!encodeRuntimeNetworkPacket(packet, bytes, errorMessage)) return false;
    const auto found = impl_->peers.find(peerId);
    if (found == impl_->peers.end()) {
        errorMessage = "TCP peer is not connected";
        return false;
    }
    if (!queueFramedBytes(found->second, std::move(bytes), errorMessage)) {
        RuntimeTcpEvent failed;
        failed.kind = RuntimeTcpEventKind::Error;
        failed.peerId = peerId;
        failed.peerAddress = found->second.address;
        failed.message = errorMessage;
        impl_->pendingEvents.push_back(std::move(failed));
        RuntimeTcpEvent disconnected;
        disconnected.kind = RuntimeTcpEventKind::Disconnected;
        disconnected.peerId = peerId;
        disconnected.peerAddress = found->second.address;
        disconnected.message = "peer disconnected after send queue overflow";
        impl_->pendingEvents.push_back(std::move(disconnected));
        disconnect(peerId);
        return false;
    }
    return true;
}

std::size_t RuntimeTcpServer::broadcast(
    const RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (!isOpen()) {
        errorMessage = "TCP server is not open";
        return 0;
    }
    std::vector<std::uint8_t> bytes;
    if (!encodeRuntimeNetworkPacket(packet, bytes, errorMessage)) return 0;
    std::size_t queued = 0;
    struct FailedPeer {
        std::uint64_t peerId = 0;
        std::string address;
        std::string message;
    };
    std::vector<FailedPeer> failed;
    for (auto& [peerId, peer] : impl_->peers) {
        std::string peerError;
        if (queueFramedBytes(peer, bytes, peerError)) {
            ++queued;
        } else {
            failed.push_back({peerId, peer.address, peerError});
            if (errorMessage.empty()) errorMessage = peerError;
        }
    }
    for (const FailedPeer& peer : failed) {
        RuntimeTcpEvent failedEvent;
        failedEvent.kind = RuntimeTcpEventKind::Error;
        failedEvent.peerId = peer.peerId;
        failedEvent.peerAddress = peer.address;
        failedEvent.message = peer.message;
        impl_->pendingEvents.push_back(std::move(failedEvent));
        RuntimeTcpEvent disconnectedEvent;
        disconnectedEvent.kind = RuntimeTcpEventKind::Disconnected;
        disconnectedEvent.peerId = peer.peerId;
        disconnectedEvent.peerAddress = peer.address;
        disconnectedEvent.message =
            "peer disconnected after broadcast queue overflow";
        impl_->pendingEvents.push_back(std::move(disconnectedEvent));
        disconnect(peer.peerId);
    }
    return queued;
}

bool RuntimeTcpServer::disconnect(std::uint64_t peerId) {
    if (!impl_) return false;
    const auto found = impl_->peers.find(peerId);
    if (found == impl_->peers.end()) return false;
    closeSocket(found->second.socket);
    impl_->peers.erase(found);
    return true;
}

void RuntimeTcpServer::close() {
    if (!impl_) return;
    for (auto& [peerId, peer] : impl_->peers) {
        (void)peerId;
        closeSocket(peer.socket);
    }
    impl_->peers.clear();
    impl_->pendingEvents.clear();
    closeSocket(impl_->listener);
    impl_->boundPort = 0;
    impl_->maximumClients = 0;
}
bool RuntimeTcpServer::isOpen() const {
    return impl_ && impl_->listener != runtimeInvalidSocket;
}
std::uint16_t RuntimeTcpServer::port() const {
    return impl_ ? impl_->boundPort : 0;
}
std::size_t RuntimeTcpServer::clientCount() const {
    return impl_ ? impl_->peers.size() : 0;
}

struct RuntimeTcpClient::Impl {
    RuntimeSocketSystem sockets;
    RuntimeTcpPeerState peer;
};

RuntimeTcpClient::RuntimeTcpClient() : impl_(std::make_unique<Impl>()) {}
RuntimeTcpClient::~RuntimeTcpClient() { close(); }
RuntimeTcpClient::RuntimeTcpClient(RuntimeTcpClient&&) noexcept = default;
RuntimeTcpClient& RuntimeTcpClient::operator=(RuntimeTcpClient&& other) noexcept {
    if (this != &other) {
        close();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool RuntimeTcpClient::connect(
    const std::string& host,
    std::uint16_t port,
    int timeoutMilliseconds,
    std::string& errorMessage
) {
    return connect(host, port, timeoutMilliseconds, {}, errorMessage);
}

bool RuntimeTcpClient::connect(
    const std::string& host,
    std::uint16_t port,
    int timeoutMilliseconds,
    std::span<const std::uint8_t> localProxyPrelude,
    std::string& errorMessage
) {
    close();
    if (!impl_) impl_ = std::make_unique<Impl>();
    errorMessage.clear();
    if (!impl_->sockets.initialized()) {
        errorMessage = "socket runtime initialization failed";
        return false;
    }
    if (host.empty() || port == 0) {
        errorMessage = "TCP host and port are required";
        return false;
    }
    if (localProxyPrelude.size() > 64U) {
        errorMessage = "local TCP proxy prelude exceeds 64 bytes";
        return false;
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* results = nullptr;
    const std::string portText = std::to_string(port);
    if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &results) != 0
        || results == nullptr) {
        errorMessage = "TCP host could not be resolved";
        if (results != nullptr) freeaddrinfo(results);
        return false;
    }

    RuntimeNativeSocket connected = runtimeInvalidSocket;
    std::string connectedAddress;
    std::string lastError = "TCP connection failed";
    const auto connectDeadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(std::max(0, timeoutMilliseconds));
    for (addrinfo* result = results; result != nullptr; result = result->ai_next) {
        RuntimeNativeSocket candidate = ::socket(
            result->ai_family,
            result->ai_socktype,
            result->ai_protocol
        );
        if (candidate == runtimeInvalidSocket) continue;
        if (!socketFitsSelect(candidate)) {
            lastError = "TCP socket exceeds select descriptor capacity";
            closeSocket(candidate);
            continue;
        }
        std::string nonBlockingError;
        if (!setNonBlocking(candidate, nonBlockingError)) {
            lastError = std::move(nonBlockingError);
            closeSocket(candidate);
            continue;
        }
        const int connectResult = ::connect(
            candidate,
            result->ai_addr,
            static_cast<RuntimeSocketLength>(result->ai_addrlen)
        );
        if (connectResult != 0) {
            const int code = lastSocketError();
            if (!socketWouldBlock(code)) {
                lastError = socketErrorText("connect", code);
                closeSocket(candidate);
                continue;
            }
            std::string waitError;
            const auto remainingDuration = std::chrono::duration_cast<
                std::chrono::milliseconds
            >(connectDeadline - std::chrono::steady_clock::now()).count();
            const int remainingMilliseconds = static_cast<int>(std::clamp<
                long long
            >(
                remainingDuration,
                0,
                static_cast<long long>(std::numeric_limits<int>::max())
            ));
            if (!waitForConnect(
                    candidate,
                    remainingMilliseconds,
                    waitError
                )) {
                lastError = std::move(waitError);
                closeSocket(candidate);
                continue;
            }
            int socketError = 0;
            RuntimeSocketLength socketErrorLength = static_cast<RuntimeSocketLength>(sizeof(socketError));
            if (getsockopt(
                    candidate,
                    SOL_SOCKET,
                    SO_ERROR,
                    reinterpret_cast<char*>(&socketError),
                    &socketErrorLength
                ) != 0
                || socketError != 0) {
                lastError = socketErrorText(
                    "connect",
                    socketError != 0 ? socketError : lastSocketError()
                );
                closeSocket(candidate);
                continue;
            }
        }
        connected = candidate;
        connectedAddress = numericAddress(
            result->ai_addr,
            static_cast<RuntimeSocketLength>(result->ai_addrlen)
        );
        break;
    }
    freeaddrinfo(results);
    if (connected == runtimeInvalidSocket) {
        errorMessage = std::move(lastError);
        return false;
    }
    setNoSigPipe(connected);
    setTcpNoDelay(connected);
    impl_->peer.socket = connected;
    impl_->peer.address = std::move(connectedAddress);
    impl_->peer.receiveBuffer.clear();
    impl_->peer.sendQueue.clear();
    impl_->peer.sendOffset = 0;
    impl_->peer.queuedBytes = 0;
    if (!localProxyPrelude.empty()) {
        std::vector<std::uint8_t> prelude(
            localProxyPrelude.begin(),
            localProxyPrelude.end()
        );
        if (!queueFramedBytes(
                impl_->peer,
                std::move(prelude),
                errorMessage
            )) {
            close();
            return false;
        }
    }
    return true;
}

std::vector<RuntimeTcpEvent> RuntimeTcpClient::poll(
    int timeoutMilliseconds,
    std::string& errorMessage
) {
    std::vector<RuntimeTcpEvent> events;
    errorMessage.clear();
    if (!isConnected()) {
        errorMessage = "TCP client is not connected";
        return events;
    }
    bool disconnected = false;
    std::size_t packetCount = parseReceivedPackets(
        1,
        impl_->peer,
        events,
        maximumPacketEventsPerPeerPerPoll,
        disconnected
    );
    if (disconnected) {
        close();
        return events;
    }
    fd_set reads;
    fd_set writes;
    FD_ZERO(&reads);
    FD_ZERO(&writes);
    FD_SET(impl_->peer.socket, &reads);
    if (!impl_->peer.sendQueue.empty()) {
        FD_SET(impl_->peer.socket, &writes);
    }
    const int selected = selectSockets(
        impl_->peer.socket,
        &reads,
        &writes,
        events.empty() ? timeoutMilliseconds : 0
    );
    if (selected < 0) {
        errorMessage = socketErrorText("select(TCP client)", lastSocketError());
        return events;
    }
    if (selected == 0) return events;
    if (FD_ISSET(impl_->peer.socket, &reads)
        && packetCount < maximumPacketEventsPerPeerPerPoll) {
        packetCount += appendReceivedPackets(
            1,
            impl_->peer,
            events,
            maximumPacketEventsPerPeerPerPoll - packetCount,
            disconnected
        );
    }
    if (!disconnected && FD_ISSET(impl_->peer.socket, &writes)) {
        flushQueuedPackets(1, impl_->peer, events, disconnected);
        if (disconnected) {
            RuntimeTcpEvent event;
            event.kind = RuntimeTcpEventKind::Disconnected;
            event.peerId = 1;
            event.peerAddress = impl_->peer.address;
            event.message = "server disconnected after a send failure";
            events.push_back(std::move(event));
        }
    }
    if (disconnected) close();
    return events;
}

bool RuntimeTcpClient::send(
    const RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (!isConnected()) {
        errorMessage = "TCP client is not connected";
        return false;
    }
    return queueFramedPacket(impl_->peer, packet, errorMessage);
}

void RuntimeTcpClient::close() {
    if (!impl_) return;
    closeSocket(impl_->peer.socket);
    impl_->peer.address.clear();
    impl_->peer.receiveBuffer.clear();
    impl_->peer.sendQueue.clear();
    impl_->peer.sendOffset = 0;
    impl_->peer.queuedBytes = 0;
}
bool RuntimeTcpClient::isConnected() const {
    return impl_ && impl_->peer.socket != runtimeInvalidSocket;
}
const std::string& RuntimeTcpClient::peerAddress() const {
    static const std::string empty;
    return impl_ ? impl_->peer.address : empty;
}
