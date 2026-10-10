#include "network/transport/RuntimeInternetTcpTunnelProxy.h"

#include "network/protocol/RuntimeMultiplayerProtocol.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")
using RuntimeTunnelSocket = SOCKET;
constexpr RuntimeTunnelSocket invalidTunnelSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <random>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using RuntimeTunnelSocket = int;
constexpr RuntimeTunnelSocket invalidTunnelSocket = -1;
#endif

namespace {

constexpr std::size_t tunnelReadChunkBytes = 64U * 1024U;
constexpr std::size_t maximumFramedBytes =
    runtimeLanMaximumPacketBytes + 12U;

class SocketSystem {
public:
    SocketSystem() {
#ifdef _WIN32
        WSADATA data{};
        initialized_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        initialized_ = true;
#endif
    }
    ~SocketSystem() {
#ifdef _WIN32
        if (initialized_) WSACleanup();
#endif
    }
    bool initialized() const { return initialized_; }
private:
    bool initialized_ = false;
};

int socketError() {
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
    return code == EWOULDBLOCK || code == EAGAIN || code == EINPROGRESS;
#endif
}

void closeSocket(RuntimeTunnelSocket& socket) noexcept {
    if (socket == invalidTunnelSocket) return;
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
    socket = invalidTunnelSocket;
}

bool setNonBlocking(RuntimeTunnelSocket socket, std::string& errorMessage) {
#ifdef _WIN32
    u_long enabled = 1;
    if (ioctlsocket(socket, FIONBIO, &enabled) != 0) {
        errorMessage = "could not make tunnel socket nonblocking";
        return false;
    }
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) != 0) {
        errorMessage = "could not make tunnel socket nonblocking";
        return false;
    }
#endif
    return true;
}

bool validProxyConfig(
    const RuntimeInternetTcpTunnelProxyConfig& config,
    std::string& errorMessage
) {
    if (config.maximumQueuedBytes < maximumFramedBytes
        || config.maximumQueuedBytes
            > runtimeInternetTcpTunnelMaximumQueuedBytes
        || config.connectTimeoutMilliseconds == 0
        || (config.heartbeatMilliseconds != 0
            && (config.heartbeatMilliseconds < 100U || config.heartbeatMilliseconds > 10000U
                || config.heartbeatTimeoutMilliseconds < config.heartbeatMilliseconds * 2U
                || config.heartbeatTimeoutMilliseconds > 30000U))) {
        errorMessage = "invalid Internet TCP tunnel proxy limits";
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
        || route.roomId.empty() || route.localPeerId.empty()
        || route.remotePeerId.empty()
        || route.localPeerId == route.remotePeerId
        || route.endpoint.address.empty() || route.endpoint.port == 0) {
        errorMessage = "Internet TCP route descriptor is incomplete";
        return false;
    }
    const bool hostRoute = route.localRole == RuntimeInternetRole::Host
        && (route.remoteRole == RuntimeInternetRole::Client
            || route.remoteRole == RuntimeInternetRole::Spectator);
    const bool remoteRoute =
        (route.localRole == RuntimeInternetRole::Client
            || route.localRole == RuntimeInternetRole::Spectator)
        && route.remoteRole == RuntimeInternetRole::Host;
    if (!hostRoute && !remoteRoute) {
        errorMessage = "Internet TCP route roles are incompatible";
        return false;
    }
    return true;
}

bool identityMatchesRoute(
    const RuntimeInternetAuthenticatedTcpStreamIdentity& identity,
    const RuntimeInternetTcpRouteDescriptor& route,
    std::string& errorMessage
) {
    if (identity.authenticatedServiceId != route.serviceId
        || identity.authenticatedPeerId != route.localPeerId
        || identity.route != route) {
        errorMessage =
            "authenticated TCP stream identity does not match its route";
        return false;
    }
    return true;
}

bool fillRandom(std::span<std::uint8_t> bytes) {
#ifdef _WIN32
    return BCryptGenRandom(
        nullptr,
        bytes.data(),
        static_cast<ULONG>(bytes.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    ) == 0;
#else
    std::random_device random;
    for (std::uint8_t& byte : bytes) {
        byte = static_cast<std::uint8_t>(random());
    }
    return true;
#endif
}

bool constantTimeEqual(
    std::span<const std::uint8_t> left,
    std::span<const std::uint8_t> right
) {
    if (left.size() != right.size()) return false;
    std::uint8_t difference = 0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        difference = static_cast<std::uint8_t>(
            difference | static_cast<std::uint8_t>(left[index] ^ right[index])
        );
    }
    return difference == 0;
}

RuntimeTunnelSocket openLoopbackListener(
    std::uint16_t& boundPort,
    std::string& errorMessage
) {
    boundPort = 0;
    RuntimeTunnelSocket listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == invalidTunnelSocket) {
        errorMessage = "could not create loopback tunnel listener";
        return invalidTunnelSocket;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(
            listener,
            reinterpret_cast<const sockaddr*>(&address),
            static_cast<int>(sizeof(address))
        ) != 0
        || ::listen(listener, 1) != 0
        || !setNonBlocking(listener, errorMessage)) {
        if (errorMessage.empty()) {
            errorMessage = "could not bind loopback tunnel listener";
        }
        closeSocket(listener);
        return invalidTunnelSocket;
    }
    sockaddr_in observed{};
#ifdef _WIN32
    int observedSize = static_cast<int>(sizeof(observed));
#else
    socklen_t observedSize = static_cast<socklen_t>(sizeof(observed));
#endif
    if (getsockname(
            listener,
            reinterpret_cast<sockaddr*>(&observed),
            &observedSize
        ) != 0) {
        errorMessage = "could not read loopback tunnel listener port";
        closeSocket(listener);
        return invalidTunnelSocket;
    }
    boundPort = ntohs(observed.sin_port);
    return listener;
}

RuntimeTunnelSocket connectLoopback(
    std::uint16_t port,
    std::uint32_t timeoutMilliseconds,
    std::string& errorMessage
) {
    RuntimeTunnelSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == invalidTunnelSocket) {
        errorMessage = "could not create loopback tunnel connection";
        return invalidTunnelSocket;
    }
    if (!setNonBlocking(socket, errorMessage)) {
        closeSocket(socket);
        return invalidTunnelSocket;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    const int connected = ::connect(
        socket,
        reinterpret_cast<const sockaddr*>(&address),
        static_cast<int>(sizeof(address))
    );
    if (connected != 0 && !socketWouldBlock(socketError())) {
        errorMessage = "could not connect to the private LAN session";
        closeSocket(socket);
        return invalidTunnelSocket;
    }
    if (connected != 0) {
        fd_set writes;
        FD_ZERO(&writes);
        FD_SET(socket, &writes);
        timeval timeout{};
        timeout.tv_sec = static_cast<long>(timeoutMilliseconds / 1000U);
        timeout.tv_usec = static_cast<long>(
            (timeoutMilliseconds % 1000U) * 1000U
        );
        const int selected = select(
#ifdef _WIN32
            0,
#else
            socket + 1,
#endif
            nullptr,
            &writes,
            nullptr,
            &timeout
        );
        int completionError = 0;
#ifdef _WIN32
        int completionSize = static_cast<int>(sizeof(completionError));
#else
        socklen_t completionSize =
            static_cast<socklen_t>(sizeof(completionError));
#endif
        if (selected != 1
            || getsockopt(
                socket,
                SOL_SOCKET,
                SO_ERROR,
                reinterpret_cast<char*>(&completionError),
                &completionSize
            ) != 0
            || completionError != 0) {
            errorMessage = "private LAN session connection timed out";
            closeSocket(socket);
            return invalidTunnelSocket;
        }
    }
    const int enabled = 1;
    (void)setsockopt(
        socket,
        IPPROTO_TCP,
        TCP_NODELAY,
        reinterpret_cast<const char*>(&enabled),
        static_cast<int>(sizeof(enabled))
    );
    return socket;
}

RuntimeInternetTcpStreamIoResult readSocket(
    RuntimeTunnelSocket socket,
    std::span<std::uint8_t> destination
) {
    const int requested = static_cast<int>(std::min<std::size_t>(
        destination.size(),
        static_cast<std::size_t>(std::numeric_limits<int>::max())
    ));
    const int received = ::recv(
        socket,
        reinterpret_cast<char*>(destination.data()),
        requested,
        0
    );
    if (received > 0) {
        return {
            RuntimeInternetTcpStreamIoStatus::Progress,
            static_cast<std::size_t>(received),
            {}
        };
    }
    if (received == 0) {
        return {RuntimeInternetTcpStreamIoStatus::Closed, 0, {}};
    }
    if (socketWouldBlock(socketError())) {
        return {RuntimeInternetTcpStreamIoStatus::WouldBlock, 0, {}};
    }
    return {
        RuntimeInternetTcpStreamIoStatus::Failed,
        0,
        "loopback tunnel receive failed"
    };
}

RuntimeInternetTcpStreamIoResult writeSocket(
    RuntimeTunnelSocket socket,
    std::span<const std::uint8_t> source
) {
    const int requested = static_cast<int>(std::min<std::size_t>(
        source.size(),
        static_cast<std::size_t>(std::numeric_limits<int>::max())
    ));
    const int sent = ::send(
        socket,
        reinterpret_cast<const char*>(source.data()),
        requested,
        0
    );
    if (sent > 0) {
        return {
            RuntimeInternetTcpStreamIoStatus::Progress,
            static_cast<std::size_t>(sent),
            {}
        };
    }
    if (sent == 0) {
        return {RuntimeInternetTcpStreamIoStatus::Closed, 0, {}};
    }
    if (socketWouldBlock(socketError())) {
        return {RuntimeInternetTcpStreamIoStatus::WouldBlock, 0, {}};
    }
    return {
        RuntimeInternetTcpStreamIoStatus::Failed,
        0,
        "loopback tunnel send failed"
    };
}

class FramedQueue {
public:
    using Validator = std::function<bool(
        const RuntimeNetworkPacket&,
        std::string&
    )>;

    bool append(
        std::span<const std::uint8_t> bytes,
        std::size_t maximumBytes,
        const Validator& validator,
        std::string& errorMessage,
        const std::function<bool(const RuntimeNetworkPacket&)>& forward = {}
    ) {
        if (bytes.size() > maximumBytes
            || bufferedBytes() > maximumBytes - bytes.size()) {
            errorMessage = "Internet TCP tunnel queue limit reached";
            return false;
        }
        pending_.insert(pending_.end(), bytes.begin(), bytes.end());
        while (!pending_.empty()) {
            RuntimeNetworkPacket packet;
            std::size_t consumed = 0;
            bool complete = false;
            std::string decodeError;
            if (!decodeRuntimeNetworkPacket(
                    pending_,
                    packet,
                    consumed,
                    complete,
                    decodeError
                )) {
                errorMessage = decodeError.empty()
                    ? "Internet TCP tunnel rejected an oversized frame"
                    : std::move(decodeError);
                return false;
            }
            if (!complete) {
                if (pending_.size() > maximumFramedBytes) {
                    errorMessage =
                        "Internet TCP tunnel incomplete frame exceeds limit";
                    return false;
                }
                break;
            }
            if (validator && !validator(packet, errorMessage)) return false;
            std::vector<std::uint8_t> frame(
                pending_.begin(),
                pending_.begin() + static_cast<std::ptrdiff_t>(consumed)
            );
            if (!forward || forward(packet)) {
                queuedBytes_ += frame.size();
                frames_.push_back(std::move(frame));
            }
            pending_.erase(
                pending_.begin(),
                pending_.begin() + static_cast<std::ptrdiff_t>(consumed)
            );
        }
        return true;
    }

    std::span<const std::uint8_t> front() const {
        if (frames_.empty()) return {};
        const std::vector<std::uint8_t>& frame = frames_.front();
        return std::span<const std::uint8_t>(frame).subspan(frontOffset_);
    }

    bool enqueuePacket(const RuntimeNetworkPacket& packet, std::size_t maximumBytes, std::string& error) {
        std::vector<std::uint8_t> frame;
        if (!encodeRuntimeNetworkPacket(packet, frame, error)) return false;
        if (frame.size() > maximumBytes || bufferedBytes() > maximumBytes - frame.size()) return true;
        queuedBytes_ += frame.size();
        frames_.push_back(std::move(frame));
        return true;
    }

    void consume(std::size_t bytes) {
        if (frames_.empty() || bytes == 0) return;
        const std::size_t remaining = frames_.front().size() - frontOffset_;
        const std::size_t consumed = std::min(bytes, remaining);
        frontOffset_ += consumed;
        queuedBytes_ -= consumed;
        if (frontOffset_ == frames_.front().size()) {
            frames_.pop_front();
            frontOffset_ = 0;
        }
    }

    std::size_t bufferedBytes() const {
        return pending_.size() + queuedBytes_;
    }

    bool empty() const { return pending_.empty() && frames_.empty(); }

    void clear() {
        pending_.clear();
        frames_.clear();
        frontOffset_ = 0;
        queuedBytes_ = 0;
    }

private:
    std::vector<std::uint8_t> pending_;
    std::deque<std::vector<std::uint8_t>> frames_;
    std::size_t frontOffset_ = 0;
    std::size_t queuedBytes_ = 0;
};

bool validIoResult(
    const RuntimeInternetTcpStreamIoResult& result,
    std::size_t capacity,
    std::string& errorMessage
) {
    if (result.status == RuntimeInternetTcpStreamIoStatus::Progress) {
        if (result.bytesTransferred == 0
            || result.bytesTransferred > capacity) {
            errorMessage = "authenticated stream returned an invalid byte count";
            return false;
        }
    } else if (result.bytesTransferred != 0) {
        errorMessage = "authenticated stream returned bytes without progress";
        return false;
    }
    return true;
}

bool idleExpired(
    std::uint64_t nowMilliseconds,
    std::uint64_t lastProgressMilliseconds,
    std::uint32_t timeoutMilliseconds
) {
    return timeoutMilliseconds != 0U
        && nowMilliseconds >= lastProgressMilliseconds
        && nowMilliseconds - lastProgressMilliseconds >= timeoutMilliseconds;
}

using ActiveRelayRouteKey = std::tuple<
    std::string,
    std::string,
    std::uint64_t,
    std::string,
    std::string,
    RuntimeInternetRole
>;

std::mutex& activeRelayRouteMutex() {
    static std::mutex mutex;
    return mutex;
}

std::set<ActiveRelayRouteKey>& activeRelayRoutes() {
    static std::set<ActiveRelayRouteKey> routes;
    return routes;
}

ActiveRelayRouteKey relayRouteKey(
    const RuntimeInternetTcpRouteDescriptor& hostRoute
) {
    return {
        hostRoute.serviceId,
        hostRoute.roomId,
        hostRoute.connectionId,
        hostRoute.localPeerId,
        hostRoute.remotePeerId,
        hostRoute.remoteRole
    };
}

bool reserveRelayRoute(const ActiveRelayRouteKey& key) {
    const std::lock_guard<std::mutex> lock(activeRelayRouteMutex());
    return activeRelayRoutes().insert(key).second;
}

void releaseRelayRoute(const ActiveRelayRouteKey& key) noexcept {
    const std::lock_guard<std::mutex> lock(activeRelayRouteMutex());
    activeRelayRoutes().erase(key);
}

} // namespace

struct RuntimeInternetTcpTunnelProxy::Impl {
    enum class Mode { None, Host, Client };

    SocketSystem sockets;
    Mode mode = Mode::None;
    RuntimeInternetTcpTunnelProxyConfig config;
    RuntimeInternetTcpRouteDescriptor route;
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> tunnel;
    RuntimeTunnelSocket listener = invalidTunnelSocket;
    RuntimeTunnelSocket local = invalidTunnelSocket;
    RuntimeInternetTcpTunnelClientEndpoint endpoint;
    std::vector<std::uint8_t> receivedPrelude;
    FramedQueue localToTunnel;
    FramedQueue tunnelToLocal;
    std::uint64_t lastProgressMilliseconds = 0;
    std::uint64_t startedMilliseconds = 0;
    std::uint64_t nextHeartbeatMilliseconds = 0;
    std::uint64_t lastTunnelReceiveMilliseconds = 0;
    static constexpr std::uint16_t heartbeatPacketType = 0x49ff;

    RuntimeNetworkPacket heartbeatPacket() const {
        RuntimeNetworkPacket packet;
        packet.type = heartbeatPacketType;
        packet.payload.push_back(static_cast<std::uint8_t>(runtimeInternetControlProtocolVersion));
        for (int shift = 56; shift >= 0; shift -= 8)
            packet.payload.push_back(static_cast<std::uint8_t>(route.connectionId >> shift));
        return packet;
    }
    bool localPreludeAccepted = false;
    bool remoteAdmissionVerified = false;
    bool localReadClosed = false;
    bool tunnelReadClosed = false;
    bool closed = false;

    void reset() noexcept {
        closeSocket(listener);
        closeSocket(local);
        if (tunnel) tunnel->close();
        tunnel.reset();
        mode = Mode::None;
        route = {};
        endpoint = {};
        receivedPrelude.clear();
        localToTunnel.clear();
        tunnelToLocal.clear();
        lastProgressMilliseconds = 0;
        startedMilliseconds = 0;
        nextHeartbeatMilliseconds = 0;
        lastTunnelReceiveMilliseconds = 0;
        localPreludeAccepted = false;
        remoteAdmissionVerified = false;
        localReadClosed = false;
        tunnelReadClosed = false;
        closed = false;
    }

    bool validateHostAdmission(
        const RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) {
        if (remoteAdmissionVerified) return true;
        RuntimeMultiplayerMessage message;
        if (!decodeRuntimeMultiplayerMessage(packet, message, errorMessage)
            || message.type
                != RuntimeMultiplayerMessageType::JoinAdmissionRequest) {
            if (errorMessage.empty()) {
                errorMessage =
                    "first relayed client frame is not JoinAdmissionRequest";
            }
            return false;
        }
        const auto* admission =
            std::get_if<RuntimeMultiplayerJoinAdmissionRequestMessage>(
                &message.body
            );
        if (admission == nullptr
            || admission->request.peerId != route.remotePeerId) {
            errorMessage =
                "relayed admission peer does not match authenticated route";
            return false;
        }
        const RuntimeMultiplayerParticipationRole expected =
            route.remoteRole == RuntimeInternetRole::Spectator
                ? RuntimeMultiplayerParticipationRole::Spectator
                : RuntimeMultiplayerParticipationRole::Combatant;
        if (admission->request.participation != expected) {
            errorMessage =
                "relayed admission role does not match authenticated route";
            return false;
        }
        remoteAdmissionVerified = true;
        return true;
    }

    RuntimeInternetTcpTunnelPollResult fail(std::string errorMessage) {
        RuntimeInternetTcpTunnelPollResult result;
        result.succeeded = false;
        result.closed = true;
        result.localConnected = local != invalidTunnelSocket;
        result.localPreludeAccepted = localPreludeAccepted;
        result.remoteAdmissionVerified = remoteAdmissionVerified;
        result.error = std::move(errorMessage);
        closeSocket(listener);
        closeSocket(local);
        if (tunnel) tunnel->close();
        closed = true;
        return result;
    }
};

RuntimeInternetTcpTunnelProxy::RuntimeInternetTcpTunnelProxy()
    : impl_(std::make_unique<Impl>()) {}
RuntimeInternetTcpTunnelProxy::~RuntimeInternetTcpTunnelProxy() { close(); }
RuntimeInternetTcpTunnelProxy::RuntimeInternetTcpTunnelProxy(
    RuntimeInternetTcpTunnelProxy&&
) noexcept = default;
RuntimeInternetTcpTunnelProxy& RuntimeInternetTcpTunnelProxy::operator=(
    RuntimeInternetTcpTunnelProxy&& other
) noexcept {
    if (this != &other) {
        close();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool RuntimeInternetTcpTunnelProxy::startHost(
    RuntimeInternetTcpTunnelProxyConfig config,
    RuntimeInternetTcpRouteDescriptor route,
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> tunnel,
    std::uint16_t loopbackSessionPort,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->reset();
    if (!impl_->sockets.initialized()
        || !validProxyConfig(config, errorMessage)
        || !validRoute(route, errorMessage)
        || route.localRole != RuntimeInternetRole::Host
        || tunnel == nullptr
        || !identityMatchesRoute(tunnel->identity(), route, errorMessage)
        || loopbackSessionPort == 0) {
        if (errorMessage.empty()) errorMessage = "invalid host tunnel setup";
        return false;
    }
    RuntimeTunnelSocket local = connectLoopback(
        loopbackSessionPort,
        config.connectTimeoutMilliseconds,
        errorMessage
    );
    if (local == invalidTunnelSocket) return false;
    impl_->mode = Impl::Mode::Host;
    impl_->config = config;
    impl_->route = std::move(route);
    impl_->tunnel = std::move(tunnel);
    impl_->local = local;
    impl_->localPreludeAccepted = true;
    impl_->lastProgressMilliseconds = nowMilliseconds;
    impl_->startedMilliseconds = nowMilliseconds;
    errorMessage.clear();
    return true;
}

bool RuntimeInternetTcpTunnelProxy::startClient(
    RuntimeInternetTcpTunnelProxyConfig config,
    RuntimeInternetTcpRouteDescriptor route,
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> tunnel,
    RuntimeLanRoomAdvertisement canonicalRoom,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->reset();
    const bool roomValid = !canonicalRoom.roomId.empty()
        && canonicalRoom.roomId == route.roomId
        && !canonicalRoom.roomName.empty() && !canonicalRoom.hostName.empty()
        && !canonicalRoom.contentFingerprint.empty()
        && canonicalRoom.protocolVersion == runtimeLanProtocolVersion
        && canonicalRoom.playerCount > 0
        && canonicalRoom.maximumPlayers >= canonicalRoom.playerCount;
    if (!impl_->sockets.initialized()
        || !validProxyConfig(config, errorMessage)
        || !validRoute(route, errorMessage)
        || route.localRole == RuntimeInternetRole::Host
        || tunnel == nullptr
        || !identityMatchesRoute(tunnel->identity(), route, errorMessage)
        || !roomValid) {
        if (errorMessage.empty()) errorMessage = "invalid client tunnel setup";
        return false;
    }
    std::uint16_t listenerPort = 0;
    RuntimeTunnelSocket listener = openLoopbackListener(
        listenerPort,
        errorMessage
    );
    if (listener == invalidTunnelSocket) return false;
    std::vector<std::uint8_t> prelude(runtimeInternetTcpTunnelPreludeBytes);
    if (!fillRandom(prelude)) {
        closeSocket(listener);
        errorMessage = "secure local tunnel prelude generation failed";
        return false;
    }
    canonicalRoom.hostAddress = "127.0.0.1";
    canonicalRoom.sessionPort = listenerPort;
    impl_->mode = Impl::Mode::Client;
    impl_->config = config;
    impl_->route = std::move(route);
    impl_->tunnel = std::move(tunnel);
    impl_->listener = listener;
    impl_->endpoint.room = std::move(canonicalRoom);
    impl_->endpoint.localProxyPrelude = std::move(prelude);
    impl_->remoteAdmissionVerified = true;
    impl_->lastProgressMilliseconds = nowMilliseconds;
    impl_->startedMilliseconds = nowMilliseconds;
    errorMessage.clear();
    return true;
}

RuntimeInternetTcpTunnelPollResult RuntimeInternetTcpTunnelProxy::poll(
    std::uint64_t nowMilliseconds
) {
    if (!impl_ || impl_->mode == Impl::Mode::None || !impl_->tunnel
        || impl_->closed) {
        RuntimeInternetTcpTunnelPollResult result;
        result.succeeded = false;
        result.closed = true;
        result.error = "Internet TCP tunnel proxy is not open";
        return result;
    }
    RuntimeInternetTcpTunnelPollResult result;
    bool progressed = false;

    if (impl_->mode == Impl::Mode::Client
        && impl_->local == invalidTunnelSocket) {
        sockaddr_in address{};
#ifdef _WIN32
        int addressSize = static_cast<int>(sizeof(address));
#else
        socklen_t addressSize = static_cast<socklen_t>(sizeof(address));
#endif
        RuntimeTunnelSocket accepted = ::accept(
            impl_->listener,
            reinterpret_cast<sockaddr*>(&address),
            &addressSize
        );
        if (accepted != invalidTunnelSocket) {
            std::string nonBlockingError;
            if (!setNonBlocking(accepted, nonBlockingError)) {
                closeSocket(accepted);
                return impl_->fail(std::move(nonBlockingError));
            }
            if (ntohl(address.sin_addr.s_addr) != INADDR_LOOPBACK) {
                closeSocket(accepted);
                return impl_->fail(
                    "client tunnel proxy accepted a non-loopback peer"
                );
            }
            impl_->local = accepted;
            closeSocket(impl_->listener);
            progressed = true;
        } else if (!socketWouldBlock(socketError())) {
            return impl_->fail("client tunnel loopback accept failed");
        }
        if (impl_->local == invalidTunnelSocket
            && idleExpired(
                nowMilliseconds,
                impl_->startedMilliseconds,
                impl_->config.connectTimeoutMilliseconds
            )) {
            return impl_->fail(
                "client LAN session did not connect to its tunnel in time"
            );
        }
    }

    std::array<std::uint8_t, tunnelReadChunkBytes> buffer{};
    if (!impl_->localReadClosed
        && impl_->local != invalidTunnelSocket
        && impl_->localToTunnel.bufferedBytes()
            < impl_->config.maximumQueuedBytes) {
        const std::size_t freeQueueBytes =
            impl_->config.maximumQueuedBytes
            - impl_->localToTunnel.bufferedBytes();
        const std::size_t missingPrelude =
            impl_->mode == Impl::Mode::Client
                && !impl_->localPreludeAccepted
            ? runtimeInternetTcpTunnelPreludeBytes
                - impl_->receivedPrelude.size()
            : 0U;
        const std::size_t readCapacity = std::min(
            buffer.size(),
            freeQueueBytes + missingPrelude
        );
        const RuntimeInternetTcpStreamIoResult received = readSocket(
            impl_->local,
            std::span<std::uint8_t>(buffer).first(readCapacity)
        );
        if (received.status == RuntimeInternetTcpStreamIoStatus::Failed) {
            return impl_->fail(received.error);
        }
        if (received.status == RuntimeInternetTcpStreamIoStatus::Closed) {
            impl_->localReadClosed = true;
            closeSocket(impl_->local);
            progressed = true;
        }
        if (received.status == RuntimeInternetTcpStreamIoStatus::Progress) {
            std::span<const std::uint8_t> bytes(
                buffer.data(),
                received.bytesTransferred
            );
            result.bytesFromLocal += received.bytesTransferred;
            progressed = true;
            if (impl_->mode == Impl::Mode::Client
                && !impl_->localPreludeAccepted) {
                const std::size_t missing = runtimeInternetTcpTunnelPreludeBytes
                    - impl_->receivedPrelude.size();
                const std::size_t taken = std::min(missing, bytes.size());
                impl_->receivedPrelude.insert(
                    impl_->receivedPrelude.end(),
                    bytes.begin(),
                    bytes.begin() + static_cast<std::ptrdiff_t>(taken)
                );
                bytes = bytes.subspan(taken);
                if (impl_->receivedPrelude.size()
                    == runtimeInternetTcpTunnelPreludeBytes) {
                    if (!constantTimeEqual(
                            impl_->receivedPrelude,
                            impl_->endpoint.localProxyPrelude
                        )) {
                        return impl_->fail(
                            "local client tunnel prelude is invalid"
                        );
                    }
                    impl_->localPreludeAccepted = true;
                }
            }
            if (!bytes.empty()) {
                std::string queueError;
                if (!impl_->localPreludeAccepted
                    || !impl_->localToTunnel.append(
                        bytes,
                        impl_->config.maximumQueuedBytes,
                        [](const RuntimeNetworkPacket& packet, std::string& error) {
                            if (packet.type != Impl::heartbeatPacketType) return true;
                            error = "local LAN endpoint cannot author Internet carrier heartbeats";return false;
                        },
                        queueError
                    )) {
                    return impl_->fail(
                        queueError.empty()
                            ? "local tunnel data preceded its prelude"
                            : std::move(queueError)
                    );
                }
            }
        }
    }

    if (!impl_->localReadClosed && !impl_->tunnelReadClosed
        && impl_->tunnelToLocal.bufferedBytes()
        < impl_->config.maximumQueuedBytes) {
        const std::size_t readCapacity = std::min(
            buffer.size(),
            impl_->config.maximumQueuedBytes
                - impl_->tunnelToLocal.bufferedBytes()
        );
        RuntimeInternetTcpStreamIoResult received = impl_->tunnel->read(
            std::span<std::uint8_t>(buffer).first(readCapacity)
        );
        std::string ioError;
        if (!validIoResult(received, readCapacity, ioError)) {
            return impl_->fail(std::move(ioError));
        }
        if (received.status == RuntimeInternetTcpStreamIoStatus::Failed) {
            return impl_->fail(
                received.error.empty()
                    ? "authenticated tunnel receive failed"
                    : std::move(received.error)
            );
        }
        if (received.status == RuntimeInternetTcpStreamIoStatus::Closed) {
            impl_->tunnelReadClosed = true;
            impl_->localToTunnel.clear();
            progressed = true;
        }
        if (received.status == RuntimeInternetTcpStreamIoStatus::Progress) {
            std::string queueError;
            FramedQueue::Validator validator = [this](
                    const RuntimeNetworkPacket& packet,
                    std::string& error
                ) {
                    if (packet.type == Impl::heartbeatPacketType) {
                        if (impl_->config.heartbeatMilliseconds != 0 && packet.payload == impl_->heartbeatPacket().payload) return true;
                        error = "Internet carrier heartbeat does not match authenticated route";return false;
                    }
                    return impl_->mode != Impl::Mode::Host || impl_->validateHostAdmission(packet, error);
                };
            if (!impl_->tunnelToLocal.append(
                    std::span<const std::uint8_t>(
                        buffer.data(),
                        received.bytesTransferred
                    ),
                    impl_->config.maximumQueuedBytes,
                    validator,
                    queueError,
                    [](const RuntimeNetworkPacket& packet) { return packet.type != Impl::heartbeatPacketType; }
                )) {
                return impl_->fail(std::move(queueError));
            }
            result.bytesFromTunnel += received.bytesTransferred;
            impl_->lastTunnelReceiveMilliseconds = nowMilliseconds;
            progressed = true;
        }
    }

    if (impl_->config.heartbeatMilliseconds != 0 && !impl_->tunnelReadClosed && !impl_->localReadClosed) {
        if (nowMilliseconds >= impl_->nextHeartbeatMilliseconds) {
            std::string error;
            if (!impl_->localToTunnel.enqueuePacket(impl_->heartbeatPacket(), impl_->config.maximumQueuedBytes, error))
                return impl_->fail(std::move(error));
            impl_->nextHeartbeatMilliseconds = nowMilliseconds + impl_->config.heartbeatMilliseconds;
        }
        const auto receivedAt = impl_->lastTunnelReceiveMilliseconds != 0
            ? impl_->lastTunnelReceiveMilliseconds : impl_->startedMilliseconds;
        if (idleExpired(nowMilliseconds, receivedAt, impl_->config.heartbeatTimeoutMilliseconds))
            return impl_->fail("authenticated Internet relay heartbeat timed out");
    }

    const std::span<const std::uint8_t> toTunnel =
        impl_->localToTunnel.front();
    if (!toTunnel.empty() && !impl_->tunnelReadClosed) {
        RuntimeInternetTcpStreamIoResult sent = impl_->tunnel->write(toTunnel);
        std::string ioError;
        if (!validIoResult(sent, toTunnel.size(), ioError)) {
            return impl_->fail(std::move(ioError));
        }
        if (sent.status == RuntimeInternetTcpStreamIoStatus::Failed) {
            return impl_->fail(
                sent.error.empty()
                    ? "authenticated tunnel send failed"
                    : std::move(sent.error)
            );
        }
        if (sent.status == RuntimeInternetTcpStreamIoStatus::Closed) {
            return impl_->fail("authenticated tunnel peer closed while sending");
        }
        if (sent.status == RuntimeInternetTcpStreamIoStatus::Progress) {
            impl_->localToTunnel.consume(sent.bytesTransferred);
            progressed = true;
        }
    }

    const std::span<const std::uint8_t> toLocal =
        impl_->tunnelToLocal.front();
    if (!toLocal.empty() && impl_->local != invalidTunnelSocket
        && impl_->localPreludeAccepted) {
        const RuntimeInternetTcpStreamIoResult sent = writeSocket(
            impl_->local,
            toLocal
        );
        if (sent.status == RuntimeInternetTcpStreamIoStatus::Failed) {
            return impl_->fail(sent.error);
        }
        if (sent.status == RuntimeInternetTcpStreamIoStatus::Closed) {
            return impl_->fail("local LAN session closed while receiving");
        }
        if (sent.status == RuntimeInternetTcpStreamIoStatus::Progress) {
            impl_->tunnelToLocal.consume(sent.bytesTransferred);
            progressed = true;
        }
    }

    if (impl_->tunnelReadClosed
        && impl_->tunnelToLocal.bufferedBytes() == 0U) {
        closeSocket(impl_->listener);
        closeSocket(impl_->local);
        if (impl_->tunnel) impl_->tunnel->close();
        impl_->closed = true;
        result.closed = true;
        result.localConnected = false;
        result.localPreludeAccepted = impl_->localPreludeAccepted;
        result.remoteAdmissionVerified = impl_->remoteAdmissionVerified;
        return result;
    }
    if (impl_->localReadClosed) impl_->tunnelToLocal.clear();
    if (impl_->localReadClosed
        && impl_->localToTunnel.bufferedBytes() == 0U) {
        closeSocket(impl_->listener);
        closeSocket(impl_->local);
        if (impl_->tunnel) impl_->tunnel->close();
        impl_->closed = true;
        result.closed = true;
        result.localConnected = false;
        result.localPreludeAccepted = impl_->localPreludeAccepted;
        result.remoteAdmissionVerified = impl_->remoteAdmissionVerified;
        return result;
    }

    if (progressed) impl_->lastProgressMilliseconds = nowMilliseconds;
    if (idleExpired(
            nowMilliseconds,
            impl_->lastProgressMilliseconds,
            impl_->config.idleTimeoutMilliseconds
        )) {
        return impl_->fail("Internet TCP tunnel proxy idle timeout");
    }
    result.localConnected = impl_->local != invalidTunnelSocket;
    result.localPreludeAccepted = impl_->localPreludeAccepted;
    result.remoteAdmissionVerified = impl_->remoteAdmissionVerified;
    result.backpressured =
        impl_->localToTunnel.bufferedBytes()
                >= impl_->config.maximumQueuedBytes
        || impl_->tunnelToLocal.bufferedBytes()
                >= impl_->config.maximumQueuedBytes;
    return result;
}

RuntimeInternetTcpTunnelClientEndpoint
RuntimeInternetTcpTunnelProxy::clientEndpoint() const {
    return impl_ ? impl_->endpoint : RuntimeInternetTcpTunnelClientEndpoint{};
}

const RuntimeInternetTcpRouteDescriptor*
RuntimeInternetTcpTunnelProxy::route() const {
    return impl_ && impl_->mode != Impl::Mode::None ? &impl_->route : nullptr;
}

bool RuntimeInternetTcpTunnelProxy::isOpen() const {
    return impl_ && impl_->mode != Impl::Mode::None && !impl_->closed;
}

void RuntimeInternetTcpTunnelProxy::close() noexcept {
    if (impl_) impl_->reset();
}

struct RuntimeInternetTcpRelayPair::Impl {
    RuntimeInternetTcpTunnelProxyConfig config;
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> host;
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> remote;
    FramedQueue hostToRemote;
    FramedQueue remoteToHost;
    std::uint64_t lastProgressMilliseconds = 0;
    std::optional<ActiveRelayRouteKey> activeRoute;
    bool hostReadClosed = false;
    bool remoteReadClosed = false;
    bool closed = false;

    void reset() noexcept {
        if (host) host->close();
        if (remote) remote->close();
        host.reset();
        remote.reset();
        hostToRemote.clear();
        remoteToHost.clear();
        lastProgressMilliseconds = 0;
        hostReadClosed = false;
        remoteReadClosed = false;
        if (activeRoute.has_value()) {
            releaseRelayRoute(*activeRoute);
            activeRoute.reset();
        }
        closed = false;
    }

    RuntimeInternetTcpRelayPairPollResult fail(std::string errorMessage) {
        RuntimeInternetTcpRelayPairPollResult result;
        result.succeeded = false;
        result.closed = true;
        result.error = std::move(errorMessage);
        if (host) host->close();
        if (remote) remote->close();
        if (activeRoute.has_value()) {
            releaseRelayRoute(*activeRoute);
            activeRoute.reset();
        }
        closed = true;
        return result;
    }

    RuntimeInternetTcpRelayPairPollResult finishOrderlyClose() {
        RuntimeInternetTcpRelayPairPollResult result;
        result.closed = true;
        if (host) host->close();
        if (remote) remote->close();
        if (activeRoute.has_value()) {
            releaseRelayRoute(*activeRoute);
            activeRoute.reset();
        }
        closed = true;
        return result;
    }
};

RuntimeInternetTcpRelayPair::RuntimeInternetTcpRelayPair()
    : impl_(std::make_unique<Impl>()) {}
RuntimeInternetTcpRelayPair::~RuntimeInternetTcpRelayPair() { close(); }
RuntimeInternetTcpRelayPair::RuntimeInternetTcpRelayPair(
    RuntimeInternetTcpRelayPair&&
) noexcept = default;
RuntimeInternetTcpRelayPair& RuntimeInternetTcpRelayPair::operator=(
    RuntimeInternetTcpRelayPair&& other
) noexcept {
    if (this != &other) {
        close();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool RuntimeInternetTcpRelayPair::open(
    RuntimeInternetTcpTunnelProxyConfig config,
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> hostSide,
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> remoteSide,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->reset();
    if (!validProxyConfig(config, errorMessage)
        || hostSide == nullptr || remoteSide == nullptr) {
        if (errorMessage.empty()) errorMessage = "relay pair streams are required";
        return false;
    }
    const auto& hostIdentity = hostSide->identity();
    const auto& remoteIdentity = remoteSide->identity();
    const auto& hostRoute = hostIdentity.route;
    const auto& remoteRoute = remoteIdentity.route;
    if (!validRoute(hostRoute, errorMessage)
        || !validRoute(remoteRoute, errorMessage)
        || !identityMatchesRoute(hostIdentity, hostRoute, errorMessage)
        || !identityMatchesRoute(remoteIdentity, remoteRoute, errorMessage)
        || hostRoute.localRole != RuntimeInternetRole::Host
        || remoteRoute.localRole == RuntimeInternetRole::Host
        || hostRoute.serviceId != remoteRoute.serviceId
        || hostRoute.roomId != remoteRoute.roomId
        || hostRoute.connectionId != remoteRoute.connectionId
        || hostRoute.localPeerId != remoteRoute.remotePeerId
        || hostRoute.remotePeerId != remoteRoute.localPeerId
        || hostRoute.localRole != remoteRoute.remoteRole
        || hostRoute.remoteRole != remoteRoute.localRole) {
        if (errorMessage.empty()) {
            errorMessage =
                "relay pair identities are not complementary and authenticated";
        }
        return false;
    }
    const ActiveRelayRouteKey activeRoute = relayRouteKey(hostRoute);
    if (!reserveRelayRoute(activeRoute)) {
        errorMessage =
            "relay route is already active; duplicate binding was rejected";
        return false;
    }
    impl_->config = config;
    impl_->host = std::move(hostSide);
    impl_->remote = std::move(remoteSide);
    impl_->activeRoute = activeRoute;
    impl_->lastProgressMilliseconds = nowMilliseconds;
    errorMessage.clear();
    return true;
}

RuntimeInternetTcpRelayPairPollResult RuntimeInternetTcpRelayPair::poll(
    std::uint64_t nowMilliseconds
) {
    if (!impl_ || !impl_->host || !impl_->remote || impl_->closed) {
        RuntimeInternetTcpRelayPairPollResult result;
        result.succeeded = false;
        result.closed = true;
        result.error = "Internet TCP relay pair is not open";
        return result;
    }
    RuntimeInternetTcpRelayPairPollResult result;
    bool progressed = false;
    std::array<std::uint8_t, tunnelReadChunkBytes> buffer{};

    const auto readSide = [&](
        RuntimeInternetAuthenticatedTcpStream& stream,
        FramedQueue& queue,
        std::uint64_t& byteCounter,
        bool& readClosed
    ) -> std::string {
        if (readClosed) return {};
        if (queue.bufferedBytes() >= impl_->config.maximumQueuedBytes) return {};
        const std::size_t readCapacity = std::min(
            buffer.size(),
            impl_->config.maximumQueuedBytes - queue.bufferedBytes()
        );
        RuntimeInternetTcpStreamIoResult received = stream.read(
            std::span<std::uint8_t>(buffer).first(readCapacity)
        );
        std::string ioError;
        if (!validIoResult(received, readCapacity, ioError)) return ioError;
        if (received.status == RuntimeInternetTcpStreamIoStatus::Failed) {
            return received.error.empty()
                ? "authenticated relay stream receive failed"
                : std::move(received.error);
        }
        if (received.status == RuntimeInternetTcpStreamIoStatus::Closed) {
            readClosed = true;
            progressed = true;
            return {};
        }
        if (received.status == RuntimeInternetTcpStreamIoStatus::Progress) {
            if (!queue.append(
                    std::span<const std::uint8_t>(
                        buffer.data(),
                        received.bytesTransferred
                    ),
                    impl_->config.maximumQueuedBytes,
                    {},
                    ioError
                )) {
                return ioError;
            }
            byteCounter += received.bytesTransferred;
            progressed = true;
        }
        return {};
    };
    std::string failure = readSide(
        *impl_->host,
        impl_->hostToRemote,
        result.bytesHostToRemote,
        impl_->hostReadClosed
    );
    if (!failure.empty()) return impl_->fail(std::move(failure));
    if (!impl_->hostReadClosed) {
        failure = readSide(
            *impl_->remote,
            impl_->remoteToHost,
            result.bytesRemoteToHost,
            impl_->remoteReadClosed
        );
        if (!failure.empty()) return impl_->fail(std::move(failure));
    }

    if (impl_->hostReadClosed) impl_->remoteToHost.clear();
    if (impl_->remoteReadClosed) impl_->hostToRemote.clear();

    const auto writeSide = [&](
        RuntimeInternetAuthenticatedTcpStream& stream,
        FramedQueue& queue
    ) -> std::string {
        const std::span<const std::uint8_t> bytes = queue.front();
        if (bytes.empty()) return {};
        RuntimeInternetTcpStreamIoResult sent = stream.write(bytes);
        std::string ioError;
        if (!validIoResult(sent, bytes.size(), ioError)) return ioError;
        if (sent.status == RuntimeInternetTcpStreamIoStatus::Failed) {
            return sent.error.empty()
                ? "authenticated relay stream send failed"
                : std::move(sent.error);
        }
        if (sent.status == RuntimeInternetTcpStreamIoStatus::Closed) {
            return "authenticated relay stream closed while sending";
        }
        if (sent.status == RuntimeInternetTcpStreamIoStatus::Progress) {
            queue.consume(sent.bytesTransferred);
            progressed = true;
        }
        return {};
    };
    if (!impl_->remoteReadClosed) {
        failure = writeSide(*impl_->remote, impl_->hostToRemote);
        if (!failure.empty()) return impl_->fail(std::move(failure));
    }
    if (!impl_->hostReadClosed) {
        failure = writeSide(*impl_->host, impl_->remoteToHost);
        if (!failure.empty()) return impl_->fail(std::move(failure));
    }

    if ((impl_->hostReadClosed
            && impl_->hostToRemote.bufferedBytes() == 0U)
        || (impl_->remoteReadClosed
            && impl_->remoteToHost.bufferedBytes() == 0U)) {
        RuntimeInternetTcpRelayPairPollResult closed =
            impl_->finishOrderlyClose();
        closed.bytesHostToRemote = result.bytesHostToRemote;
        closed.bytesRemoteToHost = result.bytesRemoteToHost;
        return closed;
    }

    if (progressed) impl_->lastProgressMilliseconds = nowMilliseconds;
    if (idleExpired(
            nowMilliseconds,
            impl_->lastProgressMilliseconds,
            impl_->config.idleTimeoutMilliseconds
        )) {
        return impl_->fail("Internet TCP relay pair idle timeout");
    }
    result.backpressured =
        impl_->hostToRemote.bufferedBytes()
                >= impl_->config.maximumQueuedBytes
        || impl_->remoteToHost.bufferedBytes()
                >= impl_->config.maximumQueuedBytes;
    return result;
}

bool RuntimeInternetTcpRelayPair::isOpen() const {
    return impl_ && impl_->host && impl_->remote && !impl_->closed;
}

void RuntimeInternetTcpRelayPair::close() noexcept {
    if (impl_) impl_->reset();
}
