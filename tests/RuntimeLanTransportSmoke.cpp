#include "RuntimeLanTransport.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

bool hasEvent(
    const std::vector<RuntimeTcpEvent>& events,
    RuntimeTcpEventKind kind,
    std::uint16_t packetType = 0
) {
    return std::any_of(events.begin(), events.end(), [&](const auto& event) {
        return event.kind == kind
            && (packetType == 0 || event.packet.type == packetType);
    });
}

std::size_t countPacketEvents(const std::vector<RuntimeTcpEvent>& events) {
    return static_cast<std::size_t>(std::count_if(
        events.begin(),
        events.end(),
        [](const auto& event) {
            return event.kind == RuntimeTcpEventKind::Packet;
        }
    ));
}

std::uint64_t connectedPeerId(const std::vector<RuntimeTcpEvent>& events) {
    const auto found = std::find_if(events.begin(), events.end(), [](const auto& event) {
        return event.kind == RuntimeTcpEventKind::Connected;
    });
    return found == events.end() ? 0 : found->peerId;
}

bool require(bool condition, const std::string& message) {
    if (condition) return true;
    std::cerr << "Runtime LAN transport smoke failed: " << message << "\n";
    return false;
}

} // namespace

int main() {
    std::string error;

    RuntimeNetworkPacket original;
    original.type = 42;
    original.payload = {0, 1, 2, 3, 0xff};
    std::vector<std::uint8_t> encoded;
    if (!require(
            encodeRuntimeNetworkPacket(original, encoded, error),
            "valid packet did not encode: " + error
        )) {
        return 1;
    }
    RuntimeNetworkPacket decoded;
    std::size_t consumed = 0;
    bool complete = false;
    std::vector<std::uint8_t> combined = encoded;
    combined.insert(combined.end(), encoded.begin(), encoded.end());
    if (!require(
            decodeRuntimeNetworkPacket(
                combined,
                decoded,
                consumed,
                complete,
                error
            )
                && complete
                && consumed == encoded.size()
                && decoded.type == original.type
                && decoded.payload == original.payload,
            "framed packet round trip is invalid: " + error
        )) {
        return 1;
    }
    std::vector<std::uint8_t> prefix(encoded.begin(), encoded.end() - 1);
    consumed = 99;
    complete = true;
    if (!require(
            decodeRuntimeNetworkPacket(
                prefix,
                decoded,
                consumed,
                complete,
                error
            )
                && !complete
                && consumed == 0,
            "valid truncated frame prefix was not retained"
        )) {
        return 1;
    }
    std::vector<std::uint8_t> corrupt = encoded;
    corrupt[0] ^= 0xffU;
    if (!require(
            !decodeRuntimeNetworkPacket(
                corrupt,
                decoded,
                consumed,
                complete,
                error
            ),
            "corrupt packet magic was accepted"
        )) {
        return 1;
    }
    std::vector<std::uint8_t> oversizedHeader = encoded;
    const std::uint32_t declaredOversize = static_cast<std::uint32_t>(
        runtimeLanMaximumPacketBytes + 1U
    );
    oversizedHeader[8] = static_cast<std::uint8_t>(declaredOversize >> 24U);
    oversizedHeader[9] = static_cast<std::uint8_t>(declaredOversize >> 16U);
    oversizedHeader[10] = static_cast<std::uint8_t>(declaredOversize >> 8U);
    oversizedHeader[11] = static_cast<std::uint8_t>(declaredOversize);
    if (!require(
            !decodeRuntimeNetworkPacket(
                oversizedHeader,
                decoded,
                consumed,
                complete,
                error
            ),
            "oversized declared payload was accepted"
        )) {
        return 1;
    }
    RuntimeNetworkPacket invalidType;
    if (!require(
            !encodeRuntimeNetworkPacket(invalidType, encoded, error),
            "reserved packet type was accepted"
        )) {
        return 1;
    }
    RuntimeNetworkPacket oversized;
    oversized.type = 1;
    oversized.payload.resize(runtimeLanMaximumPacketBytes + 1U);
    if (!require(
            !encodeRuntimeNetworkPacket(oversized, encoded, error),
            "oversized packet was accepted"
        )) {
        return 1;
    }

    RuntimeTcpServer invalidServer;
    if (!require(
            !invalidServer.open(
                0,
                runtimeLanMaximumTcpClients + 1U,
                error
            ),
            "TCP server accepted a client limit above its select capacity"
        )) {
        return 1;
    }

    RuntimeTcpServer fairnessServer;
    constexpr std::size_t fairnessClientCount = 17;
    if (!require(
            fairnessServer.open(0, fairnessClientCount, error),
            "fairness TCP server open failed: " + error
        )) {
        return 1;
    }
    std::vector<RuntimeTcpClient> fairnessClients;
    fairnessClients.reserve(fairnessClientCount);
    for (std::size_t clientIndex = 0;
         clientIndex < fairnessClientCount;
         ++clientIndex) {
        fairnessClients.emplace_back();
        if (!require(
                fairnessClients.back().connect(
                    "127.0.0.1",
                    fairnessServer.port(),
                    1000,
                    error
                ),
                "fairness TCP client connect failed: " + error
            )) {
            return 1;
        }
        const auto accepted = fairnessServer.poll(1000, error);
        if (!require(
                connectedPeerId(accepted) != 0,
                "fairness TCP client was not accepted: " + error
            )) {
            return 1;
        }
    }
    RuntimeNetworkPacket fairnessPacket;
    fairnessPacket.type = 109;
    for (RuntimeTcpClient& client : fairnessClients) {
        for (std::size_t packetIndex = 0; packetIndex < 300; ++packetIndex) {
            fairnessPacket.payload = {
                static_cast<std::uint8_t>(packetIndex >> 8U),
                static_cast<std::uint8_t>(packetIndex & 0xffU)
            };
            if (!require(
                    client.send(fairnessPacket, error),
                    "fairness packet burst did not queue: " + error
                )) {
                return 1;
            }
        }
        const auto flushed = client.poll(1000, error);
        if (!require(
                error.empty()
                    && !hasEvent(flushed, RuntimeTcpEventKind::Error),
                "fairness packet burst did not flush: " + error
            )) {
            return 1;
        }
    }
    const auto fairnessEvents = fairnessServer.poll(1000, error);
    std::map<std::uint64_t, std::size_t> packetsPerPeer;
    for (const RuntimeTcpEvent& event : fairnessEvents) {
        if (event.kind == RuntimeTcpEventKind::Packet) {
            ++packetsPerPeer[event.peerId];
        }
    }
    const std::size_t expectedFairBudget = 4096 / fairnessClientCount;
    const bool everyPeerServed = packetsPerPeer.size() == fairnessClientCount
        && std::all_of(
            packetsPerPeer.begin(),
            packetsPerPeer.end(),
            [expectedFairBudget](const auto& entry) {
                return entry.second > 0
                    && entry.second <= expectedFairBudget;
            }
        );
    if (!require(
            error.empty() && everyPeerServed,
            "global receive budget starved one or more TCP peers"
        )) {
        return 1;
    }
    for (RuntimeTcpClient& client : fairnessClients) client.close();
    fairnessServer.close();

    RuntimeTcpServer server;
    if (!require(server.open(0, 2, error), "TCP server open failed: " + error)
        || !require(server.port() != 0, "TCP server did not publish its bound port")) {
        return 1;
    }

    RuntimeLanRoomAdvertisement advertised;
    advertised.roomId = "loopback-room";
    advertised.roomName = "Concord LAN Smoke";
    advertised.hostName = "Host";
    advertised.contentFingerprint = "map-rules-mod:0123456789abcdef";
    advertised.sessionPort = server.port();
    advertised.playerCount = 1;
    advertised.maximumPlayers = 2;
    RuntimeLanDiscoveryHost discoveryHost;
    if (!require(
            discoveryHost.open(0, advertised, error),
            "discovery host open failed: " + error
        )
        || !require(discoveryHost.port() != 0, "discovery host bound port is zero")) {
        return 1;
    }
    RuntimeLanDiscoveryBrowser browser;
    if (!require(
            browser.open(discoveryHost.port(), error),
            "discovery browser open failed: " + error
        )
        || !require(
            browser.sendProbe("127.0.0.1", error),
            "discovery probe failed: " + error
        )
        || !require(
            discoveryHost.poll(1000, error) == 1,
            "discovery host did not answer one loopback probe: " + error
        )
        || !require(
            browser.poll(1000, error) == 1,
            "discovery browser did not receive one advertisement: " + error
        )) {
        return 1;
    }
    std::vector<RuntimeLanRoomAdvertisement> rooms = browser.rooms();
    if (!require(
            rooms.size() == 1
                && rooms.front().roomId == advertised.roomId
                && rooms.front().roomName == advertised.roomName
                && rooms.front().hostAddress == "127.0.0.1"
                && rooms.front().sessionPort == server.port()
                && rooms.front().contentFingerprint
                    == advertised.contentFingerprint,
            "received LAN advertisement fields do not match the host"
        )) {
        return 1;
    }
    advertised.playerCount = 2;
    advertised.sessionPort = server.port() == 65535
        ? static_cast<std::uint16_t>(server.port() - 1)
        : static_cast<std::uint16_t>(server.port() + 1);
    if (!require(
            discoveryHost.updateAdvertisement(advertised, error)
                && browser.sendProbe("127.0.0.1", error)
                && discoveryHost.poll(1000, error) == 1
                && browser.poll(1000, error) == 1,
            "updated discovery advertisement exchange failed: " + error
        )) {
        return 1;
    }
    rooms = browser.rooms();
    if (!require(
            rooms.size() == 1
                && rooms.front().playerCount == 2
                && rooms.front().sessionPort == advertised.sessionPort,
            "room refresh created a duplicate or retained stale endpoint data"
        )) {
        return 1;
    }

    RuntimeTcpClient firstClient;
    if (!require(
            firstClient.connect("127.0.0.1", server.port(), 1000, error),
            "first TCP client connect failed: " + error
        )) {
        return 1;
    }
    std::vector<RuntimeTcpEvent> serverEvents = server.poll(1000, error);
    const std::uint64_t firstPeer = connectedPeerId(serverEvents);
    if (!require(
            firstPeer != 0 && server.clientCount() == 1,
            "server did not accept first client: " + error
        )) {
        return 1;
    }
    RuntimeNetworkPacket hello;
    hello.type = 100;
    hello.payload = {'h', 'e', 'l', 'l', 'o'};
    if (!require(firstClient.send(hello, error), "client send failed: " + error)) {
        return 1;
    }
    const auto helloFlush = firstClient.poll(1000, error);
    if (!require(
            error.empty()
                && !hasEvent(helloFlush, RuntimeTcpEventKind::Error)
                && !hasEvent(helloFlush, RuntimeTcpEventKind::Disconnected),
            "client send queue flush failed: " + error
        )) {
        return 1;
    }
    serverEvents = server.poll(1000, error);
    if (!require(
            hasEvent(serverEvents, RuntimeTcpEventKind::Packet, hello.type)
                && serverEvents.back().packet.payload == hello.payload,
            "server did not receive the framed client packet: " + error
        )) {
        return 1;
    }

    RuntimeNetworkPacket floodProbe;
    floodProbe.type = 103;
    for (std::size_t index = 0; index < 300; ++index) {
        floodProbe.payload = {
            static_cast<std::uint8_t>(index >> 8U),
            static_cast<std::uint8_t>(index & 0xffU)
        };
        if (!require(
                firstClient.send(floodProbe, error),
                "bounded packet burst did not queue: " + error
            )) {
            return 1;
        }
    }
    const auto burstFlush = firstClient.poll(1000, error);
    if (!require(
            error.empty()
                && !hasEvent(burstFlush, RuntimeTcpEventKind::Error)
                && !hasEvent(burstFlush, RuntimeTcpEventKind::Disconnected),
            "bounded packet burst did not flush: " + error
        )) {
        return 1;
    }
    std::size_t receivedBurstPackets = 0;
    for (std::size_t attempt = 0;
         attempt < 20 && receivedBurstPackets < 300;
         ++attempt) {
        serverEvents = server.poll(1000, error);
        const std::size_t packetEvents = countPacketEvents(serverEvents);
        if (!require(
                error.empty()
                    && packetEvents <= 256
                    && !hasEvent(serverEvents, RuntimeTcpEventKind::Error),
                "packet event budget was exceeded: " + error
            )) {
            return 1;
        }
        for (const RuntimeTcpEvent& event : serverEvents) {
            if (event.kind != RuntimeTcpEventKind::Packet) continue;
            const std::vector<std::uint8_t> expected = {
                static_cast<std::uint8_t>(receivedBurstPackets >> 8U),
                static_cast<std::uint8_t>(receivedBurstPackets & 0xffU)
            };
            if (!require(
                    event.packet.type == floodProbe.type
                        && event.packet.payload == expected,
                    "bounded packet burst order or identity changed"
                )) {
                return 1;
            }
            ++receivedBurstPackets;
        }
    }
    if (!require(
            receivedBurstPackets == 300,
            "buffered packet burst was lost across bounded polls"
        )) {
        return 1;
    }

    RuntimeNetworkPacket maximumFrame;
    maximumFrame.type = 105;
    maximumFrame.payload.resize(runtimeLanMaximumPacketBytes, 0x5aU);
    RuntimeNetworkPacket afterMaximum;
    afterMaximum.type = 106;
    afterMaximum.payload = {0xdeU, 0xadU, 0xbeU, 0xefU};
    if (!require(
            firstClient.send(maximumFrame, error)
                && firstClient.send(afterMaximum, error),
            "maximum-size frame pair did not queue: " + error
        )) {
        return 1;
    }
    bool receivedMaximum = false;
    bool receivedAfterMaximum = false;
    for (std::size_t attempt = 0;
         attempt < 200 && (!receivedMaximum || !receivedAfterMaximum);
         ++attempt) {
        const auto senderEvents = firstClient.poll(0, error);
        if (!require(
                error.empty()
                    && !hasEvent(senderEvents, RuntimeTcpEventKind::Error)
                    && !hasEvent(senderEvents, RuntimeTcpEventKind::Disconnected),
                "maximum-size frame flush failed: " + error
            )) {
            return 1;
        }
        serverEvents = server.poll(1000, error);
        if (!require(
                error.empty()
                    && !hasEvent(serverEvents, RuntimeTcpEventKind::Error),
                "maximum-size frame receive failed: " + error
            )) {
            return 1;
        }
        for (const RuntimeTcpEvent& event : serverEvents) {
            if (event.kind != RuntimeTcpEventKind::Packet) continue;
            if (event.packet.type == maximumFrame.type) {
                receivedMaximum = event.packet.payload == maximumFrame.payload;
            } else if (event.packet.type == afterMaximum.type) {
                receivedAfterMaximum = event.packet.payload
                    == afterMaximum.payload;
            }
        }
    }
    if (!require(
            receivedMaximum && receivedAfterMaximum,
            "maximum frame followed by another frame was not preserved"
        )) {
        return 1;
    }

    RuntimeNetworkPacket welcome;
    welcome.type = 101;
    welcome.payload = {'o', 'k'};
    if (!require(
            server.send(firstPeer, welcome, error),
            "server reply failed: " + error
        )) {
        return 1;
    }
    const auto replyFlush = server.poll(1000, error);
    if (!require(
            error.empty()
                && !hasEvent(replyFlush, RuntimeTcpEventKind::Error)
                && !hasEvent(replyFlush, RuntimeTcpEventKind::Disconnected),
            "server reply queue flush failed: " + error
        )) {
        return 1;
    }
    std::vector<RuntimeTcpEvent> clientEvents = firstClient.poll(1000, error);
    if (!require(
            hasEvent(clientEvents, RuntimeTcpEventKind::Packet, welcome.type)
                && clientEvents.back().packet.payload == welcome.payload,
            "client did not receive the server reply: " + error
        )) {
        return 1;
    }

    RuntimeTcpClient secondClient;
    if (!require(
            secondClient.connect("127.0.0.1", server.port(), 1000, error),
            "second TCP client connect failed: " + error
        )) {
        return 1;
    }
    serverEvents = server.poll(1000, error);
    const std::uint64_t secondPeer = connectedPeerId(serverEvents);
    if (!require(
            secondPeer != 0 && secondPeer != firstPeer && server.clientCount() == 2,
            "server did not assign a stable second peer id"
        )) {
        return 1;
    }
    RuntimeNetworkPacket start;
    start.type = 102;
    start.payload = {7, 8, 9};
    if (!require(
            server.broadcast(start, error) == 2,
            "server did not broadcast to both peers: " + error
        )) {
        return 1;
    }
    const auto broadcastFlush = server.poll(1000, error);
    if (!require(
            error.empty()
                && !hasEvent(broadcastFlush, RuntimeTcpEventKind::Error)
                && !hasEvent(broadcastFlush, RuntimeTcpEventKind::Disconnected),
            "broadcast queue flush failed: " + error
        )) {
        return 1;
    }
    const auto firstBroadcast = firstClient.poll(1000, error);
    const auto secondBroadcast = secondClient.poll(1000, error);
    if (!require(
            hasEvent(firstBroadcast, RuntimeTcpEventKind::Packet, start.type)
                && hasEvent(secondBroadcast, RuntimeTcpEventKind::Packet, start.type),
            "one or more clients missed the broadcast"
        )) {
        return 1;
    }

    secondClient.close();
    serverEvents = server.poll(1000, error);
    if (!require(
            hasEvent(serverEvents, RuntimeTcpEventKind::Disconnected)
                && server.clientCount() == 1,
            "server did not observe the second client disconnect"
        )) {
        return 1;
    }
    if (!require(
            server.disconnect(firstPeer) && server.clientCount() == 0,
            "explicit peer disconnect failed"
        )) {
        return 1;
    }
    clientEvents = firstClient.poll(1000, error);
    if (!require(
            hasEvent(clientEvents, RuntimeTcpEventKind::Disconnected)
                && !firstClient.isConnected(),
            "client did not observe the server disconnect"
        )) {
        return 1;
    }

    RuntimeTcpClient overflowClient;
    if (!require(
            overflowClient.connect("127.0.0.1", server.port(), 1000, error),
            "queue-limit client connect failed: " + error
        )) {
        return 1;
    }
    serverEvents = server.poll(1000, error);
    const std::uint64_t overflowPeer = connectedPeerId(serverEvents);
    if (!require(overflowPeer != 0, "queue-limit client was not accepted")) {
        return 1;
    }
    RuntimeNetworkPacket queueFill;
    queueFill.type = 104;
    queueFill.payload.resize(runtimeLanMaximumPacketBytes, 0xa5U);
    for (std::size_t index = 0; index < 3; ++index) {
        if (!require(
                server.send(overflowPeer, queueFill, error),
                "valid bounded send queue fill was rejected: " + error
            )) {
            return 1;
        }
    }
    if (!require(
            !server.send(overflowPeer, queueFill, error)
                && server.clientCount() == 0,
            "send queue overflow did not disconnect the identified peer"
        )) {
        return 1;
    }
    serverEvents = server.poll(0, error);
    const bool identifiedOverflow = std::any_of(
        serverEvents.begin(),
        serverEvents.end(),
        [overflowPeer](const auto& event) {
            return event.peerId == overflowPeer
                && event.kind == RuntimeTcpEventKind::Error;
        }
    ) && std::any_of(
        serverEvents.begin(),
        serverEvents.end(),
        [overflowPeer](const auto& event) {
            return event.peerId == overflowPeer
                && event.kind == RuntimeTcpEventKind::Disconnected;
        }
    );
    if (!require(
            identifiedOverflow,
            "send queue overflow did not report its failed peer"
        )) {
        return 1;
    }

    std::cout << "Runtime LAN transport smoke passed: "
              << "discovery=loopback framedTcp=two-peer budgets=bounded "
              << "limits=validated\n";
    return 0;
}
