#include "RuntimeInternetTcpTunnelProxy.h"

#include "RuntimeMultiplayerLanSession.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct SmokeContext {
    int failures = 0;

    void expect(bool condition, const std::string& message) {
        if (condition) return;
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
};

std::uint64_t nowMilliseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

struct MemoryFlow {
    std::vector<std::uint8_t> bytes;
    std::size_t offset = 0;

    std::size_t available() const { return bytes.size() - offset; }

    void compact() {
        if (offset == bytes.size()) {
            bytes.clear();
            offset = 0;
        } else if (offset >= 64U * 1024U) {
            bytes.erase(
                bytes.begin(),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset)
            );
            offset = 0;
        }
    }
};

struct MemoryDuplex {
    MemoryFlow aToB;
    MemoryFlow bToA;
    std::size_t capacity = 128U * 1024U;
    std::size_t maximumChunk = 8192U;
    bool aClosed = false;
    bool bClosed = false;
};

class MemoryAuthenticatedStream final
    : public RuntimeInternetAuthenticatedTcpStream {
public:
    MemoryAuthenticatedStream(
        std::shared_ptr<MemoryDuplex> duplex,
        bool sideA,
        RuntimeInternetAuthenticatedTcpStreamIdentity identity
    )
        : duplex_(std::move(duplex)),
          sideA_(sideA),
          identity_(std::move(identity)) {}

    ~MemoryAuthenticatedStream() override { close(); }

    const RuntimeInternetAuthenticatedTcpStreamIdentity& identity()
        const override {
        return identity_;
    }

    RuntimeInternetTcpStreamIoResult read(
        std::span<std::uint8_t> destination
    ) override {
        if (isClosed()) {
            return {RuntimeInternetTcpStreamIoStatus::Closed, 0, {}};
        }
        MemoryFlow& incoming = sideA_ ? duplex_->bToA : duplex_->aToB;
        if (incoming.available() == 0) {
            return peerClosed()
                ? RuntimeInternetTcpStreamIoResult{
                    RuntimeInternetTcpStreamIoStatus::Closed,
                    0,
                    {}
                }
                : RuntimeInternetTcpStreamIoResult{
                    RuntimeInternetTcpStreamIoStatus::WouldBlock,
                    0,
                    {}
                };
        }
        const std::size_t copied = std::min({
            destination.size(),
            incoming.available(),
            duplex_->maximumChunk
        });
        std::copy_n(
            incoming.bytes.begin()
                + static_cast<std::ptrdiff_t>(incoming.offset),
            copied,
            destination.begin()
        );
        incoming.offset += copied;
        incoming.compact();
        return {
            RuntimeInternetTcpStreamIoStatus::Progress,
            copied,
            {}
        };
    }

    RuntimeInternetTcpStreamIoResult write(
        std::span<const std::uint8_t> source
    ) override {
        if (isClosed() || peerClosed()) {
            return {RuntimeInternetTcpStreamIoStatus::Closed, 0, {}};
        }
        MemoryFlow& outgoing = sideA_ ? duplex_->aToB : duplex_->bToA;
        const std::size_t available = outgoing.available();
        if (available >= duplex_->capacity) {
            return {
                RuntimeInternetTcpStreamIoStatus::WouldBlock,
                0,
                {}
            };
        }
        const std::size_t copied = std::min({
            source.size(),
            duplex_->capacity - available,
            duplex_->maximumChunk
        });
        outgoing.bytes.insert(
            outgoing.bytes.end(),
            source.begin(),
            source.begin() + static_cast<std::ptrdiff_t>(copied)
        );
        return {
            RuntimeInternetTcpStreamIoStatus::Progress,
            copied,
            {}
        };
    }

    void close() noexcept override {
        if (!duplex_) return;
        if (sideA_) duplex_->aClosed = true;
        else duplex_->bClosed = true;
    }

private:
    bool isClosed() const {
        return sideA_ ? duplex_->aClosed : duplex_->bClosed;
    }

    bool peerClosed() const {
        return sideA_ ? duplex_->bClosed : duplex_->aClosed;
    }

    std::shared_ptr<MemoryDuplex> duplex_;
    bool sideA_ = false;
    RuntimeInternetAuthenticatedTcpStreamIdentity identity_;
};

struct MemoryStreamPair {
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> endpoint;
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> service;
};

MemoryStreamPair makeMemoryStreamPair(
    RuntimeInternetAuthenticatedTcpStreamIdentity identity,
    std::size_t capacity = 128U * 1024U,
    std::size_t maximumChunk = 8192U
) {
    auto duplex = std::make_shared<MemoryDuplex>();
    duplex->capacity = capacity;
    duplex->maximumChunk = maximumChunk;
    MemoryStreamPair pair;
    pair.endpoint = std::make_unique<MemoryAuthenticatedStream>(
        duplex,
        true,
        identity
    );
    pair.service = std::make_unique<MemoryAuthenticatedStream>(
        std::move(duplex),
        false,
        std::move(identity)
    );
    return pair;
}

class BlockedAuthenticatedStream final
    : public RuntimeInternetAuthenticatedTcpStream {
public:
    explicit BlockedAuthenticatedStream(
        RuntimeInternetAuthenticatedTcpStreamIdentity identity
    ) : identity_(std::move(identity)) {}

    const RuntimeInternetAuthenticatedTcpStreamIdentity& identity()
        const override {
        return identity_;
    }

    RuntimeInternetTcpStreamIoResult read(
        std::span<std::uint8_t>
    ) override {
        return closed_
            ? RuntimeInternetTcpStreamIoResult{
                RuntimeInternetTcpStreamIoStatus::Closed,
                0,
                {}
            }
            : RuntimeInternetTcpStreamIoResult{
                RuntimeInternetTcpStreamIoStatus::WouldBlock,
                0,
                {}
            };
    }

    RuntimeInternetTcpStreamIoResult write(
        std::span<const std::uint8_t>
    ) override {
        return closed_
            ? RuntimeInternetTcpStreamIoResult{
                RuntimeInternetTcpStreamIoStatus::Closed,
                0,
                {}
            }
            : RuntimeInternetTcpStreamIoResult{
                RuntimeInternetTcpStreamIoStatus::WouldBlock,
                0,
                {}
            };
    }

    void close() noexcept override { closed_ = true; }

private:
    RuntimeInternetAuthenticatedTcpStreamIdentity identity_;
    bool closed_ = false;
};

RuntimeInternetTcpRouteDescriptor routeFor(
    std::uint64_t connectionId,
    const std::string& localPeerId,
    RuntimeInternetRole localRole,
    const std::string& remotePeerId,
    RuntimeInternetRole remoteRole
) {
    RuntimeInternetTcpRouteDescriptor route;
    route.connectionId = connectionId;
    route.serviceId = "relay-service";
    route.roomId = "tunnel-room";
    route.localPeerId = localPeerId;
    route.localRole = localRole;
    route.remotePeerId = remotePeerId;
    route.remoteRole = remoteRole;
    route.endpoint.address = "relay.invalid";
    route.endpoint.port = 443;
    return route;
}

RuntimeInternetAuthenticatedTcpStreamIdentity identityFor(
    const RuntimeInternetTcpRouteDescriptor& route
) {
    RuntimeInternetAuthenticatedTcpStreamIdentity identity;
    identity.authenticatedServiceId = route.serviceId;
    identity.authenticatedPeerId = route.localPeerId;
    identity.route = route;
    return identity;
}

RuntimeMultiplayerSessionConfig sessionConfig() {
    RuntimeMultiplayerSessionConfig config;
    config.roomId = "tunnel-room";
    config.hostPeerId = "host-peer";
    config.hostDisplayName = "Tunnel Host";
    config.maximumMembers = 4;
    config.compatibility.protocolFingerprint = 0x0102030405060708ULL;
    config.compatibility.contentFingerprint = 0x8877665544332211ULL;
    return config;
}

RuntimeLanRoomAdvertisement roomFor(
    std::uint8_t playerCount = 1
) {
    RuntimeLanRoomAdvertisement room;
    room.roomId = "tunnel-room";
    room.roomName = "Internet Tunnel Room";
    room.hostName = "Tunnel Host";
    room.hostAddress = "coordinator.invalid";
    room.contentFingerprint = "8877665544332211";
    room.protocolVersion = runtimeLanProtocolVersion;
    room.sessionPort = 443;
    room.playerCount = playerCount;
    room.maximumPlayers = 4;
    return room;
}

struct SnapshotProbe {
    int stages = 0;
    int commits = 0;
    std::vector<std::uint8_t> staged;
};

RuntimeMultiplayerLanSnapshotHooks snapshotHooks(SnapshotProbe& probe) {
    RuntimeMultiplayerLanSnapshotHooks hooks;
    hooks.stageVerifiedBytes = [&probe](
        const RuntimeMultiplayerSnapshotDescriptor& descriptor,
        std::span<const std::uint8_t> bytes,
        std::uint64_t& observedHash,
        std::string&
    ) {
        ++probe.stages;
        probe.staged.assign(bytes.begin(), bytes.end());
        observedHash = descriptor.identity.authoritativeStateHash;
        return true;
    };
    hooks.commitStaged = [&probe](
        const RuntimeMultiplayerSnapshotDescriptor& descriptor,
        std::uint64_t& observedHash,
        std::string& errorMessage
    ) {
        ++probe.commits;
        if (probe.staged.empty()) {
            errorMessage = "snapshot fixture has no staged bytes";
            return false;
        }
        observedHash = descriptor.identity.authoritativeStateHash;
        return true;
    };
    hooks.postSessionCommit = [](
        const RuntimeMultiplayerSnapshotDescriptor&,
        const RuntimeMultiplayerSessionState&,
        std::string&
    ) { return true; };
    hooks.discardStaged = [&probe](
        const RuntimeMultiplayerSnapshotDescriptor&
    ) { probe.staged.clear(); };
    return hooks;
}

RuntimeMultiplayerMatchSetup matchSetupFor(
    const RuntimeMultiplayerSessionState& session
) {
    RuntimeMultiplayerMatchSetup setup;
    setup.revision = 1;
    setup.hostPeerId = session.config().hostPeerId;
    setup.map.canonicalId = "maps/official/tunnel.map";
    setup.map.contentFingerprint =
        session.config().compatibility.contentFingerprint;
    setup.rules.gameMode = "battle";
    setup.rules.rulesOverride = "mpbattlemd.ini";
    setup.rules.randomSeed = 0x3141592653589793ULL;
    setup.rules.fixedTickHz = 30;
    setup.rules.startingCredits = 10000;
    setup.rules.startingUnits = 10;
    setup.rules.techLevel = 10;
    setup.rules.gameSpeed = 2;

    std::uint8_t slotIndex = 0;
    for (const RuntimeMultiplayerRoomMember& member : session.members()) {
        RuntimeMultiplayerMatchMember setupMember;
        setupMember.peerId = member.peerId;
        setupMember.displayName = member.displayName;
        setupMember.identity = member.participation
                == RuntimeMultiplayerParticipationRole::Spectator
            ? RuntimeMultiplayerMatchMemberIdentity::Spectator
            : RuntimeMultiplayerMatchMemberIdentity::Combatant;
        setup.members.push_back(std::move(setupMember));
        if (member.participation
            == RuntimeMultiplayerParticipationRole::Spectator) {
            continue;
        }
        RuntimeMultiplayerMatchSlot slot;
        slot.slotIndex = slotIndex;
        slot.controller = RuntimeMultiplayerMatchSlotController::Human;
        slot.peerId = member.peerId;
        slot.countryId = "random";
        slot.colorId = "color" + std::to_string(slotIndex);
        slot.teamId = "-";
        slot.difficultyId = "medium";
        slot.startPosition = static_cast<std::uint8_t>(slotIndex + 1U);
        setup.slots.push_back(std::move(slot));
        ++slotIndex;
    }
    std::sort(
        setup.members.begin(),
        setup.members.end(),
        [](const RuntimeMultiplayerMatchMember& left,
           const RuntimeMultiplayerMatchMember& right) {
            return left.peerId < right.peerId;
        }
    );
    return setup;
}

RuntimeMultiplayerSaveLoadProposal snapshotProposal(
    const RuntimeMultiplayerSessionState& session,
    std::span<const std::uint8_t> bytes,
    SmokeContext& context
) {
    std::uint64_t setupFingerprint = 0;
    std::string error;
    context.expect(
        session.matchSetup().has_value()
            && runtimeMultiplayerGameplaySetupFingerprint(
                *session.matchSetup(),
                setupFingerprint,
                error
            ),
        "tunnel snapshot setup fingerprint should compute: " + error
    );
    RuntimeMultiplayerSnapshotIdentity identity;
    identity.proposalId = 1;
    identity.kind = RuntimeMultiplayerSnapshotKind::Save;
    identity.sourceTimelineEpoch = session.timelineEpoch();
    identity.barrierTick = session.authoritativeTick() - 1;
    identity.snapshotTick = session.authoritativeTick() - 1;
    identity.snapshotToken = "large-tunnel-save";
    identity.authoritativeStateHash = 0x1234fedc5678ba90ULL;
    identity.gameplaySetupFingerprint = setupFingerprint;
    RuntimeMultiplayerSnapshotDescriptor descriptor;
    error.clear();
    context.expect(
        makeRuntimeMultiplayerSnapshotDescriptor(
            identity,
            bytes,
            descriptor,
            error
        ),
        "large tunnel snapshot descriptor should compute: " + error
    );
    RuntimeMultiplayerSaveLoadProposal proposal;
    proposal.timelineEpoch = session.timelineEpoch();
    proposal.proposalId = identity.proposalId;
    proposal.kind = RuntimeMultiplayerSaveLoadKind::Save;
    proposal.proposerPeerId = session.config().hostPeerId;
    proposal.descriptor = std::move(descriptor);
    proposal.tick = identity.snapshotTick;
    proposal.snapshotId = identity.snapshotToken;
    proposal.snapshotFingerprint = proposal.descriptor.snapshotFingerprint;
    return proposal;
}

struct TunnelLink {
    RuntimeInternetTcpTunnelProxy hostProxy;
    RuntimeInternetTcpTunnelProxy remoteProxy;
    RuntimeInternetTcpRelayPair relay;
    RuntimeInternetTcpTunnelClientEndpoint endpoint;
    RuntimeInternetTcpTunnelPollResult lastHostPoll;
    RuntimeInternetTcpTunnelPollResult lastRemotePoll;
    RuntimeInternetTcpRelayPairPollResult lastRelayPoll;
    std::uint64_t totalEndpointBytes = 0;
    std::uint64_t totalRelayBytes = 0;

    bool poll(std::string& errorMessage) {
        const std::uint64_t now = nowMilliseconds();
        lastHostPoll = hostProxy.poll(now);
        if (!lastHostPoll.succeeded) {
            errorMessage = "host proxy: " + lastHostPoll.error;
            return false;
        }
        totalEndpointBytes += lastHostPoll.bytesFromLocal
            + lastHostPoll.bytesFromTunnel;
        lastRelayPoll = relay.poll(now);
        if (!lastRelayPoll.succeeded) {
            errorMessage = "relay: " + lastRelayPoll.error;
            return false;
        }
        totalRelayBytes += lastRelayPoll.bytesHostToRemote
            + lastRelayPoll.bytesRemoteToHost;
        lastRemotePoll = remoteProxy.poll(now);
        if (!lastRemotePoll.succeeded) {
            errorMessage = "remote proxy: " + lastRemotePoll.error;
            return false;
        }
        totalEndpointBytes += lastRemotePoll.bytesFromLocal
            + lastRemotePoll.bytesFromTunnel;
        return true;
    }
};

std::unique_ptr<TunnelLink> startTunnelLink(
    RuntimeMultiplayerLanSession& host,
    RuntimeInternetRole remoteRole,
    const std::string& remotePeerId,
    std::uint64_t connectionId,
    RuntimeLanRoomAdvertisement room,
    SmokeContext& context
) {
    auto link = std::make_unique<TunnelLink>();
    const RuntimeInternetTcpRouteDescriptor hostRoute = routeFor(
        connectionId,
        "host-peer",
        RuntimeInternetRole::Host,
        remotePeerId,
        remoteRole
    );
    const RuntimeInternetTcpRouteDescriptor remoteRoute = routeFor(
        connectionId,
        remotePeerId,
        remoteRole,
        "host-peer",
        RuntimeInternetRole::Host
    );
    MemoryStreamPair hostStreams = makeMemoryStreamPair(
        identityFor(hostRoute)
    );
    MemoryStreamPair remoteStreams = makeMemoryStreamPair(
        identityFor(remoteRoute)
    );
    RuntimeInternetTcpTunnelProxyConfig config;
    config.connectTimeoutMilliseconds = 3000;
    config.idleTimeoutMilliseconds = 15000;
    std::string error;
    context.expect(
        link->relay.open(
            config,
            std::move(hostStreams.service),
            std::move(remoteStreams.service),
            nowMilliseconds(),
            error
        ),
        "authenticated relay pair should open: " + error
    );
    error.clear();
    context.expect(
        link->hostProxy.startHost(
            config,
            hostRoute,
            std::move(hostStreams.endpoint),
            host.sessionPort(),
            nowMilliseconds(),
            error
        ),
        "host endpoint proxy should connect privately: " + error
    );
    error.clear();
    context.expect(
        link->remoteProxy.startClient(
            config,
            remoteRoute,
            std::move(remoteStreams.endpoint),
            std::move(room),
            nowMilliseconds(),
            error
        ),
        "remote endpoint proxy should listen privately: " + error
    );
    link->endpoint = link->remoteProxy.clientEndpoint();
    return link;
}

bool pumpUntil(
    RuntimeMultiplayerLanSession& host,
    const std::vector<RuntimeMultiplayerLanSession*>& remotes,
    const std::vector<TunnelLink*>& links,
    const std::function<bool()>& complete,
    SmokeContext& context,
    const std::string& description,
    int timeoutMilliseconds = 10000
) {
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMilliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const RuntimeMultiplayerLanPollResult hostPoll = host.poll(0);
        if (!hostPoll.succeeded) {
            context.expect(
                false,
                description + ": host LAN poll failed: " + hostPoll.error
            );
            return false;
        }
        for (RuntimeMultiplayerLanSession* remote : remotes) {
            const RuntimeMultiplayerLanPollResult remotePoll = remote->poll(0);
            if (!remotePoll.succeeded) {
                context.expect(
                    false,
                    description + ": remote LAN poll failed: "
                        + remotePoll.error
                );
                return false;
            }
        }
        for (TunnelLink* link : links) {
            std::string tunnelError;
            if (!link->poll(tunnelError)) {
                context.expect(
                    false,
                    description + ": " + tunnelError
                );
                return false;
            }
        }
        if (complete()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(false, description + ": timed out");
    return false;
}

void testRouteBindingAndDuplicateRejection(SmokeContext& context) {
    RuntimeInternetTcpTunnelProxyConfig config;
    const RuntimeInternetTcpRouteDescriptor hostRoute = routeFor(
        100,
        "host-peer",
        RuntimeInternetRole::Host,
        "client-peer",
        RuntimeInternetRole::Client
    );
    const RuntimeInternetTcpRouteDescriptor clientRoute = routeFor(
        100,
        "client-peer",
        RuntimeInternetRole::Client,
        "host-peer",
        RuntimeInternetRole::Host
    );

    RuntimeInternetAuthenticatedTcpStreamIdentity forgedHostIdentity =
        identityFor(hostRoute);
    forgedHostIdentity.authenticatedPeerId = "forged-host";
    MemoryStreamPair forgedHost = makeMemoryStreamPair(
        std::move(forgedHostIdentity)
    );
    MemoryStreamPair validClient = makeMemoryStreamPair(
        identityFor(clientRoute)
    );
    RuntimeInternetTcpRelayPair forgedPair;
    std::string error;
    context.expect(
        !forgedPair.open(
            config,
            std::move(forgedHost.service),
            std::move(validClient.service),
            nowMilliseconds(),
            error
        ),
        "relay should reject a forged authenticated principal"
    );

    MemoryStreamPair firstHost = makeMemoryStreamPair(identityFor(hostRoute));
    MemoryStreamPair firstClient = makeMemoryStreamPair(
        identityFor(clientRoute)
    );
    RuntimeInternetTcpRelayPair first;
    error.clear();
    context.expect(
        first.open(
            config,
            std::move(firstHost.service),
            std::move(firstClient.service),
            nowMilliseconds(),
            error
        ),
        "first authenticated route binding should open: " + error
    );

    MemoryStreamPair replayHost = makeMemoryStreamPair(identityFor(hostRoute));
    MemoryStreamPair replayClient = makeMemoryStreamPair(
        identityFor(clientRoute)
    );
    RuntimeInternetTcpRelayPair replay;
    error.clear();
    context.expect(
        !replay.open(
            config,
            std::move(replayHost.service),
            std::move(replayClient.service),
            nowMilliseconds(),
            error
        ),
        "simultaneous replay of an active route should be rejected"
    );
    first.close();

    const RuntimeInternetTcpRouteDescriptor wrongRoomRoute = [&]() {
        RuntimeInternetTcpRouteDescriptor route = clientRoute;
        route.roomId = "other-room";
        return route;
    }();
    MemoryStreamPair matchingHost = makeMemoryStreamPair(
        identityFor(hostRoute)
    );
    MemoryStreamPair wrongRoom = makeMemoryStreamPair(
        identityFor(wrongRoomRoute)
    );
    RuntimeInternetTcpRelayPair mismatch;
    error.clear();
    context.expect(
        !mismatch.open(
            config,
            std::move(matchingHost.service),
            std::move(wrongRoom.service),
            nowMilliseconds(),
            error
        ),
        "relay should reject a cross-room route pair"
    );
}

void testOversizeAndBackpressure(SmokeContext& context) {
    RuntimeInternetTcpTunnelProxyConfig invalidConfig;
    invalidConfig.maximumQueuedBytes =
        runtimeInternetTcpTunnelMaximumQueuedBytes + 1U;
    const RuntimeInternetTcpRouteDescriptor clientRoute = routeFor(
        200,
        "client-peer",
        RuntimeInternetRole::Client,
        "host-peer",
        RuntimeInternetRole::Host
    );
    RuntimeInternetTcpTunnelProxy invalidProxy;
    std::string error;
    context.expect(
        !invalidProxy.startClient(
            invalidConfig,
            clientRoute,
            std::make_unique<BlockedAuthenticatedStream>(
                identityFor(clientRoute)
            ),
            roomFor(),
            nowMilliseconds(),
            error
        ),
        "proxy should reject queue limits above the production cap"
    );

    RuntimeInternetTcpTunnelProxyConfig backpressureConfig;
    backpressureConfig.maximumQueuedBytes =
        runtimeLanMaximumPacketBytes + 12U;
    backpressureConfig.connectTimeoutMilliseconds = 2000;
    backpressureConfig.idleTimeoutMilliseconds = 10000;
    RuntimeInternetTcpTunnelProxy backpressureProxy;
    error.clear();
    context.expect(
        backpressureProxy.startClient(
            backpressureConfig,
            clientRoute,
            std::make_unique<BlockedAuthenticatedStream>(
                identityFor(clientRoute)
            ),
            roomFor(),
            nowMilliseconds(),
            error
        ),
        "backpressure proxy should start: " + error
    );
    const RuntimeInternetTcpTunnelClientEndpoint endpoint =
        backpressureProxy.clientEndpoint();
    RuntimeTcpClient localClient;
    error.clear();
    context.expect(
        localClient.connect(
            endpoint.room.hostAddress,
            endpoint.room.sessionPort,
            2000,
            endpoint.localProxyPrelude,
            error
        ),
        "local framed client should connect to proxy: " + error
    );
    RuntimeNetworkPacket maximumPacket;
    maximumPacket.type = 0x7f01;
    maximumPacket.payload.resize(runtimeLanMaximumPacketBytes, 0x5aU);
    error.clear();
    context.expect(
        localClient.send(maximumPacket, error),
        "maximum legal LAN frame should queue locally: " + error
    );
    bool observedBackpressure = false;
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline
        && !observedBackpressure) {
        error.clear();
        const std::vector<RuntimeTcpEvent> events = localClient.poll(0, error);
        context.expect(error.empty(), "local backpressure poll should succeed");
        context.expect(
            std::none_of(
                events.begin(),
                events.end(),
                [](const RuntimeTcpEvent& event) {
                    return event.kind == RuntimeTcpEventKind::Error;
                }
            ),
            "local backpressure transport should not frame-error"
        );
        const RuntimeInternetTcpTunnelPollResult polled =
            backpressureProxy.poll(nowMilliseconds());
        context.expect(
            polled.succeeded,
            "legal full queue should backpressure, not close: " + polled.error
        );
        if (!polled.succeeded) break;
        observedBackpressure = polled.backpressured;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(
        observedBackpressure && backpressureProxy.isOpen(),
        "proxy should preserve a maximum frame under downstream backpressure"
    );

    const RuntimeInternetTcpRouteDescriptor hostRoute = routeFor(
        201,
        "host-peer",
        RuntimeInternetRole::Host,
        "client-peer",
        RuntimeInternetRole::Client
    );
    const RuntimeInternetTcpRouteDescriptor remoteRoute = routeFor(
        201,
        "client-peer",
        RuntimeInternetRole::Client,
        "host-peer",
        RuntimeInternetRole::Host
    );
    MemoryStreamPair hostStreams = makeMemoryStreamPair(identityFor(hostRoute));
    MemoryStreamPair remoteStreams = makeMemoryStreamPair(
        identityFor(remoteRoute)
    );
    RuntimeInternetAuthenticatedTcpStream* remoteEndpoint =
        remoteStreams.endpoint.get();
    RuntimeInternetTcpRelayPair relay;
    RuntimeInternetTcpTunnelProxyConfig relayConfig;
    error.clear();
    context.expect(
        relay.open(
            relayConfig,
            std::move(hostStreams.service),
            std::move(remoteStreams.service),
            nowMilliseconds(),
            error
        ),
        "oversize relay fixture should open: " + error
    );
    RuntimeNetworkPacket headerTemplate;
    headerTemplate.type = 0x7f02;
    std::vector<std::uint8_t> malformed;
    error.clear();
    context.expect(
        encodeRuntimeNetworkPacket(headerTemplate, malformed, error),
        "oversize header template should encode: " + error
    );
    const std::uint32_t oversized = static_cast<std::uint32_t>(
        runtimeLanMaximumPacketBytes + 1U
    );
    malformed[8] = static_cast<std::uint8_t>(oversized >> 24U);
    malformed[9] = static_cast<std::uint8_t>(oversized >> 16U);
    malformed[10] = static_cast<std::uint8_t>(oversized >> 8U);
    malformed[11] = static_cast<std::uint8_t>(oversized);
    const RuntimeInternetTcpStreamIoResult injected = remoteEndpoint->write(
        malformed
    );
    context.expect(
        injected.status == RuntimeInternetTcpStreamIoStatus::Progress
            && injected.bytesTransferred == malformed.size(),
        "malformed oversized header should reach relay decoder"
    );
    const RuntimeInternetTcpRelayPairPollResult rejected = relay.poll(
        nowMilliseconds()
    );
    context.expect(
        !rejected.succeeded && rejected.closed,
        "relay should close an authenticated stream with an oversized frame"
    );
}

void testTimeoutCleanup(SmokeContext& context) {
    RuntimeInternetTcpTunnelProxyConfig proxyConfig;
    proxyConfig.connectTimeoutMilliseconds = 10;
    proxyConfig.idleTimeoutMilliseconds = 100;
    const RuntimeInternetTcpRouteDescriptor clientRoute = routeFor(
        250,
        "timeout-client",
        RuntimeInternetRole::Client,
        "host-peer",
        RuntimeInternetRole::Host
    );
    RuntimeInternetTcpTunnelProxy clientProxy;
    std::string error;
    context.expect(
        clientProxy.startClient(
            proxyConfig,
            clientRoute,
            std::make_unique<BlockedAuthenticatedStream>(
                identityFor(clientRoute)
            ),
            roomFor(),
            100,
            error
        ),
        "client timeout fixture should start: " + error
    );
    const RuntimeInternetTcpTunnelPollResult clientTimeout =
        clientProxy.poll(111);
    context.expect(
        !clientTimeout.succeeded && clientTimeout.closed
            && !clientProxy.isOpen(),
        "unused one-shot client listener should close at connect timeout"
    );

    RuntimeInternetTcpTunnelProxyConfig relayConfig;
    relayConfig.connectTimeoutMilliseconds = 100;
    relayConfig.idleTimeoutMilliseconds = 10;
    const RuntimeInternetTcpRouteDescriptor hostRoute = routeFor(
        251,
        "host-peer",
        RuntimeInternetRole::Host,
        "timeout-client",
        RuntimeInternetRole::Client
    );
    const RuntimeInternetTcpRouteDescriptor remoteRoute = routeFor(
        251,
        "timeout-client",
        RuntimeInternetRole::Client,
        "host-peer",
        RuntimeInternetRole::Host
    );
    MemoryStreamPair hostStreams = makeMemoryStreamPair(identityFor(hostRoute));
    MemoryStreamPair remoteStreams = makeMemoryStreamPair(
        identityFor(remoteRoute)
    );
    RuntimeInternetTcpRelayPair relay;
    error.clear();
    context.expect(
        relay.open(
            relayConfig,
            std::move(hostStreams.service),
            std::move(remoteStreams.service),
            100,
            error
        ),
        "relay timeout fixture should bind: " + error
    );
    const RuntimeInternetTcpRelayPairPollResult relayTimeout = relay.poll(110);
    context.expect(
        !relayTimeout.succeeded && relayTimeout.closed && !relay.isOpen(),
        "idle relay should close and release its active binding"
    );

    MemoryStreamPair replacementHost = makeMemoryStreamPair(
        identityFor(hostRoute)
    );
    MemoryStreamPair replacementRemote = makeMemoryStreamPair(
        identityFor(remoteRoute)
    );
    RuntimeInternetTcpRelayPair replacement;
    error.clear();
    context.expect(
        replacement.open(
            relayConfig,
            std::move(replacementHost.service),
            std::move(replacementRemote.service),
            111,
            error
        ),
        "timed-out relay binding should be cleaned up: " + error
    );

    RuntimeInternetTcpTunnelProxyConfig idleDisabledConfig = relayConfig;
    idleDisabledConfig.idleTimeoutMilliseconds = 0U;
    const RuntimeInternetTcpRouteDescriptor persistentHostRoute = routeFor(
        252,
        "host-peer",
        RuntimeInternetRole::Host,
        "persistent-client",
        RuntimeInternetRole::Client
    );
    const RuntimeInternetTcpRouteDescriptor persistentRemoteRoute = routeFor(
        252,
        "persistent-client",
        RuntimeInternetRole::Client,
        "host-peer",
        RuntimeInternetRole::Host
    );
    MemoryStreamPair persistentHost = makeMemoryStreamPair(
        identityFor(persistentHostRoute)
    );
    MemoryStreamPair persistentRemote = makeMemoryStreamPair(
        identityFor(persistentRemoteRoute)
    );
    RuntimeInternetTcpRelayPair persistent;
    error.clear();
    context.expect(
        persistent.open(
            idleDisabledConfig,
            std::move(persistentHost.service),
            std::move(persistentRemote.service),
            100U,
            error
        ),
        "zero-idle relay fixture should bind: " + error
    );
    const RuntimeInternetTcpRelayPairPollResult stillOpen = persistent.poll(
        24ULL * 60ULL * 60ULL * 1000ULL
    );
    context.expect(
        stillOpen.succeeded && !stillOpen.closed && persistent.isOpen(),
        "zero idle timeout should preserve an inactive authenticated relay"
    );
}

void testAdmissionForgery(SmokeContext& context) {
    RuntimeMultiplayerLanSession host;
    RuntimeMultiplayerLanSession forgedClient;
    RuntimeMultiplayerLanHostOptions hostOptions;
    hostOptions.session = sessionConfig();
    hostOptions.roomName = "Internet Tunnel Room";
    hostOptions.discoveryPort = 0;
    hostOptions.sessionPort = 0;
    hostOptions.enableDiscovery = false;
    hostOptions.sessionBindScope = RuntimeTcpBindScope::LoopbackIpv4;
    std::string error;
    context.expect(
        host.startHost(hostOptions, error),
        "forged admission host should start privately: " + error
    );
    std::unique_ptr<TunnelLink> link = startTunnelLink(
        host,
        RuntimeInternetRole::Client,
        "authenticated-client",
        300,
        roomFor(),
        context
    );
    RuntimeMultiplayerLanJoinOptions join;
    join.room = link->endpoint.room;
    join.peerId = "forged-client";
    join.displayName = "Forged Client";
    join.compatibility = hostOptions.session.compatibility;
    join.localProxyPrelude = link->endpoint.localProxyPrelude;
    error.clear();
    context.expect(
        forgedClient.join(join, error),
        "forged client should reach the local proxy: " + error
    );

    bool admissionRejected = false;
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline
        && !admissionRejected) {
        (void)host.poll(0);
        (void)forgedClient.poll(0);
        const RuntimeInternetTcpTunnelPollResult hostPoll =
            link->hostProxy.poll(nowMilliseconds());
        if (!hostPoll.succeeded) {
            admissionRejected = hostPoll.error.find("peer")
                != std::string::npos;
            break;
        }
        const RuntimeInternetTcpRelayPairPollResult relayPoll =
            link->relay.poll(nowMilliseconds());
        if (!relayPoll.succeeded) break;
        const RuntimeInternetTcpTunnelPollResult remotePoll =
            link->remoteProxy.poll(nowMilliseconds());
        if (!remotePoll.succeeded) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(
        admissionRejected,
        "host proxy should reject a JoinAdmissionRequest forged behind TLS"
    );
    context.expect(
        host.session().members().size() == 1U,
        "forged authenticated peer must never enter the authoritative room"
    );

    RuntimeMultiplayerLanSession forgedRoleClient;
    std::unique_ptr<TunnelLink> roleLink = startTunnelLink(
        host,
        RuntimeInternetRole::Client,
        "role-client",
        301,
        roomFor(),
        context
    );
    RuntimeMultiplayerLanJoinOptions roleJoin;
    roleJoin.room = roleLink->endpoint.room;
    roleJoin.peerId = "role-client";
    roleJoin.displayName = "Forged Spectator Role";
    roleJoin.participation =
        RuntimeMultiplayerParticipationRole::Spectator;
    roleJoin.compatibility = hostOptions.session.compatibility;
    roleJoin.localProxyPrelude = roleLink->endpoint.localProxyPrelude;
    error.clear();
    context.expect(
        forgedRoleClient.join(roleJoin, error),
        "role-forgery client should reach its local proxy: " + error
    );
    bool roleRejected = false;
    const auto roleDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < roleDeadline
        && !roleRejected) {
        (void)host.poll(0);
        (void)forgedRoleClient.poll(0);
        const RuntimeInternetTcpTunnelPollResult hostPoll =
            roleLink->hostProxy.poll(nowMilliseconds());
        if (!hostPoll.succeeded) {
            roleRejected = hostPoll.error.find("role")
                != std::string::npos;
            break;
        }
        const RuntimeInternetTcpRelayPairPollResult relayPoll =
            roleLink->relay.poll(nowMilliseconds());
        if (!relayPoll.succeeded) break;
        const RuntimeInternetTcpTunnelPollResult remotePoll =
            roleLink->remoteProxy.poll(nowMilliseconds());
        if (!remotePoll.succeeded) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(
        roleRejected,
        "host proxy should reject participation that contradicts TLS role"
    );
    context.expect(
        host.session().members().size() == 1U,
        "forged participation role must never enter the authoritative room"
    );
}

void testRealLanSessionAndLargeSnapshot(SmokeContext& context) {
    RuntimeMultiplayerLanSession host;
    RuntimeMultiplayerLanSession client;
    RuntimeMultiplayerLanSession spectator;
    SnapshotProbe hostSnapshots;
    SnapshotProbe clientSnapshots;
    SnapshotProbe spectatorSnapshots;
    std::string error;
    context.expect(
        host.setSnapshotHooks(snapshotHooks(hostSnapshots), error),
        "host snapshot hooks should configure: " + error
    );
    error.clear();
    context.expect(
        client.setSnapshotHooks(snapshotHooks(clientSnapshots), error),
        "client snapshot hooks should configure: " + error
    );
    error.clear();
    context.expect(
        spectator.setSnapshotHooks(snapshotHooks(spectatorSnapshots), error),
        "spectator snapshot hooks should configure: " + error
    );

    RuntimeMultiplayerLanHostOptions hostOptions;
    hostOptions.session = sessionConfig();
    hostOptions.roomName = "Internet Tunnel Room";
    hostOptions.discoveryPort = 0;
    hostOptions.sessionPort = 0;
    hostOptions.enableDiscovery = false;
    hostOptions.sessionBindScope = RuntimeTcpBindScope::LoopbackIpv4;
    hostOptions.roomGeneration = 0x123456789abcdef0ULL;
    error.clear();
    context.expect(
        host.startHost(hostOptions, error),
        "Internet host should expose a private loopback session: " + error
    );

    std::unique_ptr<TunnelLink> clientLink = startTunnelLink(
        host,
        RuntimeInternetRole::Client,
        "client-peer",
        400,
        roomFor(),
        context
    );
    context.expect(
        host.session().members().size() == 1U,
        "a bound TCP tunnel alone must not admit a gameplay member"
    );
    RuntimeMultiplayerLanJoinOptions clientJoin;
    clientJoin.room = clientLink->endpoint.room;
    clientJoin.peerId = "client-peer";
    clientJoin.displayName = "Tunnel Client";
    clientJoin.compatibility = hostOptions.session.compatibility;
    clientJoin.localProxyPrelude = clientLink->endpoint.localProxyPrelude;
    error.clear();
    context.expect(
        client.join(clientJoin, error),
        "client should connect through one-shot loopback proxy: " + error
    );
    context.expect(
        client.phase() == RuntimeMultiplayerLanPhase::Joining
            && host.session().members().size() == 1U,
        "TcpTunnelBound must remain Joining until the real JoinAccepted"
    );
    if (!pumpUntil(
            host,
            {&client},
            {clientLink.get()},
            [&]() {
                return host.session().members().size() == 2U
                    && client.phase() == RuntimeMultiplayerLanPhase::Lobby
                    && clientLink->lastHostPoll.remoteAdmissionVerified;
            },
            context,
            "authenticated client admission"
        )) {
        return;
    }

    RuntimeTcpClient replay;
    error.clear();
    context.expect(
        !replay.connect(
            clientLink->endpoint.room.hostAddress,
            clientLink->endpoint.room.sessionPort,
            100,
            clientLink->endpoint.localProxyPrelude,
            error
        ),
        "used local proxy prelude/listener should not be replayable"
    );

    std::unique_ptr<TunnelLink> spectatorLink = startTunnelLink(
        host,
        RuntimeInternetRole::Spectator,
        "spectator-peer",
        401,
        roomFor(2),
        context
    );
    RuntimeMultiplayerLanJoinOptions spectatorJoin;
    spectatorJoin.room = spectatorLink->endpoint.room;
    spectatorJoin.peerId = "spectator-peer";
    spectatorJoin.displayName = "Tunnel Spectator";
    spectatorJoin.participation =
        RuntimeMultiplayerParticipationRole::Spectator;
    spectatorJoin.compatibility = hostOptions.session.compatibility;
    spectatorJoin.localProxyPrelude =
        spectatorLink->endpoint.localProxyPrelude;
    error.clear();
    context.expect(
        spectator.join(spectatorJoin, error),
        "spectator should connect through a distinct authenticated route: "
            + error
    );
    if (!pumpUntil(
            host,
            {&client, &spectator},
            {clientLink.get(), spectatorLink.get()},
            [&]() {
                return host.session().members().size() == 3U
                    && spectator.phase() == RuntimeMultiplayerLanPhase::Lobby
                    && spectatorLink->lastHostPoll.remoteAdmissionVerified;
            },
            context,
            "authenticated spectator admission"
        )) {
        return;
    }
    const auto spectatorMember = std::find_if(
        host.session().members().begin(),
        host.session().members().end(),
        [](const RuntimeMultiplayerRoomMember& member) {
            return member.peerId == "spectator-peer";
        }
    );
    context.expect(
        spectatorMember != host.session().members().end()
            && spectatorMember->participation
                == RuntimeMultiplayerParticipationRole::Spectator,
        "authenticated spectator role should survive LAN admission"
    );

    const RuntimeMultiplayerMatchSetup setup = matchSetupFor(host.session());
    const RuntimeMultiplayerLanOperationResult setupResult =
        host.setMatchSetup(setup);
    context.expect(
        setupResult.succeeded,
        "tunnel match setup should apply: " + setupResult.reason
    );
    if (!pumpUntil(
            host,
            {&client, &spectator},
            {clientLink.get(), spectatorLink.get()},
            [&]() {
                return client.session().matchSetup().has_value()
                    && spectator.session().matchSetup().has_value();
            },
            context,
            "tunnel match setup replication"
        )) {
        return;
    }
    context.expect(
        host.setReady(true).succeeded
            && client.setReady(true).succeeded
            && spectator.setReady(true).succeeded,
        "all tunnel room members should submit ready"
    );
    if (!pumpUntil(
            host,
            {&client, &spectator},
            {clientLink.get(), spectatorLink.get()},
            [&]() {
                return host.session().allMembersReady()
                    && client.session().allMembersReady()
                    && spectator.session().allMembersReady();
            },
            context,
            "tunnel ready replication"
        )) {
        return;
    }
    const RuntimeMultiplayerLanOperationResult started = host.startMatch(30);
    context.expect(
        started.succeeded,
        "tunnel room should start: " + started.reason
    );
    if (!pumpUntil(
            host,
            {&client, &spectator},
            {clientLink.get(), spectatorLink.get()},
            [&]() {
                return host.phase() == RuntimeMultiplayerLanPhase::Running
                    && client.phase() == RuntimeMultiplayerLanPhase::Running
                    && spectator.phase()
                        == RuntimeMultiplayerLanPhase::Running;
            },
            context,
            "tunnel match start"
        )) {
        return;
    }

    std::vector<std::uint8_t> snapshotBytes(
        runtimeMultiplayerSnapshotChunkBytes * 4U + 37U
    );
    for (std::size_t index = 0; index < snapshotBytes.size(); ++index) {
        snapshotBytes[index] = static_cast<std::uint8_t>(
            (index * 131U + 17U) & 0xffU
        );
    }
    RuntimeMultiplayerSaveLoadProposal proposal = snapshotProposal(
        host.session(),
        snapshotBytes,
        context
    );
    const RuntimeMultiplayerLanOperationResult proposed =
        host.proposeSaveLoad(proposal, snapshotBytes);
    context.expect(
        proposed.succeeded,
        "large snapshot should enter the tunnel: " + proposed.reason
    );
    if (!pumpUntil(
            host,
            {&client, &spectator},
            {clientLink.get(), spectatorLink.get()},
            [&]() {
                const auto& handshakes =
                    host.session().saveLoadHandshakes();
                return !handshakes.empty()
                    && handshakes.back().status
                        == RuntimeMultiplayerSaveLoadStatus::Committed;
            },
            context,
            "one-megabyte snapshot transfer and commit",
            40000
        )) {
        return;
    }
    context.expect(
        hostSnapshots.stages == 1 && hostSnapshots.commits == 1
            && clientSnapshots.stages == 1 && clientSnapshots.commits == 1
            && spectatorSnapshots.stages == 1
            && spectatorSnapshots.commits == 1,
        "host, client and spectator should each verify/commit one snapshot"
    );
    context.expect(
        hostSnapshots.staged == snapshotBytes
            && clientSnapshots.staged == snapshotBytes
            && spectatorSnapshots.staged == snapshotBytes,
        "large snapshot bytes must remain exact through both TCP splices"
    );
    context.expect(
        clientLink->totalEndpointBytes > snapshotBytes.size()
            && clientLink->totalRelayBytes > snapshotBytes.size(),
        "tunnel poll accounting should expose transferred traffic"
    );
}

} // namespace

int main() {
    SmokeContext context;
    testRouteBindingAndDuplicateRejection(context);
    testOversizeAndBackpressure(context);
    testTimeoutCleanup(context);
    testAdmissionForgery(context);
    testRealLanSessionAndLargeSnapshot(context);
    if (context.failures != 0) {
        std::cerr << context.failures
                  << " Internet TCP tunnel proxy smoke assertion(s) failed\n";
        return 1;
    }
    std::cout
        << "Internet TCP tunnel proxy smoke passed: authenticated route "
           "binding, duplicate/forgery rejection, bounded backpressure, "
           "client+spectator LAN admission and large snapshot transfer.\n";
    return 0;
}
