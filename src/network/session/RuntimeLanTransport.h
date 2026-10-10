#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

inline constexpr std::uint16_t runtimeLanProtocolVersion = 1;
inline constexpr std::uint16_t runtimeLanDefaultDiscoveryPort = 17815;
inline constexpr std::uint16_t runtimeLanDefaultSessionPort = 17816;
inline constexpr std::size_t runtimeLanMaximumPacketBytes = 1024U * 1024U;
inline constexpr std::size_t runtimeLanMaximumTcpClients = 32;
inline constexpr std::size_t runtimeLanMaximumDiscoveredRooms = 256;
inline constexpr std::size_t runtimeLanMaximumQueuedBytesPerPeer =
    4U * 1024U * 1024U;

struct RuntimeNetworkPacket {
    std::uint16_t type = 0;
    std::vector<std::uint8_t> payload;
};

struct RuntimeLanRoomAdvertisement {
    std::string roomId;
    std::string roomName;
    std::string hostName;
    std::string hostAddress;
    std::string contentFingerprint;
    std::uint16_t protocolVersion = runtimeLanProtocolVersion;
    std::uint16_t sessionPort = runtimeLanDefaultSessionPort;
    std::uint8_t playerCount = 1;
    std::uint8_t maximumPlayers = 2;
    bool passwordProtected = false;
    std::uint64_t lastSeenMilliseconds = 0;
};

// Hosts answer small UDP discovery probes. The browser keeps the source IP
// from the datagram instead of trusting an address supplied by the host.
class RuntimeLanDiscoveryHost {
public:
    RuntimeLanDiscoveryHost();
    ~RuntimeLanDiscoveryHost();
    RuntimeLanDiscoveryHost(RuntimeLanDiscoveryHost&&) noexcept;
    RuntimeLanDiscoveryHost& operator=(RuntimeLanDiscoveryHost&&) noexcept;
    RuntimeLanDiscoveryHost(const RuntimeLanDiscoveryHost&) = delete;
    RuntimeLanDiscoveryHost& operator=(const RuntimeLanDiscoveryHost&) = delete;

    bool open(
        std::uint16_t discoveryPort,
        RuntimeLanRoomAdvertisement advertisement,
        std::string& errorMessage
    );
    bool updateAdvertisement(
        RuntimeLanRoomAdvertisement advertisement,
        std::string& errorMessage
    );
    // Processes all probes already waiting, or waits up to timeoutMilliseconds
    // for the first one. Returns the number of replies sent.
    int poll(int timeoutMilliseconds, std::string& errorMessage);
    void close();
    bool isOpen() const;
    std::uint16_t port() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class RuntimeLanDiscoveryBrowser {
public:
    RuntimeLanDiscoveryBrowser();
    ~RuntimeLanDiscoveryBrowser();
    RuntimeLanDiscoveryBrowser(RuntimeLanDiscoveryBrowser&&) noexcept;
    RuntimeLanDiscoveryBrowser& operator=(RuntimeLanDiscoveryBrowser&&) noexcept;
    RuntimeLanDiscoveryBrowser(const RuntimeLanDiscoveryBrowser&) = delete;
    RuntimeLanDiscoveryBrowser& operator=(const RuntimeLanDiscoveryBrowser&) = delete;

    bool open(std::uint16_t discoveryPort, std::string& errorMessage);
    bool sendProbe(
        const std::string& targetAddress,
        std::string& errorMessage
    );
    // Receives advertisements and refreshes matching room records. Returns the
    // number of valid advertisements processed.
    int poll(int timeoutMilliseconds, std::string& errorMessage);
    std::vector<RuntimeLanRoomAdvertisement> rooms(
        std::uint64_t maximumAgeMilliseconds = 5000
    ) const;
    void close();
    bool isOpen() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class RuntimeTcpEventKind : std::uint8_t {
    Connected,
    Disconnected,
    Packet,
    Error
};

// LAN hosts listen on every IPv4 interface by default. Internet tunnel
// endpoints can instead keep the authoritative session listener private to
// this machine and expose only an authenticated proxy.
enum class RuntimeTcpBindScope : std::uint8_t {
    AnyIpv4,
    LoopbackIpv4
};

struct RuntimeTcpEvent {
    RuntimeTcpEventKind kind = RuntimeTcpEventKind::Error;
    std::uint64_t peerId = 0;
    std::string peerAddress;
    RuntimeNetworkPacket packet;
    std::string message;
};

// Framed, length-limited TCP transport used by the multiplayer session layer.
// It deliberately does not interpret lobby/game messages; malformed framing
// disconnects only the offending peer and produces an Error event.
class RuntimeTcpServer {
public:
    RuntimeTcpServer();
    ~RuntimeTcpServer();
    RuntimeTcpServer(RuntimeTcpServer&&) noexcept;
    RuntimeTcpServer& operator=(RuntimeTcpServer&&) noexcept;
    RuntimeTcpServer(const RuntimeTcpServer&) = delete;
    RuntimeTcpServer& operator=(const RuntimeTcpServer&) = delete;

    bool open(
        std::uint16_t port,
        std::size_t maximumClients,
        std::string& errorMessage
    );
    bool open(
        std::uint16_t port,
        std::size_t maximumClients,
        RuntimeTcpBindScope bindScope,
        std::string& errorMessage
    );
    std::vector<RuntimeTcpEvent> poll(
        int timeoutMilliseconds,
        std::string& errorMessage
    );
    // Queues a packet without blocking. poll() flushes pending bytes whenever
    // the socket becomes writable and reports asynchronous failures.
    bool send(
        std::uint64_t peerId,
        const RuntimeNetworkPacket& packet,
        std::string& errorMessage
    );
    // Queues the packet independently for every current peer.
    std::size_t broadcast(
        const RuntimeNetworkPacket& packet,
        std::string& errorMessage
    );
    bool disconnect(std::uint64_t peerId);
    void close();
    bool isOpen() const;
    std::uint16_t port() const;
    std::size_t clientCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class RuntimeTcpClient {
public:
    RuntimeTcpClient();
    ~RuntimeTcpClient();
    RuntimeTcpClient(RuntimeTcpClient&&) noexcept;
    RuntimeTcpClient& operator=(RuntimeTcpClient&&) noexcept;
    RuntimeTcpClient(const RuntimeTcpClient&) = delete;
    RuntimeTcpClient& operator=(const RuntimeTcpClient&) = delete;

    bool connect(
        const std::string& host,
        std::uint16_t port,
        int timeoutMilliseconds,
        std::string& errorMessage
    );
    // Optional raw bytes sent before the first framed packet. This is reserved
    // for a same-machine authenticated proxy prelude and is never interpreted
    // by the LAN protocol itself.
    bool connect(
        const std::string& host,
        std::uint16_t port,
        int timeoutMilliseconds,
        std::span<const std::uint8_t> localProxyPrelude,
        std::string& errorMessage
    );
    std::vector<RuntimeTcpEvent> poll(
        int timeoutMilliseconds,
        std::string& errorMessage
    );
    // Queues a packet without blocking; call poll() to flush it.
    bool send(
        const RuntimeNetworkPacket& packet,
        std::string& errorMessage
    );
    void close();
    bool isConnected() const;
    const std::string& peerAddress() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Public codec helpers let protocol tests prove truncated, oversized and
// corrupt frames are rejected without opening a socket.
bool encodeRuntimeNetworkPacket(
    const RuntimeNetworkPacket& packet,
    std::vector<std::uint8_t>& bytes,
    std::string& errorMessage
);

// complete is false when bytes contains only a valid frame prefix. consumed is
// set only for a complete frame. Extra bytes after one frame are allowed.
bool decodeRuntimeNetworkPacket(
    const std::vector<std::uint8_t>& bytes,
    RuntimeNetworkPacket& packet,
    std::size_t& consumed,
    bool& complete,
    std::string& errorMessage
);
